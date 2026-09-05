#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <windows.h>

inline constexpr uint32_t kWeatherMaximumUrlBytes = 2048;
inline constexpr uint32_t kWeatherMaximumBodyBytes = 256U * 1024U;
inline constexpr uint32_t kWeatherHttpCacheEntries = 8;
inline constexpr uint32_t kWeatherDefaultTimeoutMilliseconds = 10'000;
inline constexpr uint32_t kWeatherMaximumTimeoutMilliseconds = 30'000;
inline constexpr size_t kWeatherHttpResponseMaximumBytes = 64;
inline constexpr char kWeatherUserAgent[] =
    "RedXe/0.1.0 (XENEON EDGE dashboard weather; +https://api.met.no/doc/TermsOfService)";

// Heap-owned body. Putting kWeatherMaximumBodyBytes on the network-worker stack overflows (0xC00000FD / __chkstk)
// once FetchLive keeps more than one response live and WeatherHttpGet value-resets another.
struct WeatherHttpResponse final
{
    std::unique_ptr<char[]> body;
    uint32_t capacity = 0;
    uint32_t bytes = 0;
    uint32_t status = 0;
    uint32_t expiresDelayMilliseconds = 0;
    bool notModified = false;
};

static_assert(sizeof(WeatherHttpResponse) <= kWeatherHttpResponseMaximumBytes);

[[nodiscard]] inline std::string_view WeatherHttpBody(const WeatherHttpResponse& response) noexcept
{
    if (!response.body || response.bytes == 0)
    {
        return {};
    }
    return std::string_view(response.body.get(), response.bytes);
}

[[nodiscard]] HRESULT WeatherHttpInitialize() noexcept;
// Builds a complete Nominatim search URL from an unescaped UTF-8 city/postcode. Never performs I/O.
[[nodiscard]] HRESULT WeatherBuildLocationSearchUrl(std::string_view location, char* url, uint32_t capacity) noexcept;
void WeatherHttpShutdown() noexcept;
[[nodiscard]] uint32_t WeatherHttpGetCount() noexcept;
[[nodiscard]] HRESULT WeatherHttpGet(std::string_view url, HANDLE cancelEvent, WeatherHttpResponse& response) noexcept;
