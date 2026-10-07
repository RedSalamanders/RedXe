#pragma once

// Browser-only Zoom actions need neither a Zoom Workplace installation nor a Marketplace application. zoom.action.dll
// is a dedicated action DLL without settings; this model only validates the `services` entry for builtin.zoom that
// earlier releases configured, which the host still accepts and ignores (Plugins_Zoom.md).

#include <cstddef>
#include <string_view>
#include <windows.h>

struct yyjson_val;

namespace Zoom
{
inline constexpr char kPluginId[] = "builtin.zoom";
inline constexpr char kActionNamespace[] = "zoom";
inline constexpr char kWebJoinPage[] = "https://app.zoom.us/wc";

struct Settings final
{
    bool operator==(const Settings&) const noexcept = default;
};

// Accepts an object holding only the retired v1.0.102 members, each with any value; any other member is an error.
[[nodiscard]] HRESULT ParseSettings(yyjson_val* object, Settings& settings, char* diagnostic,
                                    size_t diagnosticCapacity) noexcept;
[[nodiscard]] HRESULT ParseSettingsJson(std::string_view json, Settings& settings, char* diagnostic,
                                        size_t diagnosticCapacity) noexcept;
} // namespace Zoom
