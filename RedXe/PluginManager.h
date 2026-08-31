#pragma once

#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/GpuWidget.h"
#include "PlugInterfaces/Widget.h"
#include "PlugInterfaces/WindowWidget.h"

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
    static constexpr std::uint32_t kMaximumWidgetInstances = 8;

    PluginManager() = default;
    ~PluginManager();

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;
    PluginManager(PluginManager&&) = delete;
    PluginManager& operator=(PluginManager&&) = delete;

    [[nodiscard]] HRESULT Initialize(std::uint32_t widgetInstanceCount) noexcept;
    [[nodiscard]] std::size_t WidgetCount() const noexcept;
    [[nodiscard]] IRedXeWidget* WidgetAt(std::size_t index) const noexcept;
    [[nodiscard]] IRedXeGpuWidget* GpuWidgetAt(std::size_t index) const noexcept;
    [[nodiscard]] IRedXeWindowWidget* WindowWidgetAt(std::size_t index) const noexcept;
    [[nodiscard]] std::uint32_t WidgetFlagsAt(std::size_t index) const noexcept;

  private:
    struct WidgetSlot final
    {
        wil::com_ptr_nothrow<IRedXeWidget> widget;
        wil::com_ptr_nothrow<IRedXeGpuWidget> gpuWidget;
        wil::com_ptr_nothrow<IRedXeWindowWidget> windowWidget;
        std::uint32_t flags = RedXeWidgetFlagNone;
    };

    wil::unique_hmodule _module;
    wil::com_ptr_nothrow<IRedXeWidgetProvider> _provider;
    std::array<WidgetSlot, kMaximumWidgetInstances> _widgets;
    std::size_t _widgetCount = 0;
    RedXePluginShutdownFn _shutdown = nullptr;
};
