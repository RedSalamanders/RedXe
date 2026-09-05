#pragma once

#include "PluginManager.h"

#include <array>
#include <cstddef>
#include <cstdint>
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
    [[nodiscard]] bool WidgetsVisible() const noexcept
    {
        return _widgetsVisible;
    }
    [[nodiscard]] HRESULT SetHorizontalOffset(LONG offset) noexcept;
    [[nodiscard]] LONG HorizontalOffset() const noexcept;
    [[nodiscard]] HRESULT ApplyRaisedNativeLayout(size_t widgetIndex, const RECT& content, UINT dpi) noexcept;
    [[nodiscard]] HRESULT ClearRaisedNativeLayout(UINT dpi) noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] size_t WidgetCount() const noexcept;
    [[nodiscard]] IRedXeWidget* WidgetAt(size_t index) const noexcept;
    [[nodiscard]] IRedXeGpuWidget* GpuWidgetAt(size_t index) const noexcept;
    [[nodiscard]] IRedXePreparedGpuWidget* PreparedGpuWidgetAt(size_t index) const noexcept;
    [[nodiscard]] IRedXeWindowWidget* WindowWidgetAt(size_t index) const noexcept;
    [[nodiscard]] IRedXeRaisedWidget* RaisedWidgetAt(size_t index) const noexcept;
    [[nodiscard]] IRedXeInteractiveWidget* InteractiveWidgetAt(size_t index) const noexcept;
    [[nodiscard]] IRedXeKeyboardWidget* KeyboardWidgetAt(size_t index) const noexcept;
    [[nodiscard]] IRedXeTextInputWidget* TextInputWidgetAt(size_t index) const noexcept;
    [[nodiscard]] IRedXeAccessibilityWidget* AccessibilityWidgetAt(size_t index) const noexcept;
    [[nodiscard]] const char* WidgetInstanceIdAt(size_t index) const noexcept;
    [[nodiscard]] size_t RaisedNativeIndex() const noexcept;
    [[nodiscard]] WidgetPlacement PlacementAt(size_t index) const noexcept;
    [[nodiscard]] RECT PixelBoundsAt(size_t index, UINT width, UINT height) const noexcept;
    [[nodiscard]] bool RequiresContinuousFrames() const noexcept;
    [[nodiscard]] bool HasWindowWidgets() const noexcept;
    // True when the host owns this tile's pixels: the instance failed to construct, or it reported itself
    // unavailable through IRedXeHost::ReportWidgetStatus.
    [[nodiscard]] bool RequiresPlaceholderAt(size_t index) const noexcept;
    [[nodiscard]] HRESULT GetNextFrameDelayMilliseconds(uint32_t* delayMilliseconds) const noexcept;

  private:
    PluginManager* _pluginManager = nullptr;
    std::array<WidgetPlacement, PluginManager::kMaximumWidgetInstances> _placements{};
    std::array<WidgetGridPlacement, PluginManager::kMaximumWidgetInstances> _gridPlacements{};
    std::array<AdaptiveWidgetPlacement, PluginManager::kMaximumWidgetInstances> _adaptivePlacements{};
    std::array<bool, PluginManager::kMaximumWidgetInstances> _usesAdaptivePlacement{};
    std::array<wil::unique_hwnd, PluginManager::kMaximumWidgetInstances> _windowContainers;
    size_t _widgetCount = 0;
    uint32_t _gridColumns = 0;
    uint32_t _gridRows = 0;
    bool _requiresContinuousFrames = false;
    bool _widgetsVisible = false;
    LONG _horizontalOffset = 0;
    UINT _clientWidth = 0;
    UINT _clientHeight = 0;
    size_t _raisedNativeIndex = SIZE_MAX;
};
