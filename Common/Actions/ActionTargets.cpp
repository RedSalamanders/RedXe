#include "ActionTargets.h"

#include <cstring>

namespace RedXeActions
{
namespace
{
[[nodiscard]] constexpr char Lower(char value) noexcept
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

[[nodiscard]] constexpr bool IsAlpha(char value) noexcept
{
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

[[nodiscard]] constexpr bool IsDigit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

[[nodiscard]] bool EqualsIgnoreCase(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (size_t index = 0; index < left.size(); ++index)
    {
        if (Lower(left[index]) != Lower(right[index]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ParseUnsigned(std::string_view value, uint64_t maximum, uint64_t& parsed) noexcept
{
    if (value.empty() || value.size() > 19)
    {
        return false;
    }
    uint64_t total = 0;
    for (const char character : value)
    {
        if (!IsDigit(character))
        {
            return false;
        }
        total = total * 10U + static_cast<uint64_t>(character - '0');
        if (total > maximum)
        {
            return false;
        }
    }
    parsed = total;
    return true;
}

struct NamedKey final
{
    const char* name;
    uint16_t virtualKey;
    bool extended;
};

// Names outside the single-letter, digit, and function-key ranges. Extended keys carry the E0 prefix in scan-code
// injection so applications that read scan codes see the navigation cluster rather than the numeric keypad.
constexpr NamedKey kNamedKeys[] = {
    {"enter", VK_RETURN, false},    {"esc", VK_ESCAPE, false},
    {"escape", VK_ESCAPE, false},   {"tab", VK_TAB, false},
    {"space", VK_SPACE, false},     {"backspace", VK_BACK, false},
    {"delete", VK_DELETE, true},    {"del", VK_DELETE, true},
    {"insert", VK_INSERT, true},    {"home", VK_HOME, true},
    {"end", VK_END, true},          {"pageup", VK_PRIOR, true},
    {"pagedown", VK_NEXT, true},    {"left", VK_LEFT, true},
    {"right", VK_RIGHT, true},      {"up", VK_UP, true},
    {"down", VK_DOWN, true},        {"printscreen", VK_SNAPSHOT, true},
    {"pause", VK_PAUSE, false},     {"capslock", VK_CAPITAL, false},
    {"numlock", VK_NUMLOCK, true},  {"scrolllock", VK_SCROLL, false},
    {"apps", VK_APPS, true},        {"num0", VK_NUMPAD0, false},
    {"num1", VK_NUMPAD1, false},    {"num2", VK_NUMPAD2, false},
    {"num3", VK_NUMPAD3, false},    {"num4", VK_NUMPAD4, false},
    {"num5", VK_NUMPAD5, false},    {"num6", VK_NUMPAD6, false},
    {"num7", VK_NUMPAD7, false},    {"num8", VK_NUMPAD8, false},
    {"num9", VK_NUMPAD9, false},    {"numadd", VK_ADD, false},
    {"numsub", VK_SUBTRACT, false}, {"nummul", VK_MULTIPLY, false},
    {"numdiv", VK_DIVIDE, true},    {"numdot", VK_DECIMAL, false},
    {"plus", VK_OEM_PLUS, false},   {"minus", VK_OEM_MINUS, false},
    {"comma", VK_OEM_COMMA, false}, {"period", VK_OEM_PERIOD, false},
    {"semicolon", VK_OEM_1, false}, {"quote", VK_OEM_7, false},
    {"slash", VK_OEM_2, false},     {"backslash", VK_OEM_5, false},
    {"lbracket", VK_OEM_4, false},  {"rbracket", VK_OEM_6, false},
    {"grave", VK_OEM_3, false},     {"win", VK_LWIN, true},
    {"ctrl", VK_CONTROL, false},    {"shift", VK_SHIFT, false},
    {"alt", VK_MENU, false},
};

[[nodiscard]] bool ParseKeyName(std::string_view name, KeyChord& chord) noexcept
{
    if (name.empty())
    {
        return false;
    }
    if (name.size() == 1)
    {
        const char character = Lower(name[0]);
        if (character >= 'a' && character <= 'z')
        {
            chord.virtualKey = static_cast<uint16_t>('A' + (character - 'a'));
            return true;
        }
        if (IsDigit(character))
        {
            chord.virtualKey = static_cast<uint16_t>(character);
            return true;
        }
        return false;
    }
    if ((name[0] == 'F' || name[0] == 'f') && name.size() <= 3)
    {
        uint64_t number = 0;
        if (ParseUnsigned(name.substr(1), 24, number) && number >= 1)
        {
            chord.virtualKey = static_cast<uint16_t>(VK_F1 + (number - 1));
            return true;
        }
        return false;
    }
    if (name.size() > 3 && (name[0] == 'V' || name[0] == 'v') && (name[1] == 'K' || name[1] == 'k') && name[2] == ':')
    {
        const std::string_view digits = name.substr(3);
        if (digits.empty() || digits.size() > 4)
        {
            return false;
        }
        uint32_t value = 0;
        for (const char character : digits)
        {
            const char lower = Lower(character);
            uint32_t digit = 0;
            if (IsDigit(lower))
            {
                digit = static_cast<uint32_t>(lower - '0');
            }
            else if (lower >= 'a' && lower <= 'f')
            {
                digit = static_cast<uint32_t>(lower - 'a') + 10U;
            }
            else
            {
                return false;
            }
            value = (value << 4U) | digit;
        }
        if (value == 0 || value > 0xFE)
        {
            return false;
        }
        chord.virtualKey = static_cast<uint16_t>(value);
        return true;
    }
    for (const NamedKey& candidate : kNamedKeys)
    {
        if (EqualsIgnoreCase(name, candidate.name))
        {
            chord.virtualKey = candidate.virtualKey;
            chord.extended = candidate.extended;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool ParseChord(std::string_view text, KeyChord& chord) noexcept
{
    chord = KeyChord{};
    if (text.empty())
    {
        return false;
    }
    while (true)
    {
        const size_t plus = text.find('+');
        // A trailing '+' is the Plus key itself ("Ctrl++" is Ctrl and Plus); a lone "+" is the Plus key.
        if (plus == std::string_view::npos || plus + 1 == text.size())
        {
            break;
        }
        const std::string_view modifier = text.substr(0, plus);
        if (EqualsIgnoreCase(modifier, "ctrl") || EqualsIgnoreCase(modifier, "control"))
        {
            chord.modifiers |= ChordModifierControl;
        }
        else if (EqualsIgnoreCase(modifier, "shift"))
        {
            chord.modifiers |= ChordModifierShift;
        }
        else if (EqualsIgnoreCase(modifier, "alt"))
        {
            chord.modifiers |= ChordModifierAlt;
        }
        else if (EqualsIgnoreCase(modifier, "win"))
        {
            chord.modifiers |= ChordModifierWin;
        }
        else
        {
            return false;
        }
        text = text.substr(plus + 1);
    }
    if (text == "+")
    {
        chord.virtualKey = VK_OEM_PLUS;
        return true;
    }
    return ParseKeyName(text, chord);
}

[[nodiscard]] bool ValidateSuffixes(const RedXeActionDescriptor& descriptor, std::string_view& value) noexcept
{
    if (descriptor.targetKind == RedXeActionTargetPoint)
    {
        // A point carries its own optional "@<monitor>"; ParsePoint checks it.
        return true;
    }
    std::string_view suffix;
    const std::string_view bare = SplitSuffix(value, suffix);
    if (suffix.empty() && value.find('@') == std::string_view::npos)
    {
        return true;
    }
    if ((descriptor.flags & RedXeActionFlagMonitorSuffix) != 0)
    {
        MonitorSelector monitor{};
        if (ParseMonitorSelector(suffix, true, monitor))
        {
            value = bare;
            return true;
        }
    }
    if ((descriptor.flags & RedXeActionFlagWindowSuffix) != 0)
    {
        WindowSelector window{};
        if (ParseWindowSelector(suffix, window))
        {
            value = bare;
            return true;
        }
    }
    // Kinds that legitimately contain '@' (URIs with credentials, text) keep it as part of the value.
    return descriptor.targetKind == RedXeActionTargetText || descriptor.targetKind == RedXeActionTargetPathOrUri ||
           descriptor.targetKind == RedXeActionTargetCommandLine || descriptor.targetKind == RedXeActionTargetMeeting ||
           descriptor.targetKind == RedXeActionTargetPath;
}
} // namespace

std::string_view SplitSuffix(std::string_view target, std::string_view& suffix) noexcept
{
    suffix = {};
    const size_t at = target.rfind('@');
    if (at == std::string_view::npos)
    {
        return target;
    }
    suffix = target.substr(at + 1);
    return target.substr(0, at);
}

bool IsAbsolutePath(std::string_view value) noexcept
{
    if (value.size() >= 3 && IsAlpha(value[0]) && value[1] == ':' && (value[2] == '\\' || value[2] == '/'))
    {
        return value.find('\0') == std::string_view::npos;
    }
    return value.size() >= 3 && value[0] == '\\' && value[1] == '\\' && value[2] != '\\';
}

bool IsUri(std::string_view value) noexcept
{
    size_t scheme = 0;
    while (scheme < value.size() && IsAlpha(value[scheme]))
    {
        ++scheme;
    }
    return scheme >= 2 && scheme < value.size() && value[scheme] == ':';
}

bool IsPathOrUri(std::string_view value) noexcept
{
    return IsAbsolutePath(value) || IsUri(value);
}

bool ParseCommandLine(std::string_view value, CommandLine& parsed) noexcept
{
    parsed = CommandLine{};
    if (value.empty())
    {
        return false;
    }
    std::string_view executable;
    std::string_view rest;
    if (value[0] == '"')
    {
        const size_t close = value.find('"', 1);
        if (close == std::string_view::npos || close == 1)
        {
            return false;
        }
        executable = value.substr(1, close - 1);
        rest = value.substr(close + 1);
    }
    else
    {
        const size_t space = value.find(' ');
        executable = value.substr(0, space);
        rest = space == std::string_view::npos ? std::string_view{} : value.substr(space);
    }
    if (!IsAbsolutePath(executable))
    {
        return false;
    }
    while (!rest.empty() && rest.front() == ' ')
    {
        rest.remove_prefix(1);
    }
    parsed.executable = executable;
    parsed.arguments = rest;
    return true;
}

bool ParseWidgetRef(std::string_view value, std::string_view& pageId, uint32_t& ordinal) noexcept
{
    pageId = {};
    ordinal = 0;
    if (value.empty())
    {
        return false;
    }
    std::string_view number = value;
    const size_t separator = value.rfind('/');
    if (separator != std::string_view::npos)
    {
        pageId = value.substr(0, separator);
        number = value.substr(separator + 1);
        if (pageId.empty())
        {
            return false;
        }
    }
    uint64_t parsed = 0;
    if (number.size() > 3 || !ParseUnsigned(number, 511, parsed))
    {
        return false;
    }
    ordinal = static_cast<uint32_t>(parsed);
    return true;
}

bool ParseInteger(std::string_view value, int32_t minimum, int32_t maximum, int32_t& parsed) noexcept
{
    if (value.empty())
    {
        return false;
    }
    bool negative = false;
    if (value[0] == '-')
    {
        negative = true;
        value.remove_prefix(1);
    }
    uint64_t magnitude = 0;
    if (!ParseUnsigned(value, 0x7FFFFFFFU, magnitude))
    {
        return false;
    }
    const int64_t signedValue = negative ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
    if (signedValue < minimum || signedValue > maximum)
    {
        return false;
    }
    parsed = static_cast<int32_t>(signedValue);
    return true;
}

bool ParseDelta(std::string_view value, int32_t minimum, int32_t maximum, Delta& parsed) noexcept
{
    parsed = Delta{};
    if (value.empty())
    {
        return false;
    }
    if (value[0] == '+' || value[0] == '-')
    {
        uint64_t magnitude = 0;
        const int64_t span = static_cast<int64_t>(maximum) - static_cast<int64_t>(minimum);
        if (!ParseUnsigned(value.substr(1), static_cast<uint64_t>(span < 0 ? 0 : span), magnitude) || magnitude == 0)
        {
            return false;
        }
        parsed.relative = true;
        parsed.value = value[0] == '-' ? -static_cast<int32_t>(magnitude) : static_cast<int32_t>(magnitude);
        return true;
    }
    return ParseInteger(value, minimum, maximum, parsed.value);
}

bool ParseNowOrSeconds(std::string_view value, int32_t minimum, int32_t maximum, NowOrSeconds& parsed) noexcept
{
    parsed = NowOrSeconds{};
    if (value == "now")
    {
        parsed.now = true;
        return true;
    }
    int32_t seconds = 0;
    if (!ParseInteger(value, minimum < 0 ? 0 : minimum, maximum, seconds))
    {
        return false;
    }
    parsed.seconds = static_cast<uint32_t>(seconds);
    return true;
}

bool ParseEnum(std::string_view value, const char* options, uint32_t& index) noexcept
{
    index = 0;
    if (!options || value.empty())
    {
        return false;
    }
    std::string_view remaining{options};
    uint32_t position = 0;
    while (!remaining.empty())
    {
        const size_t bar = remaining.find('|');
        const std::string_view option = remaining.substr(0, bar);
        if (option == value)
        {
            index = position;
            return true;
        }
        if (bar == std::string_view::npos)
        {
            break;
        }
        remaining.remove_prefix(bar + 1);
        ++position;
    }
    return false;
}

bool ParseChords(std::string_view value, ChordSequence& parsed) noexcept
{
    parsed = ChordSequence{};
    if (value.empty())
    {
        return false;
    }
    while (true)
    {
        const size_t comma = value.find(',');
        const std::string_view text = value.substr(0, comma);
        if (parsed.count >= kMaximumChords || !ParseChord(text, parsed.chords[parsed.count]))
        {
            parsed = ChordSequence{};
            return false;
        }
        ++parsed.count;
        if (comma == std::string_view::npos)
        {
            return true;
        }
        value = value.substr(comma + 1);
    }
}

bool ParseMonitorSelector(std::string_view value, bool allowAll, MonitorSelector& parsed) noexcept
{
    parsed = MonitorSelector{};
    if (value == "primary")
    {
        parsed.kind = MonitorSelector::Kind::Primary;
        return true;
    }
    if (value == "xeneon")
    {
        parsed.kind = MonitorSelector::Kind::Xeneon;
        return true;
    }
    if (value == "all")
    {
        parsed.kind = MonitorSelector::Kind::All;
        return allowAll;
    }
    if (value.starts_with("name:"))
    {
        parsed.kind = MonitorSelector::Kind::Name;
        parsed.name = value.substr(5);
        return !parsed.name.empty() && parsed.name.size() <= 128;
    }
    uint64_t index = 0;
    if (ParseUnsigned(value, 64, index) && index >= 1)
    {
        parsed.kind = MonitorSelector::Kind::Index;
        parsed.index = static_cast<uint32_t>(index);
        return true;
    }
    return false;
}

bool ParseWindowSelector(std::string_view value, WindowSelector& parsed) noexcept
{
    parsed = WindowSelector{};
    if (value == "foreground")
    {
        parsed.kind = WindowSelector::Kind::Foreground;
        return true;
    }
    if (value.starts_with("exe:"))
    {
        parsed.kind = WindowSelector::Kind::Executable;
        parsed.value = value.substr(4);
    }
    else if (value.starts_with("class:"))
    {
        parsed.kind = WindowSelector::Kind::Class;
        parsed.value = value.substr(6);
    }
    else if (value.starts_with("title:"))
    {
        parsed.kind = WindowSelector::Kind::Title;
        parsed.value = value.substr(6);
    }
    else
    {
        return false;
    }
    return !parsed.value.empty() && parsed.value.size() <= 260;
}

bool ParsePoint(std::string_view value, Point& parsed) noexcept
{
    parsed = Point{};
    std::string_view suffix;
    const std::string_view bare = SplitSuffix(value, suffix);
    if (!suffix.empty() || value.find('@') != std::string_view::npos)
    {
        if (!ParseMonitorSelector(suffix, false, parsed.monitor))
        {
            return false;
        }
        parsed.hasMonitor = true;
    }
    if (bare == "center")
    {
        parsed.center = true;
        return true;
    }
    const size_t comma = bare.find(',');
    if (comma == std::string_view::npos)
    {
        return false;
    }
    const std::string_view first = bare.substr(0, comma);
    const std::string_view second = bare.substr(comma + 1);
    if (first.empty() || second.empty())
    {
        return false;
    }
    const bool firstRelative = first[0] == '+' || first[0] == '-';
    const bool secondRelative = second[0] == '+' || second[0] == '-';
    if (firstRelative != secondRelative)
    {
        return false;
    }
    constexpr int32_t kLimit = 65535;
    int32_t x = 0;
    int32_t y = 0;
    if (firstRelative)
    {
        if (!ParseInteger(first[0] == '+' ? first.substr(1) : first, -kLimit, kLimit, x) ||
            !ParseInteger(second[0] == '+' ? second.substr(1) : second, -kLimit, kLimit, y))
        {
            return false;
        }
        parsed.relative = true;
    }
    else if (!ParseInteger(first, -kLimit, kLimit, x) || !ParseInteger(second, -kLimit, kLimit, y))
    {
        return false;
    }
    parsed.x = x;
    parsed.y = y;
    return true;
}

bool ParseMeeting(std::string_view value, Meeting& parsed) noexcept
{
    parsed = Meeting{};
    if (value.empty())
    {
        return false;
    }
    std::string_view digits;
    std::string_view passcode;
    if (value.starts_with("https://") || value.starts_with("http://"))
    {
        const size_t join = value.find("/j/");
        if (join == std::string_view::npos)
        {
            return false;
        }
        std::string_view rest = value.substr(join + 3);
        const size_t end = rest.find_first_of("?/#");
        digits = rest.substr(0, end);
        if (end != std::string_view::npos && rest[end] == '?')
        {
            std::string_view query = rest.substr(end + 1);
            while (!query.empty())
            {
                const size_t amp = query.find('&');
                const std::string_view pair = query.substr(0, amp);
                if (pair.starts_with("pwd="))
                {
                    passcode = pair.substr(4);
                    const size_t hash = passcode.find('#');
                    if (hash != std::string_view::npos)
                    {
                        passcode = passcode.substr(0, hash);
                    }
                }
                if (amp == std::string_view::npos)
                {
                    break;
                }
                query.remove_prefix(amp + 1);
            }
        }
    }
    else
    {
        const size_t colon = value.find(':');
        digits = value.substr(0, colon);
        passcode = colon == std::string_view::npos ? std::string_view{} : value.substr(colon + 1);
    }
    if (digits.size() < 9 || digits.size() > 11 || passcode.size() > 64)
    {
        return false;
    }
    uint64_t number = 0;
    if (!ParseUnsigned(digits, 99999999999ULL, number) || number == 0)
    {
        return false;
    }
    parsed.number = number;
    parsed.passcode = passcode;
    return true;
}

HRESULT ValidateTarget(const RedXeActionDescriptor& descriptor, std::string_view target) noexcept
{
    if (descriptor.sizeBytes != sizeof(RedXeActionDescriptor))
    {
        return E_INVALIDARG;
    }
    if (target.size() > kRedXeMaximumActionTargetBytes || target.find('\0') != std::string_view::npos)
    {
        return E_INVALIDARG;
    }
    if (descriptor.targetKind == RedXeActionTargetNone)
    {
        return S_OK;
    }
    if (target.empty())
    {
        return (descriptor.flags & RedXeActionFlagTargetOptional) != 0 &&
                       (descriptor.flags & RedXeActionFlagDestructive) == 0
                   ? S_OK
                   : E_INVALIDARG;
    }
    std::string_view value = target;
    if (!ValidateSuffixes(descriptor, value))
    {
        return E_INVALIDARG;
    }
    bool valid = false;
    switch (descriptor.targetKind)
    {
    case RedXeActionTargetText:
        valid = !value.empty();
        break;
    case RedXeActionTargetPathOrUri:
        valid = IsPathOrUri(value);
        break;
    case RedXeActionTargetPath:
        valid = IsAbsolutePath(value);
        break;
    case RedXeActionTargetCommandLine:
    {
        CommandLine parsed{};
        valid = ParseCommandLine(value, parsed);
        break;
    }
    case RedXeActionTargetPageRef:
        valid = !value.empty() && value.size() <= 128;
        break;
    case RedXeActionTargetWidgetRef:
    {
        std::string_view pageId;
        uint32_t ordinal = 0;
        valid = ParseWidgetRef(value, pageId, ordinal);
        break;
    }
    case RedXeActionTargetInteger:
    {
        int32_t parsed = 0;
        valid = ParseInteger(value, descriptor.targetMinimum, descriptor.targetMaximum, parsed);
        break;
    }
    case RedXeActionTargetDelta:
    {
        Delta parsed{};
        valid = ParseDelta(value, descriptor.targetMinimum, descriptor.targetMaximum, parsed);
        break;
    }
    case RedXeActionTargetEnum:
    {
        uint32_t index = 0;
        valid = ParseEnum(value, descriptor.targetOptions, index);
        break;
    }
    case RedXeActionTargetChords:
    {
        ChordSequence parsed{};
        valid = ParseChords(value, parsed);
        break;
    }
    case RedXeActionTargetPoint:
    {
        Point parsed{};
        valid = ParsePoint(target, parsed);
        break;
    }
    case RedXeActionTargetMonitor:
    {
        MonitorSelector parsed{};
        valid = ParseMonitorSelector(value, false, parsed);
        break;
    }
    case RedXeActionTargetWindow:
    {
        WindowSelector parsed{};
        valid = ParseWindowSelector(value, parsed);
        break;
    }
    case RedXeActionTargetMeeting:
    {
        Meeting parsed{};
        valid = ParseMeeting(value, parsed);
        break;
    }
    case RedXeActionTargetNowOrSeconds:
    {
        NowOrSeconds parsed{};
        valid = ParseNowOrSeconds(value, descriptor.targetMinimum, descriptor.targetMaximum, parsed);
        break;
    }
    default:
        valid = false;
        break;
    }
    return valid ? S_OK : E_INVALIDARG;
}
} // namespace RedXeActions
