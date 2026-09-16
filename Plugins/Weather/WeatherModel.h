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

// Early-warning routing key, chosen from the reverse-geocoded ISO 3166-1 alpha-2 country. Each region maps to
// exactly one keyless alert document per refresh; None shows the forecast without official alerts.
enum class WeatherAlertRegion : uint32_t
{
    None = 0,
    Europe,
    UnitedStates,
    Canada,
    HongKong,
};

// The body whose document produced the alerts in a snapshot; drawn in the attribution while alerts are shown.
enum class WeatherAlertProvider : uint32_t
{
    None = 0,
    MeteoAlarm,
    NationalWeatherService,
    EnvironmentCanada,
    HongKongObservatory,
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
    float precipitationMillimeters = 0.0f;
    bool hasPrecipitation = false;
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
    WeatherAlertProvider alertProvider = WeatherAlertProvider::None;
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
    // Host-resolved dashboard background (RedXeFactoryOptions::backgroundColor); the panel fill, not a settings key.
    WeatherRgb panelColor{0.0f, 0.0f, 0.0f};
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
// Uses only genuine one-hour forecast periods; false means there is no upcoming precipitation notice.
[[nodiscard]] bool WeatherSelectPrecipitationNotice(const WeatherSnapshot& snapshot, uint64_t nowFileTime100ns,
                                                    uint32_t& hourIndex) noexcept;
// Zero means there is no upcoming precipitation notice.
[[nodiscard]] uint32_t WeatherFormatPrecipitationNotice(const WeatherSnapshot& snapshot, uint64_t nowFileTime100ns,
                                                        wchar_t* text, uint32_t capacity) noexcept;
[[nodiscard]] bool WeatherSameLocalDay(uint64_t first, uint64_t second) noexcept;
[[nodiscard]] HRESULT WeatherWriteLocationSettings(const WeatherSnapshot& snapshot, char* json, uint32_t capacity,
                                                   uint32_t& written) noexcept;
[[nodiscard]] HRESULT WeatherParseSunrise(std::string_view json, WeatherSnapshot& snapshot) noexcept;
[[nodiscard]] HRESULT WeatherParseNominatim(std::string_view json, WeatherSnapshot& snapshot) noexcept;
[[nodiscard]] HRESULT WeatherParseMeteoAlarm(std::string_view json, WeatherSnapshot& snapshot) noexcept;
[[nodiscard]] HRESULT WeatherParseNwsAlerts(std::string_view json, WeatherSnapshot& snapshot) noexcept;
// ECCC MSC GeoMet `Current-Alerts` GetFeatureInfo GeoJSON at one point.
[[nodiscard]] HRESULT WeatherParseEnvironmentCanadaAlerts(std::string_view json, WeatherSnapshot& snapshot) noexcept;
// Hong Kong Observatory Open Data `warnsum` (warnings in force keyed by statement code; `{}` when none).
[[nodiscard]] HRESULT WeatherParseHongKongWarnings(std::string_view json, WeatherSnapshot& snapshot) noexcept;
// Dispatches to the region's parser; S_FALSE for a region without a provider. Every parser resets alertCount, sets
// alertProvider, and keeps at most kWeatherAlertCount bounded copies.
[[nodiscard]] HRESULT WeatherParseAlerts(WeatherAlertRegion region, std::string_view json,
                                         WeatherSnapshot& snapshot) noexcept;
// Builds the one alert document URL for a region at the coordinates; S_FALSE when the region has no provider. Never
// performs I/O.
[[nodiscard]] HRESULT WeatherBuildAlertUrl(WeatherAlertRegion region, std::string_view countryCode, double latitude,
                                           double longitude, char* url, uint32_t capacity) noexcept;
// Attribution name of the alert body, or an empty string for None.
[[nodiscard]] const wchar_t* WeatherAlertProviderName(WeatherAlertProvider provider) noexcept;
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
