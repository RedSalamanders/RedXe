#pragma once

// Browser-only Zoom actions need neither a Zoom Workplace installation nor a Marketplace application.

#include <cstddef>
#include <string_view>
#include <windows.h>

struct yyjson_val;

namespace Zoom
{
inline constexpr char kPluginId[] = "builtin.zoom";
inline constexpr char kActionNamespace[] = "zoom";
inline constexpr char kWebJoinPage[] = "https://app.zoom.us/wc";
inline constexpr char kSettingsSchema[] = R"json({"type":"object","additionalProperties":false})json";
inline constexpr char kSettingsDefaults[] = "{}";

struct Settings final
{
    bool operator==(const Settings&) const noexcept = default;
};

[[nodiscard]] HRESULT ParseSettings(yyjson_val* object, Settings& settings, char* diagnostic,
                                    size_t diagnosticCapacity) noexcept;
[[nodiscard]] HRESULT ParseSettingsJson(std::string_view json, Settings& settings, char* diagnostic,
                                        size_t diagnosticCapacity) noexcept;

// Accept only HTTPS meeting links on zoom.us or a subdomain, with a 9–11 digit /j/ meeting ID.
// The complete invite URL, including its opaque pwd query, is passed unchanged to the browser.
[[nodiscard]] bool IsMeetingUrl(std::string_view url) noexcept;
} // namespace Zoom
