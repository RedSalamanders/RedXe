#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <windows.h>

inline constexpr uint32_t kWeatherMaximumNameCharacters = 64;
inline constexpr uint32_t kWeatherMaximumAlertTitleCharacters = 96;
inline constexpr uint32_t kWeatherMaximumAlertBodyCharacters = 192;
inline constexpr uint32_t kWeatherHourlyCount = 24;
inline constexpr uint32_t kWeatherDailyCount = 9;
inline constexpr uint32_t kWeatherAlertCount = 8;
inline constexpr uint32_t kWeatherMinimumRefreshMilliseconds = 5U * 60U * 1000U;
inline constexpr uint32_t kWeatherMaximumRefreshMilliseconds = 60U * 60U * 1000U;
inline constexpr uint32_t kWeatherDefaultRefreshMilliseconds = 15U * 60U * 1000U;

enum class WeatherCondition : uint32_t
{
    Unknown = 0,
    Clear,
    PartlyCloudy,
    Cloudy,
    Rain,
    Sleet,
    Snow,
    Thunder,
    Fog,
    Wind,
};

enum class WeatherAlertSeverity : uint32_t
{
    None = 0,
    Yellow,
    Orange,
    Red,
};

enum class WeatherAlertRegion : uint32_t
{
    None = 0,
    Europe,
    UnitedStates,
};

enum class WeatherLocationMode : uint32_t
{
    Automatic = 0,
    Manual,
};

enum class WeatherTemperatureUnit : uint32_t
{
    Celsius = 0,
    Fahrenheit,
};

enum class WeatherWindUnit : uint32_t
{
    KilometersPerHour = 0,
    MilesPerHour,
};

struct WeatherRgb final
{
    float red = 0.92f;
    float green = 0.92f;
    float blue = 0.94f;
};

struct WeatherHourlyForecast final
{
    uint64_t timeFileTime100ns = 0;
    float temperatureCelsius = 0.0f;
    WeatherCondition condition = WeatherCondition::Unknown;
};

struct WeatherDailyForecast final
{
    uint64_t dayFileTime100ns = 0;
    float minimumCelsius = 0.0f;
    float maximumCelsius = 0.0f;
    WeatherCondition condition = WeatherCondition::Unknown;
};

struct WeatherAlert final
{
    std::array<wchar_t, kWeatherMaximumAlertTitleCharacters> title{};
    std::array<wchar_t, kWeatherMaximumAlertBodyCharacters> description{};
    std::array<wchar_t, kWeatherMaximumAlertBodyCharacters> instructions{};
    std::array<wchar_t, kWeatherMaximumNameCharacters> authority{};
    uint64_t startFileTime100ns = 0;
    uint64_t expiryFileTime100ns = 0;
    WeatherAlertSeverity severity = WeatherAlertSeverity::None;
};

struct WeatherSnapshot final
{
    std::array<wchar_t, kWeatherMaximumNameCharacters> locationName{};
    std::array<char, 8> countryCode{};
    double latitude = 0.0;
    double longitude = 0.0;
    uint64_t observationTimeFileTime100ns = 0;
    uint64_t sunriseFileTime100ns = 0;
    uint64_t sunsetFileTime100ns = 0;
    float temperatureCelsius = 0.0f;
    float windMetersPerSecond = 0.0f;
    float todayMinimumCelsius = 0.0f;
    float todayMaximumCelsius = 0.0f;
    WeatherCondition currentCondition = WeatherCondition::Unknown;
    uint32_t hourlyCount = 0;
    uint32_t dailyCount = 0;
    uint32_t alertCount = 0;
    bool hasCoordinates = false;
    bool regionCoordinates = false;
    bool stale = false;
    std::array<WeatherHourlyForecast, kWeatherHourlyCount> hourly{};
    std::array<WeatherDailyForecast, kWeatherDailyCount> daily{};
    std::array<WeatherAlert, kWeatherAlertCount> alerts{};
};

struct WeatherConfiguration final
{
    WeatherLocationMode locationMode = WeatherLocationMode::Automatic;
    WeatherTemperatureUnit temperatureUnit = WeatherTemperatureUnit::Celsius;
    WeatherWindUnit windUnit = WeatherWindUnit::KilometersPerHour;
    std::array<char, 129> location{};
};

bool WeatherCopyWide(std::wstring_view source, wchar_t* destination, size_t capacity) noexcept;
bool WeatherCopyNarrow(std::string_view source, char* destination, size_t capacity) noexcept;
[[nodiscard]] WeatherCondition WeatherConditionFromSymbol(std::string_view symbol) noexcept;
[[nodiscard]] WeatherRgb WeatherConditionColor(WeatherCondition condition) noexcept;
[[nodiscard]] WeatherRgb WeatherAlertColor(WeatherAlertSeverity severity) noexcept;
[[nodiscard]] WeatherAlertSeverity WeatherHighestAlertSeverity(const WeatherSnapshot& snapshot) noexcept;
[[nodiscard]] WeatherAlertRegion WeatherAlertRegionFromCountry(std::string_view countryCode) noexcept;
[[nodiscard]] bool WeatherParseIso8601Utc(std::string_view text, uint64_t& fileTime100ns) noexcept;
[[nodiscard]] bool WeatherParseLatLon(std::string_view text, double& latitude, double& longitude) noexcept;
[[nodiscard]] bool WeatherTryAutomaticLocation(wchar_t* name, size_t nameCapacity, char* countryCode,
                                               size_t countryCapacity, double& latitude, double& longitude) noexcept;
[[nodiscard]] HRESULT WeatherParseLocationForecast(std::string_view json, WeatherSnapshot& snapshot) noexcept;
[[nodiscard]] HRESULT WeatherParseSunrise(std::string_view json, WeatherSnapshot& snapshot) noexcept;
[[nodiscard]] HRESULT WeatherParseNominatim(std::string_view json, WeatherSnapshot& snapshot) noexcept;
[[nodiscard]] HRESULT WeatherParseMeteoAlarm(std::string_view json, WeatherSnapshot& snapshot) noexcept;
[[nodiscard]] HRESULT WeatherParseNwsAlerts(std::string_view json, WeatherSnapshot& snapshot) noexcept;
[[nodiscard]] uint32_t WeatherFormatTemperature(float celsius, WeatherTemperatureUnit unit, wchar_t* text,
                                                uint32_t capacity) noexcept;
[[nodiscard]] uint32_t WeatherFormatTemperatureRange(float minimumCelsius, float maximumCelsius,
                                                     WeatherTemperatureUnit unit, wchar_t* text,
                                                     uint32_t capacity) noexcept;
[[nodiscard]] uint32_t WeatherFormatWind(float metersPerSecond, WeatherWindUnit unit, wchar_t* text,
                                         uint32_t capacity) noexcept;
[[nodiscard]] uint32_t WeatherFormatClock(uint64_t fileTime100ns, wchar_t* text, uint32_t capacity) noexcept;
[[nodiscard]] uint32_t WeatherFormatForecastDay(uint64_t fileTime100ns, uint64_t nowFileTime100ns,
                                                uint32_t fallbackIndex, wchar_t* text, uint32_t capacity) noexcept;
[[nodiscard]] uint32_t WeatherClampRefreshMilliseconds(uint32_t milliseconds) noexcept;
[[nodiscard]] HRESULT WeatherReadConfiguration(std::string_view json, WeatherConfiguration& configuration) noexcept;
