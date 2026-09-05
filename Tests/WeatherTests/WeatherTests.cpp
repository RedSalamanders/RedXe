#include "../../Plugins/Weather/WeatherHttp.h"
#include "../../Plugins/Weather/WeatherTestContract.h"
#include "PlugInterfaces/Factory.h"

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

namespace
{
constexpr HRESULT kTestFailure = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
constexpr uint32_t kHttpStackProbeBytes = 192U * 1024U;

static_assert(sizeof(WeatherHttpResponse) <= kWeatherHttpResponseMaximumBytes);
static_assert(sizeof(WeatherHttpResponse) < kWeatherMaximumBodyBytes);

template <typename Function> [[nodiscard]] Function Resolve(HMODULE module, const char* name) noexcept
{
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
        const std::string expected =
            std::string("https://nominatim.openstreetmap.org/search?q=") + item.encoded + "&format=json&limit=1";
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

    shutdown();
    return S_OK;
}
} // namespace

int wmain() noexcept
{
    const HRESULT result = Run();
    if (FAILED(result))
    {
        std::wprintf(L"Weather tests failed: 0x%08X\n", static_cast<unsigned int>(result));
        return 1;
    }
    std::wprintf(L"Weather HTTP, unit, and label format tests passed.\n");
    return 0;
}
