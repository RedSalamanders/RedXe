#pragma once

#include "Host.h"

#include <cstdint>
#include <windows.h>

enum RedXePluginCapabilities : std::uint32_t
{
    RedXePluginCapabilityNone = 0,
    RedXePluginCapabilityDataProvider = 1U << 0U,
    RedXePluginCapabilityWidgetProvider = 1U << 1U,
};

struct RedXeFactoryOptions final
{
    std::uint32_t sizeBytes;
    std::uint32_t debugLevel;
    std::uint32_t reserved[8];
};

struct RedXePluginMetadata final
{
    std::uint32_t sizeBytes;
    const wchar_t* id;
    const wchar_t* displayName;
    const wchar_t* description;
    const wchar_t* author;
    const wchar_t* version;
    std::uint32_t capabilities;
    std::uint32_t reserved[8];
};

using RedXeCreateFn = HRESULT(__stdcall*)(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                                          const wchar_t* pluginId, void** result) noexcept;
using RedXeEnumeratePluginsFn = HRESULT(__stdcall*)(const RedXePluginMetadata** metadata,
                                                    std::uint32_t* count) noexcept;
using RedXePluginShutdownFn = void(__stdcall*)() noexcept;

inline constexpr char kRedXeCreateExport[] = "RedXeCreate";
inline constexpr char kRedXeEnumeratePluginsExport[] = "RedXeEnumeratePlugins";
inline constexpr char kRedXePluginShutdownExport[] = "RedXePluginShutdown";
