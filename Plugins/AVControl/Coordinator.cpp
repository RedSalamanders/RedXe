#include "Coordinator.h"
#include <algorithm>
#include <cstring>
#include <wil/result.h>

namespace AVControl
{
namespace
{
const Endpoint* FindEndpoint(const Inventory& inventory, DeviceKind kind, const DeviceId& id) noexcept
{
    const auto& endpoints = kind == DeviceKind::Output ? inventory.outputs : inventory.inputs;
    const uint32_t count = kind == DeviceKind::Output ? inventory.outputCount : inventory.inputCount;
    for (uint32_t i = 0; i < count; ++i) if (endpoints[i].id == id) return &endpoints[i];
    return nullptr;
}
bool SameDevices(const Inventory& a, const Inventory& b) noexcept
{
    if (a.outputCount != b.outputCount || a.inputCount != b.inputCount || a.cameraCount != b.cameraCount ||
        a.capabilities != b.capabilities || a.truncated != b.truncated) return false;
    for (uint32_t flow = 0; flow < 2; ++flow)
    {
        const auto& left = flow ? a.inputs : a.outputs;
        const auto& right = flow ? b.inputs : b.outputs;
        const auto count = flow ? a.inputCount : a.outputCount;
        for (uint32_t i = 0; i < count; ++i)
            if (left[i].id != right[i].id || left[i].name != right[i].name || left[i].availability != right[i].availability) return false;
    }
    for (uint32_t i = 0; i < a.cameraCount; ++i)
        if (a.cameras[i].id != b.cameras[i].id || a.cameras[i].name != b.cameras[i].name ||
            a.cameras[i].availability != b.cameras[i].availability) return false;
    return true;
}
void SelectRole(Inventory& inventory, uint32_t role) noexcept
{
    const auto* output = FindEndpoint(inventory, DeviceKind::Output, inventory.state.outputDefaults[role]);
    const auto* input = FindEndpoint(inventory, DeviceKind::Microphone, inventory.state.inputDefaults[role]);
    inventory.state.output = output ? *output : Endpoint{};
    inventory.state.microphone = input ? *input : Endpoint{};
}
bool ConnectionKnown(HRESULT result, const Broker& broker) noexcept
{
    return broker.Running() && result != HRESULT_FROM_WIN32(ERROR_INVALID_DATA) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED) &&
        result != HRESULT_FROM_WIN32(ERROR_TIMEOUT);
}
} // namespace

Coordinator::Coordinator(IRedXeHost* host, std::wstring executable, bool synthetic)
    : _host(host), _executable(std::move(executable)), _synthetic(synthetic),
      _current(std::make_unique<Inventory>()), _reply(std::make_unique<BrokerReply>()),
      _preferences(std::make_unique<InventoryPreferences>()), _workPreferences(std::make_unique<InventoryPreferences>()) {}
Coordinator::~Coordinator() { Disarm(); }
HRESULT Coordinator::Initialize() noexcept
{
    if (!_host || _executable.empty()) return E_INVALIDARG;
    _wait.reset(CreateThreadpoolWait(Changed, this, nullptr));
    RETURN_LAST_ERROR_IF(!_wait);
    return S_OK;
}
void CALLBACK Coordinator::Changed(PTP_CALLBACK_INSTANCE, void* context, PTP_WAIT, TP_WAIT_RESULT) noexcept
{
    auto& self = *static_cast<Coordinator*>(context);
    self._dirty.store(true, std::memory_order_release);
    if (self._observing.load(std::memory_order_acquire)) (void)self._host->RequestFrame();
    // One shot: UI preparation schedules one observation, and completion rearms. No callback polling or rerun loop.
}
void Coordinator::Disarm() noexcept
{
    _observing.store(false, std::memory_order_release);
    if (_wait)
    {
        SetThreadpoolWait(_wait.get(), nullptr, nullptr);
        WaitForThreadpoolWaitCallbacks(_wait.get(), TRUE);
    }
}
void Coordinator::Arm() noexcept
{
    if (!_visible || !_wait || !_broker.ChangeEvent()) return;
    _observing.store(true, std::memory_order_release);
    SetThreadpoolWait(_wait.get(), _broker.ChangeEvent(), nullptr);
}
void Coordinator::Invalidate() noexcept
{
    ++_revision;
    if (_host) (void)_host->RequestFrame();
}
void Coordinator::SetSubscriberVisible(bool visible) noexcept
{
    if (visible)
    {
        if (++_visible == 1) { _dirty.store(true, std::memory_order_release); Invalidate(); }
    }
    else if (_visible && --_visible == 0)
    {
        Disarm();
        {
            const auto guard = wil::AcquireSRWLockExclusive(&_lock);
            _stopRequested = true;
            _observeRequested = false;
        }
        // Closing driver objects belongs to the helper worker, not the visibility callback.
        (void)Schedule();
    }
}
void Coordinator::Refresh() noexcept
{
    _dirty.store(true, std::memory_order_release);
    Invalidate();
}
HRESULT Coordinator::RegisterConfiguration(const Configuration* configuration) noexcept
{
    if (!configuration || configuration->count > MaximumProfiles) return E_INVALIDARG;
    for (const auto* existing : _configurations) if (existing == configuration) return S_FALSE;
    for (auto& slot : _configurations)
    {
        if (slot) continue;
        slot = configuration;
        ConfigurationChanged();
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
}
void Coordinator::UnregisterConfiguration(const Configuration* configuration) noexcept
{
    for (auto& slot : _configurations)
    {
        if (!configuration || slot != configuration) continue;
        slot = nullptr;
        ConfigurationChanged();
        return;
    }
}
void Coordinator::ConfigurationChanged() noexcept
{
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        *_preferences = {};
        for (const auto* configuration : _configurations)
            if (configuration)
                for (uint32_t i = 0; i < configuration->count; ++i) _preferences->Add(configuration->profiles[i]);
        ++_preferenceRevision;
    }
    _dirty.store(true, std::memory_order_release);
    if (_visible) Invalidate();
}
void Coordinator::Prepare() noexcept
{
    if (!_visible) return;
    if (_dirty.exchange(false, std::memory_order_acq_rel))
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _observeRequested = true;
        _stopRequested = false;
    }
    (void)Schedule();
}
bool Coordinator::HasPending() noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    return _observeRequested || _stopRequested || _profile.has_value() ||
        std::any_of(_commands.begin(), _commands.end(), [](const auto& command) { return command.has_value(); });
}
HRESULT Coordinator::Schedule() noexcept
{
    if (_inFlight || !HasPending()) return S_FALSE;
    _inFlight = true;
    _ranProfile = false;
    _stateKnown = false;
    const HRESULT result = _host->QueueControlWork(this);
    if (FAILED(result))
    {
        _inFlight = false;
        _lastResult = result;
        // A host that rejects device work (including automated host smoke tests) gets no retry/frame loop.
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _commands = {};
        _profile.reset();
        _profilePending = false;
        _observeRequested = false;
        _stopRequested = false;
    }
    return result;
}
uint32_t Coordinator::PendingMask() const noexcept
{
    // Called only on UI; the short lock also protects the worker taking a queued lane.
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    uint32_t mask = _runningMask;
    for (uint32_t i = 0; i < _commands.size(); ++i) if (_commands[i]) mask |= 1U << i;
    if (_profilePending) mask |= 0x1fU;
    return mask;
}
HRESULT Coordinator::Submit(const ViewCommand& command) noexcept
{
    const auto device = static_cast<uint32_t>(command.device);
    if (device > 2 ||
        (command.kind != ViewCommandKind::SetMuted && command.kind != ViewCommandKind::SetCameraEnabled && command.kind != ViewCommandKind::SetLevel) ||
        (command.kind == ViewCommandKind::SetMuted && device > 1) ||
        (command.kind == ViewCommandKind::SetCameraEnabled && device != 2) ||
        (command.kind == ViewCommandKind::SetLevel && device > 1) ||
        command.value > (command.kind == ViewCommandKind::SetLevel ? 100U : 1U)) return E_INVALIDARG;
    const uint32_t lane = command.kind == ViewCommandKind::SetLevel ? device + 3 : device;
    if (_profilePending && lane >= 3) return HRESULT_FROM_WIN32(ERROR_BUSY);
    if (lane < 3) _safety.Publish(command.device, lane == 2 ? command.value == 0 : command.value != 0);
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _commands[lane] = Command{command, GetTickCount64(), _profilePending && lane < 3};
    }
    Invalidate();
    return Schedule();
}
HRESULT Coordinator::SubmitProfile(const Profile& profile) noexcept
{
    if (_profilePending) return HRESULT_FROM_WIN32(ERROR_BUSY);
    // Validate before accepting any work, including profiles submitted through future non-pointer input routes.
    Configuration validation{};
    validation.count = 1;
    validation.profiles[0] = profile;
    std::array<char, MaximumSettingsBytes> serialized{};
    uint32_t bytes = 0;
    RETURN_IF_FAILED(SerializeConfiguration(validation, serialized.data(), serialized.size(), bytes));
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _profile = profile;
        _profileAcceptedAt = GetTickCount64();
        _profilePending = true;
        // Uncommitted levels from an old view cannot follow a new route.
        _commands[3].reset();
        _commands[4].reset();
    }
    Invalidate();
    return Schedule();
}
HRESULT Coordinator::Run(HANDLE cancelEvent, uint32_t timeoutMilliseconds) noexcept
{
    _ranProfile = false;
    _stateKnown = false;
    _workApply = {};
    Command command;
    std::optional<Profile> profile;
    bool stop = false;
    uint64_t acceptedAt = GetTickCount64();
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _runningMask = 0;
        if (_workPreferenceRevision != _preferenceRevision)
        {
            *_workPreferences = *_preferences;
            _workPreferenceRevision = _preferenceRevision;
        }
        // All later mute/off lanes precede a queued profile or volume edit.
        for (uint32_t i = 0; i < _commands.size(); ++i)
        {
            if (!_commands[i]) continue;
            command = *_commands[i];
            _commands[i].reset();
            _runningMask = 1U << i;
            acceptedAt = command.acceptedAt;
            break;
        }
        if (!_runningMask && _profile)
        {
            profile = *_profile;
            _profile.reset();
            _ranProfile = true;
            acceptedAt = _profileAcceptedAt;
        }
        if (!_runningMask && !profile)
        {
            stop = _stopRequested;
            _stopRequested = false;
            _observeRequested = false;
        }
    }
    const auto start = GetTickCount64();
    const auto age = start - acceptedAt;
    if (age >= 3000) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    const uint32_t budget = (std::min)(timeoutMilliseconds, static_cast<uint32_t>(3000 - age));
    const auto remaining = [&]() noexcept -> uint32_t
    {
        const auto elapsed = GetTickCount64() - start;
        return elapsed < budget ? static_cast<uint32_t>(budget - elapsed) : 0;
    };
    if (WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    if (stop)
    {
        if (!_broker.Running()) return S_FALSE;
        BrokerCommand suspend; suspend.operation = BrokerOperation::SuspendObservation;
        const HRESULT result = _broker.Execute(suspend, *_reply, cancelEvent, remaining());
        _stateKnown = ConnectionKnown(result, _broker);
        if (FAILED(result) || !_reply->inventory.cameraRequiresHelper) _broker.Stop((std::min)(uint32_t{250}, remaining()));
        return result;
    }
    if (!_broker.Running())
    {
        RETURN_IF_FAILED(_broker.Start(_executable, _synthetic));
        _brokerPreferenceRevision = 0;
    }
    if (_brokerPreferenceRevision != _workPreferenceRevision)
    {
        RETURN_IF_FAILED(_broker.SetPreferences(*_workPreferences));
        _brokerPreferenceRevision = _workPreferenceRevision;
    }
    if (!remaining()) { _broker.Stop(0); return HRESULT_FROM_WIN32(ERROR_TIMEOUT); }
    HRESULT result = S_OK;
    if (profile)
    {
        _workApply = ApplyProfile(*profile, _broker, *_reply, _safety, cancelEvent, remaining());
        result = _workApply.result;
        if (_workApply.applied) _controlledRole = profile->audioRoles == AudioRoles::Communications ? 2U : 0U;
        _stateKnown = _workApply.stateKnown && ConnectionKnown(result, _broker);
    }
    else
    {
        BrokerCommand request;
        if (_runningMask)
        {
            const auto& intent = command.value;
            request = {intent.kind == ViewCommandKind::SetLevel ? BrokerOperation::SetLevel :
                intent.kind == ViewCommandKind::SetMuted ? BrokerOperation::SetMute : BrokerOperation::SetCameraEnabled,
                intent.device, intent.endpointId, intent.value, 0, intent.generation, intent.revision};
            if (command.followRoute)
            {
                result = _broker.Execute({}, *_reply, cancelEvent, remaining());
                if (SUCCEEDED(result))
                {
                    SelectRole(_reply->inventory, _controlledRole);
                    if (intent.device == DeviceKind::Camera) request.expectedRevision = _reply->inventory.state.camera.revision;
                    else
                    {
                        const auto& endpoint = intent.device == DeviceKind::Output ? _reply->inventory.state.output : _reply->inventory.state.microphone;
                        request.id = endpoint.id;
                        request.expectedGeneration = endpoint.generation;
                        request.expectedRevision = endpoint.muteRevision;
                    }
                }
            }
        }
        if (SUCCEEDED(result)) result = remaining() ? _broker.Execute(request, *_reply, cancelEvent, remaining()) : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        _stateKnown = ConnectionKnown(result, _broker);
    }
    if (_stateKnown) SelectRole(_reply->inventory, _controlledRole);
    return result;
}
void Coordinator::Complete(HRESULT result) noexcept
{
    _inFlight = false;
    _lastResult = result;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _runningMask = 0;
        if (_ranProfile) { _profilePending = false; _lastApply = _workApply; }
    }
    if (_stateKnown)
    {
        if (!SameDevices(*_current, _reply->inventory)) ++_deviceRevision;
        *_current = _reply->inventory;
    }
    else if (FAILED(result))
    {
        _current->state.output.availability = Availability::Unknown;
        _current->state.microphone.availability = Availability::Unknown;
        _current->state.camera.availability = Availability::Unknown;
        ++_deviceRevision;
    }
    Invalidate();
    Arm();
    (void)Schedule();
}
} // namespace AVControl
