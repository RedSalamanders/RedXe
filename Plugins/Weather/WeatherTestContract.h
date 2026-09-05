#pragma once

#include "WeatherHttp.h"
#include <cstdint>
#include <windows.h>

struct WeatherTestDiagnostics final
{
    uint32_t sizeBytes;
    uint32_t liveProviderCount;
    uint32_t liveWidgetCount;
    uint32_t paintCount;
    uint32_t networkWorkCount;
    uint32_t httpGetCount;
    uint32_t httpResponseSizeBytes;
    uint32_t lastDelayMilliseconds;
    uint32_t dailyCount;
    uint32_t alertCount;
    uint32_t lastStatus;
    uint32_t deviceCallbacksWhileVisible;
    float lastTemperatureCelsius;
    wchar_t lastLocation[64];
    uint32_t hourlyDrawn;
    uint32_t dailyDrawn;
    uint32_t overflowingQuads;
    BOOL precipitationNotice;
    uint32_t locationHelperRuns;
};

struct WeatherTestSnapshot final
{
    uint32_t sizeBytes;
    const char* locationForecastJson;
    uint32_t locationForecastBytes;
    const char* sunriseJson;
    uint32_t sunriseBytes;
    const char* nominatimJson;
    uint32_t nominatimBytes;
    const char* meteoAlarmJson;
    uint32_t meteoAlarmBytes;
    const char* nwsJson;
    uint32_t nwsBytes;
};

#if defined(REDXE_PLUGIN_EXPORTS)
#define REDXE_WEATHER_TEST_API __declspec(dllexport)
#else
#define REDXE_WEATHER_TEST_API
#endif

extern "C" REDXE_WEATHER_TEST_API HRESULT __stdcall RedXeWeatherGetTestDiagnostics(
    WeatherTestDiagnostics* diagnostics) noexcept;
extern "C" REDXE_WEATHER_TEST_API HRESULT __stdcall RedXeWeatherApplyTestSnapshot(
    const WeatherTestSnapshot* snapshot) noexcept;
extern "C" REDXE_WEATHER_TEST_API HRESULT __stdcall RedXeWeatherParseTestForecast(const char* json, uint32_t bytes,
                                                                                  float* temperatureCelsius,
                                                                                  uint32_t* dailyCount) noexcept;
extern "C" REDXE_WEATHER_TEST_API HRESULT __stdcall RedXeWeatherParseTestAlerts(const char* json, uint32_t bytes,
                                                                                BOOL unitedStates, uint32_t* alertCount,
                                                                                uint32_t* highestSeverity) noexcept;
extern "C" REDXE_WEATHER_TEST_API HRESULT __stdcall RedXeWeatherFormatTestUnits(
    float celsius, float metersPerSecond, BOOL fahrenheit, BOOL milesPerHour, wchar_t* temperature,
    uint32_t temperatureCapacity, wchar_t* wind, uint32_t windCapacity) noexcept;
extern "C" REDXE_WEATHER_TEST_API HRESULT __stdcall RedXeWeatherFormatTestClockAndDay(
    uint64_t fileTime100ns, uint64_t nowFileTime100ns, wchar_t* clock, uint32_t clockCapacity, wchar_t* day,
    uint32_t dayCapacity) noexcept;
extern "C" REDXE_WEATHER_TEST_API HRESULT __stdcall RedXeWeatherParseTestIso8601(const char* text,
                                                                                 uint64_t* fileTime100ns) noexcept;
extern "C" REDXE_WEATHER_TEST_API HRESULT __stdcall RedXeWeatherConditionIntentColor(uint32_t condition, float* red,
                                                                                     float* green,
                                                                                     float* blue) noexcept;
extern "C" REDXE_WEATHER_TEST_API HRESULT __stdcall RedXeWeatherParseTestLatLon(const char* text, double* latitude,
                                                                                double* longitude) noexcept;
extern "C" REDXE_WEATHER_TEST_API HRESULT __stdcall RedXeWeatherAlertRegionForCountry(const char* country,
                                                                                      uint32_t* region) noexcept;
extern "C" REDXE_WEATHER_TEST_API HRESULT __stdcall RedXeWeatherTryAutomaticLocation(
    wchar_t* name, uint32_t nameCapacity, char* country, uint32_t countryCapacity, double* latitude,
    double* longitude) noexcept;
extern "C" REDXE_WEATHER_TEST_API HRESULT __stdcall RedXeWeatherProbeHttpGetOnSmallStack(
    uint32_t stackReserveBytes) noexcept;
extern "C" REDXE_WEATHER_TEST_API HRESULT __stdcall RedXeWeatherBuildTestLocationSearchUrl(const char* location,
                                                                                           char* url,
                                                                                           uint32_t capacity) noexcept;
extern "C" REDXE_WEATHER_TEST_API void __stdcall RedXeWeatherSetTestTime(uint64_t now) noexcept;
extern "C" REDXE_WEATHER_TEST_API void __stdcall RedXeWeatherSetTestServices(WeatherHttpTestResponseFn response,
                                                                             uint32_t helperMode) noexcept;
extern "C" REDXE_WEATHER_TEST_API HRESULT __stdcall RedXeWeatherTestLocationHelper(HANDLE cancelEvent,
                                                                                   uint32_t mode) noexcept;

using WeatherGetTestDiagnosticsFn = decltype(&RedXeWeatherGetTestDiagnostics);
using WeatherApplyTestSnapshotFn = decltype(&RedXeWeatherApplyTestSnapshot);
using WeatherProbeHttpGetOnSmallStackFn = decltype(&RedXeWeatherProbeHttpGetOnSmallStack);
using WeatherFormatTestClockAndDayFn = decltype(&RedXeWeatherFormatTestClockAndDay);
inline constexpr char kWeatherGetTestDiagnosticsExport[] = "RedXeWeatherGetTestDiagnostics";
inline constexpr char kWeatherApplyTestSnapshotExport[] = "RedXeWeatherApplyTestSnapshot";
inline constexpr char kWeatherParseTestForecastExport[] = "RedXeWeatherParseTestForecast";
inline constexpr char kWeatherParseTestAlertsExport[] = "RedXeWeatherParseTestAlerts";
inline constexpr char kWeatherFormatTestUnitsExport[] = "RedXeWeatherFormatTestUnits";
inline constexpr char kWeatherFormatTestClockAndDayExport[] = "RedXeWeatherFormatTestClockAndDay";
inline constexpr char kWeatherParseTestIso8601Export[] = "RedXeWeatherParseTestIso8601";
inline constexpr char kWeatherConditionIntentColorExport[] = "RedXeWeatherConditionIntentColor";
inline constexpr char kWeatherParseTestLatLonExport[] = "RedXeWeatherParseTestLatLon";
inline constexpr char kWeatherAlertRegionForCountryExport[] = "RedXeWeatherAlertRegionForCountry";
inline constexpr char kWeatherTryAutomaticLocationExport[] = "RedXeWeatherTryAutomaticLocation";
inline constexpr char kWeatherProbeHttpGetOnSmallStackExport[] = "RedXeWeatherProbeHttpGetOnSmallStack";
inline constexpr char kWeatherBuildTestLocationSearchUrlExport[] = "RedXeWeatherBuildTestLocationSearchUrl";

#undef REDXE_WEATHER_TEST_API
