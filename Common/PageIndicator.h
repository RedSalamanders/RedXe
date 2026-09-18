#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// The page control shared by every paged widget: a strip of dots, one per page, the current page's dot larger and
// brighter. The DIP metrics are those of DxUi::PageIndicator (20 DIP strip, 3 / 4 DIP dot radii, 14 DIP centre
// gap) so the GPU-drawn dots in Launcher, the System Data viewers, and Weather read the same as the DxUi control
// AV Control hosts. This header owns geometry, colours, and hit testing only; each widget draws the dots with its
// own primitives from one RedXePageIndicatorLayout and hit-tests taps against the same layout, so what is drawn is
// what is tapped. Pure value types: no allocation, no Win32.

inline constexpr float kRedXePageIndicatorHeightDip = 20.0f;
inline constexpr float kRedXePageIndicatorDotRadiusDip = 3.0f;
inline constexpr float kRedXePageIndicatorSelectedRadiusDip = 4.0f;
inline constexpr float kRedXePageIndicatorDotGapDip = 14.0f;
// A wider strip than this draws no dots: no bundled widget pages beyond it (Launcher caps at 32 shortcuts).
inline constexpr uint32_t kRedXePageIndicatorMaximumPages = 32;

// Dot colours, straight RGB in the 0..1 space the bundled shaders blend in.
inline constexpr float kRedXePageIndicatorSelectedRed = 0.92f;
inline constexpr float kRedXePageIndicatorSelectedGreen = 0.94f;
inline constexpr float kRedXePageIndicatorSelectedBlue = 1.00f;
inline constexpr float kRedXePageIndicatorDotRed = 0.52f;
inline constexpr float kRedXePageIndicatorDotGreen = 0.54f;
inline constexpr float kRedXePageIndicatorDotBlue = 0.60f;

enum class RedXePageIndicatorAlign : uint8_t
{
    Center = 0,
    Right = 1,
};

[[nodiscard]] inline float RedXePageIndicatorDipToPixels(float dip, uint32_t dpi) noexcept
{
    return dip * static_cast<float>(dpi == 0 ? 96U : dpi) / 96.0f;
}

[[nodiscard]] inline float RedXePageIndicatorHeightPixels(uint32_t dpi) noexcept
{
    return RedXePageIndicatorDipToPixels(kRedXePageIndicatorHeightDip, dpi);
}

struct RedXePageIndicatorLayout final
{
    // 0 when there is nothing to draw: fewer than two pages, no room, or more than the maximum.
    uint32_t pageCount = 0;
    uint32_t selected = 0;
    float stripTop = 0.0f;
    float stripHeight = 0.0f;
    float centerY = 0.0f;
    float firstCenterX = 0.0f;
    float gap = 0.0f;
    float radius = 0.0f;
    float selectedRadius = 0.0f;

    [[nodiscard]] float CenterX(uint32_t index) const noexcept
    {
        return firstCenterX + gap * static_cast<float>(index);
    }
    // Horizontal extent of the dot block, first dot's left edge to last dot's right edge.
    [[nodiscard]] float Width() const noexcept
    {
        return pageCount < 2 ? 0.0f : gap * static_cast<float>(pageCount - 1) + selectedRadius * 2.0f;
    }
    [[nodiscard]] float Left() const noexcept
    {
        return firstCenterX - selectedRadius;
    }
};

// Extent of a dot block for pageCount pages at dpi with the nominal gap; what a footer must leave free.
[[nodiscard]] inline float RedXePageIndicatorWidthPixels(uint32_t pageCount, uint32_t dpi) noexcept
{
    if (pageCount < 2 || pageCount > kRedXePageIndicatorMaximumPages)
    {
        return 0.0f;
    }
    return RedXePageIndicatorDipToPixels(kRedXePageIndicatorDotGapDip, dpi) * static_cast<float>(pageCount - 1) +
           RedXePageIndicatorDipToPixels(kRedXePageIndicatorSelectedRadiusDip, dpi) * 2.0f;
}

// Lays the dots out inside a strip rectangle, vertically centred, centred or right-aligned horizontally. When the
// nominal gap would push the block past the strip, the gap shrinks so every dot stays inside (never below two
// selected radii, so dots stay distinct); the hit test below uses the same gap.
[[nodiscard]] inline RedXePageIndicatorLayout RedXePageIndicatorInStrip(float stripX, float stripY, float stripWidth,
                                                                        float stripHeight, uint32_t dpi,
                                                                        uint32_t pageCount, uint32_t selected,
                                                                        RedXePageIndicatorAlign align) noexcept
{
    RedXePageIndicatorLayout layout{};
    if (pageCount < 2 || pageCount > kRedXePageIndicatorMaximumPages || stripWidth <= 0.0f || stripHeight <= 0.0f ||
        !std::isfinite(stripX) || !std::isfinite(stripY) || !std::isfinite(stripWidth) || !std::isfinite(stripHeight))
    {
        return layout;
    }
    layout.pageCount = pageCount;
    layout.selected = selected < pageCount ? selected : pageCount - 1;
    layout.stripTop = stripY;
    layout.stripHeight = stripHeight;
    layout.centerY = stripY + stripHeight * 0.5f;
    layout.radius = RedXePageIndicatorDipToPixels(kRedXePageIndicatorDotRadiusDip, dpi);
    layout.selectedRadius = RedXePageIndicatorDipToPixels(kRedXePageIndicatorSelectedRadiusDip, dpi);
    const float nominalGap = RedXePageIndicatorDipToPixels(kRedXePageIndicatorDotGapDip, dpi);
    const float room = std::max(0.0f, stripWidth - layout.selectedRadius * 2.0f);
    const float fittedGap = room / static_cast<float>(pageCount - 1);
    layout.gap = std::max(layout.selectedRadius * 2.0f, std::min(nominalGap, fittedGap));
    const float width = layout.Width();
    if (align == RedXePageIndicatorAlign::Right)
    {
        layout.firstCenterX = stripX + stripWidth - width + layout.selectedRadius;
    }
    else
    {
        layout.firstCenterX = stripX + (stripWidth - width) * 0.5f + layout.selectedRadius;
    }
    return layout;
}

// Page whose dot is under (x, y), or UINT32_MAX. The whole strip height is a target; horizontally the nearest dot
// within half a gap (at least two selected radii) wins, so a fat finger on the strip still lands on a page.
[[nodiscard]] inline uint32_t RedXePageIndicatorHit(const RedXePageIndicatorLayout& layout, float x, float y) noexcept
{
    if (layout.pageCount < 2 || !std::isfinite(x) || !std::isfinite(y))
    {
        return UINT32_MAX;
    }
    if (y < layout.stripTop || y > layout.stripTop + layout.stripHeight)
    {
        return UINT32_MAX;
    }
    const float limit = std::max(layout.gap * 0.5f, layout.selectedRadius * 2.0f);
    uint32_t hit = UINT32_MAX;
    float best = limit;
    for (uint32_t index = 0; index < layout.pageCount; ++index)
    {
        const float distance = std::fabs(x - layout.CenterX(index));
        if (distance <= best)
        {
            best = distance;
            hit = index;
        }
    }
    return hit;
}
