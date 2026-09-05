#pragma once
#include "CameraFrameChannel.h"
#include <string>

namespace AVControl::Camera
{
inline constexpr uint32_t BridgeMagic = 0x42435852;
inline constexpr uint32_t BridgeVersion = 1;
inline constexpr DWORD BridgeDeadlineMs = 100;
inline constexpr size_t MaximumConsumers = 4;
struct BridgeHello final
{
    uint32_t magic = BridgeMagic, version = BridgeVersion, size = sizeof(BridgeHello), reserved = 0;
    FrameChannelNames channel;
};
struct BridgeReply final
{
    uint32_t magic = BridgeMagic, version = BridgeVersion, size = sizeof(BridgeReply);
    HRESULT status = E_PENDING;
};
static_assert(sizeof(BridgeHello) == 528 && sizeof(BridgeReply) == 16);

// Owner is a Windows principal, not a caller-supplied executable path. Production admits only a Session-0
// Local Service consumer; the client verifies the kernel pipe object's owner. A same-user process has the
// user's camera authority. Isolated tests add a unique namespace and explicitly admit their own process.
struct BridgeIdentity final
{
    std::wstring ownerSid;
    std::wstring pipeName;
    bool synthetic = false;
};
HRESULT MakeBridgeIdentity(PCWSTR ownerSid, BridgeIdentity& result, const GUID* isolatedTest = nullptr) noexcept;
HRESULT CreateBridgeSecurity(const BridgeIdentity& identity, wil::unique_hlocal& security) noexcept;
HRESULT AuthenticateBridgeServer(HANDLE pipe, const BridgeIdentity& identity) noexcept;
HRESULT AuthenticateBridgeConsumer(HANDLE pipe, const BridgeIdentity& identity) noexcept;
// The handle must be overlapped. One absolute deadline covers the entire handshake, including partial I/O.
// On failure the individual operation is canceled and drained before its stack storage can be released.
HRESULT BridgeTransfer(HANDLE pipe, bool write, void* bytes, DWORD size, ULONGLONG deadline) noexcept;

class BridgeFrameProvider final : public FrameProvider
{
  public:
    explicit BridgeFrameProvider(BridgeIdentity identity) noexcept : _identity(std::move(identity)) {}
    ~BridgeFrameProvider() override { Stop(); }
    HRESULT Start() noexcept override;
    void Stop() noexcept override;
    HRESULT Fill(BYTE* bytes, DWORD capacity, LONG pitch, LONGLONG timestamp) noexcept override;
    void EndFrame() noexcept override { _channel.EndRead(); }
  private:
    BridgeIdentity _identity;
    FrameChannel _channel;
    wil::unique_handle _pipe;
    wil::unique_event _demandEvent;
    OVERLAPPED _demand{};
    BYTE _demandByte = 1;
    bool _demandPending = false;
    ULONGLONG _retryAt = 0;
    bool _started = false;
    HRESULT Connect() noexcept;
    void Disconnect() noexcept;
    HRESULT Demand() noexcept;
};
} // namespace AVControl::Camera
