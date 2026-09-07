#pragma once
#include "../AVControlModel.h"
#include "CameraBridgeServer.h"

namespace AVControl::Camera
{
// Pure interface: no base implementation calls virtual members during construction/destruction. Suppress the
// redundant base vftable (and v145's conflicting largest-COMDAT emission across the two synthetic consumers).
class __declspec(novtable) CaptureSession
{
  public:
    virtual ~CaptureSession() = default;
    virtual HRESULT Open(const DeviceId& id, HANDLE wake) noexcept = 0;
    virtual void Close() noexcept = 0;
    virtual HRESULT ReadFrame(std::span<BYTE> output, LONGLONG& timestamp) noexcept = 0;
};
// Device-independent control/lifetime policy, shared by the real helper and explicitly injected synthetic
// tests. Public changes are serialized by the broker MTA; capture work runs exclusively on the bridge MTA.
// Display visibility does not appear here: an enabled route belongs to camera consumers, not the AV tile.
class CameraController final : private BridgeObserver
{
  public:
    CameraController(BridgeIdentity identity, std::unique_ptr<CaptureSession> capture);
    ~CameraController();
    HRESULT Initialize(HANDLE changed) noexcept;
    HRESULT Select(const DeviceId& id, uint64_t expectedRevision) noexcept;
    HRESULT Enable(bool enabled, uint64_t expectedRevision) noexcept;
    [[nodiscard]] CameraState Snapshot() noexcept;
    [[nodiscard]] bool RequiresHelper() noexcept;
    // Broker MTA: a physical removal/replacement invalidates even an already-off source's older intents.
    void InvalidateSourceRevision() noexcept;
    [[nodiscard]] HANDLE ProgressEvent() const noexcept
    {
        return _progress.get();
    }
    [[nodiscard]] DWORD WatchdogTimeout() const noexcept;
    void Shutdown() noexcept;

  private:
    BridgeIdentity _identity;
    std::unique_ptr<CaptureSession> _capture;
    BridgeServer _bridge;
    wil::unique_handle _changed;
    wil::unique_event _completed;
    wil::unique_event _progress;
    std::atomic<ULONGLONG> _driverDeadline{0};
    SRWLOCK _lock = SRWLOCK_INIT;
    CameraState _confirmed;
    DeviceId _desiredId;
    bool _desiredEnabled = false, _running = false;
    uint64_t _request = 0, _done = 0;
    HRESULT _result = S_OK;
    // Acquisition-thread state; no UI/host reference or HWND is retained.
    DeviceId _capturedId;
    bool _capturing = false, _published = false;
    uint64_t _gate = 1;
    static constexpr size_t FrameBytes = static_cast<size_t>(FrameWidth) * FrameHeight * 3 / 2;
    std::unique_ptr<std::array<BYTE, FrameBytes>> _frame;
    HRESULT Change(const DeviceId* source, bool enabled, uint64_t expectedRevision) noexcept;
    void OnBridgeWork() noexcept override;
    void OnBridgeStopping() noexcept override;
    void Finish(uint64_t serial, const DeviceId& id, bool enabled, HRESULT result) noexcept;
    void Fail(uint64_t serial, const DeviceId& id, HRESULT result) noexcept;
    void BeginDriverCall() noexcept;
    void EndDriverCall() noexcept;
    void CloseCapture() noexcept;
};
} // namespace AVControl::Camera
