#pragma once

// Logicon settings model shared by the host parser (SettingsV4.cpp), Logicon.dll, and the tests. One parser is the
// single source of truth for the closed settings object the plugin publishes as its static contract. A binding's
// action is a name (PlugInterfaces/Action.h); the parser checks the grammar, and the host resolves the name and
// its target at Start / ApplySettings through IRedXeHost::ValidateAction, which sets KeyBinding::valid.

#include "PlugInterfaces/Action.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <windows.h>

struct yyjson_val;

namespace Logicon
{
inline constexpr char kPluginId[] = "builtin.logicon";
inline constexpr char kMonitorPluginId[] = "builtin.logicon-monitor";
inline constexpr char kMonitorWidgetTypeId[] = "logicon-monitor";
inline constexpr char kActionNamespace[] = "logicon";

inline constexpr uint32_t kKeysPerPage = 9;
inline constexpr uint32_t kMaximumKeyPages = 4;
inline constexpr uint32_t kMaximumKeys = kKeysPerPage * kMaximumKeyPages;
inline constexpr uint32_t kMaximumLabelCodePoints = 16;
inline constexpr uint32_t kMaximumLabelBytes = kMaximumLabelCodePoints * 4;
inline constexpr uint32_t kMaximumTargetBytes = kRedXeMaximumActionTargetBytes;
inline constexpr uint32_t kMaximumActionBytes = kRedXeMaximumActionNameBytes;
inline constexpr uint32_t kMaximumIconBytes = 260;
inline constexpr uint32_t kMinimumBrightness = 1;
inline constexpr uint32_t kMaximumBrightness = 100;
inline constexpr uint32_t kDefaultBrightness = 70;
inline constexpr uint32_t kDialpadButtons = 4;
// The dial and the roller, each with two directions: at most four turn bindings.
inline constexpr uint32_t kDialpadTurns = 4;
inline constexpr uint8_t kControlDial = 0;
inline constexpr uint8_t kControlRoller = 1;
// cw / up.
inline constexpr uint8_t kDirectionForward = 0;
// ccw / down.
inline constexpr uint8_t kDirectionBackward = 1;

// Published static contract (Plugins_API.md schema subset) and defaults for builtin.logicon.
inline constexpr char kSettingsSchema[] =
    R"json({"type":"object","additionalProperties":false,"properties":{)json"
    R"json("brightness":{"type":"integer","minimum":1,"maximum":100},)json"
    R"json("restoreLogoOnExit":{"type":"boolean"},)json"
    R"json("pageButtons":{"type":"string","enum":["keyPages","dashboardPages"]},)json"
    R"json("keys":{"type":"array","minItems":0,"maxItems":36,"items":{"type":"object","additionalProperties":false,)json"
    R"json("required":["slot"],"properties":{)json"
    R"json("page":{"type":"integer","minimum":0,"maximum":3},)json"
    R"json("slot":{"type":"integer","minimum":0,"maximum":8},)json"
    R"json("action":{"type":"string","pattern":"^[a-z][a-zA-Z0-9]*(\\.[a-z][a-zA-Z0-9]*){1,3}$"},)json"
    R"json("target":{"type":"string","minLength":0,"maxLength":512},)json"
    R"json("label":{"type":"string","minLength":0,"maxLength":16},)json"
    R"json("icon":{"type":"string","minLength":0,"maxLength":260},)json"
    R"json("color":{"type":"string","pattern":"^#[0-9A-Fa-f]{6}$"},)json"
    R"json("face":{"type":"string","enum":["none","clock","pageIndicator","cpu","memory","gpu"]})json"
    R"json(}}},)json"
    R"json("dialpad":{"type":"object","additionalProperties":false,"properties":{)json"
    R"json("turns":{"type":"array","minItems":0,"maxItems":4,"items":{"type":"object","additionalProperties":false,)json"
    R"json("required":["control","direction"],"properties":{)json"
    R"json("control":{"type":"string","enum":["dial","roller"]},)json"
    R"json("direction":{"type":"string","enum":["cw","ccw","up","down"]},)json"
    R"json("action":{"type":"string","pattern":"^[a-z][a-zA-Z0-9]*(\\.[a-z][a-zA-Z0-9]*){1,3}$"},)json"
    R"json("target":{"type":"string","minLength":0,"maxLength":512})json"
    R"json(}}},)json"
    R"json("buttons":{"type":"array","minItems":0,"maxItems":4,"items":{"type":"object","additionalProperties":false,)json"
    R"json("required":["button"],"properties":{)json"
    R"json("button":{"type":"integer","minimum":0,"maximum":3},)json"
    R"json("action":{"type":"string","pattern":"^[a-z][a-zA-Z0-9]*(\\.[a-z][a-zA-Z0-9]*){1,3}$"},)json"
    R"json("target":{"type":"string","minLength":0,"maxLength":512})json"
    R"json(}}}}})json"
    R"json(}})json";
inline constexpr char kSettingsDefaults[] =
    R"json({"brightness":70,"restoreLogoOnExit":true,"pageButtons":"keyPages","keys":[],)json"
    R"json("dialpad":{"turns":[],"buttons":[]}})json";

// The Debug-only monitor tile accepts no settings members.
inline constexpr char kMonitorSettingsSchema[] = R"json({"type":"object","additionalProperties":false})json";
inline constexpr char kMonitorSettingsDefaults[] = R"json({})json";

enum class KeyFace : uint8_t
{
    None = 0,
    Clock,
    PageIndicator,
    // System Data percentages (builtin.system-data: cpu.summary, memory.summary, gpu.adapter).
    Cpu,
    Memory,
    Gpu,
};

[[nodiscard]] constexpr bool IsSystemFace(KeyFace face) noexcept
{
    return face == KeyFace::Cpu || face == KeyFace::Memory || face == KeyFace::Gpu;
}

enum class PageButtons : uint8_t
{
    KeyPages = 0,
    DashboardPages,
};

// One binding: a key (page, slot), a dialpad button (slot = button), or a dialpad turn (control, direction).
struct KeyBinding final
{
    uint8_t page = 0;
    uint8_t slot = 0;
    uint8_t control = kControlDial;
    uint8_t direction = kDirectionForward;
    KeyFace face = KeyFace::None;
    bool hasColor = false;
    // Whether the host resolved the action and accepted its target (IRedXeHost::ValidateAction). A binding with
    // an unsatisfied target or an unknown published verb draws the red "!" face and never dispatches.
    bool valid = true;
    uint32_t colorRgb = 0;
    uint32_t actionBytes = 0;
    uint32_t targetBytes = 0;
    uint32_t labelBytes = 0;
    uint32_t iconBytes = 0;
    std::array<char, kMaximumActionBytes + 1> action{};
    std::array<char, kMaximumTargetBytes + 1> target{};
    std::array<char, kMaximumLabelBytes + 1> label{};
    std::array<char, kMaximumIconBytes + 1> icon{};

    [[nodiscard]] bool HasAction() const noexcept
    {
        return actionBytes != 0;
    }
    [[nodiscard]] std::string_view Action() const noexcept
    {
        return std::string_view(action.data(), actionBytes);
    }
    [[nodiscard]] std::string_view Target() const noexcept
    {
        return std::string_view(target.data(), targetBytes);
    }
    [[nodiscard]] std::string_view Label() const noexcept
    {
        return std::string_view(label.data(), labelBytes);
    }
    [[nodiscard]] std::string_view Icon() const noexcept
    {
        return std::string_view(icon.data(), iconBytes);
    }
    // True for the actions the service executes itself on its lane (the published "logicon" namespace).
    [[nodiscard]] bool IsLocal() const noexcept
    {
        return HasAction() && RedXeActionInNamespace(action.data(), kActionNamespace);
    }
};

// The dialpad's bindings. Button bindings reuse KeyBinding with slot = button index (0 Back, 1 Forward, 2 Button 6,
// 3 Button 7); turn bindings use control and direction. Their face, label, icon, and color stay empty because the
// dialpad has no display.
struct DialpadSettings final
{
    uint32_t buttonCount = 0;
    uint32_t turnCount = 0;
    std::array<KeyBinding, kDialpadButtons> buttons{};
    std::array<KeyBinding, kDialpadTurns> turns{};

    // The binding for one button, or null when it is unbound.
    [[nodiscard]] const KeyBinding* Button(uint32_t button) const noexcept;
    // The binding for one control and direction, or null when that turn does nothing.
    [[nodiscard]] const KeyBinding* Turn(uint8_t control, uint8_t direction) const noexcept;
};

struct Settings final
{
    uint32_t brightness = kDefaultBrightness;
    bool restoreLogoOnExit = true;
    PageButtons pageButtons = PageButtons::KeyPages;
    uint32_t keyCount = 0;
    std::array<KeyBinding, kMaximumKeys> keys{};
    DialpadSettings dialpad{};

    // Number of key pages a binding refers to: highest page + 1, at least 1.
    [[nodiscard]] uint32_t KeyPageCount() const noexcept;
    // The binding for (page, slot), or null when the slot is blank.
    [[nodiscard]] const KeyBinding* Find(uint32_t page, uint32_t slot) const noexcept;
    // Whether any key page binds a System Data face (the service subscribes only then).
    [[nodiscard]] bool UsesSystemFaces() const noexcept;
};

// Parses the complete effective settings object (defaults already merged, or authored members only: omitted scalar
// members keep their defaults). Unknown members, wrong types, out-of-range values, unknown enum strings, an action
// outside the name grammar, more than kMaximumKeys entries, a duplicate (page, slot), button, or (control,
// direction) fail with ERROR_INVALID_DATA and a bounded ASCII diagnostic naming the member. Whether a name resolves
// and whether a target satisfies it is the host's job (IRedXeHost::ValidateAction).
[[nodiscard]] HRESULT ParseSettings(yyjson_val* object, Settings& settings, char* diagnostic,
                                    size_t diagnosticCapacity) noexcept;

// Parses a compact UTF-8 JSON object with ParseSettings.
[[nodiscard]] HRESULT ParseSettingsJson(std::string_view json, Settings& settings, char* diagnostic,
                                        size_t diagnosticCapacity) noexcept;

[[nodiscard]] const char* FaceName(KeyFace face) noexcept;
[[nodiscard]] bool FaceFromName(std::string_view name, KeyFace& face) noexcept;
[[nodiscard]] const char* ControlName(uint8_t control) noexcept;
[[nodiscard]] const char* DirectionName(uint8_t control, uint8_t direction) noexcept;

// Parses "<pageId>/<ordinal>" or "<ordinal>". pageId is empty for the bare form.
[[nodiscard]] bool ParseWidgetTarget(std::string_view target, std::string_view& pageId, uint32_t& ordinal) noexcept;
} // namespace Logicon
