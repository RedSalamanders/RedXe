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

    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;
    PluginHost(PluginHost&&) = delete;
    PluginHost& operator=(PluginHost&&) = delete;

    [[nodiscard]] IRedXeHost* Interface() noexcept;
    [[nodiscard]] HRESULT GetPluginModule(const char* pluginId, std::uint32_t requiredCapabilities,
                                          ModuleView* module) noexcept;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE GetDataProvider(const char* providerId, IRedXeDataProvider** provider) noexcept override;

  private:
    static constexpr std::size_t kMaximumDataSetsPerProvider = 256;
    static constexpr std::size_t kMaximumSubscriptions = 32;
    static constexpr std::uint32_t kMaximumSubscriptionIntervalMilliseconds = 60'000;

    class DataProvider;
    class Subscription;

    struct ModuleSlot final
    {
        wil::unique_hmodule module;
        RedXeCreateFn create = nullptr;
        RedXeEnumeratePluginsFn enumerate = nullptr;
        RedXeGetPluginSettingsContractFn getSettingsContract = nullptr;
        RedXePluginShutdownFn shutdown = nullptr;
        std::uint32_t capabilities = RedXePluginCapabilityNone;
    };

    struct DataSetRuntime final
    {
        const RedXeDataSetDescriptor* descriptor = nullptr;
        std::uint64_t due = 0;
        std::uint32_t activeIntervalMilliseconds = kMaximumSubscriptionIntervalMilliseconds;
        bool active = false;
    };

    struct ProviderRuntime final
    {
        const char* providerId = nullptr;
        wil::com_ptr_nothrow<IRedXeDataSource> source;
        wil::com_ptr_nothrow<IRedXeDataProvider> provider;
        const RedXeDataSetDescriptor* descriptors = nullptr;
        std::uint32_t descriptorCount = 0;
        std::array<DataSetRuntime, kMaximumDataSetsPerProvider> dataSets{};
    };

    struct SubscriptionSlot final
    {
        wil::com_ptr_nothrow<IRedXeDataSink> sink;
        std::uint64_t token = 0;
        std::size_t providerIndex = 0;
        std::size_t dataSetIndex = 0;
        std::uint32_t intervalMilliseconds = 0;
        bool active = false;
    };

    [[nodiscard]] HRESULT LoadModule(const RedXeBundledPluginSpec& spec, ModuleSlot& slot) noexcept;
    [[nodiscard]] HRESULT EnsureProvider(const char* providerId, std::size_t& providerIndex) noexcept;
    [[nodiscard]] HRESULT ValidateDataSets(ProviderRuntime& runtime) noexcept;
    [[nodiscard]] HRESULT EnsureWorker() noexcept;
    [[nodiscard]] HRESULT GetDataSets(std::size_t providerIndex, const RedXeDataSetDescriptor** descriptors,
                                      std::uint32_t* count) noexcept;
    [[nodiscard]] HRESULT Subscribe(std::size_t providerIndex, const RedXeDataSubscriptionOptions* options,
                                    IRedXeDataSink* sink, IRedXeDataSubscription** subscription) noexcept;
    [[nodiscard]] HRESULT SetSubscriptionActive(std::size_t index, std::uint64_t token, bool active) noexcept;
    void RemoveSubscription(std::size_t index, std::uint64_t token) noexcept;
    void Deliver(std::size_t providerIndex, std::size_t dataSetIndex, const RedXeDataSnapshot* snapshot) noexcept;
    void Worker() noexcept;
    void StopDataService() noexcept;
    void ShutdownModules() noexcept;

    std::atomic<ULONG> _references{1};
    std::array<ModuleSlot, kRedXeBundledPlugins.size()> _modules;
    std::array<ProviderRuntime, kRedXeBundledPlugins.size()> _providers;
    std::size_t _providerCount = 0;
    std::array<SubscriptionSlot, kMaximumSubscriptions> _subscriptions;
    SRWLOCK _subscriptionLock = SRWLOCK_INIT;
    wil::unique_event_nothrow _stopEvent;
    wil::unique_event_nothrow _changeEvent;
    std::jthread _worker;
    std::uint64_t _nextToken = 1;
};
