#pragma once

#include "Factory.h"

#include <cstddef>
#include <cstring>
#include <string_view>

using RedXeFactoryCreator = HRESULT (*)(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                                        void** result) noexcept;

struct RedXeFactoryEntry final
{
    const RedXePluginMetadata* metadata;
    RedXeFactoryCreator create;
};

[[nodiscard]] inline HRESULT RedXeValidateEmptyNormalizedConfiguration(const RedXeFactoryOptions* options) noexcept
{
    if (!options || options->sizeBytes < kRedXeFactoryOptionsV2Size ||
        (!options->configurationJsonUtf8 && options->configurationBytes == 0))
    {
        return S_OK;
    }
    if (!options->configurationJsonUtf8 || options->configurationBytes == 0 ||
        options->configurationBytes > kRedXeMaximumFactoryConfigurationBytes)
    {
        return E_INVALIDARG;
    }

    constexpr std::string_view expected = R"json({"plugin":{},"instance":{}})json";
    return options->configurationBytes == expected.size() &&
                   std::memcmp(options->configurationJsonUtf8, expected.data(), expected.size()) == 0
               ? S_OK
               : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

[[nodiscard]] inline HRESULT RedXeEnumerateFactoryMetadata(const RedXePluginMetadata* availableMetadata,
                                                           std::uint32_t metadataCount,
                                                           const RedXePluginMetadata** metadata,
                                                           std::uint32_t* count) noexcept
{
    if (metadata)
    {
        *metadata = nullptr;
    }
    if (count)
    {
        *count = 0;
    }
    if (!metadata || !count)
    {
        return E_POINTER;
    }

    if (!availableMetadata || metadataCount == 0 || metadataCount > 256)
    {
        return E_INVALIDARG;
    }

    *metadata = availableMetadata;
    *count = metadataCount;
    return S_OK;
}

[[nodiscard]] inline HRESULT RedXeCreateFromFactoryEntries(const RedXeFactoryEntry* entries, std::uint32_t entryCount,
                                                           REFIID interfaceId, const RedXeFactoryOptions* options,
                                                           IRedXeHost* host, const char* pluginId,
                                                           void** result) noexcept
{
    if (!result)
    {
        return E_POINTER;
    }
    *result = nullptr;

    if (!entries || entryCount == 0 || entryCount > 256)
    {
        return E_INVALIDARG;
    }
    if (options && options->sizeBytes < kRedXeFactoryOptionsV1Size)
    {
        return E_INVALIDARG;
    }

    const RedXeFactoryEntry* selected = nullptr;
    if (!pluginId || pluginId[0] == '\0')
    {
        if (entryCount != 1)
        {
            return E_INVALIDARG;
        }
        selected = &entries[0];
    }
    else
    {
        for (std::uint32_t index = 0; index < entryCount; ++index)
        {
            const RedXePluginMetadata* candidate = entries[index].metadata;
            if (candidate && candidate->id && RedXeAsciiEqualsIgnoreCase(candidate->id, pluginId))
            {
                selected = &entries[index];
                break;
            }
        }
    }

    if (!selected)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    if (!selected->create)
    {
        return E_UNEXPECTED;
    }
    return selected->create(interfaceId, options, host, result);
}
