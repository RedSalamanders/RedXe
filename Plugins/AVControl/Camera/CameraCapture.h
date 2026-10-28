#pragma once
#include "CameraMediaSource.h"
#include <span>

namespace AVControl::Camera
{
// Helper acquisition-lane object, never loaded into Frame Server or the UI. One asynchronous read and one
// completed sample are retained at a time. Callbacks own only their mailbox and a duplicate wake event;
// they cannot call a destroyed backend or publish frames from a previous capture session.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class CaptureReader final
{
  public:
    CaptureReader();
    ~CaptureReader();
    HRESULT OpenDevice(PCWSTR symbolicLink, HANDLE wake) noexcept;
    // The supplied source is exclusively managed by this reader after successful admission and is shut down
    // on Close. Tests supply a real synthetic MF source; production OpenDevice supplies its physical source.
    HRESULT OpenSource(IMFMediaSource* source, HANDLE wake) noexcept;
    void Close() noexcept;
    // S_OK returns a complete tightly packed 720p NV12 frame; S_FALSE means no frame is ready. The caller's
    // storage is reused. Errors terminate acquisition in the owning backend; no stale output is published.
    HRESULT ReadFrame(std::span<BYTE> output, LONGLONG& timestamp) noexcept;
    [[nodiscard]] bool IsOpen() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> _state;
};
#pragma warning(pop)

} // namespace AVControl::Camera
