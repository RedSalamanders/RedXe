#include "../../Plugins/WeatherLocation/LocationPolicy.h"

#include <array>
#include <cmath>
#include <cstdio>

namespace
{
constexpr HRESULT kFailure = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
#define CHECK(...)                                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(__VA_ARGS__))                                                                                            \
        {                                                                                                              \
            std::printf("%s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);                                              \
            return kFailure;                                                                                           \
        }                                                                                                              \
    } while (false)

constexpr uint64_t kNow = 134330868000000000ULL;
constexpr uint64_t kSecond = 10000000;
constexpr WeatherLocationPosition kParis{48.8566, 2.3522, 100.0, kNow};

struct Sources
{
    std::array<HRESULT, 4> results{E_ACCESSDENIED, HRESULT_FROM_WIN32(ERROR_TIMEOUT), REGDB_E_CLASSNOTREG, S_FALSE};
    std::array<WeatherLocationPosition, 4> positions{kParis, kParis, kParis, kParis};
    std::array<WeatherLocationSource, 4> calls{};
    size_t count = 0;

    static HRESULT Read(void* context, WeatherLocationSource source, WeatherLocationPosition& position) noexcept
    {
        auto& self = *static_cast<Sources*>(context);
        if (self.count >= self.calls.size())
            return E_UNEXPECTED;
        self.calls[self.count++] = source;
        const auto index = static_cast<size_t>(source);
        position = self.positions[index]; // Even failed APIs can leave partial outputs behind.
        return self.results[index];
    }
};
} // namespace

HRESULT RunWeatherLocationPolicyTests() noexcept
{
    WeatherLocationPosition found = kParis;
    CHECK(WeatherResolveLocation(nullptr, nullptr, kNow, found) == E_POINTER && std::isnan(found.latitude));
    for (size_t success = 0; success < 4; ++success)
    {
        Sources sources;
        sources.results[success] = S_OK;
        CHECK(WeatherResolveLocation(Sources::Read, &sources, kNow, found) == S_OK);
        CHECK(found.latitude == kParis.latitude && found.longitude == kParis.longitude);
        CHECK(sources.count == success + 1);
        for (size_t call = 0; call < sources.count; ++call)
            CHECK(static_cast<size_t>(sources.calls[call]) == call);
    }
    Sources unavailable;
    CHECK(WeatherResolveLocation(Sources::Read, &unavailable, kNow, found) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
    CHECK(unavailable.count == 4 && std::isnan(found.latitude) && std::isnan(found.longitude));

    std::array<WeatherLocationPosition, 12> invalid{};
    invalid.fill(kParis);
    invalid[0].latitude = std::numeric_limits<double>::quiet_NaN();
    invalid[1].longitude = std::numeric_limits<double>::infinity();
    invalid[2].latitude = 90.01;
    invalid[3].longitude = -180.01;
    invalid[4].accuracyMeters = -1;
    invalid[5].accuracyMeters = 50001;
    invalid[6].accuracyMeters = std::numeric_limits<double>::quiet_NaN();
    invalid[7].timestamp = kNow - 300 * kSecond - 1;
    invalid[8].timestamp = kNow + 60 * kSecond + 1;
    invalid[9].timestamp = 0;
    invalid[10].timestamp = UINT64_MAX;
    invalid[11] = {}; // Missing fields must not become a valid fix at 0,0.
    for (const auto& bad : invalid)
    {
        Sources sources;
        sources.results.fill(S_OK);
        sources.positions[0] = sources.positions[1] = sources.positions[2] = bad;
        sources.positions[3] = {kParis.latitude, kParis.longitude};
        CHECK(WeatherResolveLocation(Sources::Read, &sources, kNow, found) == S_OK);
        CHECK(sources.count == 4 && found.latitude == kParis.latitude && found.timestamp == 0);
    }
    for (const auto& bad : invalid)
    {
        if (std::isfinite(bad.latitude) && std::isfinite(bad.longitude) && bad.latitude >= -90.0 &&
            bad.latitude <= 90.0 && bad.longitude >= -180.0 && bad.longitude <= 180.0)
            continue;
        Sources sources;
        sources.results[3] = S_OK;
        sources.positions[3] = bad;
        CHECK(FAILED(WeatherResolveLocation(Sources::Read, &sources, kNow, found)) && std::isnan(found.latitude));
    }
    for (const auto time : {kNow - 300 * kSecond, kNow + 60 * kSecond})
    {
        Sources sources;
        sources.results[0] = S_OK;
        sources.positions[0] = {0.0, 0.0, 50000.0, time}; // Real zero coordinates and inclusive quality bounds.
        CHECK(WeatherResolveLocation(Sources::Read, &sources, kNow, found) == S_OK && sources.count == 1);
    }
    std::printf("Weather location fallback ordering, failures, quality, freshness and default tests passed.\n");
    return S_OK;
}
