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

// Published static contract (Plugins_API.md schema subset) and defaults for builtin.zoom.
inline constexpr char kSettingsSchema[] = R"json({"type":"object","additionalProperties":false,"properties":{)json"
                                          R"json("clientId":{"type":"string","minLength":0,"maxLength":128},)json"
                                          R"json("redirectPort":{"type":"integer","minimum":1024,"maximum":65535},)json"
                                          R"json("domain":{"type":"string","minLength":1,"maxLength":128},)json"
                                          R"json("displayName":{"type":"string","minLength":0,"maxLength":128},)json"
                                          R"json("autoConnect":{"type":"boolean"})json"
                                          R"json(}})json";
inline constexpr char kSettingsDefaults[] =
    R"json({"clientId":"","redirectPort":48123,"domain":"zoom.us","displayName":"","autoConnect":false})json";

struct Settings final
{
    uint32_t clientIdBytes = 0;
    uint32_t domainBytes = 0;
    uint32_t displayNameBytes = 0;
    uint32_t redirectPort = kDefaultRedirectPort;
    bool autoConnect = false;
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
};

// Parses the complete effective settings object. Unknown members, wrong types, an empty clientId, a port outside
// 1024–65535, or an overlong string fail with ERROR_INVALID_DATA and a bounded ASCII diagnostic naming the member.
[[nodiscard]] HRESULT ParseSettings(yyjson_val* object, Settings& settings, char* diagnostic,
                                    size_t diagnosticCapacity) noexcept;

// Parses a compact UTF-8 JSON object with ParseSettings.
[[nodiscard]] HRESULT ParseSettingsJson(std::string_view json, Settings& settings, char* diagnostic,
                                        size_t diagnosticCapacity) noexcept;
} // namespace Zoom
