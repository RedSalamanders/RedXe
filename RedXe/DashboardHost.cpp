#include "DashboardHost.h"

#include <cstdint>

namespace
{
constexpr float kDesignWidth = 2560.0f;
constexpr float kDesignHeight = 720.0f;
constexpr float kOuterMargin = 40.0f;
constexpr float kGap = 24.0f;
constexpr std::uint32_t kColumns = 2;
} // namespace

HRESULT DashboardHost::Initialize(PluginManager& pluginManager) noexcept
{
    if (_pluginManager || pluginManager.WidgetCount() < 2 ||
        pluginManager.WidgetCount() > PluginManager::kMaximumWidgetInstances)
    {
        return E_INVALIDARG;
    }

    const auto widgetCount = static_cast<std::uint32_t>(pluginManager.WidgetCount());
    const std::uint32_t rows = (widgetCount + kColumns - 1U) / kColumns;
    const float cellWidth =
        (kDesignWidth - 2.0f * kOuterMargin - kGap * static_cast<float>(kColumns - 1U)) / static_cast<float>(kColumns);
    const float cellHeight =
        (kDesignHeight - 2.0f * kOuterMargin - kGap * static_cast<float>(rows - 1U)) / static_cast<float>(rows);

    bool continuous = false;
    for (std::uint32_t index = 0; index < widgetCount; ++index)
    {
        if (!pluginManager.GpuWidgetAt(index))
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }
        _placements[index] = WidgetPlacement{
            kOuterMargin + static_cast<float>(index % kColumns) * (cellWidth + kGap),
            kOuterMargin + static_cast<float>(index / kColumns) * (cellHeight + kGap),
            cellWidth,
            cellHeight,
        };
        continuous = continuous || (pluginManager.WidgetFlagsAt(index) & RedXeWidgetFlagContinuousAnimation) != 0;
    }

    _pluginManager = &pluginManager;
    _widgetCount = widgetCount;
    _requiresContinuousFrames = continuous;
    return S_OK;
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

bool DashboardHost::RequiresContinuousFrames() const noexcept
{
    return _requiresContinuousFrames;
}
