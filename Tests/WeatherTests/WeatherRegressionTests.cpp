#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Widget.h"
#include "WeatherGpu.h"
#include "WeatherIcons.h"
#include "WeatherModel.h"
#include "WeatherTestContract.h"

#include <atomic>
#include <cstdio>
#include <d3d11.h>
#include <iterator>
#include <string>
#include <thread>
#include <wincodec.h>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr HRESULT kFailure = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
constexpr uint64_t kHour = 36000000000ULL;
std::string gForecast;
std::string gPlace = R"({"lat":"48.8566","lon":"2.3522","address":{"city":"Paris","country_code":"fr"}})";
uint32_t gGeocodes = 0;
uint32_t gForecastRequests = 0;
bool gFailForecast = false;
DWORD gRenderThread = 0;
uint32_t gAllocations = 0;

#if defined(_DEBUG)
int __cdecl CountAllocation(int type, void*, size_t, int, long, const unsigned char*, int)
{
    if (type != _HOOK_FREE && GetCurrentThreadId() == gRenderThread)
        ++gAllocations;
    return TRUE;
}
#endif

#define CHECK(expression)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::printf("Weather regression failed at line %d: %s\n", __LINE__, #expression);                          \
            return kFailure;                                                                                           \
        }                                                                                                              \
    } while (false)

template <typename T> T Resolve(HMODULE module, const char* name) noexcept
{
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

std::string Forecast(uint64_t now, bool hourly = true)
{
    std::string json = R"({"properties":{"timeseries":[)";
    for (uint32_t i = 0; i < 9 * 24; ++i)
    {
        const uint64_t instant = now + i * kHour;
        FILETIME time{static_cast<DWORD>(instant), static_cast<DWORD>(instant >> 32)};
        SYSTEMTIME utc{};
        (void)FileTimeToSystemTime(&time, &utc);
        char entry[512]{};
        const bool wet = i >= 3 && i < 6;
        sprintf_s(
            entry,
            R"(%s{"time":"%04u-%02u-%02uT%02u:%02u:%02uZ","data":{"instant":{"details":{"air_temperature":%u,"wind_speed":2.8}},"%s":{"summary":{"symbol_code":"%s"},"details":{"precipitation_amount":%.1f}}}})",
            i ? "," : "", utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond, 18 + i % 7,
            hourly ? "next_1_hours" : "next_6_hours", wet ? "rain" : "partlycloudy_day", wet ? 1.4 : 0.0);
        json += entry;
    }
    return json + "]}}";
}

HRESULT __stdcall Response(const char* url, uint32_t length, const char** body, uint32_t* bytes) noexcept
{
    const std::string_view request(url, length);
    const std::string* text = nullptr;
    if (request.find("nominatim") != std::string_view::npos)
    {
        ++gGeocodes;
        text = &gPlace;
    }
    else if (request.find("locationforecast") != std::string_view::npos)
    {
        ++gForecastRequests;
        if (gFailForecast)
            return E_FAIL;
        text = &gForecast;
    }
    if (!text)
        return E_FAIL; // Optional sun/authority requests are intentionally absent in this path.
    *body = text->data();
    *bytes = static_cast<uint32_t>(text->size());
    return S_OK;
}

class TestHost final : public IRedXeHost, public IRedXeSettingsQueue
{
    HRESULT STDMETHODCALLTYPE QueueControlWork(IRedXeControlWork*) noexcept override
    {
        return E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE RequestHostAction(const RedXeHostActionRequest*) noexcept override
    {
        return E_NOTIMPL;
    }

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) noexcept override
    {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (iid == __uuidof(IRedXeSettingsQueue))
            *object = static_cast<IRedXeSettingsQueue*>(this);
        else if (iid == IID_IUnknown || iid == __uuidof(IRedXeHost))
            *object = static_cast<IRedXeHost*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return 1;
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        return 1;
    }
    HRESULT STDMETHODCALLTYPE GetDataProvider(const char*, IRedXeDataProvider** p) noexcept override
    {
        if (p)
            *p = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE RequestFrame() noexcept override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReportWidgetStatus(const char*, const RedXeWidgetStatusReport*) noexcept override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PersistWidgetSettings(const char*, const char* json, uint32_t bytes) noexcept override
    {
        ++persistCalls;
        wrongThread = GetCurrentThreadId() != uiThread;
        if (failPersist)
            return E_ACCESSDENIED;
        if (bytes >= persisted.size())
            return E_INVALIDARG;
        std::memcpy(persisted.data(), json, bytes);
        persisted[bytes] = '\0';
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Log(const RedXeLogRecord*) noexcept override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueueWidgetSettings(const char*, const char* json, uint32_t bytes) noexcept override
    {
        if (bytes >= pending.size())
            return E_INVALIDARG;
        std::memcpy(pending.data(), json, bytes);
        pending[bytes] = '\0';
        pendingBytes = bytes;
        return S_OK;
    }
    void Drain() noexcept
    {
        if (pendingBytes)
            (void)PersistWidgetSettings("weather.regression", pending.data(), pendingBytes);
        pendingBytes = 0;
    }
    DWORD uiThread = GetCurrentThreadId();
    uint32_t persistCalls = 0;
    bool wrongThread = false;
    bool failPersist = false;
    std::array<char, 1024> persisted{};
    std::array<char, 1024> pending{};
    uint32_t pendingBytes = 0;
};

HRESULT Create(RedXeCreateFn create, TestHost& host, const char* json, wil::com_ptr_nothrow<IRedXeWidget>& widget)
{
    const RedXeFactoryOptions options{sizeof(options), 0, json, static_cast<uint32_t>(std::strlen(json))};
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    CHECK(create(__uuidof(IRedXeWidgetProvider), &options, &host, "builtin.weather", provider.put_void()) == S_OK);
    return provider->CreateWidget("weather", "weather.regression", widget.put());
}

HRESULT ModelTests(uint64_t now)
{
    WeatherSnapshot snapshot{};
    CHECK(WeatherParseLocationForecast(gForecast, snapshot) == S_OK);
    CHECK(snapshot.hourlyCount == 24 && snapshot.dailyCount == 9);
    CHECK(snapshot.hourly[3].hasPrecipitation && snapshot.hourly[3].precipitationMillimeters > 1.3f);
    CHECK(WeatherSameLocalDay(now, snapshot.daily[0].dayFileTime100ns));
    CHECK(!WeatherSameLocalDay(now, snapshot.daily[1].dayFileTime100ns));
    WeatherSnapshot midnight{};
    CHECK(WeatherParseLocationForecast(Forecast(now + 11 * kHour + kHour / 2), midnight) == S_OK);
    CHECK(midnight.daily[0].minimumCelsius == 18.0f && midnight.daily[0].maximumCelsius == 18.0f);
    CHECK(!WeatherSameLocalDay(midnight.daily[0].dayFileTime100ns, midnight.daily[1].dayFileTime100ns));
    CHECK(FAILED(WeatherParseNominatim(R"({"lat":"91","lon":"2"})", midnight)));
    CHECK(FAILED(WeatherParseNominatim(R"({"lat":"48oops","lon":"2"})", midnight)));
    std::array<wchar_t, 96> notice{};
    CHECK(WeatherFormatPrecipitationNotice(snapshot, now, notice.data(), 96) > 0);
    CHECK(std::wcsstr(notice.data(), L"Rain expected around") != nullptr);
    uint32_t noticeHour = 0;
    CHECK(WeatherSelectPrecipitationNotice(snapshot, now, noticeHour) && noticeHour == 3);
    snapshot.hourly[3].condition = WeatherCondition::Snow;
    CHECK(WeatherFormatPrecipitationNotice(snapshot, now, notice.data(), 96) > 0);
    CHECK(std::wcsstr(notice.data(), L"Snow expected around") != nullptr);
    snapshot.hourly[3].condition = WeatherCondition::Sleet;
    CHECK(WeatherFormatPrecipitationNotice(snapshot, now + 3 * kHour, notice.data(), 96) > 0);
    CHECK(std::wcsstr(notice.data(), L"Rain / snow forecast this hour") != nullptr);
    CHECK(WeatherFormatPrecipitationNotice(snapshot, now + 6 * kHour, notice.data(), 96) == 0);
    snapshot.stale = true;
    CHECK(WeatherFormatPrecipitationNotice(snapshot, now, notice.data(), 96) == 0);
    snapshot.stale = false;
    snapshot.hourlyCount = 1;
    snapshot.hourly[0].condition = WeatherCondition::Rain;
    snapshot.hourly[0].hasPrecipitation = true;
    snapshot.hourly[0].precipitationMillimeters = 0.0f;
    CHECK(WeatherFormatPrecipitationNotice(snapshot, now, notice.data(), 96) == 0);
    snapshot.hourly[0].hasPrecipitation = false;
    CHECK(WeatherFormatPrecipitationNotice(snapshot, now, notice.data(), 96) != 0);
    snapshot.hourly[0].timeFileTime100ns = now + 13 * kHour;
    CHECK(WeatherFormatPrecipitationNotice(snapshot, now, notice.data(), 96) == 0);
    CHECK(WeatherParseLocationForecast(Forecast(now, false), snapshot) == S_OK && snapshot.hourlyCount == 0);
    CHECK(WeatherFormatPrecipitationNotice(snapshot, now, notice.data(), 96) == 0);

    snapshot.hasCoordinates = true;
    snapshot.latitude = 48.8566;
    snapshot.longitude = 2.3522;
    WeatherCopyWide(L"Paris \"Centre\"", snapshot.locationName.data(), snapshot.locationName.size());
    WeatherCopyNarrow("FR", snapshot.countryCode.data(), snapshot.countryCode.size());
    std::array<char, 1024> json{};
    uint32_t written = 0;
    CHECK(WeatherWriteLocationSettings(snapshot, json.data(), 1024, written) == S_OK && written > 0);
    WeatherConfiguration config{};
    CHECK(WeatherReadConfiguration(json.data(), config) == S_OK);
    CHECK(std::strcmp(config.location.data(), "Paris \"Centre\", FR") == 0);
    CHECK(WeatherWriteLocationSettings(snapshot, json.data(), 2, written) ==
              HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) &&
          written == 0);
    snapshot.locationName[0] = L'\0';
    CHECK(WeatherWriteLocationSettings(snapshot, json.data(), 1024, written) == S_OK);
    CHECK(WeatherReadConfiguration(json.data(), config) == S_OK);
    double lat = 0, lon = 0;
    CHECK(WeatherParseLatLon(config.location.data(), lat, lon) && lat == snapshot.latitude &&
          lon == snapshot.longitude);
    return S_OK;
}

HRESULT SavePng(ID3D11Device& device, ID3D11DeviceContext& context, ID3D11Texture2D& source, const wchar_t* name)
{
    D3D11_TEXTURE2D_DESC desc{};
    source.GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    CHECK(SUCCEEDED(device.CreateTexture2D(&desc, nullptr, staging.put())));
    context.CopyResource(staging.get(), &source);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    CHECK(SUCCEEDED(context.Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)));
    const auto unmap = wil::scope_exit([&]() noexcept { context.Unmap(staging.get(), 0); });
    uint32_t lit = 0;
    for (uint32_t y = 0; y < desc.Height; ++y)
        for (uint32_t x = 0; x < desc.Width; ++x)
            if (static_cast<const uint8_t*>(mapped.pData)[y * mapped.RowPitch + x * 4 + 1] > 80)
                ++lit;
    CHECK(lit > 50); // Real pixel readback; blank shader output cannot pass layout-only checks.
    if (!name)
        return S_OK;
    std::array<wchar_t, 1024> path{};
    CHECK(GetModuleFileNameW(nullptr, path.data(), 1024) != 0);
    wchar_t* filename = std::wcsrchr(path.data(), L'\\');
    CHECK(filename != nullptr);
    CHECK(wcscpy_s(filename + 1, path.size() - static_cast<size_t>(filename + 1 - path.data()), name) == 0);
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    CHECK(SUCCEEDED(
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()))));
    wil::com_ptr_nothrow<IWICStream> stream;
    CHECK(SUCCEEDED(factory->CreateStream(stream.put())));
    CHECK(SUCCEEDED(stream->InitializeFromFilename(path.data(), GENERIC_WRITE)));
    wil::com_ptr_nothrow<IWICBitmapEncoder> encoder;
    CHECK(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())));
    CHECK(SUCCEEDED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)));
    wil::com_ptr_nothrow<IWICBitmapFrameEncode> frame;
    CHECK(SUCCEEDED(encoder->CreateNewFrame(frame.put(), nullptr)));
    CHECK(SUCCEEDED(frame->Initialize(nullptr)));
    CHECK(SUCCEEDED(frame->SetSize(desc.Width, desc.Height)));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    CHECK(SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGRA);
    CHECK(SUCCEEDED(frame->WritePixels(desc.Height, mapped.RowPitch, mapped.RowPitch * desc.Height,
                                       static_cast<BYTE*>(mapped.pData))));
    CHECK(SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()));
    return S_OK;
}

// Country routing, every keyless alert document parser, severity tiers, and the one-URL-per-region builder. Offline:
// the fixtures are trimmed live responses captured on 2026-09-16.
HRESULT AlertProviderTests()
{
    struct Route
    {
        const char* country;
        WeatherAlertRegion region;
    };
    constexpr Route routes[]{{"FR", WeatherAlertRegion::Europe},       {"gb", WeatherAlertRegion::Europe},
                             {"UK", WeatherAlertRegion::Europe},       {"NO", WeatherAlertRegion::Europe},
                             {"US", WeatherAlertRegion::UnitedStates}, {"pr", WeatherAlertRegion::UnitedStates},
                             {"CA", WeatherAlertRegion::Canada},       {"ca", WeatherAlertRegion::Canada},
                             {"HK", WeatherAlertRegion::HongKong},     {"JP", WeatherAlertRegion::None},
                             {"AU", WeatherAlertRegion::None},         {"BR", WeatherAlertRegion::None},
                             {"ZA", WeatherAlertRegion::None},         {"", WeatherAlertRegion::None}};
    for (const auto& route : routes)
        CHECK(WeatherAlertRegionFromCountry(route.country) == route.region);

    const auto parse = [](WeatherAlertRegion region, std::string_view json, WeatherSnapshot& out) noexcept
    {
        out = WeatherSnapshot{};
        return WeatherParseAlerts(region, json, out);
    };
    WeatherSnapshot snapshot{};

    // Environment Canada: a statement is yellow, a warning orange, a tornado warning red, a risk colour wins, ended
    // and hidden features are skipped, and the lower-case feed name gains a capital.
    constexpr char canada[] =
        R"({"type":"FeatureCollection","name":"Current-Alerts","features":[)"
        R"({"type":"Feature","properties":{"id":"1","alert_code":"SPS","alert_type":"statement","alert_name_en":"special weather statement","alert_text_en":"A fall storm featuring strong winds is expected.","publication_datetime":"2026-09-16T18:55:06.911Z","expiration_datetime":"2026-09-17T10:55:06.911Z","validity_datetime":"2026-09-19T09:00:00.000Z","event_end_datetime":"2026-09-20T06:00:00.000Z","feature_name_en":"St. John's and vicinity","province":"NL","status_en":"issued","display_status":"visible","risk_colour_en":""},"geometry":{"type":"Polygon","coordinates":[[[-52.9,47.4],[-52.5,47.4],[-52.5,47.8],[-52.9,47.4]]]}},)"
        R"({"type":"Feature","properties":{"id":"2","alert_code":"WND","alert_type":"warning","alert_name_en":"wind warning","status_en":"issued","display_status":"visible","risk_colour_en":""}},)"
        R"({"type":"Feature","properties":{"id":"3","alert_code":"TO","alert_type":"warning","alert_name_en":"tornado warning","status_en":"ended","display_status":"visible"}},)"
        R"({"type":"Feature","properties":{"id":"4","alert_code":"HW","alert_type":"warning","alert_name_en":"heat warning","status_en":"issued","display_status":"hidden"}}]})";
    CHECK(parse(WeatherAlertRegion::Canada, canada, snapshot) == S_OK);
    CHECK(snapshot.alertCount == 2 && snapshot.alertProvider == WeatherAlertProvider::EnvironmentCanada);
    CHECK(std::wcscmp(snapshot.alerts[0].title.data(), L"Special weather statement") == 0);
    CHECK(snapshot.alerts[0].severity == WeatherAlertSeverity::Yellow);
    CHECK(std::wcscmp(snapshot.alerts[0].authority.data(), L"Environment Canada") == 0);
    CHECK(snapshot.alerts[0].startFileTime100ns != 0 && snapshot.alerts[0].expiryFileTime100ns != 0);
    CHECK(std::wcscmp(snapshot.alerts[1].title.data(), L"Wind warning") == 0);
    CHECK(snapshot.alerts[1].severity == WeatherAlertSeverity::Orange);
    CHECK(WeatherHighestAlertSeverity(snapshot) == WeatherAlertSeverity::Orange);
    CHECK(
        parse(
            WeatherAlertRegion::Canada,
            R"({"features":[{"properties":{"alert_type":"warning","alert_name_en":"tornado warning","status_en":"issued"}}]})",
            snapshot) == S_OK &&
        snapshot.alertCount == 1 && snapshot.alerts[0].severity == WeatherAlertSeverity::Red);
    CHECK(
        parse(
            WeatherAlertRegion::Canada,
            R"({"features":[{"properties":{"alert_type":"advisory","alert_name_en":"fog advisory","risk_colour_en":"Red"}}]})",
            snapshot) == S_OK &&
        snapshot.alertCount == 1 && snapshot.alerts[0].severity == WeatherAlertSeverity::Red);
    CHECK(parse(WeatherAlertRegion::Canada, R"({"type":"FeatureCollection","features":[]})", snapshot) == S_OK &&
          snapshot.alertCount == 0 && snapshot.alertProvider == WeatherAlertProvider::EnvironmentCanada);
    CHECK(parse(WeatherAlertRegion::Canada, R"(<ServiceExceptionReport/>)", snapshot) == S_FALSE &&
          snapshot.alertCount == 0);

    // Hong Kong Observatory: statements in force keyed by code; the tier comes from the code, cancellations drop.
    constexpr char hongKong[] =
        R"({"WRAIN":{"name":"Rainstorm Warning Signal","code":"WRAINB","actionCode":"ISSUE","issueTime":"2026-09-16T11:45:00+08:00","expireTime":"2026-09-16T14:00:00+08:00"},)"
        R"("WTCSGNL":{"name":"Tropical Cyclone Warning Signal","code":"TC8NE","actionCode":"ISSUE","issueTime":"2026-09-16T09:40:00+08:00","type":"No. 8 Northeast Gale or Storm Signal"},)"
        R"("WFROST":{"name":"Frost Warning","code":"WFROST","actionCode":"CANCEL","issueTime":"2026-09-16T06:00:00+08:00"},)"
        R"("WTS":{"name":"Thunderstorm Warning","code":"WTS","actionCode":"EXTEND","issueTime":"2026-09-16T10:00:00+08:00","expireTime":"2026-09-16T13:00:00+08:00"}})";
    CHECK(parse(WeatherAlertRegion::HongKong, hongKong, snapshot) == S_OK);
    CHECK(snapshot.alertCount == 3 && snapshot.alertProvider == WeatherAlertProvider::HongKongObservatory);
    CHECK(std::wcscmp(snapshot.alerts[0].title.data(), L"Black Rainstorm Warning") == 0);
    CHECK(snapshot.alerts[0].severity == WeatherAlertSeverity::Red);
    CHECK(std::wcscmp(snapshot.alerts[1].title.data(), L"Tropical Cyclone Signal No. 8") == 0);
    CHECK(snapshot.alerts[1].severity == WeatherAlertSeverity::Red);
    CHECK(std::wcscmp(snapshot.alerts[2].title.data(), L"Thunderstorm Warning") == 0);
    CHECK(snapshot.alerts[2].severity == WeatherAlertSeverity::Yellow);
    CHECK(std::wcscmp(snapshot.alerts[2].authority.data(), L"Hong Kong Observatory") == 0);
    uint64_t issued = 0;
    CHECK(WeatherParseIso8601Utc("2026-09-16T11:45:00+08:00", issued) &&
          snapshot.alerts[0].startFileTime100ns == issued);
    CHECK(
        parse(
            WeatherAlertRegion::HongKong,
            R"({"WTCSGNL":{"name":"Tropical Cyclone Warning Signal","code":"TC3","actionCode":"ISSUE"},"WL":{"name":"Landslip Warning","code":"WL","actionCode":"ISSUE"},"WFIRE":{"name":"Fire Danger Warning","code":"WFIREY","actionCode":"ISSUE"}})",
            snapshot) == S_OK &&
        snapshot.alertCount == 3);
    CHECK(std::wcscmp(snapshot.alerts[0].title.data(), L"Tropical Cyclone Signal No. 3") == 0 &&
          snapshot.alerts[0].severity == WeatherAlertSeverity::Orange);
    CHECK(snapshot.alerts[1].severity == WeatherAlertSeverity::Orange);
    CHECK(std::wcscmp(snapshot.alerts[2].title.data(), L"Yellow Fire Danger Warning") == 0 &&
          snapshot.alerts[2].severity == WeatherAlertSeverity::Yellow);
    CHECK(parse(WeatherAlertRegion::HongKong, "{}", snapshot) == S_OK && snapshot.alertCount == 0 &&
          snapshot.alertProvider == WeatherAlertProvider::HongKongObservatory);
    CHECK(parse(WeatherAlertRegion::HongKong, "[]", snapshot) == S_FALSE && snapshot.alertCount == 0);

    // The shipped European and US parsers record their body too, and the awareness/severity vocabularies map.
    CHECK(
        parse(
            WeatherAlertRegion::Europe,
            R"({"warnings":[{"headline":"Red warning for rain","awareness_level":"4; red; Severe","onset":"2026-09-05T06:00:00Z","expires":"2026-09-06T06:00:00Z"},{"event":"Yellow warning for fog","awareness_level":"2; yellow; Moderate"}]})",
            snapshot) == S_OK);
    CHECK(snapshot.alertCount == 2 && snapshot.alertProvider == WeatherAlertProvider::MeteoAlarm);
    CHECK(snapshot.alerts[0].severity == WeatherAlertSeverity::Red &&
          snapshot.alerts[1].severity == WeatherAlertSeverity::Yellow);
    CHECK(std::wcscmp(snapshot.alerts[0].authority.data(), L"MeteoAlarm") == 0);
    CHECK(
        parse(
            WeatherAlertRegion::UnitedStates,
            R"({"features":[{"properties":{"event":"Tornado Warning","severity":"Extreme","senderName":"NWS Norman OK"}},{"properties":{"headline":"Heat Advisory","severity":"Moderate"}},{"properties":{"headline":"Special Weather Statement","severity":"Minor"}}]})",
            snapshot) == S_OK);
    CHECK(snapshot.alertCount == 3 && snapshot.alertProvider == WeatherAlertProvider::NationalWeatherService);
    CHECK(snapshot.alerts[0].severity == WeatherAlertSeverity::Red &&
          snapshot.alerts[1].severity == WeatherAlertSeverity::Orange &&
          snapshot.alerts[2].severity == WeatherAlertSeverity::Yellow);
    CHECK(std::wcscmp(snapshot.alerts[0].authority.data(), L"NWS Norman OK") == 0);
    CHECK(parse(WeatherAlertRegion::None, "{}", snapshot) == S_FALSE && snapshot.alertCount == 0 &&
          snapshot.alertProvider == WeatherAlertProvider::None);

    // Exactly one document URL per region; none for an unrouted country. No I/O.
    std::array<char, kWeatherMaximumUrlBytes> url{};
    CHECK(WeatherBuildAlertUrl(WeatherAlertRegion::Europe, "FR", 48.8566, 2.3522, url.data(),
                               static_cast<uint32_t>(url.size())) == S_OK);
    CHECK(std::strcmp(url.data(), "https://feeds.meteoalarm.org/api/v1/warnings/feeds-fr") == 0);
    CHECK(WeatherBuildAlertUrl(WeatherAlertRegion::UnitedStates, "US", 35.2226, -97.4395, url.data(),
                               static_cast<uint32_t>(url.size())) == S_OK);
    CHECK(std::strcmp(url.data(), "https://api.weather.gov/alerts/active?point=35.2226,-97.4395") == 0);
    CHECK(WeatherBuildAlertUrl(WeatherAlertRegion::Canada, "CA", 47.5615, -52.7126, url.data(),
                               static_cast<uint32_t>(url.size())) == S_OK);
    CHECK(std::strstr(url.data(), "https://geo.weather.gc.ca/geomet?") == url.data());
    CHECK(std::strstr(url.data(), "LAYERS=Current-Alerts&QUERY_LAYERS=Current-Alerts") != nullptr);
    CHECK(std::strstr(url.data(), "INFO_FORMAT=application/json") != nullptr);
    CHECK(std::strstr(url.data(), "BBOX=47.5515,-52.7226,47.5715,-52.7026") != nullptr);
    CHECK(std::strstr(url.data(), "FEATURE_COUNT=6") != nullptr);
    CHECK(WeatherBuildAlertUrl(WeatherAlertRegion::HongKong, "HK", 22.3, 114.17, url.data(),
                               static_cast<uint32_t>(url.size())) == S_OK);
    CHECK(std::strcmp(url.data(),
                      "https://data.weather.gov.hk/weatherAPI/opendata/weather.php?dataType=warnsum&lang=en") == 0);
    CHECK(WeatherBuildAlertUrl(WeatherAlertRegion::None, "JP", 35.68, 139.69, url.data(),
                               static_cast<uint32_t>(url.size())) == S_FALSE &&
          url[0] == '\0');
    CHECK(WeatherBuildAlertUrl(WeatherAlertRegion::Europe, "", 48.0, 2.0, url.data(),
                               static_cast<uint32_t>(url.size())) == S_FALSE);
    CHECK(WeatherBuildAlertUrl(WeatherAlertRegion::Canada, "CA", 47.0, -52.0, url.data(), 40) ==
          HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
    CHECK(std::wcscmp(WeatherAlertProviderName(WeatherAlertProvider::EnvironmentCanada), L"Environment Canada") == 0);
    CHECK(WeatherAlertProviderName(WeatherAlertProvider::None)[0] == L'\0');
    return S_OK;
}

HRESULT RenderTests(HMODULE module, RedXeCreateFn create, uint64_t now, bool stress = false)
{
    const auto fixture = Resolve<WeatherApplyTestSnapshotFn>(module, kWeatherApplyTestSnapshotExport);
    const auto diagnostics = Resolve<WeatherGetTestDiagnosticsFn>(module, kWeatherGetTestDiagnosticsExport);
    CHECK(fixture && diagnostics);
    const std::string sun =
        R"({"properties":{"sunrise":{"time":"2026-09-05T05:15:00Z"},"sunset":{"time":"2026-09-05T18:22:00Z"}}})";
    constexpr char longPlace[] =
        R"json({"lat":"48.8566","lon":"2.3522","address":{"city":"Saint-R\u00e9my-l\u00e8s-Chevreuse - \u00cele-de-France (Paris)","country_code":"fr"}})json";
    const char* place = stress ? longPlace : gPlace.data();
    WeatherTestSnapshot snapshot{sizeof(snapshot),
                                 gForecast.data(),
                                 static_cast<uint32_t>(gForecast.size()),
                                 sun.data(),
                                 static_cast<uint32_t>(sun.size()),
                                 place,
                                 static_cast<uint32_t>(std::strlen(place))};
    CHECK(fixture(&snapshot) == S_OK);
    TestHost host;
    wil::com_ptr_nothrow<IRedXeWidget> widget;
    CHECK(Create(create, host,
                 stress ? R"({"location":"Paris","temperatureUnit":"fahrenheit","windUnit":"mph"})"
                        : R"({"location":"Paris"})",
                 widget) == S_OK);
    wil::com_ptr_nothrow<IRedXeGpuWidget> gpu;
    wil::com_ptr_nothrow<IRedXeNetworkWidget> network;
    wil::com_ptr_nothrow<IRedXeRaisedWidget> raised;
    CHECK(SUCCEEDED(widget.query_to(gpu.put())) && SUCCEEDED(widget.query_to(network.put())) &&
          SUCCEEDED(widget.query_to(raised.put())));
    wil::unique_handle cancel{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    CHECK(cancel);
    uint32_t delay = 0;
    CHECK(network->RunNetworkWork(cancel.get(), &delay) == S_OK);
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr,
                                      0, D3D11_SDK_VERSION, device.put(), &level, context.put())));
    const RedXeGpuDeviceContext created{sizeof(created), device.get(), DXGI_FORMAT_B8G8R8A8_UNORM, level};
    CHECK(gpu->OnDeviceCreated(&created) == S_OK); // Atlas creation rejects every clipped glyph, including all icons.
    {
        // Every hero temperature glyph links its sharp 96 px twin cell, so the header never upscales 48 px text.
        WeatherTestDiagnostics atlas{sizeof(atlas)};
        CHECK(diagnostics(&atlas) == S_OK);
        CHECK(atlas.heroTwinCells == static_cast<uint32_t>(std::size(kWeatherHeroGlyphs)));
        // Every catalogued Weather Icons glyph owns its 48 px cell and 96 px twin, so no icon falls back to text.
        CHECK(atlas.iconCells == static_cast<uint32_t>(std::size(kWeatherIconGlyphs)));
    }
    const auto detach = wil::scope_exit(
        [&]() noexcept
        {
            (void)widget->SetVisible(FALSE);
            gpu->OnDeviceLost();
        });
    struct Size
    {
        uint32_t width, height;
        bool raised;
        const wchar_t* file;
        bool alert = false;
    };
    constexpr Size sizes[]{{766, 622, false, L"Weather-standard.png"},
                           {160, 72, false, L"Weather-tiny.png"},
                           {320, 180, false, L"Weather-compact.png"},
                           {280, 500, false, L"Weather-narrow.png"},
                           {510, 703, false, L"Weather-portrait.png"},
                           {1200, 800, true, L"Weather-raised.png"},
                           {400, 240, false, nullptr},
                           {960, 540, false, nullptr},
                           {766, 622, false, L"Weather-alert.png", true}};
    // An authority alert paints the severity banner with the storm-warning mark; applied last so the earlier layouts
    // keep the forecast notice alone.
    constexpr char alerts[] =
        R"({"warnings":[{"headline":"Orange warning for wind","awareness_level":"3; orange; Moderate","onset":"2026-09-05T06:00:00Z","expires":"2026-09-06T06:00:00Z"}]})";
    WeatherTestSnapshot alerted = snapshot;
    alerted.meteoAlarmJson = alerts;
    alerted.meteoAlarmBytes = static_cast<uint32_t>(std::strlen(alerts));
    for (const auto& size : sizes)
    {
        if (size.alert)
        {
            if (stress)
                continue;
            CHECK(fixture(&alerted) == S_OK);
            CHECK(network->RunNetworkWork(cancel.get(), &delay) == S_OK);
        }
        const RedXeGpuTargetSizeContext targetSize{sizeof(targetSize), size.width, size.height, 144};
        CHECK(gpu->OnTargetSizeChanged(&targetSize) == S_OK);
        CHECK(raised->SetRaised(size.raised) == S_OK);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = size.width;
        desc.Height = size.height;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        wil::com_ptr_nothrow<ID3D11Texture2D> texture;
        wil::com_ptr_nothrow<ID3D11RenderTargetView> view;
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture.put())));
        CHECK(SUCCEEDED(device->CreateRenderTargetView(texture.get(), nullptr, view.put())));
        const float clear[4]{0, 0, 0, 1};
        context->ClearRenderTargetView(view.get(), clear);
        ID3D11RenderTargetView* views[]{view.get()};
        context->OMSetRenderTargets(1, views, nullptr);
        D3D11_VIEWPORT viewport{0, 0, static_cast<float>(size.width), static_cast<float>(size.height), 0, 1};
        context->RSSetViewports(1, &viewport);
        const RedXeWidgetFrameContext widgetFrame{sizeof(widgetFrame), size.width, size.height, 144, 0, 0};
        const RedXeGpuFrameContext frame{sizeof(frame), &widgetFrame, context.get(), viewport};
        CHECK(gpu->Render(&frame) == S_OK);
#if defined(_DEBUG)
        const auto oldHook = _CrtSetAllocHook(CountAllocation);
        const auto restore = wil::scope_exit(
            [&]() noexcept
            {
                gRenderThread = 0;
                _CrtSetAllocHook(oldHook);
            });
#endif
        gAllocations = 0;
        gRenderThread = GetCurrentThreadId();
        const HRESULT drawn = gpu->Render(&frame);
        gRenderThread = 0;
        CHECK(drawn == S_OK && gAllocations == 0);
        WeatherTestDiagnostics info{sizeof(info)};
        CHECK(diagnostics(&info) == S_OK);
        std::printf("Weather %ux%u: hourly=%u daily=%u precipitation=%u overflow=%u\n", size.width, size.height,
                    info.hourlyDrawn, info.dailyDrawn, info.precipitationNotice, info.overflowingQuads);
        CHECK(info.overflowingQuads == 0);
        CHECK(SavePng(*device, *context, *texture,
                      stress ? (size.width == 766 ? L"Weather-long-location.png" : nullptr) : size.file) == S_OK);
        if (size.width >= 700 && size.height >= 500)
            CHECK(info.hourlyDrawn >= 8 && info.dailyDrawn >= 2 && info.precipitationNotice);
        if (size.width == 766 && !size.alert)
            CHECK(info.dailyDrawn >= 4);
        if (size.alert) // The footer names the body whose alerts are on screen.
            CHECK(info.alertCount == 1 && std::wcscmp(info.lastAttribution, L"MET Norway  |  MeteoAlarm") == 0);
        else if (size.width >= 700)
            CHECK(std::wcscmp(info.lastAttribution, L"MET Norway") == 0);
        if (size.width == 510) // Portrait tile: day rows use the leftover height instead of a fixed five-row cap.
            CHECK(info.hourlyDrawn == 6 && info.dailyDrawn >= 7);
        if (size.raised)
            CHECK(info.dailyDrawn == 8);
        if (size.width == 160)
            CHECK(info.hourlyDrawn == 0 && info.dailyDrawn == 0);
    }
    (void)now;
    return S_OK;
}

HRESULT LocationTests(HMODULE module, RedXeCreateFn create)
{
    const auto services = Resolve<decltype(&RedXeWeatherSetTestServices)>(module, "RedXeWeatherSetTestServices");
    const auto helper = Resolve<decltype(&RedXeWeatherTestLocationHelper)>(module, "RedXeWeatherTestLocationHelper");
    const auto diagnostics = Resolve<WeatherGetTestDiagnosticsFn>(module, kWeatherGetTestDiagnosticsExport);
    CHECK(services && helper && diagnostics);
    const auto clearServices = wil::scope_exit([&]() noexcept { services(nullptr, 0); });
    for (uint32_t mode = 0; mode < 6; ++mode)
    {
        services(Response, mode < 3 ? 1 : mode);
        const bool discover = mode == 1 || mode >= 3;
        TestHost host;
        wil::com_ptr_nothrow<IRedXeWidget> widget;
        CHECK(Create(create, host,
                     mode == 0  ? R"({"locationMode":"automatic","location":"Paris"})"
                     : discover ? R"({"locationMode":"manual","location":""})"
                                : R"({"location":"48.8566,2.3522"})",
                     widget) == S_OK);
        CHECK(widget->SetVisible(TRUE) == S_OK);
        wil::com_ptr_nothrow<IRedXeNetworkWidget> network;
        wil::com_ptr_nothrow<IRedXeScheduledWidget> scheduled;
        CHECK(SUCCEEDED(widget.query_to(network.put())) && SUCCEEDED(widget.query_to(scheduled.put())));
        wil::unique_handle cancel{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        CHECK(cancel);
        WeatherTestDiagnostics before{sizeof(before)}, after{sizeof(after)};
        CHECK(diagnostics(&before) == S_OK);
        gGeocodes = gForecastRequests = 0;
        HRESULT fetched = E_FAIL;
        uint32_t delay = 0;
        std::thread worker([&]() { fetched = network->RunNetworkWork(cancel.get(), &delay); });
        worker.join();
        CHECK(fetched == S_OK);
        CHECK(host.persistCalls == 0);
        CHECK(network->RunNetworkWork(cancel.get(), &delay) == S_OK);
        CHECK(gGeocodes == 1 && gForecastRequests == 2);
        CHECK(diagnostics(&after) == S_OK);
        CHECK(after.locationHelperRuns - before.locationHelperRuns == (discover ? 1U : 0U));
        // A failed refresh keeps the cached forecast on screen as Degraded instead of handing the tile to the host;
        // the next successful run clears it.
        gFailForecast = true;
        CHECK(FAILED(network->RunNetworkWork(cancel.get(), &delay)));
        gFailForecast = false;
        WeatherTestDiagnostics cached{sizeof(cached)};
        CHECK(diagnostics(&cached) == S_OK && cached.lastStatus == RedXeWidgetStatusDegraded &&
              cached.lastTemperatureCelsius == after.lastTemperatureCelsius);
        CHECK(network->RunNetworkWork(cancel.get(), &delay) == S_OK);
        CHECK(diagnostics(&cached) == S_OK && cached.lastStatus == RedXeWidgetStatusOk);
        host.failPersist = discover;
        host.Drain();
        CHECK(scheduled->GetNextFrameDelayMilliseconds(&delay) == S_OK);
        std::array<char, 1024> collected{};
        uint32_t written = 0;
        if (discover)
        {
            CHECK(widget->CollectPersistentSettings(collected.data(), 1024, &written) == S_OK && written > 0);
            CHECK(widget->CollectPersistentSettings(collected.data(), 2, &written) ==
                      HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) &&
                  written == 0);
            host.failPersist = false;
            CHECK(widget->CollectPersistentSettings(collected.data(), 1024, &written) == S_OK);
            CHECK(host.PersistWidgetSettings("weather.regression", collected.data(), written) == S_OK);
            CHECK(scheduled->GetNextFrameDelayMilliseconds(&delay) == S_OK && host.persistCalls == 2);
            CHECK(std::strstr(host.persisted.data(), "Paris, FR") != nullptr ||
                  std::strstr(host.persisted.data(), "Paris, fr") != nullptr);
            CHECK(!host.wrongThread);
            CHECK(scheduled->GetNextFrameDelayMilliseconds(&delay) == S_OK && host.persistCalls == 2);
            WeatherConfiguration saved{};
            CHECK(WeatherReadConfiguration(host.persisted.data(), saved) == S_OK && saved.location[0] != '\0');
            // A later widget instance must use the persisted city even if every discovery provider now fails.
            services(Response, 2);
            TestHost nextHost;
            wil::com_ptr_nothrow<IRedXeWidget> nextWidget;
            wil::com_ptr_nothrow<IRedXeNetworkWidget> nextNetwork;
            CHECK(Create(create, nextHost, host.persisted.data(), nextWidget) == S_OK);
            CHECK(SUCCEEDED(nextWidget.query_to(nextNetwork.put())));
            CHECK(nextNetwork->RunNetworkWork(cancel.get(), &delay) == S_OK);
            CHECK(diagnostics(&before) == S_OK && before.locationHelperRuns == after.locationHelperRuns);
            CHECK(nextHost.pendingBytes == 0);
        }
        else
            CHECK(widget->CollectPersistentSettings(collected.data(), 1024, &written) == S_FALSE && written == 0 &&
                  host.persistCalls == 0);
    }
    wil::unique_handle cancel{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    CHECK(cancel);
    std::thread cancelling(
        [&]()
        {
            (void)WaitForSingleObject(cancel.get(), 150);
            (void)SetEvent(cancel.get());
        });
    const ULONGLONG start = GetTickCount64();
    const HRESULT cancelled = helper(cancel.get(), TRUE);
    cancelling.join();
    CHECK(cancelled == HRESULT_FROM_WIN32(ERROR_CANCELLED) && GetTickCount64() - start < 3000);
    CHECK(helper(cancel.get(), FALSE) == HRESULT_FROM_WIN32(ERROR_CANCELLED));
    const ULONGLONG timeoutStart = GetTickCount64();
    CHECK(helper(nullptr, 1) == HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    CHECK(GetTickCount64() - timeoutStart >= 19000 && GetTickCount64() - timeoutStart < 25000);
    CHECK(GetModuleHandleW(L"Windows.Devices.Geolocation.dll") == nullptr &&
          GetModuleHandleW(L"locationapi.dll") == nullptr);
    services(Response, 2);
    TestHost deniedHost;
    wil::com_ptr_nothrow<IRedXeWidget> denied;
    CHECK(Create(create, deniedHost, "{}", denied) == S_OK);
    wil::com_ptr_nothrow<IRedXeNetworkWidget> deniedNetwork;
    CHECK(SUCCEEDED(denied.query_to(deniedNetwork.put())));
    CHECK(ResetEvent(cancel.get()));
    WeatherTestDiagnostics before{sizeof(before)}, after{sizeof(after)};
    CHECK(diagnostics(&before) == S_OK);
    uint32_t delay = 0;
    CHECK(FAILED(deniedNetwork->RunNetworkWork(cancel.get(), &delay)));
    CHECK(FAILED(deniedNetwork->RunNetworkWork(cancel.get(), &delay)));
    CHECK(diagnostics(&after) == S_OK && after.locationHelperRuns == before.locationHelperRuns + 1);
    CHECK(after.lastStatus == RedXeWidgetStatusUnavailable); // Nothing was ever drawn, so the host takes the tile.
    CHECK(deniedHost.pendingBytes == 0 && deniedHost.persistCalls == 0);
    return S_OK;
}
} // namespace

HRESULT RunWeatherLocationPolicyTests() noexcept;

HRESULT RunWeatherRegressionTests(HMODULE module)
{
    CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
    const auto com = wil::scope_exit([]() noexcept { CoUninitialize(); });
    const auto create = Resolve<RedXeCreateFn>(module, kRedXeCreateExport);
    const auto time = Resolve<decltype(&RedXeWeatherSetTestTime)>(module, "RedXeWeatherSetTestTime");
    CHECK(create && time);
    SYSTEMTIME local{};
    local.wYear = 2026;
    local.wMonth = 9;
    local.wDay = 5;
    local.wHour = 12;
    SYSTEMTIME utc{};
    FILETIME fileTime{};
    CHECK(TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc) && SystemTimeToFileTime(&utc, &fileTime));
    const uint64_t now = (static_cast<uint64_t>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
    time(now);
    const auto resetTime = wil::scope_exit([&]() noexcept { time(0); });
    gForecast = Forecast(now);
    CHECK(ModelTests(now) == S_OK);
    CHECK(AlertProviderTests() == S_OK);
    CHECK(RunWeatherLocationPolicyTests() == S_OK);
    CHECK(RenderTests(module, create, now) == S_OK);
    CHECK(RenderTests(module, create, now, true) == S_OK);
    CHECK(LocationTests(module, create) == S_OK);
    std::printf("Weather forecast, precipitation, WARP layout, allocation, helper isolation, cancellation, and "
                "persistence tests passed.\n");
    return S_OK;
}
