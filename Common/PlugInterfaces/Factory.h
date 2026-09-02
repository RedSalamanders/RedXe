#pragma once

#include "Host.h"

#include <cstdint>
#include <windows.h>

// Every sizeBytes field must equal the current record's sizeof value.

// Services exposed by a logical plugin.
enum RedXePluginCapabilities : uint32_t
{
    RedXePluginCapabilityNone = 0,
    RedXePluginCapabilityWidgetProvider = 1U << 0U,
    RedXePluginCapabilityDataSource = 1U << 1U,
};

// Current factory input; sizeBytes must equal sizeof(RedXeFactoryOptions).
struct RedXeFactoryOptions final
{
    uint32_t sizeBytes;
    uint32_t debugLevel;
    const char* configurationJsonUtf8;
    uint32_t configurationBytes;
};

inline constexpr uint32_t kRedXeMaximumFactoryConfigurationBytes = 8192;
static_assert(sizeof(RedXeFactoryOptions) == 24);

// Module-owned metadata for one logical plugin.
struct RedXePluginMetadata final
{
    uint32_t sizeBytes;
    const char* id;
    const wchar_t* displayName;
    const wchar_t* description;
    const wchar_t* author;
    const wchar_t* version;
    uint32_t capabilities;
};

// Module-owned settings schema and defaults for one logical plugin.
struct RedXePluginSettingsContract final
{
    uint32_t sizeBytes;
    const char* schemaJsonUtf8;
    uint32_t schemaBytes;
    const char* defaultsJsonUtf8;
    uint32_t defaultsBytes;
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
                                                             uint32_t* count) noexcept;
    REDXE_PLUGIN_API HRESULT __stdcall RedXeGetPluginSettingsContract(
        const char* pluginId, const RedXePluginSettingsContract** contract) noexcept;
    REDXE_PLUGIN_API void __stdcall RedXePluginShutdown() noexcept;
}

using RedXeCreateFn = decltype(&RedXeCreate);
using RedXeEnumeratePluginsFn = decltype(&RedXeEnumeratePlugins);
using RedXePluginShutdownFn = decltype(&RedXePluginShutdown);
using RedXeGetPluginSettingsContractFn = decltype(&RedXeGetPluginSettingsContract);

inline constexpr char kRedXeCreateExport[] = "RedXeCreate";
inline constexpr char kRedXeEnumeratePluginsExport[] = "RedXeEnumeratePlugins";
inline constexpr char kRedXePluginShutdownExport[] = "RedXePluginShutdown";
inline constexpr char kRedXeGetPluginSettingsContractExport[] = "RedXeGetPluginSettingsContract";

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
    uint32_t length = 0;
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
