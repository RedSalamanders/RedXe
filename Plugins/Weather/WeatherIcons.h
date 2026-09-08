#pragma once

#include "WeatherModel.h"

// Erik Flowers Weather Icons Private Use Area (SIL OFL 1.1).
// https://erikflowers.github.io/weather-icons/
inline constexpr wchar_t kWeatherIconDaySunny = 0xF00D;
inline constexpr wchar_t kWeatherIconDayCloudy = 0xF002;
inline constexpr wchar_t kWeatherIconCloudy = 0xF013;
inline constexpr wchar_t kWeatherIconRain = 0xF019;
inline constexpr wchar_t kWeatherIconSleet = 0xF0B5;
inline constexpr wchar_t kWeatherIconSnow = 0xF01B;
inline constexpr wchar_t kWeatherIconThunderstorm = 0xF01E;
inline constexpr wchar_t kWeatherIconFog = 0xF014;
inline constexpr wchar_t kWeatherIconStrongWind = 0xF050;
inline constexpr wchar_t kWeatherIconSunrise = 0xF051;
inline constexpr wchar_t kWeatherIconSunset = 0xF052;
inline constexpr wchar_t kWeatherIconStormWarning = 0xF0F0;
inline constexpr wchar_t kWeatherIconNa = 0xF07B;

inline constexpr wchar_t kWeatherIconGlyphs[] = {
    kWeatherIconDaySunny, kWeatherIconDayCloudy,    kWeatherIconCloudy, kWeatherIconRain,       kWeatherIconSleet,
    kWeatherIconSnow,     kWeatherIconThunderstorm, kWeatherIconFog,    kWeatherIconStrongWind, kWeatherIconSunrise,
    kWeatherIconSunset,   kWeatherIconStormWarning, kWeatherIconNa,
};

[[nodiscard]] inline wchar_t WeatherIconForCondition(WeatherCondition condition) noexcept
{
    switch (condition)
    {
    case WeatherCondition::Clear:
        return kWeatherIconDaySunny;
    case WeatherCondition::PartlyCloudy:
        return kWeatherIconDayCloudy;
    case WeatherCondition::Cloudy:
        return kWeatherIconCloudy;
    case WeatherCondition::Rain:
        return kWeatherIconRain;
    case WeatherCondition::Sleet:
        return kWeatherIconSleet;
    case WeatherCondition::Snow:
        return kWeatherIconSnow;
    case WeatherCondition::Thunder:
        return kWeatherIconThunderstorm;
    case WeatherCondition::Fog:
        return kWeatherIconFog;
    case WeatherCondition::Wind:
        return kWeatherIconStrongWind;
    default:
        return kWeatherIconNa;
    }
}
