#include "Settings.h"

#include "../Plugins/AVControl/AVControlModel.h"
#include "../Plugins/AVControl/AVControlSettings.h"
#include "../Plugins/Launcher/LauncherPaging.h"
#include "BundledPlugins.h"
#include "PlugInterfaces/Factory.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <unordered_set>
#include <vector>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

#include <yyjson.h>

namespace
{
using unique_doc = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;
using unique_mut_doc = wil::unique_any<yyjson_mut_doc*, decltype(&yyjson_mut_doc_free), yyjson_mut_doc_free>;
using unique_json = wil::unique_any<char*, decltype(&free), free>;

constexpr char kTrianglePlugin[] = "builtin.rotating-triangle";
constexpr char kGdiPlugin[] = "builtin.gdi-orbit";
constexpr char kMatrixPlugin[] = "builtin.matrix-rain";
constexpr char kProcessViewerPlugin[] = "builtin.process-viewer";
constexpr char kNetworkMeterPlugin[] = "builtin.network-meter";
constexpr char kGpuProcessesPlugin[] = "builtin.gpu-processes";
constexpr char kStudioClockPlugin[] = "builtin.studio-clock";
constexpr char kDeskClockPlugin[] = "builtin.desk-clock";
constexpr char kWeatherPlugin[] = "builtin.weather";
constexpr char kLauncherPlugin[] = "builtin.launcher";
constexpr char kAvControlPlugin[] = "builtin.av-control";
constexpr char kMatrixDefaults[] =
    R"json({"seed":1999,"glyphHeightDips":18,"densityPercent":70,"speedPercent":100,"trailLengthGlyphs":18,"mutationPerSecond":8,"headColor":"#D8FFE5","trailColor":"#00E65C","backgroundColor":"#010502","glowPercent":35})json";
constexpr char kProcessViewerDefaults[] = R"json({"topN":10})json";
constexpr char kRankedViewerDefaults[] = R"json({"topN":8})json";
constexpr char kStudioClockDefaults[] =
    R"json({"showSecondProgress":true,"externalDotsAlwaysOn":true,"showSeconds":true,"secondsColor":"#FF1616","showDate":false,"dateFormat":"dd-mm-yyyy","timeColor":"#FF1616","backgroundColor":"#111111"})json";
constexpr char kDeskClockDefaults[] =
    R"json({"flipDurationMilliseconds":420,"backgroundColor":"#000000","cardColor":"#FF3B43","digitColor":"#FFFFFF","dateColor":"#D8D8D8"})json";
constexpr char kWeatherDefaults[] =
    R"json({"locationMode":"automatic","location":"","temperatureUnit":"celsius","windUnit":"kmh"})json";
constexpr char kLauncherDefaults[] = R"json({"shortcuts":[],"iconSize":"huge"})json";

struct Declaration final
{
    std::string_view name;
    yyjson_val* definition = nullptr;
};

[[nodiscard]] size_t Utf8CodePointCount(std::string_view text) noexcept
{
    size_t count = 0;
    for (const unsigned char value : text)
    {
        if ((value & 0xC0U) != 0x80U)
            ++count;
    }
    return count;
}

[[nodiscard]] bool IsKnownPlugin(std::string_view plugin, const char*& typeId) noexcept
{
    for (const RedXeBundledWidgetSpec& candidate : kRedXeBundledWidgets)
    {
        if (plugin == candidate.pluginId)
        {
            typeId = candidate.typeId;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool CopyText(std::string_view source, SettingsText& destination, bool machineId) noexcept
{
    if (source.empty() || source.size() > kMaximumSettingsTextBytes || Utf8CodePointCount(source) > 128 ||
        source.find('\0') != std::string_view::npos)
    {
        return false;
    }
    SettingsText result{};
    std::memcpy(result.utf8.data(), source.data(), source.size());
    result.bytes = static_cast<uint32_t>(source.size());
    if (machineId && !RedXeIsValidMachineId(result.utf8.data()))
    {
        return false;
    }
    destination = result;
    return true;
}

struct JsonPathBuffer final
{
    std::array<char, 384> text{};
    uint32_t length = 1;

    JsonPathBuffer() noexcept
    {
        text[0] = '$';
    }

    [[nodiscard]] std::string_view View() const noexcept
    {
        return {text.data(), length};
    }

    bool AppendName(std::string_view name) noexcept
    {
        if (name.empty() || length + 1 + name.size() >= text.size())
        {
            return false;
        }
        text[length++] = '.';
        std::memcpy(text.data() + length, name.data(), name.size());
        length += static_cast<uint32_t>(name.size());
        text[length] = '\0';
        return true;
    }

    bool AppendIndex(size_t index) noexcept
    {
        char digits[32]{};
        const int written = sprintf_s(digits, "[%zu]", index);
        if (written <= 0 || length + static_cast<uint32_t>(written) >= text.size())
        {
            return false;
        }
        std::memcpy(text.data() + length, digits, static_cast<size_t>(written));
        length += static_cast<uint32_t>(written);
        text[length] = '\0';
        return true;
    }

    struct Scope final
    {
        JsonPathBuffer* path = nullptr;
        uint32_t restore = 0;

        Scope(JsonPathBuffer& owner, uint32_t previous) noexcept : path(&owner), restore(previous) {}
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&& other) noexcept : path(other.path), restore(other.restore)
        {
            other.path = nullptr;
        }
        Scope& operator=(Scope&&) = delete;
        ~Scope()
        {
            if (path)
            {
                path->length = restore;
                path->text[restore] = '\0';
            }
        }
    };

    [[nodiscard]] Scope PushName(std::string_view name) noexcept
    {
        const uint32_t restore = length;
        (void)AppendName(name);
        return Scope{*this, restore};
    }

    [[nodiscard]] Scope PushIndex(size_t index) noexcept
    {
        const uint32_t restore = length;
        (void)AppendIndex(index);
        return Scope{*this, restore};
    }
};

struct JsonTextCursor final
{
    std::string_view json;
    size_t index = 0;
    uint32_t line = 1;
    uint32_t column = 1;

    void SkipSpaceAndComments() noexcept
    {
        for (;;)
        {
            while (index < json.size())
            {
                const char ch = json[index];
                if (ch == ' ' || ch == '\t' || ch == '\r')
                {
                    ++index;
                    ++column;
                    continue;
                }
                if (ch == '\n')
                {
                    ++index;
                    ++line;
                    column = 1;
                    continue;
                }
                break;
            }
            if (index + 1 < json.size() && json[index] == '/' && json[index + 1] == '/')
            {
                index += 2;
                column += 2;
                while (index < json.size() && json[index] != '\n')
                {
                    ++index;
                    ++column;
                }
                continue;
            }
            if (index + 1 < json.size() && json[index] == '/' && json[index + 1] == '*')
            {
                index += 2;
                column += 2;
                while (index + 1 < json.size() && !(json[index] == '*' && json[index + 1] == '/'))
                {
                    if (json[index] == '\n')
                    {
                        ++line;
                        column = 1;
                    }
                    else
                    {
                        ++column;
                    }
                    ++index;
                }
                if (index + 1 < json.size())
                {
                    index += 2;
                    column += 2;
                }
                continue;
            }
            break;
        }
    }

    void Advance() noexcept
    {
        if (index >= json.size())
        {
            return;
        }
        if (json[index] == '\n')
        {
            ++line;
            column = 1;
        }
        else
        {
            ++column;
        }
        ++index;
    }

    [[nodiscard]] bool Consume(char expected) noexcept
    {
        SkipSpaceAndComments();
        if (index >= json.size() || json[index] != expected)
        {
            return false;
        }
        Advance();
        return true;
    }

    [[nodiscard]] bool SkipString() noexcept
    {
        SkipSpaceAndComments();
        if (index >= json.size() || json[index] != '"')
        {
            return false;
        }
        Advance();
        while (index < json.size())
        {
            const char ch = json[index];
            if (ch == '"')
            {
                Advance();
                return true;
            }
            if (ch == '\\')
            {
                Advance();
                if (index < json.size())
                {
                    Advance();
                }
                continue;
            }
            Advance();
        }
        return false;
    }

    [[nodiscard]] bool ReadString(std::string& value) noexcept
    {
        SkipSpaceAndComments();
        if (index >= json.size() || json[index] != '"')
        {
            return false;
        }
        Advance();
        value.clear();
        while (index < json.size())
        {
            const char ch = json[index];
            if (ch == '"')
            {
                Advance();
                return true;
            }
            if (ch == '\\')
            {
                Advance();
                if (index < json.size())
                {
                    value.push_back(json[index]);
                    Advance();
                }
                continue;
            }
            value.push_back(ch);
            Advance();
        }
        return false;
    }

    [[nodiscard]] bool SkipValue() noexcept
    {
        SkipSpaceAndComments();
        if (index >= json.size())
        {
            return false;
        }
        const char ch = json[index];
        if (ch == '"')
        {
            return SkipString();
        }
        if (ch == '{')
        {
            Advance();
            SkipSpaceAndComments();
            if (index < json.size() && json[index] == '}')
            {
                Advance();
                return true;
            }
            for (;;)
            {
                if (!SkipString() || !Consume(':') || !SkipValue())
                {
                    return false;
                }
                SkipSpaceAndComments();
                if (index < json.size() && json[index] == ',')
                {
                    Advance();
                    SkipSpaceAndComments();
                    if (index < json.size() && json[index] == '}')
                    {
                        Advance();
                        return true;
                    }
                    continue;
                }
                return Consume('}');
            }
        }
        if (ch == '[')
        {
            Advance();
            SkipSpaceAndComments();
            if (index < json.size() && json[index] == ']')
            {
                Advance();
                return true;
            }
            for (;;)
            {
                if (!SkipValue())
                {
                    return false;
                }
                SkipSpaceAndComments();
                if (index < json.size() && json[index] == ',')
                {
                    Advance();
                    SkipSpaceAndComments();
                    if (index < json.size() && json[index] == ']')
                    {
                        Advance();
                        return true;
                    }
                    continue;
                }
                return Consume(']');
            }
        }
        if (ch == '-' || (ch >= '0' && ch <= '9'))
        {
            while (index < json.size())
            {
                const char digit = json[index];
                if ((digit >= '0' && digit <= '9') || digit == '-' || digit == '+' || digit == '.' || digit == 'e' ||
                    digit == 'E')
                {
                    Advance();
                    continue;
                }
                break;
            }
            return true;
        }
        static constexpr std::string_view kTrue = "true";
        static constexpr std::string_view kFalse = "false";
        static constexpr std::string_view kNull = "null";
        const std::string_view word = json.substr(index);
        if (word.starts_with(kTrue))
        {
            for (size_t n = 0; n < kTrue.size(); ++n)
            {
                Advance();
            }
            return true;
        }
        if (word.starts_with(kFalse))
        {
            for (size_t n = 0; n < kFalse.size(); ++n)
            {
                Advance();
            }
            return true;
        }
        if (word.starts_with(kNull))
        {
            for (size_t n = 0; n < kNull.size(); ++n)
            {
                Advance();
            }
            return true;
        }
        return false;
    }
};

struct PointerSegment final
{
    bool isIndex = false;
    std::string_view name;
    size_t index = 0;
};

[[nodiscard]] bool ParsePointerSegments(std::string_view pointer, std::vector<PointerSegment>& segments)
{
    segments.clear();
    if (pointer.empty() || pointer[0] != '$')
    {
        return false;
    }
    size_t index = 1;
    while (index < pointer.size())
    {
        if (pointer[index] == '.')
        {
            ++index;
            const size_t start = index;
            while (index < pointer.size() && pointer[index] != '.' && pointer[index] != '[')
            {
                ++index;
            }
            if (start == index)
            {
                return false;
            }
            segments.push_back(PointerSegment{false, pointer.substr(start, index - start), 0});
            continue;
        }
        if (pointer[index] == '[')
        {
            ++index;
            size_t value = 0;
            if (index >= pointer.size() || pointer[index] < '0' || pointer[index] > '9')
            {
                return false;
            }
            while (index < pointer.size() && pointer[index] >= '0' && pointer[index] <= '9')
            {
                value = value * 10 + static_cast<size_t>(pointer[index] - '0');
                ++index;
            }
            if (index >= pointer.size() || pointer[index] != ']')
            {
                return false;
            }
            ++index;
            segments.push_back(PointerSegment{true, {}, value});
            continue;
        }
        return false;
    }
    return true;
}

[[nodiscard]] bool LocateJsonPointer(std::string_view json, std::string_view pointer, uint32_t& line, uint32_t& column,
                                     uint64_t& offset) noexcept
{
    try
    {
        JsonTextCursor cursor{json};
        if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xEF &&
            static_cast<unsigned char>(json[1]) == 0xBB && static_cast<unsigned char>(json[2]) == 0xBF)
        {
            cursor.index = 3;
            cursor.column = 4;
        }
        cursor.SkipSpaceAndComments();
        line = cursor.line;
        column = cursor.column;
        offset = cursor.index;
        std::vector<PointerSegment> segments;
        if (!ParsePointerSegments(pointer, segments))
        {
            return pointer == "$";
        }
        if (segments.empty())
        {
            return true;
        }
        auto locate = [&](auto&& self, size_t segmentIndex) -> bool
        {
            if (segmentIndex >= segments.size())
            {
                return true;
            }
            const PointerSegment& segment = segments[segmentIndex];
            cursor.SkipSpaceAndComments();
            if (segment.isIndex)
            {
                if (!cursor.Consume('['))
                {
                    return false;
                }
                for (size_t index = 0; index < segment.index; ++index)
                {
                    if (!cursor.SkipValue())
                    {
                        return false;
                    }
                    cursor.SkipSpaceAndComments();
                    if (cursor.index < cursor.json.size() && cursor.json[cursor.index] == ',')
                    {
                        cursor.Advance();
                    }
                }
                cursor.SkipSpaceAndComments();
                line = cursor.line;
                column = cursor.column;
                offset = cursor.index;
                if (segmentIndex + 1 == segments.size())
                {
                    return true;
                }
                return self(self, segmentIndex + 1);
            }
            if (!cursor.Consume('{'))
            {
                return false;
            }
            for (;;)
            {
                std::string key;
                const uint32_t keyLine = cursor.line;
                const uint32_t keyColumn = cursor.column;
                const uint64_t keyOffset = cursor.index;
                if (!cursor.ReadString(key) || !cursor.Consume(':'))
                {
                    return false;
                }
                if (key == segment.name)
                {
                    cursor.SkipSpaceAndComments();
                    line = cursor.line;
                    column = cursor.column;
                    offset = cursor.index;
                    if (segmentIndex + 1 == segments.size())
                    {
                        return true;
                    }
                    return self(self, segmentIndex + 1);
                }
                (void)keyLine;
                (void)keyColumn;
                (void)keyOffset;
                if (!cursor.SkipValue())
                {
                    return false;
                }
                cursor.SkipSpaceAndComments();
                if (cursor.index < cursor.json.size() && cursor.json[cursor.index] == ',')
                {
                    cursor.Advance();
                    continue;
                }
                return false;
            }
        };
        return locate(locate, 0);
    }
    catch (...)
    {
        return false;
    }
}

struct DiagnosticSink final
{
    std::string_view json;
    SettingsParseDiagnostic* out = nullptr;
    bool recorded = false;

    bool Fail(std::string_view path, std::string_view message) noexcept
    {
        if (out && !recorded)
        {
            recorded = true;
            try
            {
                out->path.assign(path.data(), path.size());
                out->message.assign(message.data(), message.size());
            }
            catch (...)
            {
                out->path = "$";
                out->message = "The settings document is invalid.";
            }
            uint32_t locatedLine = 1;
            uint32_t locatedColumn = 1;
            uint64_t locatedOffset = 0;
            if (LocateJsonPointer(json, out->path, locatedLine, locatedColumn, locatedOffset))
            {
                out->hasLocation = true;
                out->line = locatedLine;
                out->column = locatedColumn;
                out->byteOffset = locatedOffset;
            }
        }
        return false;
    }

    HRESULT FailHr(std::string_view path, std::string_view message) noexcept
    {
        Fail(path, message);
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
};

[[nodiscard]] const char* UnknownObjectMember(yyjson_val* object, std::initializer_list<const char*> keys) noexcept
{
    if (!yyjson_is_obj(object))
    {
        return nullptr;
    }
    yyjson_obj_iter iterator = yyjson_obj_iter_with(object);
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
    {
        const char* text = yyjson_get_str(key);
        bool known = false;
        for (const char* expected : keys)
        {
            known = known || (text && std::strcmp(text, expected) == 0);
        }
        if (!known)
        {
            return text;
        }
    }
    return nullptr;
}

[[nodiscard]] bool AcceptObjectMembers(DiagnosticSink& sink, JsonPathBuffer& path, yyjson_val* object,
                                       std::initializer_list<const char*> keys, bool allowUnknown) noexcept
{
    if (!yyjson_is_obj(object))
    {
        return sink.Fail(path.View(), "Expected a JSON object.");
    }
    if (allowUnknown)
    {
        return true;
    }
    const char* unknown = UnknownObjectMember(object, keys);
    if (!unknown)
    {
        return true;
    }
    const auto scope = path.PushName(unknown);
    std::string message = "Unknown member \"";
    message += unknown;
    message += "\".";
    return sink.Fail(path.View(), message);
}

[[nodiscard]] bool RejectDuplicateMembers(DiagnosticSink& sink, JsonPathBuffer& path, yyjson_val* value) noexcept
{
    if (yyjson_is_obj(value))
    {
        std::unordered_set<std::string_view> names;
        names.reserve(yyjson_obj_size(value));
        yyjson_obj_iter iterator = yyjson_obj_iter_with(value);
        while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
        {
            const std::string_view name(yyjson_get_str(key), yyjson_get_len(key));
            if (!names.insert(name).second)
            {
                const auto scope = path.PushName(name);
                std::string message = "Duplicate member \"";
                message.append(name);
                message += "\".";
                return sink.Fail(path.View(), message);
            }
            const auto scope = path.PushName(name);
            if (!RejectDuplicateMembers(sink, path, yyjson_obj_iter_get_val(key)))
            {
                return false;
            }
        }
    }
    else if (yyjson_is_arr(value))
    {
        const size_t count = yyjson_arr_size(value);
        for (size_t index = 0; index < count; ++index)
        {
            const auto scope = path.PushIndex(index);
            if (!RejectDuplicateMembers(sink, path, yyjson_arr_get(value, index)))
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] yyjson_mut_val* MergeValue(yyjson_mut_doc* document, yyjson_val* base, yyjson_val* patch) noexcept
{
    if (!patch || yyjson_is_null(patch))
    {
        return nullptr;
    }
    if (!yyjson_is_obj(base) || !yyjson_is_obj(patch))
    {
        return yyjson_val_mut_copy(document, patch);
    }

    yyjson_mut_val* result = yyjson_mut_obj(document);
    if (!result)
    {
        return nullptr;
    }
    yyjson_obj_iter baseIterator = yyjson_obj_iter_with(base);
    while (yyjson_val* key = yyjson_obj_iter_next(&baseIterator))
    {
        const char* name = yyjson_get_str(key);
        const size_t length = yyjson_get_len(key);
        yyjson_val* patchValue = yyjson_obj_getn(patch, name, length);
        if (patchValue)
        {
            continue;
        }
        yyjson_mut_val* copied = yyjson_val_mut_copy(document, yyjson_obj_iter_get_val(key));
        if (!copied || !yyjson_mut_obj_add_val(document, result, name, copied))
        {
            return nullptr;
        }
    }
    yyjson_obj_iter patchIterator = yyjson_obj_iter_with(patch);
    while (yyjson_val* key = yyjson_obj_iter_next(&patchIterator))
    {
        const char* name = yyjson_get_str(key);
        yyjson_val* patchValue = yyjson_obj_iter_get_val(key);
        yyjson_mut_val* merged = MergeValue(document, yyjson_obj_get(base, name), patchValue);
        if (yyjson_is_null(patchValue))
        {
            continue;
        }
        if (!merged || !yyjson_mut_obj_add_val(document, result, name, merged))
        {
            return nullptr;
        }
    }
    return result;
}

[[nodiscard]] bool CompactObject(yyjson_val* object, JsonObjectSettings& destination) noexcept
{
    if (!yyjson_is_obj(object))
    {
        return false;
    }
    unique_mut_doc mutableDocument{yyjson_mut_doc_new(nullptr)};
    if (!mutableDocument)
    {
        return false;
    }
    yyjson_mut_doc_set_root(mutableDocument.get(), yyjson_val_mut_copy(mutableDocument.get(), object));
    size_t bytes = 0;
    unique_json json{yyjson_mut_write(mutableDocument.get(), YYJSON_WRITE_NOFLAG, &bytes)};
    if (!json || bytes == 0 || bytes > kPrivateConfigurationCapacity)
    {
        return false;
    }
    JsonObjectSettings copied{};
    std::memcpy(copied.utf8.data(), json.get(), bytes);
    copied.utf8[bytes] = '\0';
    copied.bytes = static_cast<uint32_t>(bytes);
    destination = copied;
    return true;
}

[[nodiscard]] bool ColorIsRgbHex(yyjson_val* value) noexcept
{
    const char* text = yyjson_is_str(value) ? yyjson_get_str(value) : nullptr;
    if (!text || yyjson_get_len(value) != 7 || text[0] != '#')
    {
        return false;
    }
    for (size_t index = 1; index < 7; ++index)
    {
        if (!std::isxdigit(static_cast<unsigned char>(text[index])))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool RejectRange(DiagnosticSink& sink, JsonPathBuffer& path, yyjson_val* settings, const char* key,
                               uint64_t minimum, uint64_t maximum, const char* message) noexcept
{
    const auto scope = path.PushName(key);
    yyjson_val* value = yyjson_obj_get(settings, key);
    if (yyjson_is_uint(value) && yyjson_get_uint(value) >= minimum && yyjson_get_uint(value) <= maximum)
    {
        return true;
    }
    return sink.Fail(path.View(), message);
}

[[nodiscard]] bool RejectColor(DiagnosticSink& sink, JsonPathBuffer& path, yyjson_val* settings,
                               const char* key) noexcept
{
    const auto scope = path.PushName(key);
    if (ColorIsRgbHex(yyjson_obj_get(settings, key)))
    {
        return true;
    }
    return sink.Fail(path.View(), "Color must be a #RRGGBB hex string.");
}

[[nodiscard]] bool RejectBool(DiagnosticSink& sink, JsonPathBuffer& path, yyjson_val* settings,
                              const char* key) noexcept
{
    const auto scope = path.PushName(key);
    if (yyjson_is_bool(yyjson_obj_get(settings, key)))
    {
        return true;
    }
    return sink.Fail(path.View(), "Expected a boolean.");
}

[[nodiscard]] bool ValidateMatrixSettings(yyjson_val* settings, DiagnosticSink& sink, JsonPathBuffer& path) noexcept
{
    if (!AcceptObjectMembers(sink, path, settings,
                             {"seed", "glyphHeightDips", "densityPercent", "speedPercent", "trailLengthGlyphs",
                              "mutationPerSecond", "headColor", "trailColor", "backgroundColor", "glowPercent"},
                             false))
    {
        return false;
    }
    return yyjson_obj_size(settings) == 10 &&
           RejectRange(sink, path, settings, "seed", 0, UINT32_MAX,
                       "seed must be an integer from 0 through 4294967295.") &&
           RejectRange(sink, path, settings, "glyphHeightDips", 12, 48,
                       "glyphHeightDips must be an integer from 12 through 48.") &&
           RejectRange(sink, path, settings, "densityPercent", 10, 100,
                       "densityPercent must be an integer from 10 through 100.") &&
           RejectRange(sink, path, settings, "speedPercent", 25, 300,
                       "speedPercent must be an integer from 25 through 300.") &&
           RejectRange(sink, path, settings, "trailLengthGlyphs", 6, 48,
                       "trailLengthGlyphs must be an integer from 6 through 48.") &&
           RejectRange(sink, path, settings, "mutationPerSecond", 0, 30,
                       "mutationPerSecond must be an integer from 0 through 30.") &&
           RejectRange(sink, path, settings, "glowPercent", 0, 100,
                       "glowPercent must be an integer from 0 through 100.") &&
           RejectColor(sink, path, settings, "headColor") && RejectColor(sink, path, settings, "trailColor") &&
           RejectColor(sink, path, settings, "backgroundColor");
}

[[nodiscard]] bool ValidateTopNSettings(yyjson_val* settings, uint32_t minimum, uint32_t maximum, DiagnosticSink& sink,
                                        JsonPathBuffer& path) noexcept
{
    if (!AcceptObjectMembers(sink, path, settings, {"topN"}, false))
    {
        return false;
    }
    char message[96]{};
    sprintf_s(message, "topN must be an integer from %u through %u.", minimum, maximum);
    return RejectRange(sink, path, settings, "topN", minimum, maximum, message);
}

[[nodiscard]] bool ValidateProcessViewerSettings(yyjson_val* settings, DiagnosticSink& sink,
                                                 JsonPathBuffer& path) noexcept
{
    return ValidateTopNSettings(settings, 1, 32, sink, path);
}

[[nodiscard]] bool ValidateStudioClockSettings(yyjson_val* settings, DiagnosticSink& sink,
                                               JsonPathBuffer& path) noexcept
{
    if (!AcceptObjectMembers(sink, path, settings,
                             {"showSecondProgress", "externalDotsAlwaysOn", "showSeconds", "secondsColor", "showDate",
                              "dateFormat", "timeColor", "backgroundColor"},
                             false) ||
        yyjson_obj_size(settings) != 8)
    {
        return yyjson_is_obj(settings) && yyjson_obj_size(settings) != 8 &&
                       UnknownObjectMember(settings,
                                           {"showSecondProgress", "externalDotsAlwaysOn", "showSeconds", "secondsColor",
                                            "showDate", "dateFormat", "timeColor", "backgroundColor"}) == nullptr
                   ? sink.Fail(path.View(), "Studio Clock settings must include every required member.")
                   : false;
    }
    yyjson_val* dateFormatValue = yyjson_obj_get(settings, "dateFormat");
    const char* dateFormat = yyjson_is_str(dateFormatValue) ? yyjson_get_str(dateFormatValue) : nullptr;
    const bool validDateFormat =
        dateFormat && (std::strcmp(dateFormat, "dd-mm-yyyy") == 0 || std::strcmp(dateFormat, "mm-dd-yyyy") == 0 ||
                       std::strcmp(dateFormat, "yyyy-mm-dd") == 0);
    if (!validDateFormat)
    {
        const auto scope = path.PushName("dateFormat");
        return sink.Fail(path.View(), "dateFormat must be dd-mm-yyyy, mm-dd-yyyy, or yyyy-mm-dd.");
    }
    return RejectBool(sink, path, settings, "showSecondProgress") &&
           RejectBool(sink, path, settings, "externalDotsAlwaysOn") &&
           RejectBool(sink, path, settings, "showSeconds") && RejectBool(sink, path, settings, "showDate") &&
           RejectColor(sink, path, settings, "secondsColor") && RejectColor(sink, path, settings, "timeColor") &&
           RejectColor(sink, path, settings, "backgroundColor");
}

[[nodiscard]] bool ValidateDeskClockSettings(yyjson_val* settings, DiagnosticSink& sink, JsonPathBuffer& path) noexcept
{
    if (!AcceptObjectMembers(sink, path, settings,
                             {"flipDurationMilliseconds", "backgroundColor", "cardColor", "digitColor", "dateColor"},
                             false) ||
        yyjson_obj_size(settings) != 5)
    {
        return yyjson_is_obj(settings) && yyjson_obj_size(settings) != 5 &&
                       UnknownObjectMember(settings, {"flipDurationMilliseconds", "backgroundColor", "cardColor",
                                                      "digitColor", "dateColor"}) == nullptr
                   ? sink.Fail(path.View(), "Desk Clock settings must include every required member.")
                   : false;
    }
    return RejectRange(sink, path, settings, "flipDurationMilliseconds", 250, 800,
                       "flipDurationMilliseconds must be an integer from 250 through 800.") &&
           RejectColor(sink, path, settings, "backgroundColor") && RejectColor(sink, path, settings, "cardColor") &&
           RejectColor(sink, path, settings, "digitColor") && RejectColor(sink, path, settings, "dateColor");
}

[[nodiscard]] bool ValidateAvControlSettings(yyjson_val* settings, DiagnosticSink& sink, JsonPathBuffer& path) noexcept
{
    size_t length = 0;
    unique_json serialized(yyjson_val_write(settings, 0, &length));
    AVControl::Configuration configuration;
    if (serialized && SUCCEEDED(AVControl::ParseConfiguration({serialized.get(), length}, configuration)))
    {
        return true;
    }
    return sink.Fail(path.View(), "AV Control settings are invalid. Check profiles, device ids, and field types.");
}

[[nodiscard]] bool ValidateWeatherSettings(yyjson_val* settings, DiagnosticSink& sink, JsonPathBuffer& path) noexcept
{
    if (!AcceptObjectMembers(sink, path, settings, {"locationMode", "location", "temperatureUnit", "windUnit"},
                             false) ||
        yyjson_obj_size(settings) != 4)
    {
        return yyjson_is_obj(settings) && yyjson_obj_size(settings) != 4 &&
                       UnknownObjectMember(settings, {"locationMode", "location", "temperatureUnit", "windUnit"}) ==
                           nullptr
                   ? sink.Fail(path.View(), "Weather settings must include every required member.")
                   : false;
    }
    yyjson_val* locationModeValue = yyjson_obj_get(settings, "locationMode");
    yyjson_val* locationValue = yyjson_obj_get(settings, "location");
    yyjson_val* temperatureValue = yyjson_obj_get(settings, "temperatureUnit");
    yyjson_val* windValue = yyjson_obj_get(settings, "windUnit");
    const char* locationMode = yyjson_is_str(locationModeValue) ? yyjson_get_str(locationModeValue) : nullptr;
    const char* temperatureUnit = yyjson_is_str(temperatureValue) ? yyjson_get_str(temperatureValue) : nullptr;
    const char* windUnit = yyjson_is_str(windValue) ? yyjson_get_str(windValue) : nullptr;
    if (!locationMode || (std::strcmp(locationMode, "automatic") != 0 && std::strcmp(locationMode, "manual") != 0))
    {
        const auto scope = path.PushName("locationMode");
        return sink.Fail(path.View(), "locationMode must be automatic or manual.");
    }
    if (!yyjson_is_str(locationValue) || yyjson_get_len(locationValue) > 128)
    {
        const auto scope = path.PushName("location");
        return sink.Fail(path.View(), "location must be a string of at most 128 bytes.");
    }
    if (!temperatureUnit ||
        (std::strcmp(temperatureUnit, "celsius") != 0 && std::strcmp(temperatureUnit, "fahrenheit") != 0))
    {
        const auto scope = path.PushName("temperatureUnit");
        return sink.Fail(path.View(), "temperatureUnit must be celsius or fahrenheit.");
    }
    if (!windUnit || (std::strcmp(windUnit, "kmh") != 0 && std::strcmp(windUnit, "mph") != 0))
    {
        const auto scope = path.PushName("windUnit");
        return sink.Fail(path.View(), "windUnit must be kmh or mph.");
    }
    return true;
}

[[nodiscard]] int ClassifyLauncherTarget(std::string_view target) noexcept
{
    if (target.empty() || target.size() > 512)
    {
        return 0;
    }
    if (target.size() >= 3 && ((target[0] >= 'A' && target[0] <= 'Z') || (target[0] >= 'a' && target[0] <= 'z')) &&
        target[1] == ':' && (target[2] == '\\' || target[2] == '/'))
    {
        return 1;
    }
    if (target.size() >= 2 && target[0] == '\\' && target[1] == '\\')
    {
        return 1;
    }
    if (target.size() < 3 || !std::isalpha(static_cast<unsigned char>(target[0])))
    {
        return 0;
    }
    size_t index = 1;
    while (index < target.size())
    {
        const unsigned char value = static_cast<unsigned char>(target[index]);
        if (!(std::isalnum(value) || value == '+' || value == '.' || value == '-'))
        {
            break;
        }
        ++index;
    }
    return (index >= 2 && index < target.size() && target[index] == ':') ? 2 : 0;
}

[[nodiscard]] bool LauncherTargetsEqual(std::string_view left, std::string_view right, int kind) noexcept
{
    if (kind == 2)
    {
        return left == right;
    }
    std::array<wchar_t, 513> leftWide{};
    std::array<wchar_t, 513> rightWide{};
    const int leftCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, left.data(), static_cast<int>(left.size()),
                                              leftWide.data(), static_cast<int>(leftWide.size() - 1));
    const int rightCount =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, right.data(), static_cast<int>(right.size()),
                            rightWide.data(), static_cast<int>(rightWide.size() - 1));
    if (leftCount <= 0 || rightCount <= 0)
    {
        return left == right;
    }
    return CompareStringOrdinal(leftWide.data(), leftCount, rightWide.data(), rightCount, TRUE) == CSTR_EQUAL;
}

[[nodiscard]] bool ValidateLauncherSettings(yyjson_val* settings, DiagnosticSink& sink, JsonPathBuffer& path) noexcept
{
    if (!AcceptObjectMembers(sink, path, settings, {"shortcuts", "iconSize"}, false))
    {
        return false;
    }
    yyjson_val* iconSize = yyjson_obj_get(settings, "iconSize");
    if (iconSize)
    {
        const auto iconScope = path.PushName("iconSize");
        const char* size = yyjson_is_str(iconSize) ? yyjson_get_str(iconSize) : nullptr;
        LauncherIconSize parsed = LauncherIconSize::Huge;
        if (!size || !TryParseLauncherIconSize(size, parsed))
        {
            return sink.Fail(path.View(), "iconSize must be small, medium, large, huge, or automatic.");
        }
    }
    yyjson_val* shortcuts = yyjson_obj_get(settings, "shortcuts");
    {
        const auto scope = path.PushName("shortcuts");
        if (!yyjson_is_arr(shortcuts))
        {
            return sink.Fail(path.View(), "shortcuts must be an array.");
        }
        if (yyjson_arr_size(shortcuts) > kLauncherMaximumShortcuts)
        {
            return sink.Fail(path.View(), "Launcher supports at most 32 shortcuts.");
        }
        const size_t count = yyjson_arr_size(shortcuts);
        std::array<std::string_view, kLauncherMaximumShortcuts> seen{};
        std::array<int, kLauncherMaximumShortcuts> kinds{};
        for (size_t index = 0; index < count; ++index)
        {
            const auto itemScope = path.PushIndex(index);
            yyjson_val* item = yyjson_arr_get(shortcuts, index);
            if (!AcceptObjectMembers(sink, path, item, {"target", "iconPng"}, false))
            {
                return false;
            }
            yyjson_val* targetValue = yyjson_obj_get(item, "target");
            if (!yyjson_is_str(targetValue) || yyjson_get_len(targetValue) == 0 || yyjson_get_len(targetValue) > 512)
            {
                const auto targetScope = path.PushName("target");
                return sink.Fail(path.View(), "target must be a string of 1 through 512 bytes.");
            }
            const std::string_view target(yyjson_get_str(targetValue), yyjson_get_len(targetValue));
            const int kind = ClassifyLauncherTarget(target);
            if (kind == 0)
            {
                const auto targetScope = path.PushName("target");
                return sink.Fail(path.View(),
                                 "target must be an absolute Win32 path or a URI with a two-or-more-letter "
                                 "scheme.");
            }
            yyjson_val* iconValue = yyjson_obj_get(item, "iconPng");
            if (iconValue)
            {
                const auto iconScope = path.PushName("iconPng");
                if (!yyjson_is_str(iconValue) || yyjson_get_len(iconValue) > 260)
                {
                    return sink.Fail(path.View(), "iconPng must be a string of at most 260 bytes.");
                }
                const std::string_view icon(yyjson_get_str(iconValue), yyjson_get_len(iconValue));
                if (!icon.empty() && ClassifyLauncherTarget(icon) != 1)
                {
                    return sink.Fail(path.View(), "iconPng must be an absolute Win32 path.");
                }
            }
            for (size_t previous = 0; previous < index; ++previous)
            {
                if (kinds[previous] == kind && LauncherTargetsEqual(seen[previous], target, kind))
                {
                    const auto targetScope = path.PushName("target");
                    return sink.Fail(path.View(), "Duplicate shortcut target.");
                }
            }
            seen[index] = target;
            kinds[index] = kind;
        }
    }
    return true;
}

[[nodiscard]] yyjson_val* FindDeclaration(const std::vector<Declaration>& declarations, std::string_view name) noexcept
{
    for (const Declaration& declaration : declarations)
    {
        if (declaration.name == name)
            return declaration.definition;
    }
    return nullptr;
}

[[nodiscard]] bool AddUsedPlugin(AppSettings& settings, std::string_view plugin) noexcept
{
    for (uint32_t index = 0; index < settings.pluginCount; ++index)
    {
        if (settings.plugins[index].id.View() == plugin)
            return true;
    }
    if (settings.pluginCount >= kMaximumSettingsPlugins)
        return false;
    settings.plugins.emplace_back();
    PluginSettings& added = settings.plugins.back();
    ++settings.pluginCount;
    added.enabled = true;
    return CopyText(plugin, added.id, true);
}

[[nodiscard]] bool RejectRetiredMembers(DiagnosticSink& sink, JsonPathBuffer& path, yyjson_val* object) noexcept
{
    if (!yyjson_is_obj(object))
    {
        return true;
    }
    static constexpr const char* kNames[] = {"layout", "areas", "arrangeAlong", "sizeRatio", "settings", "override"};
    static constexpr const char* kMessages[] = {
        "Version 5 pages use widgets, columns, or rows; layout is not accepted.",
        "Version 5 does not accept areas; use widgets, columns, or rows.",
        "Version 5 does not accept arrangeAlong; use widgets, columns, or rows.",
        "Version 5 does not accept sizeRatio; use weight on columns or rows items.",
        "Plugin keys are flattened onto the object; nested settings is not accepted.",
        "Use flattened keys on a use-object; override is not accepted."};
    for (size_t index = 0; index < 6; ++index)
    {
        if (yyjson_obj_get(object, kNames[index]))
        {
            const auto scope = path.PushName(kNames[index]);
            return sink.Fail(path.View(), kMessages[index]);
        }
    }
    return true;
}

[[nodiscard]] bool RejectReservedSplitMembers(DiagnosticSink& sink, JsonPathBuffer& path, yyjson_val* object) noexcept
{
    static constexpr const char* kNames[] = {"weight", "widget", "rows", "columns", "along"};
    for (const char* name : kNames)
    {
        if (yyjson_obj_get(object, name))
        {
            const auto scope = path.PushName(name);
            return sink.Fail(path.View(), "This key belongs on a columns or rows item, not on a widget object.");
        }
    }
    return true;
}

[[nodiscard]] yyjson_mut_val* CopyObjectSkipping(yyjson_mut_doc* document, yyjson_val* object,
                                                 std::initializer_list<const char*> skip) noexcept
{
    if (!document || !yyjson_is_obj(object))
    {
        return nullptr;
    }
    yyjson_mut_val* result = yyjson_mut_obj(document);
    if (!result)
    {
        return nullptr;
    }
    yyjson_obj_iter iterator = yyjson_obj_iter_with(object);
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
    {
        const char* name = yyjson_get_str(key);
        bool skipped = false;
        for (const char* item : skip)
        {
            skipped = skipped || (name && std::strcmp(name, item) == 0);
        }
        if (skipped)
        {
            continue;
        }
        yyjson_mut_val* copied = yyjson_val_mut_copy(document, yyjson_obj_iter_get_val(key));
        if (!copied || !yyjson_mut_obj_add_val(document, result, name, copied))
        {
            return nullptr;
        }
    }
    return result;
}

[[nodiscard]] bool ParsePluginInstance(std::string_view plugin, yyjson_val* authoredSettings, AppSettings& settings,
                                       WidgetInstanceSettings& widget, uint32_t instanceIndex, DiagnosticSink& sink,
                                       JsonPathBuffer& path) noexcept
{
    const char* typeId = nullptr;
    if (!IsKnownPlugin(plugin, typeId))
    {
        const auto scope = path.PushName("plugin");
        std::string message = "Unknown plugin \"";
        message.append(plugin);
        message += "\". This build only accepts catalogued plugin ids.";
        return sink.Fail(path.View(), message);
    }
    if (!AddUsedPlugin(settings, plugin) || !CopyText(plugin, widget.pluginId, true) ||
        !CopyText(typeId, widget.typeId, true))
    {
        return sink.Fail(path.View(), "Too many plugins are referenced, or a plugin id is invalid.");
    }

    std::array<char, 32> instance{};
    const int written = sprintf_s(instance.data(), instance.size(), "widget.%u", instanceIndex + 1);
    if (written <= 0 || !CopyText(std::string_view(instance.data(), static_cast<size_t>(written)), widget.id, true))
        return sink.Fail(path.View(), "A widget instance id could not be assigned.");

    unique_doc defaults;
    unique_mut_doc effectiveDocument;
    unique_json effectiveJson;
    unique_doc effectiveImmutable;
    const char* defaultsText = plugin == kMatrixPlugin                                          ? kMatrixDefaults
                               : plugin == kProcessViewerPlugin                                 ? kProcessViewerDefaults
                               : plugin == kNetworkMeterPlugin || plugin == kGpuProcessesPlugin ? kRankedViewerDefaults
                               : plugin == kStudioClockPlugin                                   ? kStudioClockDefaults
                               : plugin == kDeskClockPlugin                                     ? kDeskClockDefaults
                               : plugin == kWeatherPlugin                                       ? kWeatherDefaults
                               : plugin == kLauncherPlugin                                      ? kLauncherDefaults
                               : plugin == kAvControlPlugin ? AVControl::DefaultsJson
                                                            : "{}";
    defaults.reset(yyjson_read(defaultsText, std::strlen(defaultsText), YYJSON_READ_NOFLAG));
    yyjson_val* settingsValue = authoredSettings;
    if (!settingsValue)
    {
        settingsValue = defaults ? yyjson_doc_get_root(defaults.get()) : nullptr;
    }
    else if (plugin == kMatrixPlugin || plugin == kProcessViewerPlugin || plugin == kNetworkMeterPlugin ||
             plugin == kGpuProcessesPlugin || plugin == kStudioClockPlugin || plugin == kDeskClockPlugin ||
             plugin == kWeatherPlugin || plugin == kLauncherPlugin || plugin == kAvControlPlugin)
    {
        effectiveDocument.reset(yyjson_mut_doc_new(nullptr));
        yyjson_mut_val* merged =
            effectiveDocument ? MergeValue(effectiveDocument.get(), yyjson_doc_get_root(defaults.get()), settingsValue)
                              : nullptr;
        if (!merged)
            return sink.Fail(path.View(), "Widget settings could not be merged with defaults.");
        yyjson_mut_doc_set_root(effectiveDocument.get(), merged);
        size_t effectiveBytes = 0;
        effectiveJson.reset(yyjson_mut_write(effectiveDocument.get(), YYJSON_WRITE_NOFLAG, &effectiveBytes));
        effectiveImmutable.reset(effectiveJson ? yyjson_read(effectiveJson.get(), effectiveBytes, YYJSON_READ_NOFLAG)
                                               : nullptr);
        settingsValue = effectiveImmutable ? yyjson_doc_get_root(effectiveImmutable.get()) : nullptr;
    }
    if (!yyjson_is_obj(settingsValue))
        return sink.Fail(path.View(), "Plugin settings must be a JSON object.");
    const bool validSettings =
        plugin == kMatrixPlugin          ? ValidateMatrixSettings(settingsValue, sink, path)
        : plugin == kProcessViewerPlugin ? ValidateProcessViewerSettings(settingsValue, sink, path)
        : plugin == kNetworkMeterPlugin || plugin == kGpuProcessesPlugin
            ? ValidateTopNSettings(settingsValue, 1, 16, sink, path)
        : plugin == kStudioClockPlugin        ? ValidateStudioClockSettings(settingsValue, sink, path)
        : plugin == kDeskClockPlugin          ? ValidateDeskClockSettings(settingsValue, sink, path)
        : plugin == kWeatherPlugin            ? ValidateWeatherSettings(settingsValue, sink, path)
        : plugin == kLauncherPlugin           ? ValidateLauncherSettings(settingsValue, sink, path)
        : plugin == kAvControlPlugin          ? ValidateAvControlSettings(settingsValue, sink, path)
        : yyjson_obj_size(settingsValue) == 0 ? true
                                              : sink.Fail(path.View(), "This plugin does not accept settings members.");
    if (!validSettings)
        return false;
    if (!CompactObject(settingsValue, widget.privateConfiguration))
        return sink.Fail(path.View(), "Widget settings exceed the 4096-byte compact limit.");
    return true;
}

[[nodiscard]] bool ParseFlattenedPluginObject(yyjson_val* definition, AppSettings& settings,
                                              WidgetInstanceSettings& widget, uint32_t instanceIndex,
                                              DiagnosticSink& sink, JsonPathBuffer& path) noexcept
{
    if (!RejectRetiredMembers(sink, path, definition) || !RejectReservedSplitMembers(sink, path, definition))
        return false;
    if (yyjson_obj_get(definition, "use"))
    {
        const auto scope = path.PushName("use");
        return sink.Fail(path.View(), "use belongs on a use-object; a plugin object has plugin and flattened keys.");
    }
    yyjson_val* pluginValue = yyjson_obj_get(definition, "plugin");
    if (!yyjson_is_str(pluginValue))
    {
        const auto scope = path.PushName("plugin");
        return sink.Fail(path.View(), "plugin must be a string.");
    }
    const std::string_view plugin(yyjson_get_str(pluginValue), yyjson_get_len(pluginValue));
    unique_mut_doc extracted{yyjson_mut_doc_new(nullptr)};
    yyjson_mut_val* keys = extracted ? CopyObjectSkipping(extracted.get(), definition, {"plugin"}) : nullptr;
    if (!keys)
        return sink.Fail(path.View(), "Widget settings could not be copied.");
    yyjson_mut_doc_set_root(extracted.get(), keys);
    size_t bytes = 0;
    unique_json json{yyjson_mut_write(extracted.get(), YYJSON_WRITE_NOFLAG, &bytes)};
    unique_doc immutable{json ? yyjson_read(json.get(), bytes, YYJSON_READ_NOFLAG) : nullptr};
    yyjson_val* authored = immutable ? yyjson_doc_get_root(immutable.get()) : nullptr;
    return authored && ParsePluginInstance(plugin, authored, settings, widget, instanceIndex, sink, path);
}

[[nodiscard]] bool ParseFlattenedUseObject(yyjson_val* authored, const std::vector<Declaration>& declarations,
                                           AppSettings& settings, WidgetInstanceSettings& widget,
                                           uint32_t instanceIndex, DiagnosticSink& sink, JsonPathBuffer& path) noexcept
{
    if (!RejectRetiredMembers(sink, path, authored) || !RejectReservedSplitMembers(sink, path, authored))
        return false;
    yyjson_val* use = yyjson_obj_get(authored, "use");
    if (!yyjson_is_str(use))
    {
        const auto scope = path.PushName("use");
        return sink.Fail(path.View(), "use must be a declare name.");
    }
    const std::string_view useName(yyjson_get_str(use), yyjson_get_len(use));
    yyjson_val* declaration = FindDeclaration(declarations, useName);
    if (!declaration)
    {
        const auto scope = path.PushName("use");
        std::string message = "There is no declare entry named \"";
        message.append(useName);
        message += "\".";
        return sink.Fail(path.View(), message);
    }
    yyjson_val* pluginOverride = yyjson_obj_get(authored, "plugin");
    std::string_view plugin;
    if (pluginOverride)
    {
        if (!yyjson_is_str(pluginOverride))
        {
            const auto scope = path.PushName("plugin");
            return sink.Fail(path.View(), "plugin must be a string.");
        }
        plugin = std::string_view(yyjson_get_str(pluginOverride), yyjson_get_len(pluginOverride));
    }
    else
    {
        yyjson_val* declaredPlugin = yyjson_obj_get(declaration, "plugin");
        if (!yyjson_is_str(declaredPlugin))
        {
            return sink.Fail(path.View(), "The declare entry is missing a plugin id.");
        }
        plugin = std::string_view(yyjson_get_str(declaredPlugin), yyjson_get_len(declaredPlugin));
    }

    unique_mut_doc baseDocument{yyjson_mut_doc_new(nullptr)};
    unique_mut_doc patchDocument{yyjson_mut_doc_new(nullptr)};
    yyjson_mut_val* base = baseDocument ? CopyObjectSkipping(baseDocument.get(), declaration, {"plugin"}) : nullptr;
    yyjson_mut_val* patch =
        patchDocument ? CopyObjectSkipping(patchDocument.get(), authored, {"use", "plugin"}) : nullptr;
    if (!base || !patch)
        return sink.Fail(path.View(), "Widget override could not be merged.");
    yyjson_mut_doc_set_root(baseDocument.get(), base);
    yyjson_mut_doc_set_root(patchDocument.get(), patch);
    size_t baseBytes = 0;
    size_t patchBytes = 0;
    unique_json baseJson{yyjson_mut_write(baseDocument.get(), YYJSON_WRITE_NOFLAG, &baseBytes)};
    unique_json patchJson{yyjson_mut_write(patchDocument.get(), YYJSON_WRITE_NOFLAG, &patchBytes)};
    unique_doc baseImmutable{baseJson ? yyjson_read(baseJson.get(), baseBytes, YYJSON_READ_NOFLAG) : nullptr};
    unique_doc patchImmutable{patchJson ? yyjson_read(patchJson.get(), patchBytes, YYJSON_READ_NOFLAG) : nullptr};
    unique_mut_doc resultDocument{yyjson_mut_doc_new(nullptr)};
    yyjson_mut_val* merged = resultDocument && baseImmutable && patchImmutable
                                 ? MergeValue(resultDocument.get(), yyjson_doc_get_root(baseImmutable.get()),
                                              yyjson_doc_get_root(patchImmutable.get()))
                                 : nullptr;
    if (!merged)
        return sink.Fail(path.View(), "Widget override could not be merged.");
    yyjson_mut_doc_set_root(resultDocument.get(), merged);
    size_t bytes = 0;
    unique_json json{yyjson_mut_write(resultDocument.get(), YYJSON_WRITE_NOFLAG, &bytes)};
    unique_doc immutable{json ? yyjson_read(json.get(), bytes, YYJSON_READ_NOFLAG) : nullptr};
    yyjson_val* authoredSettings = immutable ? yyjson_doc_get_root(immutable.get()) : nullptr;
    return authoredSettings &&
           ParsePluginInstance(plugin, authoredSettings, settings, widget, instanceIndex, sink, path);
}

[[nodiscard]] bool ResolveWidget(yyjson_val* authored, const std::vector<Declaration>& declarations,
                                 AppSettings& settings, WidgetInstanceSettings& widget, uint32_t instanceIndex,
                                 DiagnosticSink& sink, JsonPathBuffer& path) noexcept
{
    if (yyjson_is_str(authored))
    {
        const std::string_view name(yyjson_get_str(authored), yyjson_get_len(authored));
        yyjson_val* declaration = FindDeclaration(declarations, name);
        if (declaration)
        {
            JsonPathBuffer declarePath;
            const auto declareScope = declarePath.PushName("declare");
            const auto nameScope = declarePath.PushName(name);
            return ParseFlattenedPluginObject(declaration, settings, widget, instanceIndex, sink, declarePath);
        }
        const char* catalogTypeId = nullptr;
        if (IsKnownPlugin(name, catalogTypeId))
        {
            (void)catalogTypeId;
            return ParsePluginInstance(name, nullptr, settings, widget, instanceIndex, sink, path);
        }
        std::string message = "There is no declare entry named \"";
        message.append(name);
        message += "\".";
        return sink.Fail(path.View(), message);
    }
    if (!yyjson_is_obj(authored))
        return sink.Fail(path.View(), "A widget must be a declare name or an object.");
    if (yyjson_obj_get(authored, "use"))
        return ParseFlattenedUseObject(authored, declarations, settings, widget, instanceIndex, sink, path);
    if (yyjson_obj_get(authored, "plugin"))
        return ParseFlattenedPluginObject(authored, settings, widget, instanceIndex, sink, path);
    return sink.Fail(path.View(), "A widget object must contain plugin or use.");
}

[[nodiscard]] bool IsSplitContainerItem(yyjson_val* item) noexcept
{
    return yyjson_is_obj(item) && (yyjson_obj_get(item, "weight") || yyjson_obj_get(item, "widget") ||
                                   yyjson_obj_get(item, "rows") || yyjson_obj_get(item, "columns"));
}

[[nodiscard]] bool ReadItemWeight(yyjson_val* item, uint32_t& weight, DiagnosticSink& sink,
                                  JsonPathBuffer& path) noexcept
{
    weight = 1;
    if (!yyjson_is_obj(item))
        return true;
    yyjson_val* value = yyjson_obj_get(item, "weight");
    if (!value)
        return true;
    const auto scope = path.PushName("weight");
    if (!yyjson_is_uint(value) || yyjson_get_uint(value) == 0 || yyjson_get_uint(value) > 1000)
        return sink.Fail(path.View(), "weight must be an integer from 1 through 1000.");
    weight = static_cast<uint32_t>(yyjson_get_uint(value));
    return true;
}

[[nodiscard]] bool ParseHumanItems(yyjson_val* items, LayoutAxis axis, bool allowContainers,
                                   const std::vector<Declaration>& declarations, AppSettings& settings,
                                   DashboardPageSettings& page, AdaptiveWidgetPlacement placementPath, uint32_t depth,
                                   uint32_t& areaCount, uint32_t& instanceCount, DiagnosticSink& sink,
                                   JsonPathBuffer& path) noexcept
{
    if (depth >= kMaximumLayoutDepth)
        return sink.Fail(path.View(), "Layout nesting exceeds eight container levels.");
    if (!yyjson_is_arr(items) || yyjson_arr_size(items) == 0)
        return sink.Fail(path.View(), "Expected a nonempty array of layout items.");

    uint32_t total = 0;
    const size_t count = yyjson_arr_size(items);
    for (size_t index = 0; index < count; ++index)
    {
        const auto itemScope = path.PushIndex(index);
        yyjson_val* item = yyjson_arr_get(items, index);
        uint32_t weight = 1;
        if (!ReadItemWeight(item, weight, sink, path))
            return false;
        if (total > UINT32_MAX - weight)
            return sink.Fail(path.View(), "weight must be an integer from 1 through 1000.");
        total += weight;
    }

    uint32_t preceding = 0;
    for (size_t index = 0; index < count; ++index)
    {
        const auto itemScope = path.PushIndex(index);
        yyjson_val* item = yyjson_arr_get(items, index);
        if (++areaCount > kMaximumLayoutAreasPerPage)
            return sink.Fail(path.View(), "A page may contain at most 127 layout areas.");
        uint32_t weight = 1;
        if (!ReadItemWeight(item, weight, sink, path))
            return false;
        AdaptiveWidgetPlacement childPath = placementPath;
        childPath.steps[depth] = LayoutSplitStep{axis, preceding, weight, total};
        childPath.depth = depth + 1;
        const bool container = allowContainers && IsSplitContainerItem(item);
        if (!allowContainers && IsSplitContainerItem(item))
            return sink.Fail(path.View(), "widgets items must be widget values; use columns or rows for weights.");
        if (container)
        {
            if (!RejectRetiredMembers(sink, path, item) ||
                !AcceptObjectMembers(sink, path, item, {"weight", "widget", "rows", "columns"}, false))
                return false;
            const int kinds = (yyjson_obj_get(item, "widget") ? 1 : 0) + (yyjson_obj_get(item, "rows") ? 1 : 0) +
                              (yyjson_obj_get(item, "columns") ? 1 : 0);
            if (kinds != 1)
                return sink.Fail(path.View(), "A weighted item must contain exactly one of widget, rows, or columns.");
            yyjson_val* widgetValue = yyjson_obj_get(item, "widget");
            yyjson_val* rows = yyjson_obj_get(item, "rows");
            yyjson_val* columns = yyjson_obj_get(item, "columns");
            if (widgetValue)
            {
                if (page.widgets.size() >= kMaximumWidgetsPerPage)
                    return sink.Fail(path.View(), "A page may contain at most 32 widgets.");
                page.widgets.emplace_back();
                WidgetInstanceSettings& widget = page.widgets.back();
                widget.usesAdaptivePlacement = true;
                widget.adaptivePlacement = childPath;
                const auto widgetScope = path.PushName("widget");
                if (!ResolveWidget(widgetValue, declarations, settings, widget, instanceCount++, sink, path))
                    return false;
            }
            else if (rows)
            {
                const auto rowsScope = path.PushName("rows");
                if (!ParseHumanItems(rows, LayoutAxis::ShortSide, true, declarations, settings, page, childPath,
                                     depth + 1, areaCount, instanceCount, sink, path))
                    return false;
            }
            else
            {
                const auto columnsScope = path.PushName("columns");
                if (!ParseHumanItems(columns, LayoutAxis::LongSide, true, declarations, settings, page, childPath,
                                     depth + 1, areaCount, instanceCount, sink, path))
                    return false;
            }
        }
        else
        {
            if (page.widgets.size() >= kMaximumWidgetsPerPage)
                return sink.Fail(path.View(), "A page may contain at most 32 widgets.");
            page.widgets.emplace_back();
            WidgetInstanceSettings& widget = page.widgets.back();
            widget.usesAdaptivePlacement = true;
            widget.adaptivePlacement = childPath;
            if (!ResolveWidget(item, declarations, settings, widget, instanceCount++, sink, path))
                return false;
        }
        preceding += weight;
    }
    return true;
}
} // namespace

HRESULT ParseAppSettingsJsonV5(std::string_view json, std::unique_ptr<AppSettings>& output,
                               SettingsParseDiagnostic* diagnostic) noexcept
{
    output.reset();
    if (diagnostic)
    {
        *diagnostic = SettingsParseDiagnostic{};
    }
    DiagnosticSink sink{json, diagnostic};
    JsonPathBuffer path;
    try
    {
        if (json.empty())
            return sink.FailHr(path.View(), "The settings file is empty.");
        if (json.size() > 1024U * 1024U)
            return sink.FailHr(path.View(), "The settings file exceeds the 1 MiB limit.");
        std::vector<char> mutableJson(json.begin(), json.end());
        yyjson_read_err error{};
        unique_doc document{yyjson_read_opts(mutableJson.data(), mutableJson.size(),
                                             YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS, nullptr,
                                             &error)};
        if (!document)
        {
            if (diagnostic)
            {
                diagnostic->byteOffset = error.pos;
                diagnostic->line = 1;
                diagnostic->column = 1;
                diagnostic->hasLocation = true;
                diagnostic->path = "$";
                for (size_t index = 0; index < error.pos && index < json.size(); ++index)
                {
                    if (json[index] == '\n')
                    {
                        ++diagnostic->line;
                        diagnostic->column = 1;
                    }
                    else
                    {
                        ++diagnostic->column;
                    }
                }
                diagnostic->message = error.msg && error.msg[0] != '\0' ? error.msg : "Invalid JSON syntax.";
                sink.recorded = true;
            }
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        yyjson_val* root = yyjson_doc_get_root(document.get());
        if (!yyjson_is_obj(root))
            return sink.FailHr(path.View(), "The settings root must be a JSON object.");
        if (!RejectDuplicateMembers(sink, path, root))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        yyjson_val* version = yyjson_obj_get(root, "version");
        uint64_t fileMinor = 0;
        {
            const auto versionScope = path.PushName("version");
            if (!AcceptObjectMembers(sink, path, version, {"major", "minor"}, false))
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            yyjson_val* major = yyjson_obj_get(version, "major");
            yyjson_val* minor = yyjson_obj_get(version, "minor");
            if (!yyjson_is_uint(major) || yyjson_get_uint(major) != kRedXeSettingsVersionMajor)
            {
                const auto majorScope = path.PushName("major");
                return sink.FailHr(path.View(), "This RedXe build reads settings version 5 only.");
            }
            if (minor && !yyjson_is_uint(minor))
            {
                const auto minorScope = path.PushName("minor");
                return sink.FailHr(path.View(), "version.minor must be an integer.");
            }
            fileMinor = minor ? yyjson_get_uint(minor) : 0;
            if (fileMinor > UINT32_MAX)
            {
                const auto minorScope = path.PushName("minor");
                return sink.FailHr(path.View(), "version.minor is out of range.");
            }
        }
        const bool allowUnknown = fileMinor > kRedXeSettingsVersionMinor;
        if (!AcceptObjectMembers(sink, path, root,
                                 {"$schema", "version", "wrapPages", "logRetentionDays", "declare", "pages"},
                                 allowUnknown))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        yyjson_val* schema = yyjson_obj_get(root, "$schema");
        if (schema && (!yyjson_is_str(schema) || std::string_view(yyjson_get_str(schema), yyjson_get_len(schema)) !=
                                                     "RedXe.settings.schema.json"))
        {
            const auto schemaScope = path.PushName("$schema");
            return sink.FailHr(path.View(), "When present, $schema must be \"RedXe.settings.schema.json\".");
        }

        auto parsed = std::make_unique<AppSettings>();
        parsed->versionMajor = kRedXeSettingsVersionMajor;
        parsed->versionMinor = static_cast<uint32_t>(fileMinor);
        parsed->sourceDocument.assign(json);
        parsed->dashboard.gridColumns = 1;
        parsed->dashboard.gridRows = 1;
        yyjson_val* wrap = yyjson_obj_get(root, "wrapPages");
        if (wrap && !yyjson_is_bool(wrap))
        {
            const auto wrapScope = path.PushName("wrapPages");
            return sink.FailHr(path.View(), "wrapPages must be a boolean.");
        }
        parsed->dashboard.wrapPages = wrap && yyjson_get_bool(wrap);
        yyjson_val* retention = yyjson_obj_get(root, "logRetentionDays");
        if (retention)
        {
            const auto retentionScope = path.PushName("logRetentionDays");
            if (!yyjson_is_uint(retention))
                return sink.FailHr(path.View(), "logRetentionDays must be an integer.");
            const uint64_t days = yyjson_get_uint(retention);
            if (days < kRedXeMinimumLogRetentionDays || days > kRedXeMaximumLogRetentionDays)
                return sink.FailHr(path.View(), "logRetentionDays must be from 1 through 365.");
            parsed->logRetentionDays = static_cast<uint32_t>(days);
        }
        else
            parsed->logRetentionDays = kRedXeDefaultLogRetentionDays;

        std::vector<Declaration> declarations;
        yyjson_val* declarationObject = yyjson_obj_get(root, "declare");
        if (declarationObject)
        {
            const auto declareScope = path.PushName("declare");
            if (!yyjson_is_obj(declarationObject))
                return sink.FailHr(path.View(), "declare must be a JSON object.");
            if (yyjson_obj_size(declarationObject) > kMaximumSettingsDeclarations)
                return sink.FailHr(path.View(), "declare may contain at most 128 entries.");
            yyjson_obj_iter iterator = yyjson_obj_iter_with(declarationObject);
            while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
            {
                const std::string_view name(yyjson_get_str(key), yyjson_get_len(key));
                const auto nameScope = path.PushName(name);
                if (name.empty() || name.size() > kMaximumSettingsTextBytes || Utf8CodePointCount(name) > 128)
                    return sink.FailHr(path.View(), "A declare name must be 1 through 128 Unicode code points.");
                yyjson_val* definition = yyjson_obj_iter_get_val(key);
                if (!yyjson_is_obj(definition))
                    return sink.FailHr(path.View(), "A declare entry must be a JSON object.");
                declarations.push_back(Declaration{name, definition});
            }
            for (size_t index = 0; index < declarations.size(); ++index)
            {
                WidgetInstanceSettings validated{};
                const auto nameScope = path.PushName(declarations[index].name);
                if (!ParseFlattenedPluginObject(declarations[index].definition, *parsed, validated,
                                                static_cast<uint32_t>(index), sink, path))
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }

        yyjson_val* pages = yyjson_obj_get(root, "pages");
        {
            const auto pagesScope = path.PushName("pages");
            if (!yyjson_is_arr(pages))
                return sink.FailHr(path.View(), "pages must be an array.");
            const size_t pageCount = yyjson_arr_size(pages);
            if (pageCount == 0)
                return sink.FailHr(path.View(), "At least one page is required.");
            if (pageCount > kMaximumDashboardPages)
                return sink.FailHr(path.View(), "At most 16 pages are allowed.");
            parsed->dashboard.pages.resize(pageCount);
            uint32_t instanceCount = 0;
            for (size_t index = 0; index < pageCount; ++index)
            {
                const auto pageScope = path.PushIndex(index);
                yyjson_val* pageValue = yyjson_arr_get(pages, index);
                if (!RejectRetiredMembers(sink, path, pageValue))
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                yyjson_val* widgets = yyjson_obj_get(pageValue, "widgets");
                yyjson_val* columns = yyjson_obj_get(pageValue, "columns");
                yyjson_val* rows = yyjson_obj_get(pageValue, "rows");
                yyjson_val* along = yyjson_obj_get(pageValue, "along");
                const int shapes = (widgets ? 1 : 0) + (columns ? 1 : 0) + (rows ? 1 : 0);
                if (shapes > 1)
                    return sink.FailHr(path.View(), "A page may use only one of widgets, columns, or rows.");
                if (along && !widgets)
                {
                    const auto alongScope = path.PushName("along");
                    return sink.FailHr(path.View(), "along is valid only with widgets.");
                }
                if (widgets)
                {
                    if (!AcceptObjectMembers(sink, path, pageValue, {"id", "name", "along", "widgets"}, allowUnknown))
                        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
                else if (columns)
                {
                    if (!AcceptObjectMembers(sink, path, pageValue, {"id", "name", "columns"}, allowUnknown))
                        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
                else if (rows)
                {
                    if (!AcceptObjectMembers(sink, path, pageValue, {"id", "name", "rows"}, allowUnknown))
                        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
                else if (!AcceptObjectMembers(sink, path, pageValue, {"id", "name"}, allowUnknown))
                {
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
                DashboardPageSettings& page = parsed->dashboard.pages[index];
                yyjson_val* id = yyjson_obj_get(pageValue, "id");
                yyjson_val* name = yyjson_obj_get(pageValue, "name");
                std::array<char, 32> generated{};
                const int generatedLength = sprintf_s(generated.data(), generated.size(), "page.%zu", index + 1);
                if (id && (!yyjson_is_str(id) ||
                           !CopyText(std::string_view(yyjson_get_str(id), yyjson_get_len(id)), page.id, true)))
                {
                    const auto idScope = path.PushName("id");
                    return sink.FailHr(path.View(), "id must be a machine-safe string of at most 128 code points.");
                }
                if (!id &&
                    !CopyText(std::string_view(generated.data(), static_cast<size_t>(generatedLength)), page.id, true))
                    return sink.FailHr(path.View(), "A page id could not be assigned.");
                if (name && (!yyjson_is_str(name) ||
                             !CopyText(std::string_view(yyjson_get_str(name), yyjson_get_len(name)), page.name, false)))
                {
                    const auto nameScope = path.PushName("name");
                    return sink.FailHr(path.View(), "name must be a string of 1 through 128 Unicode code points.");
                }
                if (!name)
                {
                    const int length = sprintf_s(generated.data(), generated.size(), "Page %zu", index + 1);
                    if (!CopyText(std::string_view(generated.data(), static_cast<size_t>(length)), page.name, false))
                        return sink.FailHr(path.View(), "A page name could not be assigned.");
                }
                uint32_t areaCount = 0;
                if (widgets)
                {
                    LayoutAxis axis = LayoutAxis::LongSide;
                    if (along)
                    {
                        const auto alongScope = path.PushName("along");
                        if (!yyjson_is_str(along) ||
                            (std::string_view(yyjson_get_str(along), yyjson_get_len(along)) != "long-side" &&
                             std::string_view(yyjson_get_str(along), yyjson_get_len(along)) != "short-side"))
                            return sink.FailHr(path.View(), "along must be long-side or short-side.");
                        if (std::string_view(yyjson_get_str(along), yyjson_get_len(along)) == "short-side")
                            axis = LayoutAxis::ShortSide;
                    }
                    const auto widgetsScope = path.PushName("widgets");
                    if (!ParseHumanItems(widgets, axis, false, declarations, *parsed, page, {}, 0, areaCount,
                                         instanceCount, sink, path))
                        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
                else if (columns)
                {
                    const auto columnsScope = path.PushName("columns");
                    if (!ParseHumanItems(columns, LayoutAxis::LongSide, true, declarations, *parsed, page, {}, 0,
                                         areaCount, instanceCount, sink, path))
                        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
                else if (rows)
                {
                    const auto rowsScope = path.PushName("rows");
                    if (!ParseHumanItems(rows, LayoutAxis::ShortSide, true, declarations, *parsed, page, {}, 0,
                                         areaCount, instanceCount, sink, path))
                        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
                page.widgetCount = static_cast<uint32_t>(page.widgets.size());
            }
            parsed->dashboard.pageCount = static_cast<uint32_t>(pageCount);
        }
        parsed->dashboard.activePageIndex = 0;
        parsed->dashboard.activePageId = parsed->dashboard.pages[0].id;
        output = std::move(parsed);
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

HRESULT ParseAppSettingsJsonV4(std::string_view json, std::unique_ptr<AppSettings>& output,
                               SettingsParseDiagnostic* diagnostic) noexcept
{
    output.reset();
    if (diagnostic)
    {
        *diagnostic = SettingsParseDiagnostic{};
        diagnostic->path = "$.version.major";
        diagnostic->message = "This RedXe build reads settings version 5 only.";
    }
    (void)json;
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}
