#pragma once

#include <cstdint>
#include <limits>
#include <windows.h>

enum class WeatherLocationSource
{
    Current,
    Coarse,
    CachedReport,
    WindowsDefault
};

struct WeatherLocationPosition
{
    double latitude = std::numeric_limits<double>::quiet_NaN();
    double longitude = std::numeric_limits<double>::quiet_NaN();
    double accuracyMeters = -1.0;
    uint64_t timestamp = 0; // UTC, in FILETIME's 100 ns units.
};

using WeatherLocationProvider = HRESULT (*)(void* context, WeatherLocationSource source,
                                            WeatherLocationPosition& position) noexcept;

// This policy has no COM/WinRT dependency. Provider failures and unusable results advance to the next source.
HRESULT WeatherResolveLocation(WeatherLocationProvider provider, void* context, uint64_t now,
                               WeatherLocationPosition& position) noexcept;
