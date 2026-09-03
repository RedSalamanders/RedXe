#pragma once

#include "Factory.h"

#include <atomic>
#include <cstddef>
#include <cstring>
#include <string_view>

// Shared COM plumbing for heap-owned RedXe objects.
//
// Derive with the concrete type first, then every interface the object exposes:
//
//     class MyWidget final : public RedXeComObject<MyWidget, IRedXeWidget, IRedXeGpuWidget> { ... };
//
// The first interface supplies the controlling IUnknown, so QueryInterface(IID_IUnknown) returns the same pointer
// from every interface on the object. Plugins_API.md requires that identity, and expressing it once here makes it a
// structural property of every object instead of a rule each hand-written QueryInterface has to re-establish.
//
// Deletion uses the concrete type through CRTP, so no virtual destructor and no extra vtable slot are introduced.
// The object starts with one reference, matching the factory contract that a successful creation returns one
// caller-owned reference.
template <typename Derived, typename First, typename... Rest> class RedXeComObject : public First, public Rest...
{
  public:
    RedXeComObject() = default;
    RedXeComObject(const RedXeComObject&) = delete;
    RedXeComObject& operator=(const RedXeComObject&) = delete;
    RedXeComObject(RedXeComObject&&) = delete;
    RedXeComObject& operator=(RedXeComObject&&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId == __uuidof(IUnknown))
        {
            *result = static_cast<IUnknown*>(static_cast<First*>(this));
        }
        else if (!TrySelect<First, Rest...>(interfaceId, result))
        {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return ++_references;
    }

    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        const ULONG references = --_references;
        if (references == 0)
        {
            delete static_cast<Derived*>(this);
        }
        return references;
    }

  protected:
    ~RedXeComObject() = default;

  private:
    template <typename... Candidates> [[nodiscard]] bool TrySelect(REFIID interfaceId, void** result) noexcept
    {
        return ((interfaceId == __uuidof(Candidates)
                     ? (*result = static_cast<void*>(static_cast<Candidates*>(this)), true)
                     : false) ||
                ...);
    }

    std::atomic<ULONG> _references{1};
};

using RedXeFactoryCreator = HRESULT (*)(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                                        void** result) noexcept;

struct RedXeFactoryEntry final
{
    const RedXePluginMetadata* metadata;
    RedXeFactoryCreator create;
};

struct RedXeSettingsContractEntry final
{
    const char* pluginId;
    const RedXePluginSettingsContract* contract;
};

[[nodiscard]] inline HRESULT RedXeGetPluginSettingsContractFromEntries(
    const RedXeSettingsContractEntry* entries, uint32_t entryCount, const char* requestedPluginId,
    const RedXePluginSettingsContract** contract) noexcept
{
    if (contract)
    {
        *contract = nullptr;
    }
    if (!contract)
    {
        return E_POINTER;
    }
    if (!entries || entryCount == 0 || entryCount > 256 || !requestedPluginId)
    {
        return E_INVALIDARG;
    }
    for (uint32_t index = 0; index < entryCount; ++index)
    {
        const RedXeSettingsContractEntry& candidate = entries[index];
        if (!candidate.pluginId || !RedXeAsciiEqualsIgnoreCase(candidate.pluginId, requestedPluginId))
        {
            continue;
        }
        if (!candidate.contract || candidate.contract->sizeBytes != sizeof(RedXePluginSettingsContract) ||
            !candidate.contract->schemaJsonUtf8 || candidate.contract->schemaBytes == 0 ||
            !candidate.contract->defaultsJsonUtf8 || candidate.contract->defaultsBytes == 0)
        {
            return E_UNEXPECTED;
        }
        *contract = candidate.contract;
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

[[nodiscard]] inline HRESULT RedXeGetStaticPluginSettingsContract(const char* expectedPluginId,
                                                                  const char* requestedPluginId,
                                                                  const RedXePluginSettingsContract* availableContract,
                                                                  const RedXePluginSettingsContract** contract) noexcept
{
    if (contract)
    {
        *contract = nullptr;
    }
    if (!contract)
    {
        return E_POINTER;
    }
    if (!expectedPluginId || !requestedPluginId || !RedXeAsciiEqualsIgnoreCase(expectedPluginId, requestedPluginId))
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    if (!availableContract || availableContract->sizeBytes != sizeof(RedXePluginSettingsContract) ||
        !availableContract->schemaJsonUtf8 || availableContract->schemaBytes == 0 ||
        !availableContract->defaultsJsonUtf8 || availableContract->defaultsBytes == 0)
    {
        return E_UNEXPECTED;
    }
    *contract = availableContract;
    return S_OK;
}

[[nodiscard]] inline HRESULT RedXeValidateEmptyNormalizedConfiguration(const RedXeFactoryOptions* options) noexcept
{
    if (!options)
    {
        return S_OK;
    }
    if (options->sizeBytes != sizeof(RedXeFactoryOptions))
    {
        return E_INVALIDARG;
    }
    if (!options->configurationJsonUtf8 && options->configurationBytes == 0)
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
                                                           uint32_t metadataCount, const RedXePluginMetadata** metadata,
                                                           uint32_t* count) noexcept
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

[[nodiscard]] inline HRESULT RedXeCreateFromFactoryEntries(const RedXeFactoryEntry* entries, uint32_t entryCount,
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
    if (options && options->sizeBytes != sizeof(RedXeFactoryOptions))
    {
        return E_INVALIDARG;
    }

    const RedXeFactoryEntry* selected = nullptr;
    if (!pluginId || pluginId[0] == '\0')
    {
        return E_INVALIDARG;
    }
    for (uint32_t index = 0; index < entryCount; ++index)
    {
        const RedXePluginMetadata* candidate = entries[index].metadata;
        if (candidate && candidate->id && RedXeAsciiEqualsIgnoreCase(candidate->id, pluginId))
        {
            selected = &entries[index];
            break;
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
