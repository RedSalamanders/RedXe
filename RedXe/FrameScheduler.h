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
};

[[nodiscard]] constexpr HostFrameAction SelectHostFrameAction(const HostFrameState& state) noexcept
{
    if (!state.windowVisible || !state.displayPoweredOn || state.rendererSuspended)
    {
        return HostFrameAction::WaitForMessage;
    }
    if (state.rendererOccluded)
    {
        return state.occlusionStatusChanged ? HostFrameAction::ProbeOcclusion : HostFrameAction::WaitForMessage;
    }
    if (!state.continuousFramesRequired && !state.frameInvalidated)
    {
        return HostFrameAction::WaitForMessage;
    }
    return HostFrameAction::Render;
}
