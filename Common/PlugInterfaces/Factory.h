#pragma once

#include "Host.h"

#include <cstddef>
#include <cstdint>
#include <windows.h>

enum RedXePluginCapabilities : std::uint32_t
{
    RedXePluginCapabilityNone = 0,
    RedXePluginCapabilityWidgetProvider = 1U << 0U,
};

struct RedXeFactoryOptions final
{
    std::uint32_t sizeBytes;
    std::uint32_t debugLevel;
    const char* configurationJsonUtf8;
    std::uint32_t configurationBytes;
};

// The minimum accepted prefix is immutable even if later headers append fields.
inline constexpr std::uint32_t kRedXeFactoryOptionsV1Size = 8;
inline constexpr std::uint32_t kRedXeFactoryOptionsV2Size = static_cast<std::uint32_t>(
    offsetof(RedXeFactoryOptions, configurationBytes) + sizeof(RedXeFactoryOptions::configurationBytes));
inline constexpr std::uint32_t kRedXeMaximumFactoryConfigurationBytes = 4096;
static_assert(offsetof(RedXeFactoryOptions, configurationJsonUtf8) == kRedXeFactoryOptionsV1Size);
static_assert(offsetof(RedXeFactoryOptions, configurationBytes) == 16);
static_assert(kRedXeFactoryOptionsV2Size == 20);
static_assert(sizeof(RedXeFactoryOptions) == 24);

struct RedXePluginMetadata final
{
    std::uint32_t sizeBytes;
    const char* id;
    const wchar_t* displayName;
    const wchar_t* description;
    const wchar_t* author;
    const wchar_t* version;
    std::uint32_t capabilities;
};

#if defined(REDXE_PLUGIN_EXPORTS)
#define REDXE_PLUGIN_API __declspec(dllexport)
#else
#define REDXE_PLUGIN_API
#endif

extern "C"
{
    REDXE_PLUGIN_API HRESULT __stdcall RedXeCreate(REFIID interfaceId, const RedXeFactoryOptions* options,
                                                   IRedXeHost* host, const char* pluginId, void** result) noexcept;
    REDXE_PLUGIN_API HRESULT __stdcall RedXeEnumeratePlugins(const RedXePluginMetadata** metadata,
                                                             std::uint32_t* count) noexcept;
    REDXE_PLUGIN_API void __stdcall RedXePluginShutdown() noexcept;
}

using RedXeCreateFn = decltype(&RedXeCreate);
using RedXeEnumeratePluginsFn = decltype(&RedXeEnumeratePlugins);
using RedXePluginShutdownFn = decltype(&RedXePluginShutdown);

inline constexpr char kRedXeCreateExport[] = "RedXeCreate";
inline constexpr char kRedXeEnumeratePluginsExport[] = "RedXeEnumeratePlugins";
inline constexpr char kRedXePluginShutdownExport[] = "RedXePluginShutdown";

[[nodiscard]] constexpr char RedXeAsciiLower(char value) noexcept
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

[[nodiscard]] constexpr bool RedXeAsciiEqualsIgnoreCase(const char* left, const char* right) noexcept
{
    if (!left || !right)
    {
        return false;
    }
    while (*left != '\0' && *right != '\0')
    {
        if (RedXeAsciiLower(*left) != RedXeAsciiLower(*right))
        {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == *right;
}

[[nodiscard]] constexpr bool RedXeIsMachineIdCharacter(char value, bool first) noexcept
{
    const bool alphaNumeric =
        (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9');
    return alphaNumeric || (!first && (value == '_' || value == '.' || value == '-'));
}

[[nodiscard]] constexpr bool RedXeIsValidMachineId(const char* value) noexcept
{
    if (!value || value[0] == '\0')
    {
        return false;
    }
    std::uint32_t length = 0;
    while (value[length] != '\0')
    {
        if (length >= 128 || !RedXeIsMachineIdCharacter(value[length], length == 0))
        {
            return false;
        }
        ++length;
    }
    return true;
}

#undef REDXE_PLUGIN_API
