#include "../../Plugins/Launcher/LauncherPaging.h"
#include "../../RedXe/BundledPlugins.h"
#include "../../RedXe/CommandLine.h"
#include "../../RedXe/DockOptions.h"
#include "../../RedXe/Settings.h"
#include "../../RedXe/SettingsWatcher.h"

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
// Debug-only widgets, which only the Debug template places. Every catalogued service is configured by both.
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
    if (settings.serviceCount != kRedXeBundledServices.size())
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
    // monitor tile. No shipped page places a native-window widget.
    if (debug.dashboard.pageCount != 4 || debug.dashboard.pages[0].widgetCount != 3)
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
        release.dashboard.pageCount != 3 || release.dashboard.pages[0].widgetCount != 1 ||
        release.dashboard.pages[1].widgetCount != 7 || release.dashboard.pages[2].widgetCount != 10 ||
        debug.logRetentionDays != 15 || release.logRetentionDays != 15)
    {
        std::wprintf(L"Deployed template page inventory contract failed.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    // Both templates configure the Logicon service (settings minor 1) with a page-navigation key layout and author
    // minor 2 (the `dock` member) with the dock left off: the shipped window kind stays the XENEON one.
    const ServiceSettings* debugLogicon = FindServiceSettings(debug, "builtin.logicon");
    const ServiceSettings* releaseLogicon = FindServiceSettings(release, "builtin.logicon");
    if (debug.dock != DefaultDockSettings() || release.dock != DefaultDockSettings() ||
        debug.dock.edge != DockEdge::None || debug.sourceDocument.find("\"dock\"") == std::string::npos ||
        release.sourceDocument.find("\"dock\"") == std::string::npos)
    {
        std::wprintf(L"Deployed templates must stay undocked and carry the commented dock example.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (!debugLogicon || !releaseLogicon || debug.versionMinor != 2 || release.versionMinor != 2 ||
        debugLogicon->name.View() != "Logicon" ||
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
                          kDockDefaultHideDelayMilliseconds))
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
    for (const char* pluginDefinition : {"matrixSettings", "studioClockSettings", "deskClockSettings"})
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
    // plugin's shared model, never counted as a widget plugin.
    constexpr std::string_view servicesDocument = R"json({
      "version":{"major":5,"minor":1},
      "services":{
        "Keypad":{"plugin":"builtin.logicon","brightness":40,"keys":[
          {"slot":0,"action":"page.next"},
          {"slot":1,"action":"system.launch","target":"not-a-path"},
          {"slot":2,"action":"zoom.mute","target":"toggle"}],
          "dialpad":{"turns":[{"control":"roller","direction":"up","action":"logicon.brightness","target":"+5"}]}},
        "Meet":{"plugin":"builtin.zoom","mode":"local","labels":{"muted":"actuellement coupé"}}},
      "pages":[{"widgets":[{"plugin":"builtin.gdi-orbit"}]}]
    })json";
    AppSettings services{};
    if (FAILED(ParseAppSettingsJson(servicesDocument, services)) || services.serviceCount != 2 ||
        services.services.size() != 2 || services.services[0].name.View() != "Keypad" ||
        services.services[0].pluginId.View() != "builtin.logicon" || services.pluginCount != 1 ||
        services.services[0].privateConfiguration.View().find("\"brightness\":40") == std::string_view::npos ||
        services.services[0].privateConfiguration.View().find("\"restoreLogoOnExit\":true") == std::string_view::npos ||
        services.services[0].privateConfiguration.View().find("\"page.next\"") == std::string_view::npos ||
        services.services[0].privateConfiguration.View().find("\"not-a-path\"") == std::string_view::npos ||
        services.services[1].pluginId.View() != "builtin.zoom" ||
        services.services[1].privateConfiguration.View().find("\"redirectPort\":48123") == std::string_view::npos ||
        services.services[1].privateConfiguration.View().find("\"unmuted\":\"currently unmuted\"") ==
            std::string_view::npos ||
        !FindServiceSettings(services, "builtin.logicon") || !FindServiceSettings(services, "builtin.zoom") ||
        FindServiceSettings(services, "builtin.launcher") || FAILED(ValidateAppSettings(services)))
    {
        std::wprintf(L"The services root did not parse as expected.\n");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    AppSettings noServices{};
    if (FAILED(ParseAppSettingsJson(retentionDocument, noServices)) || noServices.serviceCount != 0)
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
        // Zoom model rejections.
        R"json({"version":{"major":5},"services":{"Z":{"plugin":"builtin.zoom","clientId":"abc","redirectPort":80}},"pages":[{}]})json",
        R"json({"version":{"major":5},"services":{"Z":{"plugin":"builtin.zoom","clientId":"abc","clientSecret":"x"}},"pages":[{}]})json",
        R"json({"version":{"major":5},"services":{"Z":{"plugin":"builtin.zoom","clientId":"abc","mode":"keys"}},"pages":[{}]})json",
        R"json({"version":{"major":5},"services":{"Z":{"plugin":"builtin.zoom","mode":"auto"}},"pages":[{}]})json",
        R"json({"version":{"major":5},"services":{"Z":{"plugin":"builtin.zoom","clientId":"abc","labels":{"mute":"x"}}},"pages":[{}]})json",
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
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.process-viewer"},{"plugin":"builtin.process-viewer","topN":7}]}]})json";
    AppSettings processViewer{};
    if (FAILED(ParseAppSettingsJson(processViewerSettings, processViewer)) ||
        processViewer.dashboard.pages[0].widgets[0].privateConfiguration.View() != R"json({"topN":10})json" ||
        processViewer.dashboard.pages[0].widgets[1].privateConfiguration.View() != R"json({"topN":7})json")
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
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.studio-clock"},{"plugin":"builtin.studio-clock","showDate":true,"backgroundColor":"#010203"}]}]})json";
    AppSettings studioClock{};
    if (FAILED(ParseAppSettingsJson(studioClockSettings, studioClock)) ||
        studioClock.dashboard.pages[0].widgets[0].privateConfiguration.View() !=
            R"json({"showSecondProgress":true,"externalDotsAlwaysOn":true,"showSeconds":true,"secondsColor":"#FF1616","showDate":false,"dateFormat":"dd-mm-yyyy","timeColor":"#FF1616"})json" ||
        studioClock.dashboard.pages[0].widgets[0].overridesBackground ||
        studioClock.dashboard.pages[0].widgets[1].privateConfiguration.View().find("\"showDate\":true") ==
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
        launcher.dashboard.pages[0].widgets[0].privateConfiguration.View().find("\"iconSize\":\"huge\"") ==
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

    constexpr std::array<std::string_view, 51> invalid{
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
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.desk-clock","flipDurationMilliseconds":249}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.desk-clock","flipDurationMilliseconds":801}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.desk-clock","cardColor":"#GG0000"}]}]})json"},
        std::string_view{
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.desk-clock","unknown":true}]}]})json"},
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
        R"json({"version":{"major":5,"minor":3},"futureRoot":true,"pages":[{"futurePage":1}]})json";
    if (FAILED(ParseAppSettingsJson(newerMinor, parsed)) || parsed.dashboard.pageCount != 1 ||
        parsed.versionMinor != 3 || parsed.sourceDocument.find("futureRoot") == std::string::npos)
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
        "peek": 6, "revealDelayMilliseconds": 0, "hideDelayMilliseconds": 1200
      },
      "pages": [{}]
    })json";
    AppSettings parsed{};
    if (FAILED(ParseAppSettingsJson(full, parsed)) || parsed.versionMinor != 2 || parsed.dock.edge != DockEdge::Left ||
        parsed.dock.monitor.View() != "name:DELL" || parsed.dock.thicknessDips != 240 ||
        parsed.dock.mode != DockMode::Autohide || parsed.dock.reserveWorkArea || parsed.dock.peekPixels != 6 ||
        parsed.dock.revealDelayMilliseconds != 0 || parsed.dock.hideDelayMilliseconds != 1200)
    {
        std::wprintf(L"A complete dock object did not parse to its typed members.\n");
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

    const std::string_view rejected[]{
        R"json({"version":{"major":5,"minor":2},"dock":{"edge":"middle"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"edge":1},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"monitor":"all"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"monitor":"0"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"monitor":"name:"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"monitor":""},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"thickness":31},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"thickness":1081},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"thickness":"180"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"mode":"hidden"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"reserveWorkArea":"yes"},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"peek":0},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"peek":65},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"revealDelayMilliseconds":2001},"pages":[{}]})json",
        R"json({"version":{"major":5,"minor":2},"dock":{"hideDelayMilliseconds":10001},"pages":[{}]})json",
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
        overrides.monitor.View() != "xeneon" || !ParseDockEdgeArgument(L"none", overrides) ||
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
    return S_OK;
}

// The command-line catalog (RedXe/CommandLine.h): every switch is unique, well formed, and printed by --help; the
// help aliases are recognized; the argument scanner accepts a full valid line and names the first stray token.
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
                R"json({"version":{"major":5,"minor":3},"futureRoot":{"label":"keep\nthis","quoted":"\"{}[],:\\","list":[[],{},true,false,null,1.25e-4,-2]},"declare":{"Launch":{"plugin":"builtin.launcher"}},"pages":[{"widgets":[)json" +
                std::string(widget) + R"json(]}]})json";
            auto settings = std::make_unique<AppSettings>();
            if (FAILED(ParseAppSettingsJson(source, *settings)))
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            const auto id = settings->dashboard.pages[0].widgets[0].id;
            if (FAILED(PatchWidgetInstanceSettings(*settings, id.View(), patch)) ||
                settings->sourceDocument.find("\n  \"version\": { \"major\": 5, \"minor\": 3 }") == std::string::npos ||
                settings->sourceDocument.find("\"Launch\": { \"plugin\": \"builtin.launcher\" }") ==
                    std::string::npos ||
                settings->sourceDocument.find("\"layout\"") != std::string::npos ||
                settings->sourceDocument.find("\"shortcuts\": [\n") == std::string::npos ||
                settings->sourceDocument.back() != '\n' ||
                settings->sourceDocument.find("{ \"target\": \"C:\\\\Apps\\\\éditeur.exe\" }") == std::string::npos ||
                settings->sourceDocument.find("\"futureRoot\":") == std::string::npos ||
                settings->dashboard.pages[0].widgets[0].privateConfiguration.View().find("éditeur.exe") ==
                    std::string_view::npos ||
                settings->dashboard.pages[0].widgets[0].privateConfiguration.View().find("\"iconSize\":\"huge\"") ==
                    std::string_view::npos)
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
                reloaded->dashboard.pages[0].widgets[0].privateConfiguration.View().find("\"iconSize\":\"huge\"") ==
                    std::string_view::npos ||
                FAILED(PatchWidgetInstanceSettings(*settings, id.View(), patch)) ||
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
            R"json({"version":{"major":5,"minor":3},"pages":[{"widgets":[{"plugin":"builtin.launcher"}]}],"futurePadding":")json";
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

        result = store.PersistWidgetSettings(*candidate, instanceId, persistPatch);
        std::string afterExplicitPersist;
        if (FAILED(result) || FAILED(ReadFile(file, afterExplicitPersist)) || afterExplicitPersist == afterValidWrite)
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
    struct Test final
    {
        const wchar_t* name;
        HRESULT (*run)() noexcept;
    };
    const Test tests[]{{L"templates/schema", ValidateTemplatesAndSchema},
                       {L"parser", ValidateParser},
                       {L"dock", ValidateDockSettings},
                       {L"command line", ValidateCommandLineCatalog},
                       {L"AV profile configuration", ValidateAvControlSettings},
                       {L"low stack", ValidateLowStack},
                       {L"watcher", ValidateWatcher},
                       {L"external selection", ValidateExternalSelection},
                       {L"default recovery", ValidateDefaultRecovery},
                       {L"legacy filename", ValidateLegacyReleaseFilenameMigration},
                       {L"logs directory", ValidateLogsDirectory},
                       {L"persist formatting", ValidatePersistFormatting},
                       {L"persist rollback", ValidatePersistRollback},
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
