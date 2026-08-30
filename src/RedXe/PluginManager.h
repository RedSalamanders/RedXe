#pragma once

#include "PlugInterfaces/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

struct WidgetPlacement final
{
    float x;
    float y;
    float width;
    float height;
};

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

    [[nodiscard]] HRESULT Initialize(std::uint32_t widgetInstanceCount, bool validateFactoryContract) noexcept;
    [[nodiscard]] std::size_t WidgetCount() const noexcept;
    [[nodiscard]] IRedXeWidget* WidgetAt(std::size_t index) const noexcept;
    [[nodiscard]] WidgetPlacement PlacementAt(std::size_t index) const noexcept;

  private:
    struct WidgetSlot final
    {
        wil::com_ptr_nothrow<IRedXeWidget> widget;
        WidgetPlacement placement{};
    };

    wil::unique_hmodule _module;
    wil::com_ptr_nothrow<IRedXeWidgetProvider> _provider;
    std::array<WidgetSlot, kMaximumWidgetInstances> _widgets;
    std::size_t _widgetCount = 0;
    void(__stdcall* _shutdown)() noexcept = nullptr;
};
