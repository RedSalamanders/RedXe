#pragma once
#include <memory>
#include <mfidl.h>

namespace AVControl::Camera
{
inline constexpr UINT FrameWidth = 1280;
inline constexpr UINT FrameHeight = 720;
inline constexpr UINT FrameRate = 30;
inline constexpr LONGLONG FrameDuration = 10'000'000 / FrameRate;
inline constexpr UINT MaximumPendingSamples = 4;
inline constexpr UINT MaximumSamplePool = 6;

// Media-transport boundary, independent of RedXe HWND/UI/device ownership. Fill runs only in the source's MF
// work queue. It must finish within the transport deadline and write every NV12 row, or return a failure which
// the source replaces with a neutral frame. The real provider communicates with the owned capture helper;
// it must never call a physical driver inside Frame Server. No recording or frame history is retained.
class FrameProvider
{
  public:
    virtual ~FrameProvider() = default;
    virtual HRESULT Start() noexcept = 0;
    virtual void Stop() noexcept = 0;
    virtual HRESULT Fill(BYTE* bytes, DWORD capacity, LONG pitch, LONGLONG timestamp) noexcept = 0;
    // A transport may hold its cross-process off gate from Fill through MEMediaSample publication. Always
    // called on the same worker, including failed fills, so an acknowledged Off cannot race a later publication.
    virtual void EndFrame() noexcept {}
};

HRESULT CreateMediaSource(std::shared_ptr<FrameProvider> provider, IMFMediaSource** result) noexcept;
uint32_t ActiveMediaObjects() noexcept;
} // namespace AVControl::Camera
