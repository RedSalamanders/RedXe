#pragma once

// Zoom service settings model shared by the host parser (SettingsV4.cpp), zoom.action.dll, and the tests. One
// parser is the single source of truth for the closed settings object the plugin publishes as its static contract.
// No OAuth secret or token is ever a settings member: the client id identifies the Marketplace app, and tokens
// live in Windows Credential Manager (ZoomAuth.h).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <windows.h>

struct yyjson_val;

namespace Zoom
{
inline constexpr char kPluginId[] = "builtin.zoom";
inline constexpr char kActionNamespace[] = "zoom";

inline constexpr uint32_t kMaximumClientIdBytes = 128;
inline constexpr uint32_t kMaximumDomainBytes = 128;
inline constexpr uint32_t kMaximumDisplayNameBytes = 128;
inline constexpr uint32_t kMinimumRedirectPort = 1024;
inline constexpr uint32_t kMaximumRedirectPort = 65535;
inline constexpr uint32_t kDefaultRedirectPort = 48123;
inline constexpr char kDefaultDomain[] = "zoom.us";
inline constexpr uint32_t kMaximumLabelBytes = 64;

// How the service reaches Zoom: the Plugin SDK session, the client's own meeting controls through its
// accessibility tree (ZoomLocal.h), or the SDK with the local path as the fallback whenever the SDK cannot serve
// (no credential, the Marketplace app refused by the account, or no SDK in the build).
enum class Mode : uint8_t
{
    Auto = 0,
    Sdk,
    Local,
};

// The meeting-toolbar button names (substrings, case-insensitive) the local path matches: three state pairs and
// four buttons to press. Zoom localizes them; the defaults are the English Zoom Workplace 7.1.5 client
// (com.zoom.client.langid 1033), whose mute button reads "Unmute, currently muted, …" / "Mute, currently
// unmuted, …", video "Start my video" / "Stop my video", hand "Raise hand" / "Lower hand", and whose share,
// record, leave, and end buttons read "Share, Alt+S", "Record", "Leave, Alt+Q", "End, Alt+Q".
enum class Label : uint8_t
{
    Muted = 0,
    Unmuted,
    VideoOn,
    VideoOff,
    HandRaised,
    HandLowered,
    Share,
    Record,
    Leave,
    End,
    Count,
};
inline constexpr std::array<const char*, static_cast<size_t>(Label::Count)> kLabelNames{
    "muted", "unmuted", "videoOn", "videoOff", "handRaised", "handLowered", "share", "record", "leave", "end"};
inline constexpr std::array<const char*, static_cast<size_t>(Label::Count)> kLabelDefaults{
    "currently muted", "currently unmuted",
    "Stop my video",   "Start my video",
    "Lower hand",      "Raise hand",
    "Share,",          "Record",
    "Leave,",          "End,"};

// Published static contract (Plugins_API.md schema subset) and defaults for builtin.zoom.
inline constexpr char kSettingsSchema[] =
    R"json({"type":"object","additionalProperties":false,"properties":{)json"
    R"json("clientId":{"type":"string","minLength":0,"maxLength":128},)json"
    R"json("redirectPort":{"type":"integer","minimum":1024,"maximum":65535},)json"
    R"json("domain":{"type":"string","minLength":1,"maxLength":128},)json"
    R"json("displayName":{"type":"string","minLength":0,"maxLength":128},)json"
    R"json("autoConnect":{"type":"boolean"},)json"
    R"json("mode":{"type":"string","enum":["auto","sdk","local"]},)json"
    R"json("labels":{"type":"object","additionalProperties":false,"properties":{)json"
    R"json("muted":{"type":"string","minLength":1,"maxLength":64},)json"
    R"json("unmuted":{"type":"string","minLength":1,"maxLength":64},)json"
    R"json("videoOn":{"type":"string","minLength":1,"maxLength":64},)json"
    R"json("videoOff":{"type":"string","minLength":1,"maxLength":64},)json"
    R"json("handRaised":{"type":"string","minLength":1,"maxLength":64},)json"
    R"json("handLowered":{"type":"string","minLength":1,"maxLength":64},)json"
    R"json("share":{"type":"string","minLength":1,"maxLength":64},)json"
    R"json("record":{"type":"string","minLength":1,"maxLength":64},)json"
    R"json("leave":{"type":"string","minLength":1,"maxLength":64},)json"
    R"json("end":{"type":"string","minLength":1,"maxLength":64}}})json"
    R"json(}})json";
inline constexpr char kSettingsDefaults[] =
    R"json({"clientId":"","redirectPort":48123,"domain":"zoom.us","displayName":"","autoConnect":false,)json"
    R"json("mode":"auto","labels":{"muted":"currently muted","unmuted":"currently unmuted",)json"
    R"json("videoOn":"Stop my video","videoOff":"Start my video","handRaised":"Lower hand","handLowered":"Raise hand",)json"
    R"json("share":"Share,","record":"Record","leave":"Leave,","end":"End,"}})json";

struct Settings final
{
    uint32_t clientIdBytes = 0;
    uint32_t domainBytes = 0;
    uint32_t displayNameBytes = 0;
    uint32_t redirectPort = kDefaultRedirectPort;
    bool autoConnect = false;
    Mode mode = Mode::Auto;
    std::array<std::array<char, kMaximumLabelBytes + 1>, static_cast<size_t>(Label::Count)> labels{};
    std::array<char, kMaximumClientIdBytes + 1> clientId{};
    std::array<char, kMaximumDomainBytes + 1> domain{};
    std::array<char, kMaximumDisplayNameBytes + 1> displayName{};

    [[nodiscard]] std::string_view ClientId() const noexcept
    {
        return std::string_view(clientId.data(), clientIdBytes);
    }
    [[nodiscard]] std::string_view Domain() const noexcept
    {
        return std::string_view(domain.data(), domainBytes);
    }
    [[nodiscard]] std::string_view DisplayName() const noexcept
    {
        return std::string_view(displayName.data(), displayNameBytes);
    }
    [[nodiscard]] std::string_view LabelText(Label label) const noexcept
    {
        return std::string_view(labels[static_cast<size_t>(label)].data());
    }
};

// Parses the complete effective settings object. Unknown members, wrong types, an empty clientId (unless mode is
// local, which needs no Marketplace app), a port outside 1024–65535, or an overlong string fail with
// ERROR_INVALID_DATA and a bounded ASCII diagnostic naming the member. Labels left out keep their defaults.
[[nodiscard]] HRESULT ParseSettings(yyjson_val* object, Settings& settings, char* diagnostic,
                                    size_t diagnosticCapacity) noexcept;

// Parses a compact UTF-8 JSON object with ParseSettings.
[[nodiscard]] HRESULT ParseSettingsJson(std::string_view json, Settings& settings, char* diagnostic,
                                        size_t diagnosticCapacity) noexcept;
} // namespace Zoom
