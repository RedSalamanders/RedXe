#include "Settings.h"

#include <array>
#include <cstring>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

#include <yyjson.h>

namespace
{
using unique_yyjson_doc = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;

constexpr char kDefaultSettings[] = R"json({
  "rotatingTriangleInstances": 4
})json";
} // namespace

HRESULT LoadDefaultSettings(AppSettings& settings) noexcept
{
    std::array<char, sizeof(kDefaultSettings)> json{};
    std::memcpy(json.data(), kDefaultSettings, sizeof(kDefaultSettings));

    yyjson_read_err error{};
    unique_yyjson_doc document{yyjson_read_opts(json.data(), json.size() - 1, YYJSON_READ_NOFLAG, nullptr, &error)};
    if (!document)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    yyjson_val* root = yyjson_doc_get_root(document.get());
    yyjson_val* instanceCount = yyjson_obj_get(root, "rotatingTriangleInstances");
    if (!yyjson_is_uint(instanceCount))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    const std::uint64_t value = yyjson_get_uint(instanceCount);
    if (value < 2 || value > 8)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    settings.rotatingTriangleInstances = static_cast<std::uint32_t>(value);
    return S_OK;
}
