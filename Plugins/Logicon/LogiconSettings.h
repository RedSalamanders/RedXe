#pragma once

// Logicon settings model shared by the host parser (SettingsV4.cpp), Logicon.dll, and the tests. One parser is the
// single source of truth for the closed settings object the plugin publishes as its static contract.

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

inline constexpr uint32_t kKeysPerPage = 9;
inline constexpr uint32_t kMaximumKeyPages = 4;
inline constexpr uint32_t kMaximumKeys = kKeysPerPage * kMaximumKeyPages;
inline constexpr uint32_t kMaximumLabelCodePoints = 16;
inline constexpr uint32_t kMaximumLabelBytes = kMaximumLabelCodePoints * 4;
inline constexpr uint32_t kMaximumTargetBytes = 512;
inline constexpr uint32_t kMaximumIconBytes = 260;
inline constexpr uint32_t kMinimumBrightness = 1;
inline constexpr uint32_t kMaximumBrightness = 100;
inline constexpr uint32_t kDefaultBrightness = 70;
inline constexpr uint32_t kDialpadButtons = 4;

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
    R"json("action":{"type":"string","enum":["none","page.next","page.previous","page.goto","widget.raise",)json"
    R"json("widget.dismiss","widget.toggle","launch","keys","keyPage.next","keyPage.previous"]},)json"
    R"json("target":{"type":"string","minLength":0,"maxLength":512},)json"
    R"json("label":{"type":"string","minLength":0,"maxLength":16},)json"
    R"json("icon":{"type":"string","minLength":0,"maxLength":260},)json"
    R"json("color":{"type":"string","pattern":"^#[0-9A-Fa-f]{6}$"},)json"
    R"json("face":{"type":"string","enum":["none","clock","pageIndicator","cpu","memory","gpu"]})json"
    R"json(}}},)json"
    R"json("dialpad":{"type":"object","additionalProperties":false,"properties":{)json"
    R"json("dial":{"type":"string","enum":["none","volume","page","keyPage","brightness"]},)json"
    R"json("roller":{"type":"string","enum":["none","volume","page","keyPage","brightness"]},)json"
    R"json("buttons":{"type":"array","minItems":0,"maxItems":4,"items":{"type":"object","additionalProperties":false,)json"
    R"json("required":["button"],"properties":{)json"
    R"json("button":{"type":"integer","minimum":0,"maximum":3},)json"
    R"json("action":{"type":"string","enum":["none","page.next","page.previous","page.goto","widget.raise",)json"
    R"json("widget.dismiss","widget.toggle","launch","keys","keyPage.next","keyPage.previous"]},)json"
    R"json("target":{"type":"string","minLength":0,"maxLength":512})json"
    R"json(}}}}})json"
    R"json(}})json";
inline constexpr char kSettingsDefaults[] =
    R"json({"brightness":70,"restoreLogoOnExit":true,"pageButtons":"keyPages","keys":[],)json"
    R"json("dialpad":{"dial":"none","roller":"none","buttons":[]}})json";

// The Debug-only monitor tile accepts no settings members.
inline constexpr char kMonitorSettingsSchema[] = R"json({"type":"object","additionalProperties":false})json";
inline constexpr char kMonitorSettingsDefaults[] = R"json({})json";

enum class KeyAction : uint8_t
{
    None = 0,
    PageNext,
    PagePrevious,
    PageGoTo,
    WidgetRaise,
    WidgetDismiss,
    WidgetToggle,
    Launch,
    Keys,
    KeyPageNext,
    KeyPagePrevious,
};

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

// What one detent of the dial or the roller does.
enum class DialAction : uint8_t
{
    None = 0,
    // volume-up / volume-down media keys.
    Volume,
    // PageNext / PagePrevious host actions.
    Page,
    // Logicon key page next / previous.
    KeyPage,
    // Keypad brightness ±5 until the next settings apply.
    Brightness,
};

// Media/system keys accepted as the target of the "keys" action.
enum class MediaKey : uint8_t
{
    None = 0,
    VolumeUp,
    VolumeDown,
    Mute,
    PlayPause,
    NextTrack,
    PreviousTrack,
};

struct KeyBinding final
{
    uint8_t page = 0;
    uint8_t slot = 0;
    KeyAction action = KeyAction::None;
    KeyFace face = KeyFace::None;
    bool hasColor = false;
    uint32_t colorRgb = 0;
    uint32_t targetBytes = 0;
    uint32_t labelBytes = 0;
    uint32_t iconBytes = 0;
    std::array<char, kMaximumTargetBytes + 1> target{};
    std::array<char, kMaximumLabelBytes + 1> label{};
    std::array<char, kMaximumIconBytes + 1> icon{};

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
};

// The dialpad's bindings. Button bindings reuse KeyBinding with slot = button index (0 Back, 1 Forward, 2 Button 6,
// 3 Button 7); their face, label, icon, and color stay empty because the dialpad has no display.
struct DialpadSettings final
{
    DialAction dial = DialAction::None;
    DialAction roller = DialAction::None;
    uint32_t buttonCount = 0;
    std::array<KeyBinding, kDialpadButtons> buttons{};

    // The binding for one button, or null when it is unbound.
    [[nodiscard]] const KeyBinding* Button(uint32_t button) const noexcept;
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
// members keep their defaults). Unknown members, wrong types, out-of-range values, unknown enum strings, more than
// kMaximumKeys entries, or a duplicate (page, slot) pair fail with ERROR_INVALID_DATA and a bounded ASCII diagnostic
// naming the member. Target semantics (paths, URIs, page references, media key names) are not checked here; see
// ClassifyTarget.
[[nodiscard]] HRESULT ParseSettings(yyjson_val* object, Settings& settings, char* diagnostic,
                                    size_t diagnosticCapacity) noexcept;

// Parses a compact UTF-8 JSON object with ParseSettings.
[[nodiscard]] HRESULT ParseSettingsJson(std::string_view json, Settings& settings, char* diagnostic,
                                        size_t diagnosticCapacity) noexcept;

[[nodiscard]] const char* ActionName(KeyAction action) noexcept;
[[nodiscard]] bool ActionFromName(std::string_view name, KeyAction& action) noexcept;
[[nodiscard]] const char* FaceName(KeyFace face) noexcept;
[[nodiscard]] bool FaceFromName(std::string_view name, KeyFace& face) noexcept;
[[nodiscard]] bool MediaKeyFromName(std::string_view name, MediaKey& key) noexcept;
[[nodiscard]] const char* MediaKeyName(MediaKey key) noexcept;
[[nodiscard]] const char* DialActionName(DialAction action) noexcept;
[[nodiscard]] bool DialActionFromName(std::string_view name, DialAction& action) noexcept;

// Whether a binding's target satisfies its action: an absolute path or URI for launch, a media key name for keys, a
// non-empty page id for page.goto, and "<pageId>/<ordinal>" or a bare ordinal for the widget actions. Actions
// without a target ignore it.
[[nodiscard]] bool TargetIsValid(const KeyBinding& binding) noexcept;

// Parses "<pageId>/<ordinal>" or "<ordinal>". pageId is empty for the bare form.
[[nodiscard]] bool ParseWidgetTarget(std::string_view target, std::string_view& pageId, uint32_t& ordinal) noexcept;
} // namespace Logicon
