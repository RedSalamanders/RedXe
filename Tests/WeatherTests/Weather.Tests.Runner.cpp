#include "Common/FailureReports.h"
#include "PlugInterfaces/Factory.h"
#include "Plugins/Weather/Weather.Tests.Contract.h"
#include "Plugins/Weather/WeatherHttp.h"
#include "Weather.Tests.HttpCache.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

[[nodiscard]] HRESULT RunWeatherRegressionTests(HMODULE module);

namespace
{
constexpr HRESULT kTestFailure = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
constexpr uint32_t kHttpStackProbeBytes = 192U * 1024U;

static_assert(sizeof(WeatherHttpResponse) <= kWeatherHttpResponseMaximumBytes);
static_assert(sizeof(WeatherHttpResponse) < kWeatherMaximumBodyBytes);

template <typename Function> [[nodiscard]] Function Resolve(HMODULE module, const char* name) noexcept
{
// Named export is bound to its declared Windows/plugin ABI; GetProcAddress erases that type.
// Owner: RedXe maintainers; review: 2027-01-31.
#pragma warning(suppress : 4191)
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

[[nodiscard]] HRESULT BuildSiblingPath(const wchar_t* relativePath, std::array<wchar_t, 1024>& path) noexcept
{
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return HRESULT_FROM_WIN32(length == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER);
    }
    wchar_t* separator = std::wcsrchr(path.data(), L'\\');
    if (!separator)
    {
        return E_UNEXPECTED;
    }
    ++separator;
    const size_t prefix = static_cast<size_t>(separator - path.data());
    const size_t suffix = std::wcslen(relativePath);
    if (prefix + suffix + 1 > path.size())
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    std::memcpy(separator, relativePath, (suffix + 1) * sizeof(wchar_t));
    return S_OK;
}

[[nodiscard]] HRESULT ValidateLocationUrls(decltype(&RedXeWeatherBuildTestLocationSearchUrl) build)
{
    struct Case final
    {
        const char* location;
        const char* encoded;
    };
    constexpr Case cases[]{{"New York", "New%20York"},
                           {"S\xC3\xA3o Paulo", "S%C3%A3o%20Paulo"},
                           {"\xE6\x9D\xB1\xE4\xBA\xAC", "%E6%9D%B1%E4%BA%AC"},
                           {"A&B#?/+%", "A%26B%23%3F%2F%2B%25"},
                           {"a-Z_0.~", "a-Z_0.~"}};
    std::array<char, 1024> url{};
    for (const auto& item : cases)
    {
        const std::string expected = std::string("https://nominatim.openstreetmap.org/search?q=") + item.encoded +
                                     "&format=json&limit=1&addressdetails=1";
        const uint32_t exactCapacity = static_cast<uint32_t>(expected.size() + 1);
        if (build(item.location, url.data(), exactCapacity) != S_OK || std::strcmp(url.data(), expected.c_str()) != 0)
            return kTestFailure;
        if (build(item.location, url.data(), exactCapacity - 1) != HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) ||
            url[0] != '\0')
            return kTestFailure;
    }
    const std::string maximum(128, ' ');
    const std::string overlong(129, 'a');
    if (build(maximum.c_str(), url.data(), static_cast<uint32_t>(url.size())) != S_OK ||
        build(overlong.c_str(), url.data(), static_cast<uint32_t>(url.size())) != E_INVALIDARG ||
        build("", url.data(), static_cast<uint32_t>(url.size())) != E_INVALIDARG ||
        build(nullptr, url.data(), static_cast<uint32_t>(url.size())) != E_INVALIDARG ||
        SUCCEEDED(build("Paris", nullptr, 1024)) || SUCCEEDED(build("Paris", url.data(), 0)))
        return kTestFailure;
    return S_OK;
}

[[nodiscard]] HRESULT Run() noexcept
{
    std::array<wchar_t, 1024> path{};
    HRESULT result = BuildSiblingPath(L"Plugins\\Weather.dll", path);
    if (FAILED(result))
    {
        return result;
    }
    wil::unique_hmodule module{
        LoadLibraryExW(path.data(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    if (!module)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const WeatherGetTestDiagnosticsFn getDiagnostics =
        Resolve<WeatherGetTestDiagnosticsFn>(module.get(), kWeatherGetTestDiagnosticsExport);
    const WeatherProbeHttpGetOnSmallStackFn probe =
        Resolve<WeatherProbeHttpGetOnSmallStackFn>(module.get(), kWeatherProbeHttpGetOnSmallStackExport);
    const WeatherFormatTestClockAndDayFn formatClockDay =
        Resolve<WeatherFormatTestClockAndDayFn>(module.get(), kWeatherFormatTestClockAndDayExport);
    const auto formatUnits =
        Resolve<decltype(&RedXeWeatherFormatTestUnits)>(module.get(), kWeatherFormatTestUnitsExport);
    const auto parseIso =
        Resolve<decltype(&RedXeWeatherParseTestIso8601)>(module.get(), kWeatherParseTestIso8601Export);
    const RedXePluginShutdownFn shutdown = Resolve<RedXePluginShutdownFn>(module.get(), kRedXePluginShutdownExport);
    const auto buildUrl = Resolve<decltype(&RedXeWeatherBuildTestLocationSearchUrl)>(
        module.get(), kWeatherBuildTestLocationSearchUrlExport);
    if (!getDiagnostics || !probe || !formatClockDay || !formatUnits || !parseIso || !shutdown || !buildUrl)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    result = ValidateLocationUrls(buildUrl);
    if (FAILED(result))
    {
        std::wprintf(L"Weather location URL encoding or bounds failed.\n");
        shutdown();
        return result;
    }

    std::array<wchar_t, 1024> fontPath{};
    result = BuildSiblingPath(L"Plugins\\weathericons-regular-webfont.ttf", fontPath);
    if (FAILED(result) || GetFileAttributesW(fontPath.data()) == INVALID_FILE_ATTRIBUTES)
    {
        std::wprintf(L"Weather Icons font missing beside Weather.dll.\n");
        shutdown();
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    WeatherTestDiagnostics diagnostics{sizeof(WeatherTestDiagnostics)};
    result = getDiagnostics(&diagnostics);
    if (FAILED(result) || diagnostics.httpResponseSizeBytes == 0 ||
        diagnostics.httpResponseSizeBytes > kWeatherHttpResponseMaximumBytes)
    {
        std::wprintf(L"Weather HTTP response is too large for the network-worker stack: %u bytes.\n",
                     diagnostics.httpResponseSizeBytes);
        shutdown();
        return FAILED(result) ? result : kTestFailure;
    }

    result = probe(kHttpStackProbeBytes);
    if (result != HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        std::wprintf(L"Weather HTTP small-stack probe failed: 0x%08X\n", static_cast<unsigned int>(result));
        shutdown();
        return FAILED(result) ? result : kTestFailure;
    }

    std::array<wchar_t, 16> temperature{};
    std::array<wchar_t, 16> wind{};
    result = formatUnits(21.0f, 17.0f / 3.6f, FALSE, FALSE, temperature.data(),
                         static_cast<uint32_t>(temperature.size()), wind.data(), static_cast<uint32_t>(wind.size()));
    if (FAILED(result) || std::wcscmp(temperature.data(), L"21\u00B0C") != 0 ||
        std::wcscmp(wind.data(), L"17km/h") != 0)
    {
        std::wprintf(L"Weather unit format mismatch: %s %s\n", temperature.data(), wind.data());
        shutdown();
        return FAILED(result) ? result : kTestFailure;
    }

    std::array<wchar_t, 8> emptyClock{};
    std::array<wchar_t, 32> emptyDay{};
    result = formatClockDay(0, 0, emptyClock.data(), static_cast<uint32_t>(emptyClock.size()), emptyDay.data(),
                            static_cast<uint32_t>(emptyDay.size()));
    if (FAILED(result) || emptyClock[0] != L'\0' || std::wcscmp(emptyDay.data(), L"Today") != 0)
    {
        std::wprintf(L"Weather empty clock/day format mismatch: %s %s\n", emptyClock.data(), emptyDay.data());
        shutdown();
        return FAILED(result) ? result : kTestFailure;
    }

    SYSTEMTIME local{};
    local.wYear = 2026;
    local.wMonth = 9;
    local.wDay = 4;
    local.wHour = 7;
    local.wMinute = 11;
    SYSTEMTIME utc{};
    FILETIME utcFileTime{};
    if (TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc) == FALSE ||
        SystemTimeToFileTime(&utc, &utcFileTime) == FALSE)
    {
        shutdown();
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const uint64_t fileTime =
        (static_cast<uint64_t>(utcFileTime.dwHighDateTime) << 32) | static_cast<uint64_t>(utcFileTime.dwLowDateTime);
    std::array<wchar_t, 8> clock{};
    std::array<wchar_t, 32> day{};
    result = formatClockDay(fileTime, fileTime, clock.data(), static_cast<uint32_t>(clock.size()), day.data(),
                            static_cast<uint32_t>(day.size()));
    if (FAILED(result) || std::wcscmp(clock.data(), L"07:11") != 0 || std::wcscmp(day.data(), L"Today") != 0)
    {
        std::wprintf(L"Weather today clock/day format mismatch: %s %s\n", clock.data(), day.data());
        shutdown();
        return FAILED(result) ? result : kTestFailure;
    }

    // Every season must round-trip in the active zone, even when its UTC bias differs from today's.
    // The fixed civil hour avoids ambiguous or nonexistent times at daylight-saving transitions.
    for (WORD month = 1; month <= 12; ++month)
    {
        SYSTEMTIME seasonalLocal = local;
        seasonalLocal.wMonth = month;
        SYSTEMTIME seasonalUtc{};
        FILETIME seasonalFileTime{};
        if (TzSpecificLocalTimeToSystemTimeEx(nullptr, &seasonalLocal, &seasonalUtc) == FALSE ||
            SystemTimeToFileTime(&seasonalUtc, &seasonalFileTime) == FALSE)
        {
            shutdown();
            return HRESULT_FROM_WIN32(GetLastError());
        }
        const uint64_t seasonalTime = (static_cast<uint64_t>(seasonalFileTime.dwHighDateTime) << 32) |
                                      static_cast<uint64_t>(seasonalFileTime.dwLowDateTime);
        result = formatClockDay(seasonalTime, seasonalTime, clock.data(), static_cast<uint32_t>(clock.size()),
                                day.data(), static_cast<uint32_t>(day.size()));
        if (FAILED(result) || std::wcscmp(clock.data(), L"07:11") != 0 || std::wcscmp(day.data(), L"Today") != 0)
        {
            std::wprintf(L"Weather seasonal clock/day mismatch in month %u: %s %s\n", month, clock.data(), day.data());
            shutdown();
            return FAILED(result) ? result : kTestFailure;
        }
    }

    constexpr uint64_t kDay = 864000000000ULL;
    std::array<wchar_t, 32> tomorrow{};
    result = formatClockDay(fileTime + kDay, fileTime, clock.data(), static_cast<uint32_t>(clock.size()),
                            tomorrow.data(), static_cast<uint32_t>(tomorrow.size()));
    if (FAILED(result) || std::wcscmp(tomorrow.data(), L"Tomorrow") != 0)
    {
        std::wprintf(L"Weather tomorrow label mismatch: %s\n", tomorrow.data());
        shutdown();
        return FAILED(result) ? result : kTestFailure;
    }

    std::array<wchar_t, 32> weekday{};
    result = formatClockDay(fileTime, fileTime - kDay * 2, clock.data(), static_cast<uint32_t>(clock.size()),
                            weekday.data(), static_cast<uint32_t>(weekday.size()));
    if (FAILED(result) || std::wcscmp(weekday.data(), L"Friday, 4") != 0)
    {
        std::wprintf(L"Weather weekday label mismatch: %s\n", weekday.data());
        shutdown();
        return FAILED(result) ? result : kTestFailure;
    }

    uint64_t offsetTime = 0;
    uint64_t zuluTime = 0;
    result = parseIso("2026-09-04T07:11+02:00", &offsetTime);
    const HRESULT zulu = parseIso("2026-09-04T05:11:00Z", &zuluTime);
    if (FAILED(result) || FAILED(zulu) || offsetTime == 0 || offsetTime != zuluTime)
    {
        std::wprintf(L"Weather ISO-8601 offset parse mismatch.\n");
        shutdown();
        return FAILED(result) ? result : kTestFailure;
    }

    // The active zone above may observe no daylight saving at all (hosted runners use UTC). Two fixed zones with
    // opposite seasons prove each timestamp's own rule through the production conversion: both minutes of every 2026
    // transition, and a Sydney local day that differs from the UTC day.
    const auto setTimeZone =
        Resolve<decltype(&RedXeWeatherSetTestTimeZone)>(module.get(), kWeatherSetTestTimeZoneExport);
    if (!setTimeZone)
    {
        shutdown();
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    struct ZoneCase final
    {
        const wchar_t* zone;
        const char* time;
        const char* now;
        const wchar_t* clock;
        const wchar_t* day;
    };
    static constexpr wchar_t kEurope[] = L"W. Europe Standard Time";
    static constexpr wchar_t kSydney[] = L"AUS Eastern Standard Time";
    constexpr ZoneCase zoneCases[]{
        {kEurope, "2026-01-04T06:11:00Z", "2026-01-04T06:11:00Z", L"07:11", L"Today"},
        {kEurope, "2026-07-04T05:11:00Z", "2026-07-04T05:11:00Z", L"07:11", L"Today"},
        {kEurope, "2026-03-29T00:59:00Z", "2026-03-29T00:59:00Z", L"01:59", L"Today"},
        {kEurope, "2026-03-29T01:00:00Z", "2026-03-29T01:00:00Z", L"03:00", L"Today"},
        {kEurope, "2026-10-25T00:59:00Z", "2026-10-25T00:59:00Z", L"02:59", L"Today"},
        {kEurope, "2026-10-25T01:00:00Z", "2026-10-25T01:00:00Z", L"02:00", L"Today"},
        {kSydney, "2026-01-04T20:11:00Z", "2026-01-04T20:11:00Z", L"07:11", L"Today"},
        {kSydney, "2026-07-04T21:11:00Z", "2026-07-04T21:11:00Z", L"07:11", L"Today"},
        {kSydney, "2026-04-04T15:59:00Z", "2026-04-04T15:59:00Z", L"02:59", L"Today"},
        {kSydney, "2026-04-04T16:00:00Z", "2026-04-04T16:00:00Z", L"02:00", L"Today"},
        {kSydney, "2026-10-03T15:59:00Z", "2026-10-03T15:59:00Z", L"01:59", L"Today"},
        {kSydney, "2026-10-03T16:00:00Z", "2026-10-03T16:00:00Z", L"03:00", L"Today"},
        // 00:30 and 23:00 on 5 January in Sydney fall on different UTC days; the label follows the local day.
        {kSydney, "2026-01-05T12:00:00Z", "2026-01-04T13:30:00Z", L"23:00", L"Today"},
        {kSydney, "2026-01-05T13:30:00Z", "2026-01-04T13:30:00Z", L"00:30", L"Tomorrow"},
    };
    for (const ZoneCase& zoneCase : zoneCases)
    {
        uint64_t time = 0;
        uint64_t now = 0;
        result = setTimeZone(zoneCase.zone);
        if (SUCCEEDED(result))
            result = parseIso(zoneCase.time, &time);
        if (SUCCEEDED(result))
            result = parseIso(zoneCase.now, &now);
        if (SUCCEEDED(result))
            result = formatClockDay(time, now, clock.data(), static_cast<uint32_t>(clock.size()), day.data(),
                                    static_cast<uint32_t>(day.size()));
        if (FAILED(result) || std::wcscmp(clock.data(), zoneCase.clock) != 0 ||
            std::wcscmp(day.data(), zoneCase.day) != 0)
        {
            std::wprintf(L"Weather %s clock/day mismatch at %hs: %s %s, expected %s %s\n", zoneCase.zone, zoneCase.time,
                         clock.data(), day.data(), zoneCase.clock, zoneCase.day);
            (void)setTimeZone(nullptr);
            shutdown();
            return FAILED(result) ? result : kTestFailure;
        }
    }
    if (setTimeZone(L"No Such Time Zone") != HRESULT_FROM_WIN32(ERROR_NOT_FOUND) || FAILED(setTimeZone(nullptr)))
    {
        std::wprintf(L"Weather test time zone did not refuse an unknown zone or restore the active one.\n");
        (void)setTimeZone(nullptr);
        shutdown();
        return kTestFailure;
    }

    result = RunWeatherHttpCacheTests(module.get());
    if (SUCCEEDED(result))
        result = RunWeatherRegressionTests(module.get());
    shutdown();
    return result;
}
} // namespace

int wmain() noexcept
{
    RedXeFailureReports::RouteAwayFromDialogs();
    const HRESULT result = Run();
    if (FAILED(result))
    {
        std::wprintf(L"Weather tests failed: 0x%08X\n", static_cast<unsigned int>(result));
        return 1;
    }
    std::wprintf(L"Weather HTTP, unit, and label format tests passed.\n");
    return 0;
}
