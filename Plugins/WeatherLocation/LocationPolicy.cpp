#include "LocationPolicy.h"

#include <cmath>

HRESULT WeatherResolveLocation(WeatherLocationProvider provider, void* context, uint64_t now,
                               WeatherLocationPosition& position) noexcept
{
    position = {};
    if (!provider)
        return E_POINTER;
    constexpr WeatherLocationSource sources[]{WeatherLocationSource::Current, WeatherLocationSource::Coarse,
                                              WeatherLocationSource::CachedReport,
                                              WeatherLocationSource::WindowsDefault};
    constexpr uint64_t maximumAge = 5ULL * 60 * 10000000;
    constexpr uint64_t futureTolerance = 60ULL * 10000000;
    for (const auto source : sources)
    {
        WeatherLocationPosition candidate{};
        if (provider(context, source, candidate) != S_OK || !std::isfinite(candidate.latitude) ||
            !std::isfinite(candidate.longitude) || candidate.latitude < -90.0 || candidate.latitude > 90.0 ||
            candidate.longitude < -180.0 || candidate.longitude > 180.0)
            continue;
        // A default is explicitly entered by the Windows user and has neither accuracy nor observation time.
        if (source != WeatherLocationSource::WindowsDefault &&
            (!std::isfinite(candidate.accuracyMeters) || candidate.accuracyMeters < 0.0 ||
             candidate.accuracyMeters > 50000.0 || candidate.timestamp == 0 ||
             (candidate.timestamp <= now ? now - candidate.timestamp > maximumAge
                                         : candidate.timestamp - now > futureTolerance)))
            continue;
        position = candidate;
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}
