#pragma once

#include <cstddef>
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
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct WeatherHttpResponse final
{
    std::unique_ptr<char[]> body;
    uint32_t capacity = 0;
    uint32_t bytes = 0;
    uint32_t status = 0;
    uint32_t expiresDelayMilliseconds = 0;
    bool notModified = false;
};
#pragma warning(pop)

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
using WeatherHttpTestResponseFn = HRESULT(__stdcall*)(const char* url, uint32_t urlBytes, const char** body,
                                                      uint32_t* bodyBytes) noexcept;
// Offline harness hook; set/clear only while no network callback is active.
void WeatherHttpSetTestResponse(WeatherHttpTestResponseFn response) noexcept;

// Borrowed offline transport result, consumed before the callback's fixture changes. It exercises production
// lookup/revalidation/store logic; the legacy response hook above intentionally continues to bypass that cache.
struct WeatherHttpTestTransportResponse final
{
    uint32_t sizeBytes;
    uint32_t status;
    const char* body;
    uint32_t bodyBytes;
    uint32_t headersBytes;
    const char* headers;
};
static_assert(sizeof(WeatherHttpTestTransportResponse) == 32);
static_assert(offsetof(WeatherHttpTestTransportResponse, body) == 8);
static_assert(offsetof(WeatherHttpTestTransportResponse, headers) == 24);
using WeatherHttpTestTransportFn = HRESULT(__stdcall*)(const char* url, uint32_t urlBytes, const char* etag,
                                                       const char* lastModified, HANDLE cancelEvent,
                                                       WeatherHttpTestTransportResponse* response) noexcept;
// Test controls change only with no network callback active. Tick zero restores the real monotonic clock.
void WeatherHttpSetTestTransport(WeatherHttpTestTransportFn transport) noexcept;
void WeatherHttpSetTestCacheTick(uint64_t tick) noexcept;
void WeatherHttpCacheMetrics(uint32_t& clients, uint32_t& entries, uint32_t& bodyCapacityBytes) noexcept;
void WeatherHttpAcquireCacheClient() noexcept;
void WeatherHttpReleaseCacheClient() noexcept;
void WeatherHttpFailNextCacheAllocationForTest(bool fail) noexcept;
