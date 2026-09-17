#include "ZoomSettings.h"

#include <cstring>

#include <yyjson.h>

namespace Zoom
{
namespace
{
void WriteDiagnostic(char* diagnostic, size_t capacity, const char* text) noexcept
{
    if (!diagnostic || capacity == 0)
    {
        return;
    }
    strncpy_s(diagnostic, capacity, text ? text : "", _TRUNCATE);
}

[[nodiscard]] HRESULT Fail(char* diagnostic, size_t capacity, const char* text) noexcept
{
    WriteDiagnostic(diagnostic, capacity, text);
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

template <size_t Capacity>
[[nodiscard]] bool CopyString(yyjson_val* value, std::array<char, Capacity>& destination, uint32_t& bytes,
                              size_t maximumBytes, bool allowEmpty) noexcept
{
    if (!yyjson_is_str(value))
    {
        return false;
    }
    const std::string_view source(yyjson_get_str(value), yyjson_get_len(value));
    if (source.size() > maximumBytes || source.size() >= Capacity || (!allowEmpty && source.empty()) ||
        source.find('\0') != std::string_view::npos)
    {
        return false;
    }
    destination.fill('\0');
    std::memcpy(destination.data(), source.data(), source.size());
    bytes = static_cast<uint32_t>(source.size());
    return true;
}
} // namespace

HRESULT ParseSettings(yyjson_val* object, Settings& settings, char* diagnostic, size_t diagnosticCapacity) noexcept
{
    WriteDiagnostic(diagnostic, diagnosticCapacity, "");
    settings = Settings{};
    std::memcpy(settings.domain.data(), kDefaultDomain, sizeof(kDefaultDomain));
    settings.domainBytes = static_cast<uint32_t>(sizeof(kDefaultDomain) - 1);
    if (!yyjson_is_obj(object))
    {
        return Fail(diagnostic, diagnosticCapacity, "Zoom settings must be a JSON object.");
    }
    bool sawClientId = false;
    yyjson_obj_iter iterator = yyjson_obj_iter_with(object);
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
    {
        const std::string_view name(yyjson_get_str(key), yyjson_get_len(key));
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        if (name == "clientId")
        {
            if (!CopyString(value, settings.clientId, settings.clientIdBytes, kMaximumClientIdBytes, true))
            {
                return Fail(diagnostic, diagnosticCapacity, "clientId must be a string of at most 128 bytes.");
            }
            sawClientId = true;
        }
        else if (name == "redirectPort")
        {
            if (!yyjson_is_uint(value) || yyjson_get_uint(value) < kMinimumRedirectPort ||
                yyjson_get_uint(value) > kMaximumRedirectPort)
            {
                return Fail(diagnostic, diagnosticCapacity, "redirectPort must be an integer from 1024 through 65535.");
            }
            settings.redirectPort = static_cast<uint32_t>(yyjson_get_uint(value));
        }
        else if (name == "domain")
        {
            if (!CopyString(value, settings.domain, settings.domainBytes, kMaximumDomainBytes, false))
            {
                return Fail(diagnostic, diagnosticCapacity, "domain must be a string of 1 through 128 bytes.");
            }
        }
        else if (name == "displayName")
        {
            if (!CopyString(value, settings.displayName, settings.displayNameBytes, kMaximumDisplayNameBytes, true))
            {
                return Fail(diagnostic, diagnosticCapacity, "displayName must be a string of at most 128 bytes.");
            }
        }
        else if (name == "autoConnect")
        {
            if (!yyjson_is_bool(value))
            {
                return Fail(diagnostic, diagnosticCapacity, "autoConnect must be a boolean.");
            }
            settings.autoConnect = yyjson_get_bool(value);
        }
        else
        {
            return Fail(diagnostic, diagnosticCapacity, "Zoom settings contain an unknown member.");
        }
    }
    if (!sawClientId || settings.clientIdBytes == 0)
    {
        return Fail(diagnostic, diagnosticCapacity, "clientId is required: the Marketplace app's client id.");
    }
    return S_OK;
}

HRESULT ParseSettingsJson(std::string_view json, Settings& settings, char* diagnostic,
                          size_t diagnosticCapacity) noexcept
{
    if (json.empty())
    {
        return Fail(diagnostic, diagnosticCapacity, "Zoom settings are empty.");
    }
    yyjson_doc* document = yyjson_read(json.data(), json.size(), YYJSON_READ_NOFLAG);
    if (!document)
    {
        return Fail(diagnostic, diagnosticCapacity, "Zoom settings are not valid JSON.");
    }
    const HRESULT result = ParseSettings(yyjson_doc_get_root(document), settings, diagnostic, diagnosticCapacity);
    yyjson_doc_free(document);
    return result;
}
} // namespace Zoom
