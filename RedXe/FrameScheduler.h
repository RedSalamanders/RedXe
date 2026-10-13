#pragma once

#include <chrono>
#include <cstdint>

enum class ContinuousFrameRate : uint32_t
{
    Display,
    Thirty,
};

enum class HostFrameAction
{
    WaitForMessage,
    ProbeOcclusion,
    Render,
};

struct HostFrameState final
{
    bool windowVisible = false;
    bool displayPoweredOn = false;
    bool rendererSuspended = true;
    bool rendererOccluded = false;
    bool occlusionStatusChanged = false;
    bool continuousFramesRequired = false;
    bool frameInvalidated = true;
    // Follow-finger pan, settle, or a staged neighbor. Presentation-paced frames MUST continue so widget animation
    // and the page-scroll ease keep moving, even when DXGI reports the swap chain occluded because a native child
    // covers it.
    bool pageNavigationActive = false;
    // Raise or dismiss settle. Same presentation-paced rule as a page swipe; settled overlay chrome does not keep
    // presenting on its own.
    bool overlayMotionActive = false;
    // Autohide dock collapsed to its peek strip: exactly one grip frame after each hide (the invalidation), then the
    // host blocks like a minimized window regardless of continuous widgets or scheduled frames.
    bool dockHidden = false;
};

[[nodiscard]] constexpr HostFrameAction SelectHostFrameAction(const HostFrameState& state) noexcept
{
    if (!state.windowVisible || !state.displayPoweredOn || state.rendererSuspended)
    {
        return HostFrameAction::WaitForMessage;
    }
    if (state.rendererOccluded && !state.pageNavigationActive && !state.overlayMotionActive)
    {
        return state.occlusionStatusChanged ? HostFrameAction::ProbeOcclusion : HostFrameAction::WaitForMessage;
    }
    if (state.dockHidden)
    {
        return state.frameInvalidated ? HostFrameAction::Render : HostFrameAction::WaitForMessage;
    }
    if (!state.continuousFramesRequired && !state.frameInvalidated && !state.pageNavigationActive &&
        !state.overlayMotionActive)
    {
        return HostFrameAction::WaitForMessage;
    }
    return HostFrameAction::Render;
}

[[nodiscard]] constexpr bool LimitContinuousFrames(ContinuousFrameRate rate, const HostFrameState& state) noexcept
{
    return rate == ContinuousFrameRate::Thirty && state.continuousFramesRequired && !state.frameInvalidated &&
           !state.pageNavigationActive && !state.overlayMotionActive && !state.dockHidden &&
           SelectHostFrameAction(state) == HostFrameAction::Render;
}

// A monotonic deadline independent of the panel's refresh rate. Late frames start a fresh interval: no catch-up burst.
class ContinuousFramePacer final
{
  public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto Period = std::chrono::nanoseconds{1'000'000'000 / 30};

    void Presented(Clock::time_point now) noexcept
    {
        _deadline = now + std::chrono::duration_cast<Clock::duration>(Period);
        _frameLatencyGeneration = 0;
    }

    void Reset() noexcept
    {
        _deadline = {};
    }

    // Readiness is consumed by a render attempt, not by deadline cancellation, visibility, or pacing-mode changes.
    // Resize retains the swap chain's waitable object. Only replacing that object changes this generation.
    void AcceptFrameLatency(uint64_t generation) noexcept
    {
        _frameLatencyGeneration = generation;
    }

    [[nodiscard]] bool HasFrameLatency(uint64_t generation) const noexcept
    {
        return generation != 0 && _frameLatencyGeneration == generation;
    }

    void ConsumeFrameLatency() noexcept
    {
        _frameLatencyGeneration = 0;
    }

    [[nodiscard]] std::chrono::nanoseconds Remaining(Clock::time_point now) const noexcept
    {
        return now < _deadline ? std::chrono::duration_cast<std::chrono::nanoseconds>(_deadline - now)
                               : std::chrono::nanoseconds::zero();
    }

  private:
    Clock::time_point _deadline{};
    uint64_t _frameLatencyGeneration = 0;
};
static_assert(sizeof(ContinuousFramePacer) == 16);
