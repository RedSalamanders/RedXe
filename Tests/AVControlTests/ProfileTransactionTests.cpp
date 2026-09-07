#include "AVControlBroker.h"
#include <filesystem>
#include <stdexcept>

namespace
{
uint32_t transactionChecks = 0;
void Check(bool condition, const char* message)
{
    ++transactionChecks;
    if (!condition)
        throw std::runtime_error(message);
}
class Intercept final : public AVControl::BackendTransport
{
  public:
    AVControl::Broker broker;
    AVControl::SafetyState* safety = nullptr;
    uint32_t mutations = 0, failAt = 0, safetyAt = 0;
    bool removeCameraCapability = false, externalLevelChange = false;
    HRESULT Execute(const AVControl::BrokerCommand& command, AVControl::BrokerReply& reply, HANDLE cancel,
                    uint32_t timeout) noexcept override
    {
        using namespace AVControl;
        if (command.operation != BrokerOperation::Observe && ++mutations == failAt)
        {
            if (externalLevelChange)
            {
                const auto& endpoint = reply.inventory.outputs[1];
                BrokerCommand external{BrokerOperation::SetLevel, DeviceKind::Output,    endpoint.id, 91, 0,
                                       endpoint.generation,       endpoint.levelRevision};
                const HRESULT hr = broker.Execute(external, reply, cancel, timeout);
                if (FAILED(hr))
                    return hr;
            }
            return E_FAIL;
        }
        const HRESULT hr = broker.Execute(command, reply, cancel, timeout);
        if (SUCCEEDED(hr) && removeCameraCapability)
            reply.inventory.capabilities &= ~CapabilityCameraRoute;
        if (SUCCEEDED(hr) && safety && command.operation != BrokerOperation::Observe && mutations == safetyAt)
            safety->Publish(DeviceKind::Microphone, true);
        return hr;
    }
};
} // namespace
uint32_t RunProfileTransactionTests()
{
    using namespace AVControl;
    wchar_t executable[MAX_PATH]{};
    Check(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "profile fixture location");
    const auto helper = std::filesystem::path(executable).parent_path() / L"Plugins" / L"AVControlBroker.exe";
    Profile profile;
    Check(profile.id.Assign("meeting") && profile.name.Assign("Meeting") &&
              profile.outputId.Assign("fixture-output-2") && profile.microphoneId.Assign("fixture-input-2") &&
              profile.cameraId.Assign("fixture-camera-2"),
          "profile fixture IDs");
    profile.restoreLevels = true;
    profile.outputLevel = 48;
    profile.microphoneLevel = 61;
    wil::unique_event_nothrow cancel;
    Check(SUCCEEDED(cancel.create(wil::EventOptions::ManualReset)), "profile cancellation event");
    auto reply = std::make_unique<BrokerReply>();
    Intercept backend;
    SafetyState safety;
    auto start = [&]
    {
        backend.mutations = backend.failAt = backend.safetyAt = 0;
        backend.removeCameraCapability = backend.externalLevelChange = false;
        backend.safety = nullptr;
        Check(SUCCEEDED(backend.broker.Start(helper.native(), true)), "profile synthetic broker starts");
        Check(backend.broker.Execute({}, *reply, cancel.get(), 3000) == S_OK, "profile initial snapshot");
    };
    start();
    const auto applied = ApplyProfile(profile, backend, *reply, safety, cancel.get(), 3000);
    Check(applied.applied && applied.result == S_OK && applied.mutations == 10,
          "profile applies every required mutation");
    for (size_t role = 0; role < 3; ++role)
        Check(reply->inventory.state.outputDefaults[role] == profile.outputId &&
                  reply->inventory.state.inputDefaults[role] == profile.microphoneId,
              "profile verifies every render/capture role");
    Check(reply->inventory.state.microphone.muted && !reply->inventory.state.camera.enabled,
          "profile preserves mic mute and camera off across source changes");
    Check(reply->inventory.state.camera.sourceId == profile.cameraId && reply->inventory.state.output.level == 48 &&
              reply->inventory.state.microphone.level == 61,
          "profile restores optional levels and camera binding");
    backend.mutations = 0;
    const auto again = ApplyProfile(profile, backend, *reply, safety, cancel.get(), 3000);
    Check(again.applied && again.mutations == 0 && backend.mutations == 0, "already-active profile is idempotent");
    for (uint32_t failure = 1; failure <= 10; ++failure)
    {
        start();
        backend.failAt = failure;
        const auto result = ApplyProfile(profile, backend, *reply, safety, cancel.get(), 3000);
        Check(result.result == E_FAIL && !result.applied && result.rollbackComplete,
              "each failed stage rolls back owned earlier fields");
        for (size_t role = 0; role < 3; ++role)
            Check(reply->inventory.state.outputDefaults[role] == reply->inventory.outputs[0].id &&
                      reply->inventory.state.inputDefaults[role] == reply->inventory.inputs[0].id,
                  "failed profile restores original audio roles");
        Check(reply->inventory.outputs[1].level == 65 && reply->inventory.inputs[1].level == 72 &&
                  !reply->inventory.inputs[1].muted,
              "failed profile restores destination levels and transaction-owned mute");
        Check(reply->inventory.state.camera.sourceId == reply->inventory.cameras[0].id &&
                  !reply->inventory.state.camera.enabled,
              "failed profile never enables camera");
    }
    start();
    backend.removeCameraCapability = true;
    auto result = ApplyProfile(profile, backend, *reply, safety, cancel.get(), 3000);
    Check(!result.applied && result.result == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) && backend.mutations == 0,
          "unsupported camera fails preflight without audio side effects");
    start();
    Profile missing = profile;
    Check(missing.cameraId.Assign("absent-camera"), "missing camera fixture");
    result = ApplyProfile(missing, backend, *reply, safety, cancel.get(), 3000);
    Check(result.result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && backend.mutations == 0,
          "missing binding prevents all mutations");
    start();
    backend.failAt = 5;
    backend.externalLevelChange = true;
    result = ApplyProfile(profile, backend, *reply, safety, cancel.get(), 3000);
    Check(!result.applied && !result.rollbackComplete && reply->inventory.outputs[1].level == 91,
          "rollback preserves newer external level revision and reports partial application");
    start();
    backend.safety = &safety;
    backend.safetyAt = 4;
    result = ApplyProfile(profile, backend, *reply, safety, cancel.get(), 3000);
    Check(!result.applied && result.superseded && !result.rollbackComplete && reply->inventory.inputs[1].muted,
          "later mute supersedes profile and prevents rollback from unmuting destination");
    start();
    profile.audioRoles = AudioRoles::Communications;
    result = ApplyProfile(profile, backend, *reply, safety, cancel.get(), 3000);
    Check(result.applied && reply->inventory.state.outputDefaults[2] == profile.outputId &&
              reply->inventory.state.inputDefaults[2] == profile.microphoneId,
          "communications profile applies its exact role");
    for (size_t role = 0; role < 2; ++role)
        Check(reply->inventory.state.outputDefaults[role] == reply->inventory.outputs[0].id &&
                  reply->inventory.state.inputDefaults[role] == reply->inventory.inputs[0].id,
              "communications profile preserves other roles");
    backend.broker.Stop();
    return transactionChecks;
}
