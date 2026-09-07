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
};

// Bundled, settings-visible, documented, and test-covered widgets that no shipped template places. A native-window
// widget's child HWND sits over the swap chain and forces composed presentation for the whole window while it is
// placed, so the shipped pages stay HWND-free and the native-window example is opt-in (Core_Settings.md template
// coverage exception).
inline constexpr std::array kRedXeOptInBundledWidgetIds{
    "builtin.gdi-orbit",
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
