#include "ProfileTransaction.h"
#include <algorithm>
#include <memory>

namespace AVControl
{
void SafetyState::Publish(DeviceKind device, bool off) noexcept
{
    const auto index = static_cast<size_t>(device);
    if (index >= _values.size())
        return;
    const auto previous = _values[index].load(std::memory_order_relaxed);
    _values[index].store(((previous >> 1) + 1) * 2 + (off ? 1 : 0), std::memory_order_release);
}
std::array<uint64_t, 3> SafetyState::Snapshot() const noexcept
{
    return {_values[0].load(std::memory_order_acquire), _values[1].load(std::memory_order_acquire), _values[2].load(std::memory_order_acquire)};
}
namespace
{
const Endpoint* FindEndpoint(const Inventory& inventory, DeviceKind device, const DeviceId& id) noexcept
{
    const auto& values = device == DeviceKind::Output ? inventory.outputs : inventory.inputs;
    const auto count   = device == DeviceKind::Output ? inventory.outputCount : inventory.inputCount;
    for (uint32_t i = 0; i < count; ++i)
        if (values[i].id == id)
            return &values[i];
    return nullptr;
}
bool ConnectionFailure(HRESULT result) noexcept
{
    return result == HRESULT_FROM_WIN32(ERROR_TIMEOUT) || result == HRESULT_FROM_WIN32(ERROR_PROCESS_ABORTED) ||
           result == HRESULT_FROM_WIN32(ERROR_CANCELLED) || result == HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}
struct Undo final
{
    BrokerCommand command;
    uint64_t ownedRevision = 0;
    bool restorable        = true;
};
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5027)
class Transaction final
{
public:
    Transaction(const Profile& p, BackendTransport& b, BrokerReply& r, const SafetyState& s, HANDLE c, uint32_t budget) noexcept
        : _profile(p),
          _backend(b),
          _reply(r),
          _safety(s),
          _cancel(c),
          _start(GetTickCount64()),
          _total(budget),
          _safetyBefore(s.Snapshot())
    {
    }
    ApplyOutcome Run()
    {
        if (! _cancel || _total < 3 || _total > 3000)
        {
            _outcome.result = E_INVALIDARG;
            return _outcome;
        }
        _outcome.result = Call({}, false);
        if (FAILED(_outcome.result))
            return _outcome;
        _original               = std::make_unique<Inventory>(_reply.inventory);
        constexpr auto required = CapabilityAudioDefaults | CapabilityAudioLevels | CapabilityCameraRoute;
        if ((_original->capabilities & required) != required)
        {
            _outcome.result = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            return _outcome;
        }
        const auto* output = FindEndpoint(*_original, DeviceKind::Output, _profile.outputId);
        const auto* input  = FindEndpoint(*_original, DeviceKind::Microphone, _profile.microphoneId);
        bool camera        = false;
        for (uint32_t i = 0; i < _original->cameraCount; ++i)
            camera = camera || (_original->cameras[i].id == _profile.cameraId && _original->cameras[i].availability == Availability::Ready);
        if (! output || ! input || output->availability != Availability::Ready || input->availability != Availability::Ready || ! camera ||
            _original->state.camera.availability != Availability::Ready)
        {
            _outcome.result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            return _outcome;
        }
        const uint32_t firstRole = _profile.audioRoles == AudioRoles::Communications ? 2U : 0U;
        for (uint32_t i = 0; i < 2 && SUCCEEDED(_outcome.result); ++i)
        {
            const auto device           = static_cast<DeviceKind>(i);
            const auto& id              = i ? _profile.microphoneId : _profile.outputId;
            const auto& previousDefault = i ? _original->state.inputDefaults[firstRole] : _original->state.outputDefaults[firstRole];
            const auto* previous        = FindEndpoint(*_original, device, previousDefault);
            const auto* destination     = FindEndpoint(_reply.inventory, device, id);
            if (! destination)
            {
                _outcome.result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
                break;
            }
            // An unknown prior route is treated conservatively; profile application can never imply unmute.
            if ((! previous || previous->muted) && ! destination->muted)
                _outcome.result = AudioMutation(BrokerOperation::SetMute, device, id, 1);
            if (SUCCEEDED(_outcome.result) && _profile.restoreLevels)
                _outcome.result = AudioMutation(BrokerOperation::SetLevel, device, id, i ? _profile.microphoneLevel : _profile.outputLevel);
        }
        for (uint32_t role = firstRole; role < 3 && SUCCEEDED(_outcome.result); ++role)
            for (uint32_t flow = 0; flow < 2 && SUCCEEDED(_outcome.result); ++flow)
                _outcome.result = RouteMutation(static_cast<DeviceKind>(flow), flow ? _profile.microphoneId : _profile.outputId, role);
        if (SUCCEEDED(_outcome.result))
            _outcome.result = CameraMutation();
        if (SUCCEEDED(_outcome.result))
            _outcome.result = CheckSafety();
        if (SUCCEEDED(_outcome.result))
        {
            _outcome.result = Call({}, false);
            if (SUCCEEDED(_outcome.result))
            {
                auto observed            = _reply.inventory.state;
                const auto* actualOutput = FindEndpoint(_reply.inventory, DeviceKind::Output, observed.outputDefaults[firstRole]);
                const auto* actualInput  = FindEndpoint(_reply.inventory, DeviceKind::Microphone, observed.inputDefaults[firstRole]);
                if (actualOutput)
                    observed.output = *actualOutput;
                if (actualInput)
                    observed.microphone = *actualInput;
                if (MatchProfile(_profile, observed) == ProfileMatch::Custom)
                    _outcome.result = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
            }
        }
        if (SUCCEEDED(_outcome.result))
            _outcome.applied = true;
        else
            Rollback();
        return _outcome;
    }

private:
    const Profile& _profile;
    BackendTransport& _backend;
    BrokerReply& _reply;
    const SafetyState& _safety;
    HANDLE _cancel;
    uint64_t _start;
    uint32_t _total;
    std::array<uint64_t, 3> _safetyBefore;
    std::unique_ptr<Inventory> _original;
    std::array<Undo, 11> _undo{};
    size_t _undoCount = 0;
    ApplyOutcome _outcome;
    uint32_t Remaining(bool rollback) const noexcept
    {
        const uint64_t limit = rollback ? _total : _total - _total / 3;
        const auto elapsed   = GetTickCount64() - _start;
        return elapsed < limit ? static_cast<uint32_t>(limit - elapsed) : 0;
    }
    HRESULT Call(const BrokerCommand& command, bool rollback) noexcept
    {
        const auto remaining = Remaining(rollback);
        if (! remaining)
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        const HRESULT hr = _backend.Execute(command, _reply, _cancel, remaining);
        if (ConnectionFailure(hr))
            _outcome.stateKnown = false;
        return hr;
    }
    HRESULT CheckSafety() noexcept
    {
        if (_safety.Snapshot() == _safetyBefore)
            return S_OK;
        _outcome.superseded = true;
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }
    HRESULT AudioMutation(BrokerOperation operation, DeviceKind device, const DeviceId& id, uint32_t value) noexcept
    {
        const HRESULT safe = CheckSafety();
        if (FAILED(safe))
            return safe;
        const auto* endpoint = FindEndpoint(_reply.inventory, device, id);
        if (! endpoint)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        const bool mute         = operation == BrokerOperation::SetMute;
        const uint32_t oldValue = mute ? (endpoint->muted ? 1U : 0U) : endpoint->level;
        if (value == oldValue)
            return S_OK;
        BrokerCommand command{operation, device, id, value, 0, endpoint->generation, mute ? endpoint->muteRevision : endpoint->levelRevision};
        Undo entry{command};
        entry.command.value = oldValue;
        const HRESULT hr    = Call(command, false);
        if (SUCCEEDED(hr))
        {
            ++_outcome.mutations;
            const auto* changed = FindEndpoint(_reply.inventory, device, id);
            entry.ownedRevision = changed ? (mute ? changed->muteRevision : changed->levelRevision) : 0;
            _undo[_undoCount++] = entry;
        }
        else
        {
            const auto* changed = FindEndpoint(_reply.inventory, device, id);
            if (! _outcome.stateKnown || ! changed || (mute ? changed->muteRevision : changed->levelRevision) != command.expectedRevision)
                _outcome.rollbackComplete = false;
        }
        return hr;
    }
    HRESULT RouteMutation(DeviceKind device, const DeviceId& id, uint32_t role) noexcept
    {
        const HRESULT safe = CheckSafety();
        if (FAILED(safe))
            return safe;
        const auto& routes    = device == DeviceKind::Output ? _reply.inventory.state.outputDefaults : _reply.inventory.state.inputDefaults;
        const auto& revisions = device == DeviceKind::Output ? _reply.inventory.outputRoleRevisions : _reply.inventory.inputRoleRevisions;
        if (routes[role] == id)
            return S_OK;
        const auto* target = FindEndpoint(_reply.inventory, device, id);
        if (! target)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        BrokerCommand command{BrokerOperation::SetDefault, device, id, 0, role, target->generation, revisions[role]};
        Undo entry{command};
        entry.command.id                 = routes[role];
        const auto* prior                = FindEndpoint(_reply.inventory, device, routes[role]);
        entry.restorable                 = prior != nullptr;
        entry.command.expectedGeneration = prior ? prior->generation : 0;
        const HRESULT hr                 = Call(command, false);
        if (SUCCEEDED(hr))
        {
            ++_outcome.mutations;
            entry.ownedRevision = revisions[role];
            _undo[_undoCount++] = entry;
        }
        else if (! _outcome.stateKnown || revisions[role] != command.expectedRevision)
            _outcome.rollbackComplete = false;
        return hr;
    }
    HRESULT CameraMutation() noexcept
    {
        const HRESULT safe = CheckSafety();
        if (FAILED(safe))
            return safe;
        if (_reply.inventory.state.camera.sourceId == _profile.cameraId)
            return S_OK;
        BrokerCommand command{BrokerOperation::SetCameraSource, DeviceKind::Camera, _profile.cameraId, 0, 0, 0, _reply.inventory.state.camera.revision};
        Undo entry{command};
        entry.command.id = _reply.inventory.state.camera.sourceId;
        const HRESULT hr = Call(command, false);
        if (SUCCEEDED(hr))
        {
            ++_outcome.mutations;
            entry.ownedRevision = _reply.inventory.state.camera.revision;
            _undo[_undoCount++] = entry;
        }
        else if (! _outcome.stateKnown || _reply.inventory.state.camera.revision != command.expectedRevision)
            _outcome.rollbackComplete = false;
        return hr;
    }
    void Rollback() noexcept
    {
        // Restore only revisions still owned by this transaction. A later safety intent prevents restoring routes or
        // state for that device; its prioritized command applies against the final observed route in the coordinator.
        while (_undoCount)
        {
            auto entry       = _undo[--_undoCount];
            const auto index = static_cast<size_t>(entry.command.device);
            if (! entry.restorable || _safety.Snapshot()[index] != _safetyBefore[index])
            {
                _outcome.rollbackComplete = false;
                continue;
            }
            uint64_t currentRevision = 0;
            if (entry.command.operation == BrokerOperation::SetDefault)
                currentRevision = (index ? _reply.inventory.inputRoleRevisions : _reply.inventory.outputRoleRevisions)[entry.command.role];
            else if (entry.command.operation == BrokerOperation::SetCameraSource)
                currentRevision = _reply.inventory.state.camera.revision;
            else if (const auto* endpoint = FindEndpoint(_reply.inventory, entry.command.device, entry.command.id))
                currentRevision = entry.command.operation == BrokerOperation::SetMute ? endpoint->muteRevision : endpoint->levelRevision;
            if (currentRevision != entry.ownedRevision || ! currentRevision)
            {
                _outcome.rollbackComplete = false;
                continue;
            }
            entry.command.expectedRevision = currentRevision;
            if (FAILED(Call(entry.command, true)))
                _outcome.rollbackComplete = false;
        }
    }
};
#pragma warning(pop)

} // namespace
ApplyOutcome ApplyProfile(
    const Profile& profile, BackendTransport& backend, BrokerReply& reply, const SafetyState& safety, HANDLE cancelEvent, uint32_t timeoutMilliseconds) noexcept
{
    try
    {
        return Transaction(profile, backend, reply, safety, cancelEvent, timeoutMilliseconds).Run();
    }
    catch (...)
    {
        return {E_OUTOFMEMORY, false, false, false};
    }
}
} // namespace AVControl
