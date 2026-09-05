#include "../../RedXe/BundledPlugins.h"
#include "../../RedXe/Settings.h"
#include "../../RedXe/SettingsWatcher.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
  "version": { "major": 4, "minor": 0 },
  "wrapPages": true,
  "declare": {
    "Matrix": { "plugin": "builtin.matrix-rain", "settings": { "seed": 7, "densityPercent": 60 } },
    "Triangle": { "plugin": "builtin.rotating-triangle" },
  },
  "pages": [
    {
      "id": "main",
      "layout": {
        "arrangeAlong": "long-side",
        "areas": [
          { "sizeRatio": 2, "widget": "Triangle" },
          {
            "sizeRatio": 1,
            "arrangeAlong": "short-side",
            "areas": [
              { "sizeRatio": 1, "widget": "Matrix" },
              { "sizeRatio": 1, "widget": { "use": "Matrix", "override": { "settings": { "seed": 9, "densityPercent": null } } } }
            ]
          }
        ]
      }
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

[[nodiscard]] bool CoversBundledPluginCatalog(const AppSettings& settings) noexcept
{
    if (settings.pluginCount != kRedXeBundledWidgets.size())
        return false;
    for (const RedXeBundledWidgetSpec& plugin : kRedXeBundledWidgets)
    {
        if (!FindPluginSettings(settings, plugin.pluginId) || !HasWidgetExample(settings, plugin.pluginId))
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
    if (FAILED(result) || !CoversBundledPluginCatalog(debug) || !CoversBundledPluginCatalog(release) ||
        debug.dashboard.pageCount != 3 || debug.dashboard.pages[0].widgetCount != 4 ||
        debug.dashboard.pages[0].widgets[0].pluginId.View() != "builtin.launcher" ||
        debug.dashboard.pages[1].widgetCount != 8 || debug.dashboard.pages[2].widgetCount != 10 ||
        release.dashboard.pageCount != 3 || release.dashboard.pages[0].widgetCount != 1 ||
        release.dashboard.pages[1].widgetCount != 8 || release.dashboard.pages[2].widgetCount != 10 ||
        !debug.dashboard.pages[0].widgets[0].usesAdaptivePlacement || debug.logRetentionDays != 15 ||
        release.logRetentionDays != 15 || FAILED(ValidateAppSettings(debug)) || FAILED(ValidateAppSettings(release)))
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

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
        parsed.dashboard.pages[1].widgetCount != 0 ||
        parsed.dashboard.pages[0].widgets[1].privateConfiguration.View().find("\"seed\":7") == std::string_view::npos ||
        parsed.dashboard.pages[0].widgets[2].privateConfiguration.View().find("\"seed\":9") == std::string_view::npos ||
        parsed.dashboard.pages[0].widgets[2].privateConfiguration.View().find("\"densityPercent\":70") ==
            std::string_view::npos)
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
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
      "version":{"major":4},
      "logRetentionDays":30,
      "pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.gdi-orbit"}}]}}]
    })json";
    AppSettings retention{};
    if (FAILED(ParseAppSettingsJson(retentionDocument, retention)) || retention.logRetentionDays != 30)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    constexpr std::string_view authoredPages = R"json({
      "version":{"major":4},
      "pages":[
        {"id":"home","layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.rotating-triangle"}}]}},
        {"id":"system","layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.gdi-orbit"}}]}}
      ]
    })json";
    constexpr std::string_view authoredReordered = R"json({
      "version":{"major":4},
      "pages":[
        {"id":"system","layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.gdi-orbit"}}]}},
        {"id":"home","layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.rotating-triangle"}}]}}
      ]
    })json";
    constexpr std::string_view authoredHomeOnly = R"json({
      "version":{"major":4},
      "pages":[
        {"id":"home","layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.rotating-triangle"}}]}}
      ]
    })json";
    AppSettings authored{};
    AppSettings reordered{};
    AppSettings homeOnly{};
    if (FAILED(ParseAppSettingsJson(authoredPages, authored)) ||
        FAILED(ParseAppSettingsJson(authoredReordered, reordered)) ||
        FAILED(ParseAppSettingsJson(authoredHomeOnly, homeOnly)) || FAILED(MoveDashboardPage(authored, 1)) ||
        authored.dashboard.activePageId.View() != "system")
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (FAILED(PreserveActiveDashboardPage(authored, reordered)) || reordered.dashboard.activePageIndex != 0 ||
        reordered.dashboard.activePageId.View() != "system")
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (FAILED(PreserveActiveDashboardPage(authored, homeOnly)) || homeOnly.dashboard.activePageIndex != 0 ||
        homeOnly.dashboard.activePageId.View() != "home")
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    constexpr std::string_view processViewerSettings =
        R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.process-viewer"}},{"sizeRatio":1,"widget":{"plugin":"builtin.process-viewer","settings":{"topN":7}}}]}}]})json";
    AppSettings processViewer{};
    if (FAILED(ParseAppSettingsJson(processViewerSettings, processViewer)) ||
        processViewer.dashboard.pages[0].widgets[0].privateConfiguration.View() != R"json({"topN":10})json" ||
        processViewer.dashboard.pages[0].widgets[1].privateConfiguration.View() != R"json({"topN":7})json")
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    constexpr std::string_view rankedViewerSettings =
        R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.network-meter"}},{"sizeRatio":1,"widget":{"plugin":"builtin.gpu-processes","settings":{"topN":4}}},{"sizeRatio":1,"widget":{"plugin":"builtin.system-pulse"}}]}}]})json";
    AppSettings rankedViewers{};
    if (FAILED(ParseAppSettingsJson(rankedViewerSettings, rankedViewers)) ||
        rankedViewers.dashboard.pages[0].widgets[0].privateConfiguration.View() != R"json({"topN":8})json" ||
        rankedViewers.dashboard.pages[0].widgets[1].privateConfiguration.View() != R"json({"topN":4})json" ||
        rankedViewers.dashboard.pages[0].widgets[2].privateConfiguration.View() != R"json({})json")
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    constexpr std::string_view studioClockSettings =
        R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.studio-clock"}},{"sizeRatio":1,"widget":{"plugin":"builtin.studio-clock","settings":{"showDate":true,"backgroundColor":"#010203"}}}]}}]})json";
    AppSettings studioClock{};
    if (FAILED(ParseAppSettingsJson(studioClockSettings, studioClock)) ||
        studioClock.dashboard.pages[0].widgets[0].privateConfiguration.View() !=
            R"json({"showSecondProgress":true,"externalDotsAlwaysOn":true,"showSeconds":true,"secondsColor":"#FF1616","showDate":false,"dateFormat":"dd-mm-yyyy","timeColor":"#FF1616","backgroundColor":"#111111"})json" ||
        studioClock.dashboard.pages[0].widgets[1].privateConfiguration.View().find("\"showDate\":true") ==
            std::string_view::npos ||
        studioClock.dashboard.pages[0].widgets[1].privateConfiguration.View().find("\"backgroundColor\":\"#010203\"") ==
            std::string_view::npos)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    constexpr std::string_view deskClockSettings =
        R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.desk-clock"}},{"sizeRatio":1,"widget":{"plugin":"builtin.desk-clock","settings":{"flipDurationMilliseconds":250,"backgroundColor":"#010203"}}}]}}]})json";
    AppSettings deskClock{};
    if (FAILED(ParseAppSettingsJson(deskClockSettings, deskClock)) ||
        deskClock.dashboard.pages[0].widgets[0].privateConfiguration.View() !=
            R"json({"flipDurationMilliseconds":420,"backgroundColor":"#000000","cardColor":"#FF3B43","digitColor":"#FFFFFF","dateColor":"#D8D8D8"})json" ||
        deskClock.dashboard.pages[0].widgets[1].privateConfiguration.View().find("\"flipDurationMilliseconds\":250") ==
            std::string_view::npos ||
        deskClock.dashboard.pages[0].widgets[1].privateConfiguration.View().find("\"backgroundColor\":\"#010203\"") ==
            std::string_view::npos)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    constexpr std::string_view weatherSettings =
        R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.weather"}},{"sizeRatio":1,"widget":{"plugin":"builtin.weather","settings":{"locationMode":"manual","location":"48.86,2.35","temperatureUnit":"fahrenheit","windUnit":"mph"}}}]}}]})json";
    AppSettings weather{};
    if (FAILED(ParseAppSettingsJson(weatherSettings, weather)) ||
        weather.dashboard.pages[0].widgets[0].privateConfiguration.View() !=
            R"json({"locationMode":"automatic","location":"","temperatureUnit":"celsius","windUnit":"kmh"})json" ||
        weather.dashboard.pages[0].widgets[1].privateConfiguration.View().find("\"locationMode\":\"manual\"") ==
            std::string_view::npos ||
        weather.dashboard.pages[0].widgets[1].privateConfiguration.View().find("\"windUnit\":\"mph\"") ==
            std::string_view::npos)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (FAILED(PatchWidgetInstanceSettings(weather, weather.dashboard.pages[0].widgets[0].id.View(),
                                           R"json({"location":"Paris"})json")) ||
        weather.dashboard.pages[0].widgets[0].privateConfiguration.View().find("\"location\":\"Paris\"") ==
            std::string_view::npos ||
        weather.dashboard.pages[0].widgets[0].privateConfiguration.View().find("\"locationMode\":\"automatic\"") ==
            std::string_view::npos ||
        weather.dashboard.pages[0].widgets[0].privateConfiguration.View().find("\"temperatureUnit\":\"celsius\"") ==
            std::string_view::npos)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (SUCCEEDED(PatchWidgetInstanceSettings(weather, weather.dashboard.pages[0].widgets[0].id.View(),
                                              R"json({"weatherApiKey":"secret"})json")))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    constexpr std::string_view launcherSettings =
        R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.launcher"}},{"sizeRatio":1,"widget":{"plugin":"builtin.launcher","settings":{"shortcuts":[{"target":"C:\\Windows\\notepad.exe"}]}}}]}}]})json";
    AppSettings launcher{};
    if (FAILED(ParseAppSettingsJson(launcherSettings, launcher)) ||
        launcher.dashboard.pages[0].widgets[0].privateConfiguration.View() != R"json({"shortcuts":[]})json" ||
        launcher.dashboard.pages[0].widgets[1].privateConfiguration.View().find("notepad.exe") ==
            std::string_view::npos)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (FAILED(PatchWidgetInstanceSettings(launcher, launcher.dashboard.pages[0].widgets[0].id.View(),
                                           R"json({"shortcuts":[{"target":"https://example.com/"}]})json")) ||
        launcher.dashboard.pages[0].widgets[0].privateConfiguration.View().find("https://example.com/") ==
            std::string_view::npos)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (SUCCEEDED(PatchWidgetInstanceSettings(launcher, launcher.dashboard.pages[0].widgets[0].id.View(),
                                              R"json({"shortcuts":[{"target":"example.com"}]})json")))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    constexpr std::
        array<std::string_view, 37>
            invalid{
                std::string_view{R"json({"pages":[{}]})json"},
                std::string_view{R"json({"version":{"major":5},"pages":[{}]})json"},
                std::string_view{R"json({"version":{"major":4,"minor":"0"},"pages":[{}]})json"},
                std::string_view{R"json({"version":{"major":4},"unknown":1,"pages":[{}]})json"},
                std::string_view{R"json({"version":{"major":4},"version":{"major":4},"pages":[{}]})json"},
                std::string_view{R"json({version:{major:4},pages:[{}]})json"},
                std::string_view{R"json({'version':{'major':4},'pages':[{}]})json"},
                std::string_view{R"json({"version":{"major":4},"pages":[]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":0,"widget":{"plugin":"builtin.gdi-orbit"}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":"missing"}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"missing.plugin"}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.gdi-orbit","settings":{"bad":1}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.process-viewer","settings":{"topN":0}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.process-viewer","settings":{"topN":33}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.process-viewer","settings":{"topN":10,"bad":1}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.network-meter","settings":{"topN":0}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.gpu-processes","settings":{"topN":17}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.studio-clock","settings":{"showSeconds":1}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.studio-clock","settings":{"externalDotsAlwaysOn":1}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.studio-clock","settings":{"dateFormat":"locale"}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.studio-clock","settings":{"secondsColor":"#GG0000"}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.studio-clock","settings":{"unknown":true}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.desk-clock","settings":{"flipDurationMilliseconds":249}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.desk-clock","settings":{"flipDurationMilliseconds":801}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.desk-clock","settings":{"cardColor":"#GG0000"}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.desk-clock","settings":{"unknown":true}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.weather","settings":{"locationMode":"gps"}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.weather","settings":{"weatherApiKey":"secret"}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.weather","settings":{"temperatureUnit":"kelvin"}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.launcher","settings":{"shortcuts":[{"target":"example.com"}]}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.launcher","settings":{"shortcuts":[{"target":"C:\\\\Windows\\\\notepad.exe"},{"target":"C:\\\\Windows\\\\write.exe"},{"target":"C:\\\\Windows\\\\regedit.exe"},{"target":"C:\\\\Windows\\\\explorer.exe"},{"target":"C:\\\\Windows\\\\notepad.exe"}]}}}]}}]})json"},
                std::string_view{
                    R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.launcher","settings":{"extra":1,"shortcuts":[]}}}]}}]})json"},
                std::string_view{R"json({"version":{"major":4},"logRetentionDays":0,"pages":[{}]})json"},
                std::string_view{R"json({"version":{"major":4},"logRetentionDays":366,"pages":[{}]})json"},
                std::string_view{R"json({"version":{"major":4},"logRetentionDays":false,"pages":[{}]})json"},
                std::string_view{R"json({"version":{"major":4},"logRetentionDays":-1,"pages":[{}]})json"},
            };
    for (const std::string_view candidate : invalid)
    {
        result = ExpectRejected(candidate);
        if (FAILED(result))
            return result;
    }

    constexpr std::string_view newerMinor =
        R"json({"version":{"major":4,"minor":1},"futureRoot":true,"pages":[{"futurePage":1}]})json";
    if (FAILED(ParseAppSettingsJson(newerMinor, parsed)) || parsed.dashboard.pageCount != 1 ||
        parsed.versionMinor != 1 || parsed.sourceDocument.find("futureRoot") == std::string::npos)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    try
    {
        std::string tooMany = R"json({"version":{"major":4},"pages":[)json";
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
            std::string document =
                R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[)json";
            for (size_t index = 0; index < count; ++index)
            {
                if (index != 0)
                    document += ',';
                document += R"json({"sizeRatio":1,"widget":{"plugin":"builtin.rotating-triangle"}})json";
            }
            document += "]}}]}";
            return document;
        };
        const std::string maximumWidgets = widgetDocument(kMaximumWidgetsPerPage);
        if (FAILED(ParseAppSettingsJson(maximumWidgets, parsed)) ||
            parsed.dashboard.pages[0].widgetCount != kMaximumWidgetsPerPage)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        if (FAILED(ExpectRejected(widgetDocument(kMaximumWidgetsPerPage + 1))))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        std::string tooManyDeclarations = R"json({"version":{"major":4},"declare":{)json";
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
            return std::string{"{\"version\":{\"major\":4},\"pages\":[{\"name\":\""} + name + "\"}]}";
        };
        if (FAILED(ParseAppSettingsJson(namedPage(128), parsed)) || FAILED(ExpectRejected(namedPage(129))))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        const auto nestedLayout = [](size_t levels)
        {
            std::string area = R"json({"sizeRatio":1,"widget":{"plugin":"builtin.gdi-orbit"}})json";
            for (size_t level = 1; level < levels; ++level)
            {
                area = R"json({"sizeRatio":1,"arrangeAlong":"long-side","areas":[)json" + area + "]}";
            }
            return R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[)json" + area +
                   "]}}]}";
        };
        if (FAILED(ParseAppSettingsJson(nestedLayout(kMaximumLayoutDepth), parsed)) ||
            FAILED(ExpectRejected(nestedLayout(kMaximumLayoutDepth + 1U))))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        std::string oversized(1024U * 1024U + 1U, ' ');
        if (FAILED(ExpectRejected(oversized)))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    catch (...)
    {
        return E_FAIL;
    }
    return S_OK;
}

[[nodiscard]] HRESULT ValidateAvControlSettings() noexcept
{
    try
    {
        const std::string prefix = R"({"version":{"major":4},"declare":{"AV":{"plugin":"builtin.av-control","settings":)";
        const std::string suffix = R"(}},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":"AV"},{"sizeRatio":1,"widget":{"use":"AV","override":{"settings":{"profiles":[]}}}}]}}]})";
        const std::string profile = R"({"id":"office","name":"Office","outputId":"opaque-output","microphoneId":"opaque-mic","cameraId":"opaque-camera","audioRoles":"communications","restoreLevels":true,"outputLevel":45,"microphoneLevel":67})";
        auto settings = std::make_unique<AppSettings>();
        if (FAILED(ParseAppSettingsJson(prefix + R"({"profiles":[)" + profile + "]}" + suffix, *settings)))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        const auto& page = settings->dashboard.pages[0];
        if (page.widgetCount != 2 || page.widgets[0].privateConfiguration.View().find("opaque-camera") == std::string_view::npos ||
            page.widgets[1].privateConfiguration.View() != R"({"profiles":[]})" || FAILED(ValidateAppSettings(*settings)))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        const std::array<std::string, 5> invalid{
            R"({"profiles":[{"id":"incomplete"}]})",
            R"({"profiles":[],"mute":true})",
            R"({"profiles":[)" + profile + "," + profile + "]}",
            R"({"profiles":[)" + profile.substr(0, profile.size() - 1) + R"(,"microphoneLevel":101}]})",
            R"({"profiles":{}})"};
        for (const auto& value : invalid)
            if (SUCCEEDED(ParseAppSettingsJson(prefix + value + suffix, *settings))) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        // A missing profile list selects the declared default; loading either form only builds configuration.
        if (FAILED(ParseAppSettingsJson(prefix + "{}" + suffix, *settings)) ||
            settings->dashboard.pages[0].widgets[0].privateConfiguration.View() != R"({"profiles":[]})")
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        return S_OK;
    }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (const std::length_error&) { return E_INVALIDARG; }
}

[[nodiscard]] HRESULT ValidatePersistFormatting() noexcept
{
    try
    {
        constexpr std::string_view patch =
            R"json({"shortcuts":[{"target":"C:\\Apps\\éditeur.exe"},{"target":"https://example.com/a?x=1&y=2"}]})json";
        const std::array<std::string_view, 3> widgets{
            R"json("Launch")json", R"json({"plugin":"builtin.launcher","settings":{"shortcuts":[]}})json",
            R"json({"use":"Launch","override":{"settings":{"shortcuts":[]}}})json"};
        for (const auto widget : widgets)
        {
            const std::string source =
                R"json({"version":{"major":4,"minor":1},"futureRoot":{"label":"keep\nthis","quoted":"\"{}[],:\\","list":[[],{},true,false,null,1.25e-4,-2]},"declare":{"Launch":{"plugin":"builtin.launcher"}},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":)json" +
                std::string(widget) + R"json(}]}}]})json";
            auto settings = std::make_unique<AppSettings>();
            if (FAILED(ParseAppSettingsJson(source, *settings)))
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            const auto id = settings->dashboard.pages[0].widgets[0].id;
            if (FAILED(PatchWidgetInstanceSettings(*settings, id.View(), patch)) ||
                settings->sourceDocument.find("\n  \"version\": { \"major\": 4, \"minor\": 1 }") == std::string::npos ||
                settings->sourceDocument.find("\"Launch\": { \"plugin\": \"builtin.launcher\" }") ==
                    std::string::npos ||
                settings->sourceDocument.find("\"shortcuts\": [\n") == std::string::npos ||
                settings->sourceDocument.back() != '\n' ||
                settings->sourceDocument.find("{ \"target\": \"C:\\\\Apps\\\\éditeur.exe\" }") == std::string::npos ||
                settings->sourceDocument.find("\"futureRoot\":") == std::string::npos ||
                settings->dashboard.pages[0].widgets[0].privateConfiguration.View() != patch)
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
                reloaded->dashboard.pages[0].widgets[0].privateConfiguration.View() != patch ||
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
            R"json({"version":{"major":4,"minor":1},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.launcher"}}]}}],"futurePadding":")json";
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
        R"({"version":{"major":4},"declare":{"Matrix":{"plugin":"builtin.matrix-rain",)"
        R"("settings":{"seed":7,"densityPercent":60}}},"pages":[{"layout":{"arrangeAlong":"long-side",)"
        R"("areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.rotating-triangle"}},)"
        R"({"sizeRatio":1,"widget":"Matrix"}]}}]})";
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
                       {L"AV profile configuration", ValidateAvControlSettings},
                       {L"low stack", ValidateLowStack},
                       {L"watcher", ValidateWatcher},
                       {L"external selection", ValidateExternalSelection},
                       {L"default recovery", ValidateDefaultRecovery},
                       {L"legacy filename", ValidateLegacyReleaseFilenameMigration},
                       {L"logs directory", ValidateLogsDirectory},
                       {L"persist formatting", ValidatePersistFormatting},
                       {L"persist rollback", ValidatePersistRollback}};
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
