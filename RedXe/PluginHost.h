#pragma once

#include "BundledPlugins.h"
#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Host.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

// One process-scoped plugin runtime. It owns every mapped plugin module, every host data provider and plugin data
// source, the single acquisition worker, and subscription drain lifetime. PluginManager instances borrow it through
// Instance() so that staging an adjacent dashboard page reuses the already-mapped modules, the already-created data
// sources, and the already-running worker instead of building a second runtime beside them.
class PluginHost final : public IRedXeHost
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

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE GetDataProvider(const char* providerId, IRedXeDataProvider** provider) noexcept override;
    HRESULT STDMETHODCALLTYPE RequestFrame() noexcept override;
    HRESULT STDMETHODCALLTYPE ReportWidgetStatus(const char* instanceId,
                                                 const RedXeWidgetStatusReport* report) noexcept override;

    // Latest status reported by one widget instance. Unknown instances read back as RedXeWidgetStatusOk so a widget
    // that never reports is never drawn as a placeholder.
    [[nodiscard]] uint32_t WidgetStatus(const char* instanceId) const noexcept;
    // Copies the latest reason text for one instance into caller storage. Returns false when there is none.
    [[nodiscard]] bool WidgetStatusReason(const char* instanceId, wchar_t* text, size_t capacity) const noexcept;
    void ClearWidgetStatus(const char* instanceId) noexcept;

  private:
    static constexpr size_t kMaximumDataSetsPerProvider = 256;
    static constexpr size_t kMaximumSubscriptions = 32;
    static constexpr uint32_t kMaximumSubscriptionIntervalMilliseconds = 60'000;
    // One slot per widget instance the host can compose at once: the current page plus its staged neighbour.
    static constexpr size_t kMaximumWidgetStatusSlots = 64;
    static constexpr size_t kMaximumWidgetStatusIdBytes = 128;
    static constexpr size_t kMaximumWidgetStatusReasonCharacters = 96;

    class DataProvider;
    class Subscription;

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
    void ShutdownModules() noexcept;

    std::atomic<ULONG> _references{1};
    std::array<ModuleSlot, kRedXeBundledPlugins.size()> _modules;
    std::array<ProviderRuntime, kRedXeBundledPlugins.size()> _providers;
    size_t _providerCount = 0;
    std::array<SubscriptionSlot, kMaximumSubscriptions> _subscriptions;
    SRWLOCK _subscriptionLock = SRWLOCK_INIT;
    std::array<WidgetStatusSlot, kMaximumWidgetStatusSlots> _widgetStatus;
    mutable SRWLOCK _widgetStatusLock = SRWLOCK_INIT;
    wil::unique_event_nothrow _stopEvent;
    wil::unique_event_nothrow _changeEvent;
    std::jthread _worker;
    uint64_t _nextToken = 1;
    std::atomic<HWND> _uiWindow{nullptr};
    std::atomic<uint32_t> _pendingInvalidate{0};
    bool _shutdown = false;
};
