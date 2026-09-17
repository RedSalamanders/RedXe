#include "LogiconSettings.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <yyjson.h>

namespace Logicon
{
namespace
{
struct NamedAction final
{
    const char* name;
    KeyAction action;
};

constexpr NamedAction kActions[] = {
    {"none", KeyAction::None},
    {"page.next", KeyAction::PageNext},
    {"page.previous", KeyAction::PagePrevious},
    {"page.goto", KeyAction::PageGoTo},
    {"widget.raise", KeyAction::WidgetRaise},
    {"widget.dismiss", KeyAction::WidgetDismiss},
    {"widget.toggle", KeyAction::WidgetToggle},
    {"launch", KeyAction::Launch},
    {"keys", KeyAction::Keys},
    {"keyPage.next", KeyAction::KeyPageNext},
    {"keyPage.previous", KeyAction::KeyPagePrevious},
};

struct NamedFace final
{
    const char* name;
    KeyFace face;
};

constexpr NamedFace kFaces[] = {
    {"none", KeyFace::None}, {"clock", KeyFace::Clock},   {"pageIndicator", KeyFace::PageIndicator},
    {"cpu", KeyFace::Cpu},   {"memory", KeyFace::Memory}, {"gpu", KeyFace::Gpu},
};

struct NamedDialAction final
{
    const char* name;
    DialAction action;
};

constexpr NamedDialAction kDialActions[] = {
    {"none", DialAction::None},       {"volume", DialAction::Volume},         {"page", DialAction::Page},
    {"keyPage", DialAction::KeyPage}, {"brightness", DialAction::Brightness},
};

struct NamedMediaKey final
{
    const char* name;
    MediaKey key;
};

constexpr NamedMediaKey kMediaKeys[] = {
    {"volume-up", MediaKey::VolumeUp},
    {"volume-down", MediaKey::VolumeDown},
    {"mute", MediaKey::Mute},
    {"play-pause", MediaKey::PlayPause},
    {"next-track", MediaKey::NextTrack},
    {"previous-track", MediaKey::PreviousTrack},
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
            if (!yyjson_is_str(value) || !ActionFromName(StringOf(value), binding.action))
            {
                return Fail(diagnostic, capacity, "keys[].action is not a known action name.");
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

[[nodiscard]] HRESULT ParseDialButton(yyjson_val* item, KeyBinding& binding, char* diagnostic, size_t capacity) noexcept
{
    if (!yyjson_is_obj(item))
    {
        return Fail(diagnostic, capacity, "dialpad.buttons items must be objects.");
    }
    binding = KeyBinding{};
    bool sawButton = false;
    yyjson_obj_iter iterator = yyjson_obj_iter_with(item);
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
    {
        const std::string_view name = StringOf(key);
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        if (name == "button")
        {
            if (!yyjson_is_uint(value) || yyjson_get_uint(value) >= kDialpadButtons)
            {
                return Fail(diagnostic, capacity, "dialpad.buttons[].button must be an integer from 0 through 3.");
            }
            binding.slot = static_cast<uint8_t>(yyjson_get_uint(value));
            sawButton = true;
        }
        else if (name == "action")
        {
            if (!yyjson_is_str(value) || !ActionFromName(StringOf(value), binding.action))
            {
                return Fail(diagnostic, capacity, "dialpad.buttons[].action is not a known action name.");
            }
        }
        else if (name == "target")
        {
            if (!yyjson_is_str(value) ||
                !CopyBounded(StringOf(value), binding.target, binding.targetBytes, kMaximumTargetBytes))
            {
                return Fail(diagnostic, capacity, "dialpad.buttons[].target must be a string of at most 512 bytes.");
            }
        }
        else
        {
            return Fail(diagnostic, capacity, "dialpad.buttons[] contains an unknown member.");
        }
    }
    if (!sawButton)
    {
        return Fail(diagnostic, capacity, "dialpad.buttons[].button is required.");
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
        if (name == "dial" || name == "roller")
        {
            DialAction& action = name == "dial" ? dialpad.dial : dialpad.roller;
            if (!yyjson_is_str(value) || !DialActionFromName(StringOf(value), action))
            {
                return Fail(diagnostic, capacity,
                            "dialpad.dial and dialpad.roller must be none, volume, page, keyPage, or brightness.");
            }
        }
        else if (name == "buttons")
        {
            if (!yyjson_is_arr(value))
            {
                return Fail(diagnostic, capacity, "dialpad.buttons must be an array.");
            }
            const size_t count = yyjson_arr_size(value);
            if (count > kDialpadButtons)
            {
                return Fail(diagnostic, capacity, "dialpad.buttons may contain at most 4 entries.");
            }
            for (size_t index = 0; index < count; ++index)
            {
                KeyBinding& binding = dialpad.buttons[index];
                const HRESULT parsed = ParseDialButton(yyjson_arr_get(value, index), binding, diagnostic, capacity);
                if (FAILED(parsed))
                {
                    return parsed;
                }
                for (size_t previous = 0; previous < index; ++previous)
                {
                    if (dialpad.buttons[previous].slot == binding.slot)
                    {
                        return Fail(diagnostic, capacity, "dialpad.buttons[] binds the same button twice.");
                    }
                }
            }
            dialpad.buttonCount = static_cast<uint32_t>(count);
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

const char* ActionName(KeyAction action) noexcept
{
    for (const NamedAction& candidate : kActions)
    {
        if (candidate.action == action)
        {
            return candidate.name;
        }
    }
    return "none";
}

bool ActionFromName(std::string_view name, KeyAction& action) noexcept
{
    for (const NamedAction& candidate : kActions)
    {
        if (name == candidate.name)
        {
            action = candidate.action;
            return true;
        }
    }
    return false;
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

bool MediaKeyFromName(std::string_view name, MediaKey& key) noexcept
{
    for (const NamedMediaKey& candidate : kMediaKeys)
    {
        if (name == candidate.name)
        {
            key = candidate.key;
            return true;
        }
    }
    return false;
}

const char* MediaKeyName(MediaKey key) noexcept
{
    for (const NamedMediaKey& candidate : kMediaKeys)
    {
        if (candidate.key == key)
        {
            return candidate.name;
        }
    }
    return "";
}

const char* DialActionName(DialAction action) noexcept
{
    for (const NamedDialAction& candidate : kDialActions)
    {
        if (candidate.action == action)
        {
            return candidate.name;
        }
    }
    return "none";
}

bool DialActionFromName(std::string_view name, DialAction& action) noexcept
{
    for (const NamedDialAction& candidate : kDialActions)
    {
        if (name == candidate.name)
        {
            action = candidate.action;
            return true;
        }
    }
    return false;
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

bool TargetIsValid(const KeyBinding& binding) noexcept
{
    const std::string_view target = binding.Target();
    switch (binding.action)
    {
    case KeyAction::Launch:
    {
        if (target.size() < 3)
        {
            return false;
        }
        // Absolute Win32 path: drive letter, colon, separator.
        const bool drivePath = ((target[0] >= 'A' && target[0] <= 'Z') || (target[0] >= 'a' && target[0] <= 'z')) &&
                               target[1] == ':' && (target[2] == '\\' || target[2] == '/');
        if (drivePath || target.starts_with("\\\\"))
        {
            return true;
        }
        // URI: alphabetic scheme of at least two characters followed by ':'.
        size_t scheme = 0;
        while (scheme < target.size() &&
               ((target[scheme] >= 'A' && target[scheme] <= 'Z') || (target[scheme] >= 'a' && target[scheme] <= 'z')))
        {
            ++scheme;
        }
        return scheme >= 2 && scheme < target.size() && target[scheme] == ':';
    }
    case KeyAction::Keys:
    {
        MediaKey key = MediaKey::None;
        return MediaKeyFromName(target, key);
    }
    case KeyAction::PageGoTo:
        return !target.empty();
    case KeyAction::WidgetRaise:
    case KeyAction::WidgetToggle:
    {
        std::string_view pageId;
        uint32_t ordinal = 0;
        return ParseWidgetTarget(target, pageId, ordinal);
    }
    default:
        return true;
    }
}
} // namespace Logicon
