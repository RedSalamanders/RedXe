#pragma once
#include "CameraMediaSource.h"
#include <array>
#include <span>
#include <wil/resource.h>

namespace AVControl::Camera
{
struct FrameChannelNames final
{
    std::array<wchar_t, 128> mapping{};
    std::array<wchar_t, 128> mutex{};
};
struct ChannelState final
{
    uint64_t revision = 1;
    bool enabled = false;
};

// One latest NV12 image, shared between the Frame Server source and its owned physical-capture helper. The
// consumer creates Global objects in Session 0; the helper only opens them (no SeCreateGlobalPrivilege needed).
// Local names are an explicit isolated-test option. Access is limited to the owner SID, Local Service and SYSTEM.
// Every method is caller-serialized. Read/EndRead form one same-thread lease through sample publication.
class FrameChannel final
{
  public:
    FrameChannel() = default;
    ~FrameChannel();
    HRESULT Create(PCWSTR ownerSid, bool localTestNamespace = false) noexcept;
    HRESULT Open(const FrameChannelNames& names, bool allowLocalTestNamespace = false) noexcept;
    void Close() noexcept;
    [[nodiscard]] const FrameChannelNames& Names() const noexcept
    {
        return _names;
    }
    HRESULT State(ChannelState& result) noexcept;
    // Revision changes clear the previous image even if enabled remains true (physical source replacement).
    HRESULT SetGate(ChannelState state) noexcept;
    HRESULT Write(uint64_t revision, std::span<const BYTE> nv12, LONGLONG timestamp) noexcept;
    HRESULT Read(BYTE* bytes, DWORD capacity, LONG pitch, LONGLONG now) noexcept;
    void EndRead() noexcept;

  private:
    struct Shared;
    FrameChannelNames _names;
    wil::unique_handle _mapping, _mutex;
    wil::unique_mapview_ptr<Shared> _view;
    wil::mutex_release_scope_exit _readLease;
    HRESULT Lock(wil::mutex_release_scope_exit& lease) noexcept;
    bool Valid() const noexcept;
};
} // namespace AVControl::Camera
