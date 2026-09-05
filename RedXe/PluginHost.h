#pragma once

#include "BundledPlugins.h"
#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Host.h"
#include "PlugInterfaces/Widget.h"
#include "Settings.h"
#include "ControlWorkQueue.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
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
    void SetControlAccessEnabled(bool enabled) noexcept { _controlAccessEnabled = enabled; }
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
        RedXePluginShutdownFn shutdown = nullptr;
        uint32_t capabilities = RedXePluginCapabilityNone;
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
    bool _shutdown = false;
};
