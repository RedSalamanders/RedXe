#pragma once

// Studio Clock widget settings model shared by the host parser (RedXe/Settings*.cpp) and StudioClock.dll, so the
// member list, the date formats, the glowPercent range, and the defaults exist once. Header-only; no allocation, no
// exceptions.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace StudioClock
{
inline constexpr char kPluginId[] = "builtin.studio-clock";
inline constexpr char kWidgetTypeId[] = "studio-clock";

// The order of the date digits. kDateFormatNames holds the `dateFormat` value of each, in this order.
enum class DateFormat : uint32_t
{
    DayMonthYear = 0,
    MonthDayYear = 1,
    YearMonthDay = 2,
};

inline constexpr std::array<std::string_view, 3> kDateFormatNames{"dd-mm-yyyy", "mm-dd-yyyy", "yyyy-mm-dd"};

inline constexpr uint32_t kMinimumGlowPercent = 0;
inline constexpr uint32_t kMaximumGlowPercent = 100;

// The compact defaults the host merges under authored keys and the DLL publishes. StudioClock.cpp checks at compile
// time that its typed defaults are exactly these.
inline constexpr char kDefaultsJson[] =
    R"json({"showSecondProgress":true,"externalDotsAlwaysOn":true,"showSeconds":true,"secondsColor":"#FF1616","showDate":false,"dateFormat":"dd-mm-yyyy","timeColor":"#FF1616","glowPercent":35})json";

// The published plugin schema (Plugins_API.md subset: closed object, booleans, colors, a string enum, a bounded
// integer).
inline constexpr char kSchemaJson[] =
    R"json({"type":"object","additionalProperties":false,"properties":{"showSecondProgress":{"type":"boolean"},"externalDotsAlwaysOn":{"type":"boolean"},"showSeconds":{"type":"boolean"},"secondsColor":{"type":"string","pattern":"^#[0-9A-Fa-f]{6}$"},"showDate":{"type":"boolean"},"dateFormat":{"type":"string","enum":["dd-mm-yyyy","mm-dd-yyyy","yyyy-mm-dd"]},"timeColor":{"type":"string","pattern":"^#[0-9A-Fa-f]{6}$"},"glowPercent":{"type":"integer","minimum":0,"maximum":100}}})json";

// Every member; each is required once the host has merged the defaults.
inline constexpr std::array<const char*, 8> kSettingsKeys{
    "showSecondProgress", "externalDotsAlwaysOn", "showSeconds", "secondsColor",
    "showDate",           "dateFormat",           "timeColor",   "glowPercent",
};

// Names are exact, like every other settings enum.
[[nodiscard]] constexpr bool TryParseDateFormat(std::string_view text, DateFormat& format) noexcept
{
    for (size_t index = 0; index < kDateFormatNames.size(); ++index)
    {
        if (text == kDateFormatNames[index])
        {
            format = static_cast<DateFormat>(index);
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr bool IsValidGlowPercent(uint64_t value) noexcept
{
    return value >= kMinimumGlowPercent && value <= kMaximumGlowPercent;
}
} // namespace StudioClock
