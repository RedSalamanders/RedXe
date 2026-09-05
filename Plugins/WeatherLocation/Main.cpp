#include "LocationPolicy.h"

#include <chrono>
#include <cstdio>
#include <cwchar>
#include <locationapi.h>
#include <windows.h>

// Projected objects and legacy COM providers exist only in this disposable helper.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Windows.Foundation.h>
#pragma warning(pop)

namespace
{
uint64_t Timestamp(const FILETIME& value) noexcept
{
    return (static_cast<uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}

// The deprecated API is intentionally a best-effort fallback when the preferred WinRT API cannot provide a fix.
#pragma warning(push)
#pragma warning(disable : 4995)
HRESULT ReadCachedReport(WeatherLocationPosition& position) noexcept
{
    wil::com_ptr_nothrow<ILocation> location;
    RETURN_IF_FAILED(CoCreateInstance(__uuidof(Location), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(location.put())));
    LOCATION_REPORT_STATUS status = REPORT_NOT_SUPPORTED;
    RETURN_IF_FAILED(location->GetReportStatus(__uuidof(ILatLongReport), &status));
    if (status == REPORT_ACCESS_DENIED)
        return E_ACCESSDENIED;
    // GetReport can return a recent cached fix even while the provider is temporarily unavailable.
    wil::com_ptr_nothrow<ILocationReport> report;
    RETURN_IF_FAILED(location->GetReport(__uuidof(ILatLongReport), report.put()));
    wil::com_ptr_nothrow<ILatLongReport> coordinates;
    RETURN_IF_FAILED(report.query_to(coordinates.put()));
    RETURN_IF_FAILED(coordinates->GetLatitude(&position.latitude));
    RETURN_IF_FAILED(coordinates->GetLongitude(&position.longitude));
    RETURN_IF_FAILED(coordinates->GetErrorRadius(&position.accuracyMeters));
    SYSTEMTIME utc{};
    FILETIME time{};
    RETURN_IF_FAILED(report->GetTimestamp(&utc));
    if (!SystemTimeToFileTime(&utc, &time))
        return HRESULT_FROM_WIN32(GetLastError());
    position.timestamp = Timestamp(time);
    return S_OK;
}

#pragma warning(pop)

HRESULT ReadWindowsPosition(void*, WeatherLocationSource source, WeatherLocationPosition& position) noexcept
{
    // Catch each attempt independently: activation/access/acquisition failure must not skip other providers.
    try
    {
        using namespace winrt::Windows::Devices::Geolocation;
        if (source == WeatherLocationSource::CachedReport)
            return ReadCachedReport(position);
        if (source == WeatherLocationSource::WindowsDefault)
        {
            const auto saved = Geolocator::DefaultGeoposition();
            if (!saved)
                return HRESULT_FROM_WIN32(ERROR_NO_DATA);
            const auto value = saved.Value();
            position.latitude = value.Latitude;
            position.longitude = value.Longitude;
            return S_OK;
        }
        const Geolocator locator;
        if (source == WeatherLocationSource::Coarse)
            locator.AllowFallbackToConsentlessPositions(); // Windows still enforces the system-wide location switch.
        const auto timeout = std::chrono::seconds(source == WeatherLocationSource::Current ? 8 : 4);
        const auto operation = locator.GetGeopositionAsync(std::chrono::minutes(5), timeout);
        if (operation.wait_for(timeout) == winrt::Windows::Foundation::AsyncStatus::Started)
        {
            operation.Cancel();
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        const auto result = operation.get();
        const auto coordinate = result.Coordinate();
        position.latitude = coordinate.Latitude();
        position.longitude = coordinate.Longitude();
        position.accuracyMeters = coordinate.Accuracy();
        const auto time = coordinate.Timestamp().time_since_epoch().count();
        if (time <= 0)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        position.timestamp = static_cast<uint64_t>(time);
        return S_OK;
    }
    catch (...)
    {
        return winrt::to_hresult();
    }
}

struct TestProvider
{
    WeatherLocationSource successfulSource = WeatherLocationSource::Current;
    uint64_t now = 0;
    bool unavailable = false;
};

HRESULT ReadTestPosition(void* context, WeatherLocationSource source, WeatherLocationPosition& position) noexcept
{
    const auto& test = *static_cast<const TestProvider*>(context);
    if (test.unavailable || source != test.successfulSource)
        return source == WeatherLocationSource::Current ? E_ACCESSDENIED : HRESULT_FROM_WIN32(ERROR_NO_DATA);
    position = {48.8566, 2.3522, 100.0, test.now};
    return S_OK;
}
} // namespace

int wmain(int argc, wchar_t** argv) noexcept
{
    if (argc == 2 && std::wcscmp(argv[1], L"--test-wait") == 0)
    {
        wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        if (event)
            (void)WaitForSingleObject(event.get(), 30000);
        return 1;
    }
    try
    {
        FILETIME time{};
        GetSystemTimeAsFileTime(&time);
        const uint64_t now = Timestamp(time);
        WeatherLocationPosition position{};
        HRESULT result = E_FAIL;
        if (argc == 1)
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            const auto apartment = wil::scope_exit([]() noexcept { winrt::uninit_apartment(); });
            result = WeatherResolveLocation(ReadWindowsPosition, nullptr, now, position);
        }
        else if (argc == 2)
        {
            TestProvider test{WeatherLocationSource::Current, now};
            if (std::wcscmp(argv[1], L"--test-coarse") == 0)
                test.successfulSource = WeatherLocationSource::Coarse;
            else if (std::wcscmp(argv[1], L"--test-cached") == 0)
                test.successfulSource = WeatherLocationSource::CachedReport;
            else if (std::wcscmp(argv[1], L"--test-default") == 0)
                test.successfulSource = WeatherLocationSource::WindowsDefault;
            else if (std::wcscmp(argv[1], L"--test-unavailable") == 0)
                test.unavailable = true;
            else if (std::wcscmp(argv[1], L"--test-result") != 0)
                return 1;
            result = WeatherResolveLocation(ReadTestPosition, &test, now, position);
        }
        if (FAILED(result))
            return 1;
        char text[96]{};
        const int length = sprintf_s(text, "%.6f,%.6f", position.latitude, position.longitude);
        DWORD written = 0;
        return length > 0 &&
                       WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, static_cast<DWORD>(length), &written,
                                 nullptr) &&
                       written == static_cast<DWORD>(length)
                   ? 0
                   : 1;
    }
    catch (...)
    {
        return 1;
    }
}
