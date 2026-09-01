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
    RedXeBundledPluginSpec{"builtin.studio-clock", L"StudioClock.dll"},
    RedXeBundledPluginSpec{"builtin.desk-clock", L"DeskClock.dll"},
    RedXeBundledPluginSpec{"builtin.system-data", L"SystemData.dll"},
};

inline constexpr std::array kRedXeBundledWidgets{
    RedXeBundledWidgetSpec{"builtin.rotating-triangle", "rotating-triangle"},
    RedXeBundledWidgetSpec{"builtin.gdi-orbit", "gdi-orbit"},
    RedXeBundledWidgetSpec{"builtin.matrix-rain", "matrix-rain"},
    RedXeBundledWidgetSpec{"builtin.process-viewer", "process-viewer"},
    RedXeBundledWidgetSpec{"builtin.studio-clock", "studio-clock"},
    RedXeBundledWidgetSpec{"builtin.desk-clock", "desk-clock"},
};

template <typename Character>
consteval bool RedXeBundledTextEquals(const Character* left, const Character* right) noexcept
{
    std::size_t index = 0;
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
    for (std::size_t index = 0; index < kRedXeBundledPlugins.size(); ++index)
    {
        const RedXeBundledPluginSpec& candidate = kRedXeBundledPlugins[index];
        if (!candidate.pluginId || candidate.pluginId[0] == '\0' || !candidate.moduleName ||
            candidate.moduleName[0] == L'\0')
        {
            return false;
        }
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            const RedXeBundledPluginSpec& earlier = kRedXeBundledPlugins[previous];
            if (RedXeBundledTextEquals(candidate.pluginId, earlier.pluginId) ||
                RedXeBundledTextEquals(candidate.moduleName, earlier.moduleName))
            {
                return false;
            }
        }
    }

    for (std::size_t index = 0; index < kRedXeBundledWidgets.size(); ++index)
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
        for (std::size_t previous = 0; previous < index; ++previous)
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
