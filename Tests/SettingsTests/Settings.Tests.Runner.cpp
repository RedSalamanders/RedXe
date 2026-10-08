#include "../../RedXe/Settings.h"
#include "../../Common/FailureReports.h"
#include "../../Plugins/Launcher/LauncherPaging.h"
#include "../../Plugins/StudioClock/StudioClockSettings.h"
#include "../../RedXe/BundledPlugins.h"
#include "../../RedXe/CommandLine.h"
#include "../../RedXe/DockOptions.h"
#include "../../RedXe/SettingsWatcher.h"
#include "Settings.Tests.ReleasedTemplates.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <string_view>
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

constexpr std::string_view kRepresentative = R"json(
{
  "version": { "major": 5, "minor": 0 },
  "wrapPages": true,
  "declare": {
    "Matrix": { "plugin": "builtin.matrix-rain", "seed": 7, "densityPercent": 60 },
    "Triangle": { "plugin": "builtin.rotating-triangle" },
  },
  "pages": [
    {
      "id": "main",
      "columns": [
        { "weight": 2, "widget": "Triangle" },
        { "rows": ["Matrix", { "use": "Matrix", "seed": 9, "densityPercent": null }] }
      ]
    },
    {},
  ]
})json";

[[nodiscard]] HRESULT GetDeployedPath(const wchar_t* name, std::filesystem::path& path) noexcept
{
    std::array<wchar_t, 32768> module{};
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length == 0 || length >= module.size())
        return HRESULT_FROM_WIN32(GetLastError());
    path = std::filesystem::path(module.data()).parent_path() / L"Settings" / name;
    return S_OK;
}

[[nodiscard]] HRESULT ReadFile(const std::filesystem::path& path, std::string& bytes) noexcept
{
    try
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT ExpectRejected(std::string_view json) noexcept
{
    AppSettings settings{};
    return FAILED(ParseAppSettingsJson(json, settings)) ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

[[nodiscard]] bool HasWidgetExample(const AppSettings& settings, std::string_view pluginId) noexcept
{
    for (uint32_t pageIndex = 0; pageIndex < settings.dashboard.pageCount; ++pageIndex)
    {
        const DashboardPageSettings& page = settings.dashboard.pages[pageIndex];
        for (uint32_t widgetIndex = 0; widgetIndex < page.widgetCount; ++widgetIndex)
        {
            if (SettingsIdEquals(page.widgets[widgetIndex].pluginId.View(), pluginId))
                return true;
        }
    }
    return false;
}

[[nodiscard]] bool IsOptInBundledWidget(std::string_view pluginId) noexcept
{
    for (const char* optIn : kRedXeOptInBundledWidgetIds)
    {
        if (SettingsIdEquals(optIn, pluginId))
            return true;
    }
    return false;
}

[[nodiscard]] bool IsDebugOnlyBundledWidget(std::string_view pluginId) noexcept
{
    for (const char* debugOnly : kRedXeDebugOnlyBundledWidgetIds)
    {
        if (SettingsIdEquals(debugOnly, pluginId))
            return true;
    }
    return false;
}

// Every bundled widget is placed in both shipped templates, except the catalog's opt-in native-window examples,
// which stay out of every shipped page (Core_Settings.md) and are covered by HostPluginTests instead, and the
// Debug-only widgets, which only the Debug template places. Every catalogued service is configured by both, and no
// retired one.
[[nodiscard]] bool CoversBundledPluginCatalog(const AppSettings& settings, bool debugTemplate) noexcept
{
    const size_t debugOnlyExcluded = debugTemplate ? 0 : kRedXeDebugOnlyBundledWidgetIds.size();
    if (settings.pluginCount != kRedXeBundledWidgets.size() - kRedXeOptInBundledWidgetIds.size() - debugOnlyExcluded)
        return false;
    for (const RedXeBundledWidgetSpec& plugin : kRedXeBundledWidgets)
    {
        if (IsOptInBundledWidget(plugin.pluginId) || (!debugTemplate && IsDebugOnlyBundledWidget(plugin.pluginId)))
        {
            if (FindPluginSettings(settings, plugin.pluginId) || HasWidgetExample(settings, plugin.pluginId))
                return false;
            continue;
        }
        if (!FindPluginSettings(settings, plugin.pluginId) || !HasWidgetExample(settings, plugin.pluginId))
            return false;
    }
    if (settings.serviceCount != kRedXeBundledServices.size() || !settings.retiredServices.empty())
        return false;
    for (const RedXeBundledServiceSpec& service : kRedXeBundledServices)
    {
        if (!FindServiceSettings(settings, service.pluginId))
            return false;
    }
    return true;
}

[[nodiscard]] bool SchemaAcceptsPlugin(yyjson_val* root, std::string_view pluginId) noexcept
{
    yyjson_val* definitions = yyjson_is_obj(root) ? yyjson_obj_get(root, "$defs") : nullptr;
    yyjson_val* widgetDefinition =
        yyjson_is_obj(definitions) ? yyjson_obj_get(definitions, "widgetDefinition") : nullptr;
    yyjson_val* alternatives = yyjson_is_obj(widgetDefinition) ? yyjson_obj_get(widgetDefinition, "oneOf") : nullptr;
    const size_t count = yyjson_is_arr(alternatives) ? yyjson_arr_size(alternatives) : 0;
    for (size_t index = 0; index < count; ++index)
    {
        yyjson_val* alternative = yyjson_arr_get(alternatives, index);
        yyjson_val* properties = yyjson_is_obj(alternative) ? yyjson_obj_get(alternative, "properties") : nullptr;
        yyjson_val* plugin = yyjson_is_obj(properties) ? yyjson_obj_get(properties, "plugin") : nullptr;
        yyjson_val* constant = yyjson_is_obj(plugin) ? yyjson_obj_get(plugin, "const") : nullptr;
        if (yyjson_is_str(constant) && pluginId == std::string_view{yyjson_get_str(constant), yyjson_get_len(constant)})
            return true;

        yyjson_val* values = yyjson_is_obj(plugin) ? yyjson_obj_get(plugin, "enum") : nullptr;
        const size_t valueCount = yyjson_is_arr(values) ? yyjson_arr_size(values) : 0;
        for (size_t valueIndex = 0; valueIndex < valueCount; ++valueIndex)
        {
            yyjson_val* value = yyjson_arr_get(values, valueIndex);
            if (yyjson_is_str(value) && pluginId == std::string_view{yyjson_get_str(value), yyjson_get_len(value)})
                return true;
        }
    }
    return false;
}

// A Studio Clock settings schema (the Specs `studioClockSettings` definition, or the schema StudioClock.dll publishes)
// carries exactly the members, date formats, and glowPercent range of StudioClockSettings.h; given the catalog
// defaults, every member's `default` is the catalog value.
[[nodiscard]] bool StudioClockSchemaMatchesCatalog(yyjson_val* schema, yyjson_val* catalogDefaults) noexcept
{
    yyjson_val* members = yyjson_obj_get(schema, "properties");
    if (!yyjson_is_false(yyjson_obj_get(schema, "additionalProperties")) || !yyjson_is_obj(members) ||
        yyjson_obj_size(members) != StudioClock::kSettingsKeys.size())
        return false;
    for (const char* key : StudioClock::kSettingsKeys)
    {
        yyjson_val* member = yyjson_obj_get(members, key);
        if (!yyjson_is_obj(member) || (catalogDefaults && !yyjson_equals(yyjson_obj_get(member, "default"),
                                                                         yyjson_obj_get(catalogDefaults, key))))
            return false;
    }
    yyjson_val* glowPercent = yyjson_obj_get(members, "glowPercent");
    yyjson_val* minimum = yyjson_obj_get(glowPercent, "minimum");
    yyjson_val* maximum = yyjson_obj_get(glowPercent, "maximum");
    yyjson_val* dateFormats = yyjson_obj_get(yyjson_obj_get(members, "dateFormat"), "enum");
    if (!yyjson_is_uint(minimum) || yyjson_get_uint(minimum) != StudioClock::kMinimumGlowPercent ||
        !yyjson_is_uint(maximum) || yyjson_get_uint(maximum) != StudioClock::kMaximumGlowPercent ||
        yyjson_arr_size(dateFormats) != StudioClock::kDateFormatNames.size())
        return false;
    for (size_t index = 0; index < StudioClock::kDateFormatNames.size(); ++index)
    {
        const std::string_view name = StudioClock::kDateFormatNames[index];
        if (!yyjson_equals_strn(yyjson_arr_get(dateFormats, index), name.data(), name.size()))
            return false;
    }
    return true;
}

// The Specs widget variant for Studio Clock offers the host backgroundColor and every catalog member, each a reference
// to its `studioClockSettings` property.
[[nodiscard]] bool StudioClockVariantMatchesCatalog(yyjson_val* root) noexcept
{
    constexpr std::string_view referencePrefix = "#/$defs/studioClockSettings/properties/";
    yyjson_val* variants = yyjson_obj_get(yyjson_obj_get(yyjson_obj_get(root, "$defs"), "widgetDefinition"), "oneOf");
    size_t index = 0;
    size_t count = 0;
    yyjson_val* variant = nullptr;
    yyjson_arr_foreach(variants, index, count, variant)
    {
        yyjson_val* properties = yyjson_obj_get(variant, "properties");
        if (!yyjson_equals_str(yyjson_obj_get(yyjson_obj_get(properties, "plugin"), "const"), StudioClock::kPluginId))
            continue;
        if (yyjson_obj_size(properties) != StudioClock::kSettingsKeys.size() + 2 ||
            !yyjson_obj_get(properties, "backgroundColor"))
            return false;
        for (const char* key : StudioClock::kSettingsKeys)
        {
            yyjson_val* reference = yyjson_obj_get(yyjson_obj_get(properties, key), "$ref");
            const std::string_view text = yyjson_is_str(reference)
                                              ? std::string_view{yyjson_get_str(reference), yyjson_get_len(reference)}
                                              : std::string_view{};
            if (!text.starts_with(referencePrefix) || text.substr(referencePrefix.size()) != key)
                return false;
        }
        return true;
    }
    return false;
}

// Whether the schema text points at definition `name` with a "#/$defs/<name>" or "#/$defs/<name>/..." reference.
[[nodiscard]] bool SchemaReferencesDefinition(std::string_view schema, std::string_view name) noexcept
{
    constexpr std::string_view pointerPrefix = "\"#/$defs/";
    for (size_t at = schema.find(pointerPrefix); at != std::string_view::npos; at = schema.find(pointerPrefix, at + 1))
    {
        const std::string_view rest = schema.substr(at + pointerPrefix.size());
        if (rest.size() > name.size() && rest.starts_with(name) &&
            (rest[name.size()] == '"' || rest[name.size()] == '/'))
            return true;
    }
    return false;
}

[[nodiscard]] HRESULT ValidateTemplatesAndSchema() noexcept
{
    std::filesystem::path debugPath;
    std::filesystem::path releasePath;
    std::filesystem::path schemaPath;
    HRESULT result = GetDeployedPath(kRedXeDebugSettingsFileName, debugPath);
    if (SUCCEEDED(result))
        result = GetDeployedPath(kRedXeReleaseSettingsFileName, releasePath);
    if (SUCCEEDED(result))
        result = GetDeployedPath(kRedXeSettingsSchemaFileName, schemaPath);
    if (FAILED(result))
        return result;

    AppSettings debug{};
    AppSettings release{};
    result = LoadAppSettingsFile(debugPath.wstring(), debug);
    if (SUCCEEDED(result))
        result = LoadAppSettingsFile(releasePath.wstring(), release);
    if (FAILED(result))
    {
        std::wprintf(L"Deployed settings templates did not load.\n");
        return result;
    }
    if (!CoversBundledPluginCatalog(debug, true) || !CoversBundledPluginCatalog(release, false))
    {
        std::wprintf(L"Deployed templates do not cover the bundled plugin catalog.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    // Development page: Launcher, Triangle, Matrix. The Debug template adds a Logicon page for the developer-only
    // monitor tile. Both templates end with a full-canvas 5H4D3R5 page. No shipped page places a native-window
    // widget.
    if (debug.dashboard.pageCount != 5 || debug.dashboard.pages[0].widgetCount != 3)
    {
        std::wprintf(L"Debug template page count contract failed (%u pages, first widgets %u).\n",
                     debug.dashboard.pageCount, debug.dashboard.pages[0].widgetCount);
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (debug.dashboard.pages[0].widgets[0].pluginId.View() != "builtin.launcher" ||
        !debug.dashboard.pages[0].widgets[0].usesAdaptivePlacement)
    {
        std::wprintf(L"Debug template first widget is not an adaptive launcher.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (debug.dashboard.pages[1].widgetCount != 2 || debug.dashboard.pages[1].id.View() != "logicon" ||
        debug.dashboard.pages[1].widgets[0].pluginId.View() != "builtin.logicon-monitor" ||
        debug.dashboard.pages[2].widgetCount != 7 || debug.dashboard.pages[3].widgetCount != 10 ||
        debug.dashboard.pages[4].widgetCount != 1 ||
        debug.dashboard.pages[4].widgets[0].pluginId.View() != "builtin.5h4d3r5" || release.dashboard.pageCount != 4 ||
        release.dashboard.pages[0].widgetCount != 1 || release.dashboard.pages[1].widgetCount != 7 ||
        release.dashboard.pages[2].widgetCount != 10 || release.dashboard.pages[3].widgetCount != 1 ||
        release.dashboard.pages[3].widgets[0].pluginId.View() != "builtin.5h4d3r5" || debug.logRetentionDays != 15 ||
        release.logRetentionDays != 15)
    {
        std::wprintf(L"Deployed template page inventory contract failed.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    // The 5H4D3R5 pages author only the keys they change; the rest merge from the plugin defaults.
    const std::string_view debugShaders = debug.dashboard.pages[4].widgets[0].privateConfiguration.View();
    const std::string_view releaseShaders = release.dashboard.pages[3].widgets[0].privateConfiguration.View();
    if (debugShaders.find("\"shuffle\":true") == std::string_view::npos ||
        debugShaders.find("\"intervalSeconds\":60") == std::string_view::npos ||
        debugShaders.find("\"mode\":\"slideshow\"") == std::string_view::npos ||
        releaseShaders.find("\"intervalSeconds\":120") == std::string_view::npos ||
        releaseShaders.find("\"shader\":\"seascape\"") == std::string_view::npos ||
        releaseShaders.find("\"renderScalePercent\":50") == std::string_view::npos)
    {
        std::wprintf(L"Deployed templates do not configure the 5H4D3R5 slideshow as expected.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    // Both templates configure the Logicon service (settings minor 1) with a page-navigation key layout and author
    // the current minor (2 added `dock`, 3 `trayIcon`) with the dock left off: the shipped window kind stays the
    // XENEON one.
    const ServiceSettings* debugLogicon = FindServiceSettings(debug, "builtin.logicon");
    const ServiceSettings* releaseLogicon = FindServiceSettings(release, "builtin.logicon");
    if (debug.dock != DefaultDockSettings() || release.dock != DefaultDockSettings() ||
        debug.dock.edge != DockEdge::None || debug.sourceDocument.find("\"dock\"") == std::string::npos ||
        release.sourceDocument.find("\"dock\"") == std::string::npos)
    {
        std::wprintf(L"Deployed templates must stay undocked and carry the commented dock example.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (!debugLogicon || !releaseLogicon || debug.versionMinor != kRedXeSettingsVersionMinor ||
        release.versionMinor != kRedXeSettingsVersionMinor || debugLogicon->name.View() != "Logicon" ||
        debugLogicon->privateConfiguration.View().find("\"logicon.keyPage.next\"") == std::string_view::npos ||
        releaseLogicon->privateConfiguration.View().find("\"dashboardPages\"") == std::string_view::npos)
    {
        std::wprintf(L"Deployed templates do not configure the Logicon service as expected.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (debug.backgroundRgb != kRedXeDefaultBackgroundRgb || release.backgroundRgb != kRedXeDefaultBackgroundRgb ||
        debug.sourceDocument.find("\"backgroundColor\"") == std::string::npos ||
        release.sourceDocument.find("\"backgroundColor\"") == std::string::npos)
    {
        std::wprintf(L"Deployed templates must author the document backgroundColor explicitly.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    // The notification-area icon is authored explicitly: shown by the Release template, hidden by the Debug one,
    // matching what each build assumes when the member is omitted.
    if (debug.trayIcon || !release.trayIcon || debug.sourceDocument.find("\"trayIcon\": false") == std::string::npos ||
        release.sourceDocument.find("\"trayIcon\": true") == std::string::npos)
    {
        std::wprintf(L"Deployed templates must author trayIcon: false in Debug and true in Release.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    const WidgetInstanceSettings& gpu = release.dashboard.pages[2].widgets[7];
    const WidgetInstanceSettings& thermal = release.dashboard.pages[2].widgets[8];
    const WidgetInstanceSettings& power = release.dashboard.pages[2].widgets[9];
    const auto isRatio =
        [](const LayoutSplitStep& step, LayoutAxis axis, uint32_t sizeRatio, uint32_t totalRatio) noexcept
    { return step.axis == axis && step.sizeRatio == sizeRatio && step.totalRatio == totalRatio; };
    if (gpu.pluginId.View() != "builtin.gpu-meter" || thermal.pluginId.View() != "builtin.thermal-meter" ||
        power.pluginId.View() != "builtin.power-meter" || gpu.adaptivePlacement.depth != 2 ||
        thermal.adaptivePlacement.depth != 2 || power.adaptivePlacement.depth != 2 ||
        !isRatio(gpu.adaptivePlacement.steps[0], LayoutAxis::LongSide, 3, 13) ||
        !isRatio(thermal.adaptivePlacement.steps[0], LayoutAxis::LongSide, 3, 13) ||
        !isRatio(power.adaptivePlacement.steps[0], LayoutAxis::LongSide, 3, 13) ||
        !isRatio(gpu.adaptivePlacement.steps[1], LayoutAxis::ShortSide, 2, 6) ||
        !isRatio(thermal.adaptivePlacement.steps[1], LayoutAxis::ShortSide, 3, 6) ||
        !isRatio(power.adaptivePlacement.steps[1], LayoutAxis::ShortSide, 1, 6))
    {
        std::wprintf(L"Release System last column is not Gpu/Thermal/Power 2-3-1.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (FAILED(ValidateAppSettings(debug)) || FAILED(ValidateAppSettings(release)))
    {
        std::wprintf(L"Deployed templates failed ValidateAppSettings.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    std::string schemaBytes;
    result = ReadFile(schemaPath, schemaBytes);
    unique_doc schema{SUCCEEDED(result) ? yyjson_read(schemaBytes.data(), schemaBytes.size(), YYJSON_READ_NOFLAG)
                                        : nullptr};
    yyjson_val* root = schema ? yyjson_doc_get_root(schema.get()) : nullptr;
    yyjson_val* properties = yyjson_is_obj(root) ? yyjson_obj_get(root, "properties") : nullptr;
    yyjson_val* version = yyjson_is_obj(properties) ? yyjson_obj_get(properties, "version") : nullptr;
    yyjson_val* pages = yyjson_is_obj(properties) ? yyjson_obj_get(properties, "pages") : nullptr;
    yyjson_val* retention = yyjson_is_obj(properties) ? yyjson_obj_get(properties, "logRetentionDays") : nullptr;
    if (!yyjson_is_obj(root) || !yyjson_is_obj(version) || !yyjson_is_uint(yyjson_obj_get(pages, "maxItems")) ||
        yyjson_get_uint(yyjson_obj_get(pages, "maxItems")) != kMaximumDashboardPages || !yyjson_is_obj(retention) ||
        !yyjson_is_uint(yyjson_obj_get(retention, "minimum")) ||
        yyjson_get_uint(yyjson_obj_get(retention, "minimum")) != kRedXeMinimumLogRetentionDays ||
        !yyjson_is_uint(yyjson_obj_get(retention, "maximum")) ||
        yyjson_get_uint(yyjson_obj_get(retention, "maximum")) != kRedXeMaximumLogRetentionDays ||
        !yyjson_is_uint(yyjson_obj_get(retention, "default")) ||
        yyjson_get_uint(yyjson_obj_get(retention, "default")) != kRedXeDefaultLogRetentionDays)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    yyjson_val* rootBackground = yyjson_is_obj(properties) ? yyjson_obj_get(properties, "backgroundColor") : nullptr;
    if (!yyjson_is_obj(rootBackground) || !yyjson_is_str(yyjson_obj_get(rootBackground, "default")) ||
        std::string_view(yyjson_get_str(yyjson_obj_get(rootBackground, "default"))) != "#000000")
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    // trayIcon is a boolean without a schema default: the omitted value depends on the build.
    yyjson_val* rootTrayIcon = yyjson_is_obj(properties) ? yyjson_obj_get(properties, "trayIcon") : nullptr;
    if (!yyjson_is_obj(rootTrayIcon) || !yyjson_is_str(yyjson_obj_get(rootTrayIcon, "type")) ||
        std::string_view(yyjson_get_str(yyjson_obj_get(rootTrayIcon, "type"))) != "boolean" ||
        yyjson_obj_get(rootTrayIcon, "default"))
    {
        std::wprintf(L"The schema must declare trayIcon as a boolean without a default.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    yyjson_val* defs = yyjson_obj_get(root, "$defs");
    // The `dock` definition carries the same ranges and defaults as DockPlacement.h.
    {
        yyjson_val* rootDock = yyjson_is_obj(properties) ? yyjson_obj_get(properties, "dock") : nullptr;
        yyjson_val* dockDefinition = yyjson_is_obj(defs) ? yyjson_obj_get(defs, "dock") : nullptr;
        yyjson_val* dockProperties =
            yyjson_is_obj(dockDefinition) ? yyjson_obj_get(dockDefinition, "properties") : nullptr;
        const auto integerRange =
            [&](const char* member, uint64_t minimum, uint64_t maximum, uint64_t fallback) noexcept
        {
            yyjson_val* property = yyjson_is_obj(dockProperties) ? yyjson_obj_get(dockProperties, member) : nullptr;
            return yyjson_is_obj(property) && yyjson_is_uint(yyjson_obj_get(property, "minimum")) &&
                   yyjson_get_uint(yyjson_obj_get(property, "minimum")) == minimum &&
                   yyjson_is_uint(yyjson_obj_get(property, "maximum")) &&
                   yyjson_get_uint(yyjson_obj_get(property, "maximum")) == maximum &&
                   yyjson_is_uint(yyjson_obj_get(property, "default")) &&
                   yyjson_get_uint(yyjson_obj_get(property, "default")) == fallback;
        };
        yyjson_val* edge = yyjson_is_obj(dockProperties) ? yyjson_obj_get(dockProperties, "edge") : nullptr;
        yyjson_val* mode = yyjson_is_obj(dockProperties) ? yyjson_obj_get(dockProperties, "mode") : nullptr;
        yyjson_val* reserve =
            yyjson_is_obj(dockProperties) ? yyjson_obj_get(dockProperties, "reserveWorkArea") : nullptr;
        yyjson_val* monitor = yyjson_is_obj(dockProperties) ? yyjson_obj_get(dockProperties, "monitor") : nullptr;
        if (!yyjson_is_obj(rootDock) || !yyjson_is_obj(dockDefinition) ||
            !yyjson_is_false(yyjson_obj_get(dockDefinition, "additionalProperties")) || !yyjson_is_obj(edge) ||
            !yyjson_is_arr(yyjson_obj_get(edge, "enum")) || yyjson_arr_size(yyjson_obj_get(edge, "enum")) != 5 ||
            std::string_view(yyjson_get_str(yyjson_obj_get(edge, "default"))) != "none" || !yyjson_is_obj(mode) ||
            std::string_view(yyjson_get_str(yyjson_obj_get(mode, "default"))) != "fixed" || !yyjson_is_obj(reserve) ||
            !yyjson_is_true(yyjson_obj_get(reserve, "default")) || !yyjson_is_obj(monitor) ||
            std::string_view(yyjson_get_str(yyjson_obj_get(monitor, "default"))) != kDockDefaultMonitor ||
            !integerRange("thickness", kDockMinimumThicknessDips, kDockMaximumThicknessDips,
                          kDockDefaultThicknessDips) ||
            !integerRange("peek", kDockMinimumPeekPixels, kDockMaximumPeekPixels, kDockDefaultPeekPixels) ||
            !integerRange("revealDelayMilliseconds", 0, kDockMaximumRevealDelayMilliseconds,
                          kDockDefaultRevealDelayMilliseconds) ||
            !integerRange("hideDelayMilliseconds", 0, kDockMaximumHideDelayMilliseconds,
                          kDockDefaultHideDelayMilliseconds) ||
            !integerRange("animationMilliseconds", 0, kDockMaximumAnimationMilliseconds,
                          kDockDefaultAnimationMilliseconds))
        {
            std::wprintf(L"The schema dock definition does not match DockPlacement.h.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    yyjson_val* widgetDefinition = yyjson_is_obj(defs) ? yyjson_obj_get(defs, "widgetDefinition") : nullptr;
    yyjson_val* variants = yyjson_is_obj(widgetDefinition) ? yyjson_obj_get(widgetDefinition, "oneOf") : nullptr;
    if (!yyjson_is_arr(variants))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    {
        size_t variantIndex = 0;
        size_t variantMax = 0;
        yyjson_val* variant = nullptr;
        yyjson_arr_foreach(variants, variantIndex, variantMax, variant)
        {
            yyjson_val* variantProperties = yyjson_is_obj(variant) ? yyjson_obj_get(variant, "properties") : nullptr;
            if (!yyjson_is_obj(variantProperties) || !yyjson_obj_get(variantProperties, "backgroundColor"))
            {
                std::wprintf(L"Every widget object variant must accept the host backgroundColor key.\n");
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }
    }
    for (const char* pluginDefinition :
         {"matrixSettings", "studioClockSettings", "deskClockSettings", "shadersSettings"})
    {
        yyjson_val* definition = yyjson_is_obj(defs) ? yyjson_obj_get(defs, pluginDefinition) : nullptr;
        yyjson_val* definitionProperties =
            yyjson_is_obj(definition) ? yyjson_obj_get(definition, "properties") : nullptr;
        if (!yyjson_is_obj(definitionProperties) || yyjson_obj_get(definitionProperties, "backgroundColor"))
        {
            std::wprintf(L"Plugin settings definitions must not own backgroundColor.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    // StudioClockSettings.h is the one Studio Clock catalog the host parser and StudioClock.dll compile in: its
    // defaults name every member, and the Specs definition (members, defaults, date formats, glowPercent range), the
    // Specs widget variant, and the schema the DLL publishes all match it.
    {
        unique_doc catalogDefaults{
            yyjson_read(StudioClock::kDefaultsJson, sizeof(StudioClock::kDefaultsJson) - 1, YYJSON_READ_NOFLAG)};
        unique_doc publishedSchema{
            yyjson_read(StudioClock::kSchemaJson, sizeof(StudioClock::kSchemaJson) - 1, YYJSON_READ_NOFLAG)};
        yyjson_val* catalog = catalogDefaults ? yyjson_doc_get_root(catalogDefaults.get()) : nullptr;
        if (!yyjson_is_obj(catalog) || yyjson_obj_size(catalog) != StudioClock::kSettingsKeys.size() ||
            !publishedSchema ||
            !StudioClockSchemaMatchesCatalog(yyjson_obj_get(defs, "studioClockSettings"), catalog) ||
            !StudioClockSchemaMatchesCatalog(yyjson_doc_get_root(publishedSchema.get()), nullptr) ||
            !StudioClockVariantMatchesCatalog(root))
        {
            std::wprintf(L"The Studio Clock schema, widget variant, or published schema differs from "
                         L"StudioClockSettings.h.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    yyjson_val* launcherSettings = yyjson_is_obj(defs) ? yyjson_obj_get(defs, "launcherSettings") : nullptr;
    yyjson_val* launcherProperties =
        yyjson_is_obj(launcherSettings) ? yyjson_obj_get(launcherSettings, "properties") : nullptr;
    yyjson_val* shortcuts =
        yyjson_is_obj(launcherProperties) ? yyjson_obj_get(launcherProperties, "shortcuts") : nullptr;
    if (!yyjson_is_uint(yyjson_obj_get(shortcuts, "maxItems")) ||
        yyjson_get_uint(yyjson_obj_get(shortcuts, "maxItems")) != kLauncherMaximumShortcuts)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    for (const RedXeBundledWidgetSpec& plugin : kRedXeBundledWidgets)
    {
        if (!SchemaAcceptsPlugin(root, plugin.pluginId))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    // The retired services Zoom variant is deprecated as a whole and keeps the retired v1.0.102 members as deprecated,
    // ignored properties, so an editor flags the entry as no longer needed but accepts a file that still carries it.
    yyjson_val* serviceVariants = yyjson_obj_get(yyjson_obj_get(defs, "serviceDefinition"), "oneOf");
    bool zoomVariantFound = false;
    size_t serviceIndex = 0;
    size_t serviceMax = 0;
    yyjson_val* serviceVariant = nullptr;
    yyjson_arr_foreach(serviceVariants, serviceIndex, serviceMax, serviceVariant)
    {
        yyjson_val* serviceProperties = yyjson_obj_get(serviceVariant, "properties");
        const char* plugin = yyjson_get_str(yyjson_obj_get(yyjson_obj_get(serviceProperties, "plugin"), "const"));
        if (!plugin || std::string_view(plugin) != "builtin.zoom")
            continue;
        zoomVariantFound = yyjson_is_true(yyjson_obj_get(serviceVariant, "deprecated"));
        for (const char* retired :
             {"clientId", "redirectPort", "domain", "displayName", "autoConnect", "mode", "labels"})
        {
            const char* reference = yyjson_get_str(yyjson_obj_get(yyjson_obj_get(serviceProperties, retired), "$ref"));
            if (!reference || std::string_view(reference) != "#/$defs/retiredZoomMember")
                zoomVariantFound = false;
        }
    }
    if (!zoomVariantFound || !yyjson_is_true(yyjson_obj_get(yyjson_obj_get(defs, "retiredZoomMember"), "deprecated")))
    {
        std::wprintf(L"The schema services Zoom variant must be deprecated and accept the retired members.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    // Every definition is referenced: an orphan validates nothing, yet reads as a settings model to edit (as the
    // removed Zoom SDK's zoomSettings did).
    size_t definitionIndex = 0;
    size_t definitionMax = 0;
    yyjson_val* definitionName = nullptr;
    yyjson_val* definition = nullptr;
    yyjson_obj_foreach(defs, definitionIndex, definitionMax, definitionName, definition)
    {
        const std::string_view name{yyjson_get_str(definitionName), yyjson_get_len(definitionName)};
        if (!SchemaReferencesDefinition(schemaBytes, name))
        {
            std::wprintf(L"The schema definition %.*S is never referenced.\n", static_cast<int>(name.size()),
                         name.data());
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    return S_OK;
}

[[nodiscard]] HRESULT ValidateParser() noexcept
{
    AppSettings parsed{};
    HRESULT result = ParseAppSettingsJson(kRepresentative, parsed);
    if (FAILED(result) || !parsed.dashboard.wrapPages || parsed.logRetentionDays != kRedXeDefaultLogRetentionDays ||
        parsed.dashboard.pageCount != 2 || parsed.dashboard.pages[0].widgetCount != 3 ||
        parsed.dashboard.pages[1].widgetCount != 0)
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    const std::string_view cfg1 = parsed.dashboard.pages[0].widgets[1].privateConfiguration.View();
    const std::string_view cfg2 = parsed.dashboard.pages[0].widgets[2].privateConfiguration.View();
    if (cfg1.find("\"seed\":7") == std::string_view::npos || cfg2.find("\"seed\":9") == std::string_view::npos ||
        cfg2.find("\"densityPercent\":70") == std::string_view::npos)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (FAILED(MoveDashboardPage(parsed, -1)) || parsed.dashboard.activePageIndex != 1 ||
        FAILED(MoveDashboardPage(parsed, 1)) || parsed.dashboard.activePageIndex != 0)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    parsed.dashboard.wrapPages = false;
    if (MoveDashboardPage(parsed, -1) != HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS) ||
        parsed.dashboard.activePageIndex != 0)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    AppSettings reloaded{};
    if (FAILED(ParseAppSettingsJson(kRepresentative, reloaded)) || reloaded.dashboard.activePageIndex != 0)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    parsed.dashboard.wrapPages = true;
    if (FAILED(MoveDashboardPage(parsed, -1)) || parsed.dashboard.activePageIndex != 1)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (FAILED(PreserveActiveDashboardPage(parsed, reloaded)) || reloaded.dashboard.activePageIndex != 1 ||
        reloaded.dashboard.activePageId.View() != parsed.dashboard.activePageId.View())
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    constexpr std::string_view retentionDocument = R"json({
      "version":{"major":5},
      "logRetentionDays":30,
      "pages":[{"widgets":[{"plugin":"builtin.gdi-orbit"}]}]
    })json";
    AppSettings retention{};
    if (FAILED(ParseAppSettingsJson(retentionDocument, retention)) || retention.logRetentionDays != 30)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    // services (minor 1): a flattened plugin object per catalogued service, defaults merged, validated by the
    // plugin's shared model, never counted as a widget plugin. The retired builtin.zoom entry loads and is ignored:
    // it is not a service, and zoom.* bindings need no entry.
    constexpr std::string_view servicesDocument = R"json({
      "version":{"major":5,"minor":1},
      "services":{
        "Keypad":{"plugin":"builtin.logicon","brightness":40,"keys":[
          {"slot":0,"action":"page.next"},
          {"slot":1,"action":"system.launch","target":"not-a-path"},
          {"slot":2,"action":"zoom.open"}],
          "dialpad":{"turns":[{"control":"roller","direction":"up","action":"logicon.brightness","target":"+5"}]}},
        "Meet":{"plugin":"builtin.zoom"}},
      "pages":[{"widgets":[{"plugin":"builtin.gdi-orbit"}]}]
    })json";
    AppSettings services{};
    if (FAILED(ParseAppSettingsJson(servicesDocument, services)) || services.serviceCount != 1 ||
        services.services.size() != 1 || services.services[0].name.View() != "Keypad" ||
        services.services[0].pluginId.View() != "builtin.logicon" || services.pluginCount != 1 ||
        services.services[0].privateConfiguration.View().find("\"brightness\":40") == std::string_view::npos ||
        services.services[0].privateConfiguration.View().find("\"restoreLogoOnExit\":true") == std::string_view::npos ||
        services.services[0].privateConfiguration.View().find("\"page.next\"") == std::string_view::npos ||
        services.services[0].privateConfiguration.View().find("\"not-a-path\"") == std::string_view::npos ||
        services.services[0].privateConfiguration.View().find("\"zoom.open\"") == std::string_view::npos ||
        services.retiredServices.size() != 1 || services.retiredServices[0].View() != "builtin.zoom" ||
        !FindServiceSettings(services, "builtin.logicon") || FindServiceSettings(services, "builtin.zoom") ||
        FindServiceSettings(services, "builtin.launcher") || FAILED(ValidateAppSettings(services)))
    {
        std::wprintf(L"The services root did not parse as expected.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    AppSettings noServices{};
    if (FAILED(ParseAppSettingsJson(retentionDocument, noServices)) || noServices.serviceCount != 0 ||
        !noServices.retiredServices.empty())
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    const std::string_view rejectedServices[] = {
        // A widget plugin is not a service.
        R"json({"version":{"major":5},"services":{"A":{"plugin":"builtin.launcher"}},"pages":[{}]})json",
        // Unknown service plugin.
        R"json({"version":{"major":5},"services":{"A":{"plugin":"builtin.nope"}},"pages":[{}]})json",
        // The same service twice.
        R"json({"version":{"major":5},"services":{"A":{"plugin":"builtin.logicon"},"B":{"plugin":"builtin.logicon"}},"pages":[{}]})json",
        // Plugin-model rejections surface as document errors.
        R"json({"version":{"major":5},"services":{"A":{"plugin":"builtin.logicon","keys":[{"slot":9}]}},"pages":[{}]})json",
        R"json({"version":{"major":5},"services":{"A":{"plugin":"builtin.logicon","brightness":0}},"pages":[{}]})json",
        R"json({"version":{"major":5},"services":{"A":{"plugin":"builtin.logicon","extra":true}},"pages":[{}]})json",
        // Action names: the former bare names, an unknown default verb, and an unregistered namespace are document
        // errors; an unsatisfied target is not (Plugins_Actions.md).
        R"json({"version":{"major":5},"services":{"A":{"plugin":"builtin.logicon","keys":[{"slot":0,"action":"launch","target":"C:\\x.exe"}]}},"pages":[{}]})json",
        R"json({"version":{"major":5},"services":{"A":{"plugin":"builtin.logicon","keys":[{"slot":0,"action":"page.nowhere"}]}},"pages":[{}]})json",
        R"json({"version":{"major":5},"services":{"A":{"plugin":"builtin.logicon","dialpad":{"turns":[{"control":"dial","direction":"cw","action":"nowhere.go"}]}}},"pages":[{}]})json",
        R"json({"version":{"major":5},"services":{"A":{"plugin":"builtin.logicon","dialpad":{"dial":"page"}}},"pages":[{}]})json",
        // Retired Zoom entry rejections: any member other than the retired v1.0.102 ones, which load and are ignored
        // (ValidateReleasedTemplates), including a retired name in another case, and the entry twice.
        R"json({"version":{"major":5},"services":{"Z":{"plugin":"builtin.zoom","meeting":"abc"}},"pages":[{}]})json",
        R"json({"version":{"major":5},"services":{"Z":{"plugin":"builtin.zoom","clientId":"abc","ClientID":"abc"}},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"services":{"Z":{"plugin":"builtin.zoom","redirectPort":48123,"sdkPath":"x"}},"pages":[{}]})json",
        R"json({"version":{"major":5},"services":{"Y":{"plugin":"builtin.zoom"},"Z":{"plugin":"builtin.zoom"}},"pages":[{}]})json",
        // Shape errors.
        R"json({"version":{"major":5},"services":[],"pages":[{}]})json",
        R"json({"version":{"major":5},"services":{"A":"builtin.logicon"},"pages":[{}]})json",
        R"json({"version":{"major":5},"services":{"A":{"use":"X"}},"pages":[{}]})json",
    };
    for (const std::string_view rejected : rejectedServices)
    {
        AppSettings ignored{};
        if (SUCCEEDED(ParseAppSettingsJson(rejected, ignored)))
        {
            std::wprintf(L"A malformed services document was accepted: %.*S\n", static_cast<int>(rejected.size()),
                         rejected.data());
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }

    constexpr std::string_view authoredPages = R"json({
      "version":{"major":5},
      "pages":[
        {"id":"home","widgets":[{"plugin":"builtin.rotating-triangle"}]},
        {"id":"system","widgets":[{"plugin":"builtin.gdi-orbit"}]}
      ]
    })json";
    constexpr std::string_view authoredReordered = R"json({
      "version":{"major":5},
      "pages":[
        {"id":"system","widgets":[{"plugin":"builtin.gdi-orbit"}]},
        {"id":"home","widgets":[{"plugin":"builtin.rotating-triangle"}]}
      ]
    })json";
    constexpr std::string_view authoredHomeOnly = R"json({
      "version":{"major":5},
      "pages":[
        {"id":"home","widgets":[{"plugin":"builtin.rotating-triangle"}]}
      ]
    })json";
    AppSettings authored{};
    AppSettings reordered{};
    AppSettings homeOnly{};
    if (FAILED(ParseAppSettingsJson(authoredPages, authored)) ||
        FAILED(ParseAppSettingsJson(authoredReordered, reordered)) ||
        FAILED(ParseAppSettingsJson(authoredHomeOnly, homeOnly)) || FAILED(MoveDashboardPage(authored, 1)) ||
        authored.dashboard.activePageId.View() != "system")
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (FAILED(PreserveActiveDashboardPage(authored, reordered)) || reordered.dashboard.activePageIndex != 0 ||
        reordered.dashboard.activePageId.View() != "system")
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (FAILED(PreserveActiveDashboardPage(authored, homeOnly)) || homeOnly.dashboard.activePageIndex != 0 ||
        homeOnly.dashboard.activePageId.View() != "home")
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    constexpr std::string_view processViewerSettings =
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.process-viewer"},{"plugin":"builtin.process-viewer","topN":7},{"plugin":"builtin.process-viewer","hideIdle":false}]}]})json";
    AppSettings processViewer{};
    if (FAILED(ParseAppSettingsJson(processViewerSettings, processViewer)) ||
        processViewer.dashboard.pages[0].widgets[0].privateConfiguration.View() !=
            R"json({"topN":10,"hideIdle":true})json" ||
        processViewer.dashboard.pages[0].widgets[1].privateConfiguration.View() !=
            R"json({"hideIdle":true,"topN":7})json" ||
        processViewer.dashboard.pages[0].widgets[2].privateConfiguration.View() !=
            R"json({"topN":10,"hideIdle":false})json")
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    constexpr std::string_view rankedViewerSettings =
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.network-meter"},{"plugin":"builtin.gpu-processes","topN":4},{"plugin":"builtin.system-pulse"}]}]})json";
    AppSettings rankedViewers{};
    if (FAILED(ParseAppSettingsJson(rankedViewerSettings, rankedViewers)) ||
        rankedViewers.dashboard.pages[0].widgets[0].privateConfiguration.View() != R"json({"topN":8})json" ||
        rankedViewers.dashboard.pages[0].widgets[1].privateConfiguration.View() != R"json({"topN":4})json" ||
        rankedViewers.dashboard.pages[0].widgets[2].privateConfiguration.View() != R"json({})json")
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    // backgroundColor is a host-reserved widget key: it lifts onto the typed instance and never enters the plugin
    // settings object, so the plugin defaults merge and validators stay unaware of it.
    constexpr std::string_view studioClockSettings =
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.studio-clock"},{"plugin":"builtin.studio-clock","showDate":true,"glowPercent":0,"backgroundColor":"#010203"}]}]})json";
    AppSettings studioClock{};
    if (FAILED(ParseAppSettingsJson(studioClockSettings, studioClock)) ||
        studioClock.dashboard.pages[0].widgets[0].privateConfiguration.View() != StudioClock::kDefaultsJson ||
        studioClock.dashboard.pages[0].widgets[0].overridesBackground ||
        studioClock.dashboard.pages[0].widgets[1].privateConfiguration.View().find("\"showDate\":true") ==
            std::string_view::npos ||
        studioClock.dashboard.pages[0].widgets[1].privateConfiguration.View().find("\"glowPercent\":0}") ==
            std::string_view::npos ||
        studioClock.dashboard.pages[0].widgets[1].privateConfiguration.View().find("backgroundColor") !=
            std::string_view::npos ||
        !studioClock.dashboard.pages[0].widgets[1].overridesBackground ||
        studioClock.dashboard.pages[0].widgets[1].backgroundRgb != 0x010203 ||
        EffectiveWidgetBackgroundRgb(studioClock, studioClock.dashboard.pages[0].widgets[0]) !=
            kRedXeDefaultBackgroundRgb ||
        EffectiveWidgetBackgroundRgb(studioClock, studioClock.dashboard.pages[0].widgets[1]) != 0x010203)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    // Both host validators (the document parser and ValidateAppSettings) take the date formats and the glowPercent
    // range from StudioClockSettings.h, as the DLL does.
    const auto studioClockDocument = [](std::string_view member)
    {
        return std::string(R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.studio-clock",)json") +
               std::string(member) + "}]}]}";
    };
    for (const std::string_view dateFormat : StudioClock::kDateFormatNames)
    {
        AppSettings dated{};
        if (FAILED(ParseAppSettingsJson(studioClockDocument("\"dateFormat\":\"" + std::string(dateFormat) + "\""),
                                        dated)) ||
            FAILED(ValidateAppSettings(dated)))
        {
            std::wprintf(L"A catalogued Studio Clock date format was rejected.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    AppSettings brightest{};
    if (FAILED(ParseAppSettingsJson(
            studioClockDocument("\"glowPercent\":" + std::to_string(StudioClock::kMaximumGlowPercent)), brightest)) ||
        FAILED(ValidateAppSettings(brightest)) ||
        FAILED(ExpectRejected(
            studioClockDocument("\"glowPercent\":" + std::to_string(StudioClock::kMaximumGlowPercent + 1U)))))
    {
        std::wprintf(L"The host glowPercent range differs from StudioClockSettings.h.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    constexpr std::string_view deskClockSettings =
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.desk-clock"},{"plugin":"builtin.desk-clock","flipDurationMilliseconds":250,"backgroundColor":"#010203"}]}]})json";
    AppSettings deskClock{};
    if (FAILED(ParseAppSettingsJson(deskClockSettings, deskClock)) ||
        deskClock.dashboard.pages[0].widgets[0].privateConfiguration.View() !=
            R"json({"flipDurationMilliseconds":420,"cardColor":"#FF3B43","digitColor":"#FFFFFF","dateColor":"#D8D8D8"})json" ||
        deskClock.dashboard.pages[0].widgets[1].privateConfiguration.View().find("\"flipDurationMilliseconds\":250") ==
            std::string_view::npos ||
        deskClock.dashboard.pages[0].widgets[1].privateConfiguration.View().find("backgroundColor") !=
            std::string_view::npos ||
        !deskClock.dashboard.pages[0].widgets[1].overridesBackground ||
        deskClock.dashboard.pages[0].widgets[1].backgroundRgb != 0x010203)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    // 5H4D3R5: the defaults are a two-minute sequential slideshow starting at Seascape at half resolution; authored
    // keys merge over them and a single shader is selected by its catalog name.
    constexpr std::string_view shadersSettings =
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.5h4d3r5"},{"plugin":"builtin.5h4d3r5","mode":"single","shader":"protean-clouds","renderScalePercent":100,"backgroundColor":"#010203"}]}]})json";
    AppSettings shaders{};
    if (FAILED(ParseAppSettingsJson(shadersSettings, shaders)) ||
        shaders.dashboard.pages[0].widgets[0].privateConfiguration.View() !=
            R"json({"mode":"slideshow","shader":"seascape","intervalSeconds":120,"shuffle":false,"renderScalePercent":50})json" ||
        shaders.dashboard.pages[0].widgets[1].privateConfiguration.View() !=
            R"json({"intervalSeconds":120,"shuffle":false,"mode":"single","shader":"protean-clouds","renderScalePercent":100})json" ||
        !shaders.dashboard.pages[0].widgets[1].overridesBackground ||
        shaders.dashboard.pages[0].widgets[1].backgroundRgb != 0x010203)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    // The document color feeds every plugin, including ones with closed empty settings, and a declare-level
    // override flows through use-objects (which may override it again or remove it with null).
    constexpr std::string_view backgroundSettings =
        R"json({"version":{"major":5},"backgroundColor":"#0a0B0c","declare":{"Cpu":{"plugin":"builtin.cpu-meter","backgroundColor":"#101010"}},"pages":[{"widgets":["Cpu",{"use":"Cpu","backgroundColor":"#202020"},{"use":"Cpu","backgroundColor":null},{"plugin":"builtin.weather","backgroundColor":"#303030"},{"plugin":"builtin.rotating-triangle"}]}]})json";
    AppSettings background{};
    if (FAILED(ParseAppSettingsJson(backgroundSettings, background)) || background.backgroundRgb != 0x0A0B0C ||
        background.dashboard.pages[0].widgetCount != 5 ||
        EffectiveWidgetBackgroundRgb(background, background.dashboard.pages[0].widgets[0]) != 0x101010 ||
        EffectiveWidgetBackgroundRgb(background, background.dashboard.pages[0].widgets[1]) != 0x202020 ||
        background.dashboard.pages[0].widgets[2].overridesBackground ||
        EffectiveWidgetBackgroundRgb(background, background.dashboard.pages[0].widgets[2]) != 0x0A0B0C ||
        EffectiveWidgetBackgroundRgb(background, background.dashboard.pages[0].widgets[3]) != 0x303030 ||
        background.dashboard.pages[0].widgets[3].privateConfiguration.View().find("backgroundColor") !=
            std::string_view::npos ||
        background.dashboard.pages[0].widgets[0].privateConfiguration.View() != "{}" ||
        background.dashboard.pages[0].widgets[4].overridesBackground ||
        EffectiveWidgetBackgroundRgb(background, background.dashboard.pages[0].widgets[4]) != 0x0A0B0C)
    {
        std::wprintf(L"Document and per-widget backgroundColor did not resolve as expected.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    AppSettings backgroundChanged = background;
    backgroundChanged.backgroundRgb = 0x000000;
    if (ActiveDashboardRuntimeEquals(background, backgroundChanged))
    {
        std::wprintf(L"A document backgroundColor change must be a runtime change.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    // A plugin persist rewrites only the plugin's flattened keys; the host-owned override on the widget object stays.
    if (FAILED(PatchWidgetInstanceSettings(background, background.dashboard.pages[0].widgets[3].id.View(),
                                           R"json({"location":"Paris"})json")) ||
        !background.dashboard.pages[0].widgets[3].overridesBackground ||
        SUCCEEDED(PatchWidgetInstanceSettings(background, background.dashboard.pages[0].widgets[3].id.View(),
                                              R"json({"backgroundColor":"#404040"})json")))
    {
        std::wprintf(L"Persist did not preserve the widget backgroundColor override.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    AppSettings persisted{};
    if (FAILED(ParseAppSettingsJson(background.sourceDocument, persisted)) ||
        EffectiveWidgetBackgroundRgb(persisted, persisted.dashboard.pages[0].widgets[3]) != 0x303030 ||
        persisted.dashboard.pages[0].widgets[3].privateConfiguration.View().find("\"location\":\"Paris\"") ==
            std::string_view::npos)
    {
        std::wprintf(L"The persisted document lost the widget backgroundColor override.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    constexpr std::string_view weatherSettings =
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.weather"},{"plugin":"builtin.weather","locationMode":"manual","location":"48.86,2.35","temperatureUnit":"fahrenheit","windUnit":"mph"}]}]})json";
    AppSettings weather{};
    if (FAILED(ParseAppSettingsJson(weatherSettings, weather)) ||
        weather.dashboard.pages[0].widgets[0].privateConfiguration.View() !=
            R"json({"locationMode":"automatic","location":"","temperatureUnit":"celsius","windUnit":"kmh"})json" ||
        weather.dashboard.pages[0].widgets[1].privateConfiguration.View().find("\"locationMode\":\"manual\"") ==
            std::string_view::npos ||
        weather.dashboard.pages[0].widgets[1].privateConfiguration.View().find("\"windUnit\":\"mph\"") ==
            std::string_view::npos)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (FAILED(PatchWidgetInstanceSettings(weather, weather.dashboard.pages[0].widgets[0].id.View(),
                                           R"json({"location":"Paris"})json")) ||
        weather.dashboard.pages[0].widgets[0].privateConfiguration.View().find("\"location\":\"Paris\"") ==
            std::string_view::npos ||
        weather.dashboard.pages[0].widgets[0].privateConfiguration.View().find("\"locationMode\":\"automatic\"") ==
            std::string_view::npos ||
        weather.dashboard.pages[0].widgets[0].privateConfiguration.View().find("\"temperatureUnit\":\"celsius\"") ==
            std::string_view::npos)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (SUCCEEDED(PatchWidgetInstanceSettings(weather, weather.dashboard.pages[0].widgets[0].id.View(),
                                              R"json({"weatherApiKey":"secret"})json")))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    constexpr std::string_view launcherSettings =
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.launcher"},{"plugin":"builtin.launcher","shortcuts":[{"target":"C:\\Windows\\notepad.exe"}]}]}]})json";
    AppSettings launcher{};
    if (FAILED(ParseAppSettingsJson(launcherSettings, launcher)) ||
        launcher.dashboard.pages[0].widgets[0].privateConfiguration.View().find("\"shortcuts\":[]") ==
            std::string_view::npos ||
        launcher.dashboard.pages[0].widgets[0].privateConfiguration.View().find("\"iconSize\":\"automatic\"") ==
            std::string_view::npos ||
        launcher.dashboard.pages[0].widgets[1].privateConfiguration.View().find("notepad.exe") ==
            std::string_view::npos)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    for (const char* size : {"small", "medium", "large", "huge", "automatic"})
    {
        const std::string sized =
            std::string(
                R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.launcher","iconSize":")json") +
            size + "\"}]}]}";
        AppSettings sizedSettings{};
        if (FAILED(ParseAppSettingsJson(sized, sizedSettings)) ||
            sizedSettings.dashboard.pages[0].widgets[0].privateConfiguration.View().find(size) ==
                std::string_view::npos)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    {
        std::string thirtyTwo =
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.launcher","shortcuts":[)json";
        for (uint32_t index = 0; index < kLauncherMaximumShortcuts; ++index)
        {
            if (index != 0)
            {
                thirtyTwo += ',';
            }
            char item[80]{};
            (void)sprintf_s(item, R"({"target":"C:\\Windows\\n%02u.exe"})", index);
            thirtyTwo += item;
        }
        thirtyTwo += "]}]}]}";
        AppSettings cap{};
        if (FAILED(ParseAppSettingsJson(thirtyTwo, cap)))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        std::string thirtyThree =
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.launcher","shortcuts":[)json";
        for (uint32_t index = 0; index < kLauncherMaximumShortcuts + 1; ++index)
        {
            if (index != 0)
            {
                thirtyThree += ',';
            }
            char item[80]{};
            (void)sprintf_s(item, R"({"target":"C:\\Windows\\n%02u.exe"})", index);
            thirtyThree += item;
        }
        thirtyThree += "]}]}]}";
        if (FAILED(ExpectRejected(thirtyThree)))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    if (FAILED(PatchWidgetInstanceSettings(launcher, launcher.dashboard.pages[0].widgets[0].id.View(),
                                           R"json({"shortcuts":[{"target":"https://example.com/"}]})json")) ||
        launcher.dashboard.pages[0].widgets[0].privateConfiguration.View().find("https://example.com/") ==
            std::string_view::npos)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    // An unsatisfied launch target is a runtime warning tile, not a document error; an unknown action name and a
    // non-launch action without an icon are structural errors.
    if (FAILED(PatchWidgetInstanceSettings(launcher, launcher.dashboard.pages[0].widgets[0].id.View(),
                                           R"json({"shortcuts":[{"target":"example.com"}]})json")) ||
        FAILED(PatchWidgetInstanceSettings(launcher, launcher.dashboard.pages[0].widgets[0].id.View(),
                                           R"json({"shortcuts":[{"action":"page.next","icon":"Home"}]})json")) ||
        launcher.dashboard.pages[0].widgets[0].privateConfiguration.View().find("\"action\":\"page.next\"") ==
            std::string_view::npos)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (SUCCEEDED(PatchWidgetInstanceSettings(
            launcher, launcher.dashboard.pages[0].widgets[0].id.View(),
            R"json({"shortcuts":[{"action":"nowhere.go","target":"x","icon":"Home"}]})json")) ||
        SUCCEEDED(PatchWidgetInstanceSettings(launcher, launcher.dashboard.pages[0].widgets[0].id.View(),
                                              R"json({"shortcuts":[{"action":"page.next"}]})json")) ||
        SUCCEEDED(
            PatchWidgetInstanceSettings(launcher, launcher.dashboard.pages[0].widgets[0].id.View(),
                                        R"json({"shortcuts":[{"target":"C:\\x.exe","iconPng":"C:\\i.png"}]})json")))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    constexpr std::array<std::string_view, 64> invalid{
        std::string_view{R"json({"pages":[{}]})json"},
        std::string_view{R"json({"version":{"major":4},"pages":[{}]})json"},
        std::string_view{R"json({"version":{"major":5,"minor":"0"},"pages":[{}]})json"},
        std::string_view{R"json({"version":{"major":5},"unknown":1,"pages":[{}]})json"},
        std::string_view{R"json({"version":{"major":5},"version":{"major":5},"pages":[{}]})json"},
        std::string_view{R"json({version:{major:5},pages:[{}]})json"},
        std::string_view{R"json({'version':{'major':5},'pages':[{}]})json"},
        std::string_view{R"json({"version":{"major":5},"pages":[]})json"},
        std::string_view{R"json({"version":{"major":5},"pages":[{"widgets":[]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"columns":[{"weight":0,"widget":{"plugin":"builtin.gdi-orbit"}}]}]})json"},
        std::string_view{R"json({"version":{"major":5},"pages":[{"widgets":["missing"]}]})json"},
        std::string_view{R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"missing.plugin"}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.gdi-orbit","bad":1}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.process-viewer","topN":0}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.process-viewer","topN":33}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.process-viewer","topN":10,"bad":1}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.process-viewer","hideIdle":1}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.network-meter","topN":0}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.gpu-processes","topN":17}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.studio-clock","showSeconds":1}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.studio-clock","externalDotsAlwaysOn":1}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.studio-clock","dateFormat":"locale"}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.studio-clock","secondsColor":"#GG0000"}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.studio-clock","unknown":true}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.studio-clock","glowPercent":101}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.studio-clock","glowPercent":-1}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.studio-clock","glowPercent":3.5}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.studio-clock","glowPercent":"35"}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.desk-clock","flipDurationMilliseconds":249}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.desk-clock","flipDurationMilliseconds":801}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.desk-clock","cardColor":"#GG0000"}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.desk-clock","unknown":true}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.5h4d3r5","mode":"loop"}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.5h4d3r5","shader":"Seascape"}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.5h4d3r5","intervalSeconds":9}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.5h4d3r5","intervalSeconds":3601}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.5h4d3r5","shuffle":1}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.5h4d3r5","renderScalePercent":24}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.5h4d3r5","renderScalePercent":101}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.5h4d3r5","unknown":true}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.weather","locationMode":"gps"}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.weather","weatherApiKey":"secret"}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.weather","temperatureUnit":"kelvin"}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.launcher","shortcuts":[{"action":"nowhere.go","target":"x","icon":"Home"}]}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.launcher","shortcuts":[{"target":"C:\\\\Windows\\\\notepad.exe"},{"target":"C:\\\\Windows\\\\write.exe"},{"target":"C:\\\\Windows\\\\regedit.exe"},{"target":"C:\\\\Windows\\\\explorer.exe"},{"target":"C:\\\\Windows\\\\notepad.exe"}]}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.launcher","extra":1,"shortcuts":[]}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.launcher","iconSize":"jumbo","shortcuts":[]}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.launcher","iconSize":"auto"}]}]})json"},
        std::string_view{R"json({"version":{"major":5},"backgroundColor":"#12345","pages":[{}]})json"},
        std::string_view{R"json({"version":{"major":5},"backgroundColor":"123456","pages":[{}]})json"},
        std::string_view{R"json({"version":{"major":5},"backgroundColor":1,"pages":[{}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.cpu-meter","backgroundColor":"#GG0000"}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.studio-clock","backgroundColor":7}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"declare":{"X":{"plugin":"builtin.matrix-rain","backgroundColor":"black"}},"pages":[{}]})json"},
        std::string_view{R"json({"version":{"major":5},"logRetentionDays":0,"pages":[{}]})json"},
        std::string_view{R"json({"version":{"major":5},"logRetentionDays":366,"pages":[{}]})json"},
        std::string_view{R"json({"version":{"major":5},"logRetentionDays":false,"pages":[{}]})json"},
        std::string_view{R"json({"version":{"major":5},"logRetentionDays":-1,"pages":[{}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.gdi-orbit"}}]}}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.gdi-orbit","settings":{}}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"declare":{"X":{"plugin":"builtin.matrix-rain","settings":{"seed":1}}},"pages":[{}]})json"},
        std::string_view{R"json({"version":{"major":5},"pages":[{"widgets":[{"use":"Missing","seed":1}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.launcher","settings":{"shortcuts":[]},"shortcuts":[]}]}]})json"},
        std::string_view{R"json({"version":{"major":5},"pages":[{"columns":[],"rows":[]}]})json"},
    };
    for (size_t index = 0; index < invalid.size(); ++index)
    {
        result = ExpectRejected(invalid[index]);
        if (FAILED(result))
        {
            return result;
        }
    }

    constexpr std::string_view newerMinor =
        R"json({"version":{"major":5,"minor":4},"futureRoot":true,"pages":[{"futurePage":1}]})json";
    if (FAILED(ParseAppSettingsJson(newerMinor, parsed)) || parsed.dashboard.pageCount != 1 ||
        parsed.versionMinor != 4 || parsed.sourceDocument.find("futureRoot") == std::string::npos)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    try
    {
        std::string tooMany = R"json({"version":{"major":5},"pages":[)json";
        for (size_t index = 0; index <= kMaximumDashboardPages; ++index)
        {
            if (index != 0)
                tooMany += ',';
            tooMany += "{}";
        }
        tooMany += "]}";
        if (FAILED(ExpectRejected(tooMany)))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        const auto widgetDocument = [](size_t count)
        {
            std::string document = R"json({"version":{"major":5},"pages":[{"widgets":[)json";
            for (size_t index = 0; index < count; ++index)
            {
                if (index != 0)
                    document += ',';
                document += R"json({"plugin":"builtin.rotating-triangle"})json";
            }
            document += "]}]}";
            return document;
        };
        const std::string maximumWidgets = widgetDocument(kMaximumWidgetsPerPage);
        if (FAILED(ParseAppSettingsJson(maximumWidgets, parsed)) ||
            parsed.dashboard.pages[0].widgetCount != kMaximumWidgetsPerPage)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        if (FAILED(ExpectRejected(widgetDocument(kMaximumWidgetsPerPage + 1))))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        std::string tooManyDeclarations = R"json({"version":{"major":5},"declare":{)json";
        for (size_t index = 0; index <= kMaximumSettingsDeclarations; ++index)
        {
            if (index != 0)
                tooManyDeclarations += ',';
            tooManyDeclarations += "\"D" + std::to_string(index) + "\":{\"plugin\":\"builtin.rotating-triangle\"}";
        }
        tooManyDeclarations += R"json(},"pages":[{}]})json";
        if (FAILED(ExpectRejected(tooManyDeclarations)))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        const auto namedPage = [](size_t codePoints)
        {
            std::string name;
            name.reserve(codePoints * 2U);
            for (size_t index = 0; index < codePoints; ++index)
                name += "\xC3\xA9";
            return std::string{"{\"version\":{\"major\":5},\"pages\":[{\"name\":\""} + name + "\"}]}";
        };
        if (FAILED(ParseAppSettingsJson(namedPage(128), parsed)) || FAILED(ExpectRejected(namedPage(129))))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        const auto nestedLayout = [](size_t levels)
        {
            std::string area = R"json({"plugin":"builtin.gdi-orbit"})json";
            for (size_t level = 1; level < levels; ++level)
                area = R"json({"columns":[)json" + area + "]}";
            return R"json({"version":{"major":5},"pages":[{"columns":[)json" + area + "]}]}";
        };
        if (FAILED(ParseAppSettingsJson(nestedLayout(kMaximumLayoutDepth), parsed)))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (FAILED(ExpectRejected(nestedLayout(kMaximumLayoutDepth + 1U))))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        std::string oversized(1024U * 1024U + 1U, ' ');
        if (FAILED(ExpectRejected(oversized)))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        auto expectDiagnostic = [](std::string_view json, std::string_view pathNeedle,
                                   std::string_view messageNeedle) noexcept -> HRESULT
        {
            AppSettings settings{};
            SettingsParseDiagnostic diagnostic;
            if (SUCCEEDED(ParseAppSettingsJsonDetailed(json, settings, diagnostic)))
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            if (diagnostic.path.find(pathNeedle) == std::string::npos ||
                diagnostic.message.find(messageNeedle) == std::string::npos ||
                diagnostic.message.find("version 4 schema") != std::string::npos)
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            return S_OK;
        };
        if (FAILED(expectDiagnostic(R"json({"version":{"major":5},"unknown":1,"pages":[{}]})json", ".unknown",
                                    "Unknown member")))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (FAILED(expectDiagnostic(R"json({"version":{"major":4},"pages":[{}]})json", ".version.major", "version 5")))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (FAILED(expectDiagnostic(
                R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"missing.plugin"}]}]})json", ".plugin",
                "Unknown plugin")))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (FAILED(expectDiagnostic(
                R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.process-viewer","topN":0}]}]})json",
                ".topN", "topN must be an integer")))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (FAILED(expectDiagnostic(R"json({"version":{"major":5},"pages":[]})json", ".pages", "At least one page")))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (FAILED(expectDiagnostic(
                R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.launcher","iconSize":"jumbo"}]}]})json",
                ".iconSize", "iconSize must be small, medium, large, huge, or automatic")))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (FAILED(expectDiagnostic(
                R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.launcher","iconSize":"auto"}]}]})json",
                ".iconSize", "iconSize must be small, medium, large, huge, or automatic")))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (FAILED(expectDiagnostic(
                R"json({"version":{"major":5},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[]}}]})json",
                ".layout", "layout is not accepted")))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (FAILED(expectDiagnostic(
                R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.gdi-orbit","settings":{}}]}]})json",
                ".settings", "flattened")))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        constexpr std::string_view catalogIds =
            R"json({"version":{"major":5},"pages":[{"widgets":["builtin.launcher","builtin.matrix-rain"]}]})json";
        if (FAILED(ParseAppSettingsJson(catalogIds, parsed)) || parsed.dashboard.pages[0].widgetCount != 2 ||
            parsed.dashboard.pages[0].widgets[0].pluginId.View() != "builtin.launcher" ||
            parsed.dashboard.pages[0].widgets[0].adaptivePlacement.steps[0].sizeRatio != 1 ||
            parsed.dashboard.pages[0].widgets[0].adaptivePlacement.steps[0].axis != LayoutAxis::LongSide)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (FAILED(ExpectRejected(R"json({"version":{"major":5},"pages":[{"widgets":["Launcher"]}]})json")))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    catch (...)
    {
        return E_FAIL;
    }
    return S_OK;
}

// The `dock` root member (minor 2) and the --dock* command line: defaults, every rejection, older-minor documents,
// the switch grammar with its errors, and the merge precedence (DockOptions.h).
[[nodiscard]] HRESULT ValidateDockSettings() noexcept
{
    constexpr std::string_view full = R"json({
      "version": { "major": 5, "minor": 2 },
      "dock": {
        "edge": "left", "monitor": "name:DELL", "thickness": 240, "mode": "autohide", "reserveWorkArea": false,
        "peek": 6, "revealDelayMilliseconds": 0, "hideDelayMilliseconds": 1200, "animationMilliseconds": 350
      },
      "pages": [{}]
    })json";
    AppSettings parsed{};
    if (FAILED(ParseAppSettingsJson(full, parsed)) || parsed.versionMinor != 2 || parsed.dock.edge != DockEdge::Left ||
        parsed.dock.monitor.View() != "name:DELL" || parsed.dock.thicknessDips != 240 ||
        parsed.dock.mode != DockMode::Autohide || parsed.dock.reserveWorkArea || parsed.dock.peekPixels != 6 ||
        parsed.dock.revealDelayMilliseconds != 0 || parsed.dock.hideDelayMilliseconds != 1200 ||
        parsed.dock.animationMilliseconds != 350)
    {
        std::wprintf(L"A complete dock object did not parse to its typed members.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    // `secondary` (minor 3) names the second screen: the first display that is not the primary.
    AppSettings secondScreen{};
    if (FAILED(ParseAppSettingsJson(
            R"json({"version":{"major":5,"minor":3},"dock":{"edge":"top","monitor":"secondary"},"pages":[{}]})json",
            secondScreen)) ||
        secondScreen.dock.edge != DockEdge::Top || secondScreen.dock.monitor.View() != kDockSecondaryMonitor)
    {
        std::wprintf(L"The secondary monitor selector was refused.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    // Defaults merge member by member; an empty object, an omitted object, and a minor 1 document are the same value.
    constexpr std::string_view partial =
        R"json({"version":{"major":5,"minor":2},"dock":{"edge":"bottom"},"pages":[{}]})json";
    constexpr std::string_view omitted = R"json({"version":{"major":5,"minor":2},"pages":[{}]})json";
    constexpr std::string_view olderMinor = R"json({"version":{"major":5,"minor":1},"pages":[{}]})json";
    constexpr std::string_view emptyObject = R"json({"version":{"major":5},"dock":{},"pages":[{}]})json";
    AppSettings partialSettings{};
    AppSettings omittedSettings{};
    AppSettings olderSettings{};
    AppSettings emptySettings{};
    if (FAILED(ParseAppSettingsJson(partial, partialSettings)) || partialSettings.dock.edge != DockEdge::Bottom ||
        partialSettings.dock.monitor.View() != kDockDefaultMonitor ||
        partialSettings.dock.thicknessDips != kDockDefaultThicknessDips ||
        partialSettings.dock.mode != DockMode::Fixed || !partialSettings.dock.reserveWorkArea ||
        partialSettings.dock.peekPixels != kDockDefaultPeekPixels ||
        partialSettings.dock.revealDelayMilliseconds != kDockDefaultRevealDelayMilliseconds ||
        partialSettings.dock.hideDelayMilliseconds != kDockDefaultHideDelayMilliseconds ||
        partialSettings.dock.animationMilliseconds != kDockDefaultAnimationMilliseconds ||
        FAILED(ParseAppSettingsJson(omitted, omittedSettings)) || omittedSettings.dock != DefaultDockSettings() ||
        FAILED(ParseAppSettingsJson(olderMinor, olderSettings)) || olderSettings.dock != DefaultDockSettings() ||
        olderSettings.versionMinor != 1 || FAILED(ParseAppSettingsJson(emptyObject, emptySettings)) ||
        emptySettings.dock != DefaultDockSettings())
    {
        std::wprintf(L"Dock defaults did not merge as documented.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    // edge none keeps every other member validated and inert.
    constexpr std::string_view inert =
        R"json({"version":{"major":5,"minor":2},"dock":{"edge":"none","thickness":400},"pages":[{}]})json";
    AppSettings inertSettings{};
    if (FAILED(ParseAppSettingsJson(inert, inertSettings)) || inertSettings.dock.edge != DockEdge::None ||
        inertSettings.dock.thicknessDips != 400)
    {
        std::wprintf(L"edge none must still parse the other members.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    // animationMilliseconds 0 turns the slide off: the bar reveals and hides in one step.
    AppSettings unanimated{};
    if (FAILED(ParseAppSettingsJson(
            R"json({"version":{"major":5,"minor":3},"dock":{"animationMilliseconds":0},"pages":[{}]})json",
            unanimated)) ||
        unanimated.dock.animationMilliseconds != 0)
    {
        std::wprintf(L"animationMilliseconds 0 was refused.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    const std::string_view rejected[]{
        R"json({"version":{"major":5,"minor":2},"dock":{"edge":"middle"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"edge":1},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"monitor":"all"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"monitor":"0"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"monitor":"name:"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"monitor":""},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":3},"dock":{"monitor":"Secondary"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":3},"dock":{"monitor":"second"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"thickness":31},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"thickness":1081},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"thickness":"180"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"mode":"hidden"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"reserveWorkArea":"yes"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"peek":0},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"peek":65},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"revealDelayMilliseconds":2001},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"hideDelayMilliseconds":10001},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":3},"dock":{"animationMilliseconds":1001},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":3},"dock":{"animationMilliseconds":-1},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":3},"dock":{"animationMilliseconds":"200"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":3},"dock":{"animationMilliseconds":1.5},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"length":100},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":[],"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"edge":"top","edge":"top"},"pages":[{}]})json",
    };
    for (const std::string_view document : rejected)
    {
        if (FAILED(ExpectRejected(document)))
        {
            std::wprintf(L"A malformed dock member was accepted.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    // The diagnostic names the authored member.
    {
        AppSettings diagnosed{};
        SettingsParseDiagnostic diagnostic{};
        if (SUCCEEDED(ParseAppSettingsJsonDetailed(
                R"json({"version":{"major":5,"minor":2},"dock":{"peek":65},"pages":[{}]})json", diagnosed,
                diagnostic)) ||
            diagnostic.path != "$.dock.peek")
        {
            std::wprintf(L"The dock diagnostic path is %hs, not $.dock.peek.\n", diagnostic.path.c_str());
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }

    // Command line: the grammar, its errors, and the merge over the document.
    DockOverrides overrides{};
    if (!ParseDockEdgeArgument(L"bottom", overrides) || overrides.edge != DockEdge::Bottom || overrides.hasMonitor ||
        !ParseDockEdgeArgument(L"left@2", overrides) || overrides.edge != DockEdge::Left || !overrides.hasMonitor ||
        overrides.monitor.View() != "2" || !ParseDockEdgeArgument(L"top@name:DELL U2723", overrides) ||
        overrides.monitor.View() != "name:DELL U2723" || !ParseDockEdgeArgument(L"right@xeneon", overrides) ||
        overrides.monitor.View() != "xeneon" || !ParseDockEdgeArgument(L"top@secondary", overrides) ||
        overrides.monitor.View() != kDockSecondaryMonitor || !ParseDockEdgeArgument(L"none", overrides) ||
        overrides.edge != DockEdge::None || !ParseDockModeArgument(L"autohide", overrides) ||
        overrides.mode != DockMode::Autohide || !ParseDockModeArgument(L"fixed", overrides) ||
        overrides.mode != DockMode::Fixed || !ParseDockThicknessArgument(L"32", overrides) ||
        overrides.thicknessDips != 32 || !ParseDockThicknessArgument(L"1080", overrides) ||
        overrides.thicknessDips != 1080 || !ParseDockReserveArgument(L"off", overrides) || overrides.reserveWorkArea ||
        !ParseDockReserveArgument(L"on", overrides) || !overrides.reserveWorkArea ||
        !ParseDockPeekArgument(L"64", overrides) || overrides.peekPixels != 64 || !overrides.Any())
    {
        std::wprintf(L"A valid --dock* value was refused or misread.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    DockOverrides scratch{};
    if (ParseDockEdgeArgument(L"middle", scratch) || ParseDockEdgeArgument(L"bottom@all", scratch) ||
        ParseDockEdgeArgument(L"bottom@0", scratch) || ParseDockEdgeArgument(L"bottom@", scratch) ||
        ParseDockEdgeArgument(L"@primary", scratch) || ParseDockEdgeArgument(L"", scratch) ||
        ParseDockModeArgument(L"maybe", scratch) || ParseDockThicknessArgument(L"31", scratch) ||
        ParseDockThicknessArgument(L"1081", scratch) || ParseDockThicknessArgument(L"-5", scratch) ||
        ParseDockThicknessArgument(L"18x", scratch) || ParseDockReserveArgument(L"maybe", scratch) ||
        ParseDockReserveArgument(L"true", scratch) || ParseDockPeekArgument(L"0", scratch) ||
        ParseDockPeekArgument(L"65", scratch) || scratch.Any())
    {
        std::wprintf(L"An invalid --dock* value was accepted.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    // Precedence: each present switch replaces its member; the rest stays with the document.
    DockOverrides partialOverrides{};
    if (!ParseDockEdgeArgument(L"top", partialOverrides) || !ParseDockPeekArgument(L"9", partialOverrides))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    const DockSettings effective = EffectiveDockSettings(parsed.dock, partialOverrides);
    const DockSettings untouched = EffectiveDockSettings(parsed.dock, DockOverrides{});
    if (effective.edge != DockEdge::Top || effective.peekPixels != 9 || effective.monitor.View() != "name:DELL" ||
        effective.thicknessDips != 240 || effective.mode != DockMode::Autohide || effective.reserveWorkArea ||
        effective.hideDelayMilliseconds != 1200 || untouched != parsed.dock)
    {
        std::wprintf(L"The --dock* merge did not replace exactly the named members.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    // A dragged edge persists as dock.thickness: an existing member is replaced, a missing object is created and
    // the document moves to minor 2, and the result parses back to the same value.
    AppSettings dragged{};
    if (FAILED(ParseAppSettingsJson(full, dragged)) || FAILED(PatchDockThickness(dragged, 96)) ||
        dragged.dock.thicknessDips != 96 || dragged.sourceDocument.find("\"thickness\": 96") == std::string::npos ||
        dragged.sourceDocument.find("\"thickness\": 240") != std::string::npos)
    {
        std::wprintf(L"PatchDockThickness did not replace the existing thickness.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    AppSettings reparsed{};
    if (FAILED(ParseAppSettingsJson(dragged.sourceDocument, reparsed)) || reparsed.dock.thicknessDips != 96 ||
        reparsed.dock.edge != DockEdge::Left || reparsed.dock.mode != DockMode::Autohide)
    {
        std::wprintf(L"The patched document does not parse back to the dragged thickness.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    std::string annotated{full};
    annotated.insert(annotated.find("\"dock\""), "// user dock example\n      ");
    annotated.insert(annotated.find("\"edge\""), "/* keep the dock note */ ");
    std::string expected = annotated;
    const size_t thicknessPosition = expected.find("\"thickness\": 240");
    if (thicknessPosition == std::string::npos)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    expected.replace(thicknessPosition, sizeof("\"thickness\": 240") - 1, "\"thickness\": 144");
    AppSettings annotatedSettings{};
    if (FAILED(ParseAppSettingsJson(annotated, annotatedSettings)) ||
        FAILED(PatchDockThickness(annotatedSettings, 144)) || annotatedSettings.sourceDocument != expected ||
        FAILED(ParseAppSettingsJson(annotatedSettings.sourceDocument, reparsed)) || reparsed.dock.thicknessDips != 144)
    {
        std::wprintf(L"PatchDockThickness changed comments or unrelated settings text.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    std::string escaped{full};
    escaped.replace(escaped.find("\"dock\""), sizeof("\"dock\"") - 1, "\"\\u0064ock\"");
    escaped.replace(escaped.find("\"thickness\""), sizeof("\"thickness\"") - 1, "\"thi\\u0063kness\"");
    AppSettings escapedSettings{};
    if (FAILED(ParseAppSettingsJson(escaped, escapedSettings)) || FAILED(PatchDockThickness(escapedSettings, 150)) ||
        escapedSettings.sourceDocument.find("\"thi\\u0063kness\": 150") == std::string::npos ||
        FAILED(ParseAppSettingsJson(escapedSettings.sourceDocument, reparsed)) || reparsed.dock.thicknessDips != 150)
    {
        std::wprintf(L"PatchDockThickness did not preserve escaped dock member names.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    constexpr std::string_view commentedExample = R"json({
      "version":{"major":5,"minor":1,},
      "pages":[{}],
      // "dock":{"edge":"left","thickness":999}
    })json";
    AppSettings exampleSettings{};
    if (FAILED(ParseAppSettingsJson(commentedExample, exampleSettings)) ||
        FAILED(PatchDockThickness(exampleSettings, 220)) ||
        exampleSettings.sourceDocument.find("// \"dock\":{\"edge\":\"left\",\"thickness\":999}") == std::string::npos ||
        FAILED(ParseAppSettingsJson(exampleSettings.sourceDocument, reparsed)) || reparsed.dock.thicknessDips != 220)
    {
        std::wprintf(L"PatchDockThickness did not retain the commented dock example and trailing comma.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    AppSettings created{};
    if (FAILED(ParseAppSettingsJson(olderMinor, created)) || FAILED(PatchDockThickness(created, 300)) ||
        created.dock.thicknessDips != 300 || created.versionMinor != 2 ||
        created.sourceDocument.find("\"dock\"") == std::string::npos ||
        created.sourceDocument.find("\"minor\":2") == std::string::npos ||
        FAILED(ParseAppSettingsJson(created.sourceDocument, reparsed)) || reparsed.dock.thicknessDips != 300 ||
        reparsed.versionMinor != 2 || reparsed.dock.edge != DockEdge::None)
    {
        std::wprintf(
            L"PatchDockThickness did not create the dock object on a minor 1 document, typed minor included.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (SUCCEEDED(PatchDockThickness(created, 31)) || SUCCEEDED(PatchDockThickness(created, 1081)) ||
        created.dock.thicknessDips != 300)
    {
        std::wprintf(L"PatchDockThickness accepted an out-of-range thickness.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

// A dragged edge writes `dock.thickness` in the document's own layout: a new `dock` on its own line after `version`
// (one line in each shipped template, in its line breaks) and below a comment that ends version's line, a missing
// `thickness` after the last dock member, on its line or on a new line at its indentation, or one level deeper than
// the closing brace of an empty dock, and a missing `minor` the same way inside `version`. The typed minor follows a
// raised source minor, a line comment ends at a lone CR exactly where the parser ends it (for the diagnostic locator,
// which shares the scanner, too), and a CR-only document keeps CR. A release at the current thickness changes nothing,
// and a patch that would not parse back to the running dock is refused.
[[nodiscard]] HRESULT ValidateDockThicknessLayout() noexcept
{
    try
    {
        for (const wchar_t* name : {kRedXeDebugSettingsFileName, kRedXeReleaseSettingsFileName})
        {
            std::filesystem::path path;
            std::string original;
            HRESULT result = GetDeployedPath(name, path);
            if (SUCCEEDED(result))
                result = ReadFile(path, original);
            if (FAILED(result))
                return result;
            constexpr std::string_view versionMember = "\"version\": { \"major\": 5, \"minor\": 3 },";
            const size_t versionAt = original.find(versionMember);
            AppSettings dragged{};
            if (versionAt == std::string::npos || FAILED(ParseAppSettingsJson(original, dragged)) ||
                FAILED(PatchDockThickness(dragged, 220)))
            {
                std::wprintf(L"PatchDockThickness could not patch the %s template.\n", name);
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            std::string expected = original;
            expected.insert(versionAt + versionMember.size(),
                            (original.find("\r\n") != std::string::npos ? "\r\n" : "\n") +
                                std::string("  \"dock\": { \"thickness\": 220 },"));
            AppSettings reparsed{};
            if (dragged.sourceDocument != expected || FAILED(ParseAppSettingsJson(dragged.sourceDocument, reparsed)) ||
                reparsed.dock.thicknessDips != 220 || reparsed.dock.edge != DockEdge::None ||
                dragged.versionMinor != kRedXeSettingsVersionMinor)
            {
                std::wprintf(L"PatchDockThickness did not add dock to %s as one line after version.\n", name);
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }

        struct LayoutCase
        {
            std::string_view source;
            std::string_view expected;
        };
        const LayoutCase cases[]{
            // On the last member's line.
            {R"json({"version":{"major":5,"minor":2},"dock":{ "edge": "bottom" },"pages":[{}]})json",
             R"json({"version":{"major":5,"minor":2},"dock":{ "edge": "bottom", "thickness": 220 },"pages":[{}]})json"},
            // Before the last member's trailing comma and comment, which then follow it.
            {R"json({"version":{"major":5,"minor":2},"dock":{ "edge": "top", /* keep */ },"pages":[{}]})json",
             R"json({"version":{"major":5,"minor":2},"dock":{ "edge": "top", "thickness": 220, /* keep */ },)json"
             R"json("pages":[{}]})json"},
            // On a new line at the last member's indentation.
            {"{\n  \"version\": { \"major\": 5, \"minor\": 2 },\n  \"dock\": {\n    \"edge\": \"left\" // keep\n"
             "  },\n  \"pages\": [{}]\n}\n",
             "{\n  \"version\": { \"major\": 5, \"minor\": 2 },\n  \"dock\": {\n    \"edge\": \"left\",\n"
             "    \"thickness\": 220 // keep\n  },\n  \"pages\": [{}]\n}\n"},
            // An empty dock, and the minor a minor 0 document gains.
            {R"json({"version":{"major":5},"dock":{},"pages":[{}]})json",
             R"json({"version":{"major":5, "minor": 2},"dock":{ "thickness": 220 },"pages":[{}]})json"},
            // A line comment that ends at a lone CR: the dock after it is a member to the parser, so it is patched in
            // place instead of gaining a duplicate that the next start would reject.
            {"{\"version\":{\"major\":5,\"minor\":2},// note\r\"dock\":{\"edge\":\"top\",\"thickness\":100},\n"
             "\"pages\":[{}]}",
             "{\"version\":{\"major\":5,\"minor\":2},// note\r\"dock\":{\"edge\":\"top\",\"thickness\":220},\n"
             "\"pages\":[{}]}"},
            // A comment that ends version's line stays on it; the new dock takes the next line.
            {"{\n  \"version\": { \"major\": 5, \"minor\": 2 }, // the version\n  \"pages\": [{}]\n}\n",
             "{\n  \"version\": { \"major\": 5, \"minor\": 2 }, // the version\n"
             "  \"dock\": { \"thickness\": 220 },\n  \"pages\": [{}]\n}\n"},
            // The same for a last `version` without a comma, which gains one before its comments.
            {"{\n  \"pages\": [{}],\n  \"version\": { \"major\": 5, \"minor\": 2 } /* v */ // last\n}\n",
             "{\n  \"pages\": [{}],\n  \"version\": { \"major\": 5, \"minor\": 2 }, /* v */ // last\n"
             "  \"dock\": { \"thickness\": 220 }\n}\n"},
            // An empty dock closed on its own line: the member gets its own line, one level deeper than the brace.
            {"{\n  \"version\": { \"major\": 5, \"minor\": 2 },\n  \"dock\": {\n  },\n  \"pages\": [{}]\n}\n",
             "{\n  \"version\": { \"major\": 5, \"minor\": 2 },\n  \"dock\": {\n    \"thickness\": 220\n  },\n"
             "  \"pages\": [{}]\n}\n"},
            // A document that breaks lines only with CR: the new lines and their indentation use CR too.
            {"{\r  \"version\": {\r    \"major\": 5\r  },\r  \"pages\": [{}]\r}\r",
             "{\r  \"version\": {\r    \"major\": 5,\r    \"minor\": 2\r  },\r  \"dock\": { \"thickness\": 220 },\r"
             "  \"pages\": [{}]\r}\r"},
        };
        for (const LayoutCase& layout : cases)
        {
            AppSettings dragged{};
            AppSettings reparsed{};
            if (FAILED(ParseAppSettingsJson(layout.source, dragged)) || PatchDockThickness(dragged, 220) != S_OK ||
                dragged.sourceDocument != layout.expected || dragged.versionMinor != kRedXeSettingsDockMinor ||
                FAILED(ParseAppSettingsJson(dragged.sourceDocument, reparsed)) || reparsed.dock != dragged.dock ||
                reparsed.versionMinor != dragged.versionMinor)
            {
                std::wprintf(L"PatchDockThickness did not follow the document's layout:\n%hs\n",
                             dragged.sourceDocument.c_str());
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }
        // The diagnostic for a member after a line comment ended by a lone CR is located where the parser reads it.
        constexpr std::string_view loneCr =
            "{\"version\":{\"major\":5,\"minor\":2},// note\r\"dock\":{\"peek\":65},\"pages\":[{}]}";
        const size_t peekAt = loneCr.find("65");
        AppSettings diagnosed{};
        SettingsParseDiagnostic diagnostic{};
        if (SUCCEEDED(ParseAppSettingsJsonDetailed(loneCr, diagnosed, diagnostic)) ||
            diagnostic.path != "$.dock.peek" || !diagnostic.hasLocation || diagnostic.byteOffset != peekAt ||
            diagnostic.line != 1 || diagnostic.column != peekAt + 1)
        {
            std::wprintf(L"A member after a line comment ended by a lone CR was located at offset %llu, not %zu.\n",
                         static_cast<unsigned long long>(diagnostic.byteOffset), peekAt);
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        // A release at the thickness the document already has (a click on the edge) adds no `dock` and no minor.
        constexpr std::string_view noDock = R"json({"version":{"major":5,"minor":1},"pages":[{}]})json";
        AppSettings clicked{};
        if (FAILED(ParseAppSettingsJson(noDock, clicked)) ||
            PatchDockThickness(clicked, clicked.dock.thicknessDips) != S_FALSE || clicked.sourceDocument != noDock ||
            clicked.versionMinor != 1)
        {
            std::wprintf(L"PatchDockThickness changed the document for an unchanged thickness.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        // Typed settings that no longer describe the document (here a typed edge the text does not have): the patched
        // text would not parse back to the running dock, so the source, the thickness, and the minor stay.
        AppSettings diverged{};
        if (FAILED(ParseAppSettingsJson(noDock, diverged)))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        diverged.dock.edge = DockEdge::Top;
        if (PatchDockThickness(diverged, 200) != HRESULT_FROM_WIN32(ERROR_INVALID_DATA) ||
            diverged.sourceDocument != noDock || diverged.dock.thicknessDips != kDockDefaultThicknessDips ||
            diverged.versionMinor != 1)
        {
            std::wprintf(L"PatchDockThickness kept a patch that does not parse back to the running dock.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

// The `trayIcon` root member (minor 3): omitted, it follows the build (Release shows the notification-area icon,
// Debug hides it), also in an older-minor document; an authored boolean wins in both builds; anything else rejects
// the document with the diagnostic on $.trayIcon. A toggle changes no other typed member, is a runtime change by the
// check a live reload makes before it applies anything (RuntimeSettingsEqual), and keeps the active page
// (ActiveDashboardRuntimeEquals), so the host applies it without rebuilding the page.
[[nodiscard]] HRESULT ValidateTrayIconSettings() noexcept
{
#if defined(_DEBUG)
    constexpr bool buildDefault = false;
#else
    constexpr bool buildDefault = true;
#endif
    AppSettings omitted{};
    AppSettings olderMinor{};
    AppSettings shown{};
    AppSettings hidden{};
    if (kRedXeDefaultTrayIcon != buildDefault || AppSettings{}.trayIcon != buildDefault ||
        FAILED(ParseAppSettingsJson(R"json({"version":{"major":5,"minor":3},"pages":[{}]})json", omitted)) ||
        omitted.trayIcon != buildDefault ||
        FAILED(ParseAppSettingsJson(R"json({"version":{"major":5,"minor":2},"pages":[{}]})json", olderMinor)) ||
        olderMinor.trayIcon != buildDefault ||
        FAILED(
            ParseAppSettingsJson(R"json({"version":{"major":5,"minor":3},"trayIcon":true,"pages":[{}]})json", shown)) ||
        !shown.trayIcon ||
        FAILED(ParseAppSettingsJson(R"json({"version":{"major":5,"minor":3},"trayIcon":false,"pages":[{}]})json",
                                    hidden)) ||
        hidden.trayIcon)
    {
        std::wprintf(L"trayIcon did not parse to the build default or the authored boolean.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    // Only the member differs between the two documents, and the typed settings see it.
    AppSettings toggled = hidden;
    toggled.sourceDocument = shown.sourceDocument;
    toggled.trayIcon = true;
    if (toggled != shown)
    {
        std::wprintf(L"trayIcon changed typed settings other than trayIcon.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (RuntimeSettingsEqual(hidden, shown) || RuntimeSettingsEqual(shown, hidden) ||
        !ActiveDashboardRuntimeEquals(hidden, shown))
    {
        std::wprintf(L"A trayIcon toggle is not a runtime change that keeps the active page.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    const std::string_view rejected[]{
        R"json({"version":{"major":5,"minor":3},"trayIcon":"true","pages":[{}]})json",
        R"json({"version":{"major":5,"minor":3},"trayIcon":1,"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":3},"trayIcon":null,"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":3},"trayIcon":{},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":3},"trayIcon":true,"trayIcon":false,"pages":[{}]})json",
    };
    for (const std::string_view document : rejected)
    {
        if (FAILED(ExpectRejected(document)))
        {
            std::wprintf(L"A malformed trayIcon was accepted.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    AppSettings diagnosed{};
    SettingsParseDiagnostic diagnostic{};
    if (SUCCEEDED(ParseAppSettingsJsonDetailed(
            R"json({"version":{"major":5,"minor":3},"trayIcon":"yes","pages":[{}]})json", diagnosed, diagnostic)) ||
        diagnostic.path != "$.trayIcon")
    {
        std::wprintf(L"The trayIcon diagnostic path is %hs, not $.trayIcon.\n", diagnostic.path.c_str());
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

// RuntimeSettingsEqual, the check a live reload makes before it applies anything (Application::ApplySettings): a
// candidate that differs from the running settings in any one root member is a runtime change, and one that differs
// only in the retained source text or the retired services entries, which nothing runs, only becomes the source.
[[nodiscard]] HRESULT ValidateRuntimeSettingsEqual() noexcept
{
    try
    {
        constexpr std::string_view document = R"json({
          "version":{"major":5,"minor":3},
          "services":{"Keypad":{"plugin":"builtin.logicon"},"Meet":{"plugin":"builtin.zoom"}},
          "pages":[{"widgets":[{"plugin":"builtin.gdi-orbit"}]}]
        })json";
        AppSettings running{};
        if (FAILED(ParseAppSettingsJson(document, running)) || running.pluginCount != 1 || running.serviceCount != 1 ||
            running.retiredServices.size() != 1 || !RuntimeSettingsEqual(running, running))
        {
            std::wprintf(L"The runtime comparison document did not parse as expected.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        struct MemberChange final
        {
            const wchar_t* member;
            void (*change)(AppSettings& settings) noexcept;
            bool runtime;
        };
        const MemberChange changes[]{
            {L"versionMajor", [](AppSettings& settings) noexcept { ++settings.versionMajor; }, true},
            {L"versionMinor", [](AppSettings& settings) noexcept { ++settings.versionMinor; }, true},
            {L"logRetentionDays", [](AppSettings& settings) noexcept { ++settings.logRetentionDays; }, true},
            {L"backgroundRgb", [](AppSettings& settings) noexcept { settings.backgroundRgb ^= 0x010203U; }, true},
            {L"dock", [](AppSettings& settings) noexcept { ++settings.dock.animationMilliseconds; }, true},
            {L"trayIcon", [](AppSettings& settings) noexcept { settings.trayIcon = !settings.trayIcon; }, true},
            {L"plugins",
             [](AppSettings& settings) noexcept { settings.plugins[0].enabled = !settings.plugins[0].enabled; }, true},
            {L"pluginCount", [](AppSettings& settings) noexcept { ++settings.pluginCount; }, true},
            {L"services", [](AppSettings& settings) noexcept { settings.services[0].name = SettingsText{}; }, true},
            {L"serviceCount", [](AppSettings& settings) noexcept { ++settings.serviceCount; }, true},
            {L"dashboard", [](AppSettings& settings) noexcept
             { settings.dashboard.wrapPages = !settings.dashboard.wrapPages; }, true},
            {L"sourceDocument", [](AppSettings& settings) noexcept { settings.sourceDocument.clear(); }, false},
            {L"retiredServices", [](AppSettings& settings) noexcept { settings.retiredServices.clear(); }, false},
        };
        for (const MemberChange& change : changes)
        {
            AppSettings candidate = running;
            change.change(candidate);
            if (candidate == running || RuntimeSettingsEqual(candidate, running) == change.runtime ||
                RuntimeSettingsEqual(running, candidate) == change.runtime)
            {
                std::wprintf(L"A change of %s alone is %s.\n", change.member,
                             change.runtime ? L"not a runtime change" : L"a runtime change");
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

// The command-line catalog (RedXe/CommandLine.h): every switch is unique, well formed, and printed by --help; the
// help aliases are recognized; the help says a run without switches can be a bar; the unattended runs, a capture run's
// exit code, the exit-code box, and the names the failure-exit record gives the codes follow the policy Main.cpp
// applies; the argument scanner accepts a full valid line and names the first stray token.
[[nodiscard]] HRESULT ValidateCommandLineCatalog() noexcept
{
    try
    {
        const std::wstring help = RedXeFormatCommandLineHelp();
        for (size_t index = 0; index < kRedXeCommandLineSwitches.size(); ++index)
        {
            const RedXeCommandLineSwitch& entry = kRedXeCommandLineSwitches[index];
            const std::wstring_view name{entry.name};
            if (static_cast<size_t>(entry.id) != index || !name.starts_with(L"--") || name.size() < 3 ||
                !entry.summary || entry.summary[0] == L'\0' || !entry.group ||
                (entry.valueKind != RedXeSwitchValue::None) != (entry.value != nullptr))
            {
                std::wprintf(L"Command-line switch %zu is malformed.\n", index);
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            for (size_t other = 0; other < index; ++other)
            {
                if (name == kRedXeCommandLineSwitches[other].name)
                {
                    std::wprintf(L"Command-line switch %s is declared twice.\n", entry.name);
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
            }
            bool groupKnown = false;
            for (const wchar_t* group : kRedXeCommandLineGroups)
            {
                groupKnown = groupKnown || std::wstring_view{group} == entry.group;
            }
            const std::wstring lead = std::wstring(L"  ") + entry.name;
            // The summary is word-wrapped under the switch, so its first words are what stays contiguous.
            const std::wstring_view summaryStart = std::wstring_view{entry.summary}.substr(0, 24);
            if (!groupKnown || help.find(lead) == std::wstring::npos || help.find(summaryStart) == std::wstring::npos)
            {
                std::wprintf(L"--help does not print %s with its summary under a known group.\n", entry.name);
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }
        if (help.find(L"Usage: RedXe.exe") == std::wstring::npos || help.find(L"Exit codes:") == std::wstring::npos ||
            help.find(L"-h, /?, -?") == std::wstring::npos)
        {
            std::wprintf(L"--help lacks the usage line, the exit codes, or the help aliases.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        // Without switches RedXe follows the settings file, so a `dock` there, including the one a first start without
        // a XENEON writes, makes it a bar (the mode table's Dock row): --help names both, not only the two windows.
        if (help.find(L"bar on a screen edge") == std::wstring::npos ||
            help.find(L"first start without a XENEON") == std::wstring::npos)
        {
            std::wprintf(L"--help does not say that a run without switches can be a bar, or which file writes one.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        // The alias launcher waits exactly for the modes that end on their own and forwards their exit code. Every
        // other switch (--settings, --warp, --dock*, --page, --widget, --after, --crash-test-directory) only modifies
        // a run: without one of those modes the line starts the dashboard, and waiting for it would hold the terminal
        // for the whole session.
        for (const RedXeCommandLineSwitch& entry : kRedXeCommandLineSwitches)
        {
            const bool endsOnItsOwn = entry.id == RedXeSwitch::Help || entry.id == RedXeSwitch::Screenshot ||
                                      entry.id == RedXeSwitch::SelfTest || entry.id == RedXeSwitch::CrashTest ||
                                      entry.id == RedXeSwitch::CrashTestStackOverflow;
            if (entry.launcherWaitForExit != endsOnItsOwn)
            {
                std::wprintf(L"The launcher wait policy of %s does not match whether it ends on its own.\n",
                             entry.name);
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }
        // Main.cpp: --self-test and --screenshot are the unattended runs, which never show a modal box (command-line
        // errors, the settings fallback notice, the prompts, and the exit-code box all follow RedXeIsUnattendedRun); a
        // --screenshot run exits 8 whenever no PNG was written, also when a startup or graphics failure ended it
        // first; only a failed interactive run shows the exit-code box.
        if (RedXeIsUnattendedRun(false, false) || !RedXeIsUnattendedRun(true, false) ||
            !RedXeIsUnattendedRun(false, true) || !RedXeIsUnattendedRun(true, true) ||
            RedXeScreenshotExitCode(0, true) != 0 || RedXeScreenshotExitCode(0, false) != 8 ||
            RedXeScreenshotExitCode(5, false) != 8 || RedXeScreenshotExitCode(1, false) != 8 ||
            RedXeScreenshotExitCode(5, true) != 5 || !RedXeShowsExitCodeBox(5, false, false) ||
            RedXeShowsExitCodeBox(0, false, false) || RedXeShowsExitCodeBox(5, false, true) ||
            RedXeShowsExitCodeBox(8, false, true) || RedXeShowsExitCodeBox(5, true, false) ||
            RedXeShowsExitCodeBox(2, true, true))
        {
            std::wprintf(L"The unattended-run policy, the --screenshot exit code, or the exit-code box is wrong.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        // The failure-exit record names each code the way --help lists it.
        for (const int code : {1, 2, 3, 5, 7, 8})
        {
            const std::string_view name{RedXeExitCodeName(code)};
            const std::wstring listed = std::to_wstring(code) + L" " + std::wstring(name.begin(), name.end());
            if (help.find(listed) == std::wstring::npos)
            {
                std::wprintf(L"--help does not list exit code %d as %hs.\n", code, RedXeExitCodeName(code));
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }
        if (std::string_view{RedXeExitCodeName(4)} != "startup" || std::string_view{RedXeExitCodeName(6)} != "startup")
        {
            std::wprintf(L"An exit code --help does not list has a name of its own.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        for (const wchar_t* alias : kRedXeHelpArguments)
        {
            if (!RedXeIsHelpArgument(alias) || RedXeFindSwitch(alias) != &RedXeSwitchInfo(RedXeSwitch::Help))
            {
                std::wprintf(L"Help alias %s is not recognized.\n", alias);
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }
        if (RedXeIsHelpArgument(L"--HELP") || RedXeIsHelpArgument(L"help") || RedXeFindSwitch(L"--settings=x") ||
            RedXeFindSwitch(L"--crash-test-directory") || RedXeFindSwitch(L"--crash-test-directory=") ||
            !RedXeFindSwitch(L"--crash-test-directory=C:\\dumps") || RedXeFindSwitch(L"--dock-peek=4"))
        {
            std::wprintf(L"Switch matching is not exact.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        wchar_t exe[] = L"RedXe.exe";
        wchar_t settings[] = L"--settings";
        wchar_t path[] = L"C:\\Dash\\bar.settings.json";
        wchar_t dock[] = L"--dock";
        wchar_t edge[] = L"bottom@primary";
        wchar_t mode[] = L"--dock-mode";
        wchar_t autohide[] = L"autohide";
        wchar_t screenshot[] = L"--screenshot";
        wchar_t png[] = L"--looks-like-a-switch.png";
        wchar_t warp[] = L"--warp";
        wchar_t crash[] = L"--crash-test-directory=C:\\dumps";
        wchar_t stray[] = L"--dock-peek=4";
        wchar_t* valid[]{exe, settings, path, dock, edge, mode, autohide, screenshot, png, warp, crash};
        wchar_t* invalid[]{exe, settings, path, stray, warp};
        wchar_t* dangling[]{exe, warp, settings};
        if (RedXeFindUnknownArgument(valid, static_cast<int>(std::size(valid))) != nullptr ||
            RedXeFindUnknownArgument(invalid, static_cast<int>(std::size(invalid))) != stray ||
            RedXeFindUnknownArgument(dangling, static_cast<int>(std::size(dangling))) != nullptr ||
            RedXeFindUnknownArgument(valid, 1) != nullptr)
        {
            std::wprintf(L"The unknown-argument scanner misjudged a command line.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT ValidateAvControlSettings() noexcept
{
    try
    {
        const std::string prefix = R"({"version":{"major":5},"declare":{"AV":{"plugin":"builtin.av-control")";
        const std::string suffix = R"(}},"pages":[{"widgets":["AV",{"use":"AV","profiles":[]}]}]})";
        const std::string profile =
            R"({"id":"office","name":"Office","outputId":"opaque-output","microphoneId":"opaque-mic","cameraId":"opaque-camera","audioRoles":"communications","restoreLevels":true,"outputLevel":45,"microphoneLevel":67})";
        auto settings = std::make_unique<AppSettings>();
        if (FAILED(ParseAppSettingsJson(prefix + R"(,"profiles":[)" + profile + "]" + suffix, *settings)))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        const auto& page = settings->dashboard.pages[0];
        if (page.widgetCount != 2 ||
            page.widgets[0].privateConfiguration.View().find("opaque-camera") == std::string_view::npos ||
            page.widgets[1].privateConfiguration.View() != R"({"profiles":[]})" ||
            FAILED(ValidateAppSettings(*settings)))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        const std::array<std::string, 5> invalid{
            R"(,"profiles":[{"id":"incomplete"}])", R"(,"profiles":[],"mute":true)",
            R"(,"profiles":[)" + profile + "," + profile + "]",
            R"(,"profiles":[)" + profile.substr(0, profile.size() - 1) + R"(,"microphoneLevel":101}])",
            R"(,"profiles":{})"};
        for (const auto& value : invalid)
            if (SUCCEEDED(ParseAppSettingsJson(prefix + value + suffix, *settings)))
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        // A missing profile list selects the declared default; loading either form only builds configuration.
        if (FAILED(ParseAppSettingsJson(prefix + suffix, *settings)) ||
            settings->dashboard.pages[0].widgets[0].privateConfiguration.View() != R"({"profiles":[]})")
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const std::length_error&)
    {
        return E_INVALIDARG;
    }
}

[[nodiscard]] HRESULT ValidatePersistFormatting() noexcept
{
    try
    {
        constexpr std::string_view patch =
            R"json({"shortcuts":[{"target":"C:\\Apps\\éditeur.exe"},{"target":"https://example.com/a?x=1&y=2"}]})json";
        const std::array<std::string_view, 3> widgets{R"json("Launch")json",
                                                      R"json({"plugin":"builtin.launcher","shortcuts":[]})json",
                                                      R"json({"use":"Launch","shortcuts":[]})json"};
        for (const auto widget : widgets)
        {
            const std::string source =
                R"json({"version":{"major":5,"minor":4},"futureRoot":{"label":"keep\nthis","quoted":"\"{}[],:\\","list":[[],{},true,false,null,1.25e-4,-2]},"declare":{"Launch":{"plugin":"builtin.launcher"}},"pages":[{"widgets":[)json" +
                std::string(widget) + R"json(]}]})json";
            auto settings = std::make_unique<AppSettings>();
            if (FAILED(ParseAppSettingsJson(source, *settings)))
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            const auto id = settings->dashboard.pages[0].widgets[0].id;
            if (FAILED(PatchWidgetInstanceSettings(*settings, id.View(), patch)) ||
                settings->sourceDocument.find("\n  \"version\": { \"major\": 5, \"minor\": 4 }") == std::string::npos ||
                settings->sourceDocument.find("\"Launch\": { \"plugin\": \"builtin.launcher\" }") ==
                    std::string::npos ||
                settings->sourceDocument.find("\"layout\"") != std::string::npos ||
                settings->sourceDocument.find("\"shortcuts\": [\n") == std::string::npos ||
                settings->sourceDocument.back() != '\n' ||
                settings->sourceDocument.find("{ \"target\": \"C:\\\\Apps\\\\éditeur.exe\" }") == std::string::npos ||
                settings->sourceDocument.find("\"futureRoot\":") == std::string::npos ||
                settings->dashboard.pages[0].widgets[0].privateConfiguration.View().find("éditeur.exe") ==
                    std::string_view::npos ||
                settings->dashboard.pages[0].widgets[0].privateConfiguration.View().find(
                    "\"iconSize\":\"automatic\"") == std::string_view::npos)
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            const std::string formatted = settings->sourceDocument;
            unique_doc before{yyjson_read(source.data(), source.size(), YYJSON_READ_NOFLAG)};
            unique_doc after{yyjson_read(formatted.data(), formatted.size(), YYJSON_READ_NOFLAG)};
            if (!before || !after ||
                !yyjson_equals(yyjson_obj_get(yyjson_doc_get_root(before.get()), "futureRoot"),
                               yyjson_obj_get(yyjson_doc_get_root(after.get()), "futureRoot")))
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            auto reloaded = std::make_unique<AppSettings>();
            if (FAILED(ParseAppSettingsJson(formatted, *reloaded)) ||
                reloaded->dashboard.pages[0].widgets[0].privateConfiguration.View().find("éditeur.exe") ==
                    std::string_view::npos ||
                reloaded->dashboard.pages[0].widgets[0].privateConfiguration.View().find(
                    "\"iconSize\":\"automatic\"") == std::string_view::npos)
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            // The same patch again changes nothing and leaves the source alone; a real change and its reversal format
            // back to the same bytes.
            if (PatchWidgetInstanceSettings(*settings, id.View(), patch) != S_FALSE ||
                settings->sourceDocument != formatted ||
                PatchWidgetInstanceSettings(*settings, id.View(), R"json({"iconSize":"large"})json") != S_OK ||
                settings->sourceDocument == formatted ||
                PatchWidgetInstanceSettings(*settings, id.View(), R"json({"iconSize":"automatic"})json") != S_OK ||
                settings->sourceDocument != formatted)
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        // Save a complete shipped-template preview using the same production persistence formatter.
        std::string templateJson;
        std::filesystem::path templatePath;
        auto preview = std::make_unique<AppSettings>();
        if (FAILED(GetDeployedPath(L"RedXe.settings.json", templatePath)) ||
            FAILED(ReadFile(templatePath, templateJson)) || FAILED(ParseAppSettingsJson(templateJson, *preview)))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        bool previewSaved = false;
        for (uint32_t page = 0; page < preview->dashboard.pageCount && !previewSaved; ++page)
        {
            auto& layout = preview->dashboard.pages[page];
            for (uint32_t item = 0; item < layout.widgetCount && !previewSaved; ++item)
            {
                auto& widget = layout.widgets[item];
                if (widget.pluginId.View() != "builtin.launcher")
                    continue;
                // Long single-property shortcut records stay on one line.
                const std::string longPatch =
                    R"({"shortcuts":[{"target":"https://example.com/)" + std::string(160, 'a') + R"("}]})";
                if (FAILED(PatchWidgetInstanceSettings(*preview, widget.id.View(), longPatch)) ||
                    preview->sourceDocument.find("{ \"target\": \"https://example.com/" + std::string(160, 'a') +
                                                 "\" }") == std::string::npos ||
                    FAILED(PatchWidgetInstanceSettings(*preview, widget.id.View(), patch)))
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                if (preview->sourceDocument.find("\"columns\"") == std::string::npos ||
                    preview->sourceDocument.find("\"layout\"") != std::string::npos)
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                unique_doc document{yyjson_read(preview->sourceDocument.data(), preview->sourceDocument.size(), 0)};
                if (!document)
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                size_t bytes = 0;
                using unique_text = wil::unique_any<char*, decltype(&free), free>;
                unique_text expanded{yyjson_write(document.get(), YYJSON_WRITE_PRETTY_TWO_SPACES, &bytes)};
                if (!expanded)
                    return E_OUTOFMEMORY;
                const auto lines = [](std::string_view text)
                {
                    size_t count = 0;
                    for (const char c : text)
                        count += c == '\n' ? 1U : 0U;
                    return count;
                };
                const size_t compactLines = lines(preview->sourceDocument),
                             expandedLines = lines({expanded.get(), bytes});
                if (compactLines * 100 > expandedLines * 70)
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                std::printf("Settings formatting: %zu lines instead of %zu.\n", compactLines, expandedLines);
                std::ofstream stream(templatePath.parent_path().parent_path() / L"compact-settings-preview.json",
                                     std::ios::binary);
                stream << preview->sourceDocument;
                if (!stream)
                    return E_FAIL;
                previewSaved = true;
            }
        }
        if (!previewSaved)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

        // A compact accepted document must not be written as an oversized, subsequently unreadable pretty document.
        std::string large =
            R"json({"version":{"major":5,"minor":4},"pages":[{"widgets":[{"plugin":"builtin.launcher"}]}],"futurePadding":")json";
        large.append(1024U * 1024U - large.size() - 2, 'x');
        large += "\"}";
        auto settings = std::make_unique<AppSettings>();
        if (FAILED(ParseAppSettingsJson(large, *settings)))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        const auto& widget = settings->dashboard.pages[0].widgets[0];
        const std::string previousPrivate(widget.privateConfiguration.View());
        if (PatchWidgetInstanceSettings(*settings, widget.id.View(), patch) !=
                HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ||
            settings->sourceDocument != large || widget.privateConfiguration.View() != previousPrivate)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

struct LowStackContext final
{
    HRESULT result = E_FAIL;
};

DWORD WINAPI ParseOnLowStack(void* context) noexcept
{
    auto* state = static_cast<LowStackContext*>(context);
    auto settings = std::make_unique<AppSettings>();
    state->result = ParseAppSettingsJson(kRepresentative, *settings);
    return 0;
}

[[nodiscard]] HRESULT ValidateLowStack() noexcept
{
    LowStackContext context{};
    wil::unique_handle thread{
        CreateThread(nullptr, 128U * 1024U, ParseOnLowStack, &context, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr)};
    if (!thread)
        return HRESULT_FROM_WIN32(GetLastError());
    return WaitForSingleObject(thread.get(), 5000) == WAIT_OBJECT_0 ? context.result
                                                                    : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
}

[[nodiscard]] bool WaitForMessage(HWND window, DWORD timeout) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + timeout;
    while (GetTickCount64() < deadline)
    {
        MSG message{};
        if (PeekMessageW(&message, window, SettingsWatcher::kSettingsChangedMessage,
                         SettingsWatcher::kSettingsChangedMessage, PM_REMOVE))
            return true;
        const DWORD remaining = static_cast<DWORD>(deadline - GetTickCount64());
        if (MsgWaitForMultipleObjectsEx(0, nullptr, remaining, QS_POSTMESSAGE, MWMO_INPUTAVAILABLE) == WAIT_TIMEOUT)
            return false;
    }
    return false;
}

[[nodiscard]] HRESULT ValidateWatcher() noexcept
{
    try
    {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                                (L"RedXe.SettingsV4Tests." + std::to_wstring(GetCurrentProcessId()) +
                                                 L"." + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directory(directory);
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                std::error_code error;
                std::filesystem::remove_all(directory, error);
            });
        wil::unique_hwnd window{
            CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr)};
        if (!window)
            return HRESULT_FROM_WIN32(GetLastError());
        SettingsWatcher watcher;
        HRESULT result = watcher.Start(window.get(), directory.wstring());
        if (FAILED(result) || !WaitForMessage(window.get(), 5000))
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        watcher.AcknowledgeNotification();
        const std::filesystem::path file = directory / L"external.settings.json";
        {
            std::ofstream stream(file, std::ios::binary);
            stream << kRepresentative;
            stream.flush();
        }
        if (!WaitForMessage(window.get(), 5000))
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        SettingsFileStamp stamp{};
        if (QuerySettingsFileStamp(file.wstring(), stamp) != S_OK || stamp.fileSize == 0)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        watcher.Stop();
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT ValidateExternalSelection() noexcept
{
    try
    {
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path() /
            (L"RedXe.ExternalSettingsTests." + std::to_wstring(GetCurrentProcessId()) + L"." +
             std::to_wstring(GetTickCount64()));
        std::filesystem::create_directory(directory);
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                std::error_code error;
                std::filesystem::remove_all(directory, error);
            });
        const std::filesystem::path selected = directory / L"portable.json";
        {
            std::ofstream stream(selected, std::ios::binary);
            stream << "invalid";
        }
        SettingsStore fallbackStore;
        std::unique_ptr<AppSettings> fallback;
        HRESULT result = fallbackStore.Initialize(false, selected.wstring(), fallback);
        std::string unchanged;
        if (SUCCEEDED(result))
            result = ReadFile(selected, unchanged);
        if (FAILED(result) || !fallback || !fallbackStore.UsedInitialFallback() || unchanged != "invalid" ||
            fallbackStore.SettingsPath() != std::filesystem::absolute(selected).wstring())
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        constexpr std::string_view v4Portable = R"json({"version":{"major":4},"pages":[{}]})json";
        {
            std::ofstream stream(selected, std::ios::binary | std::ios::trunc);
            stream << v4Portable;
        }
        SettingsStore v4Store;
        std::unique_ptr<AppSettings> v4Fallback;
        result = v4Store.Initialize(false, selected.wstring(), v4Fallback);
        std::string v4Unchanged;
        if (SUCCEEDED(result))
            result = ReadFile(selected, v4Unchanged);
        if (FAILED(result) || !v4Fallback || !v4Store.UsedInitialFallback() || v4Unchanged != v4Portable ||
            v4Fallback->versionMajor != kRedXeSettingsVersionMajor)
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        {
            std::ofstream stream(selected, std::ios::binary | std::ios::trunc);
            stream << kRepresentative;
        }
        SettingsStore selectedStore;
        std::unique_ptr<AppSettings> loaded;
        result = selectedStore.Initialize(false, selected.wstring(), loaded);
        if (FAILED(result) || !loaded || selectedStore.UsedInitialFallback() || !loaded->dashboard.wrapPages)
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT ValidateDefaultRecovery() noexcept
{
    try
    {
        const std::filesystem::path localRoot =
            std::filesystem::temp_directory_path() /
            (L"RedXe.DefaultRecoveryTests." + std::to_wstring(GetCurrentProcessId()) + L"." +
             std::to_wstring(GetTickCount64()));
        const std::filesystem::path settingsDirectory = localRoot / L"RedXe" / L"Settings";
        std::filesystem::create_directories(settingsDirectory);
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                std::error_code error;
                std::filesystem::remove_all(localRoot, error);
            });
#if defined(_DEBUG)
        constexpr const wchar_t* selectedName = kRedXeDebugSettingsFileName;
#else
        constexpr const wchar_t* selectedName = kRedXeReleaseSettingsFileName;
#endif
        constexpr std::string_view invalidBytes = "invalid default bytes\r\n";
        const std::filesystem::path selected = settingsDirectory / selectedName;
        {
            std::ofstream stream(selected, std::ios::binary);
            stream.write(invalidBytes.data(), static_cast<std::streamsize>(invalidBytes.size()));
        }

        SettingsStore store;
        std::unique_ptr<AppSettings> recovered;
        HRESULT result = store.Initialize(false, {}, recovered, localRoot.wstring());
        if (FAILED(result) || !recovered || !store.UsedInitialFallback() ||
            store.InitialNotice().find(L".invalid-") == std::wstring::npos)
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        size_t backupCount = 0;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(settingsDirectory))
        {
            const std::wstring name = entry.path().filename().wstring();
            if (!name.starts_with(std::filesystem::path(selectedName).stem().wstring() + L".invalid-") ||
                entry.path().extension() != L".json")
                continue;
            ++backupCount;
            std::string preserved;
            result = ReadFile(entry.path(), preserved);
            if (FAILED(result) || preserved != invalidBytes)
                return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        AppSettings installed{};
        if (backupCount != 1 || FAILED(LoadAppSettingsFile(selected.wstring(), installed)))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        const std::filesystem::path v4Root =
            std::filesystem::temp_directory_path() /
            (L"RedXe.DefaultRecoveryV4Tests." + std::to_wstring(GetCurrentProcessId()) + L"." +
             std::to_wstring(GetTickCount64()));
        const std::filesystem::path v4Directory = v4Root / L"RedXe" / L"Settings";
        std::filesystem::create_directories(v4Directory);
        const auto cleanupV4 = wil::scope_exit(
            [&]() noexcept
            {
                std::error_code error;
                std::filesystem::remove_all(v4Root, error);
            });
        constexpr std::string_view v4Bytes = R"json({"version":{"major":4},"pages":[{}]})json";
        const std::filesystem::path v4Selected = v4Directory / selectedName;
        {
            std::ofstream stream(v4Selected, std::ios::binary);
            stream.write(v4Bytes.data(), static_cast<std::streamsize>(v4Bytes.size()));
        }
        SettingsStore v4Store;
        std::unique_ptr<AppSettings> v4Recovered;
        result = v4Store.Initialize(false, {}, v4Recovered, v4Root.wstring());
        if (FAILED(result) || !v4Recovered || !v4Store.UsedInitialFallback() ||
            v4Recovered->versionMajor != kRedXeSettingsVersionMajor ||
            v4Store.InitialNotice().find(L".invalid-") == std::wstring::npos)
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        size_t v4BackupCount = 0;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(v4Directory))
        {
            const std::wstring name = entry.path().filename().wstring();
            if (!name.starts_with(std::filesystem::path(selectedName).stem().wstring() + L".invalid-") ||
                entry.path().extension() != L".json")
                continue;
            ++v4BackupCount;
            std::string preserved;
            result = ReadFile(entry.path(), preserved);
            if (FAILED(result) || preserved != v4Bytes)
                return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        AppSettings v4Installed{};
        if (v4BackupCount != 1 || FAILED(LoadAppSettingsFile(v4Selected.wstring(), v4Installed)) ||
            v4Installed.versionMajor != kRedXeSettingsVersionMajor)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

// Settings written by the public v1.0.102 release load unchanged (Core_Settings.md "Version 5 document"). Its templates
// carry the retired services Zoom entry with its retired members, which loads and is ignored, and the Debug one binds
// removed zoom.* verbs, which stay invalid bindings rather than document errors. The default store keeps such a file
// byte for byte instead of backing it up and replacing it.
[[nodiscard]] HRESULT ValidateReleasedTemplates() noexcept
{
    struct ReleasedTemplate final
    {
        const wchar_t* name;
        std::string_view text;
        uint32_t pageCount;
        bool bindsRemovedZoomVerbs;
    };
    const ReleasedTemplate releasedTemplates[]{{L"Release", kV102ReleaseTemplate, 4, false},
                                               {L"Debug", kV102DebugTemplate, 5, true}};
    for (const ReleasedTemplate& released : releasedTemplates)
    {
        AppSettings loaded{};
        SettingsParseDiagnostic diagnostic{};
        const HRESULT result = ParseAppSettingsJsonDetailed(released.text, loaded, diagnostic);
        const ServiceSettings* logicon = SUCCEEDED(result) ? FindServiceSettings(loaded, "builtin.logicon") : nullptr;
        if (FAILED(result) || FAILED(ValidateAppSettings(loaded)) || loaded.versionMinor != 2 ||
            loaded.dashboard.pageCount != released.pageCount || !logicon || loaded.serviceCount != 1 ||
            loaded.retiredServices.size() != 1 || loaded.retiredServices[0].View() != "builtin.zoom")
        {
            std::wprintf(L"The v1.0.102 %s template did not load unchanged: %S %S\n", released.name,
                         diagnostic.path.c_str(), diagnostic.message.c_str());
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (released.bindsRemovedZoomVerbs &&
            logicon->privateConfiguration.View().find("\"zoom.signIn\"") == std::string_view::npos)
        {
            std::wprintf(L"The v1.0.102 Debug template lost its removed zoom.* bindings.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }

    // The retired Zoom entry loads with any value of every retired member, also in a current-minor document, and
    // without them (the template entry of earlier builds); either way it is recorded for the warning and never started.
    constexpr std::string_view anyRetiredValue =
        R"json({"version":{"major":5,"minor":3},"services":{"Z":{"plugin":"builtin.zoom","clientId":null,"redirectPort":"48123","domain":[],"displayName":7,"autoConnect":"no","mode":{"x":1},"labels":[{"mute":2}]}},"pages":[{}]})json";
    constexpr std::string_view noRetiredMember =
        R"json({"version":{"major":5,"minor":3},"services":{"Z":{"plugin":"builtin.zoom"}},"pages":[{}]})json";
    AppSettings retired{};
    AppSettings current{};
    if (FAILED(ParseAppSettingsJson(anyRetiredValue, retired)) || FAILED(ValidateAppSettings(retired)) ||
        retired.serviceCount != 0 || retired.retiredServices.size() != 1 ||
        FAILED(ParseAppSettingsJson(noRetiredMember, current)) || FAILED(ValidateAppSettings(current)) ||
        current.serviceCount != 0 || current.retiredServices.size() != 1 ||
        current.retiredServices[0].View() != "builtin.zoom")
    {
        std::wprintf(L"The retired Zoom entry is not accepted with any retired member value and ignored.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    // v1.0.102 also accepted keys.down and mouse.down on a Logicon key, dialpad button, or turn. They still load: the
    // service receives them and makes each one an invalid binding (Plugins_Logicon.md).
    constexpr std::string_view heldBindings = R"json({
      "version":{"major":5,"minor":2},
      "services":{"Keypad":{"plugin":"builtin.logicon",
        "keys":[{"slot":4,"action":"keys.down","target":"Ctrl+Shift+M"}],
        "dialpad":{"buttons":[{"button":0,"action":"mouse.down","target":"left"}],
          "turns":[{"control":"dial","direction":"cw","action":"keys.down","target":"Shift"}]}}},
      "pages":[{"widgets":[{"plugin":"builtin.gdi-orbit"}]}]
    })json";
    AppSettings held{};
    if (FAILED(ParseAppSettingsJson(heldBindings, held)) || FAILED(ValidateAppSettings(held)) ||
        held.serviceCount != 1 ||
        held.services[0].privateConfiguration.View().find("\"keys.down\"") == std::string_view::npos ||
        held.services[0].privateConfiguration.View().find("\"mouse.down\"") == std::string_view::npos)
    {
        std::wprintf(L"A Logicon document with keys.down / mouse.down bindings did not load.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    try
    {
        const std::filesystem::path localRoot =
            std::filesystem::temp_directory_path() /
            (L"RedXe.ReleasedTemplateTests." + std::to_wstring(GetCurrentProcessId()) + L"." +
             std::to_wstring(GetTickCount64()));
        const std::filesystem::path settingsDirectory = localRoot / L"RedXe" / L"Settings";
        std::filesystem::create_directories(settingsDirectory);
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                std::error_code error;
                std::filesystem::remove_all(localRoot, error);
            });
#if defined(_DEBUG)
        constexpr const wchar_t* selectedName = kRedXeDebugSettingsFileName;
        constexpr std::string_view releasedBytes = kV102DebugTemplate;
#else
        constexpr const wchar_t* selectedName = kRedXeReleaseSettingsFileName;
        constexpr std::string_view releasedBytes = kV102ReleaseTemplate;
#endif
        const std::filesystem::path selected = settingsDirectory / selectedName;
        {
            std::ofstream stream(selected, std::ios::binary);
            stream.write(releasedBytes.data(), static_cast<std::streamsize>(releasedBytes.size()));
        }
        SettingsStore store;
        std::unique_ptr<AppSettings> loaded;
        HRESULT result = store.Initialize(false, {}, loaded, localRoot.wstring());
        std::string kept;
        if (SUCCEEDED(result))
            result = ReadFile(selected, kept);
        if (FAILED(result) || !loaded || store.UsedInitialFallback() || kept != releasedBytes ||
            loaded->versionMinor != 2 || loaded->serviceCount != 1 || loaded->retiredServices.size() != 1)
        {
            std::wprintf(L"The default store did not keep the v1.0.102 settings file.\n");
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(settingsDirectory))
        {
            if (entry.path().filename().wstring().starts_with(std::filesystem::path(selectedName).stem().wstring() +
                                                              L".invalid-"))
            {
                std::wprintf(L"The v1.0.102 settings file was backed up as invalid.\n");
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT ValidateLegacyReleaseFilenameMigration() noexcept
{
#if defined(_DEBUG)
    return S_OK;
#else
    try
    {
        const std::filesystem::path localRoot =
            std::filesystem::temp_directory_path() /
            (L"RedXe.ReleaseFilenameMigrationTests." + std::to_wstring(GetCurrentProcessId()) + L"." +
             std::to_wstring(GetTickCount64()));
        const std::filesystem::path settingsDirectory = localRoot / L"RedXe" / L"Settings";
        std::filesystem::create_directories(settingsDirectory);
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                std::error_code error;
                std::filesystem::remove_all(localRoot, error);
            });
        const std::filesystem::path legacy = settingsDirectory / L"RedXe-1.0.settings.json";
        const std::filesystem::path selected = settingsDirectory / kRedXeReleaseSettingsFileName;
        {
            std::ofstream stream(legacy, std::ios::binary);
            stream.write(kRepresentative.data(), static_cast<std::streamsize>(kRepresentative.size()));
        }

        SettingsStore store;
        std::unique_ptr<AppSettings> migrated;
        HRESULT result = store.Initialize(false, {}, migrated, localRoot.wstring());
        std::string selectedBytes;
        if (SUCCEEDED(result))
            result = ReadFile(selected, selectedBytes);
        if (FAILED(result) || !migrated || !migrated->dashboard.wrapPages || std::filesystem::exists(legacy) ||
            !std::filesystem::exists(selected) || selectedBytes != kRepresentative)
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
#endif
}

// First start without a XENEON (Core_Settings.md "Cold load and recovery"): PatchFirstRunDock inserts `dock` into both
// shipped templates as one commented run after `version`, in the file's own line breaks, and removes the template's
// commented-out `dock` example with the comment that says to uncomment it, leaving every other byte and member; the
// store writes that document, with every dock member as it was made, for a missing default file only, never over an
// existing one, never when it recovers an invalid one, and never for a `--settings` file, and it asks its provider for
// the dock only for that missing file, once, so no other start measures the displays for it.
[[nodiscard]] HRESULT ValidateFirstRunDock() noexcept
{
    try
    {
        // The installed file defines the dock once and never tells the reader to uncomment a second one.
        const auto definesOneDock = [](std::string_view text) noexcept
        {
            size_t docks = 0;
            for (size_t at = text.find("\"dock\""); at != std::string_view::npos; at = text.find("\"dock\"", at + 1))
                ++docks;
            return docks == 1 && text.find("Uncomment") == std::string_view::npos &&
                   text.find("uncomment") == std::string_view::npos;
        };
        // The first-run dock of a machine with two displays and a bottom taskbar: the top of the second screen.
        const auto withMonitor = [](DockSettings value, std::string_view monitor) noexcept
        {
            value.monitor = SettingsText{};
            monitor.copy(value.monitor.utf8.data(), monitor.size());
            value.monitor.bytes = static_cast<uint32_t>(monitor.size());
            return value;
        };
        DockSettings dock = withMonitor(DefaultDockSettings(), kDockSecondaryMonitor);
        dock.edge = DockEdge::Top;
        dock.mode = DockMode::Autohide;
        dock.thicknessDips = 720;
        const auto isFirstRunDock = [&dock](const DockSettings& value) noexcept
        {
            return value.edge == dock.edge && value.mode == dock.mode && value.thicknessDips == dock.thicknessDips &&
                   value.monitor.View() == dock.monitor.View() && value.reserveWorkArea &&
                   value.peekPixels == kDockDefaultPeekPixels &&
                   value.revealDelayMilliseconds == kDockDefaultRevealDelayMilliseconds &&
                   value.hideDelayMilliseconds == kDockDefaultHideDelayMilliseconds;
        };
        const auto sameExceptDock = [](const AppSettings& left, const AppSettings& right) noexcept
        {
            return left.versionMajor == right.versionMajor && left.versionMinor == right.versionMinor &&
                   left.logRetentionDays == right.logRetentionDays && left.backgroundRgb == right.backgroundRgb &&
                   left.trayIcon == right.trayIcon && left.plugins == right.plugins &&
                   left.pluginCount == right.pluginCount && left.services == right.services &&
                   left.serviceCount == right.serviceCount && left.dashboard == right.dashboard;
        };
        for (const wchar_t* name : {kRedXeDebugSettingsFileName, kRedXeReleaseSettingsFileName})
        {
            std::filesystem::path path;
            std::string original;
            HRESULT result = GetDeployedPath(name, path);
            if (SUCCEEDED(result))
                result = ReadFile(path, original);
            if (FAILED(result))
                return result;
            std::string patched = original;
            AppSettings before{};
            AppSettings after{};
            if (FAILED(PatchFirstRunDock(patched, dock)) || FAILED(ParseAppSettingsJson(original, before)) ||
                FAILED(ParseAppSettingsJson(patched, after)) || before.dock.edge != DockEdge::None ||
                !isFirstRunDock(after.dock) || !sameExceptDock(before, after))
            {
                std::wprintf(L"The first-run dock did not patch %s into the same document plus the dock.\n", name);
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            // The template's commented dock example, from its introduction through the `// "dock":` line, is gone; the
            // rest of the template is unchanged around one contiguous insertion right after the `version` member, in
            // the template's own line breaks.
            const size_t introductionAt = original.find("// Screen-edge dock (minor 2)");
            const size_t exampleAt = original.find("// \"dock\": {", introductionAt);
            if (introductionAt == std::string::npos || exampleAt == std::string::npos ||
                original.find('\n', exampleAt) == std::string::npos || definesOneDock(original) ||
                !definesOneDock(patched))
            {
                std::wprintf(L"The first-run dock in %s is not the only dock definition.\n", name);
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            std::string kept = original;
            const size_t removedAt = kept.rfind('\n', introductionAt) + 1;
            kept.erase(removedAt, kept.find('\n', exampleAt) + 1 - removedAt);
            constexpr std::string_view versionMember = "\"version\": { \"major\": 5, \"minor\": 3 },";
            const size_t versionAt = kept.find(versionMember);
            if (versionAt == std::string::npos || patched.size() <= kept.size())
            {
                std::wprintf(L"The %s template has no version member to follow.\n", name);
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            const size_t insertAt = versionAt + versionMember.size();
            const size_t insertedBytes = patched.size() - kept.size();
            const std::string inserted = patched.substr(insertAt, insertedBytes);
            const std::string_view lineBreak = original.find("\r\n") != std::string::npos ? "\r\n" : "\n";
            size_t bareLineFeeds = 0;
            for (size_t index = 0; index < inserted.size(); ++index)
                bareLineFeeds += inserted[index] == '\n' && (index == 0 || inserted[index - 1] != '\r') ? 1U : 0U;
            if (patched.compare(0, insertAt, kept, 0, insertAt) != 0 ||
                patched.compare(insertAt + insertedBytes, std::string::npos, kept, insertAt, std::string::npos) != 0 ||
                !inserted.starts_with(lineBreak) || !inserted.ends_with("},") ||
                inserted.find("// No XENEON display was found") == std::string::npos ||
                inserted.find(
                    R"("dock": { "edge": "top", "monitor": "secondary", "mode": "autohide", "thickness": 720 })") ==
                    std::string::npos ||
                (lineBreak == "\r\n" && bareLineFeeds != 0))
            {
                std::wprintf(L"The first-run dock in %s is not one commented line run after version.\n", name);
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }

        // Other shapes: an older minor rises to the minor the dock needs (3 for `secondary`, else 2, the minor that
        // added `dock`), `version` as the last member, and an existing `dock` value is replaced. Invalid docks and
        // malformed documents leave the source untouched.
        AppSettings parsed{};
        std::string olderMinor = R"json({"version":{"major":5,"minor":1},"pages":[{}]})json";
        if (FAILED(PatchFirstRunDock(olderMinor, dock)) || FAILED(ParseAppSettingsJson(olderMinor, parsed)) ||
            parsed.versionMinor != kRedXeSettingsSecondaryMonitorMinor || !isFirstRunDock(parsed.dock))
        {
            std::wprintf(L"The first-run dock on the second screen did not raise a minor 1 document to 3.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        DockSettings primaryDock = withMonitor(dock, kDockDefaultMonitor);
        primaryDock.edge = DockEdge::Bottom;
        std::string primaryOlderMinor = R"json({"version":{"major":5,"minor":1},"pages":[{}]})json";
        if (FAILED(PatchFirstRunDock(primaryOlderMinor, primaryDock)) ||
            FAILED(ParseAppSettingsJson(primaryOlderMinor, parsed)) || parsed.versionMinor != kRedXeSettingsDockMinor ||
            parsed.dock.edge != DockEdge::Bottom || parsed.dock.monitor.View() != kDockDefaultMonitor)
        {
            std::wprintf(L"The first-run dock on the primary did not raise a minor 1 document to 2.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        // A slide duration other than the default is written and needs minor 3, even on the primary.
        DockSettings unanimatedDock = primaryDock;
        unanimatedDock.animationMilliseconds = 0;
        std::string unanimatedSource = R"json({"version":{"major":5,"minor":1},"pages":[{}]})json";
        if (FAILED(PatchFirstRunDock(unanimatedSource, unanimatedDock)) ||
            FAILED(ParseAppSettingsJson(unanimatedSource, parsed)) ||
            parsed.versionMinor != kRedXeSettingsDockAnimationMinor || parsed.dock.animationMilliseconds != 0 ||
            unanimatedSource.find("\"animationMilliseconds\": 0") == std::string::npos)
        {
            std::wprintf(L"The first-run dock did not write a non-default slide duration with minor 3.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        DockSettings slowDock = dock;
        slowDock.animationMilliseconds = kDockMaximumAnimationMilliseconds + 1;
        std::string refusedSlow = R"json({"version":{"major":5,"minor":2},"pages":[{}]})json";
        if (PatchFirstRunDock(refusedSlow, slowDock) != E_INVALIDARG ||
            refusedSlow != R"json({"version":{"major":5,"minor":2},"pages":[{}]})json")
        {
            std::wprintf(L"The first-run dock accepted a slide duration out of range.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        std::string versionLast = R"json({"pages":[{}],"version":{"major":5}})json";
        if (FAILED(PatchFirstRunDock(versionLast, dock)) || FAILED(ParseAppSettingsJson(versionLast, parsed)) ||
            parsed.versionMinor != kRedXeSettingsSecondaryMonitorMinor || !isFirstRunDock(parsed.dock))
        {
            std::wprintf(L"The first-run dock did not follow a last version member.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        std::string existing =
            R"json({"version":{"major":5,"minor":2},"dock":{"edge":"none","thickness":200},"pages":[{}]})json";
        DockSettings named = dock;
        named.monitor = SettingsText{};
        constexpr std::string_view namedMonitor = "name:DELL";
        namedMonitor.copy(named.monitor.utf8.data(), namedMonitor.size());
        named.monitor.bytes = static_cast<uint32_t>(namedMonitor.size());
        named.peekPixels = 6;
        if (FAILED(PatchFirstRunDock(existing, named)) || FAILED(ParseAppSettingsJson(existing, parsed)) ||
            parsed.dock.edge != DockEdge::Top || parsed.dock.thicknessDips != 720 ||
            parsed.dock.monitor.View() != namedMonitor || parsed.dock.peekPixels != 6 ||
            parsed.versionMinor != kRedXeSettingsDockMinor || existing.find("\"thickness\":200") != std::string::npos)
        {
            std::wprintf(L"The first-run dock did not replace an existing dock value.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        // Every member away from its default is written and parses back: the store installs a first-run dock only
        // when the written file holds exactly that dock.
        DockSettings everyMember = withMonitor(DefaultDockSettings(), namedMonitor);
        everyMember.edge = DockEdge::Left;
        everyMember.thicknessDips = 240;
        everyMember.mode = DockMode::Autohide;
        everyMember.reserveWorkArea = false;
        everyMember.peekPixels = 6;
        everyMember.revealDelayMilliseconds = 0;
        everyMember.hideDelayMilliseconds = 1500;
        everyMember.animationMilliseconds = 0;
        std::string everySource = R"json({"version":{"major":5,"minor":1},"pages":[{}]})json";
        if (FAILED(PatchFirstRunDock(everySource, everyMember)) || FAILED(ParseAppSettingsJson(everySource, parsed)) ||
            parsed.dock != everyMember || parsed.versionMinor != kRedXeSettingsDockAnimationMinor)
        {
            std::wprintf(L"The first-run dock did not write every member that leaves its default:\n%hs\n",
                         everySource.c_str());
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        // Only a root `// "dock":` example goes, with the comment lines right above it and their line breaks: a comment
        // a blank line separates from it, an example inside another value, and a comment after a member stay.
        std::string examples =
            "{\n  \"version\": { \"major\": 5, \"minor\": 3 },\n  // Kept: a blank line follows.\n\n"
            "  // Introduces the example;\n  // uncomment it.\n  // \"dock\": { \"edge\": \"bottom\" },\n"
            "  \"pages\": [{\n    // \"dock\": {}\n  }] // \"dock\": after a member\n}\n";
        constexpr std::string_view examplesKept = "\n  // Kept: a blank line follows.\n\n  \"pages\": [{\n"
                                                  "    // \"dock\": {}\n  }] // \"dock\": after a member\n}\n";
        if (FAILED(PatchFirstRunDock(examples, dock)) || FAILED(ParseAppSettingsJson(examples, parsed)) ||
            !isFirstRunDock(parsed.dock) || !examples.ends_with(examplesKept) ||
            examples.find("Introduces the example") != std::string::npos ||
            examples.find("uncomment it") != std::string::npos || examples.find("\"bottom\"") != std::string::npos)
        {
            std::wprintf(L"The first-run dock did not remove exactly the root dock example:\n%hs\n", examples.c_str());
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        DockSettings none = dock;
        none.edge = DockEdge::None;
        DockSettings thick = dock;
        thick.thicknessDips = kDockMaximumThicknessDips + 1;
        DockSettings strayEdge = dock;
        strayEdge.edge = static_cast<DockEdge>(9);
        DockSettings strayMode = dock;
        strayMode.mode = static_cast<DockMode>(7);
        DockSettings everyMonitor = dock;
        everyMonitor.monitor = SettingsText{};
        constexpr std::string_view allMonitors = "all";
        allMonitors.copy(everyMonitor.monitor.utf8.data(), allMonitors.size());
        everyMonitor.monitor.bytes = static_cast<uint32_t>(allMonitors.size());
        std::string untouched = R"json({"version":{"major":5,"minor":2},"pages":[{}]})json";
        std::string malformed = R"json({"version":{"major":5,"minor":"2"},"pages":[{}]})json";
        const std::string malformedCopy = malformed;
        std::string scalarVersion = R"json({"version":5,"pages":[{}]})json";
        const std::string scalarVersionCopy = scalarVersion;
        std::string oldMajor = R"json({"version":{"major":4},"pages":[{}]})json";
        const std::string oldMajorCopy = oldMajor;
        if (PatchFirstRunDock(untouched, none) != E_INVALIDARG || PatchFirstRunDock(untouched, thick) != E_INVALIDARG ||
            PatchFirstRunDock(untouched, strayEdge) != E_INVALIDARG ||
            PatchFirstRunDock(untouched, strayMode) != E_INVALIDARG ||
            PatchFirstRunDock(untouched, everyMonitor) != E_INVALIDARG ||
            untouched != R"json({"version":{"major":5,"minor":2},"pages":[{}]})json" ||
            SUCCEEDED(PatchFirstRunDock(malformed, dock)) || malformed != malformedCopy ||
            SUCCEEDED(PatchFirstRunDock(scalarVersion, dock)) || scalarVersion != scalarVersionCopy ||
            SUCCEEDED(PatchFirstRunDock(oldMajor, dock)) || oldMajor != oldMajorCopy)
        {
            std::wprintf(L"The first-run dock accepted an invalid dock or document.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        // The store: install a missing file with the dock, keep an existing file, recover an invalid one with the plain
        // template, install the plain template when no dock is offered or none is made, and never write a missing
        // `--settings` file. The provider counts how often the store asks it for the dock.
        struct OfferedDock final
        {
            DockSettings dock{};
            bool made = true;
            uint32_t requests = 0;
        };
        const auto offer = [](OfferedDock& offered) noexcept
        {
            return FirstRunDockProvider{[](void* context, DockSettings& value) noexcept
                                        {
                                            auto& source = *static_cast<OfferedDock*>(context);
                                            ++source.requests;
                                            value = source.dock;
                                            return source.made;
                                        },
                                        &offered};
        };
        const std::filesystem::path localRoot = std::filesystem::temp_directory_path() /
                                                (L"RedXe.FirstRunDockTests." + std::to_wstring(GetCurrentProcessId()) +
                                                 L"." + std::to_wstring(GetTickCount64()));
        const std::filesystem::path settingsDirectory = localRoot / L"RedXe" / L"Settings";
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                std::error_code error;
                std::filesystem::remove_all(localRoot, error);
            });
#if defined(_DEBUG)
        constexpr const wchar_t* selectedName = kRedXeDebugSettingsFileName;
#else
        constexpr const wchar_t* selectedName = kRedXeReleaseSettingsFileName;
#endif
        std::filesystem::path templatePath;
        std::string templateBytes;
        HRESULT result = GetDeployedPath(selectedName, templatePath);
        if (SUCCEEDED(result))
            result = ReadFile(templatePath, templateBytes);
        if (FAILED(result))
            return result;
        const std::filesystem::path selected = settingsDirectory / selectedName;

        SettingsStore installStore;
        std::unique_ptr<AppSettings> installed;
        OfferedDock installOffer{dock};
        result = installStore.Initialize(false, {}, installed, localRoot.wstring(), offer(installOffer));
        std::string installedBytes;
        if (SUCCEEDED(result))
            result = ReadFile(selected, installedBytes);
        if (FAILED(result) || !installed || installOffer.requests != 1 || !installStore.InstalledFirstRunDock() ||
            installStore.UsedInitialFallback() || !isFirstRunDock(installed->dock) ||
            installedBytes.find("// No XENEON display was found") == std::string::npos ||
            !definesOneDock(installedBytes))
        {
            std::wprintf(L"A missing default file was not installed with the first-run dock, made once.\n");
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        const std::filesystem::path everyRoot = localRoot / L"EveryMember";
        SettingsStore everyStore;
        std::unique_ptr<AppSettings> everyInstalled;
        OfferedDock everyOffer{everyMember};
        result = everyStore.Initialize(false, {}, everyInstalled, everyRoot.wstring(), offer(everyOffer));
        if (FAILED(result) || !everyInstalled || everyOffer.requests != 1 || !everyStore.InstalledFirstRunDock() ||
            everyInstalled->dock != everyMember)
        {
            std::wprintf(L"A first-run dock with every member set was not installed as it was made.\n");
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        {
            std::ofstream stream(selected, std::ios::binary | std::ios::trunc);
            stream.write(templateBytes.data(), static_cast<std::streamsize>(templateBytes.size()));
        }
        SettingsStore keepStore;
        std::unique_ptr<AppSettings> kept;
        OfferedDock keepOffer{dock};
        result = keepStore.Initialize(false, {}, kept, localRoot.wstring(), offer(keepOffer));
        std::string keptBytes;
        if (SUCCEEDED(result))
            result = ReadFile(selected, keptBytes);
        if (FAILED(result) || !kept || keepOffer.requests != 0 || keepStore.InstalledFirstRunDock() ||
            kept->dock.edge != DockEdge::None || keptBytes != templateBytes)
        {
            std::wprintf(L"An existing default file was changed or measured for by the first-run dock.\n");
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        {
            std::ofstream stream(selected, std::ios::binary | std::ios::trunc);
            stream << "invalid default bytes";
        }
        // Recovery reinstalls the plain template even with a dock offered: only a missing file gets the bar.
        SettingsStore recoverStore;
        std::unique_ptr<AppSettings> recovered;
        OfferedDock recoverOffer{dock};
        result = recoverStore.Initialize(false, {}, recovered, localRoot.wstring(), offer(recoverOffer));
        std::string recoveredBytes;
        if (SUCCEEDED(result))
            result = ReadFile(selected, recoveredBytes);
        if (FAILED(result) || !recovered || recoverOffer.requests != 0 || !recoverStore.UsedInitialFallback() ||
            recoverStore.InstalledFirstRunDock() || recovered->dock.edge != DockEdge::None ||
            recoveredBytes != templateBytes ||
            recoverStore.InitialNotice().find(L"A fresh default configuration was installed.") == std::wstring::npos ||
            recoverStore.InitialNotice().find(L"bar on a screen edge") != std::wstring::npos)
        {
            std::wprintf(L"An invalid default file was not recovered with the plain template.\n");
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        const std::filesystem::path plainRoot = localRoot / L"Plain";
        SettingsStore plainStore;
        std::unique_ptr<AppSettings> plain;
        result = plainStore.Initialize(false, {}, plain, plainRoot.wstring());
        std::string plainBytes;
        if (SUCCEEDED(result))
            result = ReadFile(plainRoot / L"RedXe" / L"Settings" / selectedName, plainBytes);
        if (FAILED(result) || !plain || plainStore.InstalledFirstRunDock() || plain->dock.edge != DockEdge::None ||
            plainBytes != templateBytes)
        {
            std::wprintf(L"Without a first-run dock the install is not the template byte for byte.\n");
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        // A dock the patch refuses, or no dock at all (no display was chosen), never blocks the install: the plain
        // template goes in instead.
        const std::filesystem::path refusedRoot = localRoot / L"Refused";
        DockSettings refused = dock;
        refused.thicknessDips = kDockMinimumThicknessDips - 1;
        SettingsStore refusedStore;
        std::unique_ptr<AppSettings> refusedSettings;
        OfferedDock refusedOffer{refused};
        result = refusedStore.Initialize(false, {}, refusedSettings, refusedRoot.wstring(), offer(refusedOffer));
        std::string refusedBytes;
        if (SUCCEEDED(result))
            result = ReadFile(refusedRoot / L"RedXe" / L"Settings" / selectedName, refusedBytes);
        if (FAILED(result) || !refusedSettings || refusedOffer.requests != 1 || refusedStore.InstalledFirstRunDock() ||
            refusedSettings->dock.edge != DockEdge::None || refusedBytes != templateBytes)
        {
            std::wprintf(L"A first-run dock the patch refuses blocked or changed the plain install.\n");
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        const std::filesystem::path unmadeRoot = localRoot / L"Unmade";
        SettingsStore unmadeStore;
        std::unique_ptr<AppSettings> unmade;
        OfferedDock unmadeOffer{dock, false};
        result = unmadeStore.Initialize(false, {}, unmade, unmadeRoot.wstring(), offer(unmadeOffer));
        std::string unmadeBytes;
        if (SUCCEEDED(result))
            result = ReadFile(unmadeRoot / L"RedXe" / L"Settings" / selectedName, unmadeBytes);
        if (FAILED(result) || !unmade || unmadeOffer.requests != 1 || unmadeStore.InstalledFirstRunDock() ||
            unmade->dock.edge != DockEdge::None || unmadeBytes != templateBytes)
        {
            std::wprintf(L"A first-run dock that was not made blocked or changed the plain install.\n");
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        const std::filesystem::path portable = localRoot / L"portable.settings.json";
        SettingsStore portableStore;
        std::unique_ptr<AppSettings> portableSettings;
        OfferedDock portableOffer{dock};
        result = portableStore.Initialize(false, portable.wstring(), portableSettings, {}, offer(portableOffer));
        if (FAILED(result) || !portableSettings || portableOffer.requests != 0 ||
            !portableStore.UsedInitialFallback() || portableStore.InstalledFirstRunDock() ||
            portableSettings->dock.edge != DockEdge::None || std::filesystem::exists(portable))
        {
            std::wprintf(L"A missing --settings file was written, docked, or measured for.\n");
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        // The self-test loads the deployed template and never installs.
        SettingsStore selfTestStore;
        std::unique_ptr<AppSettings> selfTestSettings;
        OfferedDock selfTestOffer{dock};
        result = selfTestStore.Initialize(true, {}, selfTestSettings, {}, offer(selfTestOffer));
        if (FAILED(result) || !selfTestSettings || selfTestOffer.requests != 0 || selfTestStore.InstalledFirstRunDock())
        {
            std::wprintf(L"The self-test asked for a first-run dock.\n");
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

// A document that starts with a UTF-8 BOM (Core_Settings.md "Version 5 document"), as Windows PowerShell 5.1
// `Set-Content -Encoding UTF8` saves it: it loads like the same text without one, its diagnostics name the real
// problem, the dock and first-run patches and a widget persist keep the BOM, and a default file saved that way loads
// at startup instead of being backed up and replaced.
[[nodiscard]] HRESULT ValidateByteOrderMark() noexcept
{
    constexpr std::string_view bom = "\xEF\xBB\xBF";
    try
    {
        for (const wchar_t* name : {kRedXeDebugSettingsFileName, kRedXeReleaseSettingsFileName})
        {
            std::filesystem::path path;
            std::string original;
            HRESULT result = GetDeployedPath(name, path);
            if (SUCCEEDED(result))
                result = ReadFile(path, original);
            if (FAILED(result))
                return result;
            const std::string marked = std::string(bom) + original;
            AppSettings plain{};
            AppSettings withBom{};
            if (FAILED(ParseAppSettingsJson(original, plain)) || FAILED(ParseAppSettingsJson(marked, withBom)) ||
                withBom.sourceDocument != marked)
            {
                std::wprintf(L"The %s template with a BOM did not load.\n", name);
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            withBom.sourceDocument = plain.sourceDocument;
            // The dock drag and the first-run dock patch the text after the BOM exactly as without it.
            AppSettings dragged = plain;
            AppSettings markedDragged{};
            std::string firstRun = original;
            std::string markedFirstRun = marked;
            DockSettings firstRunDock = DefaultDockSettings();
            firstRunDock.edge = DockEdge::Bottom;
            if (withBom != plain || FAILED(ParseAppSettingsJson(marked, markedDragged)) ||
                PatchDockThickness(dragged, 220) != S_OK || PatchDockThickness(markedDragged, 220) != S_OK ||
                markedDragged.sourceDocument != std::string(bom) + dragged.sourceDocument ||
                markedDragged.versionMinor != dragged.versionMinor ||
                FAILED(PatchFirstRunDock(firstRun, firstRunDock)) ||
                FAILED(PatchFirstRunDock(markedFirstRun, firstRunDock)) ||
                markedFirstRun != std::string(bom) + firstRun ||
                FAILED(ParseAppSettingsJson(markedFirstRun, withBom)) || withBom.dock.edge != DockEdge::Bottom)
            {
                std::wprintf(L"A dock patch of the %s template with a BOM did not keep the BOM.\n", name);
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }

        // A widget persist rewrites the document with the BOM it started with, once.
        const std::string launcher =
            std::string(bom) +
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.launcher"}]}]})json";
        AppSettings persisted{};
        AppSettings reparsed{};
        if (FAILED(ParseAppSettingsJson(launcher, persisted)) ||
            PatchWidgetInstanceSettings(persisted, persisted.dashboard.pages[0].widgets[0].id.View(),
                                        R"json({"iconSize":"large"})json") != S_OK ||
            !persisted.sourceDocument.starts_with(bom) ||
            persisted.sourceDocument.compare(bom.size(), bom.size(), bom) == 0 ||
            FAILED(ParseAppSettingsJson(persisted.sourceDocument, reparsed)) ||
            reparsed.dashboard.pages[0].widgets[0].privateConfiguration.View().find("\"iconSize\":\"large\"") ==
                std::string_view::npos)
        {
            std::wprintf(L"A widget persist did not keep the document's BOM.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        // Diagnostics after a BOM name the real problem, and a BOM alone, or with only comments, is an empty file.
        const auto diagnose = [](std::string_view json, SettingsParseDiagnostic& diagnostic) noexcept
        {
            AppSettings settings{};
            return FAILED(ParseAppSettingsJsonDetailed(json, settings, diagnostic));
        };
        SettingsParseDiagnostic unknown;
        SettingsParseDiagnostic syntax;
        SettingsParseDiagnostic empty;
        SettingsParseDiagnostic commentsOnly;
        if (!diagnose(std::string(bom) + R"json({"version":{"major":5},"unknown":1,"pages":[{}]})json", unknown) ||
            unknown.path.find(".unknown") == std::string::npos ||
            !diagnose(std::string(bom) + "{\n  \"version\": { \"major\": 5 },\n  \"pages\": [{}] x\n}\n", syntax) ||
            syntax.line != 3 || syntax.message.find("BOM") != std::string::npos || !diagnose(bom, empty) ||
            empty.message != "The settings file is empty." ||
            !diagnose(std::string(bom) + "\r\n// Only a comment.\r\n", commentsOnly) ||
            commentsOnly.message != "The settings file is empty.")
        {
            std::wprintf(L"A document with a BOM was diagnosed as '%hs' (line %u), '%hs', '%hs'.\n",
                         syntax.message.c_str(), syntax.line, empty.message.c_str(), commentsOnly.message.c_str());
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        // The store: a default file saved with a BOM loads at startup, untouched and without a backup, and a host write
        // to a loaded file keeps its BOM on disk.
        const std::filesystem::path localRoot = std::filesystem::temp_directory_path() /
                                                (L"RedXe.ByteOrderMarkTests." + std::to_wstring(GetCurrentProcessId()) +
                                                 L"." + std::to_wstring(GetTickCount64()));
        const std::filesystem::path settingsDirectory = localRoot / L"RedXe" / L"Settings";
        std::filesystem::create_directories(settingsDirectory);
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                std::error_code error;
                std::filesystem::remove_all(localRoot, error);
            });
#if defined(_DEBUG)
        constexpr const wchar_t* selectedName = kRedXeDebugSettingsFileName;
#else
        constexpr const wchar_t* selectedName = kRedXeReleaseSettingsFileName;
#endif
        std::filesystem::path templatePath;
        std::string templateBytes;
        HRESULT result = GetDeployedPath(selectedName, templatePath);
        if (SUCCEEDED(result))
            result = ReadFile(templatePath, templateBytes);
        if (FAILED(result))
            return result;
        const std::string markedTemplate = std::string(bom) + templateBytes;
        const std::filesystem::path selected = settingsDirectory / selectedName;
        {
            std::ofstream stream(selected, std::ios::binary | std::ios::trunc);
            stream.write(markedTemplate.data(), static_cast<std::streamsize>(markedTemplate.size()));
        }
        SettingsStore store;
        std::unique_ptr<AppSettings> loaded;
        result = store.Initialize(false, {}, loaded, localRoot.wstring());
        std::string loadedBytes;
        if (SUCCEEDED(result))
            result = ReadFile(selected, loadedBytes);
        size_t backups = 0;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(settingsDirectory))
            backups += entry.path().filename().wstring().find(L".invalid-") != std::wstring::npos ? 1U : 0U;
        if (FAILED(result) || !loaded || store.UsedInitialFallback() || loaded->sourceDocument != markedTemplate ||
            loadedBytes != markedTemplate || backups != 0 || !store.InitialNotice().empty())
        {
            std::wprintf(L"A default file with a BOM was not loaded as it is.\n");
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        std::string draggedBytes;
        result = store.PersistDockThickness(*loaded, 220);
        if (SUCCEEDED(result))
            result = ReadFile(selected, draggedBytes);
        if (result != S_OK || !draggedBytes.starts_with(bom) || draggedBytes != loaded->sourceDocument ||
            draggedBytes.find("\"dock\": { \"thickness\": 220 }") == std::string::npos)
        {
            std::wprintf(L"A dock drag did not keep the BOM of the settings file.\n");
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT ValidatePersistRollback() noexcept
{
    constexpr std::string_view documentJson =
        R"({"version":{"major":5},"declare":{"Matrix":{"plugin":"builtin.matrix-rain",)"
        R"("seed":7,"densityPercent":60}},"pages":[{"widgets":[{"plugin":"builtin.rotating-triangle"},)"
        R"("Matrix"]}]})";
    try
    {
        const auto directory =
            std::filesystem::temp_directory_path() / (L"RedXe.PersistTests." + std::to_wstring(GetCurrentProcessId()) +
                                                      L"." + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(directory);
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                std::error_code error;
                std::filesystem::remove_all(directory, error);
            });
        const auto path = directory / L"custom.settings.json";
        {
            std::ofstream stream(path, std::ios::binary);
            stream << documentJson;
        }
        SettingsStore store;
        std::unique_ptr<AppSettings> loaded;
        HRESULT result = store.Initialize(false, path.wstring(), loaded);
        if (FAILED(result) || !loaded || loaded->dashboard.pages[0].widgets.size() < 2)
            return FAILED(result) ? result : E_UNEXPECTED;
        const auto before = std::make_unique<AppSettings>(*loaded);
        const std::string id(loaded->dashboard.pages[0].widgets[1].id.View());
        // A real open handle without FILE_SHARE_DELETE prevents the atomic replacement from committing.
        wil::unique_hfile locked{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (!locked)
            return HRESULT_FROM_WIN32(GetLastError());
        result = store.PersistWidgetSettings(*loaded, id, R"({"seed":17})");
        std::string disk;
        if ((result != HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) && result != E_ACCESSDENIED) || *loaded != *before ||
            FAILED(ReadFile(path, disk)) || disk != documentJson)
        {
            std::wprintf(L"Failed persistence changed the authoritative settings or disk.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        // A failed dock drag rolls back the same way, including the minor it raised (0 to 2 here).
        result = store.PersistDockThickness(*loaded, 200);
        if ((result != HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) && result != E_ACCESSDENIED) || *loaded != *before ||
            FAILED(ReadFile(path, disk)) || disk != documentJson)
        {
            std::wprintf(L"A failed dock-thickness persist changed the authoritative settings or disk.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        locked.reset();
        if (SUCCEEDED(store.PersistWidgetSettings(*loaded, id, "[]")) || *loaded != *before)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        result = store.PersistWidgetSettings(*loaded, id, R"({"densityPercent":75})");
        if (FAILED(result))
            return result;
        auto reloaded = std::make_unique<AppSettings>();
        result = LoadAppSettingsFile(path.wstring(), *reloaded);
        if (FAILED(result))
            return result;
        const auto& saved = reloaded->dashboard.pages[0].widgets[1].privateConfiguration;
        unique_doc document{yyjson_read(saved.View().data(), saved.View().size(), YYJSON_READ_NOFLAG)};
        yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
        if (!root || yyjson_get_uint(yyjson_obj_get(root, "seed")) != 7 ||
            yyjson_get_uint(yyjson_obj_get(root, "densityPercent")) != 75 ||
            saved != loaded->dashboard.pages[0].widgets[1].privateConfiguration)
        {
            std::wprintf(L"A later save resurrected the rejected patch or lost the new patch.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        // A committed drag leaves the typed minor equal to the file's.
        if (store.PersistDockThickness(*loaded, 200) != S_OK ||
            FAILED(LoadAppSettingsFile(path.wstring(), *reloaded)) || reloaded->dock.thicknessDips != 200 ||
            reloaded->versionMinor != kRedXeSettingsDockMinor || loaded->versionMinor != reloaded->versionMinor)
        {
            std::wprintf(L"A committed dock-thickness persist left the typed minor apart from the file.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

// Core_Settings.md "Plugin persist": the store writes only over the document last applied. Any other file on disk keeps
// its bytes (or its absence), the change stays in memory without a rollback (S_FALSE), and each distinct on-disk state
// raises one deferral notice. A merge or dock drag that changes nothing never rewrites the file.
[[nodiscard]] HRESULT ValidatePersistWriteGate() noexcept
{
    constexpr std::string_view commented =
        "{\r\n"
        "  // A note the person wrote; an unchanged persist keeps it.\r\n"
        "  \"version\": { \"major\": 5 },\r\n"
        "  \"pages\": [{ \"widgets\": [{ \"plugin\": \"builtin.matrix-rain\", \"seed\": 7 }] }]\r\n"
        "}\r\n";
    constexpr std::string_view rejected = "{ \"version\": { \"major\": 5 }, \"pages\": [";
    constexpr std::string_view external =
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.matrix-rain","seed":3}]}]})json";
    try
    {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                                (L"RedXe.PersistGateTests." + std::to_wstring(GetCurrentProcessId()) +
                                                 L"." + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directory(directory);
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                std::error_code error;
                std::filesystem::remove_all(directory, error);
            });
        const auto write = [](const std::filesystem::path& path, std::string_view bytes)
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            return static_cast<bool>(stream);
        };
        const auto holds = [](const std::filesystem::path& path, std::string_view expected)
        {
            std::string bytes;
            return SUCCEEDED(ReadFile(path, bytes)) && bytes == expected;
        };
        const auto privateOf = [](const AppSettings& settings) noexcept
        { return settings.dashboard.pages[0].widgets[0].privateConfiguration.View(); };
        const std::filesystem::path file = directory / L"gate.settings.json";
        if (!write(file, commented))
            return E_FAIL;

        SettingsStore store;
        std::unique_ptr<AppSettings> loaded;
        HRESULT result = store.Initialize(false, file.wstring(), loaded);
        if (FAILED(result) || !loaded || loaded->dashboard.pages[0].widgets.empty())
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        const std::string id(loaded->dashboard.pages[0].widgets[0].id.View());

        // A collect that resends the stored object, or one member at its current value, changes nothing; neither does a
        // dock drag released at the current thickness (the document has no `dock` and minor 0 to add here).
        const auto before = std::make_unique<AppSettings>(*loaded);
        const std::string stored(privateOf(*loaded));
        if (store.PersistWidgetSettings(*loaded, id, stored) != S_FALSE ||
            store.PersistWidgetSettings(*loaded, id, R"({"seed":7})") != S_FALSE ||
            store.PersistDockThickness(*loaded, loaded->dock.thicknessDips) != S_FALSE || *loaded != *before ||
            !holds(file, commented) || store.TakeDeferredPersistNotice())
        {
            std::wprintf(L"An unchanged widget persist or dock drag rewrote the settings file or its document.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        // A rejected save stays for the editor: widget persists and a dock drag keep their change in memory.
        std::unique_ptr<AppSettings> candidate;
        SettingsFileStamp stamp{};
        SettingsReloadStatus status = SettingsReloadStatus::Unchanged;
        if (!write(file, rejected) || FAILED(store.TryLoadChanged(candidate, stamp, status)) ||
            status != SettingsReloadStatus::Invalid)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        if (store.PersistWidgetSettings(*loaded, id, R"({"seed":17})") != S_FALSE ||
            !store.TakeDeferredPersistNotice() ||
            store.PersistWidgetSettings(*loaded, id, R"({"densityPercent":75})") != S_FALSE ||
            store.PersistDockThickness(*loaded, 200) != S_FALSE || store.TakeDeferredPersistNotice() ||
            !holds(file, rejected) || privateOf(*loaded).find("\"seed\":17") == std::string_view::npos ||
            privateOf(*loaded).find("\"densityPercent\":75") == std::string_view::npos ||
            loaded->dock.thicknessDips != 200)
        {
            std::wprintf(L"A persist wrote over a rejected save or rolled back its change.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        // A valid save the watcher has not applied yet is not overwritten either. Once applied, its document replaces
        // the change held in memory, and a later change writes again.
        if (!write(file, external) || store.PersistWidgetSettings(*loaded, id, R"({"seed":19})") != S_FALSE ||
            !store.TakeDeferredPersistNotice() || !holds(file, external))
        {
            std::wprintf(L"A persist wrote over a save that was not applied yet.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        candidate.reset();
        if (FAILED(store.TryLoadChanged(candidate, stamp, status)) || status != SettingsReloadStatus::Loaded ||
            !candidate)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        loaded = std::move(candidate);
        store.MarkApplied(stamp);
        auto saved = std::make_unique<AppSettings>();
        if (store.PersistWidgetSettings(*loaded, id, R"({"densityPercent":80})") != S_OK ||
            FAILED(LoadAppSettingsFile(file.wstring(), *saved)) ||
            privateOf(*saved).find("\"seed\":3") == std::string_view::npos ||
            privateOf(*saved).find("\"densityPercent\":80") == std::string_view::npos ||
            saved->dock.thicknessDips != kDockDefaultThicknessDips)
        {
            std::wprintf(L"A persist after an applied reload did not write the reloaded document.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        // A deleted file is not recreated. The same file restored (a Recycle Bin restore keeps its identity and time)
        // takes the change held in memory with the next persist, even one that changes nothing more.
        std::string applied;
        if (FAILED(ReadFile(file, applied)))
            return E_FAIL;
        const std::filesystem::path recycled = directory / L"gate.recycled.json";
        if (!MoveFileExW(file.c_str(), recycled.c_str(), 0))
            return HRESULT_FROM_WIN32(GetLastError());
        candidate.reset();
        if (FAILED(store.TryLoadChanged(candidate, stamp, status)) || status != SettingsReloadStatus::Missing ||
            store.PersistWidgetSettings(*loaded, id, R"({"seed":23})") != S_FALSE ||
            !store.TakeDeferredPersistNotice() || store.PersistDockThickness(*loaded, 240) != S_FALSE ||
            store.TakeDeferredPersistNotice() || std::filesystem::exists(file))
        {
            std::wprintf(L"A persist recreated a deleted settings file.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (!MoveFileExW(recycled.c_str(), file.c_str(), 0))
            return HRESULT_FROM_WIN32(GetLastError());
        candidate.reset();
        if (FAILED(store.TryLoadChanged(candidate, stamp, status)) || status != SettingsReloadStatus::Unchanged ||
            !holds(file, applied) || store.PersistWidgetSettings(*loaded, id, R"({"seed":23})") != S_OK ||
            FAILED(LoadAppSettingsFile(file.wstring(), *saved)) ||
            privateOf(*saved).find("\"seed\":23") == std::string_view::npos || saved->dock.thicknessDips != 240)
        {
            std::wprintf(L"The change held in memory was not written once the same file was restored.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        // A dock drag released at the current thickness writes such a held change too, and after that nothing.
        if (!MoveFileExW(file.c_str(), recycled.c_str(), 0))
            return HRESULT_FROM_WIN32(GetLastError());
        candidate.reset();
        if (FAILED(store.TryLoadChanged(candidate, stamp, status)) || status != SettingsReloadStatus::Missing ||
            store.PersistWidgetSettings(*loaded, id, R"({"seed":29})") != S_FALSE || !store.TakeDeferredPersistNotice())
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        if (!MoveFileExW(recycled.c_str(), file.c_str(), 0))
            return HRESULT_FROM_WIN32(GetLastError());
        candidate.reset();
        if (FAILED(store.TryLoadChanged(candidate, stamp, status)) || status != SettingsReloadStatus::Unchanged ||
            store.PersistDockThickness(*loaded, 240) != S_OK || FAILED(LoadAppSettingsFile(file.wstring(), *saved)) ||
            privateOf(*saved).find("\"seed\":29") == std::string_view::npos || saved->dock.thicknessDips != 240 ||
            FAILED(ReadFile(file, applied)) || store.PersistDockThickness(*loaded, 240) != S_FALSE ||
            !holds(file, applied))
        {
            std::wprintf(L"An unchanged dock drag did not write the change held in memory, or wrote again.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        // A --settings file that fell back to the template keeps its bytes, and a missing one stays missing.
        const std::filesystem::path portable = directory / L"portable.settings.json";
        if (!write(portable, rejected))
            return E_FAIL;
        SettingsStore fallbackStore;
        std::unique_ptr<AppSettings> fallback;
        result = fallbackStore.Initialize(false, portable.wstring(), fallback);
        if (FAILED(result) || !fallback || !fallbackStore.UsedInitialFallback())
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        std::string launcherId;
        for (uint32_t page = 0; page < fallback->dashboard.pageCount && launcherId.empty(); ++page)
        {
            const auto& layout = fallback->dashboard.pages[page];
            for (uint32_t item = 0; item < layout.widgetCount && launcherId.empty(); ++item)
            {
                if (layout.widgets[item].pluginId.View() == "builtin.launcher")
                    launcherId = layout.widgets[item].id.View();
            }
        }
        if (launcherId.empty() ||
            fallbackStore.PersistWidgetSettings(
                *fallback, launcherId, R"json({"shortcuts":[{"target":"C:\\Windows\\notepad.exe"}]})json") != S_FALSE ||
            !fallbackStore.TakeDeferredPersistNotice() ||
            fallbackStore.PersistDockThickness(*fallback, 200) != S_FALSE ||
            fallbackStore.TakeDeferredPersistNotice() || !holds(portable, rejected))
        {
            std::wprintf(L"A persist wrote the template over a --settings file that failed to load.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        const std::filesystem::path absent = directory / L"absent.settings.json";
        SettingsStore absentStore;
        std::unique_ptr<AppSettings> absentSettings;
        result = absentStore.Initialize(false, absent.wstring(), absentSettings);
        if (FAILED(result) || !absentSettings || absentStore.PersistDockThickness(*absentSettings, 200) != S_FALSE ||
            !absentStore.TakeDeferredPersistNotice() || std::filesystem::exists(absent))
        {
            std::wprintf(L"A persist created a missing --settings file.\n");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT ValidateLogsDirectory() noexcept
{
    try
    {
        const std::filesystem::path localRoot = std::filesystem::temp_directory_path() /
                                                (L"RedXe.LogsDirectoryTests." + std::to_wstring(GetCurrentProcessId()) +
                                                 L"." + std::to_wstring(GetTickCount64()));
        const std::filesystem::path settingsDirectory = localRoot / L"RedXe" / L"Settings";
        std::filesystem::create_directories(settingsDirectory);
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                std::error_code error;
                std::filesystem::remove_all(localRoot, error);
            });

        SettingsStore store;
        std::unique_ptr<AppSettings> settings;
        HRESULT result = store.Initialize(false, {}, settings, localRoot.wstring());
        const std::filesystem::path expectedLogs = localRoot / L"RedXe" / kRedXeLogsDirectoryName;
        if (FAILED(result) || !settings || store.LogsDirectory() != expectedLogs.wstring())
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        const std::filesystem::path portableDirectory = localRoot / L"portable";
        std::filesystem::create_directories(portableDirectory);
        const std::filesystem::path portable = portableDirectory / L"custom.settings.json";
        {
            std::ofstream stream(portable, std::ios::binary);
            stream << kRepresentative;
        }
        SettingsStore portableStore;
        std::unique_ptr<AppSettings> loaded;
        result = portableStore.Initialize(false, portable.wstring(), loaded);
        const std::filesystem::path expectedPortableLogs = portableDirectory / kRedXeLogsDirectoryName;
        if (FAILED(result) || !loaded || portableStore.LogsDirectory() != expectedPortableLogs.wstring())
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        SYSTEMTIME utc{};
        utc.wYear = 2026;
        utc.wMonth = 9;
        utc.wDay = 4;
        wchar_t name[kRedXeLogFileNameCapacity]{};
        SYSTEMTIME parsed{};
        SYSTEMTIME later{};
        later.wYear = 2026;
        later.wMonth = 9;
        later.wDay = 19;
        if (!RedXeFormatLogFileName(name, kRedXeLogFileNameCapacity, utc) || !RedXeTryParseLogFileDate(name, parsed) ||
            parsed.wYear != 2026 || parsed.wMonth != 9 || parsed.wDay != 4 ||
            !RedXeIsLegacyLogFileName(L"RedXe.jsonl") || !RedXeIsLegacyLogFileName(L"RedXe-debug.jsonl.1") ||
            RedXeIsLegacyLogFileName(name) || RedXeUtcDateDayDifference(utc, later) != 15 ||
            RedXeUtcDateDayDifference(utc, utc) != 0)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT ValidateLiveReloadNoWrite() noexcept
{
    try
    {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                                (L"RedXe.LiveReloadTests." + std::to_wstring(GetCurrentProcessId()) +
                                                 L"." + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directory(directory);
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                std::error_code error;
                std::filesystem::remove_all(directory, error);
            });
        const std::filesystem::path file = directory / L"live.settings.json";
        {
            std::ofstream stream(file, std::ios::binary);
            stream << kRepresentative;
        }
        SettingsStore store;
        std::unique_ptr<AppSettings> loaded;
        HRESULT result = store.Initialize(false, file.wstring(), loaded);
        if (FAILED(result) || !loaded)
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        constexpr std::string_view nextValid =
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.launcher"}]}]})json";
        {
            std::ofstream stream(file, std::ios::binary | std::ios::trunc);
            stream << nextValid;
        }
        std::string afterValidWrite;
        result = ReadFile(file, afterValidWrite);
        std::unique_ptr<AppSettings> candidate;
        SettingsFileStamp stamp{};
        SettingsReloadStatus status = SettingsReloadStatus::Unchanged;
        if (FAILED(result) || FAILED(store.TryLoadChanged(candidate, stamp, status)) ||
            status != SettingsReloadStatus::Loaded || !candidate || candidate->dashboard.pages[0].widgets.empty())
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        std::string afterValidLoad;
        if (FAILED(ReadFile(file, afterValidLoad)) || afterValidLoad != afterValidWrite)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        constexpr std::string_view persistPatch = R"json({"shortcuts":[{"target":"C:\\Windows\\notepad.exe"}]})json";
        const std::string_view instanceId = candidate->dashboard.pages[0].widgets[0].id.View();
        store.SuppressDocumentWrites(true);
        result = store.PersistWidgetSettings(*candidate, instanceId, persistPatch);
        store.SuppressDocumentWrites(false);
        std::string afterSuppressedPersist;
        if (FAILED(result) || FAILED(ReadFile(file, afterSuppressedPersist)) ||
            afterSuppressedPersist != afterValidWrite)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        // As in OnSettingsChanged, the loaded stamp is marked applied once the candidate is live; a later explicit
        // change may then write the file.
        store.MarkApplied(stamp);
        constexpr std::string_view explicitPatch = R"json({"shortcuts":[{"target":"C:\\Windows\\regedit.exe"}]})json";
        result = store.PersistWidgetSettings(*candidate, instanceId, explicitPatch);
        std::string afterExplicitPersist;
        if (result != S_OK || FAILED(ReadFile(file, afterExplicitPersist)) || afterExplicitPersist == afterValidWrite)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        constexpr std::string_view invalid = R"json({"version":{"major":4},"pages":[{}]})json";
        {
            std::ofstream stream(file, std::ios::binary | std::ios::trunc);
            stream << invalid;
        }
        std::string afterInvalidWrite;
        result = ReadFile(file, afterInvalidWrite);
        candidate.reset();
        status = SettingsReloadStatus::Unchanged;
        if (FAILED(result) || FAILED(store.TryLoadChanged(candidate, stamp, status)) ||
            status != SettingsReloadStatus::Invalid || candidate)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        std::string afterInvalidLoad;
        if (FAILED(ReadFile(file, afterInvalidLoad)) || afterInvalidLoad != afterInvalidWrite ||
            store.LastDiagnosticText().find(L"version 5") == std::wstring::npos)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        constexpr std::string_view laterValid = R"json({"version":{"major":5},"wrapPages":true,"pages":[{}]})json";
        {
            std::ofstream stream(file, std::ios::binary | std::ios::trunc);
            stream << laterValid;
        }
        std::string afterLaterWrite;
        candidate.reset();
        status = SettingsReloadStatus::Unchanged;
        if (FAILED(ReadFile(file, afterLaterWrite)) || FAILED(store.TryLoadChanged(candidate, stamp, status)) ||
            status != SettingsReloadStatus::Loaded || !candidate)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        std::string afterLaterLoad;
        if (FAILED(ReadFile(file, afterLaterLoad)) || afterLaterLoad != afterLaterWrite)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}
} // namespace

int wmain()
{
    RedXeFailureReports::RouteAwayFromDialogs();
    struct Test final
    {
        const wchar_t* name;
        HRESULT (*run)() noexcept;
    };
    const Test tests[]{{L"templates/schema", ValidateTemplatesAndSchema},
                       {L"parser", ValidateParser},
                       {L"dock", ValidateDockSettings},
                       {L"dock thickness layout", ValidateDockThicknessLayout},
                       {L"tray icon", ValidateTrayIconSettings},
                       {L"runtime settings", ValidateRuntimeSettingsEqual},
                       {L"command line", ValidateCommandLineCatalog},
                       {L"AV profile configuration", ValidateAvControlSettings},
                       {L"low stack", ValidateLowStack},
                       {L"watcher", ValidateWatcher},
                       {L"external selection", ValidateExternalSelection},
                       {L"default recovery", ValidateDefaultRecovery},
                       {L"v1.0.102 settings", ValidateReleasedTemplates},
                       {L"legacy filename", ValidateLegacyReleaseFilenameMigration},
                       {L"first-run dock", ValidateFirstRunDock},
                       {L"byte order mark", ValidateByteOrderMark},
                       {L"logs directory", ValidateLogsDirectory},
                       {L"persist formatting", ValidatePersistFormatting},
                       {L"persist rollback", ValidatePersistRollback},
                       {L"persist write gate", ValidatePersistWriteGate},
                       {L"live reload no write", ValidateLiveReloadNoWrite}};
    for (const auto& test : tests)
    {
        const HRESULT result = test.run();
        if (FAILED(result))
        {
            std::wprintf(L"Settings test '%s' failed: 0x%08X\n", test.name, static_cast<unsigned int>(result));
            return 1;
        }
    }
    return 0;
}
