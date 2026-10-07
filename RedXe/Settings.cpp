#include "Settings.h"

#include "../Plugins/5H4D3R5/ShadersSettings.h"
#include "../Plugins/AVControl/AVControlModel.h"
#include "../Plugins/Launcher/LauncherBindings.h"
#include "../Plugins/Launcher/LauncherPaging.h"
#include "BundledPlugins.h"
#include "HostActionCatalog.h"
#include "PlugInterfaces/Factory.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <shlobj.h>
#include <string>
#include <utility>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

#include <yyjson.h>

namespace
{
using unique_yyjson_doc = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;
using unique_mut_doc = wil::unique_any<yyjson_mut_doc*, decltype(&yyjson_mut_doc_free), yyjson_mut_doc_free>;
using unique_malloc_string = wil::unique_any<char*, decltype(&free), free>;

[[nodiscard]] unique_yyjson_doc ParseStoredObject(const JsonObjectSettings& settings) noexcept;

constexpr size_t kMaximumSettingsBytes = 1024U * 1024U;
// A user document MAY start with it (Core_Settings.md "Version 5 document"); a host write keeps it there.
constexpr std::string_view kUtf8ByteOrderMark = "\xEF\xBB\xBF";

// Input is valid compact JSON from yyjson. Only whitespace outside tokens changes here.
[[nodiscard]] size_t JsonTokenEnd(std::string_view json, size_t begin, size_t limit) noexcept
{
    size_t cursor = begin;
    if (json[begin] == '"')
    {
        ++cursor;
        while (cursor < limit)
        {
            const char value = json[cursor++];
            if (value == '"')
                return cursor;
            if (value == '\\' && cursor < limit)
                ++cursor;
        }
        return limit;
    }
    while (cursor < limit && std::strchr("{}[],:", json[cursor]) == nullptr)
        ++cursor;
    return cursor;
}

[[nodiscard]] bool IsStructuredJsonSection(std::string_view key) noexcept
{
    return key == "\"declare\"" || key == "\"pages\"" || key == "\"widgets\"" || key == "\"columns\"" ||
           key == "\"rows\"" || key == "\"shortcuts\"" || key == "\"services\"" || key == "\"keys\"";
}

[[nodiscard]] bool FitsInlineJson(std::string_view json, size_t begin, size_t column) noexcept
{
    // Keep a single scalar property together even when an indivisible path/string exceeds the soft line width.
    if (json[begin] == '{' && begin + 1 < json.size() && json[begin + 1] == '"')
    {
        const size_t keyEnd = JsonTokenEnd(json, begin + 1, json.size());
        const size_t value = keyEnd + 1;
        if (value < json.size() && json[keyEnd] == ':' && json[value] != '{' && json[value] != '[')
        {
            const size_t valueEnd = JsonTokenEnd(json, value, json.size());
            if (valueEnd < json.size() && json[valueEnd] == '}')
                return true;
        }
    }
    constexpr size_t lineWidth = 120;
    if (column >= lineWidth)
        return false;
    const size_t available = lineWidth - column;
    const size_t scanEnd = begin + ((json.size() - begin < available) ? json.size() - begin : available);
    size_t width = 0, depth = 0;
    std::string_view lastString;
    for (size_t cursor = begin; cursor < scanEnd; ++cursor)
    {
        const char value = json[cursor];
        if (value == '"')
        {
            const size_t end = JsonTokenEnd(json, cursor, scanEnd);
            if (end == scanEnd)
                return false;
            lastString = json.substr(cursor, end - cursor);
            width += end - cursor;
            cursor = end - 1;
        }
        else if (value == '{' || value == '[')
        {
            if (value == '[' && cursor + 1 < json.size() && json[cursor + 1] != ']')
                return false; // Non-empty lists stay one item per line.
            ++depth;
            width += (cursor + 1 < json.size() && (json[cursor + 1] == '}' || json[cursor + 1] == ']')) ? 1 : 2;
        }
        else if (value == '}' || value == ']')
        {
            width += (json[cursor - 1] == '{' || json[cursor - 1] == '[') ? 1 : 2;
            if (--depth == 0)
                return width <= available;
        }
        else if (value == ':')
        {
            if (IsStructuredJsonSection(lastString) && cursor + 2 < json.size() &&
                (json[cursor + 1] == '{' || json[cursor + 1] == '[') && json[cursor + 2] != '}' &&
                json[cursor + 2] != ']')
                return false;
            width += 2;
        }
        else
            width += value == ',' ? 2 : 1;
        if (width > available)
            return false;
    }
    return false;
}

// `byteOrderMark` starts the output with the UTF-8 BOM, which counts toward the size cap but not the line width.
[[nodiscard]] HRESULT FormatCompactSettingsJson(std::string_view json, bool byteOrderMark,
                                                std::string& formatted) noexcept
{
    try
    {
        std::string output;
        output.reserve(json.size() < kMaximumSettingsBytes ? json.size() : kMaximumSettingsBytes);
        if (byteOrderMark)
            output.append(kUtf8ByteOrderMark);
        std::vector<uint8_t> inlineContainers;
        inlineContainers.reserve(32);
        size_t column = 0;
        bool fits = true, structuredSection = true;
        std::string_view lastString;
        const auto append = [&](std::string_view text)
        {
            if (text.size() > kMaximumSettingsBytes - output.size())
                fits = false;
            else
            {
                output.append(text);
                column += text.size();
            }
        };
        const auto newline = [&](size_t depth)
        {
            append("\n");
            const size_t spaces = depth * 2;
            if (spaces > kMaximumSettingsBytes - output.size())
                fits = false;
            else
                output.append(spaces, ' ');
            column = spaces;
        };
        for (size_t cursor = 0; cursor < json.size() && fits;)
        {
            const char value = json[cursor];
            if (value == '{' || value == '[')
            {
                if (cursor + 1 < json.size() && (json[cursor + 1] == '}' || json[cursor + 1] == ']'))
                {
                    append(json.substr(cursor, 2));
                    cursor += 2;
                }
                else
                {
                    const bool inlineValue =
                        !structuredSection && ((!inlineContainers.empty() && inlineContainers.back() != 0) ||
                                               FitsInlineJson(json, cursor, column));
                    append(json.substr(cursor++, 1));
                    inlineContainers.push_back(inlineValue ? 1 : 0);
                    if (inlineValue)
                        append(" ");
                    else
                        newline(inlineContainers.size());
                }
                structuredSection = false;
            }
            else if (value == '}' || value == ']')
            {
                if (inlineContainers.empty())
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                if (inlineContainers.back() != 0)
                    append(" ");
                else
                    newline(inlineContainers.size() - 1);
                inlineContainers.pop_back();
                append(json.substr(cursor++, 1));
            }
            else if (value == ',')
            {
                append(json.substr(cursor++, 1));
                if (!inlineContainers.empty() && inlineContainers.back() != 0)
                    append(" ");
                else
                    newline(inlineContainers.size());
            }
            else if (value == ':')
            {
                append(": ");
                structuredSection = IsStructuredJsonSection(lastString);
                ++cursor;
            }
            else
            {
                const size_t end = JsonTokenEnd(json, cursor, json.size());
                lastString = value == '"' ? json.substr(cursor, end - cursor) : std::string_view{};
                append(json.substr(cursor, end - cursor));
                cursor = end;
                structuredSection = false;
            }
        }
        append("\n");
        if (!fits)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        formatted.swap(output);
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

constexpr char kSchemaReference[] = "RedXe.settings.schema.json";
constexpr char kMatrixPluginId[] = "builtin.matrix-rain";
constexpr char kProcessViewerPluginId[] = "builtin.process-viewer";
constexpr char kStudioClockPluginId[] = "builtin.studio-clock";
constexpr char kDeskClockPluginId[] = "builtin.desk-clock";
constexpr char kShadersPluginId[] = "builtin.5h4d3r5";
constexpr char kWeatherPluginId[] = "builtin.weather";
constexpr char kLauncherPluginId[] = "builtin.launcher";
constexpr char kAvControlPluginId[] = "builtin.av-control";

#if defined(_DEBUG)
constexpr const wchar_t* kSelectedSettingsFileName = kRedXeDebugSettingsFileName;
#else
constexpr const wchar_t* kSelectedSettingsFileName = kRedXeReleaseSettingsFileName;
constexpr wchar_t kLegacyReleaseSettingsFileName[] = L"RedXe-1.0.settings.json";
#endif

template <size_t Count>
[[nodiscard]] bool HasExactKeys(yyjson_val* object, const std::array<const char*, Count>& expected) noexcept
{
    if (!yyjson_is_obj(object) || yyjson_obj_size(object) != Count)
    {
        return false;
    }

    std::array<bool, Count> seen{};
    yyjson_obj_iter iterator = yyjson_obj_iter_with(object);
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
    {
        const char* text = yyjson_get_str(key);
        bool matched = false;
        for (size_t index = 0; index < expected.size(); ++index)
        {
            if (text && std::strcmp(text, expected[index]) == 0 && !seen[index])
            {
                seen[index] = true;
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsValidStoredText(const SettingsText& text, bool machineId) noexcept
{
    if (text.bytes == 0 || text.bytes > kMaximumSettingsTextBytes || text.utf8[text.bytes] != '\0')
    {
        return false;
    }
    const std::string_view view = text.View();
    if (view.find('\0') != std::string_view::npos)
    {
        return false;
    }
    return !machineId || RedXeIsValidMachineId(text.utf8.data());
}

[[nodiscard]] bool CopyText(yyjson_val* value, SettingsText& destination, bool machineId) noexcept
{
    if (!yyjson_is_str(value))
    {
        return false;
    }
    const size_t length = yyjson_get_len(value);
    const char* text = yyjson_get_str(value);
    if (!text || length == 0 || length > kMaximumSettingsTextBytes ||
        std::string_view(text, length).find('\0') != std::string_view::npos)
    {
        return false;
    }

    SettingsText copied{};
    std::memcpy(copied.utf8.data(), text, length);
    copied.bytes = static_cast<uint32_t>(length);
    if (machineId && !RedXeIsValidMachineId(copied.utf8.data()))
    {
        return false;
    }
    destination = copied;
    return true;
}

[[nodiscard]] bool ReadUnsigned(yyjson_val* object, const char* key, uint32_t minimum, uint32_t maximum,
                                uint32_t& value) noexcept
{
    yyjson_val* member = yyjson_obj_get(object, key);
    if (!yyjson_is_uint(member))
    {
        return false;
    }
    const uint64_t parsed = yyjson_get_uint(member);
    if (parsed < minimum || parsed > maximum)
    {
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

[[nodiscard]] bool IsColor(yyjson_val* object, const char* key) noexcept
{
    yyjson_val* value = yyjson_obj_get(object, key);
    if (!yyjson_is_str(value) || yyjson_get_len(value) != 7)
    {
        return false;
    }
    const char* text = yyjson_get_str(value);
    if (!text || text[0] != '#')
    {
        return false;
    }
    for (size_t index = 1; index < 7; ++index)
    {
        const char character = text[index];
        if (!((character >= '0' && character <= '9') || (character >= 'A' && character <= 'F') ||
              (character >= 'a' && character <= 'f')))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsValidMatrixPrivate(yyjson_val* object) noexcept
{
    constexpr std::array keys{
        "seed",      "glyphHeightDips", "densityPercent", "speedPercent", "trailLengthGlyphs", "mutationPerSecond",
        "headColor", "trailColor",      "glowPercent",
    };
    uint32_t value = 0;
    return HasExactKeys(object, keys) && ReadUnsigned(object, "seed", 0, UINT32_MAX, value) &&
           ReadUnsigned(object, "glyphHeightDips", 12, 48, value) &&
           ReadUnsigned(object, "densityPercent", 10, 100, value) &&
           ReadUnsigned(object, "speedPercent", 25, 300, value) &&
           ReadUnsigned(object, "trailLengthGlyphs", 6, 48, value) &&
           ReadUnsigned(object, "mutationPerSecond", 0, 30, value) && IsColor(object, "headColor") &&
           IsColor(object, "trailColor") && ReadUnsigned(object, "glowPercent", 0, 100, value);
}

[[nodiscard]] bool IsValidStudioClockPrivate(yyjson_val* object) noexcept
{
    constexpr std::array keys{
        "showSecondProgress", "externalDotsAlwaysOn", "showSeconds", "secondsColor",
        "showDate",           "dateFormat",           "timeColor",   "glowPercent",
    };
    yyjson_val* dateFormatValue = yyjson_obj_get(object, "dateFormat");
    const char* dateFormat = yyjson_is_str(dateFormatValue) ? yyjson_get_str(dateFormatValue) : nullptr;
    const bool validDateFormat =
        dateFormat && (std::strcmp(dateFormat, "dd-mm-yyyy") == 0 || std::strcmp(dateFormat, "mm-dd-yyyy") == 0 ||
                       std::strcmp(dateFormat, "yyyy-mm-dd") == 0);
    uint32_t value = 0;
    return HasExactKeys(object, keys) && yyjson_is_bool(yyjson_obj_get(object, "showSecondProgress")) &&
           yyjson_is_bool(yyjson_obj_get(object, "externalDotsAlwaysOn")) &&
           yyjson_is_bool(yyjson_obj_get(object, "showSeconds")) &&
           yyjson_is_bool(yyjson_obj_get(object, "showDate")) && validDateFormat && IsColor(object, "secondsColor") &&
           IsColor(object, "timeColor") && ReadUnsigned(object, "glowPercent", 0, 100, value);
}

[[nodiscard]] bool IsValidDeskClockPrivate(yyjson_val* object) noexcept
{
    constexpr std::array keys{
        "flipDurationMilliseconds",
        "cardColor",
        "digitColor",
        "dateColor",
    };
    uint32_t duration = 0;
    return HasExactKeys(object, keys) && ReadUnsigned(object, "flipDurationMilliseconds", 250, 800, duration) &&
           IsColor(object, "cardColor") && IsColor(object, "digitColor") && IsColor(object, "dateColor");
}

[[nodiscard]] bool IsValidShadersPrivate(yyjson_val* object) noexcept
{
    yyjson_val* modeValue = yyjson_obj_get(object, "mode");
    yyjson_val* shaderValue = yyjson_obj_get(object, "shader");
    Shaders::Mode mode = Shaders::Mode::Slideshow;
    uint32_t shaderIndex = 0;
    uint32_t value = 0;
    return HasExactKeys(object, Shaders::kSettingsKeys) && yyjson_is_str(modeValue) &&
           Shaders::TryParseMode({yyjson_get_str(modeValue), yyjson_get_len(modeValue)}, mode) &&
           yyjson_is_str(shaderValue) &&
           Shaders::TryFindShader({yyjson_get_str(shaderValue), yyjson_get_len(shaderValue)}, shaderIndex) &&
           ReadUnsigned(object, "intervalSeconds", Shaders::kMinimumIntervalSeconds, Shaders::kMaximumIntervalSeconds,
                        value) &&
           yyjson_is_bool(yyjson_obj_get(object, "shuffle")) &&
           ReadUnsigned(object, "renderScalePercent", Shaders::kMinimumRenderScalePercent,
                        Shaders::kMaximumRenderScalePercent, value);
}

[[nodiscard]] bool IsValidWeatherPrivate(yyjson_val* object) noexcept
{
    constexpr std::array keys{"locationMode", "location", "temperatureUnit", "windUnit"};
    yyjson_val* locationModeValue = yyjson_obj_get(object, "locationMode");
    yyjson_val* locationValue = yyjson_obj_get(object, "location");
    yyjson_val* temperatureValue = yyjson_obj_get(object, "temperatureUnit");
    yyjson_val* windValue = yyjson_obj_get(object, "windUnit");
    const char* locationMode = yyjson_is_str(locationModeValue) ? yyjson_get_str(locationModeValue) : nullptr;
    const char* temperatureUnit = yyjson_is_str(temperatureValue) ? yyjson_get_str(temperatureValue) : nullptr;
    const char* windUnit = yyjson_is_str(windValue) ? yyjson_get_str(windValue) : nullptr;
    const bool validLocationMode =
        locationMode && (std::strcmp(locationMode, "automatic") == 0 || std::strcmp(locationMode, "manual") == 0);
    const bool validTemperature = temperatureUnit && (std::strcmp(temperatureUnit, "celsius") == 0 ||
                                                      std::strcmp(temperatureUnit, "fahrenheit") == 0);
    const bool validWind = windUnit && (std::strcmp(windUnit, "kmh") == 0 || std::strcmp(windUnit, "mph") == 0);
    return HasExactKeys(object, keys) && validLocationMode && yyjson_is_str(locationValue) &&
           yyjson_get_len(locationValue) <= 128 && validTemperature && validWind;
}

[[nodiscard]] bool IsWeatherPrivate(const JsonObjectSettings& settings) noexcept
{
    unique_yyjson_doc document = ParseStoredObject(settings);
    return document && IsValidWeatherPrivate(yyjson_doc_get_root(document.get()));
}

[[nodiscard]] bool IsSupportedPluginType(std::string_view pluginId, std::string_view typeId) noexcept
{
    for (const RedXeBundledWidgetSpec& candidate : kRedXeBundledWidgets)
    {
        if (SettingsIdEquals(pluginId, candidate.pluginId) && SettingsIdEquals(typeId, candidate.typeId))
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] unique_yyjson_doc ParseStoredObject(const JsonObjectSettings& settings) noexcept
{
    if (settings.bytes < 2 || settings.bytes > kPrivateConfigurationCapacity || settings.utf8[settings.bytes] != '\0')
    {
        return {};
    }
    std::array<char, kPrivateConfigurationCapacity + 1> mutableJson = settings.utf8;
    yyjson_read_err error{};
    unique_yyjson_doc document{
        yyjson_read_opts(mutableJson.data(), settings.bytes, YYJSON_READ_NOFLAG, nullptr, &error)};
    if (!document || !yyjson_is_obj(yyjson_doc_get_root(document.get())))
    {
        return {};
    }
    return document;
}

[[nodiscard]] bool IsEmptyPrivate(const JsonObjectSettings& settings) noexcept
{
    unique_yyjson_doc document = ParseStoredObject(settings);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    return yyjson_is_obj(root) && yyjson_obj_size(root) == 0;
}

[[nodiscard]] bool IsMatrixPrivate(const JsonObjectSettings& settings) noexcept
{
    unique_yyjson_doc document = ParseStoredObject(settings);
    return document && IsValidMatrixPrivate(yyjson_doc_get_root(document.get()));
}

[[nodiscard]] bool IsProcessViewerPrivate(const JsonObjectSettings& settings) noexcept
{
    unique_yyjson_doc document = ParseStoredObject(settings);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    constexpr std::array keys{"topN", "hideIdle"};
    uint32_t topN = 0;
    return root && HasExactKeys(root, keys) && ReadUnsigned(root, "topN", 1, 32, topN) &&
           yyjson_is_bool(yyjson_obj_get(root, "hideIdle"));
}

[[nodiscard]] bool IsRankedViewerPrivate(const JsonObjectSettings& settings) noexcept
{
    unique_yyjson_doc document = ParseStoredObject(settings);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    constexpr std::array keys{"topN"};
    uint32_t topN = 0;
    return root && HasExactKeys(root, keys) && ReadUnsigned(root, "topN", 1, 16, topN);
}

[[nodiscard]] bool IsStudioClockPrivate(const JsonObjectSettings& settings) noexcept
{
    unique_yyjson_doc document = ParseStoredObject(settings);
    return document && IsValidStudioClockPrivate(yyjson_doc_get_root(document.get()));
}

[[nodiscard]] bool IsDeskClockPrivate(const JsonObjectSettings& settings) noexcept
{
    unique_yyjson_doc document = ParseStoredObject(settings);
    return document && IsValidDeskClockPrivate(yyjson_doc_get_root(document.get()));
}

[[nodiscard]] bool IsShadersPrivate(const JsonObjectSettings& settings) noexcept
{
    unique_yyjson_doc document = ParseStoredObject(settings);
    return document && IsValidShadersPrivate(yyjson_doc_get_root(document.get()));
}

[[nodiscard]] bool IsValidLauncherIconSize(yyjson_val* iconSize) noexcept
{
    if (!iconSize)
    {
        return true;
    }
    if (!yyjson_is_str(iconSize))
    {
        return false;
    }
    LauncherIconSize parsed = LauncherIconSize::Huge;
    return TryParseLauncherIconSize({yyjson_get_str(iconSize), yyjson_get_len(iconSize)}, parsed);
}

[[nodiscard]] bool IsValidLauncherPrivate(yyjson_val* settings) noexcept
{
    if (!yyjson_is_obj(settings))
    {
        return false;
    }
    const bool hasIconSize = yyjson_obj_get(settings, "iconSize") != nullptr;
    if (hasIconSize)
    {
        if (!HasExactKeys(settings, std::array{"shortcuts", "iconSize"}) ||
            !IsValidLauncherIconSize(yyjson_obj_get(settings, "iconSize")))
        {
            return false;
        }
    }
    else if (!HasExactKeys(settings, std::array{"shortcuts"}))
    {
        return false;
    }
    yyjson_val* shortcuts = yyjson_obj_get(settings, "shortcuts");
    if (!yyjson_is_arr(shortcuts) || yyjson_arr_size(shortcuts) > kLauncherMaximumShortcuts)
    {
        return false;
    }
    const size_t count = yyjson_arr_size(shortcuts);
    std::array<Launcher::ShortcutItem, kLauncherMaximumShortcuts> seen{};
    for (size_t index = 0; index < count; ++index)
    {
        Launcher::ShortcutItem parsed{};
        const char* error = nullptr;
        if (!Launcher::ParseShortcutItem(yyjson_arr_get(shortcuts, index), &HostActionCatalog::IsKnownActionName,
                                         parsed, &error))
        {
            return false;
        }
        for (size_t previous = 0; previous < index; ++previous)
        {
            if (Launcher::ShortcutsEqual(seen[previous].action, seen[previous].target, parsed.action, parsed.target))
            {
                return false;
            }
        }
        seen[index] = parsed;
    }
    return true;
}

[[nodiscard]] bool IsLauncherPrivate(const JsonObjectSettings& settings) noexcept
{
    unique_yyjson_doc document = ParseStoredObject(settings);
    return document && IsValidLauncherPrivate(yyjson_doc_get_root(document.get()));
}

[[nodiscard]] HRESULT CopyObject(yyjson_val* object, JsonObjectSettings& destination) noexcept
{
    if (!yyjson_is_obj(object))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    yyjson_write_err error{};
    size_t length = 0;
    unique_malloc_string serialized{yyjson_val_write_opts(object, YYJSON_WRITE_NOFLAG, nullptr, &length, &error)};
    if (!serialized)
    {
        return E_OUTOFMEMORY;
    }
    if (length == 0 || length > kPrivateConfigurationCapacity)
    {
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    }

    JsonObjectSettings copied{};
    copied.utf8.fill('\0');
    std::memcpy(copied.utf8.data(), serialized.get(), length);
    copied.bytes = static_cast<uint32_t>(length);
    destination = copied;
    return S_OK;
}

[[nodiscard]] HRESULT MergeSettingsObject(const JsonObjectSettings& base, yyjson_val* patch,
                                          JsonObjectSettings& destination) noexcept
{
    if (!yyjson_is_obj(patch))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    unique_yyjson_doc baseDocument = ParseStoredObject(base);
    yyjson_val* baseRoot = baseDocument ? yyjson_doc_get_root(baseDocument.get()) : nullptr;
    if (!yyjson_is_obj(baseRoot))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    unique_mut_doc mutableDocument{yyjson_mut_doc_new(nullptr)};
    yyjson_mut_val* mutableRoot = mutableDocument ? yyjson_val_mut_copy(mutableDocument.get(), baseRoot) : nullptr;
    if (!mutableRoot)
    {
        return E_OUTOFMEMORY;
    }
    yyjson_mut_doc_set_root(mutableDocument.get(), mutableRoot);
    yyjson_obj_iter iterator = yyjson_obj_iter_with(patch);
    yyjson_val* key = nullptr;
    while ((key = yyjson_obj_iter_next(&iterator)))
    {
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        yyjson_mut_val* mutKey = yyjson_mut_strncpy(mutableDocument.get(), yyjson_get_str(key), yyjson_get_len(key));
        yyjson_mut_val* mutValue = yyjson_val_mut_copy(mutableDocument.get(), value);
        if (!mutKey || !mutValue || !yyjson_mut_obj_put(mutableRoot, mutKey, mutValue))
        {
            return E_OUTOFMEMORY;
        }
    }
    size_t length = 0;
    unique_malloc_string written{yyjson_mut_write(mutableDocument.get(), YYJSON_WRITE_NOFLAG, &length)};
    if (!written || length == 0)
    {
        return E_OUTOFMEMORY;
    }
    if (length > kPrivateConfigurationCapacity)
    {
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    }
    JsonObjectSettings copied{};
    copied.utf8.fill('\0');
    std::memcpy(copied.utf8.data(), written.get(), length);
    copied.bytes = static_cast<uint32_t>(length);
    destination = copied;
    return S_OK;
}

[[nodiscard]] HRESULT ParsePlugin(yyjson_val* value, PluginSettings& plugin) noexcept
{
    constexpr std::array keys{"id", "enabled", "private"};
    yyjson_val* enabled = yyjson_is_obj(value) ? yyjson_obj_get(value, "enabled") : nullptr;
    if (!HasExactKeys(value, keys) || !CopyText(yyjson_obj_get(value, "id"), plugin.id, true) ||
        !yyjson_is_bool(enabled))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    plugin.enabled = yyjson_get_bool(enabled);
    return CopyObject(yyjson_obj_get(value, "private"), plugin.privateConfiguration);
}

[[nodiscard]] HRESULT ParsePlacement(yyjson_val* value, WidgetGridPlacement& placement) noexcept
{
    constexpr std::array keys{"column", "row", "columnSpan", "rowSpan"};
    if (!HasExactKeys(value, keys) ||
        !ReadUnsigned(value, "column", 0, kMaximumDashboardGridDimension - 1, placement.column) ||
        !ReadUnsigned(value, "row", 0, kMaximumDashboardGridDimension - 1, placement.row) ||
        !ReadUnsigned(value, "columnSpan", 1, kMaximumDashboardGridDimension, placement.columnSpan) ||
        !ReadUnsigned(value, "rowSpan", 1, kMaximumDashboardGridDimension, placement.rowSpan))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

[[nodiscard]] HRESULT ParseWidget(yyjson_val* value, WidgetInstanceSettings& widget) noexcept
{
    constexpr std::array keys{"id", "pluginId", "typeId", "placement", "private"};
    if (!HasExactKeys(value, keys) || !CopyText(yyjson_obj_get(value, "id"), widget.id, true) ||
        !CopyText(yyjson_obj_get(value, "pluginId"), widget.pluginId, true) ||
        !CopyText(yyjson_obj_get(value, "typeId"), widget.typeId, true))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    HRESULT result = ParsePlacement(yyjson_obj_get(value, "placement"), widget.placement);
    if (SUCCEEDED(result))
    {
        result = CopyObject(yyjson_obj_get(value, "private"), widget.privateConfiguration);
    }
    return result;
}

[[nodiscard]] HRESULT ParsePage(yyjson_val* value, DashboardPageSettings& page)
{
    constexpr std::array keys{"id", "name", "widgets"};
    yyjson_val* widgets = yyjson_is_obj(value) ? yyjson_obj_get(value, "widgets") : nullptr;
    if (!HasExactKeys(value, keys) || !CopyText(yyjson_obj_get(value, "id"), page.id, true) ||
        !CopyText(yyjson_obj_get(value, "name"), page.name, false) || !yyjson_is_arr(widgets))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    const size_t count = yyjson_arr_size(widgets);
    if (count == 0 || count > kMaximumWidgetsPerPage)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    page.widgets.resize(count);
    for (size_t index = 0; index < count; ++index)
    {
        const HRESULT result = ParseWidget(yyjson_arr_get(widgets, index), page.widgets[index]);
        if (FAILED(result))
        {
            return result;
        }
    }
    page.widgetCount = static_cast<uint32_t>(count);
    return S_OK;
}

[[nodiscard]] HRESULT GetModuleDirectory(std::filesystem::path& directory) noexcept
{
    try
    {
        std::vector<wchar_t> path(512);
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0)
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
            if (length < path.size() - 1)
            {
                directory = std::filesystem::path(path.data()).parent_path();
                return directory.empty() ? HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME) : S_OK;
            }
            if (path.size() >= 32768)
            {
                return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            }
            path.resize(path.size() * 2);
        }
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT EnsureDirectory(const std::filesystem::path& directory) noexcept
{
    const int result = SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);
    if (result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS)
    {
        return S_OK;
    }
    return HRESULT_FROM_WIN32(static_cast<DWORD>(result));
}

[[nodiscard]] std::wstring MakeLogsDirectory(const std::wstring& settingsDirectory) noexcept
{
    try
    {
        const std::filesystem::path settings(settingsDirectory);
        if (_wcsicmp(settings.filename().c_str(), L"Settings") == 0)
        {
            return (settings.parent_path() / kRedXeLogsDirectoryName).wstring();
        }
        return (settings / kRedXeLogsDirectoryName).wstring();
    }
    catch (...)
    {
        return {};
    }
}

// The schema copy, refreshed from the deployed file on every start. It skips the data flush a settings document gets
// (InstallTemplateFile): a schema that a power loss empties is copied again at the next start.
[[nodiscard]] HRESULT CopyFileAtomically(const std::filesystem::path& source, const std::filesystem::path& target,
                                         bool replaceExisting) noexcept
{
    try
    {
        std::wstring temporary = target.wstring();
        temporary.append(L".tmp.");
        temporary.append(std::to_wstring(GetCurrentProcessId()));
        temporary.push_back(L'.');
        temporary.append(std::to_wstring(GetTickCount64()));

        if (!CopyFileW(source.c_str(), temporary.c_str(), TRUE))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        const auto cleanup = wil::scope_exit([&temporary]() noexcept { DeleteFileW(temporary.c_str()); });
        const DWORD flags = MOVEFILE_WRITE_THROUGH | (replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0U);
        if (!MoveFileExW(temporary.c_str(), target.c_str(), flags))
        {
            const DWORD error = GetLastError();
            if (!replaceExisting && (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS))
            {
                return S_FALSE;
            }
            return HRESULT_FROM_WIN32(error);
        }
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

#if !defined(_DEBUG)
[[nodiscard]] HRESULT MigrateLegacyReleaseSettingsName(const std::filesystem::path& directory,
                                                       const std::filesystem::path& target) noexcept
{
    const DWORD targetAttributes = GetFileAttributesW(target.c_str());
    if (targetAttributes != INVALID_FILE_ATTRIBUTES)
    {
        return (targetAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ? S_FALSE : HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    }
    const DWORD targetError = GetLastError();
    if (targetError != ERROR_FILE_NOT_FOUND && targetError != ERROR_PATH_NOT_FOUND)
    {
        return HRESULT_FROM_WIN32(targetError);
    }

    const std::filesystem::path legacy = directory / kLegacyReleaseSettingsFileName;
    const DWORD legacyAttributes = GetFileAttributesW(legacy.c_str());
    if (legacyAttributes == INVALID_FILE_ATTRIBUTES)
    {
        const DWORD legacyError = GetLastError();
        return legacyError == ERROR_FILE_NOT_FOUND || legacyError == ERROR_PATH_NOT_FOUND
                   ? S_FALSE
                   : HRESULT_FROM_WIN32(legacyError);
    }
    if ((legacyAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    }
    return MoveFileExW(legacy.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) ? S_OK
                                                                               : HRESULT_FROM_WIN32(GetLastError());
}
#endif

[[nodiscard]] HRESULT BackupInvalidSettings(const std::filesystem::path& path,
                                            std::filesystem::path& backupPath) noexcept
{
    try
    {
        SYSTEMTIME utc{};
        GetSystemTime(&utc);
        wchar_t suffix[64]{};
        const int written = swprintf_s(suffix, L".invalid-%04u-%02u-%02u_%02u-%02u-%02uZ", utc.wYear, utc.wMonth,
                                       utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
        if (written <= 0)
        {
            return E_FAIL;
        }
        const std::filesystem::path backup =
            path.parent_path() / (path.stem().wstring() + suffix + path.extension().wstring());
        if (!MoveFileExW(path.c_str(), backup.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        backupPath = backup;
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT ReadFileBytes(std::wstring_view path, std::vector<char>& bytes) noexcept
{
    try
    {
        const std::wstring pathText(path);
        wil::unique_hfile file{CreateFileW(pathText.c_str(), GENERIC_READ,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
        if (!file)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.get(), &size))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) > kMaximumSettingsBytes)
        {
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        }

        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD totalRead = 0;
        while (totalRead < bytes.size())
        {
            DWORD chunkRead = 0;
            const DWORD remaining = static_cast<DWORD>(bytes.size() - totalRead);
            if (!ReadFile(file.get(), bytes.data() + totalRead, remaining, &chunkRead, nullptr))
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
            if (chunkRead == 0)
            {
                return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
            }
            totalRead += chunkRead;
        }
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] bool ParseWidgetInstanceIndex(std::string_view instanceId, uint32_t& index) noexcept
{
    constexpr std::string_view prefix{"widget."};
    if (instanceId.size() <= prefix.size() || instanceId.compare(0, prefix.size(), prefix) != 0)
    {
        return false;
    }
    uint32_t value = 0;
    for (size_t cursor = prefix.size(); cursor < instanceId.size(); ++cursor)
    {
        const char digit = instanceId[cursor];
        if (digit < '0' || digit > '9' || value > (UINT32_MAX - 9U) / 10U)
        {
            return false;
        }
        value = value * 10U + static_cast<uint32_t>(digit - '0');
    }
    if (value == 0)
    {
        return false;
    }
    index = value - 1U;
    return true;
}

[[nodiscard]] WidgetInstanceSettings* FindWidgetInstance(AppSettings& settings, std::string_view instanceId) noexcept
{
    for (uint32_t pageIndex = 0; pageIndex < settings.dashboard.pageCount; ++pageIndex)
    {
        DashboardPageSettings& page = settings.dashboard.pages[pageIndex];
        for (uint32_t widgetIndex = 0; widgetIndex < page.widgetCount; ++widgetIndex)
        {
            if (SettingsIdEquals(page.widgets[widgetIndex].id.View(), instanceId))
            {
                return &page.widgets[widgetIndex];
            }
        }
    }
    return nullptr;
}

[[nodiscard]] bool ApplyFlattenedSettings(yyjson_mut_doc* document, yyjson_mut_val* widget,
                                          yyjson_mut_val* settingsObject) noexcept
{
    if (!document || !yyjson_mut_is_obj(widget) || !yyjson_mut_is_obj(settingsObject))
    {
        return false;
    }
    // plugin, use, and backgroundColor are host-owned widget keys: a plugin persist replaces only the plugin's
    // flattened members around them.
    const auto hostOwned = [](const char* name) noexcept
    {
        return name && (std::strcmp(name, "plugin") == 0 || std::strcmp(name, "use") == 0 ||
                        std::strcmp(name, "backgroundColor") == 0);
    };
    std::vector<std::string> remove;
    yyjson_mut_obj_iter iterator = yyjson_mut_obj_iter_with(widget);
    while (yyjson_mut_val* key = yyjson_mut_obj_iter_next(&iterator))
    {
        const char* name = yyjson_mut_get_str(key);
        if (name && !hostOwned(name))
        {
            remove.emplace_back(name);
        }
    }
    for (const std::string& name : remove)
    {
        yyjson_mut_obj_remove_key(widget, name.c_str());
    }
    yyjson_mut_obj_iter settingsIterator = yyjson_mut_obj_iter_with(settingsObject);
    while (yyjson_mut_val* key = yyjson_mut_obj_iter_next(&settingsIterator))
    {
        const char* name = yyjson_mut_get_str(key);
        if (!name || hostOwned(name))
        {
            continue;
        }
        yyjson_mut_val* copiedValue = yyjson_mut_val_mut_copy(document, yyjson_mut_obj_iter_get_val(key));
        if (!copiedValue || !yyjson_mut_obj_add_val(document, widget, name, copiedValue))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsDeclareName(yyjson_mut_val* declare, std::string_view name) noexcept
{
    return yyjson_mut_is_obj(declare) && yyjson_mut_obj_getn(declare, name.data(), name.size()) != nullptr;
}

[[nodiscard]] bool PatchMutableWidgetValue(yyjson_mut_doc* document, yyjson_mut_val* parent, bool parentIsArray,
                                           size_t arrayIndex, const char* objectKey, yyjson_mut_val* settingsObject,
                                           yyjson_mut_val* declare) noexcept
{
    if (!document || !parent || !settingsObject)
    {
        return false;
    }
    yyjson_mut_val* widget =
        parentIsArray ? yyjson_mut_arr_get(parent, arrayIndex) : yyjson_mut_obj_get(parent, objectKey);
    if (!widget)
    {
        return false;
    }
    if (yyjson_mut_is_str(widget))
    {
        yyjson_mut_val* wrapper = yyjson_mut_obj(document);
        if (!wrapper)
        {
            return false;
        }
        const std::string_view name(yyjson_mut_get_str(widget), yyjson_mut_get_len(widget));
        yyjson_mut_val* copiedName = yyjson_mut_strncpy(document, name.data(), name.size());
        const bool useDeclare = IsDeclareName(declare, name);
        if (!copiedName || !yyjson_mut_obj_add_val(document, wrapper, useDeclare ? "use" : "plugin", copiedName) ||
            !ApplyFlattenedSettings(document, wrapper, settingsObject))
        {
            return false;
        }
        if (parentIsArray)
        {
            return yyjson_mut_arr_replace(parent, arrayIndex, wrapper) != nullptr;
        }
        yyjson_mut_val* key = yyjson_mut_str(document, objectKey);
        return key && yyjson_mut_obj_put(parent, key, wrapper);
    }
    if (!yyjson_mut_is_obj(widget))
    {
        return false;
    }
    return ApplyFlattenedSettings(document, widget, settingsObject);
}

[[nodiscard]] bool IsMutableSplitContainer(yyjson_mut_val* item) noexcept
{
    return yyjson_mut_is_obj(item) && (yyjson_mut_obj_get(item, "weight") || yyjson_mut_obj_get(item, "widget") ||
                                       yyjson_mut_obj_get(item, "rows") || yyjson_mut_obj_get(item, "columns"));
}

[[nodiscard]] bool PatchMutableHumanItems(yyjson_mut_doc* document, yyjson_mut_val* items, uint32_t& instanceIndex,
                                          uint32_t targetIndex, yyjson_mut_val* settingsObject,
                                          yyjson_mut_val* declare) noexcept;

[[nodiscard]] bool PatchMutableHumanItems(yyjson_mut_doc* document, yyjson_mut_val* items, uint32_t& instanceIndex,
                                          uint32_t targetIndex, yyjson_mut_val* settingsObject,
                                          yyjson_mut_val* declare) noexcept
{
    if (!yyjson_mut_is_arr(items))
    {
        return false;
    }
    const size_t count = yyjson_mut_arr_size(items);
    for (size_t index = 0; index < count; ++index)
    {
        yyjson_mut_val* item = yyjson_mut_arr_get(items, index);
        if (IsMutableSplitContainer(item))
        {
            yyjson_mut_val* widget = yyjson_mut_obj_get(item, "widget");
            yyjson_mut_val* rows = yyjson_mut_obj_get(item, "rows");
            yyjson_mut_val* columns = yyjson_mut_obj_get(item, "columns");
            if (widget)
            {
                if (instanceIndex == targetIndex)
                {
                    return PatchMutableWidgetValue(document, item, false, 0, "widget", settingsObject, declare);
                }
                ++instanceIndex;
            }
            else if (rows)
            {
                if (PatchMutableHumanItems(document, rows, instanceIndex, targetIndex, settingsObject, declare))
                {
                    return true;
                }
            }
            else if (columns)
            {
                if (PatchMutableHumanItems(document, columns, instanceIndex, targetIndex, settingsObject, declare))
                {
                    return true;
                }
            }
        }
        else
        {
            if (instanceIndex == targetIndex)
            {
                return PatchMutableWidgetValue(document, items, true, index, nullptr, settingsObject, declare);
            }
            ++instanceIndex;
        }
    }
    return false;
}

[[nodiscard]] bool PatchMutablePage(yyjson_mut_doc* document, yyjson_mut_val* page, uint32_t& instanceIndex,
                                    uint32_t targetIndex, yyjson_mut_val* settingsObject,
                                    yyjson_mut_val* declare) noexcept
{
    if (!yyjson_mut_is_obj(page))
    {
        return false;
    }
    yyjson_mut_val* widgets = yyjson_mut_obj_get(page, "widgets");
    yyjson_mut_val* columns = yyjson_mut_obj_get(page, "columns");
    yyjson_mut_val* rows = yyjson_mut_obj_get(page, "rows");
    yyjson_mut_val* items = widgets ? widgets : (columns ? columns : rows);
    return items && PatchMutableHumanItems(document, items, instanceIndex, targetIndex, settingsObject, declare);
}

// S_FALSE without replaceExisting when the target already exists (an install that lost the race keeps that file).
[[nodiscard]] HRESULT WriteUtf8FileAtomically(const std::filesystem::path& target, std::string_view bytes,
                                              bool replaceExisting = true) noexcept
{
    try
    {
        std::wstring temporary = target.wstring();
        temporary.append(L".tmp.");
        temporary.append(std::to_wstring(GetCurrentProcessId()));
        temporary.push_back(L'.');
        temporary.append(std::to_wstring(GetTickCount64()));
        wil::unique_hfile file{CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
        if (!file)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        // The handle closes first: the temporary file is not shared, so deleting it while open fails and leaves it.
        const auto cleanup = wil::scope_exit(
            [&temporary, &file]() noexcept
            {
                file.reset();
                DeleteFileW(temporary.c_str());
            });
        DWORD written = 0;
        if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        // A short write that reports no error is still a failed write: never rename a truncated document into place.
        if (written != bytes.size())
        {
            return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        }
        // MOVEFILE_WRITE_THROUGH makes only the rename durable. Flush the data first, so a power loss right after a
        // save cannot leave the settings name on bytes that never reached the disk.
        if (!FlushFileBuffers(file.get()))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        file.reset();
        const DWORD flags = MOVEFILE_WRITE_THROUGH | (replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0U);
        if (!MoveFileExW(temporary.c_str(), target.c_str(), flags))
        {
            const DWORD error = GetLastError();
            if (!replaceExisting && (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS))
            {
                return S_FALSE;
            }
            return HRESULT_FROM_WIN32(error);
        }
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

// A plain template install or recovery: the template's bytes through the same flushed atomic write as a persist, so a
// power loss right after it cannot leave an empty settings file that the next start backs up and replaces. S_FALSE
// without replaceExisting when the target already exists.
[[nodiscard]] HRESULT InstallTemplateFile(const std::filesystem::path& source, const std::filesystem::path& target,
                                          bool replaceExisting) noexcept
{
    std::vector<char> bytes;
    const HRESULT result = ReadFileBytes(source.native(), bytes);
    return FAILED(result)
               ? result
               : WriteUtf8FileAtomically(target, std::string_view(bytes.data(), bytes.size()), replaceExisting);
}

[[nodiscard]] HRESULT InstallIfMissing(const std::filesystem::path& source,
                                       const std::filesystem::path& target) noexcept
{
    const DWORD attributes = GetFileAttributesW(target.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES)
    {
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ? S_FALSE : HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    }
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
    {
        return HRESULT_FROM_WIN32(error);
    }
    return InstallTemplateFile(source, target, false);
}
} // namespace

bool SettingsIdEquals(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (size_t index = 0; index < left.size(); ++index)
    {
        if (RedXeAsciiLower(left[index]) != RedXeAsciiLower(right[index]))
        {
            return false;
        }
    }
    return true;
}

const PluginSettings* FindPluginSettings(const AppSettings& settings, std::string_view pluginId) noexcept
{
    for (uint32_t index = 0; index < settings.pluginCount; ++index)
    {
        if (SettingsIdEquals(settings.plugins[index].id.View(), pluginId))
        {
            return &settings.plugins[index];
        }
    }
    return nullptr;
}

PluginSettings* FindPluginSettings(AppSettings& settings, std::string_view pluginId) noexcept
{
    return const_cast<PluginSettings*>(FindPluginSettings(static_cast<const AppSettings&>(settings), pluginId));
}

const DashboardPageSettings* FindDashboardPage(const AppSettings& settings, std::string_view pageId) noexcept
{
    for (uint32_t index = 0; index < settings.dashboard.pageCount; ++index)
    {
        if (SettingsIdEquals(settings.dashboard.pages[index].id.View(), pageId))
        {
            return &settings.dashboard.pages[index];
        }
    }
    return nullptr;
}

DashboardPageSettings* FindDashboardPage(AppSettings& settings, std::string_view pageId) noexcept
{
    return const_cast<DashboardPageSettings*>(FindDashboardPage(static_cast<const AppSettings&>(settings), pageId));
}

const DashboardPageSettings* FindActiveDashboardPage(const AppSettings& settings) noexcept
{
    return FindDashboardPage(settings, settings.dashboard.activePageId.View());
}

DashboardPageSettings* FindActiveDashboardPage(AppSettings& settings) noexcept
{
    return FindDashboardPage(settings, settings.dashboard.activePageId.View());
}

HRESULT MoveDashboardPage(AppSettings& settings, int direction) noexcept
{
    if (settings.dashboard.pageCount < 2 || settings.dashboard.pages.size() != settings.dashboard.pageCount ||
        settings.dashboard.activePageIndex >= settings.dashboard.pageCount || (direction != -1 && direction != 1))
    {
        return E_INVALIDARG;
    }
    const uint32_t current = settings.dashboard.activePageIndex;
    uint32_t selected = current;
    if (direction > 0)
    {
        if (current + 1U < settings.dashboard.pageCount)
            selected = current + 1U;
        else if (settings.dashboard.wrapPages)
            selected = 0;
        else
            return HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);
    }
    else
    {
        if (current > 0)
            selected = current - 1U;
        else if (settings.dashboard.wrapPages)
            selected = settings.dashboard.pageCount - 1U;
        else
            return HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);
    }
    settings.dashboard.activePageIndex = selected;
    settings.dashboard.activePageId = settings.dashboard.pages[selected].id;
    return S_OK;
}

HRESULT PreserveActiveDashboardPage(const AppSettings& previous, AppSettings& candidate) noexcept
{
    if (candidate.dashboard.pageCount == 0 || candidate.dashboard.pages.size() != candidate.dashboard.pageCount)
    {
        return E_INVALIDARG;
    }

    // Launch still starts on page 0. Live reload keeps the page that was current when that page still exists: same
    // id first (authored ids follow a reorder; generated page.N ids stay put when the list is unchanged), otherwise
    // the same index if it is still in range, otherwise the first page. The active page is not written to disk.
    if (previous.dashboard.activePageId.bytes != 0)
    {
        for (uint32_t index = 0; index < candidate.dashboard.pageCount; ++index)
        {
            if (SettingsIdEquals(candidate.dashboard.pages[index].id.View(), previous.dashboard.activePageId.View()))
            {
                candidate.dashboard.activePageIndex = index;
                candidate.dashboard.activePageId = candidate.dashboard.pages[index].id;
                return S_OK;
            }
        }
    }
    if (previous.dashboard.activePageIndex < candidate.dashboard.pageCount)
    {
        candidate.dashboard.activePageIndex = previous.dashboard.activePageIndex;
        candidate.dashboard.activePageId = candidate.dashboard.pages[previous.dashboard.activePageIndex].id;
        return S_OK;
    }
    candidate.dashboard.activePageIndex = 0;
    candidate.dashboard.activePageId = candidate.dashboard.pages[0].id;
    return S_OK;
}

bool ActiveDashboardRuntimeEquals(const AppSettings& left, const AppSettings& right) noexcept
{
    if (left.dashboard.gridColumns != right.dashboard.gridColumns ||
        left.dashboard.gridRows != right.dashboard.gridRows || left.backgroundRgb != right.backgroundRgb)
    {
        return false;
    }

    const DashboardPageSettings* leftPage = FindActiveDashboardPage(left);
    const DashboardPageSettings* rightPage = FindActiveDashboardPage(right);
    if (!leftPage || !rightPage || leftPage->widgetCount != rightPage->widgetCount)
    {
        return false;
    }

    for (uint32_t index = 0; index < leftPage->widgetCount; ++index)
    {
        const WidgetInstanceSettings& leftWidget = leftPage->widgets[index];
        const WidgetInstanceSettings& rightWidget = rightPage->widgets[index];
        if (leftWidget != rightWidget)
        {
            return false;
        }

        const PluginSettings* leftPlugin = FindPluginSettings(left, leftWidget.pluginId.View());
        const PluginSettings* rightPlugin = FindPluginSettings(right, rightWidget.pluginId.View());
        if (!leftPlugin || !rightPlugin || *leftPlugin != *rightPlugin)
        {
            return false;
        }
    }
    return true;
}

HRESULT SetJsonObjectSettings(std::string_view json, JsonObjectSettings& settings) noexcept
{
    if (json.empty() || json.size() > kPrivateConfigurationCapacity)
    {
        return E_INVALIDARG;
    }
    try
    {
        std::vector<char> mutableJson(json.begin(), json.end());
        yyjson_read_err error{};
        unique_yyjson_doc document{
            yyjson_read_opts(mutableJson.data(), mutableJson.size(), YYJSON_READ_NOFLAG, nullptr, &error)};
        return document ? CopyObject(yyjson_doc_get_root(document.get()), settings)
                        : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

HRESULT ValidateAppSettings(const AppSettings& settings) noexcept
{
    if (settings.pluginCount > kMaximumSettingsPlugins || settings.plugins.size() != settings.pluginCount ||
        settings.dashboard.gridColumns == 0 || settings.dashboard.gridRows == 0 ||
        settings.dashboard.gridColumns > kMaximumDashboardGridDimension ||
        settings.dashboard.gridRows > kMaximumDashboardGridDimension || settings.dashboard.pageCount == 0 ||
        settings.dashboard.pageCount > kMaximumDashboardPages ||
        !IsValidStoredText(settings.dashboard.activePageId, true))
    {
        return E_INVALIDARG;
    }

    for (uint32_t index = 0; index < settings.pluginCount; ++index)
    {
        const PluginSettings& plugin = settings.plugins[index];
        if (!IsValidStoredText(plugin.id, true) || !ParseStoredObject(plugin.privateConfiguration))
        {
            return E_INVALIDARG;
        }
        for (uint32_t previous = 0; previous < index; ++previous)
        {
            if (SettingsIdEquals(settings.plugins[previous].id.View(), plugin.id.View()))
            {
                return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
            }
        }
        if (!IsEmptyPrivate(plugin.privateConfiguration))
        {
            return E_INVALIDARG;
        }
    }

    if (settings.serviceCount > kMaximumSettingsServices || settings.services.size() != settings.serviceCount)
    {
        return E_INVALIDARG;
    }
    for (uint32_t index = 0; index < settings.serviceCount; ++index)
    {
        const ServiceSettings& service = settings.services[index];
        if (!IsValidStoredText(service.name, false) || !IsValidStoredText(service.pluginId, true) ||
            !ParseStoredObject(service.privateConfiguration))
        {
            return E_INVALIDARG;
        }
        bool catalogued = false;
        for (const RedXeBundledServiceSpec& candidate : kRedXeBundledServices)
        {
            catalogued = catalogued || SettingsIdEquals(candidate.pluginId, service.pluginId.View());
        }
        if (!catalogued)
        {
            return E_INVALIDARG;
        }
        for (uint32_t previous = 0; previous < index; ++previous)
        {
            if (settings.services[previous].name.View() == service.name.View() ||
                SettingsIdEquals(settings.services[previous].pluginId.View(), service.pluginId.View()))
            {
                return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
            }
        }
    }

    bool activeFound = false;
    for (uint32_t pageIndex = 0; pageIndex < settings.dashboard.pageCount; ++pageIndex)
    {
        const DashboardPageSettings& page = settings.dashboard.pages[pageIndex];
        if (!IsValidStoredText(page.id, true) || !IsValidStoredText(page.name, false) ||
            page.widgetCount > kMaximumWidgetsPerPage || page.widgets.size() != page.widgetCount)
        {
            return E_INVALIDARG;
        }
        if (SettingsIdEquals(page.id.View(), settings.dashboard.activePageId.View()))
        {
            activeFound = true;
        }
        for (uint32_t previousPage = 0; previousPage < pageIndex; ++previousPage)
        {
            if (SettingsIdEquals(settings.dashboard.pages[previousPage].id.View(), page.id.View()))
            {
                return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
            }
        }

        std::array<bool, kMaximumDashboardGridDimension * kMaximumDashboardGridDimension> occupied{};
        uint32_t matrixCount = 0;
        for (uint32_t widgetIndex = 0; widgetIndex < page.widgetCount; ++widgetIndex)
        {
            const WidgetInstanceSettings& widget = page.widgets[widgetIndex];
            const WidgetGridPlacement& placement = widget.placement;
            if (!IsValidStoredText(widget.id, true) || !IsValidStoredText(widget.pluginId, true) ||
                !IsValidStoredText(widget.typeId, true) || !ParseStoredObject(widget.privateConfiguration) ||
                (!widget.usesAdaptivePlacement &&
                 (placement.column >= settings.dashboard.gridColumns || placement.row >= settings.dashboard.gridRows ||
                  placement.columnSpan == 0 || placement.rowSpan == 0 ||
                  placement.columnSpan > settings.dashboard.gridColumns - placement.column ||
                  placement.rowSpan > settings.dashboard.gridRows - placement.row)) ||
                (widget.usesAdaptivePlacement &&
                 (widget.adaptivePlacement.depth == 0 || widget.adaptivePlacement.depth > kMaximumLayoutDepth)))
            {
                return E_INVALIDARG;
            }
            if (widget.usesAdaptivePlacement)
            {
                for (uint32_t stepIndex = 0; stepIndex < widget.adaptivePlacement.depth; ++stepIndex)
                {
                    const LayoutSplitStep& step = widget.adaptivePlacement.steps[stepIndex];
                    if ((step.axis != LayoutAxis::LongSide && step.axis != LayoutAxis::ShortSide) ||
                        step.sizeRatio == 0 || step.sizeRatio > 1000 || step.totalRatio == 0 ||
                        step.precedingRatio >= step.totalRatio ||
                        step.sizeRatio > step.totalRatio - step.precedingRatio)
                    {
                        return E_INVALIDARG;
                    }
                }
            }

            for (uint32_t previousPage = 0; previousPage <= pageIndex; ++previousPage)
            {
                const DashboardPageSettings& earlierPage = settings.dashboard.pages[previousPage];
                const uint32_t limit = previousPage == pageIndex ? widgetIndex : earlierPage.widgetCount;
                for (uint32_t previousWidget = 0; previousWidget < limit; ++previousWidget)
                {
                    if (SettingsIdEquals(earlierPage.widgets[previousWidget].id.View(), widget.id.View()))
                    {
                        return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
                    }
                }
            }

            const PluginSettings* plugin = FindPluginSettings(settings, widget.pluginId.View());
            if (!plugin || !plugin->enabled || !IsSupportedPluginType(widget.pluginId.View(), widget.typeId.View()))
            {
                return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            }
            if (SettingsIdEquals(widget.pluginId.View(), kMatrixPluginId))
            {
                ++matrixCount;
                if (!IsMatrixPrivate(widget.privateConfiguration))
                {
                    return E_INVALIDARG;
                }
            }
            else if (SettingsIdEquals(widget.pluginId.View(), kProcessViewerPluginId))
            {
                if (!IsProcessViewerPrivate(widget.privateConfiguration))
                {
                    return E_INVALIDARG;
                }
            }
            else if (SettingsIdEquals(widget.pluginId.View(), "builtin.network-meter") ||
                     SettingsIdEquals(widget.pluginId.View(), "builtin.gpu-processes"))
            {
                if (!IsRankedViewerPrivate(widget.privateConfiguration))
                {
                    return E_INVALIDARG;
                }
            }
            else if (SettingsIdEquals(widget.pluginId.View(), kStudioClockPluginId))
            {
                if (!IsStudioClockPrivate(widget.privateConfiguration))
                {
                    return E_INVALIDARG;
                }
            }
            else if (SettingsIdEquals(widget.pluginId.View(), kDeskClockPluginId))
            {
                if (!IsDeskClockPrivate(widget.privateConfiguration))
                {
                    return E_INVALIDARG;
                }
            }
            else if (SettingsIdEquals(widget.pluginId.View(), kShadersPluginId))
            {
                if (!IsShadersPrivate(widget.privateConfiguration))
                {
                    return E_INVALIDARG;
                }
            }
            else if (SettingsIdEquals(widget.pluginId.View(), kWeatherPluginId))
            {
                if (!IsWeatherPrivate(widget.privateConfiguration))
                {
                    return E_INVALIDARG;
                }
            }
            else if (SettingsIdEquals(widget.pluginId.View(), kLauncherPluginId))
            {
                if (!IsLauncherPrivate(widget.privateConfiguration))
                {
                    return E_INVALIDARG;
                }
            }
            else if (SettingsIdEquals(widget.pluginId.View(), kAvControlPluginId))
            {
                AVControl::Configuration configuration;
                if (FAILED(AVControl::ParseConfiguration(
                        {widget.privateConfiguration.utf8.data(), widget.privateConfiguration.bytes}, configuration)))
                    return E_INVALIDARG;
            }
            else if (!IsEmptyPrivate(widget.privateConfiguration))
            {
                return E_INVALIDARG;
            }

            if (widget.usesAdaptivePlacement)
            {
                continue;
            }
            for (uint32_t row = placement.row; row < placement.row + placement.rowSpan; ++row)
            {
                for (uint32_t column = placement.column; column < placement.column + placement.columnSpan; ++column)
                {
                    const size_t cell = static_cast<size_t>(row) * settings.dashboard.gridColumns + column;
                    if (occupied[cell])
                    {
                        return HRESULT_FROM_WIN32(ERROR_ALREADY_ASSIGNED);
                    }
                    occupied[cell] = true;
                }
            }
        }
        if (matrixCount > 1)
        {
            return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
        }
    }
    return activeFound ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

[[nodiscard]] HRESULT ParseAppSettingsJsonCandidate(std::string_view json,
                                                    std::unique_ptr<AppSettings>& settings) noexcept
{
    return ParseAppSettingsJsonV5(json, settings);
}

HRESULT ParseAppSettingsJson(std::string_view json, AppSettings& settings) noexcept
{
    std::unique_ptr<AppSettings> parsed;
    const HRESULT result = ParseAppSettingsJsonCandidate(json, parsed);
    if (SUCCEEDED(result))
    {
        settings = *parsed;
    }
    return result;
}

HRESULT ParseAppSettingsJsonDetailed(std::string_view json, AppSettings& settings,
                                     SettingsParseDiagnostic& diagnostic) noexcept
{
    std::unique_ptr<AppSettings> parsed;
    const HRESULT result = ParseAppSettingsJsonV5(json, parsed, &diagnostic);
    if (SUCCEEDED(result))
    {
        settings = *parsed;
    }
    return result;
}

HRESULT SerializeFactoryConfigurationJson(const PluginSettings& plugin, const WidgetInstanceSettings& instance,
                                          std::array<char, kFactoryConfigurationCapacity>& json,
                                          uint32_t& jsonBytes) noexcept
{
    json.fill('\0');
    jsonBytes = 0;
    if (plugin.privateConfiguration.bytes == 0 || instance.privateConfiguration.bytes == 0 ||
        plugin.privateConfiguration.bytes > kPrivateConfigurationCapacity ||
        instance.privateConfiguration.bytes > kPrivateConfigurationCapacity)
    {
        return E_INVALIDARG;
    }

    const int written =
        sprintf_s(json.data(), json.size(), "{\"plugin\":%.*s,\"instance\":%.*s}",
                  static_cast<int>(plugin.privateConfiguration.bytes), plugin.privateConfiguration.utf8.data(),
                  static_cast<int>(instance.privateConfiguration.bytes), instance.privateConfiguration.utf8.data());
    if (written <= 0 || static_cast<size_t>(written) >= json.size())
    {
        json.fill('\0');
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    jsonBytes = static_cast<uint32_t>(written);
    return S_OK;
}

HRESULT SerializeServiceConfigurationJson(const ServiceSettings& service,
                                          std::array<char, kFactoryConfigurationCapacity>& json,
                                          uint32_t& jsonBytes) noexcept
{
    json.fill('\0');
    jsonBytes = 0;
    if (service.privateConfiguration.bytes == 0 || service.privateConfiguration.bytes > kPrivateConfigurationCapacity)
    {
        return E_INVALIDARG;
    }
    const int written =
        sprintf_s(json.data(), json.size(), "{\"plugin\":{},\"instance\":%.*s}",
                  static_cast<int>(service.privateConfiguration.bytes), service.privateConfiguration.utf8.data());
    if (written <= 0 || static_cast<size_t>(written) >= json.size())
    {
        json.fill('\0');
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    jsonBytes = static_cast<uint32_t>(written);
    return S_OK;
}

const ServiceSettings* FindServiceSettings(const AppSettings& settings, std::string_view pluginId) noexcept
{
    for (uint32_t index = 0; index < settings.serviceCount && index < settings.services.size(); ++index)
    {
        if (SettingsIdEquals(settings.services[index].pluginId.View(), pluginId))
        {
            return &settings.services[index];
        }
    }
    return nullptr;
}

HRESULT PatchWidgetInstanceSettings(AppSettings& settings, std::string_view instanceId,
                                    std::string_view settingsJson) noexcept
{
    if (instanceId.empty() || settingsJson.empty() || settingsJson.size() > kPrivateConfigurationCapacity)
    {
        return E_INVALIDARG;
    }
    WidgetInstanceSettings* widget = FindWidgetInstance(settings, instanceId);
    uint32_t targetIndex = 0;
    if (!widget || !ParseWidgetInstanceIndex(instanceId, targetIndex))
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    unique_yyjson_doc document;
    try
    {
        std::vector<char> mutableJson(settingsJson.begin(), settingsJson.end());
        yyjson_read_err error{};
        document.reset(yyjson_read_opts(mutableJson.data(), mutableJson.size(), YYJSON_READ_NOFLAG, nullptr, &error));
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
    yyjson_val* patchRoot = document ? yyjson_doc_get_root(document.get()) : nullptr;
    if (!yyjson_is_obj(patchRoot))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    const JsonObjectSettings previousPrivate = widget->privateConfiguration;

    HRESULT result = MergeSettingsObject(previousPrivate, patchRoot, widget->privateConfiguration);
    // A merge that changes nothing (an unchanged collect, a repeated import) leaves the already valid typed settings
    // and the retained source as they are, so the document is not re-serialized and keeps its comments.
    if (SUCCEEDED(result) && widget->privateConfiguration.View() == previousPrivate.View())
    {
        widget->privateConfiguration = previousPrivate;
        return S_FALSE;
    }
    if (SUCCEEDED(result))
    {
        result = ValidateAppSettings(settings);
    }
    if (FAILED(result))
    {
        widget->privateConfiguration = previousPrivate;
        return result;
    }
    if (settings.sourceDocument.empty())
    {
        return S_OK;
    }

    unique_yyjson_doc merged = ParseStoredObject(widget->privateConfiguration);
    yyjson_val* mergedRoot = merged ? yyjson_doc_get_root(merged.get()) : nullptr;
    unique_yyjson_doc source;
    try
    {
        std::vector<char> mutableSource(settings.sourceDocument.begin(), settings.sourceDocument.end());
        yyjson_read_err error{};
        source.reset(yyjson_read_opts(
            mutableSource.data(), mutableSource.size(),
            YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_BOM, nullptr, &error));
    }
    catch (const std::bad_alloc&)
    {
        widget->privateConfiguration = previousPrivate;
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        widget->privateConfiguration = previousPrivate;
        return E_FAIL;
    }
    unique_mut_doc mutableDocument{yyjson_mut_doc_new(nullptr)};
    yyjson_mut_val* mutableRoot = mutableDocument && source
                                      ? yyjson_val_mut_copy(mutableDocument.get(), yyjson_doc_get_root(source.get()))
                                      : nullptr;
    yyjson_mut_val* settingsCopy =
        mutableDocument && mergedRoot ? yyjson_val_mut_copy(mutableDocument.get(), mergedRoot) : nullptr;
    if (!mutableRoot || !settingsCopy)
    {
        widget->privateConfiguration = previousPrivate;
        return E_OUTOFMEMORY;
    }
    yyjson_mut_doc_set_root(mutableDocument.get(), mutableRoot);
    yyjson_mut_val* pages = yyjson_mut_obj_get(mutableRoot, "pages");
    if (!yyjson_mut_is_arr(pages))
    {
        widget->privateConfiguration = previousPrivate;
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    uint32_t instanceIndex = 0;
    bool patched = false;
    yyjson_mut_val* declare = yyjson_mut_obj_get(mutableRoot, "declare");
    const size_t pageCount = yyjson_mut_arr_size(pages);
    for (size_t pageIndex = 0; pageIndex < pageCount && !patched; ++pageIndex)
    {
        yyjson_mut_val* page = yyjson_mut_arr_get(pages, pageIndex);
        patched = PatchMutablePage(mutableDocument.get(), page, instanceIndex, targetIndex, settingsCopy, declare);
    }
    if (!patched)
    {
        widget->privateConfiguration = previousPrivate;
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    size_t length = 0;
    unique_malloc_string written{yyjson_mut_write(mutableDocument.get(), YYJSON_WRITE_NOFLAG, &length)};
    if (!written || length == 0)
    {
        widget->privateConfiguration = previousPrivate;
        return E_OUTOFMEMORY;
    }
    // The rewritten document keeps a BOM the user's file starts with.
    result =
        FormatCompactSettingsJson(std::string_view(written.get(), length),
                                  settings.sourceDocument.starts_with(kUtf8ByteOrderMark), settings.sourceDocument);
    if (FAILED(result))
    {
        widget->privateConfiguration = previousPrivate;
        return result;
    }
    return S_OK;
}

namespace
{
struct SourceMember final
{
    size_t keyBegin = 0;
    size_t valueBegin = 0;
    size_t valueEnd = 0;
    size_t closingBrace = 0;
    // The object's last member, where a new member is appended.
    size_t lastKeyBegin = 0;
    size_t lastValueEnd = 0;
    bool found = false;
    bool hasMembers = false;
};

[[nodiscard]] bool SkipSourceTrivia(std::string_view source, size_t& position) noexcept
{
    for (;;)
    {
        while (position < source.size() && std::isspace(static_cast<unsigned char>(source[position])))
        {
            ++position;
        }
        if (position + 1 >= source.size() || source[position] != '/')
        {
            return true;
        }
        if (source[position + 1] == '/')
        {
            // A line comment ends at CR or LF, exactly where the parser ends it.
            position += 2;
            while (position < source.size() && source[position] != '\n' && source[position] != '\r')
            {
                ++position;
            }
        }
        else if (source[position + 1] == '*')
        {
            position += 2;
            const size_t end = source.find("*/", position);
            if (end == std::string_view::npos)
            {
                return false;
            }
            position = end + 2;
        }
        else
        {
            return true;
        }
    }
}

[[nodiscard]] bool SkipSourceString(std::string_view source, size_t& position) noexcept
{
    if (position >= source.size() || source[position++] != '"')
    {
        return false;
    }
    while (position < source.size())
    {
        const char character = source[position++];
        if (character == '"')
        {
            return true;
        }
        if (character == '\\' && position < source.size())
        {
            ++position;
        }
    }
    return false;
}

[[nodiscard]] bool SourceKeyEquals(std::string_view source, size_t begin, size_t end,
                                   std::string_view expected) noexcept
{
    if (end <= begin + 1 || expected.size() < 2 || source[begin] != '"' || expected.front() != '"')
    {
        return false;
    }
    size_t actual = begin + 1;
    size_t wanted = 1;
    while (actual + 1 < end)
    {
        char character = source[actual++];
        if (character == '\\')
        {
            if (actual + 1 >= end)
            {
                return false;
            }
            character = source[actual++];
            if (character == 'u')
            {
                if (actual + 4 >= end)
                {
                    return false;
                }
                uint32_t codepoint = 0;
                for (uint32_t digit = 0; digit < 4; ++digit)
                {
                    const char hex = source[actual++];
                    const uint32_t value = hex >= '0' && hex <= '9'   ? static_cast<uint32_t>(hex - '0')
                                           : hex >= 'a' && hex <= 'f' ? static_cast<uint32_t>(hex - 'a' + 10)
                                           : hex >= 'A' && hex <= 'F' ? static_cast<uint32_t>(hex - 'A' + 10)
                                                                      : 0x100U;
                    if (value > 15)
                    {
                        return false;
                    }
                    codepoint = (codepoint << 4U) | value;
                }
                if (codepoint > 0x7f)
                {
                    return false;
                }
                character = static_cast<char>(codepoint);
            }
            else if (character == 'b' || character == 'f' || character == 'n' || character == 'r' || character == 't')
            {
                // Target member names are printable ASCII; these escapes cannot match them.
                return false;
            }
        }
        if (wanted + 1 >= expected.size() || character != expected[wanted++])
        {
            return false;
        }
    }
    return wanted + 1 == expected.size();
}

[[nodiscard]] bool SkipSourceValue(std::string_view source, size_t& position) noexcept
{
    if (position >= source.size())
    {
        return false;
    }
    if (source[position] == '"')
    {
        return SkipSourceString(source, position);
    }
    if (source[position] == '{' || source[position] == '[')
    {
        uint32_t depth = 0;
        do
        {
            if (!SkipSourceTrivia(source, position) || position >= source.size())
            {
                return false;
            }
            if (source[position] == '"')
            {
                if (!SkipSourceString(source, position))
                {
                    return false;
                }
                continue;
            }
            const char character = source[position++];
            if (character == '{' || character == '[')
            {
                ++depth;
            }
            else if (character == '}' || character == ']')
            {
                --depth;
            }
        } while (depth != 0);
        return true;
    }
    const size_t begin = position;
    while (position < source.size() && std::strchr(",}]", source[position]) == nullptr &&
           !std::isspace(static_cast<unsigned char>(source[position])) && source[position] != '/')
    {
        ++position;
    }
    return position > begin;
}

[[nodiscard]] bool FindSourceMember(std::string_view source, size_t objectBegin, std::string_view key,
                                    SourceMember& found) noexcept
{
    found = SourceMember{};
    if (objectBegin >= source.size() || source[objectBegin] != '{')
    {
        return false;
    }
    size_t position = objectBegin + 1;
    for (;;)
    {
        if (!SkipSourceTrivia(source, position) || position >= source.size())
        {
            return false;
        }
        if (source[position] == '}')
        {
            found.closingBrace = position;
            return true;
        }
        const size_t keyBegin = position;
        if (!SkipSourceString(source, position))
        {
            return false;
        }
        const bool matching = SourceKeyEquals(source, keyBegin, position, key);
        if (!SkipSourceTrivia(source, position) || position >= source.size() || source[position++] != ':' ||
            !SkipSourceTrivia(source, position))
        {
            return false;
        }
        const size_t valueBegin = position;
        if (!SkipSourceValue(source, position))
        {
            return false;
        }
        found.hasMembers = true;
        found.lastKeyBegin = keyBegin;
        found.lastValueEnd = position;
        if (matching)
        {
            found.found = true;
            found.keyBegin = keyBegin;
            found.valueBegin = valueBegin;
            found.valueEnd = position;
        }
        if (!SkipSourceTrivia(source, position) || position >= source.size())
        {
            return false;
        }
        if (source[position] == ',')
        {
            ++position;
        }
        else if (source[position] != '}')
        {
            return false;
        }
    }
}

// The document's own line break: CRLF when it has one, CR in a document that breaks lines only with CR, otherwise LF.
[[nodiscard]] std::string_view SourceLineBreak(std::string_view source) noexcept
{
    if (source.find("\r\n") != std::string_view::npos)
    {
        return "\r\n";
    }
    return source.find('\r') != std::string_view::npos && source.find('\n') == std::string_view::npos ? "\r" : "\n";
}

// The start of the line holding `position`; a line ends at CR or LF.
[[nodiscard]] size_t SourceLineBegin(std::string_view source, size_t position) noexcept
{
    const size_t lineBreak = source.find_last_of("\r\n", position);
    return lineBreak == std::string_view::npos ? 0 : lineBreak + 1;
}

// Replaces the value of `key`, or appends `"key": replacement` in the object's own layout: right after the last
// member's value (its trailing comma or comment then follows the new member), on a new line at that member's
// indentation when the member starts its own line, otherwise on the same line. In an empty object whose closing brace
// starts its own line, the member gets a line of its own one level deeper than the brace. Throws only std::bad_alloc.
[[nodiscard]] bool PatchSourceMember(std::string& source, size_t objectBegin, std::string_view key,
                                     std::string_view replacement)
{
    SourceMember member{};
    if (!FindSourceMember(source, objectBegin, key, member))
    {
        return false;
    }
    if (member.found)
    {
        source.replace(member.valueBegin, member.valueEnd - member.valueBegin, replacement);
        return true;
    }
    std::string addition;
    size_t insertAt = member.closingBrace;
    const size_t anchor = member.hasMembers ? member.lastKeyBegin : member.closingBrace;
    const size_t lineBegin = SourceLineBegin(source, anchor);
    const bool ownLine = lineBegin > objectBegin + 1 && source.find_first_not_of(" \t", lineBegin) == anchor;
    if (member.hasMembers)
    {
        insertAt = member.lastValueEnd;
        addition.push_back(',');
        if (ownLine)
        {
            addition.append(SourceLineBreak(source)).append(source, lineBegin, anchor - lineBegin);
        }
        else
        {
            addition.push_back(' ');
        }
        addition.append(key).append(": ").append(replacement);
    }
    else if (ownLine)
    {
        insertAt = lineBegin;
        const bool tabs = source.find('\t', lineBegin) < anchor;
        addition.append(source, lineBegin, anchor - lineBegin).append(tabs ? "\t" : "  ");
        addition.append(key).append(": ").append(replacement).append(SourceLineBreak(source));
    }
    else
    {
        if (!std::isspace(static_cast<unsigned char>(source[insertAt - 1])))
        {
            addition.push_back(' ');
        }
        addition.append(key).append(": ").append(replacement).push_back(' ');
    }
    source.insert(insertAt, addition);
    return true;
}

// Preconditions shared by the dock source patches: the whole source is a valid v5 document, parsed into `validated`.
// `raiseMinor` reports a minor below `requiredMinor` (2 added `dock`, 3 the `secondary` monitor selector);
// `rootBegin` is the root's '{', after a leading UTF-8 BOM, which the patch leaves in place.
[[nodiscard]] HRESULT PrepareDockSourcePatch(std::string& source, uint32_t requiredMinor, bool& raiseMinor,
                                             size_t& rootBegin, std::unique_ptr<AppSettings>& validated) noexcept
{
    raiseMinor = false;
    rootBegin = 0;
    if (const HRESULT result = ParseAppSettingsJsonV5(source, validated); FAILED(result))
    {
        return result;
    }
    yyjson_read_err error{};
    unique_yyjson_doc document{yyjson_read_opts(
        source.data(), source.size(),
        YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_BOM, nullptr, &error)};
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    if (!yyjson_is_obj(root))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    raiseMinor = validated->versionMinor < requiredMinor;
    rootBegin = source.starts_with(kUtf8ByteOrderMark) ? kUtf8ByteOrderMark.size() : 0;
    return SkipSourceTrivia(source, rootBegin) ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

// Throws only std::bad_alloc.
[[nodiscard]] bool RaiseDockSourceMinor(std::string& source, size_t rootBegin, uint32_t minor)
{
    std::array<char, 16> digits{};
    const int length = std::snprintf(digits.data(), digits.size(), "%u", minor);
    SourceMember version{};
    return length > 0 && FindSourceMember(source, rootBegin, "\"version\"", version) && version.found &&
           PatchSourceMember(source, version.valueBegin, "\"minor\"",
                             std::string_view(digits.data(), static_cast<size_t>(length)));
}

// The end of the line holding `position` when only spaces and comments follow it there, so a comment that ends the
// line stays on it; otherwise `position` itself.
[[nodiscard]] size_t SourceTrailingTriviaEnd(std::string_view source, size_t position) noexcept
{
    size_t scan = position;
    for (;;)
    {
        while (scan < source.size() && (source[scan] == ' ' || source[scan] == '\t'))
        {
            ++scan;
        }
        if (scan >= source.size() || source[scan] == '\r' || source[scan] == '\n')
        {
            return scan;
        }
        if (source.substr(scan, 2) == "//")
        {
            return std::min(source.find_first_of("\r\n", scan), source.size());
        }
        const size_t commentEnd = source.substr(scan, 2) == "/*" ? source.find("*/", scan + 2) : std::string_view::npos;
        if (commentEnd == std::string_view::npos || source.find_first_of("\r\n", scan) < commentEnd)
        {
            return position;
        }
        scan = commentEnd + 2;
    }
}

// A new root member (`"key": value`) on its own line right after `version`, at that member's indentation and in the
// file's own line breaks, below the given whole comment lines. A comment that ends version's line stays there.
// Throws only std::bad_alloc.
[[nodiscard]] bool InsertRootMemberAfterVersion(std::string& source, size_t rootBegin, std::string_view member,
                                                std::initializer_list<std::string_view> comments)
{
    SourceMember version{};
    if (!FindSourceMember(source, rootBegin, "\"version\"", version) || !version.found)
    {
        return false;
    }
    const size_t lineBegin = SourceLineBegin(source, version.keyBegin);
    std::string indentation = source.substr(lineBegin, version.keyBegin - lineBegin);
    if (indentation.find_first_not_of(" \t") != std::string::npos)
    {
        indentation = "  ";
    }
    const std::string_view lineBreak = SourceLineBreak(source);
    std::string lines;
    for (const std::string_view comment : comments)
    {
        lines.append(lineBreak).append(indentation).append(comment);
    }
    lines.append(lineBreak).append(indentation).append(member);
    size_t after = version.valueEnd;
    if (!SkipSourceTrivia(source, after) || after >= source.size())
    {
        return false;
    }
    if (source[after] == ',')
    {
        source.insert(SourceTrailingTriviaEnd(source, after + 1), lines + ",");
    }
    else
    {
        source.insert(SourceTrailingTriviaEnd(source, version.valueEnd), lines);
        source.insert(version.valueEnd, ",");
    }
    return true;
}

// Removes every commented-out example of the root member `key`: a `//` comment alone on its line whose text starts with
// `key` and a colon (the templates' `// "dock": { ... },`), together with the comment lines right above it that
// introduce it, each line with its line break. Only comments between root members count; a member or a blank line
// ends an introduction. Throws only std::bad_alloc.
[[nodiscard]] bool RemoveCommentedRootExamples(std::string& source, std::string_view key)
{
    size_t depth = 0;
    // The start of the line that begins the current run of root comment lines, and whether the line being read is one.
    size_t runBegin = std::string::npos;
    bool commentLine = false;
    size_t position = 0;
    while (position < source.size())
    {
        const char character = source[position];
        if (character == '"')
        {
            if (!SkipSourceString(source, position))
            {
                return false;
            }
            runBegin = std::string::npos;
            continue;
        }
        if (character == '/' && source.compare(position, 2, "/*") == 0)
        {
            const size_t end = source.find("*/", position + 2);
            if (end == std::string::npos)
            {
                return false;
            }
            position = end + 2;
            runBegin = std::string::npos;
            continue;
        }
        if (character == '/' && source.compare(position, 2, "//") == 0)
        {
            // A line comment ends at CR or LF, exactly where the parser ends it.
            const size_t lineBegin = SourceLineBegin(source, position);
            const size_t lineEnd = std::min(source.find_first_of("\r\n", position), source.size());
            if (depth != 1 || source.find_first_not_of(" \t", lineBegin) != position)
            {
                runBegin = std::string::npos;
                position = lineEnd;
                continue;
            }
            runBegin = runBegin == std::string::npos ? lineBegin : runBegin;
            commentLine = true;
            size_t text = source.find_first_not_of(" \t", position + 2);
            if (text < lineEnd && source.compare(text, key.size(), key) == 0)
            {
                text = source.find_first_not_of(" \t", text + key.size());
                if (text < lineEnd && source[text] == ':')
                {
                    size_t next = lineEnd;
                    next += next < source.size() && source[next] == '\r' ? 1U : 0U;
                    next += next < source.size() && source[next] == '\n' ? 1U : 0U;
                    source.erase(runBegin, next - runBegin);
                    position = runBegin;
                    runBegin = std::string::npos;
                    commentLine = false;
                    continue;
                }
            }
            position = lineEnd;
            continue;
        }
        if (character == '\r' || character == '\n')
        {
            // CRLF is one line break.
            if (character == '\r' || position == 0 || source[position - 1] != '\r')
            {
                runBegin = commentLine ? runBegin : std::string::npos;
                commentLine = false;
            }
        }
        else if (!std::isspace(static_cast<unsigned char>(character)))
        {
            runBegin = std::string::npos;
            if (character == '{' || character == '[')
            {
                ++depth;
            }
            else if ((character == '}' || character == ']') && depth != 0)
            {
                --depth;
            }
        }
        ++position;
    }
    return true;
}
} // namespace

HRESULT PatchDockThickness(AppSettings& settings, uint32_t thicknessDips) noexcept
{
    if (thicknessDips < kDockMinimumThicknessDips || thicknessDips > kDockMaximumThicknessDips)
    {
        return E_INVALIDARG;
    }
    // A release at the thickness the document already has (a click on the edge) changes nothing: no `dock` or minor
    // is added and nothing is written.
    if (settings.dock.thicknessDips == thicknessDips)
    {
        return S_FALSE;
    }
    if (settings.sourceDocument.empty())
    {
        settings.dock.thicknessDips = thicknessDips;
        return S_OK;
    }
    try
    {
        std::string updated = settings.sourceDocument;
        bool raiseMinor = false;
        size_t rootBegin = 0;
        std::unique_ptr<AppSettings> original;
        if (const HRESULT prepared =
                PrepareDockSourcePatch(updated, kRedXeSettingsDockMinor, raiseMinor, rootBegin, original);
            FAILED(prepared))
        {
            return prepared;
        }
        SourceMember dock{};
        if (!FindSourceMember(updated, rootBegin, "\"dock\"", dock))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        // An existing dock gains or replaces `thickness`; a new one goes where the first-run dock goes, on its own line
        // after `version`.
        const std::string thickness = std::to_string(thicknessDips);
        if (dock.found)
        {
            if (!PatchSourceMember(updated, dock.valueBegin, "\"thickness\"", thickness))
            {
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }
        else if (!InsertRootMemberAfterVersion(updated, rootBegin, "\"dock\": { \"thickness\": " + thickness + " }",
                                               {}))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (raiseMinor && !RaiseDockSourceMinor(updated, rootBegin, kRedXeSettingsDockMinor))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (updated.size() > kMaximumSettingsBytes)
        {
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        }
        // The patched text is what gets written, so it must parse back to the running dock with the new thickness, and
        // to the document it was made from with only that thickness and the raised minor changed. A scanner that read
        // the document differently from the parser would otherwise persist a file the next start rejects and resets,
        // or one that changes another member.
        DockSettings expected = settings.dock;
        expected.thicknessDips = thicknessDips;
        std::unique_ptr<AppSettings> patched;
        if (const HRESULT validated = ParseAppSettingsJsonV5(updated, patched); FAILED(validated))
        {
            return validated;
        }
        if (!patched || !original)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        original->dock.thicknessDips = thicknessDips;
        if (raiseMinor)
        {
            original->versionMinor = kRedXeSettingsDockMinor;
        }
        original->sourceDocument.clear();
        patched->sourceDocument.clear();
        if (patched->dock != expected || *patched != *original)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        settings.sourceDocument = std::move(updated);
        settings.dock.thicknessDips = thicknessDips;
        // A raised source minor is the typed minor too, so the next comment-only reload still matches the runtime.
        settings.versionMinor = patched->versionMinor;
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

HRESULT PatchFirstRunDock(std::string& source, const DockSettings& dock) noexcept
{
    // Only a dock the parser would accept back: one of the four edges, a known mode, a monitor selector of the shared
    // grammar (never `all`), and every number in its settings range.
    const bool knownEdge = dock.edge == DockEdge::Top || dock.edge == DockEdge::Bottom || dock.edge == DockEdge::Left ||
                           dock.edge == DockEdge::Right;
    const bool knownMode = dock.mode == DockMode::Fixed || dock.mode == DockMode::Autohide;
    RedXeActions::MonitorSelector selector{};
    if (!knownEdge || !knownMode || dock.monitor.bytes == 0 || dock.monitor.bytes > kMaximumSettingsTextBytes ||
        !RedXeActions::ParseMonitorSelector(dock.monitor.View(), false, selector) ||
        dock.thicknessDips < kDockMinimumThicknessDips || dock.thicknessDips > kDockMaximumThicknessDips ||
        dock.peekPixels < kDockMinimumPeekPixels || dock.peekPixels > kDockMaximumPeekPixels ||
        dock.revealDelayMilliseconds > kDockMaximumRevealDelayMilliseconds ||
        dock.hideDelayMilliseconds > kDockMaximumHideDelayMilliseconds ||
        dock.animationMilliseconds > kDockMaximumAnimationMilliseconds)
    {
        return E_INVALIDARG;
    }
    try
    {
        // Edge, monitor, mode, and thickness always; the other members only where they leave their defaults.
        std::string member = "{ \"edge\": \"";
        member += DockEdgeName(dock.edge);
        member += "\", \"monitor\": \"";
        for (const char character : dock.monitor.View())
        {
            if (static_cast<unsigned char>(character) < 0x20)
            {
                return E_INVALIDARG;
            }
            if (character == '"' || character == '\\')
            {
                member.push_back('\\');
            }
            member.push_back(character);
        }
        member += "\", \"mode\": \"";
        member += dock.mode == DockMode::Autohide ? "autohide" : "fixed";
        member += "\", \"thickness\": " + std::to_string(dock.thicknessDips);
        if (!dock.reserveWorkArea)
        {
            member += ", \"reserveWorkArea\": false";
        }
        if (dock.peekPixels != kDockDefaultPeekPixels)
        {
            member += ", \"peek\": " + std::to_string(dock.peekPixels);
        }
        if (dock.revealDelayMilliseconds != kDockDefaultRevealDelayMilliseconds)
        {
            member += ", \"revealDelayMilliseconds\": " + std::to_string(dock.revealDelayMilliseconds);
        }
        if (dock.hideDelayMilliseconds != kDockDefaultHideDelayMilliseconds)
        {
            member += ", \"hideDelayMilliseconds\": " + std::to_string(dock.hideDelayMilliseconds);
        }
        if (dock.animationMilliseconds != kDockDefaultAnimationMilliseconds)
        {
            member += ", \"animationMilliseconds\": " + std::to_string(dock.animationMilliseconds);
        }
        member += " }";

        // The minor that added `dock`, or a later one for a member value the dock uses.
        uint32_t requiredMinor = kRedXeSettingsDockMinor;
        if (selector.kind == RedXeActions::MonitorSelector::Kind::Secondary)
        {
            requiredMinor = std::max(requiredMinor, kRedXeSettingsSecondaryMonitorMinor);
        }
        if (dock.animationMilliseconds != kDockDefaultAnimationMilliseconds)
        {
            requiredMinor = std::max(requiredMinor, kRedXeSettingsDockAnimationMinor);
        }
        std::string updated = source;
        bool raiseMinor = false;
        size_t rootBegin = 0;
        std::unique_ptr<AppSettings> validated;
        if (const HRESULT prepared = PrepareDockSourcePatch(updated, requiredMinor, raiseMinor, rootBegin, validated);
            FAILED(prepared))
        {
            return prepared;
        }
        // The template's commented-out `dock` example goes with the comment that says to uncomment it: the file then
        // defines the bar once, and following its comments cannot add a second, duplicate `dock` member.
        SourceMember existing{};
        if (!RemoveCommentedRootExamples(updated, "\"dock\"") ||
            !FindSourceMember(updated, rootBegin, "\"dock\"", existing))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        // An existing value is replaced. A new member goes on its own line after `version`, with the reason above it
        // so the person who opens the file knows where the bar came from, how to turn it off, and where the rest of
        // its members are described.
        if (existing.found)
        {
            updated.replace(existing.valueBegin, existing.valueEnd - existing.valueBegin, member);
        }
        else if (!InsertRootMemberAfterVersion(
                     updated, rootBegin, "\"dock\": " + member,
                     {"// No XENEON display was found when RedXe installed this file, so this dock runs it as a bar on "
                      "a screen edge;",
                      "// set \"edge\" to \"none\" to use the standard window instead. See docs/usage.md \"Dock\" for "
                      "the other members."}))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (raiseMinor && !RaiseDockSourceMinor(updated, rootBegin, requiredMinor))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (updated.size() > kMaximumSettingsBytes)
        {
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        }
        source = std::move(updated);
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

HRESULT LoadAppSettingsFile(std::wstring_view path, AppSettings& settings) noexcept
{
    std::vector<char> bytes;
    const HRESULT result = ReadFileBytes(path, bytes);
    return SUCCEEDED(result) ? ParseAppSettingsJson(std::string_view(bytes.data(), bytes.size()), settings) : result;
}

[[nodiscard]] HRESULT LoadAppSettingsFileCandidate(std::wstring_view path, std::unique_ptr<AppSettings>& settings,
                                                   SettingsParseDiagnostic* diagnostic = nullptr) noexcept
{
    std::vector<char> bytes;
    const HRESULT result = ReadFileBytes(path, bytes);
    return SUCCEEDED(result)
               ? (diagnostic
                      ? ParseAppSettingsJsonV5(std::string_view(bytes.data(), bytes.size()), settings, diagnostic)
                      : ParseAppSettingsJsonCandidate(std::string_view(bytes.data(), bytes.size()), settings))
               : result;
}

[[nodiscard]] std::wstring FormatDiagnostic(const SettingsParseDiagnostic& diagnostic) noexcept
{
    try
    {
        std::wstring message;
        const auto appendUtf8 = [&message](std::string_view text)
        {
            if (text.empty())
                return;
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                                  static_cast<int>(text.size()), nullptr, 0);
            if (count <= 0)
                return;
            const size_t offset = message.size();
            message.resize(offset + static_cast<size_t>(count));
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                message.data() + offset, count);
        };
        if (diagnostic.hasLocation)
        {
            message += L"Line " + std::to_wstring(diagnostic.line) + L", column " + std::to_wstring(diagnostic.column) +
                       L"\r\n";
        }
        message += L"Path: ";
        appendUtf8(diagnostic.path.empty() ? "$" : diagnostic.path);
        message += L"\r\n\r\n";
        if (diagnostic.message.empty())
        {
            message += L"The settings file is not valid version 5.";
        }
        else
        {
            appendUtf8(diagnostic.message);
        }
        return message;
    }
    catch (...)
    {
        return L"The settings file is invalid.";
    }
}

HRESULT QuerySettingsFileStamp(std::wstring_view path, SettingsFileStamp& stamp) noexcept
{
    try
    {
        const std::wstring pathText(path);
        wil::unique_hfile file{CreateFileW(pathText.c_str(), FILE_READ_ATTRIBUTES,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (!file)
        {
            const DWORD error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? S_FALSE : HRESULT_FROM_WIN32(error);
        }

        BY_HANDLE_FILE_INFORMATION information{};
        if (!GetFileInformationByHandle(file.get(), &information))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        SettingsFileStamp queried{};
        queried.volumeSerialNumber = information.dwVolumeSerialNumber;
        queried.fileIndexHigh = information.nFileIndexHigh;
        queried.fileIndexLow = information.nFileIndexLow;
        queried.lastWriteTime = (static_cast<uint64_t>(information.ftLastWriteTime.dwHighDateTime) << 32U) |
                                information.ftLastWriteTime.dwLowDateTime;
        queried.fileSize = (static_cast<uint64_t>(information.nFileSizeHigh) << 32U) | information.nFileSizeLow;
        stamp = queried;
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

namespace
{
// The selected template with the first-run dock patched in (Core_Settings.md "Cold load and recovery"), validated and
// then written with the same atomic same-directory write as a plain install. Only a missing target is installed: an
// existing one wins (S_FALSE), exactly as InstallIfMissing.
[[nodiscard]] HRESULT InstallTemplateWithDock(const std::filesystem::path& source, const std::filesystem::path& target,
                                              const DockSettings& dock) noexcept
{
    const DWORD attributes = GetFileAttributesW(target.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES)
    {
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ? S_FALSE : HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    }
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
    {
        return HRESULT_FROM_WIN32(error);
    }
    try
    {
        std::vector<char> bytes;
        HRESULT result = ReadFileBytes(source.wstring(), bytes);
        if (FAILED(result))
        {
            return result;
        }
        std::string text(bytes.begin(), bytes.end());
        result = PatchFirstRunDock(text, dock);
        std::unique_ptr<AppSettings> validated;
        if (SUCCEEDED(result))
        {
            result = ParseAppSettingsJsonCandidate(text, validated);
        }
        if (SUCCEEDED(result) &&
            (!validated || validated->dock.edge != dock.edge || validated->dock.mode != dock.mode ||
             validated->dock.thicknessDips != dock.thicknessDips ||
             validated->dock.monitor.View() != dock.monitor.View()))
        {
            result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (FAILED(result))
        {
            return result;
        }
        return WriteUtf8FileAtomically(target, text, false);
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}
} // namespace

HRESULT SettingsStore::Initialize(bool selfTest, std::wstring_view selectedPath, std::unique_ptr<AppSettings>& settings,
                                  std::wstring_view localAppDataOverride, const DockSettings* firstRunDock) noexcept
{
    settings.reset();
    _usedInitialFallback = false;
    _installedFirstRunDock = false;
    _selfTest = selfTest;
    _suppressDocumentWrites = false;
    _initialNotice.clear();
    // Only a document this call loads from the selected path may be written over later (PersistPatchedDocument).
    _lastAppliedStamp.reset();
    _lastRejectedStamp.reset();
    _deferredStamp.reset();
    _deferredNoticePending = false;
    try
    {
        std::filesystem::path moduleDirectory;
        HRESULT result = GetModuleDirectory(moduleDirectory);
        if (FAILED(result))
        {
            return result;
        }

        const std::filesystem::path deployedSettings = moduleDirectory / L"Settings";
        const std::filesystem::path selectedTemplate = deployedSettings / kSelectedSettingsFileName;
        const std::filesystem::path deployedSchema = deployedSettings / kRedXeSettingsSchemaFileName;
        if (selfTest)
        {
            _settingsPath = selectedTemplate.wstring();
            _settingsDirectory = deployedSettings.wstring();
            _logsDirectory = MakeLogsDirectory(_settingsDirectory);
            _schemaPath = deployedSchema.wstring();
            result = LoadAppSettingsFileCandidate(_settingsPath, settings);
            if (FAILED(result))
            {
                return result;
            }
            SettingsFileStamp stamp{};
            if (QuerySettingsFileStamp(_settingsPath, stamp) == S_OK)
            {
                _lastAppliedStamp = stamp;
            }
            return S_OK;
        }

        if (!selectedPath.empty())
        {
            const std::filesystem::path externalPath = std::filesystem::absolute(std::filesystem::path(selectedPath));
            _settingsPath = externalPath.wstring();
            _settingsDirectory = externalPath.parent_path().wstring();
            _logsDirectory = MakeLogsDirectory(_settingsDirectory);
            _schemaPath = deployedSchema.wstring();

            result = LoadAppSettingsFileCandidate(_settingsPath, settings);
            if (FAILED(result))
            {
                _usedInitialFallback = true;
                _initialNotice = L"The selected settings file could not be loaded. RedXe is running with its "
                                 L"default configuration; the selected file was not changed.";
                result = LoadAppSettingsFileCandidate(selectedTemplate.wstring(), settings);
            }
            if (FAILED(result))
            {
                return result;
            }
            SettingsFileStamp stamp{};
            if (QuerySettingsFileStamp(_settingsPath, stamp) == S_OK && !_usedInitialFallback)
            {
                _lastAppliedStamp = stamp;
            }
            return S_OK;
        }

        wil::unique_cotaskmem_string localAppData;
        if (localAppDataOverride.empty())
        {
            result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, localAppData.put());
            if (FAILED(result))
            {
                return result;
            }
        }

        const std::filesystem::path settingsDirectory =
            std::filesystem::path(localAppDataOverride.empty() ? localAppData.get() : localAppDataOverride) / L"RedXe" /
            L"Settings";
        result = EnsureDirectory(settingsDirectory);
        if (FAILED(result))
        {
            return result;
        }

        const std::filesystem::path settingsPath = settingsDirectory / kSelectedSettingsFileName;
        const std::filesystem::path schemaPath = settingsDirectory / kRedXeSettingsSchemaFileName;
        _settingsPath = settingsPath.wstring();
        _settingsDirectory = settingsDirectory.wstring();
        _logsDirectory = MakeLogsDirectory(_settingsDirectory);
        _schemaPath = schemaPath.wstring();

        result = CopyFileAtomically(deployedSchema, schemaPath, true);
        if (FAILED(result))
        {
            return result;
        }

        std::filesystem::path initialSource = selectedTemplate;
#if !defined(_DEBUG)
        result = MigrateLegacyReleaseSettingsName(settingsDirectory, settingsPath);
        if (FAILED(result))
        {
            return result;
        }
#endif
        // Without a XENEON the installed default is the template plus the first-run dock, so RedXe starts as a bar
        // on the primary display instead of asking about the missing display. The dock is a convenience: a template
        // it cannot patch still installs plainly.
        result = firstRunDock ? InstallTemplateWithDock(initialSource, settingsPath, *firstRunDock)
                              : InstallIfMissing(initialSource, settingsPath);
        _installedFirstRunDock = firstRunDock && result == S_OK;
        if (FAILED(result) && firstRunDock)
        {
            result = InstallIfMissing(initialSource, settingsPath);
        }
        if (FAILED(result))
        {
            return result;
        }

        result = LoadAppSettingsFileCandidate(_settingsPath, settings);
        if (result == HRESULT_FROM_WIN32(ERROR_INVALID_DATA) || result == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE))
        {
            std::filesystem::path backupPath;
            const HRESULT backupResult = BackupInvalidSettings(settingsPath, backupPath);
            if (FAILED(backupResult))
            {
                return backupResult;
            }
            // Recovery installs the plain template even without a XENEON: only a missing file gets the first-run dock,
            // so a file that failed validation never turns the user's XENEON or window configuration into a bar.
            _installedFirstRunDock = false;
            result = InstallTemplateFile(selectedTemplate, settingsPath, true);
            if (SUCCEEDED(result))
            {
                result = LoadAppSettingsFileCandidate(_settingsPath, settings);
            }
            if (SUCCEEDED(result))
            {
                _usedInitialFallback = true;
                _initialNotice = L"The default settings file was incompatible or invalid. It was preserved as:\n" +
                                 backupPath.wstring() + L"\n\nA fresh default configuration was installed.";
            }
        }
        if (FAILED(result))
        {
            return result;
        }

        SettingsFileStamp stamp{};
        result = QuerySettingsFileStamp(_settingsPath, stamp);
        if (result != S_OK)
        {
            return result == S_FALSE ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : result;
        }
        _lastAppliedStamp = stamp;
        _lastRejectedStamp.reset();
        _missingObserved = false;
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

HRESULT SettingsStore::TryLoadChanged(std::unique_ptr<AppSettings>& settings, SettingsFileStamp& stamp,
                                      SettingsReloadStatus& status) noexcept
{
    settings.reset();
    status = SettingsReloadStatus::Unchanged;
    SettingsFileStamp currentStamp{};
    const HRESULT stampResult = QuerySettingsFileStamp(_settingsPath, currentStamp);
    if (stampResult == S_FALSE)
    {
        if (!_missingObserved)
        {
            _missingObserved = true;
            status = SettingsReloadStatus::Missing;
        }
        return S_OK;
    }
    if (FAILED(stampResult))
    {
        return stampResult;
    }
    _missingObserved = false;

    if ((_lastAppliedStamp && *_lastAppliedStamp == currentStamp) ||
        (_lastRejectedStamp && *_lastRejectedStamp == currentStamp))
    {
        return S_OK;
    }

    std::unique_ptr<AppSettings> candidate;
    SettingsParseDiagnostic diagnostic;
    const HRESULT loadResult = LoadAppSettingsFileCandidate(_settingsPath, candidate, &diagnostic);
    if (loadResult == HRESULT_FROM_WIN32(ERROR_INVALID_DATA) || loadResult == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE))
    {
        _lastRejectedStamp = currentStamp;
        _lastDiagnosticText = FormatDiagnostic(diagnostic);
        stamp = currentStamp;
        status = SettingsReloadStatus::Invalid;
        return S_OK;
    }
    if (FAILED(loadResult))
    {
        return loadResult;
    }

    settings = std::move(candidate);
    _lastDiagnosticText.clear();
    stamp = currentStamp;
    status = SettingsReloadStatus::Loaded;
    return S_OK;
}

void SettingsStore::MarkApplied(const SettingsFileStamp& stamp) noexcept
{
    _lastAppliedStamp = stamp;
    _lastRejectedStamp.reset();
    _deferredStamp.reset();
}

void SettingsStore::MarkRejected(const SettingsFileStamp& stamp) noexcept
{
    _lastRejectedStamp = stamp;
}

void SettingsStore::ForgetStamps() noexcept
{
    _lastAppliedStamp.reset();
    _lastRejectedStamp.reset();
}

void SettingsStore::SuppressDocumentWrites(bool suppress) noexcept
{
    _suppressDocumentWrites = suppress;
}

const std::wstring& SettingsStore::SettingsPath() const noexcept
{
    return _settingsPath;
}

const std::wstring& SettingsStore::SettingsDirectory() const noexcept
{
    return _settingsDirectory;
}

const std::wstring& SettingsStore::LogsDirectory() const noexcept
{
    return _logsDirectory;
}

const std::wstring& SettingsStore::SchemaPath() const noexcept
{
    return _schemaPath;
}

bool SettingsStore::UsedInitialFallback() const noexcept
{
    return _usedInitialFallback;
}

bool SettingsStore::InstalledFirstRunDock() const noexcept
{
    return _installedFirstRunDock;
}

const std::wstring& SettingsStore::InitialNotice() const noexcept
{
    return _initialNotice;
}

const std::wstring& SettingsStore::LastDiagnosticText() const noexcept
{
    return _lastDiagnosticText;
}

HRESULT SettingsStore::PersistPatchedDocument(const AppSettings& settings) noexcept
{
    if (_selfTest || _suppressDocumentWrites)
    {
        return S_OK;
    }
    if (_settingsPath.empty() || settings.sourceDocument.empty())
    {
        return E_UNEXPECTED;
    }
    // Write only over the document last applied. Any other file on disk (rejected, replaced by a save the watcher has
    // not processed, deleted, unreadable, or a `--settings` file that fell back to the template) belongs to the user:
    // the patch stays in memory until the next applied load replaces it. A missing or unreadable file keeps the zero
    // stamp, so each distinct on-disk state is reported once.
    SettingsFileStamp current{};
    if (QuerySettingsFileStamp(_settingsPath, current) != S_OK || !_lastAppliedStamp || *_lastAppliedStamp != current)
    {
        if (!_deferredStamp || *_deferredStamp != current)
        {
            _deferredStamp = current;
            _deferredNoticePending = true;
        }
        return S_FALSE;
    }
    const HRESULT result = WriteUtf8FileAtomically(_settingsPath, settings.sourceDocument);
    if (FAILED(result))
    {
        return result;
    }
    _deferredStamp.reset();
    SettingsFileStamp stamp{};
    const HRESULT stampResult = QuerySettingsFileStamp(_settingsPath, stamp);
    if (stampResult != S_OK)
    {
        // The atomic replacement already committed. Stamp bookkeeping must not turn that successful write into
        // a reported failure and make a widget roll back its state. Let the watcher read the next notification.
        _lastAppliedStamp.reset();
        return S_OK;
    }
    _lastAppliedStamp = stamp;
    return S_OK;
}

HRESULT SettingsStore::PersistDockThickness(AppSettings& settings, uint32_t thicknessDips) noexcept
{
    const uint32_t previousThickness = settings.dock.thicknessDips;
    const uint32_t previousMinor = settings.versionMinor;
    try
    {
        std::string previousSource = settings.sourceDocument;
        // S_FALSE from the patch: the thickness is unchanged, so nothing is written, unless an earlier deferred write
        // still holds changes in memory.
        HRESULT result = PatchDockThickness(settings, thicknessDips);
        if ((result == S_OK || (result == S_FALSE && _deferredStamp)) && !_selfTest && !_suppressDocumentWrites)
        {
            result = PersistPatchedDocument(settings);
        }
        if (FAILED(result))
        {
            settings.dock.thicknessDips = previousThickness;
            settings.versionMinor = previousMinor;
            settings.sourceDocument = std::move(previousSource);
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

HRESULT SettingsStore::PersistWidgetSettings(AppSettings& settings, std::string_view instanceId,
                                             std::string_view settingsJson) noexcept
{
    WidgetInstanceSettings* widget = FindWidgetInstance(settings, instanceId);
    if (!widget)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    const JsonObjectSettings previousPrivate = widget->privateConfiguration;
    try
    {
        std::string previousSource = settings.sourceDocument;
        // S_FALSE from the patch: nothing changed, so the file is neither rewritten nor reformatted, unless an earlier
        // deferred write still holds changes in memory.
        HRESULT result = PatchWidgetInstanceSettings(settings, instanceId, settingsJson);
        if ((result == S_OK || (result == S_FALSE && _deferredStamp)) && !_selfTest && !_suppressDocumentWrites)
        {
            result = PersistPatchedDocument(settings);
        }
        if (FAILED(result))
        {
            widget->privateConfiguration = previousPrivate;
            settings.sourceDocument = std::move(previousSource);
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

bool SettingsStore::TakeDeferredPersistNotice() noexcept
{
    return std::exchange(_deferredNoticePending, false);
}
