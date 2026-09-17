#include "Coordinator.h"
#include <filesystem>
#include <stdexcept>
#include <thread>

namespace
{
uint32_t checks = 0;
void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(description);
}
// Deterministic host scheduler: each real IPC operation runs on an MTA worker, then completes on the caller/UI.
// Deferred start lets tests place simultaneous UI intents without races or sleeps.
class Host final : public RedXeComObject<Host, IRedXeHost>
{
  public:
    wil::com_ptr_nothrow<IRedXeControlWork> pending;
    std::atomic<uint32_t> frames{0};
    uint32_t runs = 0;
    bool reject = false;
    HRESULT STDMETHODCALLTYPE GetDataProvider(const char*, IRedXeDataProvider** value) noexcept override
    {
        if (value)
            *value = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE RequestFrame() noexcept override
    {
        ++frames;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReportWidgetStatus(const char*, const RedXeWidgetStatusReport*) noexcept override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PersistWidgetSettings(const char*, const char*, uint32_t) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Log(const RedXeLogRecord*) noexcept override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueueControlWork(IRedXeControlWork* work) noexcept override
    {
        if (reject)
            return E_ACCESSDENIED;
        if (pending)
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        pending = work;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RequestHostAction(const RedXeHostActionRequest*) noexcept override
    {
        return E_NOTIMPL;
    }
    void RunNext()
    {
        Check(bool(pending), "coordinator queued one bounded work object");
        auto work = std::move(pending);
        wil::unique_event_nothrow cancel;
        Check(SUCCEEDED(cancel.create(wil::EventOptions::ManualReset)), "coordinator fixture cancellation");
        HRESULT result = E_PENDING;
        std::thread worker(
            [&]
            {
                const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                result = FAILED(apartment) ? apartment : work->Run(cancel.get(), 3000);
                if (SUCCEEDED(apartment))
                    CoUninitialize();
            });
        worker.join();
        ++runs;
        work->Complete(result);
    }
    void Drain()
    {
        for (uint32_t i = 0; pending && i < 12; ++i)
            RunNext();
        Check(!pending, "coordinator drains bounded pending lanes without an observation loop");
    }
};
AVControl::ViewCommand Mute(const AVControl::Endpoint& endpoint, AVControl::DeviceKind kind, bool muted)
{
    return {AVControl::ViewCommandKind::SetMuted,
            kind,
            endpoint.id,
            endpoint.generation,
            endpoint.muteRevision,
            muted ? 1U : 0U};
}
} // namespace
uint32_t RunCoordinatorTests()
{
    using namespace AVControl;
    checks = 0;
    wchar_t path[MAX_PATH]{};
    Check(GetModuleFileNameW(nullptr, path, MAX_PATH) != 0, "coordinator helper location");
    const auto executable = std::filesystem::path(path).parent_path() / L"Plugins" / L"AVControlBroker.exe";
    Host host;
    wil::com_ptr_nothrow<Coordinator> coordinator;
    coordinator.attach(new Coordinator(&host, executable.native(), true));
    Check(coordinator->Initialize() == S_OK, "coordinator initializes without starting hardware work");
    coordinator->Prepare();
    Check(!host.pending && host.frames == 0, "inactive coordinator does not start or request periodic work");
    coordinator->SetSubscriberVisible(true);
    coordinator->SetSubscriberVisible(true);
    coordinator->Prepare();
    coordinator->Prepare();
    Check(bool(host.pending), "visible instances share one initial observation");
    host.Drain();
    Check(host.runs == 1 && coordinator->Current().outputCount == 2,
          "shared observation publishes confirmed synthetic inventory");
    const auto initialDeviceRevision = coordinator->DeviceRevision();
    const auto output = coordinator->Current().state.output;
    ViewCommand level{ViewCommandKind::SetLevel, DeviceKind::Output,   output.id,
                      output.generation,         output.levelRevision, 26};
    Check(SUCCEEDED(coordinator->Submit(level)), "first output level accepted");
    level.value = 39;
    Check(SUCCEEDED(coordinator->Submit(level)), "later same-lane level coalesces");
    Check(coordinator->PendingMask() & 8, "pending output level shown before worker execution");
    Check(SUCCEEDED(coordinator->Submit(Mute(output, DeviceKind::Output, true))),
          "output mute accepted beside pending level");
    host.RunNext();
    Check(coordinator->Current().state.output.muted && coordinator->Current().state.output.level == output.level,
          "mute lane runs before older volume edit");
    host.Drain();
    Check(coordinator->Current().state.output.level == 39 && coordinator->Current().state.output.muted,
          "coalesced final level is confirmed and preserves mute");
    Check(coordinator->DeviceRevision() == initialDeviceRevision,
          "level-only acknowledgement does not rebuild device selectors");
    Check(coordinator->PendingMask() == 0, "acknowledged commands clear pending marks");
    level.value = 73;
    Check(SUCCEEDED(coordinator->Submit(level)), "stale gesture enters bounded backend validation");
    host.Drain();
    Check(coordinator->LastResult() == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH) &&
              coordinator->Current().state.output.level == 39,
          "stale field revision cannot overwrite confirmed endpoint level");

    Profile profile;
    Check(profile.id.Assign("studio") && profile.name.Assign("Studio") &&
              profile.outputId.Assign(coordinator->Current().outputs[1].id.View()) &&
              profile.microphoneId.Assign(coordinator->Current().inputs[1].id.View()) &&
              profile.cameraId.Assign(coordinator->Current().cameras[1].id.View()),
          "profile fixture binds second devices");
    profile.restoreLevels = true;
    profile.outputLevel = 64;
    profile.microphoneLevel = 27;
    Check(SUCCEEDED(coordinator->SubmitProfile(profile)), "profile accepted as one coordinator transaction");
    Check(coordinator->SubmitProfile(profile) == HRESULT_FROM_WIN32(ERROR_BUSY),
          "concurrent profile operation rejected");
    Check(coordinator->Submit(level) == HRESULT_FROM_WIN32(ERROR_BUSY),
          "volume from the old route is disabled during profile change");
    Check(SUCCEEDED(coordinator->Submit(Mute(coordinator->Current().state.microphone, DeviceKind::Microphone, true))),
          "safety mute remains available during profile work");
    host.Drain();
    Check(coordinator->LastApply().applied && !coordinator->ProfilePending(),
          "profile completes and publishes transaction outcome");
    Check(MatchProfile(profile, coordinator->Current().state) == ProfileMatch::Active,
          "profile identity waits for complete observed routing");
    Check(coordinator->Current().state.output.muted && coordinator->Current().state.microphone.muted &&
              !coordinator->Current().state.camera.enabled,
          "profile and later safety intent cannot unmute or turn the camera on");
    Check(coordinator->PendingMask() == 0, "profile and safety acknowledgements clear pending controls");
    profile.outputId = coordinator->Current().outputs[0].id;
    profile.microphoneId = coordinator->Current().inputs[0].id;
    profile.audioRoles = AudioRoles::Communications;
    Check(SUCCEEDED(coordinator->SubmitProfile(profile)), "communications profile accepted");
    host.Drain();
    Check(coordinator->Current().state.output.id == profile.outputId &&
              coordinator->Current().state.outputDefaults[0] != profile.outputId &&
              MatchProfile(profile, coordinator->Current().state) == ProfileMatch::Active,
          "communications-only profile controls its selected role without changing console defaults");

    auto camera = coordinator->Current().state.camera;
    Check(SUCCEEDED(coordinator->Submit(
              {ViewCommandKind::SetCameraEnabled, DeviceKind::Camera, camera.sourceId, 0, camera.revision, 1})),
          "explicit camera-on intent accepted before hiding the display");
    host.Drain();
    Check(coordinator->Current().state.camera.enabled, "camera-on is confirmed before display suspension");
    const auto priorLevel = coordinator->Current().state.output.level;
    coordinator->SetSubscriberVisible(false);
    Check(!host.pending, "hiding one of two subscribers keeps the shared route available");
    coordinator->SetSubscriberVisible(false);
    host.Drain();
    const auto hiddenFrames = host.frames.load();
    for (uint32_t i = 0; i < 100; ++i)
        coordinator->Prepare();
    Check(!host.pending && host.frames == hiddenFrames, "hidden coordinator produces no observations or frame loop");
    coordinator->SetSubscriberVisible(true);
    coordinator->Prepare();
    host.Drain();
    Check(coordinator->Current().state.output.availability == Availability::Ready,
          "showing reconnects an owned stopped helper");
    Check(coordinator->Current().state.camera.enabled && coordinator->Current().state.output.level == priorLevel,
          "hiding all AV tiles preserved the camera helper and its confirmed route");
    camera = coordinator->Current().state.camera;
    Check(SUCCEEDED(coordinator->Submit(
              {ViewCommandKind::SetCameraEnabled, DeviceKind::Camera, camera.sourceId, 0, camera.revision, 0})),
          "camera off remains usable after display observation resumes");
    host.Drain();
    coordinator->SetSubscriberVisible(false);
    host.Drain();
    coordinator.reset();

    coordinator.attach(new Coordinator(&host, executable.native(), true));
    Check(coordinator->Initialize() == S_OK, "configuration membership coordinator initialized");
    auto configurations = std::make_unique<std::array<Configuration, 65>>();
    for (size_t i = 0; i < configurations->size(); ++i)
    {
        auto& configuration = (*configurations)[i];
        if (i < 9)
        {
            configuration.count = MaximumProfiles;
            for (uint32_t j = 0; j < MaximumProfiles; ++j)
            {
                auto& item = configuration.profiles[j];
                Check(item.outputId.Assign("retained-output-" + std::to_string(i * MaximumProfiles + j)) &&
                          item.microphoneId.Assign("retained-input-" + std::to_string(i * MaximumProfiles + j)) &&
                          item.cameraId.Assign("retained-camera-" + std::to_string(i * MaximumProfiles + j)),
                      "independent saved binding fixture");
            }
        }
        Check(coordinator->RegisterConfiguration(&configuration) ==
                  (i < 64 ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)),
              "loaded configuration registration is bounded to the host's two-page capacity");
    }
    Check(!host.pending && coordinator->RegisterConfiguration(&(*configurations)[0]) == S_FALSE,
          "registering hidden definitions neither starts hardware work nor duplicates membership");
    coordinator->SetSubscriberVisible(true);
    coordinator->Prepare();
    host.Drain();
    Check(coordinator->Current().truncated,
          "all loaded widget bindings contribute to the shared bounded inventory union");
    const auto membershipRevision = coordinator->DeviceRevision();
    for (size_t i = 4; i < configurations->size(); ++i)
        coordinator->UnregisterConfiguration(&(*configurations)[i]);
    coordinator->Prepare();
    host.Drain();
    Check(!coordinator->Current().truncated && coordinator->DeviceRevision() > membershipRevision,
          "removing widgets rebuilds preferences and clears the displayed overflow notice");
    (*configurations)[4].count = 0;
    Check(coordinator->RegisterConfiguration(&(*configurations)[4]) == S_OK, "retired membership slots can be reused");
    (*configurations)[4] = (*configurations)[0];
    coordinator->ConfigurationChanged();
    coordinator->Prepare();
    host.Drain();
    Check(!coordinator->Current().truncated,
          "identical references from another profile do not consume duplicate capacity");
    (*configurations)[4] = (*configurations)[8];
    coordinator->ConfigurationChanged();
    coordinator->Prepare();
    host.Drain();
    Check(coordinator->Current().truncated, "saved definition edits refresh the helper preferences");
    coordinator->SetSubscriberVisible(false);
    host.Drain();
    for (const auto& configuration : *configurations)
        coordinator->UnregisterConfiguration(&configuration);
    Check(!host.pending, "hidden configuration retirement creates no device or frame loop");
    coordinator.reset();

    host.reject = true;
    coordinator.attach(new Coordinator(&host, executable.native(), true));
    Check(coordinator->Initialize() == S_OK, "rejected-work fixture initialization");
    coordinator->SetSubscriberVisible(true);
    coordinator->Prepare();
    const auto refusedFrames = host.frames.load();
    for (uint32_t i = 0; i < 100; ++i)
        coordinator->Prepare();
    Check(!host.pending && host.frames == refusedFrames && coordinator->LastResult() == E_ACCESSDENIED,
          "host hardware suppression produces no retries, subprocess or polling");
    coordinator->SetSubscriberVisible(false);
    coordinator.reset();
    return checks;
}
