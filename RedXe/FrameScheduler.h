#pragma once

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
    if (!state.continuousFramesRequired && !state.frameInvalidated && !state.pageNavigationActive &&
        !state.overlayMotionActive)
    {
        return HostFrameAction::WaitForMessage;
    }
    return HostFrameAction::Render;
}
