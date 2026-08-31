#pragma once

#include "PluginManager.h"

#include <array>
#include <cstddef>
#include <windows.h>

struct WidgetPlacement final
{
    float x;
    float y;
    float width;
    float height;
};

class DashboardHost final
{
  public:
    [[nodiscard]] HRESULT Initialize(PluginManager& pluginManager) noexcept;
    [[nodiscard]] std::size_t WidgetCount() const noexcept;
    [[nodiscard]] IRedXeWidget* WidgetAt(std::size_t index) const noexcept;
    [[nodiscard]] IRedXeGpuWidget* GpuWidgetAt(std::size_t index) const noexcept;
    [[nodiscard]] IRedXeWindowWidget* WindowWidgetAt(std::size_t index) const noexcept;
    [[nodiscard]] WidgetPlacement PlacementAt(std::size_t index) const noexcept;
    [[nodiscard]] bool RequiresContinuousFrames() const noexcept;

  private:
    PluginManager* _pluginManager = nullptr;
    std::array<WidgetPlacement, PluginManager::kMaximumWidgetInstances> _placements{};
    std::size_t _widgetCount = 0;
    bool _requiresContinuousFrames = false;
};
