#include "ZoomSettings.h"

#include <algorithm>
#include <cstring>

#include <yyjson.h>

namespace Zoom
{
namespace
{
// The Zoom SDK members of a v1.0.102 settings file; that release's templates authored three of them.
constexpr std::string_view kRetiredMemberNames[] = {"clientId",    "redirectPort", "domain", "displayName",
                                                    "autoConnect", "mode",         "labels"};

void Diagnostic(char* text, size_t capacity, const char* message) noexcept
{
    if (text && capacity != 0)
    {
        (void)strncpy_s(text, capacity, message, _TRUNCATE);
    }
}
} // namespace

HRESULT ParseSettings(yyjson_val* object, Settings& settings, char* diagnostic, size_t diagnosticCapacity) noexcept
{
    settings = Settings{};
    if (!yyjson_is_obj(object))
    {
        Diagnostic(diagnostic, diagnosticCapacity, "Zoom settings must be a JSON object.");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    // A settings file written by v1.0.102 keeps loading: its retired members are ignored whatever their value. Any
    // other member is an error.
    yyjson_obj_iter iterator = yyjson_obj_iter_with(object);
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
    {
        const std::string_view name(yyjson_get_str(key), yyjson_get_len(key));
        if (std::find(std::begin(kRetiredMemberNames), std::end(kRetiredMemberNames), name) ==
            std::end(kRetiredMemberNames))
        {
            Diagnostic(diagnostic, diagnosticCapacity,
                       "Zoom settings contain an unknown member; the entry takes only \"plugin\".");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    Diagnostic(diagnostic, diagnosticCapacity, "");
    return S_OK;
}

HRESULT ParseSettingsJson(std::string_view json, Settings& settings, char* diagnostic,
                          size_t diagnosticCapacity) noexcept
{
    yyjson_read_err error{};
    yyjson_doc* document =
        yyjson_read_opts(const_cast<char*>(json.data()), json.size(), YYJSON_READ_NOFLAG, nullptr, &error);
    if (!document)
    {
        Diagnostic(diagnostic, diagnosticCapacity, "Zoom settings must be a JSON object.");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    const HRESULT result = ParseSettings(yyjson_doc_get_root(document), settings, diagnostic, diagnosticCapacity);
    yyjson_doc_free(document);
    return result;
}
} // namespace Zoom
