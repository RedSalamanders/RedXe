#include "AVControlLayout.h"
#include "AVControlModel.h"

#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

uint32_t RunControlWorkQueueTests();
uint32_t RunNativeViewTests();
uint32_t RunBrokerTests();
uint32_t RunProfileTransactionTests();

namespace
{
uint32_t checks = 0;
void Require(bool condition, const char* description)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(description);
}
constexpr std::string_view valid =
    R"({"profiles":[{"id":"studio","name":"Studio","outputId":"output","microphoneId":"input","cameraId":"camera","audioRoles":"all","restoreLevels":true,"outputLevel":68,"microphoneLevel":72}]})";

void SettingsTests()
{
    using namespace AVControl;
    Configuration config;
    Require(ParseConfiguration(valid, config) == S_OK && config.count == 1, "valid profile parses");
    std::array<char, MaximumSettingsBytes> bytes{};
    uint32_t written = 0;
    Require(SerializeConfiguration(config, bytes.data(), bytes.size(), written) == S_OK, "profile serializes");
    Configuration roundtrip;
    Require(ParseConfiguration({bytes.data(), written}, roundtrip) == S_OK, "serialized profile reparses");
    Require(roundtrip.profiles[0].cameraId.View() == "camera" && roundtrip.profiles[0].microphoneLevel == 72,
            "profile roundtrip preserves fields");
    constexpr std::array invalid{R"({})",
                                 R"({"profiles":null})",
                                 R"({"profiles":[],"extra":true})",
                                 R"({"profiles":[],"profiles":[]})",
                                 R"({"profiles":[{}]})",
                                 R"({"profiles":[{}, {}, {}, {}, {}]})",
                                 R"({"profiles":[]} trailing)"};
    for (std::string_view value : invalid)
    {
        Require(FAILED(ParseConfiguration(value, config)), "malformed configuration rejected");
        Require(config.count == 1 && config.profiles[0].id.View() == "studio",
                "failure preserves previous configuration");
    }
    const auto replace = [](std::string_view from, std::string_view to)
    {
        std::string result{valid};
        result.replace(result.find(from), from.size(), to);
        return result;
    };
    for (const std::string& value :
         {replace("\"studio\"", "\"invalid id\""), replace("\"Studio\"", "\"\\u0001name\""),
          replace("\"Studio\"", "\"\\uD800\""), replace("\"camera\"", "\"cam\\u0000era\""), replace("\"output\"", "42"),
          replace("\"all\"", "\"multimedia\""), replace("\"restoreLevels\":true", "\"restoreLevels\":1"),
          replace("\"outputLevel\":68", "\"outputLevel\":101"), replace("\"outputLevel\":68", "\"outputLevel\":68.0"),
          replace("\"outputLevel\":68", "\"outputLevel\":-1"), replace("\"microphoneLevel\":72", "\"outputLevel\":72")})
        Require(FAILED(ParseConfiguration(value, config)), "strict profile semantics rejected");

    const auto object = valid.substr(valid.find('[') + 1, valid.rfind(']') - valid.find('[') - 1);
    std::string duplicate = "{\"profiles\":[" + std::string{object} + ',' + std::string{object} + "]}";
    const size_t last = duplicate.rfind("studio");
    duplicate.replace(last, 6, "STUDIO");
    ConfigurationError reason = ConfigurationError::None;
    Require(FAILED(ParseConfiguration(duplicate, config, &reason)) && reason == ConfigurationError::DuplicateProfile,
            "case-insensitive duplicate id rejected");
    Require(ParseConfiguration(replace("Studio", "Réunion 🎤"), config) == S_OK, "Unicode profile name accepted");
    Require(ParseConfiguration(replace("Studio", std::string(49, 'x')), config) == E_INVALIDARG,
            "49 scalar name rejected");
    Require(ParseConfiguration(replace("Studio", std::string(48, 'x')), config) == S_OK, "48 scalar name accepted");
    Require(ParseConfiguration(replace("\"camera\"", '"' + std::string(1025, 'x') + '"'), config) == E_INVALIDARG,
            "oversize device id never truncated");
    Require(ParseConfiguration(std::string(MaximumSettingsBytes + 1, ' '), config) == E_INVALIDARG,
            "serialized settings ceiling enforced");
    bytes.fill('!');
    Require(FAILED(SerializeConfiguration(config, bytes.data(), 1, written)) && written == 0 && bytes[0] == '!',
            "failed serialization publishes no partial bytes");
    Require(ParseConfiguration(R"({"profiles":[]})", config) == S_OK && config.count == 0,
            "fresh machine has empty profiles");
}

void GestureAndMatchingTests()
{
    using namespace AVControl;
    Configuration config;
    Require(ParseConfiguration(valid, config) == S_OK, "gesture profile fixture");
    const Profile& profile = config.profiles[0];
    ConfirmedState state;
    state.output.id = profile.outputId;
    state.microphone.id = profile.microphoneId;
    state.camera.sourceId = profile.cameraId;
    state.output.availability = state.microphone.availability = state.camera.availability = Availability::Ready;
    state.output.level = profile.outputLevel;
    state.microphone.level = profile.microphoneLevel;
    state.outputDefaults.fill(profile.outputId);
    state.inputDefaults.fill(profile.microphoneId);
    Require(MatchProfile(profile, state) == ProfileMatch::Active, "all bindings verified active");
    state.output.muted = state.microphone.muted = true;
    state.camera.enabled = false;
    Require(MatchProfile(profile, state) == ProfileMatch::Active, "mute and camera-off do not change profile identity");
    ++state.output.level;
    Require(MatchProfile(profile, state) == ProfileMatch::Adjusted, "optional level drift is adjusted");
    state.camera.availability = Availability::Unknown;
    Require(MatchProfile(profile, state) == ProfileMatch::Custom, "unknown camera is never active");
    state.camera.availability = Availability::Ready;
    Require(state.outputDefaults[0].Assign("different"), "external role fixture");
    Require(MatchProfile(profile, state) == ProfileMatch::Custom, "every requested role is checked");
    Profile communications = profile;
    communications.audioRoles = AudioRoles::Communications;
    communications.restoreLevels = false;
    Require(MatchProfile(communications, state) == ProfileMatch::Active,
            "communications scope ignores unrelated roles");

    LevelGesture gesture;
    uint32_t committed = 777;
    Require(gesture.Begin(state.output, 4) && gesture.Preview(91), "gesture previews a draft");
    Require(state.output.level != 91 && state.output.muted, "preview never changes confirmed level or mute");
    gesture.Cancel();
    Require(!gesture.Commit(state.output, 4, committed) && committed == 777, "cancel sends no level command");
    Require(gesture.Begin(state.output, 4) && gesture.Preview(0) && gesture.Commit(state.output, 4, committed) &&
                committed == 0,
            "zero slider endpoint commits");
    Require(!gesture.Commit(state.output, 4, committed), "gesture can commit only once");
    Require(gesture.Begin(state.output, 4) && gesture.Preview(100), "hundred slider endpoint previews");
    ++state.output.levelRevision;
    Require(!gesture.Commit(state.output, 4, committed), "external level change cancels stale draft");
    Require(gesture.Begin(state.output, 4) && !gesture.Commit(state.output, 5, committed),
            "layout replacement cancels gesture");
    Require(gesture.Begin(state.output, 4), "device generation gesture begins");
    ++state.output.generation;
    Require(!gesture.Commit(state.output, 4, committed), "device generation replacement cancels gesture");
    Require(StepLevel(98, 5) == 100 && StepLevel(2, -5) == 0 && StepLevel(50, 1) == 51,
            "steps clamp and preserve keyboard precision");
    Require(StepLevel(50, INT32_MAX) == 100 && StepLevel(50, INT32_MIN) == 0, "step arithmetic cannot overflow");
}

void LayoutTests()
{
    using namespace AVControl;
    const auto validate = [](float width, float height)
    {
        const LiveLayout layout = LayoutLive(width, height);
        Require(layout.density != Density::Unusable, "supported rectangle has usable density");
        std::vector<Rect> targets(layout.toggles.begin(), layout.toggles.end());
        targets.insert(targets.end(), layout.sliders.begin(), layout.sliders.end());
        targets.push_back(layout.profileSelector);
        for (size_t i = 0; i < targets.size(); ++i)
        {
            const Rect& r = targets[i];
            Require(r.width >= 48 && r.height >= 48, "every touch target respects 48 logical pixel floor");
            Require(r.x >= 0 && r.y >= 0 && r.x + r.width <= width + 0.01f && r.y + r.height <= height + 0.01f,
                    "every target lies inside the tile");
            for (size_t j = 0; j < i; ++j)
            {
                const Rect& previous = targets[j];
                Require(r.x + r.width <= previous.x + 0.01f || previous.x + previous.width <= r.x + 0.01f ||
                            r.y + r.height <= previous.y + 0.01f || previous.y + previous.height <= r.y + 0.01f,
                        "live hit targets do not overlap");
            }
        }
    };
    constexpr std::array<std::array<float, 2>, 10> examples{{{1280, 720},
                                                             {640, 720},
                                                             {1280, 360},
                                                             {640, 360},
                                                             {320, 720},
                                                             {320, 360},
                                                             {640, 180},
                                                             {320, 180},
                                                             {160, 180},
                                                             {360, 2560}}};
    for (const auto& size : examples)
        validate(size[0], size[1]);
    for (float width : {160.0f, 319.0f, 320.0f, 321.0f, 799.0f, 800.0f, 801.0f, 1280.0f})
        for (float height : {180.0f, 299.0f, 300.0f, 301.0f, 479.0f, 480.0f, 481.0f, 619.0f, 620.0f, 621.0f, 720.0f})
            validate(width, height);
    Require(LayoutLive(159, 180).density == Density::Unusable && LayoutLive(160, 179).density == Density::Unusable,
            "below-minimum placements rejected");
    Require(LayoutLive(std::numeric_limits<float>::infinity(), 720).density == Density::Unusable,
            "invalid dimensions rejected");
}
} // namespace

#include "AudioRevisionState.h"
void AudioRevisionTests()
{
    AVControl::AudioRevisionState revisions;
    auto initial = revisions.Observe(.701f, false);
    Require(initial.level == 1 && initial.mute == 1, "audio revision baseline is initialized once");
    auto next = revisions.Observe(.704f, false);
    Require(next.level == 2 && next.mute == 1, "fractional changes within one displayed percent invalidate a gesture");
    next = revisions.Observe(.701f, false);
    Require(next.level == 3 && next.mute == 1, "change back before UI observation retains external level history");
    next = revisions.Observe(.701f, true);
    Require(next.level == 3 && next.mute == 2, "mute notification does not change level identity");
    next = revisions.Observe(.701f, true);
    Require(next.level == 3 && next.mute == 2, "duplicate readback and callback do not increment revisions");
    next = revisions.Observe(std::numeric_limits<float>::quiet_NaN(), false);
    Require(next.level == 3 && next.mute == 2, "invalid audio callback does not publish a new identity");
    Require(revisions.Current().level == 3 && revisions.Current().mute == 2,
            "latest callback revisions can be checked immediately before mutation");
}
uint32_t RunCoordinatorTests();
uint32_t RunInventorySelectionTests();
uint32_t RunAudioBackendTests();
uint32_t RunModuleTests();
uint32_t RunCameraMediaTests();
uint32_t RunCameraChannelTests();
uint32_t RunCameraBridgeTests();
uint32_t RunCameraActivationTests();
uint32_t RunCameraCaptureTests();
uint32_t RunCameraCrossProcessTests();
uint32_t RunCameraControllerTests();
int RunCameraBridgeChild(const wchar_t* identifier);
int RunCameraWatchdogChild(const wchar_t* identifier, const wchar_t* stage);
uint32_t RunCameraWatchdogTests();
uint32_t RunTextTransportTests();
uint32_t RunWidgetTextClientTests();
uint32_t RunAccessibilityHostTests();
int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc == 3 && std::wstring_view(argv[1]) == L"--camera-bridge-fixture")
            return RunCameraBridgeChild(argv[2]);
        if (argc == 4 && std::wstring_view(argv[1]) == L"--camera-watchdog-fixture")
            return RunCameraWatchdogChild(argv[2], argv[3]);
        if (argc != 1)
            return 2;
        SettingsTests();
        GestureAndMatchingTests();
        LayoutTests();
        AudioRevisionTests();
        checks += RunTextTransportTests();
        checks += RunWidgetTextClientTests();
        checks += RunAccessibilityHostTests();
        checks += RunInventorySelectionTests();
        checks += RunAudioBackendTests();
        checks += RunControlWorkQueueTests();
        checks += RunNativeViewTests();
        checks += RunBrokerTests();
        checks += RunProfileTransactionTests();
        checks += RunCoordinatorTests();
        checks += RunModuleTests();
        checks += RunCameraMediaTests();
        checks += RunCameraChannelTests();
        checks += RunCameraBridgeTests();
        checks += RunCameraActivationTests();
        checks += RunCameraCaptureTests();
        checks += RunCameraCrossProcessTests();
        checks += RunCameraControllerTests();
        checks += RunCameraWatchdogTests();
        std::printf("PASS AVControl: %u checks (synthetic model and layout; no hardware changes)\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL AVControl: %s\n", error.what());
        return 1;
    }
}
