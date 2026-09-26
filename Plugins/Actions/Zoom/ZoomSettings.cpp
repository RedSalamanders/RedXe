#include "ZoomSettings.h"

#include <algorithm>
#include <cstring>

#include <yyjson.h>

namespace Zoom
{
namespace
{
void Diagnostic(char* text, size_t capacity, const char* message) noexcept
{
    if (text && capacity != 0)
    {
        (void)strncpy_s(text, capacity, message, _TRUNCATE);
    }
}

[[nodiscard]] bool EqualAsciiIgnoreCase(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (size_t index = 0; index < left.size(); ++index)
    {
        const unsigned char character = static_cast<unsigned char>(left[index]);
        const char lower = character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A'))
                                                                : static_cast<char>(character);
        if (lower != right[index])
        {
            return false;
        }
    }
    return true;
}
} // namespace

HRESULT ParseSettings(yyjson_val* object, Settings& settings, char* diagnostic, size_t diagnosticCapacity) noexcept
{
    settings = Settings{};
    if (!yyjson_is_obj(object) || yyjson_obj_size(object) != 0)
    {
        Diagnostic(diagnostic, diagnosticCapacity, "Zoom settings accept only an empty object.");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
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

bool IsMeetingUrl(std::string_view url) noexcept
{
    if (url.size() > 512 || !url.starts_with("https://"))
    {
        return false;
    }
    for (const unsigned char character : url)
    {
        if (character < 0x21 || character > 0x7e || character == '"' || character == '<' || character == '>' ||
            character == '\\')
        {
            return false;
        }
    }
    const std::string_view rest = url.substr(8);
    const size_t slash = rest.find('/');
    if (slash == std::string_view::npos)
    {
        return false;
    }
    const std::string_view host = rest.substr(0, slash);
    const bool zoomHost = EqualAsciiIgnoreCase(host, "zoom.us") ||
                          (host.size() > 8 && EqualAsciiIgnoreCase(host.substr(host.size() - 8), ".zoom.us"));
    if (!zoomHost || host.find_first_of("@:#?") != std::string_view::npos)
    {
        return false;
    }
    const std::string_view path = rest.substr(slash);
    if (!path.starts_with("/j/"))
    {
        return false;
    }
    const size_t suffix = path.find_first_of("?#", 3);
    const std::string_view meeting = path.substr(3, suffix == std::string_view::npos ? suffix : suffix - 3);
    if (meeting.size() < 9 || meeting.size() > 11)
    {
        return false;
    }
    return std::all_of(meeting.begin(), meeting.end(),
                       [](char digit) noexcept { return digit >= '0' && digit <= '9'; });
}
} // namespace Zoom
