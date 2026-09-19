#pragma once

#include "PageIndicator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <windows.h>

// The page-dot strip is the shared page control (Common/PageIndicator.h): DxUi::PageIndicator metrics, drawn by the
// launcher shader from the layout Launcher computes on the CPU.
inline constexpr float kLauncherSmallCellDip = 72.0f;
inline constexpr float kLauncherMediumCellDip = 96.0f;
inline constexpr float kLauncherLargeCellDip = 144.0f;
inline constexpr float kLauncherHugeCellDip = 192.0f;
inline constexpr float kLauncherAutomaticFloorDip = kLauncherSmallCellDip;
inline constexpr float kLauncherPageIndicatorHeightDip = kRedXePageIndicatorHeightDip;
inline constexpr float kLauncherPageIndicatorDotRadiusDip = kRedXePageIndicatorDotRadiusDip;
inline constexpr float kLauncherPageIndicatorSelectedRadiusDip = kRedXePageIndicatorSelectedRadiusDip;
inline constexpr float kLauncherPageIndicatorDotGapDip = kRedXePageIndicatorDotGapDip;
inline constexpr float kLauncherIconInnerGutterDip = 4.0f;
// Minimum gap between neighboring icon edges, DPI-scaled. Matching inset from the tile edge and page-dot strip.
inline constexpr float kLauncherMinGutterDip = 8.0f;
inline constexpr float kLauncherEdgeInsetDip = 8.0f;
inline constexpr float kLauncherMaxIconDip = 256.0f;
inline constexpr uint32_t kLauncherMaximumShortcuts = 32;

enum class LauncherIconSize : uint8_t
{
    Small,
    Medium,
    Large,
    Huge,
    Automatic,
};

[[nodiscard]] constexpr bool LauncherIconSizeIsAutomatic(LauncherIconSize iconSize) noexcept
{
    return iconSize == LauncherIconSize::Automatic;
}

[[nodiscard]] constexpr float LauncherIconSizeCellDip(LauncherIconSize iconSize) noexcept
{
    switch (iconSize)
    {
    case LauncherIconSize::Small:
        return kLauncherSmallCellDip;
    case LauncherIconSize::Medium:
        return kLauncherMediumCellDip;
    case LauncherIconSize::Large:
        return kLauncherLargeCellDip;
    case LauncherIconSize::Huge:
    case LauncherIconSize::Automatic:
        return kLauncherHugeCellDip;
    }
    return kLauncherHugeCellDip;
}

[[nodiscard]] constexpr const char* LauncherIconSizeName(LauncherIconSize iconSize) noexcept
{
    switch (iconSize)
    {
    case LauncherIconSize::Small:
        return "small";
    case LauncherIconSize::Medium:
        return "medium";
    case LauncherIconSize::Large:
        return "large";
    case LauncherIconSize::Automatic:
        return "automatic";
    case LauncherIconSize::Huge:
        break;
    }
    return "huge";
}

[[nodiscard]] constexpr bool TryParseLauncherIconSize(std::string_view text, LauncherIconSize& iconSize) noexcept
{
    if (text == "small")
    {
        iconSize = LauncherIconSize::Small;
        return true;
    }
    if (text == "medium")
    {
        iconSize = LauncherIconSize::Medium;
        return true;
    }
    if (text == "large")
    {
        iconSize = LauncherIconSize::Large;
        return true;
    }
    if (text == "huge")
    {
        iconSize = LauncherIconSize::Huge;
        return true;
    }
    if (text == "automatic")
    {
        iconSize = LauncherIconSize::Automatic;
        return true;
    }
    return false;
}

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
    float cellSizePx = 0.0f;
    float iconSizePx = 0.0f;
};

[[nodiscard]] inline float LauncherDipToPixels(float dip, UINT dpi) noexcept
{
    const float scale = static_cast<float>(dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi) / 96.0f;
    return dip * scale;
}

[[nodiscard]] inline float LauncherEvenGutterPixels(UINT dpi) noexcept
{
    return std::max(LauncherDipToPixels(kLauncherMinGutterDip, dpi), LauncherDipToPixels(kLauncherEdgeInsetDip, dpi));
}

[[nodiscard]] inline uint32_t LauncherCellsAlong(float extentPx, float cellPx, float gutterPx) noexcept
{
    if (extentPx < 1.0f || cellPx < 1.0f)
    {
        return 1;
    }
    const float gutter = std::max(0.0f, gutterPx);
    const float denom = cellPx + gutter;
    const float usable = extentPx - gutter;
    if (denom < 1.0f || usable < cellPx)
    {
        return 1;
    }
    // A cell sized to fill the extent exactly (LauncherEvenSlotLimit) must count as fitting despite float rounding.
    return std::max(1U, static_cast<uint32_t>(usable / denom + 1.0e-3f));
}

[[nodiscard]] inline float LauncherEvenSlotLimit(float extentPx, uint32_t count, float gutterPx) noexcept
{
    count = std::max(1U, count);
    const float gutter = std::max(0.0f, gutterPx);
    return (extentPx - gutter * static_cast<float>(count + 1U)) / static_cast<float>(count);
}

inline void LauncherChooseSpreadGrid(uint32_t widthPx, uint32_t heightPx, uint32_t shortcutCount, uint32_t maxColumns,
                                     uint32_t maxRows, uint32_t& columns, uint32_t& rows) noexcept
{
    const uint32_t count = std::max(1U, shortcutCount);
    maxColumns = std::max(1U, std::min(maxColumns, count));
    maxRows = std::max(1U, maxRows);
    columns = 1;
    rows = count;
    float bestScore = 3.4e38f;
    for (uint32_t candidateCols = 1; candidateCols <= maxColumns; ++candidateCols)
    {
        const uint32_t candidateRows = (count + candidateCols - 1U) / candidateCols;
        if (candidateRows > maxRows)
        {
            continue;
        }
        const float slotW = static_cast<float>(widthPx) / static_cast<float>(candidateCols);
        const float slotH = static_cast<float>(heightPx) / static_cast<float>(candidateRows);
        const float aspectPenalty = std::fabs(slotW - slotH);
        const uint32_t empty = candidateCols * candidateRows - count;
        const float score = aspectPenalty + static_cast<float>(empty) * 0.25f * std::min(slotW, slotH);
        if (score < bestScore)
        {
            bestScore = score;
            columns = candidateCols;
            rows = candidateRows;
        }
    }
    if (rows > maxRows)
    {
        columns = maxColumns;
        rows = std::min(maxRows, std::max(1U, (count + columns - 1U) / columns));
    }
}

// The grid `automatic` settles on: the cell it draws, the columns and rows of one page, and the page count.
struct LauncherAutomaticGrid final
{
    float cellPx = 0.0f;
    uint32_t columns = 1;
    uint32_t rows = 1;
    uint32_t pageCount = 1;
};

// `automatic` weighs icon size against page count instead of insisting on either. Every whole grid of columns x
// rows is a candidate, with the cell as large as the tile allows for that grid (at most the huge cell, never below
// the 72 DIP floor; a grid that pages gives up the dot strip), scored by cell edge divided by page count: a second
// page has to buy icons twice the edge of one page's, a third three times. Eight shortcuts on a wide tile become one
// page of near-huge icons rather than four pages of two huge ones; nine on a narrow strip two pages of 99 DIP icons
// rather than nine pages of one. Ties (the cell capped at huge) go to the fewest pages, then the fewest cells per
// page, which keeps the last page as full as the others. When not even one floor cell fits, the floor cell pages one
// shortcut at a time and the draw shrinks it to the tile.
[[nodiscard]] inline LauncherAutomaticGrid LauncherChooseAutomaticGrid(uint32_t widthPx, uint32_t heightPx, UINT dpi,
                                                                       uint32_t shortcutCount) noexcept
{
    const float gutter = LauncherEvenGutterPixels(dpi);
    const float width = static_cast<float>(widthPx);
    const float height = static_cast<float>(heightPx);
    const float insetLimit = std::max(1.0f, std::min(width - 2.0f * gutter, height - 2.0f * gutter));
    const float floorPx = std::max(1.0f, LauncherDipToPixels(kLauncherAutomaticFloorDip, dpi));
    const float hugePx = std::min(std::max(1.0f, LauncherDipToPixels(kLauncherHugeCellDip, dpi)), insetLimit);
    const float indicatorPx = LauncherDipToPixels(kLauncherPageIndicatorHeightDip, dpi);
    LauncherAutomaticGrid best{};
    if (shortcutCount == 0)
    {
        best.cellPx = hugePx;
        return best;
    }
    const uint32_t count = std::min(shortcutCount, kLauncherMaximumShortcuts);
    best.cellPx = std::min(floorPx, insetLimit);
    best.pageCount = count;
    float bestScore = 0.0f;
    for (uint32_t columns = 1; columns <= count; ++columns)
    {
        const uint32_t rowsForOnePage = (count + columns - 1U) / columns;
        for (uint32_t rows = 1; rows <= rowsForOnePage; ++rows)
        {
            const uint32_t perPage = columns * rows;
            const uint32_t pages = (count + perPage - 1U) / perPage;
            const float contentHeight = pages > 1 ? std::max(1.0f, height - indicatorPx) : height;
            const float cell = std::min(hugePx, std::min(LauncherEvenSlotLimit(width, columns, gutter),
                                                         LauncherEvenSlotLimit(contentHeight, rows, gutter)));
            if (cell + 0.01f < floorPx)
            {
                continue;
            }
            const float score = cell / static_cast<float>(pages);
            const bool tie = std::fabs(score - bestScore) <= 0.01f;
            const bool better =
                score > bestScore + 0.01f ||
                (tie && (pages < best.pageCount || (pages == best.pageCount && perPage < best.columns * best.rows)));
            if (better)
            {
                best.cellPx = cell;
                best.columns = columns;
                best.rows = rows;
                best.pageCount = pages;
                bestScore = score;
            }
        }
    }
    return best;
}

[[nodiscard]] inline float LauncherChosenCellPixels(uint32_t widthPx, uint32_t heightPx, UINT dpi,
                                                    uint32_t shortcutCount, LauncherIconSize iconSize) noexcept
{
    if (LauncherIconSizeIsAutomatic(iconSize))
    {
        return LauncherChooseAutomaticGrid(widthPx, heightPx, dpi, shortcutCount).cellPx;
    }
    const float gutter = LauncherEvenGutterPixels(dpi);
    const float width = static_cast<float>(widthPx);
    const float height = static_cast<float>(heightPx);
    const float insetLimit = std::max(1.0f, std::min(width - 2.0f * gutter, height - 2.0f * gutter));
    const float desired = std::max(1.0f, LauncherDipToPixels(LauncherIconSizeCellDip(iconSize), dpi));
    return std::min(desired, insetLimit);
}

[[nodiscard]] inline LauncherPageGeometry ComputeLauncherPages(
    uint32_t widthPx, uint32_t heightPx, UINT dpi, uint32_t shortcutCount, uint32_t requestedPage,
    LauncherIconSize iconSize = LauncherIconSize::Huge) noexcept
{
    LauncherPageGeometry geometry{};
    geometry.contentHeightPx = static_cast<float>(heightPx);
    if (shortcutCount == 0 || widthPx == 0 || heightPx == 0)
    {
        geometry.visibleCount = shortcutCount;
        return geometry;
    }

    const float gutter = LauncherEvenGutterPixels(dpi);
    const bool automatic = LauncherIconSizeIsAutomatic(iconSize);
    const LauncherAutomaticGrid chosen =
        automatic ? LauncherChooseAutomaticGrid(widthPx, heightPx, dpi, shortcutCount) : LauncherAutomaticGrid{};
    const float cellPx =
        automatic ? chosen.cellPx : LauncherChosenCellPixels(widthPx, heightPx, dpi, shortcutCount, iconSize);
    geometry.cellSizePx = cellPx;
    geometry.iconSizePx = cellPx;
    uint32_t columnsFit = LauncherCellsAlong(static_cast<float>(widthPx), cellPx, gutter);
    uint32_t rowsFit = LauncherCellsAlong(static_cast<float>(heightPx), cellPx, gutter);
    if (automatic && chosen.pageCount <= 1)
    {
        columnsFit = std::max(columnsFit, chosen.columns);
        rowsFit = std::max(rowsFit, chosen.rows);
    }
    if (shortcutCount <= columnsFit * rowsFit && (!automatic || chosen.pageCount <= 1))
    {
        LauncherChooseSpreadGrid(widthPx, heightPx, shortcutCount, columnsFit, rowsFit, geometry.columns,
                                 geometry.rows);
        geometry.perPage = shortcutCount;
        geometry.visibleCount = shortcutCount;
        geometry.pageIndex = 0;
        geometry.pageCount = 1;
        return geometry;
    }

    geometry.indicatorHeightPx = LauncherDipToPixels(kLauncherPageIndicatorHeightDip, dpi);
    geometry.contentHeightPx = std::max(1.0f, static_cast<float>(heightPx) - geometry.indicatorHeightPx);
    if (automatic)
    {
        // The chosen grid already pays for the dot strip; a named size packs as many cells as fit beside it.
        geometry.columns = std::max(1U, chosen.columns);
        geometry.rows = std::max(1U, chosen.rows);
    }
    else
    {
        geometry.columns = columnsFit;
        geometry.rows = std::max(1U, LauncherCellsAlong(geometry.contentHeightPx, cellPx, gutter));
    }
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

// The launcher's dot strip: the shared page control laid along the bottom of the tile, centred.
[[nodiscard]] inline RedXePageIndicatorLayout LauncherPageDots(uint32_t widthPx, uint32_t heightPx, UINT dpi,
                                                               uint32_t pageCount, uint32_t pageIndex) noexcept
{
    if (widthPx == 0 || heightPx == 0)
    {
        return RedXePageIndicatorLayout{};
    }
    const float strip = LauncherDipToPixels(kLauncherPageIndicatorHeightDip, dpi);
    return RedXePageIndicatorInStrip(0.0f, static_cast<float>(heightPx) - strip, static_cast<float>(widthPx), strip,
                                     dpi, pageCount, pageIndex, RedXePageIndicatorAlign::Center);
}

[[nodiscard]] inline uint32_t HitLauncherPageDot(float x, float y, uint32_t widthPx, uint32_t heightPx, UINT dpi,
                                                 uint32_t pageCount) noexcept
{
    return RedXePageIndicatorHit(LauncherPageDots(widthPx, heightPx, dpi, pageCount, 0), x, y);
}

[[nodiscard]] inline LONG LauncherPageSwipeThresholdPixels(UINT dpi) noexcept
{
    const int scaleDpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : static_cast<int>(dpi);
    return MulDiv(16, scaleDpi, USER_DEFAULT_SCREEN_DPI);
}

[[nodiscard]] inline LONG LauncherPageSwipeFlickSpeedPixelsPerSecond(UINT dpi) noexcept
{
    const int scaleDpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : static_cast<int>(dpi);
    return MulDiv(2200, scaleDpi, USER_DEFAULT_SCREEN_DPI);
}

[[nodiscard]] constexpr bool LauncherPageSwipeLocksHorizontal(LONG deltaX, LONG deltaY, LONG threshold) noexcept
{
    const LONG absX = deltaX < 0 ? -deltaX : deltaX;
    const LONG absY = deltaY < 0 ? -deltaY : deltaY;
    return absX >= threshold && absX > absY;
}

[[nodiscard]] constexpr bool LauncherPageSwipeBlocksDirection(LONG offset, bool atFirst, bool atLast) noexcept
{
    if (offset == 0)
    {
        return false;
    }
    return (offset > 0 && atFirst) || (offset < 0 && atLast);
}

[[nodiscard]] inline LONG LauncherApplyPageEdgeResistance(LONG offset, LONG pageWidth, bool blocked) noexcept
{
    if (pageWidth <= 0)
    {
        return 0;
    }
    const LONG clamped = std::clamp(offset, -pageWidth, pageWidth);
    if (!blocked)
    {
        return clamped;
    }
    const LONG limit = std::max(pageWidth / 8, 1L);
    return std::clamp(clamped / 4, -limit, limit);
}

[[nodiscard]] constexpr LONG LauncherPageSwipeCommitDistance(LONG pageWidth) noexcept
{
    return pageWidth > 0 ? pageWidth / 4 : 0;
}

[[nodiscard]] inline bool LauncherShouldCommitPageSwipe(LONG offset, LONG pageWidth, float velocityPxPerSec, UINT dpi,
                                                        bool atFirst, bool atLast) noexcept
{
    if (pageWidth <= 0 || offset == 0 || LauncherPageSwipeBlocksDirection(offset, atFirst, atLast))
    {
        return false;
    }
    const LONG distance = offset < 0 ? -offset : offset;
    if (distance >= LauncherPageSwipeCommitDistance(pageWidth))
    {
        return true;
    }
    const float directedVelocity = offset < 0 ? -velocityPxPerSec : velocityPxPerSec;
    return directedVelocity >= static_cast<float>(LauncherPageSwipeFlickSpeedPixelsPerSecond(dpi));
}

[[nodiscard]] constexpr float LauncherEaseOutCubic(float t) noexcept
{
    if (t <= 0.0f)
    {
        return 0.0f;
    }
    if (t >= 1.0f)
    {
        return 1.0f;
    }
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

[[nodiscard]] inline LONG LauncherInterpolatePageOffset(LONG start, LONG target, float t) noexcept
{
    const float eased = LauncherEaseOutCubic(t);
    const float mixed = static_cast<float>(start) + static_cast<float>(target - start) * eased;
    return mixed >= 0.0f ? static_cast<LONG>(mixed + 0.5f) : static_cast<LONG>(mixed - 0.5f);
}

[[nodiscard]] inline UINT LauncherPageSettleDurationMilliseconds(LONG remaining, float velocityPxPerSec) noexcept
{
    const float distance = std::fabs(static_cast<float>(remaining));
    const float speed = std::max(std::fabs(velocityPxPerSec), 1400.0f);
    const float milliseconds = distance / speed * 1000.0f;
    const UINT rounded = static_cast<UINT>(milliseconds + 0.5f);
    return std::clamp(rounded, 140U, 280U);
}

[[nodiscard]] inline float LauncherIconDrawPixels(float iconSizePx, float cellPx, UINT dpi) noexcept
{
    const float gutter = LauncherDipToPixels(kLauncherIconInnerGutterDip, dpi) * 2.0f;
    float icon = iconSizePx > 0.0f ? iconSizePx : cellPx;
    if (icon <= 0.0f)
    {
        icon = cellPx;
    }
    icon = std::min(icon, cellPx);
    icon = std::min(icon, std::max(8.0f, cellPx - gutter));
    return std::max(8.0f, icon);
}

[[nodiscard]] inline float LauncherSpreadIconPixels(float widthPx, float contentHeightPx, uint32_t columns,
                                                    uint32_t rows, float namedCellPx, UINT dpi) noexcept
{
    columns = std::max(1U, columns);
    rows = std::max(1U, rows);
    const float gutter = LauncherEvenGutterPixels(dpi);
    const float cap = LauncherDipToPixels(kLauncherMaxIconDip, dpi);
    float named = namedCellPx > 0.0f ? namedCellPx : cap;
    named = std::min(named, cap);
    float icon = LauncherIconDrawPixels(named, named, dpi);
    icon = std::min(icon, LauncherEvenSlotLimit(widthPx, columns, gutter));
    icon = std::min(icon, LauncherEvenSlotLimit(contentHeightPx, rows, gutter));
    return std::max(8.0f, icon);
}

[[nodiscard]] constexpr uint32_t LauncherPackedIconCount(const LauncherPageGeometry& pages,
                                                         uint32_t shortcutCount) noexcept
{
    return pages.pageCount > 1 ? pages.visibleCount : shortcutCount;
}

inline void FillLauncherPageCells(uint32_t widthPx, uint32_t heightPx, UINT dpi, uint32_t shortcutCount,
                                  const LauncherPageGeometry& pages,
                                  std::array<std::array<float, 4>, kLauncherMaximumShortcuts>& cells) noexcept
{
    // Space-evenly: leftover width/height becomes equal gutters around and between icon quads (min 8 DIP).
    cells = {};
    const uint32_t packed = LauncherPackedIconCount(pages, shortcutCount);
    if (packed == 0 || widthPx == 0 || heightPx == 0)
    {
        return;
    }
    const float contentHeight = pages.contentHeightPx > 0.0f ? pages.contentHeightPx : static_cast<float>(heightPx);
    const uint32_t columns = std::max(1U, pages.columns);
    const uint32_t rows = std::max(1U, pages.rows);
    const float named = pages.iconSizePx > 0.0f ? pages.iconSizePx : pages.cellSizePx;
    const float icon = LauncherSpreadIconPixels(static_cast<float>(widthPx), contentHeight, columns, rows, named, dpi);
    const float half = icon * 0.5f;
    const float gutterX =
        (static_cast<float>(widthPx) - static_cast<float>(columns) * icon) / static_cast<float>(columns + 1U);
    const float gutterY = (contentHeight - static_cast<float>(rows) * icon) / static_cast<float>(rows + 1U);
    for (uint32_t index = 0; index < packed && index < cells.size(); ++index)
    {
        const uint32_t column = index % columns;
        const uint32_t row = index / columns;
        cells[index][0] = gutterX + static_cast<float>(column) * (icon + gutterX) + half;
        cells[index][1] = gutterY + static_cast<float>(row) * (icon + gutterY) + half;
        cells[index][2] = half;
        cells[index][3] = half;
    }
}
