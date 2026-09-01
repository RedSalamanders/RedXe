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

    bool operator==(const WidgetPlacement&) const noexcept = default;
};

class DashboardHost final
{
  public:
    DashboardHost() = default;
    ~DashboardHost();

    DashboardHost(const DashboardHost&) = delete;
    DashboardHost& operator=(const DashboardHost&) = delete;
    DashboardHost(DashboardHost&&) = delete;
    DashboardHost& operator=(DashboardHost&&) = delete;

    [[nodiscard]] HRESULT Initialize(PluginManager& pluginManager, HWND parent, UINT width, UINT height, UINT dpi,
                                     bool visible) noexcept;
    [[nodiscard]] HRESULT Resize(UINT width, UINT height, UINT dpi) noexcept;
    [[nodiscard]] HRESULT SetWidgetsVisible(bool visible) noexcept;
    [[nodiscard]] HRESULT SetHorizontalOffset(LONG offset) noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] std::size_t WidgetCount() const noexcept;
    [[nodiscard]] IRedXeWidget* WidgetAt(std::size_t index) const noexcept;
    [[nodiscard]] IRedXeGpuWidget* GpuWidgetAt(std::size_t index) const noexcept;
    [[nodiscard]] IRedXeWindowWidget* WindowWidgetAt(std::size_t index) const noexcept;
    [[nodiscard]] WidgetPlacement PlacementAt(std::size_t index) const noexcept;
    [[nodiscard]] RECT PixelBoundsAt(std::size_t index, UINT width, UINT height) const noexcept;
    [[nodiscard]] bool RequiresContinuousFrames() const noexcept;
    [[nodiscard]] HRESULT GetNextFrameDelayMilliseconds(std::uint32_t* delayMilliseconds) const noexcept;

  private:
    PluginManager* _pluginManager = nullptr;
    std::array<WidgetPlacement, PluginManager::kMaximumWidgetInstances> _placements{};
    std::array<WidgetGridPlacement, PluginManager::kMaximumWidgetInstances> _gridPlacements{};
    std::array<AdaptiveWidgetPlacement, PluginManager::kMaximumWidgetInstances> _adaptivePlacements{};
    std::array<bool, PluginManager::kMaximumWidgetInstances> _usesAdaptivePlacement{};
    std::array<wil::unique_hwnd, PluginManager::kMaximumWidgetInstances> _windowContainers;
    std::size_t _widgetCount = 0;
    std::uint32_t _gridColumns = 0;
    std::uint32_t _gridRows = 0;
    bool _requiresContinuousFrames = false;
    bool _widgetsVisible = false;
    LONG _horizontalOffset = 0;
    UINT _clientWidth = 0;
    UINT _clientHeight = 0;
};
