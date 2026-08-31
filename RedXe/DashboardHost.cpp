#include "DashboardHost.h"

#include <array>
#include <cstdint>

namespace
{
constexpr float kDesignWidth = 2560.0f;
constexpr float kDesignHeight = 720.0f;

[[nodiscard]] WidgetPlacement ToDesignPlacement(const WidgetGridPlacement& placement, std::uint32_t columns,
                                                std::uint32_t rows) noexcept
{
    const float left = kDesignWidth * static_cast<float>(placement.column) / static_cast<float>(columns);
    const float top = kDesignHeight * static_cast<float>(placement.row) / static_cast<float>(rows);
    const float right =
        kDesignWidth * static_cast<float>(placement.column + placement.columnSpan) / static_cast<float>(columns);
    const float bottom =
        kDesignHeight * static_cast<float>(placement.row + placement.rowSpan) / static_cast<float>(rows);
    return WidgetPlacement{left, top, right - left, bottom - top};
}

[[nodiscard]] LONG RoundGridEdge(std::uint32_t cell, UINT extent, std::uint32_t divisions) noexcept
{
    const std::uint64_t doubled = static_cast<std::uint64_t>(cell) * extent * 2U;
    return static_cast<LONG>((doubled + divisions) / (static_cast<std::uint64_t>(divisions) * 2U));
}

[[nodiscard]] RECT ToPixelBounds(const WidgetGridPlacement& placement, std::uint32_t columns, std::uint32_t rows,
                                 UINT width, UINT height) noexcept
{
    return RECT{
        RoundGridEdge(placement.column, width, columns),
        RoundGridEdge(placement.row, height, rows),
        RoundGridEdge(placement.column + placement.columnSpan, width, columns),
        RoundGridEdge(placement.row + placement.rowSpan, height, rows),
    };
}
} // namespace

DashboardHost::~DashboardHost()
{
    Shutdown();
}

HRESULT DashboardHost::Initialize(PluginManager& pluginManager, HWND parent, UINT width, UINT height, UINT dpi,
                                  bool visible) noexcept
{
    if (_pluginManager || !parent || width == 0 || height == 0 || dpi == 0 || pluginManager.WidgetCount() == 0 ||
        pluginManager.WidgetCount() > PluginManager::kMaximumWidgetInstances)
    {
        return E_INVALIDARG;
    }

    const auto widgetCount = static_cast<std::uint32_t>(pluginManager.WidgetCount());
    const std::uint32_t columns = pluginManager.GridColumns();
    const std::uint32_t rows = pluginManager.GridRows();
    if (columns == 0 || rows == 0 || columns > kMaximumDashboardGridDimension || rows > kMaximumDashboardGridDimension)
    {
        return E_INVALIDARG;
    }

    std::array<WidgetPlacement, PluginManager::kMaximumWidgetInstances> placements{};
    std::array<WidgetGridPlacement, PluginManager::kMaximumWidgetInstances> gridPlacements{};
    std::array<wil::unique_hwnd, PluginManager::kMaximumWidgetInstances> containers;
    bool continuous = false;
    for (std::uint32_t index = 0; index < widgetCount; ++index)
    {
        IRedXeGpuWidget* gpuWidget = pluginManager.GpuWidgetAt(index);
        IRedXeWindowWidget* windowWidget = pluginManager.WindowWidgetAt(index);
        if (!gpuWidget && !windowWidget)
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }

        const WidgetGridPlacement gridPlacement = pluginManager.WidgetGridPlacementAt(index);
        if (gridPlacement.column >= columns || gridPlacement.row >= rows || gridPlacement.columnSpan == 0 ||
            gridPlacement.rowSpan == 0 || gridPlacement.columnSpan > columns - gridPlacement.column ||
            gridPlacement.rowSpan > rows - gridPlacement.row)
        {
            return E_INVALIDARG;
        }
        gridPlacements[index] = gridPlacement;
        placements[index] = ToDesignPlacement(gridPlacement, columns, rows);
        continuous = continuous || (pluginManager.WidgetFlagsAt(index) & RedXeWidgetFlagContinuousAnimation) != 0;

        if (!windowWidget)
        {
            continue;
        }

        const RECT bounds = ToPixelBounds(gridPlacement, columns, rows, width, height);
        const HWND container = CreateWindowExW(
            WS_EX_NOPARENTNOTIFY, L"STATIC", L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, bounds.left, bounds.top,
            bounds.right - bounds.left, bounds.bottom - bounds.top, parent, nullptr, nullptr, nullptr);
        if (!container)
        {
            const HRESULT result = HRESULT_FROM_WIN32(GetLastError());
            for (std::uint32_t previous = 0; previous < index; ++previous)
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
            static_cast<std::uint32_t>(bounds.right - bounds.left),
            static_cast<std::uint32_t>(bounds.bottom - bounds.top),
            dpi,
        };
        HRESULT result = windowWidget->Attach(&context);
        if (SUCCEEDED(result))
        {
            result = windowWidget->SetVisible(visible ? TRUE : FALSE);
        }
        if (FAILED(result))
        {
            windowWidget->Detach();
            for (std::uint32_t previous = 0; previous < index; ++previous)
            {
                if (containers[previous])
                {
                    pluginManager.WindowWidgetAt(previous)->Detach();
                }
            }
            return result;
        }
        if (visible)
        {
            ShowWindow(container, SW_SHOWNA);
        }
    }

    _pluginManager = &pluginManager;
    _placements = placements;
    _gridPlacements = gridPlacements;
    _windowContainers = std::move(containers);
    _widgetCount = widgetCount;
    _gridColumns = columns;
    _gridRows = rows;
    _requiresContinuousFrames = continuous;
    _windowWidgetsVisible = visible;
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
        return SetWindowWidgetsVisible(false);
    }

    for (std::size_t index = 0; index < _widgetCount; ++index)
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
            static_cast<std::uint32_t>(bounds.right - bounds.left),
            static_cast<std::uint32_t>(bounds.bottom - bounds.top),
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

HRESULT DashboardHost::SetWindowWidgetsVisible(bool visible) noexcept
{
    if (!_pluginManager)
    {
        return E_UNEXPECTED;
    }
    if (_windowWidgetsVisible == visible)
    {
        return S_OK;
    }

    std::size_t changedCount = 0;
    for (std::size_t index = 0; index < _widgetCount; ++index)
    {
        IRedXeWindowWidget* widget = _pluginManager->WindowWidgetAt(index);
        if (!widget)
        {
            continue;
        }

        const HRESULT result = widget->SetVisible(visible ? TRUE : FALSE);
        if (FAILED(result))
        {
            for (std::size_t previous = 0; previous < changedCount; ++previous)
            {
                IRedXeWindowWidget* previousWidget = _pluginManager->WindowWidgetAt(previous);
                if (previousWidget)
                {
                    (void)previousWidget->SetVisible(_windowWidgetsVisible ? TRUE : FALSE);
                }
            }
            return result;
        }
        changedCount = index + 1;
    }

    for (std::size_t index = 0; index < _widgetCount; ++index)
    {
        if (_windowContainers[index])
        {
            ShowWindow(_windowContainers[index].get(), visible ? SW_SHOWNA : SW_HIDE);
        }
    }
    _windowWidgetsVisible = visible;
    return S_OK;
}

void DashboardHost::Shutdown() noexcept
{
    if (!_pluginManager)
    {
        return;
    }

    (void)SetWindowWidgetsVisible(false);
    for (std::size_t index = _widgetCount; index > 0; --index)
    {
        const std::size_t widgetIndex = index - 1;
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
    _windowWidgetsVisible = false;
}

std::size_t DashboardHost::WidgetCount() const noexcept
{
    return _widgetCount;
}

IRedXeWidget* DashboardHost::WidgetAt(std::size_t index) const noexcept
{
    return _pluginManager && index < _widgetCount ? _pluginManager->WidgetAt(index) : nullptr;
}

IRedXeGpuWidget* DashboardHost::GpuWidgetAt(std::size_t index) const noexcept
{
    return _pluginManager && index < _widgetCount ? _pluginManager->GpuWidgetAt(index) : nullptr;
}

IRedXeWindowWidget* DashboardHost::WindowWidgetAt(std::size_t index) const noexcept
{
    return _pluginManager && index < _widgetCount ? _pluginManager->WindowWidgetAt(index) : nullptr;
}

WidgetPlacement DashboardHost::PlacementAt(std::size_t index) const noexcept
{
    return index < _widgetCount ? _placements[index] : WidgetPlacement{};
}

RECT DashboardHost::PixelBoundsAt(std::size_t index, UINT width, UINT height) const noexcept
{
    return index < _widgetCount && width != 0 && height != 0 && _gridColumns != 0 && _gridRows != 0
               ? ToPixelBounds(_gridPlacements[index], _gridColumns, _gridRows, width, height)
               : RECT{};
}

bool DashboardHost::RequiresContinuousFrames() const noexcept
{
    return _requiresContinuousFrames;
}
