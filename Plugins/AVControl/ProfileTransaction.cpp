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
    return {_values[0].load(std::memory_order_acquire), _values[1].load(std::memory_order_acquire),
            _values[2].load(std::memory_order_acquire)};
}
namespace
{
const Endpoint* FindEndpoint(const Inventory& inventory, DeviceKind device, const DeviceId& id) noexcept
{
    const auto& values = device == DeviceKind::Output ? inventory.outputs : inventory.inputs;
    const auto count = device == DeviceKind::Output ? inventory.outputCount : inventory.inputCount;
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
    bool restorable = true;
};
class Transaction final
{
  public:
    Transaction(const Profile& p, BackendTransport& b, BrokerReply& r, const SafetyState& s, HANDLE c,
                uint32_t budget) noexcept
        : profile(p), backend(b), reply(r), safety(s), cancel(c), start(GetTickCount64()), total(budget),
          safetyBefore(s.Snapshot())
    {
    }
    ApplyOutcome Run()
    {
        if (!cancel || total < 3 || total > 3000)
        {
            outcome.result = E_INVALIDARG;
            return outcome;
        }
        outcome.result = Call({}, false);
        if (FAILED(outcome.result))
            return outcome;
        original = std::make_unique<Inventory>(reply.inventory);
        constexpr auto required = CapabilityAudioDefaults | CapabilityAudioLevels | CapabilityCameraRoute;
        if ((original->capabilities & required) != required)
        {
            outcome.result = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            return outcome;
        }
        const auto* output = FindEndpoint(*original, DeviceKind::Output, profile.outputId);
        const auto* input = FindEndpoint(*original, DeviceKind::Microphone, profile.microphoneId);
        bool camera = false;
        for (uint32_t i = 0; i < original->cameraCount; ++i)
            camera = camera || (original->cameras[i].id == profile.cameraId &&
                                original->cameras[i].availability == Availability::Ready);
        if (!output || !input || output->availability != Availability::Ready ||
            input->availability != Availability::Ready || !camera ||
            original->state.camera.availability != Availability::Ready)
        {
            outcome.result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            return outcome;
        }
        const uint32_t firstRole = profile.audioRoles == AudioRoles::Communications ? 2U : 0U;
        for (uint32_t i = 0; i < 2 && SUCCEEDED(outcome.result); ++i)
        {
            const auto device = static_cast<DeviceKind>(i);
            const auto& id = i ? profile.microphoneId : profile.outputId;
            const auto& previousDefault =
                i ? original->state.inputDefaults[firstRole] : original->state.outputDefaults[firstRole];
            const auto* previous = FindEndpoint(*original, device, previousDefault);
            const auto* destination = FindEndpoint(reply.inventory, device, id);
            if (!destination)
            {
                outcome.result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
                break;
            }
            // An unknown prior route is treated conservatively; profile application can never imply unmute.
            if ((!previous || previous->muted) && !destination->muted)
                outcome.result = AudioMutation(BrokerOperation::SetMute, device, id, 1);
            if (SUCCEEDED(outcome.result) && profile.restoreLevels)
                outcome.result = AudioMutation(BrokerOperation::SetLevel, device, id,
                                               i ? profile.microphoneLevel : profile.outputLevel);
        }
        for (uint32_t role = firstRole; role < 3 && SUCCEEDED(outcome.result); ++role)
            for (uint32_t flow = 0; flow < 2 && SUCCEEDED(outcome.result); ++flow)
                outcome.result =
                    RouteMutation(static_cast<DeviceKind>(flow), flow ? profile.microphoneId : profile.outputId, role);
        if (SUCCEEDED(outcome.result))
            outcome.result = CameraMutation();
        if (SUCCEEDED(outcome.result))
            outcome.result = CheckSafety();
        if (SUCCEEDED(outcome.result))
        {
            outcome.result = Call({}, false);
            if (SUCCEEDED(outcome.result))
            {
                auto observed = reply.inventory.state;
                const auto* actualOutput =
                    FindEndpoint(reply.inventory, DeviceKind::Output, observed.outputDefaults[firstRole]);
                const auto* actualInput =
                    FindEndpoint(reply.inventory, DeviceKind::Microphone, observed.inputDefaults[firstRole]);
                if (actualOutput)
                    observed.output = *actualOutput;
                if (actualInput)
                    observed.microphone = *actualInput;
                if (MatchProfile(profile, observed) == ProfileMatch::Custom)
                    outcome.result = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
            }
        }
        if (SUCCEEDED(outcome.result))
            outcome.applied = true;
        else
            Rollback();
        return outcome;
    }

  private:
    const Profile& profile;
    BackendTransport& backend;
    BrokerReply& reply;
    const SafetyState& safety;
    HANDLE cancel;
    uint64_t start;
    uint32_t total;
    std::array<uint64_t, 3> safetyBefore;
    std::unique_ptr<Inventory> original;
    std::array<Undo, 11> undo{};
    size_t undoCount = 0;
    ApplyOutcome outcome;
    uint32_t Remaining(bool rollback) const noexcept
    {
        const uint64_t limit = rollback ? total : total - total / 3;
        const auto elapsed = GetTickCount64() - start;
        return elapsed < limit ? static_cast<uint32_t>(limit - elapsed) : 0;
    }
    HRESULT Call(const BrokerCommand& command, bool rollback) noexcept
    {
        const auto remaining = Remaining(rollback);
        if (!remaining)
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        const HRESULT hr = backend.Execute(command, reply, cancel, remaining);
        if (ConnectionFailure(hr))
            outcome.stateKnown = false;
        return hr;
    }
    HRESULT CheckSafety() noexcept
    {
        if (safety.Snapshot() == safetyBefore)
            return S_OK;
        outcome.superseded = true;
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }
    HRESULT AudioMutation(BrokerOperation operation, DeviceKind device, const DeviceId& id, uint32_t value) noexcept
    {
        const HRESULT safe = CheckSafety();
        if (FAILED(safe))
            return safe;
        const auto* endpoint = FindEndpoint(reply.inventory, device, id);
        if (!endpoint)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        const bool mute = operation == BrokerOperation::SetMute;
        const uint32_t oldValue = mute ? (endpoint->muted ? 1U : 0U) : endpoint->level;
        if (value == oldValue)
            return S_OK;
        BrokerCommand command{operation,
                              device,
                              id,
                              value,
                              0,
                              endpoint->generation,
                              mute ? endpoint->muteRevision : endpoint->levelRevision};
        Undo entry{command};
        entry.command.value = oldValue;
        const HRESULT hr = Call(command, false);
        if (SUCCEEDED(hr))
        {
            ++outcome.mutations;
            const auto* changed = FindEndpoint(reply.inventory, device, id);
            entry.ownedRevision = changed ? (mute ? changed->muteRevision : changed->levelRevision) : 0;
            undo[undoCount++] = entry;
        }
        else
        {
            const auto* changed = FindEndpoint(reply.inventory, device, id);
            if (!outcome.stateKnown || !changed ||
                (mute ? changed->muteRevision : changed->levelRevision) != command.expectedRevision)
                outcome.rollbackComplete = false;
        }
        return hr;
    }
    HRESULT RouteMutation(DeviceKind device, const DeviceId& id, uint32_t role) noexcept
    {
        const HRESULT safe = CheckSafety();
        if (FAILED(safe))
            return safe;
        const auto& routes =
            device == DeviceKind::Output ? reply.inventory.state.outputDefaults : reply.inventory.state.inputDefaults;
        const auto& revisions =
            device == DeviceKind::Output ? reply.inventory.outputRoleRevisions : reply.inventory.inputRoleRevisions;
        if (routes[role] == id)
            return S_OK;
        const auto* target = FindEndpoint(reply.inventory, device, id);
        if (!target)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        BrokerCommand command{BrokerOperation::SetDefault, device, id, 0, role, target->generation, revisions[role]};
        Undo entry{command};
        entry.command.id = routes[role];
        const auto* prior = FindEndpoint(reply.inventory, device, routes[role]);
        entry.restorable = prior != nullptr;
        entry.command.expectedGeneration = prior ? prior->generation : 0;
        const HRESULT hr = Call(command, false);
        if (SUCCEEDED(hr))
        {
            ++outcome.mutations;
            entry.ownedRevision = revisions[role];
            undo[undoCount++] = entry;
        }
        else if (!outcome.stateKnown || revisions[role] != command.expectedRevision)
            outcome.rollbackComplete = false;
        return hr;
    }
    HRESULT CameraMutation() noexcept
    {
        const HRESULT safe = CheckSafety();
        if (FAILED(safe))
            return safe;
        if (reply.inventory.state.camera.sourceId == profile.cameraId)
            return S_OK;
        BrokerCommand command{BrokerOperation::SetCameraSource,     DeviceKind::Camera, profile.cameraId, 0, 0, 0,
                              reply.inventory.state.camera.revision};
        Undo entry{command};
        entry.command.id = reply.inventory.state.camera.sourceId;
        const HRESULT hr = Call(command, false);
        if (SUCCEEDED(hr))
        {
            ++outcome.mutations;
            entry.ownedRevision = reply.inventory.state.camera.revision;
            undo[undoCount++] = entry;
        }
        else if (!outcome.stateKnown || reply.inventory.state.camera.revision != command.expectedRevision)
            outcome.rollbackComplete = false;
        return hr;
    }
    void Rollback() noexcept
    {
        // Restore only revisions still owned by this transaction. A later safety intent prevents restoring routes or
        // state for that device; its prioritized command applies against the final observed route in the coordinator.
        while (undoCount)
        {
            auto entry = undo[--undoCount];
            const auto index = static_cast<size_t>(entry.command.device);
            if (!entry.restorable || safety.Snapshot()[index] != safetyBefore[index])
            {
                outcome.rollbackComplete = false;
                continue;
            }
            uint64_t currentRevision = 0;
            if (entry.command.operation == BrokerOperation::SetDefault)
                currentRevision = (index ? reply.inventory.inputRoleRevisions
                                         : reply.inventory.outputRoleRevisions)[entry.command.role];
            else if (entry.command.operation == BrokerOperation::SetCameraSource)
                currentRevision = reply.inventory.state.camera.revision;
            else if (const auto* endpoint = FindEndpoint(reply.inventory, entry.command.device, entry.command.id))
                currentRevision = entry.command.operation == BrokerOperation::SetMute ? endpoint->muteRevision
                                                                                      : endpoint->levelRevision;
            if (currentRevision != entry.ownedRevision || !currentRevision)
            {
                outcome.rollbackComplete = false;
                continue;
            }
            entry.command.expectedRevision = currentRevision;
            if (FAILED(Call(entry.command, true)))
                outcome.rollbackComplete = false;
        }
    }
};
} // namespace
ApplyOutcome ApplyProfile(const Profile& profile, BackendTransport& backend, BrokerReply& reply,
                          const SafetyState& safety, HANDLE cancelEvent, uint32_t timeoutMilliseconds) noexcept
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
