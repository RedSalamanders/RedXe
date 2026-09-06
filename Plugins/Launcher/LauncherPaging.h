#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <windows.h>

// DIP metrics match DxUi::PageIndicator so GPU-drawn launcher dots align with the shared control.
inline constexpr float kLauncherMinCellDip = 72.0f;
inline constexpr float kLauncherPageIndicatorHeightDip = 20.0f;
inline constexpr float kLauncherPageIndicatorDotRadiusDip = 3.0f;
inline constexpr float kLauncherPageIndicatorSelectedRadiusDip = 4.0f;
inline constexpr float kLauncherPageIndicatorDotGapDip = 14.0f;

struct LauncherPageGeometry final
{
    uint32_t columns = 1;
    uint32_t rows = 1;
    uint32_t perPage = 1;
    uint32_t pageCount = 1;
    uint32_t pageIndex = 0;
    uint32_t firstIndex = 0;
    uint32_t visibleCount = 0;
    float indicatorHeightPx = 0.0f;
    float contentHeightPx = 0.0f;
};

[[nodiscard]] inline float LauncherDipToPixels(float dip, UINT dpi) noexcept
{
    const float scale = static_cast<float>(dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi) / 96.0f;
    return dip * scale;
}

[[nodiscard]] inline LauncherPageGeometry ComputeLauncherPages(uint32_t widthPx, uint32_t heightPx, UINT dpi,
                                                               uint32_t shortcutCount, uint32_t requestedPage) noexcept
{
    LauncherPageGeometry geometry{};
    geometry.contentHeightPx = static_cast<float>(heightPx);
    if (shortcutCount == 0 || widthPx == 0 || heightPx == 0)
    {
        geometry.visibleCount = shortcutCount;
        return geometry;
    }

    const float minCell = std::max(1.0f, LauncherDipToPixels(kLauncherMinCellDip, dpi));
    const uint32_t columnsFit = std::max(1U, static_cast<uint32_t>(static_cast<float>(widthPx) / minCell));
    const uint32_t rowsFit = std::max(1U, static_cast<uint32_t>(static_cast<float>(heightPx) / minCell));
    if (shortcutCount <= columnsFit * rowsFit)
    {
        geometry.perPage = shortcutCount;
        geometry.visibleCount = shortcutCount;
        geometry.pageIndex = 0;
        return geometry;
    }

    geometry.indicatorHeightPx = LauncherDipToPixels(kLauncherPageIndicatorHeightDip, dpi);
    geometry.contentHeightPx = std::max(minCell, static_cast<float>(heightPx) - geometry.indicatorHeightPx);
    geometry.columns = std::max(1U, static_cast<uint32_t>(static_cast<float>(widthPx) / minCell));
    geometry.rows = std::max(1U, static_cast<uint32_t>(geometry.contentHeightPx / minCell));
    geometry.perPage = std::max(1U, geometry.columns * geometry.rows);
    geometry.pageCount = (shortcutCount + geometry.perPage - 1U) / geometry.perPage;
    if (geometry.pageCount == 0)
    {
        geometry.pageCount = 1;
    }
    geometry.pageIndex = (std::min)(requestedPage, geometry.pageCount - 1);
    geometry.firstIndex = geometry.pageIndex * geometry.perPage;
    geometry.visibleCount = (std::min)(geometry.perPage, shortcutCount - geometry.firstIndex);
    return geometry;
}

[[nodiscard]] inline uint32_t HitLauncherPageDot(float x, float y, uint32_t widthPx, uint32_t heightPx, UINT dpi,
                                                 uint32_t pageCount) noexcept
{
    if (pageCount < 2 || widthPx == 0 || heightPx == 0)
    {
        return UINT32_MAX;
    }
    const float radius = LauncherDipToPixels(kLauncherPageIndicatorSelectedRadiusDip, dpi);
    const float gap = LauncherDipToPixels(kLauncherPageIndicatorDotGapDip, dpi);
    const float strip = LauncherDipToPixels(kLauncherPageIndicatorHeightDip, dpi);
    const float originY = static_cast<float>(heightPx) - strip * 0.5f;
    if (y < originY - strip * 0.5f || y > static_cast<float>(heightPx))
    {
        return UINT32_MAX;
    }
    const float total = gap * static_cast<float>(pageCount - 1);
    const float originX = static_cast<float>(widthPx) * 0.5f - total * 0.5f;
    const float limit = gap * 0.5f;
    uint32_t hit = UINT32_MAX;
    float best = limit;
    for (uint32_t index = 0; index < pageCount; ++index)
    {
        const float cx = originX + gap * static_cast<float>(index);
        const float dx = x - cx;
        const float dy = y - originY;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= (std::max)(limit, radius * 2.0f) && distance <= best)
        {
            best = distance;
            hit = index;
        }
    }
    return hit;
}

[[nodiscard]] inline LONG LauncherPageSwipeThresholdPixels(UINT dpi) noexcept
{
    const int scaleDpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : static_cast<int>(dpi);
    return MulDiv(16, scaleDpi, USER_DEFAULT_SCREEN_DPI);
}

[[nodiscard]] constexpr bool LauncherPageSwipeLocksHorizontal(LONG deltaX, LONG deltaY, LONG threshold) noexcept
{
    const LONG absX = deltaX < 0 ? -deltaX : deltaX;
    const LONG absY = deltaY < 0 ? -deltaY : deltaY;
    return absX >= threshold && absX > absY;
}
