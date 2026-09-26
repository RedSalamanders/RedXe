#pragma once

#include "BundledPlugins.h"
#include "ControlWorkQueue.h"
#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Host.h"
#include "PlugInterfaces/Service.h"
#include "PlugInterfaces/Widget.h"
#include "Settings.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

// One process-scoped plugin runtime. It owns every mapped plugin module, every host data provider and plugin data
// source, the single local acquisition worker, the optional serial network worker, the JSONL diagnostic writer, and
// subscription drain lifetime. PluginManager instances borrow it through Instance() so that staging an adjacent
// dashboard page reuses the already-mapped modules, the already-created data sources, and the already-running workers
// instead of building a second runtime beside them.
class PluginHost final : public IRedXeHost, public IRedXeSettingsQueue
{
  public:
    struct ModuleView final
    {
        RedXeCreateFn create = nullptr;
        RedXeGetPluginSettingsContractFn getSettingsContract = nullptr;
    };

    PluginHost() = default;
    ~PluginHost();

    // The process runtime. Every PluginManager in the application borrows this instance; tests may still construct a
    // private PluginHost when they need an isolated runtime.
    [[nodiscard]] static PluginHost& Instance() noexcept;

    // Releases the process runtime. The caller MUST have destroyed every PluginManager, widget, provider, and
    // subscription first. Modules stay mapped, and optional RedXePluginShutdown runs exactly once per module here.
    static void ShutdownProcessRuntime() noexcept;

    // Idempotent teardown of this runtime's worker, providers, sources, and module bindings.
    void Shutdown() noexcept;

    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;
    PluginHost(PluginHost&&) = delete;
    PluginHost& operator=(PluginHost&&) = delete;

    static constexpr UINT kDataSnapshotInvalidateMessage = WM_APP + 3;
    // Posted once per batch of queued host actions; the UI thread drains them with DrainHostActions.
    static constexpr UINT kHostActionMessage = WM_APP + 5;

    [[nodiscard]] IRedXeHost* Interface() noexcept;
    [[nodiscard]] HRESULT GetPluginModule(const char* pluginId, uint32_t requiredCapabilities,
                                          ModuleView* module) noexcept;
    void SetUiInvalidateTarget(HWND window) noexcept;
    void AcknowledgeUiInvalidate() noexcept;

    // Interactive RedXe leaves network access enabled. `--self-test` and HostPluginTests disable it before any widget
    // is created so RunNetworkWork is never invoked and the network worker is never started.
    void SetNetworkAccessEnabled(bool enabled) noexcept;
    [[nodiscard]] bool NetworkAccessEnabled() const noexcept;
    [[nodiscard]] bool NetworkWorkerRunning() const noexcept;
    [[nodiscard]] HRESULT RegisterNetworkWidget(IRedXeNetworkWidget* widget) noexcept;
    void UnregisterNetworkWidget(IRedXeNetworkWidget* widget) noexcept;
    void SetNetworkWidgetActive(IRedXeNetworkWidget* widget, bool active) noexcept;

    using SettingsPersistHandler = HRESULT (*)(void* context, const char* instanceId, const char* settingsJsonUtf8,
                                               uint32_t settingsBytes) noexcept;
    void SetSettingsPersistHandler(SettingsPersistHandler handler, void* context) noexcept;

    // Named actions (Action.h). Application registers the handler that executes the page, widget, and redxe
    // namespaces itself; PluginHost executes the other default namespaces (HostActions) and every published one.
    // The handler runs on the UI thread from DrainHostActions or ExecuteAction; strings are bounded copies valid
    // for the call only. `completed` runs after every drained action (executed or dropped) so Application can
    // publish host state once.
    using HostActionHandler = HRESULT (*)(void* context, const char* actionUtf8, const char* targetUtf8) noexcept;
    using HostActionCompleted = void (*)(void* context) noexcept;
    void SetHostActionHandler(HostActionHandler handler, HostActionCompleted completed, void* context) noexcept;
    // UI thread: executes every queued action in submission order, outside any lock.
    void DrainHostActions() noexcept;
    [[nodiscard]] uint32_t PendingHostActionCount() const noexcept;

    // Action publishers (Action.h): the namespace registry in BundledPlugins.h resolved against the contracts of
    // mapped modules. A collision, an unregistered namespace, a registered plugin that does not publish its
    // namespace, or a publisher that cannot be loaded produces one Error log line and one bounded notice line;
    // Application shows the notices in its settings-error dialog. UI thread only.
    static constexpr size_t kMaximumActionNotices = 8;
    static constexpr size_t kActionNoticeCharacters = 256;
    // Copies every current notice line (newline separated) into text; returns the number of lines.
    [[nodiscard]] uint32_t CopyActionNotices(wchar_t* text, size_t capacity) const noexcept;
    // Increments whenever a notice is added or cleared, so callers can detect new notices cheaply.
    [[nodiscard]] uint32_t ActionNoticeGeneration() const noexcept;
    // Clears the "unavailable" mark of every publisher so a repaired deployment retries once (settings apply).
    void ResetActionPublishers() noexcept;
    // Test surface: the state of one registered namespace.
    enum class ActionPublisherState : uint8_t
    {
        Unresolved = 0,
        Ready,
        Missing,
        Unavailable,
    };
    [[nodiscard]] ActionPublisherState ActionPublisherStateOf(const char* actionNamespace) const noexcept;

    // Headless services (Service.h). All calls run on the UI thread. StartServices creates and starts every
    // service the document configures; ApplyServiceSettings starts, stops, or re-applies services after a live
    // reload; PublishHostState fans one state record out to started services; StopServices signals every device
    // lane and waits at most kRedXeDeviceWorkerDrainMilliseconds per lane. A late lane retains its service and
    // host runtime until it returns; Stop runs only after that return. Interactive RedXe leaves
    // device access enabled; --self-test and host tests disable it before StartServices.
    [[nodiscard]] HRESULT StartServices(const AppSettings& settings) noexcept;
    [[nodiscard]] HRESULT ApplyServiceSettings(const AppSettings& settings) noexcept;
    void PublishHostState(const RedXeHostState& state) noexcept;
    void StopServices() noexcept;
    void SetDeviceAccessEnabled(bool enabled) noexcept;
    [[nodiscard]] bool DeviceAccessEnabled() const noexcept;
    [[nodiscard]] uint32_t StartedServiceCount() const noexcept;
    [[nodiscard]] uint32_t RunningDeviceWorkerCount() const noexcept;
    // The started service for one catalogued plugin ID, or null. Borrowed; UI thread only.
    [[nodiscard]] IRedXeService* ServiceFor(const char* pluginId) const noexcept;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE GetDataProvider(const char* providerId, IRedXeDataProvider** provider) noexcept override;
    HRESULT STDMETHODCALLTYPE RequestFrame() noexcept override;
    HRESULT STDMETHODCALLTYPE ReportWidgetStatus(const char* instanceId,
                                                 const RedXeWidgetStatusReport* report) noexcept override;
    HRESULT STDMETHODCALLTYPE PersistWidgetSettings(const char* instanceId, const char* settingsJsonUtf8,
                                                    uint32_t settingsBytes) noexcept override;
    HRESULT STDMETHODCALLTYPE Log(const RedXeLogRecord* record) noexcept override;
    HRESULT STDMETHODCALLTYPE QueueControlWork(IRedXeControlWork* work) noexcept override;
    HRESULT STDMETHODCALLTYPE RequestAction(const RedXeActionRequest* request) noexcept override;
    HRESULT STDMETHODCALLTYPE ExecuteAction(const RedXeActionRequest* request) noexcept override;
    HRESULT STDMETHODCALLTYPE ValidateAction(const RedXeActionRequest* request,
                                             const RedXeActionDescriptor** descriptor) noexcept override;
    void SetControlAccessEnabled(bool enabled) noexcept
    {
        _controlAccessEnabled = enabled;
    }
    HRESULT STDMETHODCALLTYPE QueueWidgetSettings(const char* instanceId, const char* jsonUtf8,
                                                  uint32_t bytes) noexcept override;

    // Opens `%LocalAppData%\RedXe\Logs` (or a test directory) and starts the event-blocked writer. `--self-test`
    // must not call this. Until it succeeds, Log returns S_OK and drops the line. A null or empty directory returns
    // E_INVALIDARG. The writer uses a UTC-dated JSONL file and deletes older dated logs according to retention.
    [[nodiscard]] HRESULT SetLogDirectory(const wchar_t* directory) noexcept;
    // 1 through 365 days. Omitted settings use 15. Changing retention wakes the writer to delete expired files.
    [[nodiscard]] HRESULT SetLogRetentionDays(uint32_t days) noexcept;
    // Blocks until queued lines are on disk, or the timeout elapses. Tests use this; production shutdown flushes.
    [[nodiscard]] HRESULT FlushLog(uint32_t timeoutMilliseconds) noexcept;

    // Latest status reported by one widget instance. Unknown instances read back as RedXeWidgetStatusOk so a widget
    // that never reports is never drawn as a placeholder.
    [[nodiscard]] uint32_t WidgetStatus(const char* instanceId) const noexcept;
    // Copies the latest reason text for one instance into caller storage. Returns false when there is none.
    [[nodiscard]] bool WidgetStatusReason(const char* instanceId, wchar_t* text, size_t capacity) const noexcept;
    void ClearWidgetStatus(const char* instanceId) noexcept;

  private:
#if defined(REDXE_HOST_PLUGIN_TESTS)
    friend struct PluginHostTestAccess;
#endif
    ControlWorkQueue _controlWork;
    bool _controlAccessEnabled = true;
    static constexpr size_t kMaximumDataSetsPerProvider = 256;
    static constexpr size_t kMaximumSubscriptions = 32;
    static constexpr uint32_t kMaximumSubscriptionIntervalMilliseconds = 60'000;
    // One slot per widget instance the host can compose at once: the current page plus its staged neighbour.
    static constexpr size_t kMaximumWidgetStatusSlots = 64;
    static constexpr size_t kMaximumWidgetStatusIdBytes = 128;
    static constexpr size_t kMaximumWidgetStatusReasonCharacters = 96;
    static constexpr size_t kMaximumNetworkWidgets = 8;
    static constexpr size_t kLogRingSlots = 32;
    static constexpr size_t kLogLineCapacity = 1024;
    static constexpr size_t kHostActionRingSlots = 16;

    class DataProvider;
    class Subscription;

    struct PendingSettings final
    {
        std::array<char, 128> instanceId{};
        std::array<char, 4097> json{};
        uint32_t bytes = 0;
    };
    std::array<std::unique_ptr<PendingSettings>, 8> _pendingSettings{};
    SRWLOCK _pendingSettingsLock = SRWLOCK_INIT;
    std::atomic<bool> _hasPendingSettings{false};

    struct ModuleSlot final
    {
        wil::unique_hmodule module;
        RedXeCreateFn create = nullptr;
        RedXeEnumeratePluginsFn enumerate = nullptr;
        RedXeGetPluginSettingsContractFn getSettingsContract = nullptr;
        RedXeGetActionContractFn getActionContract = nullptr;
        RedXePluginShutdownFn shutdown = nullptr;
        uint32_t capabilities = RedXePluginCapabilityNone;
    };

    // One registered action namespace (kRedXeBundledActionNamespaces) and what the host learned about it.
    struct PublisherSlot final
    {
        const RedXeBundledActionNamespaceSpec* spec = nullptr;
        // Borrowed from the publishing module; valid while it stays mapped (modules never unmap before teardown).
        const RedXeActionNamespace* contract = nullptr;
        // Executor for a dedicated action DLL or a widget provider. A service publisher's executor is queried on
        // its started service object instead and never retained here.
        wil::com_ptr_nothrow<IRedXeActionPack> executor;
        ActionPublisherState state = ActionPublisherState::Unresolved;
        bool noticeShown = false;
    };

    struct ActionNotice final
    {
        std::array<wchar_t, kActionNoticeCharacters> text{};
        bool used = false;
    };

    struct DataSetRuntime final
    {
        const RedXeDataSetDescriptor* descriptor = nullptr;
        uint64_t due = 0;
        uint32_t activeIntervalMilliseconds = kMaximumSubscriptionIntervalMilliseconds;
        bool active = false;
    };

    struct ProviderRuntime final
    {
        const char* providerId = nullptr;
        wil::com_ptr_nothrow<IRedXeDataSource> source;
        wil::com_ptr_nothrow<IRedXeDataProvider> provider;
        const RedXeDataSetDescriptor* descriptors = nullptr;
        uint32_t descriptorCount = 0;
        std::array<DataSetRuntime, kMaximumDataSetsPerProvider> dataSets{};
    };

    struct WidgetStatusSlot final
    {
        std::array<char, kMaximumWidgetStatusIdBytes> instanceId{};
        std::array<wchar_t, kMaximumWidgetStatusReasonCharacters> reason{};
        uint32_t status = RedXeWidgetStatusOk;
        bool used = false;
    };

    struct SubscriptionSlot final
    {
        wil::com_ptr_nothrow<IRedXeDataSink> sink;
        uint64_t token = 0;
        size_t providerIndex = 0;
        size_t dataSetIndex = 0;
        uint32_t intervalMilliseconds = 0;
        bool active = false;
        bool removing = false;
        uint32_t inFlight = 0;
    };

    struct NetworkSlot final
    {
        wil::com_ptr_nothrow<IRedXeNetworkWidget> widget;
        uint64_t due = 0;
        bool active = false;
        bool inFlight = false;
    };

    struct LogLineSlot final
    {
        std::array<char, kLogLineCapacity> line{};
        uint32_t bytes = 0;
    };

    struct HostActionSlot final
    {
        std::array<char, kRedXeMaximumActionNameBytes + 1> action{};
        std::array<char, kRedXeMaximumActionTargetBytes + 1> target{};
        bool used = false;
    };

    struct ServiceSlot final
    {
        const RedXeBundledServiceSpec* spec = nullptr;
        wil::com_ptr_nothrow<IRedXeService> service;
        wil::com_ptr_nothrow<IRedXeDeviceWorker> worker;
        std::jthread lane;
        wil::unique_event_nothrow stopEvent;
        wil::unique_event_nothrow wakeEvent;
        // The effective compact settings object the service was last given, so a live reload re-applies only when
        // the object actually changed.
        JsonObjectSettings settings;
        bool started = false;
        bool laneRunning = false;
        bool stopPending = false;
        bool laneTombstoned = false;
    };

    [[nodiscard]] HRESULT LoadModule(const RedXeBundledPluginSpec& spec, ModuleSlot& slot) noexcept;
    [[nodiscard]] HRESULT BindModule(const RedXeBundledPluginSpec& spec, ModuleSlot& slot) noexcept;
    [[nodiscard]] HRESULT AttachSharedModule(const ModuleSlot& owner, const char* pluginId, ModuleSlot& slot) noexcept;
    void RequestUiInvalidate() noexcept;
    [[nodiscard]] HRESULT EnsureProvider(const char* providerId, size_t& providerIndex) noexcept;
    [[nodiscard]] HRESULT ValidateDataSets(ProviderRuntime& runtime) noexcept;
    [[nodiscard]] HRESULT EnsureWorker() noexcept;
    [[nodiscard]] HRESULT GetDataSets(size_t providerIndex, const RedXeDataSetDescriptor** descriptors,
                                      uint32_t* count) noexcept;
    [[nodiscard]] HRESULT Subscribe(size_t providerIndex, const RedXeDataSubscriptionOptions* options,
                                    IRedXeDataSink* sink, IRedXeDataSubscription** subscription) noexcept;
    [[nodiscard]] HRESULT SetSubscriptionActive(size_t index, uint64_t token, bool active) noexcept;
    void RemoveSubscription(size_t index, uint64_t token) noexcept;
    void Deliver(size_t providerIndex, size_t dataSetIndex, const RedXeDataSnapshot* snapshot) noexcept;
    void Worker() noexcept;
    void StopDataService() noexcept;
    [[nodiscard]] HRESULT EnsureNetworkWorker() noexcept;
    void NetworkWorker() noexcept;
    void JoinNetworkWorker() noexcept;
    void StopNetworkService() noexcept;
    void ShutdownModules() noexcept;
    [[nodiscard]] HRESULT EnsureLogWorker() noexcept;
    void LogWorker() noexcept;
    void StopLogService() noexcept;
    [[nodiscard]] HRESULT EnsureCurrentLogFile() noexcept;
    void PurgeExpiredLogs() noexcept;
    [[nodiscard]] HRESULT EnqueueLogLine(const char* line, uint32_t bytes) noexcept;
    void RequestHostActionDrain() noexcept;
    // Executes one action now on the UI thread: application namespaces through the handler, system/keys/mouse
    // through HostActions, published namespaces through their executor. Logs a Debug line on failure.
    [[nodiscard]] HRESULT ExecuteNow(const char* actionUtf8, const char* targetUtf8) noexcept;
    // Reads and registers the action contract of one plugin id whose module just mapped.
    void ReadActionContract(ModuleSlot& slot, const char* pluginId) noexcept;
    [[nodiscard]] PublisherSlot* FindPublisher(std::string_view actionNamespace) noexcept;
    // Maps the publisher's module if needed so its contract is known; sets the slot state.
    [[nodiscard]] HRESULT EnsurePublisherContract(PublisherSlot& slot) noexcept;
    // The executor for a publisher: the started service object, or a created (retained) pack object.
    [[nodiscard]] HRESULT EnsurePublisherExecutor(PublisherSlot& slot, IRedXeActionPack** executor) noexcept;
    [[nodiscard]] const RedXeActionDescriptor* FindPublishedAction(const PublisherSlot& slot,
                                                                   std::string_view actionName) const noexcept;
    void AddActionNotice(const wchar_t* text) noexcept;
    void ReleaseActionExecutors() noexcept;
    [[nodiscard]] HRESULT CreateService(ServiceSlot& slot, const ServiceSettings& settings) noexcept;
    [[nodiscard]] HRESULT StartService(ServiceSlot& slot) noexcept;
    void StopService(ServiceSlot& slot) noexcept;
    [[nodiscard]] HRESULT StartDeviceLane(ServiceSlot& slot) noexcept;
    [[nodiscard]] bool StopDeviceLane(ServiceSlot& slot) noexcept;
    void DeviceLane(ServiceSlot& slot) noexcept;

    std::atomic<ULONG> _references{1};
    std::array<ModuleSlot, kRedXeBundledPlugins.size()> _modules;
    std::array<ProviderRuntime, kRedXeBundledPlugins.size()> _providers;
    size_t _providerCount = 0;
    std::array<SubscriptionSlot, kMaximumSubscriptions> _subscriptions;
    SRWLOCK _subscriptionLock = SRWLOCK_INIT;
    CONDITION_VARIABLE _subscriptionDrained = CONDITION_VARIABLE_INIT;
    std::array<WidgetStatusSlot, kMaximumWidgetStatusSlots> _widgetStatus;
    mutable SRWLOCK _widgetStatusLock = SRWLOCK_INIT;
    wil::unique_event_nothrow _stopEvent;
    wil::unique_event_nothrow _changeEvent;
    std::jthread _worker;
    std::array<NetworkSlot, kMaximumNetworkWidgets> _networkSlots;
    SRWLOCK _networkLock = SRWLOCK_INIT;
    wil::unique_event_nothrow _networkStopEvent;
    wil::unique_event_nothrow _networkWakeEvent;
    wil::unique_event_nothrow _networkCancelEvent;
    wil::unique_event_nothrow _networkIdleEvent;
    std::jthread _networkWorker;
    std::atomic<bool> _networkAccessEnabled{true};
    uint64_t _nextToken = 1;
    std::atomic<HWND> _uiWindow{nullptr};
    std::atomic<uint32_t> _pendingInvalidate{0};
    SettingsPersistHandler _settingsPersistHandler = nullptr;
    void* _settingsPersistContext = nullptr;
    std::wstring _logDirectory;
    std::wstring _logFilePath;
    wil::unique_hfile _logFile;
    wil::unique_event_nothrow _logStopEvent;
    wil::unique_event_nothrow _logWakeEvent;
    wil::unique_event_nothrow _logIdleEvent;
    std::jthread _logWorker;
    std::array<LogLineSlot, kLogRingSlots> _logRing{};
    size_t _logHead = 0;
    size_t _logTail = 0;
    size_t _logCount = 0;
    SRWLOCK _logLock = SRWLOCK_INIT;
    std::atomic<uint32_t> _logQueued{0};
    std::atomic<uint32_t> _logRetentionDays{kRedXeDefaultLogRetentionDays};
    std::array<HostActionSlot, kHostActionRingSlots> _hostActions{};
    size_t _hostActionHead = 0;
    size_t _hostActionCount = 0;
    mutable SRWLOCK _hostActionLock = SRWLOCK_INIT;
    std::atomic<uint32_t> _pendingHostActionPost{0};
    HostActionHandler _hostActionHandler = nullptr;
    HostActionCompleted _hostActionCompleted = nullptr;
    void* _hostActionContext = nullptr;
    std::array<PublisherSlot, kRedXeBundledActionNamespaces.size()> _publishers;
    std::array<ActionNotice, kMaximumActionNotices> _actionNotices{};
    uint32_t _actionNoticeGeneration = 0;
    std::array<ServiceSlot, kRedXeBundledServices.size()> _services;
    std::atomic<bool> _deviceAccessEnabled{true};
    bool _shutdown = false;
};
