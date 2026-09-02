#pragma once

#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Widget.h"
#include "PluginHost.h"
#include "Settings.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

class PluginManager final
{
  public:
    static constexpr uint32_t kMaximumWidgetInstances = static_cast<uint32_t>(kMaximumWidgetsPerPage);

    PluginManager() = default;
    ~PluginManager();

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;
    PluginManager(PluginManager&&) = delete;
    PluginManager& operator=(PluginManager&&) = delete;

    [[nodiscard]] HRESULT Initialize(const AppSettings& settings) noexcept;
    [[nodiscard]] HRESULT Reconfigure(const AppSettings& settings) noexcept;
    void SetUiInvalidateTarget(HWND window) noexcept;
    void AcknowledgeUiInvalidate() noexcept;
    [[nodiscard]] size_t ProviderCount() const noexcept;
    [[nodiscard]] size_t WidgetCount() const noexcept;
    [[nodiscard]] IRedXeWidget* WidgetAt(size_t index) const noexcept;
    [[nodiscard]] IRedXeGpuWidget* GpuWidgetAt(size_t index) const noexcept;
    [[nodiscard]] IRedXeScheduledWidget* ScheduledWidgetAt(size_t index) const noexcept;
    [[nodiscard]] IRedXeWindowWidget* WindowWidgetAt(size_t index) const noexcept;
    [[nodiscard]] uint32_t WidgetFlagsAt(size_t index) const noexcept;
    [[nodiscard]] WidgetGridPlacement WidgetGridPlacementAt(size_t index) const noexcept;
    [[nodiscard]] AdaptiveWidgetPlacement AdaptivePlacementAt(size_t index) const noexcept;
    [[nodiscard]] bool UsesAdaptivePlacementAt(size_t index) const noexcept;
    [[nodiscard]] const char* WidgetInstanceIdAt(size_t index) const noexcept;
    [[nodiscard]] uint32_t GridColumns() const noexcept;
    [[nodiscard]] uint32_t GridRows() const noexcept;

  private:
    struct WidgetSlot final
    {
        wil::com_ptr_nothrow<IRedXeGpuWidget> gpuWidget;
        wil::com_ptr_nothrow<IRedXeScheduledWidget> scheduledWidget;
        wil::com_ptr_nothrow<IRedXeWindowWidget> windowWidget;
        wil::com_ptr_nothrow<IRedXeWidget> widget;
        SettingsText instanceId;
        WidgetGridPlacement placement;
        AdaptiveWidgetPlacement adaptivePlacement;
        bool usesAdaptivePlacement = false;
        uint32_t flags = RedXeWidgetFlagNone;
    };

    struct ProviderSlot final
    {
        wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    };

    struct ProviderBuildKey final
    {
        std::array<char, kFactoryConfigurationCapacity> configuration{};
        uint32_t configurationBytes = 0;
        const char* pluginId = nullptr;
    };

    [[nodiscard]] HRESULT CreateBundledProvider(const char* pluginId, const char* configurationJson,
                                                uint32_t configurationBytes, IRedXeWidgetProvider** provider) noexcept;
    [[nodiscard]] HRESULT CreateWidgetInstance(IRedXeWidgetProvider& provider, const WidgetInstanceSettings& settings,
                                               WidgetSlot& widgetSlot) noexcept;
    [[nodiscard]] HRESULT StageActivePage(const AppSettings& settings,
                                          std::array<ProviderSlot, kMaximumWidgetInstances>& providers,
                                          std::array<ProviderBuildKey, kMaximumWidgetInstances>& providerKeys,
                                          size_t& providerCount,
                                          std::array<WidgetSlot, kMaximumWidgetInstances>& widgets,
                                          size_t& widgetCount) noexcept;

    PluginHost _pluginHost;
    std::array<ProviderSlot, kMaximumWidgetInstances> _providers;
    std::array<WidgetSlot, kMaximumWidgetInstances> _widgets;
    size_t _providerCount = 0;
    size_t _widgetCount = 0;
    uint32_t _gridColumns = 0;
    uint32_t _gridRows = 0;
    bool _initialized = false;
};
