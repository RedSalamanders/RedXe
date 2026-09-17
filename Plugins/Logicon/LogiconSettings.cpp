#include "LogiconSettings.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <yyjson.h>

namespace Logicon
{
namespace
{
struct NamedFace final
{
    const char* name;
    KeyFace face;
};

constexpr NamedFace kFaces[] = {
    {"none", KeyFace::None}, {"clock", KeyFace::Clock},   {"pageIndicator", KeyFace::PageIndicator},
    {"cpu", KeyFace::Cpu},   {"memory", KeyFace::Memory}, {"gpu", KeyFace::Gpu},
};

void WriteDiagnostic(char* diagnostic, size_t capacity, const char* text) noexcept
{
    if (!diagnostic || capacity == 0)
    {
        return;
    }
    strncpy_s(diagnostic, capacity, text ? text : "", _TRUNCATE);
}

[[nodiscard]] HRESULT Fail(char* diagnostic, size_t capacity, const char* text) noexcept
{
    WriteDiagnostic(diagnostic, capacity, text);
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

[[nodiscard]] std::string_view StringOf(yyjson_val* value) noexcept
{
    return std::string_view(yyjson_get_str(value), yyjson_get_len(value));
}

[[nodiscard]] size_t Utf8CodePoints(std::string_view text) noexcept
{
    size_t count = 0;
    for (const unsigned char value : text)
    {
        if ((value & 0xC0U) != 0x80U)
        {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] bool IsHexDigit(char value) noexcept
{
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

[[nodiscard]] bool ParseColor(std::string_view text, uint32_t& rgb) noexcept
{
    if (text.size() != 7 || text[0] != '#')
    {
        return false;
    }
    uint32_t value = 0;
    for (size_t index = 1; index < 7; ++index)
    {
        const char character = text[index];
        if (!IsHexDigit(character))
        {
            return false;
        }
        uint32_t digit = 0;
        if (character >= '0' && character <= '9')
        {
            digit = static_cast<uint32_t>(character - '0');
        }
        else if (character >= 'a' && character <= 'f')
        {
            digit = static_cast<uint32_t>(character - 'a') + 10U;
        }
        else
        {
            digit = static_cast<uint32_t>(character - 'A') + 10U;
        }
        value = (value << 4U) | digit;
    }
    rgb = value;
    return true;
}

template <size_t Capacity>
[[nodiscard]] bool CopyBounded(std::string_view source, std::array<char, Capacity>& destination, uint32_t& bytes,
                               size_t maximumBytes) noexcept
{
    if (source.size() > maximumBytes || source.size() >= Capacity || source.find('\0') != std::string_view::npos)
    {
        return false;
    }
    destination.fill('\0');
    std::memcpy(destination.data(), source.data(), source.size());
    bytes = static_cast<uint32_t>(source.size());
    return true;
}

// "none" clears the action; anything else must satisfy the action-name grammar.
[[nodiscard]] bool ParseAction(yyjson_val* value, KeyBinding& binding) noexcept
{
    if (!yyjson_is_str(value))
    {
        return false;
    }
    const std::string_view name = StringOf(value);
    if (name == "none")
    {
        binding.action.fill('\0');
        binding.actionBytes = 0;
        return true;
    }
    if (!CopyBounded(name, binding.action, binding.actionBytes, kMaximumActionBytes))
    {
        return false;
    }
    return RedXeIsActionNameSyntax(binding.action.data());
}

[[nodiscard]] HRESULT ParseKey(yyjson_val* item, KeyBinding& binding, char* diagnostic, size_t capacity) noexcept
{
    if (!yyjson_is_obj(item))
    {
        return Fail(diagnostic, capacity, "keys items must be objects.");
    }
    binding = KeyBinding{};
    bool sawSlot = false;
    yyjson_obj_iter iterator = yyjson_obj_iter_with(item);
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
    {
        const std::string_view name = StringOf(key);
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        if (name == "page")
        {
            if (!yyjson_is_uint(value) || yyjson_get_uint(value) >= kMaximumKeyPages)
            {
                return Fail(diagnostic, capacity, "keys[].page must be an integer from 0 through 3.");
            }
            binding.page = static_cast<uint8_t>(yyjson_get_uint(value));
        }
        else if (name == "slot")
        {
            if (!yyjson_is_uint(value) || yyjson_get_uint(value) >= kKeysPerPage)
            {
                return Fail(diagnostic, capacity, "keys[].slot must be an integer from 0 through 8.");
            }
            binding.slot = static_cast<uint8_t>(yyjson_get_uint(value));
            sawSlot = true;
        }
        else if (name == "action")
        {
            if (!ParseAction(value, binding))
            {
                return Fail(diagnostic, capacity, "keys[].action is not an action name such as page.next.");
            }
        }
        else if (name == "face")
        {
            if (!yyjson_is_str(value) || !FaceFromName(StringOf(value), binding.face))
            {
                return Fail(diagnostic, capacity, "keys[].face is not a known face name.");
            }
        }
        else if (name == "target")
        {
            if (!yyjson_is_str(value) ||
                !CopyBounded(StringOf(value), binding.target, binding.targetBytes, kMaximumTargetBytes))
            {
                return Fail(diagnostic, capacity, "keys[].target must be a string of at most 512 bytes.");
            }
        }
        else if (name == "label")
        {
            if (!yyjson_is_str(value) || Utf8CodePoints(StringOf(value)) > kMaximumLabelCodePoints ||
                !CopyBounded(StringOf(value), binding.label, binding.labelBytes, kMaximumLabelBytes))
            {
                return Fail(diagnostic, capacity, "keys[].label must be a string of at most 16 code points.");
            }
        }
        else if (name == "icon")
        {
            if (!yyjson_is_str(value) ||
                !CopyBounded(StringOf(value), binding.icon, binding.iconBytes, kMaximumIconBytes))
            {
                return Fail(diagnostic, capacity, "keys[].icon must be a string of at most 260 bytes.");
            }
        }
        else if (name == "color")
        {
            if (!yyjson_is_str(value) || !ParseColor(StringOf(value), binding.colorRgb))
            {
                return Fail(diagnostic, capacity, "keys[].color must be a #RRGGBB string.");
            }
            binding.hasColor = true;
        }
        else
        {
            return Fail(diagnostic, capacity, "keys[] contains an unknown member.");
        }
    }
    if (!sawSlot)
    {
        return Fail(diagnostic, capacity, "keys[].slot is required.");
    }
    return S_OK;
}

// One dialpad button ({button, action, target}) or one turn ({control, direction, action, target}).
[[nodiscard]] HRESULT ParseDialBinding(yyjson_val* item, bool turn, KeyBinding& binding, char* diagnostic,
                                       size_t capacity) noexcept
{
    if (!yyjson_is_obj(item))
    {
        return Fail(diagnostic, capacity,
                    turn ? "dialpad.turns items must be objects." : "dialpad.buttons items must be objects.");
    }
    binding = KeyBinding{};
    bool sawButton = false;
    bool sawControl = false;
    bool sawDirection = false;
    std::string_view directionName;
    yyjson_obj_iter iterator = yyjson_obj_iter_with(item);
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
    {
        const std::string_view name = StringOf(key);
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        if (!turn && name == "button")
        {
            if (!yyjson_is_uint(value) || yyjson_get_uint(value) >= kDialpadButtons)
            {
                return Fail(diagnostic, capacity, "dialpad.buttons[].button must be an integer from 0 through 3.");
            }
            binding.slot = static_cast<uint8_t>(yyjson_get_uint(value));
            sawButton = true;
        }
        else if (turn && name == "control")
        {
            if (!yyjson_is_str(value))
            {
                return Fail(diagnostic, capacity, "dialpad.turns[].control must be dial or roller.");
            }
            const std::string_view control = StringOf(value);
            if (control == "dial")
            {
                binding.control = kControlDial;
            }
            else if (control == "roller")
            {
                binding.control = kControlRoller;
            }
            else
            {
                return Fail(diagnostic, capacity, "dialpad.turns[].control must be dial or roller.");
            }
            sawControl = true;
        }
        else if (turn && name == "direction")
        {
            if (!yyjson_is_str(value))
            {
                return Fail(diagnostic, capacity, "dialpad.turns[].direction must be cw, ccw, up, or down.");
            }
            directionName = StringOf(value);
            sawDirection = true;
        }
        else if (name == "action")
        {
            if (!ParseAction(value, binding))
            {
                return Fail(diagnostic, capacity,
                            turn ? "dialpad.turns[].action is not an action name such as page.next."
                                 : "dialpad.buttons[].action is not an action name such as page.next.");
            }
        }
        else if (name == "target")
        {
            if (!yyjson_is_str(value) ||
                !CopyBounded(StringOf(value), binding.target, binding.targetBytes, kMaximumTargetBytes))
            {
                return Fail(diagnostic, capacity,
                            turn ? "dialpad.turns[].target must be a string of at most 512 bytes."
                                 : "dialpad.buttons[].target must be a string of at most 512 bytes.");
            }
        }
        else
        {
            return Fail(diagnostic, capacity,
                        turn ? "dialpad.turns[] contains an unknown member."
                             : "dialpad.buttons[] contains an unknown member.");
        }
    }
    if (!turn && !sawButton)
    {
        return Fail(diagnostic, capacity, "dialpad.buttons[].button is required.");
    }
    if (turn)
    {
        if (!sawControl || !sawDirection)
        {
            return Fail(diagnostic, capacity, "dialpad.turns[] requires control and direction.");
        }
        // The dial turns cw / ccw; the roller moves up / down. A direction from the other control is rejected so a
        // document cannot describe a turn that does not exist.
        if (binding.control == kControlDial && directionName == "cw")
        {
            binding.direction = kDirectionForward;
        }
        else if (binding.control == kControlDial && directionName == "ccw")
        {
            binding.direction = kDirectionBackward;
        }
        else if (binding.control == kControlRoller && directionName == "up")
        {
            binding.direction = kDirectionForward;
        }
        else if (binding.control == kControlRoller && directionName == "down")
        {
            binding.direction = kDirectionBackward;
        }
        else
        {
            return Fail(diagnostic, capacity,
                        "dialpad.turns[].direction must be cw or ccw for the dial and up or down for the roller.");
        }
    }
    return S_OK;
}

[[nodiscard]] HRESULT ParseDialpad(yyjson_val* object, DialpadSettings& dialpad, char* diagnostic,
                                   size_t capacity) noexcept
{
    if (!yyjson_is_obj(object))
    {
        return Fail(diagnostic, capacity, "dialpad must be an object.");
    }
    dialpad = DialpadSettings{};
    yyjson_obj_iter iterator = yyjson_obj_iter_with(object);
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
    {
        const std::string_view name = StringOf(key);
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        if (name == "buttons" || name == "turns")
        {
            const bool turns = name == "turns";
            if (!yyjson_is_arr(value))
            {
                return Fail(diagnostic, capacity,
                            turns ? "dialpad.turns must be an array." : "dialpad.buttons must be an array.");
            }
            const size_t count = yyjson_arr_size(value);
            if (count > (turns ? kDialpadTurns : kDialpadButtons))
            {
                return Fail(diagnostic, capacity,
                            turns ? "dialpad.turns may contain at most 4 entries."
                                  : "dialpad.buttons may contain at most 4 entries.");
            }
            for (size_t index = 0; index < count; ++index)
            {
                KeyBinding& binding = turns ? dialpad.turns[index] : dialpad.buttons[index];
                const HRESULT parsed =
                    ParseDialBinding(yyjson_arr_get(value, index), turns, binding, diagnostic, capacity);
                if (FAILED(parsed))
                {
                    return parsed;
                }
                for (size_t previous = 0; previous < index; ++previous)
                {
                    const KeyBinding& earlier = turns ? dialpad.turns[previous] : dialpad.buttons[previous];
                    const bool duplicate =
                        turns ? (earlier.control == binding.control && earlier.direction == binding.direction)
                              : earlier.slot == binding.slot;
                    if (duplicate)
                    {
                        return Fail(diagnostic, capacity,
                                    turns ? "dialpad.turns[] binds the same control and direction twice."
                                          : "dialpad.buttons[] binds the same button twice.");
                    }
                }
            }
            (turns ? dialpad.turnCount : dialpad.buttonCount) = static_cast<uint32_t>(count);
        }
        else
        {
            return Fail(diagnostic, capacity, "dialpad contains an unknown member.");
        }
    }
    return S_OK;
}
} // namespace

const KeyBinding* DialpadSettings::Button(uint32_t button) const noexcept
{
    for (uint32_t index = 0; index < buttonCount && index < buttons.size(); ++index)
    {
        if (buttons[index].slot == button)
        {
            return &buttons[index];
        }
    }
    return nullptr;
}

const KeyBinding* DialpadSettings::Turn(uint8_t control, uint8_t direction) const noexcept
{
    for (uint32_t index = 0; index < turnCount && index < turns.size(); ++index)
    {
        if (turns[index].control == control && turns[index].direction == direction)
        {
            return &turns[index];
        }
    }
    return nullptr;
}

bool Settings::UsesSystemFaces() const noexcept
{
    for (uint32_t index = 0; index < keyCount && index < keys.size(); ++index)
    {
        if (IsSystemFace(keys[index].face))
        {
            return true;
        }
    }
    return false;
}

uint32_t Settings::KeyPageCount() const noexcept
{
    uint32_t pages = 1;
    for (uint32_t index = 0; index < keyCount && index < keys.size(); ++index)
    {
        pages = std::max(pages, static_cast<uint32_t>(keys[index].page) + 1U);
    }
    return pages;
}

const KeyBinding* Settings::Find(uint32_t page, uint32_t slot) const noexcept
{
    for (uint32_t index = 0; index < keyCount && index < keys.size(); ++index)
    {
        if (keys[index].page == page && keys[index].slot == slot)
        {
            return &keys[index];
        }
    }
    return nullptr;
}

HRESULT ParseSettings(yyjson_val* object, Settings& settings, char* diagnostic, size_t diagnosticCapacity) noexcept
{
    WriteDiagnostic(diagnostic, diagnosticCapacity, "");
    settings = Settings{};
    if (!yyjson_is_obj(object))
    {
        return Fail(diagnostic, diagnosticCapacity, "Logicon settings must be a JSON object.");
    }
    yyjson_obj_iter iterator = yyjson_obj_iter_with(object);
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
    {
        const std::string_view name = StringOf(key);
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        if (name == "brightness")
        {
            if (!yyjson_is_uint(value) || yyjson_get_uint(value) < kMinimumBrightness ||
                yyjson_get_uint(value) > kMaximumBrightness)
            {
                return Fail(diagnostic, diagnosticCapacity, "brightness must be an integer from 1 through 100.");
            }
            settings.brightness = static_cast<uint32_t>(yyjson_get_uint(value));
        }
        else if (name == "restoreLogoOnExit")
        {
            if (!yyjson_is_bool(value))
            {
                return Fail(diagnostic, diagnosticCapacity, "restoreLogoOnExit must be a boolean.");
            }
            settings.restoreLogoOnExit = yyjson_get_bool(value);
        }
        else if (name == "pageButtons")
        {
            if (!yyjson_is_str(value))
            {
                return Fail(diagnostic, diagnosticCapacity, "pageButtons must be keyPages or dashboardPages.");
            }
            const std::string_view mode = StringOf(value);
            if (mode == "keyPages")
            {
                settings.pageButtons = PageButtons::KeyPages;
            }
            else if (mode == "dashboardPages")
            {
                settings.pageButtons = PageButtons::DashboardPages;
            }
            else
            {
                return Fail(diagnostic, diagnosticCapacity, "pageButtons must be keyPages or dashboardPages.");
            }
        }
        else if (name == "keys")
        {
            if (!yyjson_is_arr(value))
            {
                return Fail(diagnostic, diagnosticCapacity, "keys must be an array.");
            }
            const size_t count = yyjson_arr_size(value);
            if (count > kMaximumKeys)
            {
                return Fail(diagnostic, diagnosticCapacity, "keys may contain at most 36 entries.");
            }
            for (size_t index = 0; index < count; ++index)
            {
                KeyBinding& binding = settings.keys[index];
                const HRESULT parsed = ParseKey(yyjson_arr_get(value, index), binding, diagnostic, diagnosticCapacity);
                if (FAILED(parsed))
                {
                    return parsed;
                }
                for (size_t previous = 0; previous < index; ++previous)
                {
                    if (settings.keys[previous].page == binding.page && settings.keys[previous].slot == binding.slot)
                    {
                        return Fail(diagnostic, diagnosticCapacity, "keys[] binds the same page and slot twice.");
                    }
                }
            }
            settings.keyCount = static_cast<uint32_t>(count);
        }
        else if (name == "dialpad")
        {
            const HRESULT parsed = ParseDialpad(value, settings.dialpad, diagnostic, diagnosticCapacity);
            if (FAILED(parsed))
            {
                return parsed;
            }
        }
        else
        {
            return Fail(diagnostic, diagnosticCapacity, "Logicon settings contain an unknown member.");
        }
    }
    return S_OK;
}

HRESULT ParseSettingsJson(std::string_view json, Settings& settings, char* diagnostic,
                          size_t diagnosticCapacity) noexcept
{
    if (json.empty())
    {
        return Fail(diagnostic, diagnosticCapacity, "Logicon settings are empty.");
    }
    yyjson_doc* document = yyjson_read(json.data(), json.size(), YYJSON_READ_NOFLAG);
    if (!document)
    {
        return Fail(diagnostic, diagnosticCapacity, "Logicon settings are not valid JSON.");
    }
    const HRESULT result = ParseSettings(yyjson_doc_get_root(document), settings, diagnostic, diagnosticCapacity);
    yyjson_doc_free(document);
    return result;
}

const char* FaceName(KeyFace face) noexcept
{
    for (const NamedFace& candidate : kFaces)
    {
        if (candidate.face == face)
        {
            return candidate.name;
        }
    }
    return "none";
}

bool FaceFromName(std::string_view name, KeyFace& face) noexcept
{
    for (const NamedFace& candidate : kFaces)
    {
        if (name == candidate.name)
        {
            face = candidate.face;
            return true;
        }
    }
    return false;
}

const char* ControlName(uint8_t control) noexcept
{
    return control == kControlRoller ? "roller" : "dial";
}

const char* DirectionName(uint8_t control, uint8_t direction) noexcept
{
    if (control == kControlRoller)
    {
        return direction == kDirectionBackward ? "down" : "up";
    }
    return direction == kDirectionBackward ? "ccw" : "cw";
}

bool ParseWidgetTarget(std::string_view target, std::string_view& pageId, uint32_t& ordinal) noexcept
{
    pageId = {};
    ordinal = 0;
    if (target.empty())
    {
        return false;
    }
    std::string_view number = target;
    const size_t separator = target.rfind('/');
    if (separator != std::string_view::npos)
    {
        pageId = target.substr(0, separator);
        number = target.substr(separator + 1);
        if (pageId.empty())
        {
            return false;
        }
    }
    if (number.empty() || number.size() > 3)
    {
        return false;
    }
    uint32_t value = 0;
    for (const char character : number)
    {
        if (character < '0' || character > '9')
        {
            return false;
        }
        value = value * 10U + static_cast<uint32_t>(character - '0');
    }
    if (value >= 512)
    {
        return false;
    }
    ordinal = value;
    return true;
}

} // namespace Logicon
