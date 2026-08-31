#include "Settings.h"

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
constexpr char kMatrixDefaults[] =
    R"json({"seed":1999,"glyphHeightDips":18,"densityPercent":70,"speedPercent":100,"trailLengthGlyphs":18,"mutationPerSecond":8,"headColor":"#D8FFE5","trailColor":"#00E65C","backgroundColor":"#010502","glowPercent":35})json";

struct Declaration final
{
    std::string_view name;
    yyjson_val* definition = nullptr;
};

[[nodiscard]] std::size_t Utf8CodePointCount(std::string_view text) noexcept
{
    std::size_t count = 0;
    for (const unsigned char value : text)
    {
        if ((value & 0xC0U) != 0x80U)
            ++count;
    }
    return count;
}

[[nodiscard]] bool IsKnownPlugin(std::string_view plugin, const char*& typeId) noexcept
{
    if (plugin == kTrianglePlugin)
    {
        typeId = "rotating-triangle";
        return true;
    }
    if (plugin == kGdiPlugin)
    {
        typeId = "gdi-orbit";
        return true;
    }
    if (plugin == kMatrixPlugin)
    {
        typeId = "matrix-rain";
        return true;
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
    result.bytes = static_cast<std::uint32_t>(source.size());
    if (machineId && !RedXeIsValidMachineId(result.utf8.data()))
    {
        return false;
    }
    destination = result;
    return true;
}

[[nodiscard]] bool ObjectHasOnly(yyjson_val* object, std::initializer_list<const char*> keys,
                                 bool allowUnknown) noexcept
{
    if (!yyjson_is_obj(object))
    {
        return false;
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
        if (!known && !allowUnknown)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool HasDuplicateMembers(yyjson_val* value)
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
                return true;
            if (HasDuplicateMembers(yyjson_obj_iter_get_val(key)))
            {
                return true;
            }
        }
    }
    else if (yyjson_is_arr(value))
    {
        const std::size_t count = yyjson_arr_size(value);
        for (std::size_t index = 0; index < count; ++index)
        {
            if (HasDuplicateMembers(yyjson_arr_get(value, index)))
            {
                return true;
            }
        }
    }
    return false;
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
        const std::size_t length = yyjson_get_len(key);
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
    std::size_t bytes = 0;
    unique_json json{yyjson_mut_write(mutableDocument.get(), YYJSON_WRITE_NOFLAG, &bytes)};
    if (!json || bytes == 0 || bytes > kPrivateConfigurationCapacity)
    {
        return false;
    }
    JsonObjectSettings copied{};
    std::memcpy(copied.utf8.data(), json.get(), bytes);
    copied.utf8[bytes] = '\0';
    copied.bytes = static_cast<std::uint32_t>(bytes);
    destination = copied;
    return true;
}

[[nodiscard]] bool ValidateMatrixSettings(yyjson_val* settings) noexcept
{
    if (!ObjectHasOnly(settings,
                       {"seed", "glyphHeightDips", "densityPercent", "speedPercent", "trailLengthGlyphs",
                        "mutationPerSecond", "headColor", "trailColor", "backgroundColor", "glowPercent"},
                       false))
    {
        return false;
    }
    auto inRange = [settings](const char* key, std::uint64_t minimum, std::uint64_t maximum) noexcept
    {
        yyjson_val* value = yyjson_obj_get(settings, key);
        return yyjson_is_uint(value) && yyjson_get_uint(value) >= minimum && yyjson_get_uint(value) <= maximum;
    };
    auto color = [settings](const char* key) noexcept
    {
        yyjson_val* value = yyjson_obj_get(settings, key);
        const char* text = yyjson_is_str(value) ? yyjson_get_str(value) : nullptr;
        if (!text || yyjson_get_len(value) != 7 || text[0] != '#')
            return false;
        for (std::size_t index = 1; index < 7; ++index)
        {
            if (!std::isxdigit(static_cast<unsigned char>(text[index])))
                return false;
        }
        return true;
    };
    return yyjson_obj_size(settings) == 10 && inRange("seed", 0, UINT32_MAX) && inRange("glyphHeightDips", 12, 48) &&
           inRange("densityPercent", 10, 100) && inRange("speedPercent", 25, 300) &&
           inRange("trailLengthGlyphs", 6, 48) && inRange("mutationPerSecond", 0, 30) &&
           inRange("glowPercent", 0, 100) && color("headColor") && color("trailColor") && color("backgroundColor");
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
    for (std::uint32_t index = 0; index < settings.pluginCount; ++index)
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

[[nodiscard]] bool ParseWidgetDefinition(yyjson_val* definition, AppSettings& settings, WidgetInstanceSettings& widget,
                                         std::uint32_t instanceIndex) noexcept
{
    if (!ObjectHasOnly(definition, {"plugin", "settings"}, false))
        return false;
    yyjson_val* pluginValue = yyjson_obj_get(definition, "plugin");
    if (!yyjson_is_str(pluginValue))
        return false;
    const std::string_view plugin(yyjson_get_str(pluginValue), yyjson_get_len(pluginValue));
    const char* typeId = nullptr;
    if (!IsKnownPlugin(plugin, typeId) || !AddUsedPlugin(settings, plugin) ||
        !CopyText(plugin, widget.pluginId, true) || !CopyText(typeId, widget.typeId, true))
        return false;

    std::array<char, 32> instance{};
    const int written = sprintf_s(instance.data(), instance.size(), "widget.%u", instanceIndex + 1);
    if (written <= 0 ||
        !CopyText(std::string_view(instance.data(), static_cast<std::size_t>(written)), widget.id, true))
        return false;

    yyjson_val* settingsValue = yyjson_obj_get(definition, "settings");
    unique_doc defaults;
    unique_mut_doc effectiveDocument;
    unique_json effectiveJson;
    unique_doc effectiveImmutable;
    const char* defaultsText = plugin == kMatrixPlugin ? kMatrixDefaults : "{}";
    defaults.reset(yyjson_read(defaultsText, std::strlen(defaultsText), YYJSON_READ_NOFLAG));
    if (!settingsValue)
    {
        settingsValue = defaults ? yyjson_doc_get_root(defaults.get()) : nullptr;
    }
    else if (plugin == kMatrixPlugin)
    {
        effectiveDocument.reset(yyjson_mut_doc_new(nullptr));
        yyjson_mut_val* merged =
            effectiveDocument ? MergeValue(effectiveDocument.get(), yyjson_doc_get_root(defaults.get()), settingsValue)
                              : nullptr;
        if (!merged)
            return false;
        yyjson_mut_doc_set_root(effectiveDocument.get(), merged);
        std::size_t effectiveBytes = 0;
        effectiveJson.reset(yyjson_mut_write(effectiveDocument.get(), YYJSON_WRITE_NOFLAG, &effectiveBytes));
        effectiveImmutable.reset(effectiveJson ? yyjson_read(effectiveJson.get(), effectiveBytes, YYJSON_READ_NOFLAG)
                                               : nullptr);
        settingsValue = effectiveImmutable ? yyjson_doc_get_root(effectiveImmutable.get()) : nullptr;
    }
    if (!yyjson_is_obj(settingsValue))
        return false;
    if (plugin == kMatrixPlugin ? !ValidateMatrixSettings(settingsValue) : yyjson_obj_size(settingsValue) != 0)
        return false;
    return CompactObject(settingsValue, widget.privateConfiguration);
}

[[nodiscard]] bool ResolveWidget(yyjson_val* authored, const std::vector<Declaration>& declarations,
                                 AppSettings& settings, WidgetInstanceSettings& widget,
                                 std::uint32_t instanceIndex) noexcept
{
    if (yyjson_is_str(authored))
    {
        yyjson_val* declaration =
            FindDeclaration(declarations, std::string_view(yyjson_get_str(authored), yyjson_get_len(authored)));
        return declaration && ParseWidgetDefinition(declaration, settings, widget, instanceIndex);
    }
    if (!yyjson_is_obj(authored))
        return false;
    if (yyjson_obj_get(authored, "plugin"))
        return ParseWidgetDefinition(authored, settings, widget, instanceIndex);
    if (!ObjectHasOnly(authored, {"use", "override"}, false))
        return false;
    yyjson_val* use = yyjson_obj_get(authored, "use");
    yyjson_val* overrideValue = yyjson_obj_get(authored, "override");
    if (!yyjson_is_str(use) || !yyjson_is_obj(overrideValue))
        return false;
    yyjson_val* declaration = FindDeclaration(declarations, std::string_view(yyjson_get_str(use), yyjson_get_len(use)));
    if (!declaration)
        return false;
    unique_mut_doc mergedDocument{yyjson_mut_doc_new(nullptr)};
    yyjson_mut_val* merged = mergedDocument ? MergeValue(mergedDocument.get(), declaration, overrideValue) : nullptr;
    if (!merged)
        return false;
    yyjson_mut_doc_set_root(mergedDocument.get(), merged);
    std::size_t bytes = 0;
    unique_json json{yyjson_mut_write(mergedDocument.get(), YYJSON_WRITE_NOFLAG, &bytes)};
    unique_doc immutable{json ? yyjson_read(json.get(), bytes, YYJSON_READ_NOFLAG) : nullptr};
    return immutable && ParseWidgetDefinition(yyjson_doc_get_root(immutable.get()), settings, widget, instanceIndex);
}

[[nodiscard]] bool ParseLayout(yyjson_val* layout, const std::vector<Declaration>& declarations, AppSettings& settings,
                               DashboardPageSettings& page, AdaptiveWidgetPlacement path, std::uint32_t depth,
                               std::uint32_t& areaCount, std::uint32_t& instanceCount, bool nestedArea) noexcept
{
    if (depth >= kMaximumLayoutDepth ||
        !(nestedArea ? ObjectHasOnly(layout, {"sizeRatio", "arrangeAlong", "areas"}, false)
                     : ObjectHasOnly(layout, {"arrangeAlong", "areas"}, false)))
        return false;
    yyjson_val* axisValue = yyjson_obj_get(layout, "arrangeAlong");
    yyjson_val* areas = yyjson_obj_get(layout, "areas");
    if (!yyjson_is_str(axisValue) || !yyjson_is_arr(areas) || yyjson_arr_size(areas) == 0)
        return false;
    const std::string_view axisText(yyjson_get_str(axisValue), yyjson_get_len(axisValue));
    LayoutAxis axis{};
    if (axisText == "long-side")
        axis = LayoutAxis::LongSide;
    else if (axisText == "short-side")
        axis = LayoutAxis::ShortSide;
    else
        return false;

    std::uint32_t total = 0;
    for (std::size_t index = 0; index < yyjson_arr_size(areas); ++index)
    {
        yyjson_val* ratio = yyjson_obj_get(yyjson_arr_get(areas, index), "sizeRatio");
        if (!yyjson_is_uint(ratio) || yyjson_get_uint(ratio) == 0 || yyjson_get_uint(ratio) > 1000 ||
            total > UINT32_MAX - static_cast<std::uint32_t>(yyjson_get_uint(ratio)))
            return false;
        total += static_cast<std::uint32_t>(yyjson_get_uint(ratio));
    }
    std::uint32_t preceding = 0;
    for (std::size_t index = 0; index < yyjson_arr_size(areas); ++index)
    {
        yyjson_val* area = yyjson_arr_get(areas, index);
        if (!yyjson_is_obj(area) || ++areaCount > kMaximumLayoutAreasPerPage)
            return false;
        const std::uint32_t ratio = static_cast<std::uint32_t>(yyjson_get_uint(yyjson_obj_get(area, "sizeRatio")));
        AdaptiveWidgetPlacement childPath = path;
        childPath.steps[depth] = LayoutSplitStep{axis, preceding, ratio, total};
        childPath.depth = depth + 1;
        yyjson_val* widgetValue = yyjson_obj_get(area, "widget");
        yyjson_val* childAreas = yyjson_obj_get(area, "areas");
        if (widgetValue && !childAreas)
        {
            if (!ObjectHasOnly(area, {"sizeRatio", "widget"}, false) || page.widgets.size() >= kMaximumWidgetsPerPage)
                return false;
            page.widgets.emplace_back();
            WidgetInstanceSettings& widget = page.widgets.back();
            widget.usesAdaptivePlacement = true;
            widget.adaptivePlacement = childPath;
            if (!ResolveWidget(widgetValue, declarations, settings, widget, instanceCount++))
                return false;
        }
        else if (!widgetValue && childAreas)
        {
            if (!ObjectHasOnly(area, {"sizeRatio", "arrangeAlong", "areas"}, false) ||
                !ParseLayout(area, declarations, settings, page, childPath, depth + 1, areaCount, instanceCount, true))
                return false;
        }
        else
            return false;
        preceding += ratio;
    }
    return true;
}
} // namespace

HRESULT ParseAppSettingsJsonV4(std::string_view json, std::unique_ptr<AppSettings>& output,
                               SettingsParseDiagnostic* diagnostic) noexcept
{
    output.reset();
    if (diagnostic)
    {
        *diagnostic = SettingsParseDiagnostic{};
        diagnostic->message = "The settings document does not match the supported version 4 schema.";
    }
    try
    {
        if (json.empty() || json.size() > 1024U * 1024U)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        std::vector<char> mutableJson(json.begin(), json.end());
        yyjson_read_err error{};
        unique_doc document{yyjson_read_opts(mutableJson.data(), mutableJson.size(),
                                             YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS, nullptr,
                                             &error)};
        if (!document && diagnostic)
        {
            diagnostic->byteOffset = error.pos;
            diagnostic->line = 1;
            diagnostic->column = 1;
            for (std::size_t index = 0; index < error.pos && index < json.size(); ++index)
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
            diagnostic->message = error.msg ? error.msg : "Invalid JSON syntax.";
        }
        yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
        if (!yyjson_is_obj(root) || HasDuplicateMembers(root))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        yyjson_val* version = yyjson_obj_get(root, "version");
        yyjson_val* major = yyjson_is_obj(version) ? yyjson_obj_get(version, "major") : nullptr;
        yyjson_val* minor = yyjson_is_obj(version) ? yyjson_obj_get(version, "minor") : nullptr;
        if (!ObjectHasOnly(version, {"major", "minor"}, false) || !yyjson_is_uint(major) ||
            yyjson_get_uint(major) != kRedXeSettingsVersionMajor || (minor && !yyjson_is_uint(minor)))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        const std::uint64_t fileMinor = minor ? yyjson_get_uint(minor) : 0;
        if (fileMinor > UINT32_MAX)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        const bool allowUnknown = fileMinor > kRedXeSettingsVersionMinor;
        if (!ObjectHasOnly(root, {"$schema", "version", "wrapPages", "declare", "pages"}, allowUnknown))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        yyjson_val* schema = yyjson_obj_get(root, "$schema");
        if (schema && (!yyjson_is_str(schema) || std::string_view(yyjson_get_str(schema), yyjson_get_len(schema)) !=
                                                     "RedXe.settings.schema.json"))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        auto parsed = std::make_unique<AppSettings>();
        parsed->versionMajor = kRedXeSettingsVersionMajor;
        parsed->versionMinor = static_cast<std::uint32_t>(fileMinor);
        parsed->sourceDocument.assign(json);
        parsed->dashboard.gridColumns = 1;
        parsed->dashboard.gridRows = 1;
        yyjson_val* wrap = yyjson_obj_get(root, "wrapPages");
        if (wrap && !yyjson_is_bool(wrap))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        parsed->dashboard.wrapPages = wrap && yyjson_get_bool(wrap);

        std::vector<Declaration> declarations;
        yyjson_val* declarationObject = yyjson_obj_get(root, "declare");
        if (declarationObject)
        {
            if (!yyjson_is_obj(declarationObject) || yyjson_obj_size(declarationObject) > kMaximumSettingsDeclarations)
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            yyjson_obj_iter iterator = yyjson_obj_iter_with(declarationObject);
            while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
            {
                const std::string_view name(yyjson_get_str(key), yyjson_get_len(key));
                if (name.empty() || name.size() > kMaximumSettingsTextBytes || Utf8CodePointCount(name) > 128 ||
                    !ObjectHasOnly(yyjson_obj_iter_get_val(key), {"plugin", "settings"}, false))
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                declarations.push_back(Declaration{name, yyjson_obj_iter_get_val(key)});
            }
            for (std::size_t index = 0; index < declarations.size(); ++index)
            {
                WidgetInstanceSettings validated{};
                if (!ParseWidgetDefinition(declarations[index].definition, *parsed, validated,
                                           static_cast<std::uint32_t>(index)))
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }

        yyjson_val* pages = yyjson_obj_get(root, "pages");
        const std::size_t pageCount = yyjson_is_arr(pages) ? yyjson_arr_size(pages) : 0;
        if (pageCount == 0 || pageCount > kMaximumDashboardPages)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        parsed->dashboard.pages.resize(pageCount);
        std::uint32_t instanceCount = 0;
        for (std::size_t index = 0; index < pageCount; ++index)
        {
            yyjson_val* pageValue = yyjson_arr_get(pages, index);
            if (!ObjectHasOnly(pageValue, {"id", "name", "layout"}, allowUnknown))
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            DashboardPageSettings& page = parsed->dashboard.pages[index];
            yyjson_val* id = yyjson_obj_get(pageValue, "id");
            yyjson_val* name = yyjson_obj_get(pageValue, "name");
            std::array<char, 32> generated{};
            const int generatedLength = sprintf_s(generated.data(), generated.size(), "page.%zu", index + 1);
            if ((id && (!yyjson_is_str(id) ||
                        !CopyText(std::string_view(yyjson_get_str(id), yyjson_get_len(id)), page.id, true))) ||
                (!id && !CopyText(std::string_view(generated.data(), static_cast<std::size_t>(generatedLength)),
                                  page.id, true)) ||
                (name && (!yyjson_is_str(name) ||
                          !CopyText(std::string_view(yyjson_get_str(name), yyjson_get_len(name)), page.name, false))))
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            if (!name)
            {
                const int length = sprintf_s(generated.data(), generated.size(), "Page %zu", index + 1);
                if (!CopyText(std::string_view(generated.data(), static_cast<std::size_t>(length)), page.name, false))
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            yyjson_val* layout = yyjson_obj_get(pageValue, "layout");
            std::uint32_t areaCount = 0;
            if (layout && !ParseLayout(layout, declarations, *parsed, page, {}, 0, areaCount, instanceCount, false))
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            page.widgetCount = static_cast<std::uint32_t>(page.widgets.size());
        }
        parsed->dashboard.pageCount = static_cast<std::uint32_t>(pageCount);
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
