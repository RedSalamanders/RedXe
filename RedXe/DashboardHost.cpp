#include "DashboardHost.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace
{
constexpr wchar_t kPointerForwardPrevProc[] = L"RedXe.PtrPrevProc";

LRESULT CALLBACK PointerForwardProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    const auto previous = reinterpret_cast<WNDPROC>(GetPropW(window, kPointerForwardPrevProc));
    if (message == WM_POINTERDOWN || message == WM_POINTERUPDATE || message == WM_POINTERUP ||
        message == WM_POINTERCAPTURECHANGED)
    {
        const HWND root = GetAncestor(window, GA_ROOT);
        if (root && root != window)
        {
            const LRESULT result = SendMessageW(root, message, wParam, lParam);
            if (result != 0 && message != WM_POINTERDOWN)
            {
                return 0;
            }
        }
    }

    if (!previous)
    {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    if (message == WM_NCDESTROY)
    {
        const LRESULT result = CallWindowProcW(previous, window, message, wParam, lParam);
        RemovePropW(window, kPointerForwardPrevProc);
        return result;
    }
    return CallWindowProcW(previous, window, message, wParam, lParam);
}

void SubclassPointerForwarder(HWND window) noexcept
{
    if (!window || GetPropW(window, kPointerForwardPrevProc))
    {
        return;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous =
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(PointerForwardProcedure));
    if (previous == 0 && GetLastError() != ERROR_SUCCESS)
    {
        return;
    }
    if (!SetPropW(window, kPointerForwardPrevProc, reinterpret_cast<HANDLE>(previous)))
    {
        SetWindowLongPtrW(window, GWLP_WNDPROC, previous);
    }
}

BOOL CALLBACK EnumPointerForwardSubclass(HWND window, LPARAM) noexcept
{
    SubclassPointerForwarder(window);
    return TRUE;
}

void InstallPointerForwarding(HWND container) noexcept
{
    SubclassPointerForwarder(container);
    EnumChildWindows(container, EnumPointerForwardSubclass, 0);
}

constexpr float kDesignWidth = 2560.0f;
constexpr float kDesignHeight = 720.0f;

[[nodiscard]] WidgetPlacement ToDesignPlacement(const WidgetGridPlacement& placement, uint32_t columns,
                                                uint32_t rows) noexcept
{
    const float left = kDesignWidth * static_cast<float>(placement.column) / static_cast<float>(columns);
    const float top = kDesignHeight * static_cast<float>(placement.row) / static_cast<float>(rows);
    const float right =
        kDesignWidth * static_cast<float>(placement.column + placement.columnSpan) / static_cast<float>(columns);
    const float bottom =
        kDesignHeight * static_cast<float>(placement.row + placement.rowSpan) / static_cast<float>(rows);
    return WidgetPlacement{left, top, right - left, bottom - top};
}

[[nodiscard]] LONG RoundGridEdge(uint32_t cell, UINT extent, uint32_t divisions) noexcept
{
    const uint64_t doubled = static_cast<uint64_t>(cell) * extent * 2U;
    return static_cast<LONG>((doubled + divisions) / (static_cast<uint64_t>(divisions) * 2U));
}

[[nodiscard]] RECT ToPixelBounds(const WidgetGridPlacement& placement, uint32_t columns, uint32_t rows, UINT width,
                                 UINT height) noexcept
{
    return RECT{
        RoundGridEdge(placement.column, width, columns),
        RoundGridEdge(placement.row, height, rows),
        RoundGridEdge(placement.column + placement.columnSpan, width, columns),
        RoundGridEdge(placement.row + placement.rowSpan, height, rows),
    };
}

[[nodiscard]] RECT ToAdaptivePixelBounds(const AdaptiveWidgetPlacement& placement, UINT width, UINT height) noexcept
{
    RECT bounds{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    const bool landscape = width >= height;
    for (uint32_t index = 0; index < placement.depth; ++index)
    {
        const LayoutSplitStep& step = placement.steps[index];
        if (step.sizeRatio == 0 || step.totalRatio == 0 || step.precedingRatio + step.sizeRatio > step.totalRatio)
        {
            return RECT{};
        }
        const bool horizontal = (step.axis == LayoutAxis::LongSide) == landscape;
        const LONG origin = horizontal ? bounds.left : bounds.top;
        const LONG extent = horizontal ? bounds.right - bounds.left : bounds.bottom - bounds.top;
        const LONG first =
            origin + static_cast<LONG>(static_cast<uint64_t>(extent) * step.precedingRatio / step.totalRatio);
        const LONG last = origin + static_cast<LONG>(static_cast<uint64_t>(extent) *
                                                     (step.precedingRatio + step.sizeRatio) / step.totalRatio);
        if (horizontal)
        {
            bounds.left = first;
            bounds.right = last;
        }
        else
        {
            bounds.top = first;
            bounds.bottom = last;
        }
    }
    return bounds;
}
} // namespace

DashboardHost::~DashboardHost()
{
    Shutdown();
}

HRESULT DashboardHost::Initialize(PluginManager& pluginManager, HWND parent, UINT width, UINT height, UINT dpi,
                                  bool visible) noexcept
{
    if (_pluginManager || !parent || width == 0 || height == 0 || dpi == 0 ||
        pluginManager.WidgetCount() > PluginManager::kMaximumWidgetInstances)
    {
        return E_INVALIDARG;
    }

    const auto widgetCount = static_cast<uint32_t>(pluginManager.WidgetCount());
    const uint32_t columns = pluginManager.GridColumns();
    const uint32_t rows = pluginManager.GridRows();
    if (columns == 0 || rows == 0 || columns > kMaximumDashboardGridDimension || rows > kMaximumDashboardGridDimension)
    {
        return E_INVALIDARG;
    }

    std::array<WidgetPlacement, PluginManager::kMaximumWidgetInstances> placements{};
    std::array<WidgetGridPlacement, PluginManager::kMaximumWidgetInstances> gridPlacements{};
    std::array<AdaptiveWidgetPlacement, PluginManager::kMaximumWidgetInstances> adaptivePlacements{};
    std::array<bool, PluginManager::kMaximumWidgetInstances> usesAdaptivePlacement{};
    std::array<wil::unique_hwnd, PluginManager::kMaximumWidgetInstances> containers;
    bool continuous = false;
    for (uint32_t index = 0; index < widgetCount; ++index)
    {
        IRedXeGpuWidget* gpuWidget = pluginManager.GpuWidgetAt(index);
        IRedXeWindowWidget* windowWidget = pluginManager.WindowWidgetAt(index);
        if (!gpuWidget && !windowWidget)
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }

        const WidgetGridPlacement gridPlacement = pluginManager.WidgetGridPlacementAt(index);
        const bool adaptive = pluginManager.UsesAdaptivePlacementAt(index);
        const AdaptiveWidgetPlacement adaptivePlacement = pluginManager.AdaptivePlacementAt(index);
        if ((!adaptive &&
             (gridPlacement.column >= columns || gridPlacement.row >= rows || gridPlacement.columnSpan == 0 ||
              gridPlacement.rowSpan == 0 || gridPlacement.columnSpan > columns - gridPlacement.column ||
              gridPlacement.rowSpan > rows - gridPlacement.row)) ||
            (adaptive && (adaptivePlacement.depth == 0 || adaptivePlacement.depth > kMaximumLayoutDepth)))
        {
            return E_INVALIDARG;
        }
        gridPlacements[index] = gridPlacement;
        adaptivePlacements[index] = adaptivePlacement;
        usesAdaptivePlacement[index] = adaptive;
        if (adaptive)
        {
            const RECT designBounds = ToAdaptivePixelBounds(adaptivePlacement, static_cast<UINT>(kDesignWidth),
                                                            static_cast<UINT>(kDesignHeight));
            placements[index] =
                WidgetPlacement{static_cast<float>(designBounds.left), static_cast<float>(designBounds.top),
                                static_cast<float>(designBounds.right - designBounds.left),
                                static_cast<float>(designBounds.bottom - designBounds.top)};
        }
        else
        {
            placements[index] = ToDesignPlacement(gridPlacement, columns, rows);
        }
        continuous = continuous || (pluginManager.WidgetFlagsAt(index) & RedXeWidgetFlagContinuousAnimation) != 0;

        if (!windowWidget)
        {
            continue;
        }

        const RECT bounds = adaptive ? ToAdaptivePixelBounds(adaptivePlacement, width, height)
                                     : ToPixelBounds(gridPlacement, columns, rows, width, height);
        const HWND container = CreateWindowExW(
            WS_EX_NOPARENTNOTIFY, L"STATIC", L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, bounds.left, bounds.top,
            bounds.right - bounds.left, bounds.bottom - bounds.top, parent, nullptr, nullptr, nullptr);
        if (!container)
        {
            const HRESULT result = HRESULT_FROM_WIN32(GetLastError());
            for (uint32_t previous = 0; previous < index; ++previous)
            {
                if (containers[previous])
                {
                    pluginManager.WindowWidgetAt(previous)->Detach();
                }
            }
            return result;
        }
        containers[index].reset(container);

        const RedXeWindowWidgetAttachContext context{
            sizeof(RedXeWindowWidgetAttachContext),
            container,
            static_cast<uint32_t>(bounds.right - bounds.left),
            static_cast<uint32_t>(bounds.bottom - bounds.top),
            dpi,
        };
        HRESULT result = windowWidget->Attach(&context);
        if (FAILED(result))
        {
            windowWidget->Detach();
            for (uint32_t previous = 0; previous < index; ++previous)
            {
                if (containers[previous])
                {
                    pluginManager.WindowWidgetAt(previous)->Detach();
                }
            }
            return result;
        }
        InstallPointerForwarding(container);
    }

    for (uint32_t index = 0; index < widgetCount; ++index)
    {
        IRedXeWidget* widget = pluginManager.WidgetAt(index);
        const HRESULT result = widget ? widget->SetVisible(visible ? TRUE : FALSE) : E_UNEXPECTED;
        if (FAILED(result))
        {
            for (uint32_t previous = 0; previous < index; ++previous)
            {
                (void)pluginManager.WidgetAt(previous)->SetVisible(FALSE);
            }
            for (uint32_t previous = widgetCount; previous > 0; --previous)
            {
                const uint32_t widgetIndex = previous - 1;
                if (containers[widgetIndex])
                {
                    pluginManager.WindowWidgetAt(widgetIndex)->Detach();
                }
            }
            return result;
        }
    }
    if (visible)
    {
        for (wil::unique_hwnd& container : containers)
        {
            if (container)
            {
                ShowWindow(container.get(), SW_SHOWNA);
            }
        }
    }

    _pluginManager = &pluginManager;
    pluginManager.SetUiInvalidateTarget(parent);
    _placements = placements;
    _gridPlacements = gridPlacements;
    _adaptivePlacements = adaptivePlacements;
    _usesAdaptivePlacement = usesAdaptivePlacement;
    _windowContainers = std::move(containers);
    _widgetCount = widgetCount;
    _gridColumns = columns;
    _gridRows = rows;
    _requiresContinuousFrames = continuous;
    _widgetsVisible = visible;
    _clientWidth = width;
    _clientHeight = height;
    return S_OK;
}

HRESULT DashboardHost::Resize(UINT width, UINT height, UINT dpi) noexcept
{
    if (!_pluginManager || dpi == 0)
    {
        return E_UNEXPECTED;
    }
    if (width == 0 || height == 0)
    {
        return SetWidgetsVisible(false);
    }
    _clientWidth = width;
    _clientHeight = height;

    for (size_t index = 0; index < _widgetCount; ++index)
    {
        IRedXeWindowWidget* widget = _pluginManager->WindowWidgetAt(index);
        if (!widget)
        {
            continue;
        }

        const RECT bounds = PixelBoundsAt(index, width, height);
        if (!SetWindowPos(_windowContainers[index].get(), nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
                          bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_NOZORDER))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        const RedXeWindowWidgetSizeContext context{
            sizeof(RedXeWindowWidgetSizeContext),
            static_cast<uint32_t>(bounds.right - bounds.left),
            static_cast<uint32_t>(bounds.bottom - bounds.top),
            dpi,
        };
        const HRESULT result = widget->Resize(&context);
        if (FAILED(result))
        {
            return result;
        }
    }
    return S_OK;
}

HRESULT DashboardHost::SetHorizontalOffset(LONG offset) noexcept
{
    if (!_pluginManager)
    {
        return E_UNEXPECTED;
    }
    if (_horizontalOffset == offset)
    {
        return S_OK;
    }
    _horizontalOffset = offset;

    UINT nativeCount = 0;
    for (size_t index = 0; index < _widgetCount; ++index)
    {
        if (_windowContainers[index])
        {
            ++nativeCount;
        }
    }
    if (nativeCount == 0)
    {
        return S_OK;
    }

    HDWP defer = BeginDeferWindowPos(static_cast<int>(nativeCount));
    if (!defer)
    {
        for (size_t index = 0; index < _widgetCount; ++index)
        {
            if (!_windowContainers[index])
            {
                continue;
            }
            const RECT bounds = PixelBoundsAt(index, _clientWidth, _clientHeight);
            if (!SetWindowPos(_windowContainers[index].get(), nullptr, bounds.left, bounds.top,
                              bounds.right - bounds.left, bounds.bottom - bounds.top,
                              SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE))
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
        }
        return S_OK;
    }

    for (size_t index = 0; index < _widgetCount; ++index)
    {
        if (!_windowContainers[index])
        {
            continue;
        }
        const RECT bounds = PixelBoundsAt(index, _clientWidth, _clientHeight);
        defer = DeferWindowPos(defer, _windowContainers[index].get(), nullptr, bounds.left, bounds.top,
                               bounds.right - bounds.left, bounds.bottom - bounds.top,
                               SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
        if (!defer)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }
    if (!EndDeferWindowPos(defer))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

LONG DashboardHost::HorizontalOffset() const noexcept
{
    return _horizontalOffset;
}

HRESULT DashboardHost::ApplyRaisedNativeLayout(size_t widgetIndex, const RECT& content, UINT dpi) noexcept
{
    if (!_pluginManager || dpi == 0 || widgetIndex >= _widgetCount || content.right <= content.left ||
        content.bottom <= content.top)
    {
        return E_INVALIDARG;
    }
    if (_raisedNativeIndex != SIZE_MAX && _raisedNativeIndex != widgetIndex)
    {
        const HRESULT clearResult = ClearRaisedNativeLayout(dpi);
        if (FAILED(clearResult))
        {
            return clearResult;
        }
    }

    for (size_t index = 0; index < _widgetCount; ++index)
    {
        if (!_windowContainers[index])
        {
            continue;
        }
        if (index != widgetIndex)
        {
            continue;
        }

        IRedXeWindowWidget* widget = _pluginManager->WindowWidgetAt(index);
        if (!widget)
        {
            continue;
        }
        if (!SetWindowPos(_windowContainers[index].get(), HWND_TOP, content.left, content.top,
                          content.right - content.left, content.bottom - content.top, SWP_NOACTIVATE))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        const RedXeWindowWidgetSizeContext context{
            sizeof(RedXeWindowWidgetSizeContext),
            static_cast<uint32_t>(content.right - content.left),
            static_cast<uint32_t>(content.bottom - content.top),
            dpi,
        };
        const HRESULT result = widget->Resize(&context);
        if (FAILED(result))
        {
            return result;
        }
        if (_widgetsVisible)
        {
            ShowWindow(_windowContainers[index].get(), SW_SHOWNA);
        }
    }
    _raisedNativeIndex = widgetIndex;
    return S_OK;
}

HRESULT DashboardHost::ClearRaisedNativeLayout(UINT dpi) noexcept
{
    if (!_pluginManager)
    {
        return E_UNEXPECTED;
    }
    if (_raisedNativeIndex == SIZE_MAX)
    {
        return S_OK;
    }
    if (dpi == 0)
    {
        return E_INVALIDARG;
    }

    for (size_t index = 0; index < _widgetCount; ++index)
    {
        if (!_windowContainers[index])
        {
            continue;
        }
        IRedXeWindowWidget* widget = _pluginManager->WindowWidgetAt(index);
        if (!widget)
        {
            continue;
        }
        const RECT bounds = PixelBoundsAt(index, _clientWidth, _clientHeight);
        if (!SetWindowPos(_windowContainers[index].get(), nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
                          bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_NOZORDER))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        const RedXeWindowWidgetSizeContext context{
            sizeof(RedXeWindowWidgetSizeContext),
            static_cast<uint32_t>(std::max(1L, bounds.right - bounds.left)),
            static_cast<uint32_t>(std::max(1L, bounds.bottom - bounds.top)),
            dpi,
        };
        const HRESULT result = widget->Resize(&context);
        if (FAILED(result))
        {
            return result;
        }
        ShowWindow(_windowContainers[index].get(), _widgetsVisible ? SW_SHOWNA : SW_HIDE);
    }
    _raisedNativeIndex = SIZE_MAX;
    return S_OK;
}

HRESULT DashboardHost::SetWidgetsVisible(bool visible) noexcept
{
    if (!_pluginManager)
    {
        return E_UNEXPECTED;
    }
    if (_widgetsVisible == visible)
    {
        return S_OK;
    }

    size_t changedCount = 0;
    for (size_t index = 0; index < _widgetCount; ++index)
    {
        IRedXeWidget* widget = _pluginManager->WidgetAt(index);
        const HRESULT result = widget->SetVisible(visible ? TRUE : FALSE);
        if (FAILED(result))
        {
            for (size_t previous = 0; previous < changedCount; ++previous)
            {
                IRedXeWidget* previousWidget = _pluginManager->WidgetAt(previous);
                (void)previousWidget->SetVisible(_widgetsVisible ? TRUE : FALSE);
            }
            return result;
        }
        changedCount = index + 1;
    }

    for (size_t index = 0; index < _widgetCount; ++index)
    {
        if (_windowContainers[index])
        {
            ShowWindow(_windowContainers[index].get(), visible ? SW_SHOWNA : SW_HIDE);
        }
    }
    _widgetsVisible = visible;
    return S_OK;
}

void DashboardHost::Shutdown() noexcept
{
    if (!_pluginManager)
    {
        return;
    }

    (void)SetWidgetsVisible(false);
    _pluginManager->SetUiInvalidateTarget(nullptr);
    for (size_t index = _widgetCount; index > 0; --index)
    {
        const size_t widgetIndex = index - 1;
        IRedXeWindowWidget* widget = _pluginManager->WindowWidgetAt(widgetIndex);
        if (widget)
        {
            widget->Detach();
        }
        _windowContainers[widgetIndex].reset();
    }

    _pluginManager = nullptr;
    _widgetCount = 0;
    _gridColumns = 0;
    _gridRows = 0;
    _requiresContinuousFrames = false;
    _widgetsVisible = false;
    _horizontalOffset = 0;
    _clientWidth = 0;
    _clientHeight = 0;
    _raisedNativeIndex = SIZE_MAX;
}

size_t DashboardHost::WidgetCount() const noexcept
{
    return _widgetCount;
}

IRedXeWidget* DashboardHost::WidgetAt(size_t index) const noexcept
{
    return _pluginManager && index < _widgetCount ? _pluginManager->WidgetAt(index) : nullptr;
}

IRedXeGpuWidget* DashboardHost::GpuWidgetAt(size_t index) const noexcept
{
    return _pluginManager && index < _widgetCount ? _pluginManager->GpuWidgetAt(index) : nullptr;
}

IRedXeWindowWidget* DashboardHost::WindowWidgetAt(size_t index) const noexcept
{
    return _pluginManager && index < _widgetCount ? _pluginManager->WindowWidgetAt(index) : nullptr;
}

IRedXeRaisedWidget* DashboardHost::RaisedWidgetAt(size_t index) const noexcept
{
    return _pluginManager && index < _widgetCount ? _pluginManager->RaisedWidgetAt(index) : nullptr;
}

size_t DashboardHost::RaisedNativeIndex() const noexcept
{
    return _raisedNativeIndex;
}

WidgetPlacement DashboardHost::PlacementAt(size_t index) const noexcept
{
    return index < _widgetCount ? _placements[index] : WidgetPlacement{};
}

RECT DashboardHost::PixelBoundsAt(size_t index, UINT width, UINT height) const noexcept
{
    if (index >= _widgetCount || width == 0 || height == 0)
    {
        return RECT{};
    }
    RECT bounds = _usesAdaptivePlacement[index]
                      ? ToAdaptivePixelBounds(_adaptivePlacements[index], width, height)
                      : (_gridColumns != 0 && _gridRows != 0
                             ? ToPixelBounds(_gridPlacements[index], _gridColumns, _gridRows, width, height)
                             : RECT{});
    bounds.left += _horizontalOffset;
    bounds.right += _horizontalOffset;
    return bounds;
}

bool DashboardHost::RequiresContinuousFrames() const noexcept
{
    return _requiresContinuousFrames;
}

HRESULT DashboardHost::GetNextFrameDelayMilliseconds(uint32_t* delayMilliseconds) const noexcept
{
    if (!delayMilliseconds)
    {
        return E_POINTER;
    }
    *delayMilliseconds = 0;
    if (!_pluginManager)
    {
        return S_FALSE;
    }

    uint32_t earliest = kRedXeMaximumScheduledFrameDelayMilliseconds;
    bool found = false;
    for (size_t index = 0; index < _widgetCount; ++index)
    {
        IRedXeScheduledWidget* scheduledWidget = _pluginManager->ScheduledWidgetAt(index);
        if (!scheduledWidget)
        {
            continue;
        }

        uint32_t candidate = 0;
        const HRESULT result = scheduledWidget->GetNextFrameDelayMilliseconds(&candidate);
        if (result != S_OK || candidate == 0 || candidate > kRedXeMaximumScheduledFrameDelayMilliseconds)
        {
            continue;
        }
        earliest = candidate < earliest ? candidate : earliest;
        found = true;
    }

    if (!found)
    {
        return S_FALSE;
    }
    *delayMilliseconds = earliest;
    return S_OK;
}
