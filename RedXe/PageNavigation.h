#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <windows.h>

[[nodiscard]] inline LONG PageSwipeThresholdPixels(UINT dpi) noexcept
{
    const int scaleDpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : static_cast<int>(dpi);
    return MulDiv(16, scaleDpi, USER_DEFAULT_SCREEN_DPI);
}

[[nodiscard]] inline LONG PageSwipeFlickSpeedPixelsPerSecond(UINT dpi) noexcept
{
    const int scaleDpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : static_cast<int>(dpi);
    return MulDiv(2200, scaleDpi, USER_DEFAULT_SCREEN_DPI);
}

inline constexpr uint32_t kPageSwipeMinFingerCount = 2;
inline constexpr uint32_t kPageSwipeMaxFingerCount = 3;

[[nodiscard]] constexpr bool PageSwipeAcceptsFingerCount(uint32_t fingers) noexcept
{
    return fingers >= kPageSwipeMinFingerCount && fingers <= kPageSwipeMaxFingerCount;
}

// A second or third finger may begin tracking, but it must not cancel a captured widget
// gesture until the pan locks horizontally. Overlapping double-taps are not a page swipe.
[[nodiscard]] constexpr bool PageSwipeStealsWidgetGesture(bool panLocked) noexcept
{
    return panLocked;
}

[[nodiscard]] constexpr bool PageSwipeLocksHorizontal(LONG deltaX, LONG deltaY, LONG threshold) noexcept
{
    const LONG absX = deltaX < 0 ? -deltaX : deltaX;
    const LONG absY = deltaY < 0 ? -deltaY : deltaY;
    return absX >= threshold && absX > absY;
}

[[nodiscard]] constexpr bool PageSwipeRejectsAsVertical(LONG deltaX, LONG deltaY, LONG threshold) noexcept
{
    const LONG absX = deltaX < 0 ? -deltaX : deltaX;
    const LONG absY = deltaY < 0 ? -deltaY : deltaY;
    return absY >= threshold && absY >= absX;
}

[[nodiscard]] constexpr bool PageSwipeBlocksDirection(LONG offset, bool wrapPages, bool atFirst, bool atLast) noexcept
{
    if (wrapPages || offset == 0)
    {
        return false;
    }
    return (offset > 0 && atFirst) || (offset < 0 && atLast);
}

[[nodiscard]] inline LONG ApplyPageEdgeResistance(LONG offset, LONG pageWidth, bool blocked) noexcept
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

[[nodiscard]] constexpr int PageSwipeDirection(LONG offset) noexcept
{
    if (offset < 0)
    {
        return 1;
    }
    if (offset > 0)
    {
        return -1;
    }
    return 0;
}

[[nodiscard]] constexpr LONG PageSwipeCommitDistance(LONG pageWidth) noexcept
{
    return pageWidth > 0 ? pageWidth / 4 : 0;
}

[[nodiscard]] inline bool ShouldCommitPageSwipe(LONG offset, LONG pageWidth, float velocityPxPerSec, UINT dpi,
                                                bool wrapPages, bool atFirst, bool atLast) noexcept
{
    if (pageWidth <= 0 || offset == 0 || PageSwipeBlocksDirection(offset, wrapPages, atFirst, atLast))
    {
        return false;
    }

    const LONG distance = offset < 0 ? -offset : offset;
    if (distance >= PageSwipeCommitDistance(pageWidth))
    {
        return true;
    }

    const float directedVelocity = offset < 0 ? -velocityPxPerSec : velocityPxPerSec;
    return directedVelocity >= static_cast<float>(PageSwipeFlickSpeedPixelsPerSecond(dpi));
}

[[nodiscard]] constexpr float EaseOutCubic(float t) noexcept
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

[[nodiscard]] inline LONG InterpolatePageOffset(LONG start, LONG target, float t) noexcept
{
    const float eased = EaseOutCubic(t);
    const float mixed = static_cast<float>(start) + static_cast<float>(target - start) * eased;
    return mixed >= 0.0f ? static_cast<LONG>(mixed + 0.5f) : static_cast<LONG>(mixed - 0.5f);
}

[[nodiscard]] inline UINT PageSettleDurationMilliseconds(LONG remaining, float velocityPxPerSec) noexcept
{
    const float distance = std::fabs(static_cast<float>(remaining));
    const float speed = std::max(std::fabs(velocityPxPerSec), 1400.0f);
    const float milliseconds = distance / speed * 1000.0f;
    const UINT rounded = static_cast<UINT>(milliseconds + 0.5f);
    return std::clamp(rounded, 140U, 280U);
}
