#pragma once

// Pure parsers for action targets (PlugInterfaces/Action.h). The host validates every binding's target from the
// owning descriptor with ValidateTarget, and executors parse the same grammars at execution, so validation and
// execution agree by construction. Nothing here touches a window, a monitor, a device, or the heap.

#include "PlugInterfaces/Action.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <windows.h>

namespace RedXeActions
{
inline constexpr uint32_t kMaximumChords = 8;

// Checks target against descriptor.targetKind, its bounds and options, and the descriptor flags (an optional target
// may be empty; a destructive action needs its confirming target; suffixes are allowed only when flagged). Returns
// S_OK, or E_INVALIDARG when the target does not satisfy the grammar.
[[nodiscard]] HRESULT ValidateTarget(const RedXeActionDescriptor& descriptor, std::string_view target) noexcept;

// Splits "value@selector" at the last '@'. Returns the value part; suffix receives the selector or stays empty.
[[nodiscard]] std::string_view SplitSuffix(std::string_view target, std::string_view& suffix) noexcept;

// An absolute Win32 path (drive letter, colon, separator) or a UNC path.
[[nodiscard]] bool IsAbsolutePath(std::string_view value) noexcept;
// IsAbsolutePath, or a URI with an alphabetic scheme of at least two characters followed by ':'.
[[nodiscard]] bool IsPathOrUri(std::string_view value) noexcept;
// A URI with an alphabetic scheme of at least two characters followed by ':'.
[[nodiscard]] bool IsUri(std::string_view value) noexcept;

// "<exe>" or "\"<exe with spaces>\"" followed by optional arguments; the executable must be an absolute path.
struct CommandLine final
{
    std::string_view executable;
    std::string_view arguments;
};
[[nodiscard]] bool ParseCommandLine(std::string_view value, CommandLine& parsed) noexcept;

// "<pageId>/<ordinal>" or "<ordinal>" (at most three digits, below 512). pageId is empty for the bare form.
[[nodiscard]] bool ParseWidgetRef(std::string_view value, std::string_view& pageId, uint32_t& ordinal) noexcept;

// A decimal integer within [minimum, maximum] with an optional leading '-'.
[[nodiscard]] bool ParseInteger(std::string_view value, int32_t minimum, int32_t maximum, int32_t& parsed) noexcept;

// "n" (absolute, within bounds), "+n", or "-n" (relative, magnitude at most maximum - minimum).
struct Delta final
{
    int32_t value = 0;
    bool relative = false;
};
[[nodiscard]] bool ParseDelta(std::string_view value, int32_t minimum, int32_t maximum, Delta& parsed) noexcept;

// "now" (parsed.seconds = 0, now = true) or a delay within bounds.
struct NowOrSeconds final
{
    uint32_t seconds = 0;
    bool now = false;
};
[[nodiscard]] bool ParseNowOrSeconds(std::string_view value, int32_t minimum, int32_t maximum,
                                     NowOrSeconds& parsed) noexcept;

// One of the '|'-separated options; index receives its zero-based position.
[[nodiscard]] bool ParseEnum(std::string_view value, const char* options, uint32_t& index) noexcept;

enum ChordModifiers : uint32_t
{
    ChordModifierNone = 0,
    ChordModifierControl = 1U << 0U,
    ChordModifierShift = 1U << 1U,
    ChordModifierAlt = 1U << 2U,
    ChordModifierWin = 1U << 3U,
};

struct KeyChord final
{
    uint32_t modifiers = ChordModifierNone;
    uint16_t virtualKey = 0;
    // The key sits in the extended block (navigation cluster, right-side modifiers, numeric divide/enter).
    bool extended = false;
};

struct ChordSequence final
{
    std::array<KeyChord, kMaximumChords> chords{};
    uint32_t count = 0;
};

// "chord(,chord)*" with chord = "(Mod+)*Key"; modifiers Ctrl, Shift, Alt, Win; key names per Plugins_Actions.md
// (letters, digits, F1-F24, named keys, Num*, punctuation, VK:<hex>). Names are case-insensitive.
[[nodiscard]] bool ParseChords(std::string_view value, ChordSequence& parsed) noexcept;

struct MonitorSelector final
{
    enum class Kind : uint8_t
    {
        Primary = 0,
        Xeneon,
        All,
        Index,
        Name,
    };
    Kind kind = Kind::Primary;
    uint32_t index = 0;
    std::string_view name;
};
// "primary", "xeneon", "all", "<n>" (1-based), or "name:<substring>". allowAll rejects "all" when false.
[[nodiscard]] bool ParseMonitorSelector(std::string_view value, bool allowAll, MonitorSelector& parsed) noexcept;

struct WindowSelector final
{
    enum class Kind : uint8_t
    {
        Foreground = 0,
        Executable,
        Class,
        Title,
    };
    Kind kind = Kind::Foreground;
    std::string_view value;
};
// "foreground", "exe:<image.exe>", "class:<class>", or "title:<substring>".
[[nodiscard]] bool ParseWindowSelector(std::string_view value, WindowSelector& parsed) noexcept;

struct Point final
{
    int32_t x = 0;
    int32_t y = 0;
    bool relative = false;
    bool center = false;
    bool hasMonitor = false;
    MonitorSelector monitor{};
};
// "x,y", "+dx,+dy" (both signed), or "center", each optionally followed by "@<monitor>".
[[nodiscard]] bool ParsePoint(std::string_view value, Point& parsed) noexcept;

struct Meeting final
{
    // 9 through 11 decimal digits.
    uint64_t number = 0;
    std::string_view passcode;
};
// "https://<host>/j/<id>[?pwd=<passcode>...]" or "<id>[:<passcode>]".
[[nodiscard]] bool ParseMeeting(std::string_view value, Meeting& parsed) noexcept;
} // namespace RedXeActions
