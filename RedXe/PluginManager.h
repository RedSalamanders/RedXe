#pragma once

#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/GpuWidget.h"
#include "PlugInterfaces/Widget.h"
#include "PlugInterfaces/WindowWidget.h"
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
    static constexpr std::uint32_t kMaximumWidgetInstances = static_cast<std::uint32_t>(kMaximumWidgetsPerPage);

    PluginManager() = default;
    ~PluginManager();

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;
    PluginManager(PluginManager&&) = delete;
    PluginManager& operator=(PluginManager&&) = delete;

    [[nodiscard]] HRESULT Initialize(const AppSettings& settings) noexcept;
    [[nodiscard]] HRESULT Reconfigure(const AppSettings& settings) noexcept;
    [[nodiscard]] std::size_t ProviderCount() const noexcept;
    [[nodiscard]] std::size_t WidgetCount() const noexcept;
    [[nodiscard]] IRedXeWidget* WidgetAt(std::size_t index) const noexcept;
    [[nodiscard]] IRedXeGpuWidget* GpuWidgetAt(std::size_t index) const noexcept;
    [[nodiscard]] IRedXeWindowWidget* WindowWidgetAt(std::size_t index) const noexcept;
    [[nodiscard]] std::uint32_t WidgetFlagsAt(std::size_t index) const noexcept;
    [[nodiscard]] WidgetGridPlacement WidgetGridPlacementAt(std::size_t index) const noexcept;
    [[nodiscard]] const char* WidgetInstanceIdAt(std::size_t index) const noexcept;
    [[nodiscard]] std::uint32_t GridColumns() const noexcept;
    [[nodiscard]] std::uint32_t GridRows() const noexcept;

  private:
    static constexpr std::size_t kMaximumModules = kMaximumSettingsPlugins;

    struct WidgetSlot final
    {
        wil::com_ptr_nothrow<IRedXeWidget> widget;
        wil::com_ptr_nothrow<IRedXeGpuWidget> gpuWidget;
        wil::com_ptr_nothrow<IRedXeWindowWidget> windowWidget;
        SettingsText instanceId;
        WidgetGridPlacement placement;
        std::uint32_t flags = RedXeWidgetFlagNone;
    };

    struct ProviderSlot final
    {
        wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    };

    struct ModuleSlot final
    {
        wil::unique_hmodule module;
        RedXeCreateFn create = nullptr;
        RedXePluginShutdownFn shutdown = nullptr;
    };

    struct ProviderBuildKey final
    {
        std::array<char, kFactoryConfigurationCapacity> configuration{};
        std::uint32_t configurationBytes = 0;
        std::size_t moduleIndex = 0;
    };

    [[nodiscard]] HRESULT LoadBundledModule(const wchar_t* moduleName, const char* pluginId,
                                            ModuleSlot& moduleSlot) noexcept;
    [[nodiscard]] HRESULT CreateBundledProvider(ModuleSlot& moduleSlot, const char* pluginId,
                                                const char* configurationJson, std::uint32_t configurationBytes,
                                                IRedXeWidgetProvider** provider) noexcept;
    [[nodiscard]] HRESULT CreateWidgetInstance(IRedXeWidgetProvider& provider, const WidgetInstanceSettings& settings,
                                               WidgetSlot& widgetSlot) noexcept;
    [[nodiscard]] HRESULT StageActivePage(const AppSettings& settings,
                                          std::array<ModuleSlot, kMaximumModules>& loadedModules,
                                          std::array<ProviderSlot, kMaximumWidgetInstances>& providers,
                                          std::array<ProviderBuildKey, kMaximumWidgetInstances>& providerKeys,
                                          std::size_t& providerCount,
                                          std::array<WidgetSlot, kMaximumWidgetInstances>& widgets,
                                          std::size_t& widgetCount) noexcept;

    std::array<ModuleSlot, kMaximumModules> _modules;
    std::array<ProviderSlot, kMaximumWidgetInstances> _providers;
    std::array<WidgetSlot, kMaximumWidgetInstances> _widgets;
    std::size_t _providerCount = 0;
    std::size_t _widgetCount = 0;
    std::uint32_t _gridColumns = 0;
    std::uint32_t _gridRows = 0;
    bool _initialized = false;
};
