#pragma once

#include "PageEdgeAffordance.h"
#include "WheelDetent.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Mouse wheel routing policy: the widget under the pointer first, then dashboard page navigation.
//
// A wheel *sequence* is a run of samples with less than kWheelSequenceGapMilliseconds between consecutive samples.
// The first sample of a sequence is offered to the topmost interactive widget under the pointer, and its answer
// decides who owns the rest of the sequence: a widget that consumed it keeps every later sample (even ones it then
// declines, so overshooting a paged widget's last page never throws the user onto another dashboard page), and a
// declined or unanswered first sample gives the sequence to the host, which then does not offer later samples to any
// widget and turns each whole detent into one page navigation. A pause ends the sequence and the next sample is
// offered to the widget again. This is the wheel-latching model browsers use for nested scrolling.
//
// Everything here is a pure function of sample time, axis, delta, and the widget's answer, so the policy is testable
// without a window. Application feeds it GetTickCount64 and calls NavigateToAdjacentPage when it says so.

inline constexpr uint64_t kWheelSequenceGapMilliseconds = 500;

enum class WheelAxis : uint8_t
{
    Vertical = 0,
    Horizontal = 1,
};

enum class WheelOwner : uint8_t
{
    None = 0,
    Widget = 1,
    Host = 2,
};

// Units a sample travels the dashboard, positive toward the next page: wheel down (negative Win32 delta) and tilt
// right (positive Win32 delta) both advance, matching swipe-left.
[[nodiscard]] constexpr float WheelNavigationUnits(WheelAxis axis, float wheelDelta) noexcept
{
    return axis == WheelAxis::Vertical ? -wheelDelta : wheelDelta;
}

struct WheelNavigator final
{
    WheelOwner owner = WheelOwner::None;
    size_t widgetIndex = SIZE_MAX;
    uint64_t lastSampleMilliseconds = 0;
    bool inSequence = false;
    // Per-axis accumulation in navigation units (positive = next page).
    std::array<RedXeWheelDetent, 2> detents{};

    // Records a sample's arrival and returns the owner it belongs to. A gap of kWheelSequenceGapMilliseconds or more
    // since the previous sample ends the sequence, so this sample starts a new one whose owner is still undecided
    // (None): offer it to the widget under the pointer, then call Latch with the answer.
    [[nodiscard]] WheelOwner Begin(uint64_t nowMilliseconds) noexcept
    {
        if (!inSequence || nowMilliseconds - lastSampleMilliseconds >= kWheelSequenceGapMilliseconds)
        {
            Reset();
        }
        inSequence = true;
        lastSampleMilliseconds = nowMilliseconds;
        return owner;
    }

    // The first sample's answer. A consumed sample latches the sequence to that widget; anything else gives it to
    // the host. Later calls in the same sequence do not change the owner.
    void Latch(size_t index, bool consumed) noexcept
    {
        if (owner != WheelOwner::None)
        {
            return;
        }
        owner = consumed ? WheelOwner::Widget : WheelOwner::Host;
        widgetIndex = consumed ? index : SIZE_MAX;
    }

    // A host-owned sample. Returns kPageEdgeDirectionNext or kPageEdgeDirectionPrevious once a whole detent has
    // accumulated on that axis, otherwise 0. A completed detent clears both axes: one navigation per notch, and a
    // surplus is dropped rather than banked for later.
    [[nodiscard]] int Accumulate(WheelAxis axis, float wheelDelta) noexcept
    {
        const int steps = detents[static_cast<size_t>(axis)].Accumulate(WheelNavigationUnits(axis, wheelDelta));
        if (steps == 0)
        {
            return 0;
        }
        ClearAccumulation();
        return steps > 0 ? kPageEdgeDirectionNext : kPageEdgeDirectionPrevious;
    }

    // Drops partial travel. Called when navigation is refused (settle, pan, raise, blocked end, hidden) so nothing
    // accumulated during a suppressed state plays back afterwards.
    void ClearAccumulation() noexcept
    {
        for (RedXeWheelDetent& detent : detents)
        {
            detent.Clear();
        }
    }

    // Ends the sequence outright: hide, resize, DPI change, settings apply, raise, dismiss, capture loss.
    void Reset() noexcept
    {
        owner = WheelOwner::None;
        widgetIndex = SIZE_MAX;
        inSequence = false;
        lastSampleMilliseconds = 0;
        ClearAccumulation();
    }

    // The page changed under the sequence. A widget latch refers to the old page's slots and must go; a host latch
    // keeps a spin flipping pages until the user pauses.
    void ReleaseWidget() noexcept
    {
        if (owner == WheelOwner::Widget)
        {
            Reset();
        }
    }
};
