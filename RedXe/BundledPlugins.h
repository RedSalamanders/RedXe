#pragma once

#include <array>
#include <cstddef>

// Maps every bundled logical plugin to its DLL.
struct RedXeBundledPluginSpec final
{
    const char* pluginId;
    const wchar_t* moduleName;
};

// Maps settings-visible widget plugins to their one current widget type.
struct RedXeBundledWidgetSpec final
{
    const char* pluginId;
    const char* typeId;
};

// Names a bundled plugin the host starts as a headless IRedXeService when the settings document configures it under
// `services`. A service plugin ID is never a widget plugin ID.
struct RedXeBundledServiceSpec final
{
    const char* pluginId;
};

inline constexpr std::array kRedXeBundledPlugins{
    RedXeBundledPluginSpec{"builtin.rotating-triangle", L"RotatingTriangle.dll"},
    RedXeBundledPluginSpec{"builtin.gdi-orbit", L"GdiOrbit.dll"},
    RedXeBundledPluginSpec{"builtin.matrix-rain", L"MatrixRain.dll"},
    RedXeBundledPluginSpec{"builtin.process-viewer", L"ProcessViewer.dll"},
    RedXeBundledPluginSpec{"builtin.system-pulse", L"ProcessViewer.dll"},
    RedXeBundledPluginSpec{"builtin.cpu-meter", L"ProcessViewer.dll"},
    RedXeBundledPluginSpec{"builtin.memory-meter", L"ProcessViewer.dll"},
    RedXeBundledPluginSpec{"builtin.network-meter", L"ProcessViewer.dll"},
    RedXeBundledPluginSpec{"builtin.storage-meter", L"ProcessViewer.dll"},
    RedXeBundledPluginSpec{"builtin.gpu-meter", L"ProcessViewer.dll"},
    RedXeBundledPluginSpec{"builtin.gpu-processes", L"ProcessViewer.dll"},
    RedXeBundledPluginSpec{"builtin.power-meter", L"ProcessViewer.dll"},
    RedXeBundledPluginSpec{"builtin.thermal-meter", L"ProcessViewer.dll"},
    RedXeBundledPluginSpec{"builtin.studio-clock", L"StudioClock.dll"},
    RedXeBundledPluginSpec{"builtin.desk-clock", L"DeskClock.dll"},
    RedXeBundledPluginSpec{"builtin.launcher", L"Launcher.dll"},
    RedXeBundledPluginSpec{"builtin.weather", L"Weather.dll"},
    RedXeBundledPluginSpec{"builtin.av-control", L"AVControl.dll"},
    RedXeBundledPluginSpec{"builtin.system-data", L"SystemData.dll"},
    RedXeBundledPluginSpec{"builtin.logicon", L"Logicon.dll"},
    RedXeBundledPluginSpec{"builtin.logicon-monitor", L"Logicon.dll"},
};

inline constexpr std::array kRedXeBundledWidgets{
    RedXeBundledWidgetSpec{"builtin.rotating-triangle", "rotating-triangle"},
    RedXeBundledWidgetSpec{"builtin.gdi-orbit", "gdi-orbit"},
    RedXeBundledWidgetSpec{"builtin.matrix-rain", "matrix-rain"},
    RedXeBundledWidgetSpec{"builtin.process-viewer", "process-viewer"},
    RedXeBundledWidgetSpec{"builtin.system-pulse", "system-pulse"},
    RedXeBundledWidgetSpec{"builtin.cpu-meter", "cpu-meter"},
    RedXeBundledWidgetSpec{"builtin.memory-meter", "memory-meter"},
    RedXeBundledWidgetSpec{"builtin.network-meter", "network-meter"},
    RedXeBundledWidgetSpec{"builtin.storage-meter", "storage-meter"},
    RedXeBundledWidgetSpec{"builtin.gpu-meter", "gpu-meter"},
    RedXeBundledWidgetSpec{"builtin.gpu-processes", "gpu-processes"},
    RedXeBundledWidgetSpec{"builtin.power-meter", "power-meter"},
    RedXeBundledWidgetSpec{"builtin.thermal-meter", "thermal-meter"},
    RedXeBundledWidgetSpec{"builtin.studio-clock", "studio-clock"},
    RedXeBundledWidgetSpec{"builtin.desk-clock", "desk-clock"},
    RedXeBundledWidgetSpec{"builtin.launcher", "launcher"},
    RedXeBundledWidgetSpec{"builtin.weather", "weather"},
    RedXeBundledWidgetSpec{"builtin.av-control", "av-control"},
    RedXeBundledWidgetSpec{"builtin.logicon-monitor", "logicon-monitor"},
};

// Headless service plugins the host may start from the `services` settings root. Each is created once per process.
inline constexpr std::array kRedXeBundledServices{
    RedXeBundledServiceSpec{"builtin.logicon"},
};

// Bundled, settings-visible, documented, and test-covered widgets that no shipped template places. A native-window
// widget's child HWND sits over the swap chain and forces composed presentation for the whole window while it is
// placed, so the shipped pages stay HWND-free and the native-window example is opt-in (Core_Settings.md template
// coverage exception).
inline constexpr std::array kRedXeOptInBundledWidgetIds{
    "builtin.gdi-orbit",
};

// Developer-only widgets: catalogued and schema-accepted in every build so both shipped templates parse everywhere,
// placed only by the Debug template, and constructed only by Debug builds of their DLL. A Release build publishes
// the contract but refuses the instance, so a Release document that places one shows a host placeholder tile. The
// Logicon monitor is the developer view of the headless Logicon service; Release ships the service without a tile.
inline constexpr std::array kRedXeDebugOnlyBundledWidgetIds{
    "builtin.logicon-monitor",
};

template <typename Character>
consteval bool RedXeBundledTextEquals(const Character* left, const Character* right) noexcept
{
    size_t index = 0;
    while (left[index] != static_cast<Character>(0) && right[index] != static_cast<Character>(0))
    {
        if (left[index] != right[index])
        {
            return false;
        }
        ++index;
    }
    return left[index] == right[index];
}

consteval bool RedXeBundledPluginCatalogIsValid() noexcept
{
    for (size_t index = 0; index < kRedXeBundledPlugins.size(); ++index)
    {
        const RedXeBundledPluginSpec& candidate = kRedXeBundledPlugins[index];
        if (!candidate.pluginId || candidate.pluginId[0] == '\0' || !candidate.moduleName ||
            candidate.moduleName[0] == L'\0')
        {
            return false;
        }
        for (size_t previous = 0; previous < index; ++previous)
        {
            const RedXeBundledPluginSpec& earlier = kRedXeBundledPlugins[previous];
            if (RedXeBundledTextEquals(candidate.pluginId, earlier.pluginId))
            {
                return false;
            }
        }
    }

    for (const char* optIn : kRedXeOptInBundledWidgetIds)
    {
        bool catalogued = false;
        for (const RedXeBundledWidgetSpec& widget : kRedXeBundledWidgets)
        {
            catalogued = catalogued || RedXeBundledTextEquals(optIn, widget.pluginId);
        }
        if (!catalogued)
        {
            return false;
        }
    }

    for (const char* debugOnly : kRedXeDebugOnlyBundledWidgetIds)
    {
        bool catalogued = false;
        for (const RedXeBundledWidgetSpec& widget : kRedXeBundledWidgets)
        {
            catalogued = catalogued || RedXeBundledTextEquals(debugOnly, widget.pluginId);
        }
        if (!catalogued)
        {
            return false;
        }
    }

    for (size_t index = 0; index < kRedXeBundledServices.size(); ++index)
    {
        const RedXeBundledServiceSpec& service = kRedXeBundledServices[index];
        if (!service.pluginId || service.pluginId[0] == '\0')
        {
            return false;
        }
        bool foundPlugin = false;
        for (const RedXeBundledPluginSpec& plugin : kRedXeBundledPlugins)
        {
            foundPlugin = foundPlugin || RedXeBundledTextEquals(service.pluginId, plugin.pluginId);
        }
        if (!foundPlugin)
        {
            return false;
        }
        // A service plugin ID is never also a widget plugin ID; the monitor tile has its own ID.
        for (const RedXeBundledWidgetSpec& widget : kRedXeBundledWidgets)
        {
            if (RedXeBundledTextEquals(service.pluginId, widget.pluginId))
            {
                return false;
            }
        }
        for (size_t previous = 0; previous < index; ++previous)
        {
            if (RedXeBundledTextEquals(service.pluginId, kRedXeBundledServices[previous].pluginId))
            {
                return false;
            }
        }
    }

    for (size_t index = 0; index < kRedXeBundledWidgets.size(); ++index)
    {
        const RedXeBundledWidgetSpec& candidate = kRedXeBundledWidgets[index];
        if (!candidate.pluginId || candidate.pluginId[0] == '\0' || !candidate.typeId || candidate.typeId[0] == '\0')
        {
            return false;
        }
        bool foundPlugin = false;
        for (const RedXeBundledPluginSpec& plugin : kRedXeBundledPlugins)
        {
            foundPlugin = foundPlugin || RedXeBundledTextEquals(candidate.pluginId, plugin.pluginId);
        }
        if (!foundPlugin)
        {
            return false;
        }
        for (size_t previous = 0; previous < index; ++previous)
        {
            const RedXeBundledWidgetSpec& earlier = kRedXeBundledWidgets[previous];
            if (RedXeBundledTextEquals(candidate.pluginId, earlier.pluginId) ||
                RedXeBundledTextEquals(candidate.typeId, earlier.typeId))
            {
                return false;
            }
        }
    }
    return true;
}

static_assert(RedXeBundledPluginCatalogIsValid());
