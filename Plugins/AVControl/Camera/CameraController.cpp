#include "CameraController.h"
#include <mferror.h>
#include <wil/result.h>

namespace AVControl::Camera
{
namespace
{
Availability FailureState(HRESULT result) noexcept
{
    if (SUCCEEDED(result)) return Availability::Ready;
    if (result == E_ACCESSDENIED || result == HRESULT_FROM_WIN32(ERROR_ACCESS_DISABLED_BY_POLICY)) return Availability::AccessDenied;
    if (result == HRESULT_FROM_WIN32(ERROR_BUSY) || result == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION)) return Availability::Busy;
    if (result == MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED || result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) return Availability::Missing;
    if (result == E_NOTIMPL || result == MF_E_INVALIDMEDIATYPE) return Availability::Unsupported;
    return Availability::Failed;
}
}
CameraController::CameraController(BridgeIdentity identity, std::unique_ptr<CaptureSession> capture)
    : _identity(std::move(identity)), _capture(std::move(capture))
{
    _confirmed.availability = Availability::Ready; _confirmed.revision = 1;
}
CameraController::~CameraController() { Shutdown(); }
HRESULT CameraController::Initialize(HANDLE changed) noexcept
{
    if (!changed || !_capture || _identity.ownerSid.empty()) return E_INVALIDARG;
    RETURN_IF_WIN32_BOOL_FALSE(DuplicateHandle(GetCurrentProcess(), changed, GetCurrentProcess(), _changed.put(), EVENT_MODIFY_STATE, FALSE, 0));
    _completed.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    _progress.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    return _completed && _progress ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}
void CameraController::BeginDriverCall() noexcept
{
    _driverDeadline.store(GetTickCount64() + 2250, std::memory_order_release);
    if (_progress) SetEvent(_progress.get());
}
void CameraController::EndDriverCall() noexcept
{
    _driverDeadline.store(0, std::memory_order_release);
    if (_progress) SetEvent(_progress.get());
}
DWORD CameraController::WatchdogTimeout() const noexcept
{
    const auto deadline = _driverDeadline.load(std::memory_order_acquire);
    if (!deadline) return INFINITE;
    const auto now = GetTickCount64();
    return static_cast<DWORD>(deadline > now ? deadline - now : 0);
}
void CameraController::CloseCapture() noexcept
{
    if (!_capture) return;
    BeginDriverCall(); _capture->Close(); EndDriverCall();
}
CameraState CameraController::Snapshot() noexcept
{
    const auto lock = wil::AcquireSRWLockShared(&_lock); return _confirmed;
}
bool CameraController::RequiresHelper() noexcept
{
    const auto lock = wil::AcquireSRWLockShared(&_lock); return _desiredEnabled || _confirmed.enabled || _request != _done;
}
void CameraController::InvalidateSourceRevision() noexcept
{
    const auto lock = wil::AcquireSRWLockExclusive(&_lock);
    ++_confirmed.revision;
    if (_changed) SetEvent(_changed.get());
}
HRESULT CameraController::Select(const DeviceId& id, uint64_t expectedRevision) noexcept
{
    if (id.View().empty()) return E_INVALIDARG;
    return Change(&id, false, expectedRevision);
}
HRESULT CameraController::Enable(bool enabled, uint64_t expectedRevision) noexcept { return Change(nullptr, enabled, expectedRevision); }
HRESULT CameraController::Change(const DeviceId* source, bool enabled, uint64_t expectedRevision) noexcept
{
    if (!_completed || !_changed) return E_UNEXPECTED;
    uint64_t serial = 0;
    bool desiredEnabled = false;
    DeviceId id;
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_lock);
        if (_request != _done) return E_PENDING;
        if (expectedRevision != _confirmed.revision) return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
        if (source) _desiredId = *source; else _desiredEnabled = enabled;
        if (_desiredEnabled && _desiredId.View().empty()) { _desiredEnabled = false; return HRESULT_FROM_WIN32(ERROR_NOT_FOUND); }
        id = _desiredId; desiredEnabled = _desiredEnabled; serial = ++_request; _result = E_PENDING;
    }
    if (!_running && !desiredEnabled) { Finish(serial, id, false, S_OK); return S_OK; }
    if (!_running)
    {
        // Merely selecting a source while off creates no capture thread, pipe, shared frame or media reader.
        _gate = 1;
        const HRESULT started = _bridge.Start(_identity, this);
        if (FAILED(started)) { Fail(serial, id, started); return started; }
        _running = true;
    }
    _bridge.Wake();
    const ULONGLONG deadline = GetTickCount64() + 2500;
    for (;;)
    {
        bool done = false; HRESULT result = E_PENDING;
        { const auto lock = wil::AcquireSRWLockShared(&_lock); done = _done >= serial; result = _result; }
        if (done)
        {
            if (!desiredEnabled) { _bridge.Stop(); _running = false; }
            return result;
        }
        const auto now = GetTickCount64();
        if (now >= deadline) return HRESULT_FROM_WIN32(ERROR_TIMEOUT); // Parent contains a stalled driver by terminating only this helper.
        (void)WaitForSingleObject(_completed.get(), static_cast<DWORD>(deadline - now));
    }
}
void CameraController::Finish(uint64_t serial, const DeviceId& id, bool enabled, HRESULT result) noexcept
{
    const auto lock = wil::AcquireSRWLockExclusive(&_lock);
    const auto availability = FailureState(result);
    if (_confirmed.sourceId != id || _confirmed.enabled != enabled || _confirmed.availability != availability)
    {
        _confirmed.sourceId = id; _confirmed.enabled = enabled; _confirmed.availability = availability; ++_confirmed.revision;
        SetEvent(_changed.get());
    }
    if (serial == _request && _done != serial) { _done = serial; _result = result; SetEvent(_completed.get()); }
}
void CameraController::Fail(uint64_t serial, const DeviceId& id, HRESULT result) noexcept
{
    (void)_bridge.SetGate(++_gate, false);
    CloseCapture(); _capturing = false; _published = false; _frame.reset();
    { const auto lock = wil::AcquireSRWLockExclusive(&_lock); if (serial == _request) _desiredEnabled = false; }
    Finish(serial, id, false, result);
}
void CameraController::OnBridgeWork() noexcept
{
    DeviceId id; bool enabled = false; uint64_t serial = 0; bool pending = false;
    {
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        id = _desiredId; enabled = _desiredEnabled; serial = _request; pending = _done != serial;
    }
    if (!enabled || !_bridge.DemandingConsumers())
    {
        if (_capturing || pending)
        {
            const HRESULT gated = _bridge.SetGate(++_gate, enabled);
            if (FAILED(gated)) { Fail(serial, id, gated); return; }
            CloseCapture(); _capturing = false; _published = false; _frame.reset();
        }
        // Enabled without consumers is armed but never opens a physical source. Off releases capture before
        // completion; the broker then retires the idle listener/thread outside this callback.
        if (pending) Finish(serial, id, enabled, S_OK);
        return;
    }
    if (!_capturing || _capturedId != id)
    {
        const HRESULT gated = _bridge.SetGate(++_gate, false);
        if (FAILED(gated)) { Fail(serial, id, gated); return; }
        CloseCapture(); _capturing = false; _published = false;
        try { if (!_frame) _frame = std::make_unique<std::array<BYTE, FrameBytes>>(); }
        catch (const std::bad_alloc&) { Fail(serial, id, E_OUTOFMEMORY); return; }
        BeginDriverCall();
        const HRESULT opened = _capture->Open(id, _bridge.WakeEvent());
        EndDriverCall();
        if (FAILED(opened)) { Fail(serial, id, opened); return; }
        _capturedId = id; _capturing = true;
    }
    LONGLONG timestamp = 0;
    BeginDriverCall();
    const HRESULT frame = _capture->ReadFrame(*_frame, timestamp);
    EndDriverCall();
    if (FAILED(frame)) { Fail(serial, id, frame); return; }
    if (frame == S_OK)
    {
        if (!_published)
        {
            const HRESULT gated = _bridge.SetGate(++_gate, true);
            if (FAILED(gated)) { Fail(serial, id, gated); return; }
            _published = true;
        }
        const HRESULT published = _bridge.Publish(_gate, *_frame, timestamp);
        if (FAILED(published)) { Fail(serial, id, published); return; }
        Finish(serial, id, true, S_OK);
    }
    else if (_published && pending) Finish(serial, id, true, S_OK);
}
void CameraController::OnBridgeStopping() noexcept
{
    (void)_bridge.SetGate(++_gate, false);
    CloseCapture();
    _capturing = false; _published = false; _frame.reset(); _capturedId = {};
}
void CameraController::Shutdown() noexcept
{
    _bridge.Stop(); _running = false;
    _changed.reset(); _completed.reset(); _progress.reset();
}
} // namespace AVControl::Camera
