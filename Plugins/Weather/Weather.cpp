#define REDXE_PLUGIN_EXPORTS
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Host.h"
#include "PlugInterfaces/Widget.h"
#include "WeatherGpu.h"
#include "WeatherHttp.h"
#include "WeatherIcons.h"
#include "WeatherModel.h"
#include "WeatherTestContract.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include <string_view>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
constexpr char kPluginId[] = "builtin.weather";
constexpr char kWidgetTypeId[] = "weather";
constexpr char kSettingsSchema[] =
    R"json({"type":"object","additionalProperties":false,"properties":{"locationMode":{"type":"string","enum":["automatic","manual"]},"location":{"type":"string"},"temperatureUnit":{"type":"string","enum":["celsius","fahrenheit"]},"windUnit":{"type":"string","enum":["kmh","mph"]}}})json";
constexpr char kSettingsDefaults[] =
    R"json({"locationMode":"automatic","location":"","temperatureUnit":"celsius","windUnit":"kmh"})json";
constexpr RedXePluginSettingsContract kSettingsContract{
    sizeof(RedXePluginSettingsContract), kSettingsSchema, sizeof(kSettingsSchema) - 1, kSettingsDefaults,
    sizeof(kSettingsDefaults) - 1,
};
constexpr float kPanelR = 10.0f / 255.0f;
constexpr float kPanelG = 10.0f / 255.0f;
constexpr float kPanelB = 10.0f / 255.0f;
constexpr float kTextR = 0.90f;
constexpr float kTextG = 0.90f;
constexpr float kTextB = 0.92f;
constexpr float kMuted = 0.55f;
constexpr float kPanelInset = 4.0f;
constexpr float kPadFloorPx = 12.0f;
constexpr float kLabelFloorPx = 16.0f;
constexpr float kRowFloorPx = 18.0f;
constexpr float kListFloorPx = 22.0f;
constexpr float kKpiFloorPx = 30.0f;
constexpr float kHeroFloorPx = 48.0f;

constexpr std::array kMetadata{
    RedXePluginMetadata{
        sizeof(RedXePluginMetadata),
        kPluginId,
        L"Weather",
        L"Current conditions, forecast, and official alerts from MET Norway, MeteoAlarm, and NWS.",
        L"RedXe",
        L"1.0.0",
        RedXePluginCapabilityWidgetProvider,
    },
};

constexpr std::array kWidgetTypes{
    RedXeWidgetTypeDescriptor{
        sizeof(RedXeWidgetTypeDescriptor),
        kWidgetTypeId,
        L"Weather",
        L"Adaptive Direct3D weather tile with forecast and alerts.",
        960.0f,
        540.0f,
        160.0f,
        72.0f,
        RedXeWidgetFlagNone,
    },
};

enum class WeatherDensity : uint32_t
{
    Tiny = 0,
    Compact,
    Standard,
};

std::atomic<uint32_t> gLiveProviderCount{0};
std::atomic<uint32_t> gLiveWidgetCount{0};
std::atomic<uint32_t> gPaintCount{0};
std::atomic<uint32_t> gNetworkWorkCount{0};
std::atomic<uint32_t> gLastDelay{0};
std::atomic<uint32_t> gLastStatus{RedXeWidgetStatusInitializing};
std::atomic<float> gLastTemperature{0.0f};
std::atomic<uint32_t> gLastDailyCount{0};
std::atomic<uint32_t> gLastAlertCount{0};
std::array<wchar_t, 64> gLastLocation{};
SRWLOCK gLastLocationLock = SRWLOCK_INIT;
std::atomic<bool> gUseTestSnapshot{false};
WeatherSnapshot gTestSnapshot{};
SRWLOCK gTestSnapshotLock = SRWLOCK_INIT;

[[nodiscard]] WeatherDensity DensityForSize(float width, float height, bool raised) noexcept
{
    if (raised || (width >= 400.0f && height >= 240.0f) || (width >= 280.0f && height >= 400.0f))
    {
        return WeatherDensity::Standard;
    }
    if (width >= 280.0f && height >= 120.0f)
    {
        return WeatherDensity::Compact;
    }
    return WeatherDensity::Tiny;
}

[[nodiscard]] uint32_t WideCount(const wchar_t* text) noexcept
{
    return text ? static_cast<uint32_t>(wcslen(text)) : 0;
}

[[nodiscard]] float OutlineStroke(float size) noexcept
{
    return std::max(2.0f, size * 0.07f);
}

[[nodiscard]] WeatherRgb MixIconColor(WeatherCondition condition) noexcept
{
    const WeatherRgb raw = WeatherConditionColor(condition);
    return WeatherRgb{raw.red * 0.40f + kTextR * 0.60f, raw.green * 0.40f + kTextG * 0.60f,
                      raw.blue * 0.40f + kTextB * 0.60f};
}

void AppendTextLeft(WeatherGpuResources& resources, WeatherDrawList& list, float x, float y, float height, float red,
                    float green, float blue, float alpha, const wchar_t* text, uint32_t characters) noexcept
{
    (void)resources.AppendText(list, x, y, height, red, green, blue, alpha, text, characters);
}

void DrawSunOutline(WeatherDrawList& list, float x, float y, float size, float red, float green, float blue) noexcept
{
    const float stroke = OutlineStroke(size);
    const float cx = x + size * 0.5f;
    const float cy = y + size * 0.5f;
    const float disc = size * 0.42f;
    (void)list.AddRing(cx - disc * 0.5f, cy - disc * 0.5f, disc, disc, red, green, blue, 1.0f, disc * 0.5f - stroke);
    const float rayW = std::max(stroke, size * 0.08f);
    const float rayLen = size * 0.16f;
    const float inner = disc * 0.5f + size * 0.05f;
    (void)list.AddFill(cx - rayW * 0.5f, cy - inner - rayLen, rayW, rayLen, red, green, blue, 1.0f, rayW * 0.4f);
    (void)list.AddFill(cx - rayW * 0.5f, cy + inner, rayW, rayLen, red, green, blue, 1.0f, rayW * 0.4f);
    (void)list.AddFill(cx - inner - rayLen, cy - rayW * 0.5f, rayLen, rayW, red, green, blue, 1.0f, rayW * 0.4f);
    (void)list.AddFill(cx + inner, cy - rayW * 0.5f, rayLen, rayW, red, green, blue, 1.0f, rayW * 0.4f);
    const float tick = std::max(stroke, size * 0.09f);
    const float diag = (inner + rayLen * 0.35f) * 0.7071f;
    (void)list.AddFill(cx + diag - tick * 0.5f, cy - diag - tick * 0.5f, tick, tick, red, green, blue, 1.0f,
                       tick * 0.3f);
    (void)list.AddFill(cx + diag - tick * 0.5f, cy + diag - tick * 0.5f, tick, tick, red, green, blue, 1.0f,
                       tick * 0.3f);
    (void)list.AddFill(cx - diag - tick * 0.5f, cy + diag - tick * 0.5f, tick, tick, red, green, blue, 1.0f,
                       tick * 0.3f);
    (void)list.AddFill(cx - diag - tick * 0.5f, cy - diag - tick * 0.5f, tick, tick, red, green, blue, 1.0f,
                       tick * 0.3f);
}

void DrawCloudOutline(WeatherDrawList& list, float x, float y, float size, float red, float green, float blue) noexcept
{
    const float stroke = OutlineStroke(size);
    const float bodyW = size * 0.84f;
    const float bodyH = size * 0.46f;
    (void)list.AddStroke(x + size * 0.08f, y + size * 0.42f, bodyW, bodyH, red, green, blue, 1.0f, bodyH * 0.50f,
                         stroke);
    const float puff = size * 0.46f;
    (void)list.AddStroke(x + size * 0.28f, y + size * 0.14f, puff, puff, red, green, blue, 1.0f, puff * 0.50f, stroke);
    const float left = size * 0.36f;
    (void)list.AddStroke(x + size * 0.10f, y + size * 0.30f, left, left, red, green, blue, 1.0f, left * 0.50f, stroke);
}

void DrawWindMark(WeatherGpuResources& resources, WeatherDrawList& list, float x, float y, float size, float red,
                  float green, float blue) noexcept
{
    if (resources.AppendIcon(list, x, y, size, kWeatherIconStrongWind, red, green, blue, 1.0f) == S_OK)
    {
        return;
    }
    const float stroke = OutlineStroke(size);
    for (uint32_t line = 0; line < 3; ++line)
    {
        const float width = size * (1.0f - 0.16f * static_cast<float>(line));
        const float lineY = y + size * (0.18f + 0.28f * static_cast<float>(line));
        (void)list.AddStroke(x + (size - width), lineY, width, stroke * 1.35f, red, green, blue, 1.0f, stroke, stroke);
    }
}

void DrawHorizonSun(WeatherGpuResources& resources, WeatherDrawList& list, float x, float y, float size, float red,
                    float green, float blue, bool rising) noexcept
{
    const wchar_t glyph = rising ? kWeatherIconSunrise : kWeatherIconSunset;
    if (resources.AppendIcon(list, x, y, size, glyph, red, green, blue, 1.0f) == S_OK)
    {
        return;
    }
    const float stroke = OutlineStroke(size);
    const float sun = size * 0.50f;
    const float sunX = x + (size - sun) * 0.5f;
    const float sunY = rising ? y + size * 0.04f : y + size * 0.26f;
    (void)list.AddRing(sunX, sunY, sun, sun, red, green, blue, 1.0f, sun * 0.5f - stroke);
    (void)list.AddStroke(x + size * 0.04f, y + size * 0.62f, size * 0.92f, stroke, red, green, blue, 1.0f,
                         stroke * 0.5f, stroke);
    (void)list.AddFill(x + size * 0.48f, y, stroke, size * 0.10f, red, green, blue, 1.0f, stroke * 0.4f);
    (void)list.AddFill(x + size * 0.06f, y + size * 0.16f, size * 0.12f, stroke, red, green, blue, 1.0f, stroke * 0.4f);
    (void)list.AddFill(x + size * 0.82f, y + size * 0.16f, size * 0.12f, stroke, red, green, blue, 1.0f, stroke * 0.4f);
}

void DrawConditionIcon(WeatherGpuResources& resources, WeatherDrawList& list, float x, float y, float size,
                       WeatherCondition condition, const WeatherRgb& color) noexcept
{
    const float red = color.red;
    const float green = color.green;
    const float blue = color.blue;
    if (resources.AppendIcon(list, x, y, size, WeatherIconForCondition(condition), red, green, blue, 1.0f) == S_OK)
    {
        return;
    }
    const float stroke = OutlineStroke(size);
    switch (condition)
    {
    case WeatherCondition::Clear:
        DrawSunOutline(list, x, y, size, red, green, blue);
        break;
    case WeatherCondition::PartlyCloudy:
        DrawSunOutline(list, x, y, size * 0.55f, red, green, blue);
        DrawCloudOutline(list, x + size * 0.16f, y + size * 0.26f, size * 0.82f, red, green, blue);
        break;
    case WeatherCondition::Rain:
    case WeatherCondition::Sleet:
        DrawCloudOutline(list, x, y, size * 0.78f, red, green, blue);
        for (uint32_t drop = 0; drop < 3; ++drop)
        {
            (void)list.AddStroke(x + size * (0.28f + 0.18f * static_cast<float>(drop)), y + size * 0.72f, stroke,
                                 size * 0.20f, red, green, blue, 1.0f, stroke * 0.5f, stroke);
        }
        break;
    case WeatherCondition::Snow:
        DrawCloudOutline(list, x, y, size * 0.74f, red, green, blue);
        for (uint32_t flake = 0; flake < 3; ++flake)
        {
            const float flakeSize = size * 0.12f;
            const float flakeStroke = OutlineStroke(flakeSize);
            (void)list.AddRing(x + size * (0.26f + 0.22f * static_cast<float>(flake)), y + size * 0.78f, flakeSize,
                               flakeSize, red, green, blue, 1.0f, flakeSize * 0.5f - flakeStroke);
        }
        break;
    case WeatherCondition::Thunder:
        DrawCloudOutline(list, x, y, size * 0.72f, red, green, blue);
        (void)list.AddFill(x + size * 0.42f, y + size * 0.52f, size * 0.22f, stroke * 1.2f, red, green, blue, 1.0f,
                           stroke * 0.4f);
        (void)list.AddFill(x + size * 0.52f, y + size * 0.52f, stroke * 1.2f, size * 0.20f, red, green, blue, 1.0f,
                           stroke * 0.4f);
        (void)list.AddFill(x + size * 0.36f, y + size * 0.70f, size * 0.22f, stroke * 1.2f, red, green, blue, 1.0f,
                           stroke * 0.4f);
        break;
    case WeatherCondition::Fog:
    case WeatherCondition::Wind:
        DrawWindMark(resources, list, x, y, size, red, green, blue);
        break;
    default:
        DrawCloudOutline(list, x, y, size, red, green, blue);
        break;
    }
}

void ReportStatus(IRedXeHost* host, const char* instanceId, uint32_t status, const wchar_t* reason) noexcept
{
    if (!host || !instanceId)
    {
        return;
    }
    RedXeWidgetStatusReport report{sizeof(RedXeWidgetStatusReport), status, reason};
    (void)host->ReportWidgetStatus(instanceId, &report);
    gLastStatus.store(status, std::memory_order_relaxed);
}

void LogWeather(IRedXeHost* host, uint32_t level, const char* instanceId, const char* eventId, const char* message,
                HRESULT code = S_OK) noexcept
{
    (void)RedXeHostLog(host, level, kPluginId, instanceId, eventId, message, code);
}

class WeatherWidget final : public RedXeComObject<WeatherWidget, IRedXeWidget, IRedXeGpuWidget, IRedXeScheduledWidget,
                                                  IRedXeRaisedWidget, IRedXeNetworkWidget>
{
  public:
    WeatherWidget(wil::com_ptr_nothrow<IRedXeWidgetProvider>&& providerOwner, const WeatherConfiguration& configuration,
                  IRedXeHost* host, const char* instanceId) noexcept
        : _providerOwner(std::move(providerOwner)), _configuration(configuration), _host(host)
    {
        WeatherCopyNarrow(instanceId ? instanceId : "", _instanceId.data(), _instanceId.size());
        gLiveWidgetCount.fetch_add(1, std::memory_order_relaxed);
        LogWeather(_host, RedXeLogLevelInfo, _instanceId.data(), "widget-created", "weather widget constructed.");
        ReportStatus(_host, _instanceId.data(), RedXeWidgetStatusInitializing, L"Waiting for weather.");
    }

    ~WeatherWidget()
    {
        if (_gpuHeld.exchange(false, std::memory_order_acq_rel))
        {
            WeatherGpuRelease();
        }
        gLiveWidgetCount.fetch_sub(1, std::memory_order_relaxed);
    }

    HRESULT STDMETHODCALLTYPE SetVisible(BOOL visible) noexcept override
    {
        _visible.store(visible != FALSE, std::memory_order_release);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CollectPersistentSettings(char* jsonUtf8, uint32_t capacityBytes,
                                                        uint32_t* writtenBytes) noexcept override
    {
        return RedXeCollectNoPersistentSettings(jsonUtf8, capacityBytes, writtenBytes);
    }

    HRESULT STDMETHODCALLTYPE GetRaisedExtent(RedXeRaisedExtent* extent) noexcept override
    {
        if (!extent)
        {
            return E_POINTER;
        }
        *extent = RedXeRaisedExtentHalf;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetRaised(BOOL raised) noexcept override
    {
        _raised.store(raised != FALSE, std::memory_order_release);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceCreated(const RedXeGpuDeviceContext* context) noexcept override
    {
        if (!context || context->sizeBytes != sizeof(RedXeGpuDeviceContext) || !context->device)
        {
            return E_INVALIDARG;
        }
        if (context->featureLevel < D3D_FEATURE_LEVEL_11_0)
        {
            return E_NOTIMPL;
        }
        const HRESULT result = WeatherGpuAcquire(context->device);
        if (SUCCEEDED(result))
        {
            _gpuHeld.store(true, std::memory_order_release);
            RasterizeGlyphs();
            LogWeather(_host, RedXeLogLevelInfo, _instanceId.data(), "gpu-ready", "weather GPU resources acquired.");
        }
        else
        {
            LogWeather(_host, RedXeLogLevelError, _instanceId.data(), "gpu-acquire-failed", "WeatherGpuAcquire failed.",
                       result);
        }
        return result;
    }

    void STDMETHODCALLTYPE OnDeviceLost() noexcept override
    {
        if (_gpuHeld.exchange(false, std::memory_order_acq_rel))
        {
            WeatherGpuRelease();
        }
    }

    HRESULT STDMETHODCALLTYPE OnTargetSizeChanged(const RedXeGpuTargetSizeContext* context) noexcept override
    {
        if (!context || context->sizeBytes != sizeof(RedXeGpuTargetSizeContext))
        {
            return E_INVALIDARG;
        }
        RasterizeGlyphs();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Render(const RedXeGpuFrameContext* context) noexcept override
    {
        if (!context || context->sizeBytes != sizeof(RedXeGpuFrameContext) || !context->widget ||
            !context->deviceContext)
        {
            return E_INVALIDARG;
        }
        const RedXeWidgetFrameContext& frame = *context->widget;
        if (frame.sizeBytes != sizeof(RedXeWidgetFrameContext) || frame.widthPixels == 0 || frame.heightPixels == 0)
        {
            return S_OK;
        }
        WeatherGpuResources* resources = WeatherGpuGet();
        if (!resources)
        {
            return E_UNEXPECTED;
        }

        AcquireSRWLockShared(&_lock);
        const WeatherSnapshot snapshot = _snapshot;
        const bool hasSnapshot = _hasSnapshot;
        ReleaseSRWLockShared(&_lock);

        WeatherDrawList list;
        WeatherGpuLock();
        HRESULT result = BuildScene(*resources, list, snapshot, hasSnapshot, static_cast<float>(frame.widthPixels),
                                    static_cast<float>(frame.heightPixels));
        if (SUCCEEDED(result))
        {
            result = resources->Render(context->deviceContext, static_cast<float>(frame.widthPixels),
                                       static_cast<float>(frame.heightPixels), list);
        }
        WeatherGpuUnlock();
        if (SUCCEEDED(result))
        {
            gPaintCount.fetch_add(1, std::memory_order_relaxed);
        }
        return result;
    }

    HRESULT STDMETHODCALLTYPE GetNextFrameDelayMilliseconds(uint32_t* delayMilliseconds) noexcept override
    {
        if (delayMilliseconds)
        {
            *delayMilliseconds = 0;
        }
        if (!delayMilliseconds)
        {
            return E_POINTER;
        }
        if (!_visible.load(std::memory_order_acquire))
        {
            return S_FALSE;
        }
        *delayMilliseconds = std::max(1U, _nextFrameDelay.load(std::memory_order_acquire));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE RunNetworkWork(HANDLE cancelEvent, uint32_t* nextDelayMilliseconds) noexcept override
    {
        if (nextDelayMilliseconds)
        {
            *nextDelayMilliseconds = 0;
        }
        if (!cancelEvent || !nextDelayMilliseconds)
        {
            return E_POINTER;
        }
        gNetworkWorkCount.fetch_add(1, std::memory_order_relaxed);
        if (WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0)
        {
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }

        WeatherSnapshot snapshot{};
        bool usedFixture = false;
        AcquireSRWLockShared(&gTestSnapshotLock);
        if (gUseTestSnapshot.load(std::memory_order_acquire))
        {
            snapshot = gTestSnapshot;
            usedFixture = true;
        }
        ReleaseSRWLockShared(&gTestSnapshotLock);

        uint32_t delay = kWeatherDefaultRefreshMilliseconds;
        HRESULT result = S_OK;
        if (usedFixture)
        {
            result = ApplySnapshot(snapshot, L"Showing fixture weather.", false);
        }
        else
        {
            result = FetchLive(cancelEvent, snapshot, delay);
        }
        *nextDelayMilliseconds = WeatherClampRefreshMilliseconds(delay);
        _nextFrameDelay.store(*nextDelayMilliseconds, std::memory_order_release);
        gLastDelay.store(*nextDelayMilliseconds, std::memory_order_relaxed);
        if (FAILED(result))
        {
            LogWeather(_host, RedXeLogLevelWarning, _instanceId.data(), "forecast-failed",
                       "weather fetch or parse failed.", result);
        }
        else if (!usedFixture)
        {
            LogWeather(_host, RedXeLogLevelInfo, _instanceId.data(), "forecast-ok", "weather snapshot applied.");
        }
        if (_host)
        {
            (void)_host->RequestFrame();
        }
        return SUCCEEDED(result) ? S_OK : result;
    }

  private:
    [[nodiscard]] HRESULT FetchLive(HANDLE cancelEvent, WeatherSnapshot& snapshot, uint32_t& delay) noexcept
    {
        delay = kWeatherDefaultRefreshMilliseconds;
        wchar_t reason[96] = L"Enter a city for a local forecast.";
        if (_configuration.locationMode == WeatherLocationMode::Manual)
        {
            double latitude = 0.0;
            double longitude = 0.0;
            if (WeatherParseLatLon(_configuration.location.data(), latitude, longitude))
            {
                snapshot.latitude = latitude;
                snapshot.longitude = longitude;
                snapshot.hasCoordinates = true;
            }
            else if (_configuration.location[0] != '\0')
            {
                std::array<char, kWeatherMaximumUrlBytes> url{};
                const HRESULT built = WeatherBuildLocationSearchUrl(_configuration.location.data(), url.data(),
                                                                     static_cast<uint32_t>(url.size()));
                if (FAILED(built))
                {
                    return built;
                }
                WeatherHttpResponse http{};
                const HRESULT fetch = WeatherHttpGet(url.data(), cancelEvent, http);
                if (FAILED(fetch))
                {
                    ReportStatus(_host, _instanceId.data(), RedXeWidgetStatusUnavailable, L"Location lookup failed.");
                    return fetch;
                }
                delay = std::max(delay, http.expiresDelayMilliseconds);
                if (FAILED(WeatherParseNominatim(WeatherHttpBody(http), snapshot)))
                {
                    ReportStatus(_host, _instanceId.data(), RedXeWidgetStatusUnavailable, L"Location was not found.");
                    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
                }
            }
        }
        if (!snapshot.hasCoordinates)
        {
            if (!WeatherTryAutomaticLocation(snapshot.locationName.data(), snapshot.locationName.size(),
                                             snapshot.countryCode.data(), snapshot.countryCode.size(),
                                             snapshot.latitude, snapshot.longitude))
            {
                ReportStatus(_host, _instanceId.data(), RedXeWidgetStatusUnavailable, reason);
                return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            }
            snapshot.hasCoordinates = true;
            snapshot.regionCoordinates = true;
            wcscpy_s(reason, L"Windows region only. Enter a city for a local forecast.");
        }

        WeatherHttpResponse http{};
        if (snapshot.hasCoordinates && (snapshot.regionCoordinates || snapshot.locationName[0] == L'\0'))
        {
            std::array<char, kWeatherMaximumUrlBytes> reverseUrl{};
            sprintf_s(
                reverseUrl.data(), reverseUrl.size(),
                "https://nominatim.openstreetmap.org/reverse?lat=%.4f&lon=%.4f&format=json&addressdetails=1&zoom=10",
                snapshot.latitude, snapshot.longitude);
            if (SUCCEEDED(WeatherHttpGet(reverseUrl.data(), cancelEvent, http)))
            {
                delay = std::max(delay, http.expiresDelayMilliseconds);
                WeatherSnapshot place = snapshot;
                if (SUCCEEDED(WeatherParseNominatim(WeatherHttpBody(http), place)))
                {
                    WeatherCopyWide(place.locationName.data(), snapshot.locationName.data(),
                                    snapshot.locationName.size());
                    WeatherCopyNarrow(place.countryCode.data(), snapshot.countryCode.data(),
                                      snapshot.countryCode.size());
                }
            }
        }

        std::array<char, kWeatherMaximumUrlBytes> forecastUrl{};
        sprintf_s(forecastUrl.data(), forecastUrl.size(),
                  "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=%.4f&lon=%.4f", snapshot.latitude,
                  snapshot.longitude);
        HRESULT fetch = WeatherHttpGet(forecastUrl.data(), cancelEvent, http);
        if (FAILED(fetch))
        {
            ReportStatus(_host, _instanceId.data(), RedXeWidgetStatusUnavailable, L"Forecast request failed.");
            return fetch;
        }
        delay = std::max(delay, http.expiresDelayMilliseconds);
        fetch = WeatherParseLocationForecast(WeatherHttpBody(http), snapshot);
        if (FAILED(fetch))
        {
            ReportStatus(_host, _instanceId.data(), RedXeWidgetStatusUnavailable, L"Forecast data was unusable.");
            return fetch;
        }

        SYSTEMTIME utc{};
        GetSystemTime(&utc);
        std::array<char, kWeatherMaximumUrlBytes> sunriseUrl{};
        sprintf_s(sunriseUrl.data(), sunriseUrl.size(),
                  "https://api.met.no/weatherapi/sunrise/3.0/sun?lat=%.4f&lon=%.4f&date=%04u-%02u-%02u",
                  snapshot.latitude, snapshot.longitude, utc.wYear, utc.wMonth, utc.wDay);
        if (SUCCEEDED(WeatherHttpGet(sunriseUrl.data(), cancelEvent, http)))
        {
            delay = std::max(delay, http.expiresDelayMilliseconds);
            (void)WeatherParseSunrise(WeatherHttpBody(http), snapshot);
        }

        const WeatherAlertRegion region = WeatherAlertRegionFromCountry(snapshot.countryCode.data());
        if (region == WeatherAlertRegion::UnitedStates)
        {
            std::array<char, kWeatherMaximumUrlBytes> nwsUrl{};
            sprintf_s(nwsUrl.data(), nwsUrl.size(), "https://api.weather.gov/alerts/active?point=%.4f,%.4f",
                      snapshot.latitude, snapshot.longitude);
            if (SUCCEEDED(WeatherHttpGet(nwsUrl.data(), cancelEvent, http)))
            {
                delay = std::max(delay, http.expiresDelayMilliseconds);
                (void)WeatherParseNwsAlerts(WeatherHttpBody(http), snapshot);
            }
        }
        else if (region == WeatherAlertRegion::Europe && snapshot.countryCode[0] != '\0')
        {
            std::array<char, 8> lower{};
            WeatherCopyNarrow(snapshot.countryCode.data(), lower.data(), lower.size());
            for (char& value : lower)
            {
                if (value >= 'A' && value <= 'Z')
                {
                    value = static_cast<char>(value - 'A' + 'a');
                }
            }
            std::array<char, kWeatherMaximumUrlBytes> meteoUrl{};
            sprintf_s(meteoUrl.data(), meteoUrl.size(), "https://feeds.meteoalarm.org/api/v1/warnings/feeds-%s",
                      lower.data());
            if (SUCCEEDED(WeatherHttpGet(meteoUrl.data(), cancelEvent, http)))
            {
                delay = std::max(delay, http.expiresDelayMilliseconds);
                (void)WeatherParseMeteoAlarm(WeatherHttpBody(http), snapshot);
            }
        }
        const wchar_t* statusReason = snapshot.regionCoordinates ? reason : L"";
        return ApplySnapshot(snapshot, statusReason, snapshot.regionCoordinates);
    }

    [[nodiscard]] HRESULT ApplySnapshot(const WeatherSnapshot& snapshot, const wchar_t* reason, bool degraded) noexcept
    {
        AcquireSRWLockExclusive(&_lock);
        _snapshot = snapshot;
        _hasSnapshot = true;
        ReleaseSRWLockExclusive(&_lock);
        gLastTemperature.store(snapshot.temperatureCelsius, std::memory_order_relaxed);
        gLastDailyCount.store(snapshot.dailyCount, std::memory_order_relaxed);
        gLastAlertCount.store(snapshot.alertCount, std::memory_order_relaxed);
        AcquireSRWLockExclusive(&gLastLocationLock);
        WeatherCopyWide(snapshot.locationName.data(), gLastLocation.data(), gLastLocation.size());
        ReleaseSRWLockExclusive(&gLastLocationLock);
        RasterizeGlyphs();
        const uint32_t status = degraded || snapshot.stale ? RedXeWidgetStatusDegraded : RedXeWidgetStatusOk;
        ReportStatus(_host, _instanceId.data(), status, reason && reason[0] != L'\0' ? reason : nullptr);
        return S_OK;
    }

    void RasterizeGlyphs() noexcept
    {
        WeatherGpuLock();
        const auto unlock = wil::scope_exit([]() noexcept { WeatherGpuUnlock(); });
        if (!_gpuHeld.load(std::memory_order_acquire))
        {
            return;
        }
        WeatherGpuResources* resources = WeatherGpuGet();
        if (!resources)
        {
            return;
        }
        AcquireSRWLockShared(&_lock);
        const WeatherSnapshot snapshot = _snapshot;
        ReleaseSRWLockShared(&_lock);
        (void)resources->EnsureGlyphs(snapshot.locationName.data(),
                                      static_cast<uint32_t>(wcslen(snapshot.locationName.data())));
        (void)resources->EnsureGlyphs(
            L"TodayTomorrowSundayMondayTuesdayWednesdayThursdayFridaySaturdayMET Norway",
            static_cast<uint32_t>(
                wcslen(L"TodayTomorrowSundayMondayTuesdayWednesdayThursdayFridaySaturdayMET Norway")));
        for (uint32_t index = 0; index < snapshot.alertCount; ++index)
        {
            (void)resources->EnsureGlyphs(snapshot.alerts[index].title.data(),
                                          static_cast<uint32_t>(wcslen(snapshot.alerts[index].title.data())));
        }
    }

    [[nodiscard]] HRESULT BuildScene(WeatherGpuResources& resources, WeatherDrawList& list,
                                     const WeatherSnapshot& snapshot, bool hasSnapshot, float width,
                                     float height) noexcept
    {
        const float inset = kPanelInset;
        const float pad = kPadFloorPx;
        (void)list.AddFill(inset, inset, width - inset * 2.0f, height - inset * 2.0f, kPanelR, kPanelG, kPanelB, 1.0f,
                           10.0f);
        const WeatherRgb iconColor = MixIconColor(snapshot.currentCondition);
        const WeatherAlertSeverity alertSeverity = WeatherHighestAlertSeverity(snapshot);
        const WeatherRgb alertColor = WeatherAlertColor(alertSeverity);
        if (alertSeverity != WeatherAlertSeverity::None)
        {
            (void)list.AddFill(inset, inset, 4.0f, height - inset * 2.0f, alertColor.red, alertColor.green,
                               alertColor.blue, 1.0f, 0.0f);
        }

        const bool raised = _raised.load(std::memory_order_acquire);
        const WeatherDensity density = DensityForSize(width, height, raised);
        const float contentLeft = inset + pad + (alertSeverity != WeatherAlertSeverity::None ? 6.0f : 0.0f);
        const float contentRight = width - inset - pad;
        const float contentWidth = contentRight - contentLeft;
        float y = inset + pad;
        if (!hasSnapshot)
        {
            AppendTextLeft(resources, list, contentLeft, y, kKpiFloorPx, kTextR, kTextG, kTextB, 1.0f, L"Weather", 7);
            return S_OK;
        }

        std::array<wchar_t, 32> temperature{};
        (void)WeatherFormatTemperature(snapshot.temperatureCelsius, _configuration.temperatureUnit, temperature.data(),
                                       static_cast<uint32_t>(temperature.size()));
        const uint32_t temperatureChars = WideCount(temperature.data());
        std::array<wchar_t, 48> todayRange{};
        (void)WeatherFormatTemperatureRange(snapshot.todayMinimumCelsius, snapshot.todayMaximumCelsius,
                                            _configuration.temperatureUnit, todayRange.data(),
                                            static_cast<uint32_t>(todayRange.size()));
        const uint32_t todayRangeChars = WideCount(todayRange.data());
        std::array<wchar_t, 32> wind{};
        (void)WeatherFormatWind(snapshot.windMetersPerSecond, _configuration.windUnit, wind.data(),
                                static_cast<uint32_t>(wind.size()));
        const uint32_t windChars = WideCount(wind.data());
        std::array<wchar_t, 8> sunrise{};
        std::array<wchar_t, 8> sunset{};
        const uint32_t sunriseChars =
            WeatherFormatClock(snapshot.sunriseFileTime100ns, sunrise.data(), static_cast<uint32_t>(sunrise.size()));
        const uint32_t sunsetChars =
            WeatherFormatClock(snapshot.sunsetFileTime100ns, sunset.data(), static_cast<uint32_t>(sunset.size()));
        const uint32_t locationChars = WideCount(snapshot.locationName.data());

        if (density == WeatherDensity::Tiny)
        {
            const float iconSize = std::min(height - pad * 2.0f, 56.0f);
            const float tempH = kKpiFloorPx;
            AppendTextLeft(resources, list, contentLeft, y + (iconSize - tempH) * 0.5f, tempH, kTextR, kTextG, kTextB,
                           1.0f, temperature.data(), temperatureChars);
            DrawConditionIcon(resources, list, contentRight - iconSize, y, iconSize, snapshot.currentCondition,
                              iconColor);
            return S_OK;
        }

        const float headerH = std::clamp(std::min(contentWidth * 0.28f, height * 0.26f), 72.0f,
                                         density == WeatherDensity::Standard ? 168.0f : 120.0f);
        const float tempH = std::clamp(headerH * 0.78f, kHeroFloorPx, 104.0f);
        float iconSize = std::clamp(headerH * 0.88f, 48.0f, 128.0f);
        const bool showSunTimes = (sunriseChars > 0 || sunsetChars > 0) && contentWidth >= 260.0f;
        const bool showWind = windChars > 0 && contentWidth >= 260.0f;
        const float sunTextH = std::clamp(headerH * 0.26f, kLabelFloorPx, 22.0f);
        const float sunIcon = sunTextH * 1.2f;
        const float clockWidth = std::max(resources.MeasureText(sunrise.data(), sunriseChars, sunTextH),
                                          std::max(resources.MeasureText(sunset.data(), sunsetChars, sunTextH),
                                                   resources.MeasureText(L"00:00", 5, sunTextH)));
        const float sunBlockW = showSunTimes ? sunIcon + 8.0f + clockWidth : 0.0f;
        const float tempWidth = resources.MeasureText(temperature.data(), temperatureChars, tempH);
        float iconX = (width - iconSize) * 0.5f;
        if (iconX < contentLeft + tempWidth + 10.0f)
        {
            iconX = contentLeft + tempWidth + 10.0f;
        }
        if (showSunTimes && iconX + iconSize > contentRight - sunBlockW - 8.0f)
        {
            iconX = contentRight - sunBlockW - 8.0f - iconSize;
            if (iconX < contentLeft + tempWidth + 8.0f)
            {
                iconSize = std::max(40.0f, iconSize - (contentLeft + tempWidth + 8.0f - iconX));
                iconX = contentLeft + tempWidth + 8.0f;
            }
        }

        const float tempY = y + (headerH - tempH) * 0.5f;
        AppendTextLeft(resources, list, contentLeft, tempY, tempH, kTextR, kTextG, kTextB, 1.0f, temperature.data(),
                       temperatureChars);
        DrawConditionIcon(resources, list, iconX, y + (headerH - iconSize) * 0.5f, iconSize, snapshot.currentCondition,
                          iconColor);
        if (showSunTimes)
        {
            const float sunBlockH = sunTextH * 2.0f + 8.0f;
            const float sunY = y + (headerH - sunBlockH) * 0.5f;
            const float sunX = contentRight - sunBlockW;
            if (sunriseChars > 0)
            {
                DrawHorizonSun(resources, list, sunX, sunY, sunIcon, kTextR, kTextG, kTextB, true);
                AppendTextLeft(resources, list, sunX + sunIcon + 6.0f, sunY + (sunIcon - sunTextH) * 0.5f, sunTextH,
                               kTextR, kTextG, kTextB, 1.0f, sunrise.data(), sunriseChars);
            }
            if (sunsetChars > 0)
            {
                const float rowY = sunY + sunTextH + 8.0f;
                DrawHorizonSun(resources, list, sunX, rowY, sunIcon, kTextR, kTextG, kTextB, false);
                AppendTextLeft(resources, list, sunX + sunIcon + 6.0f, rowY + (sunIcon - sunTextH) * 0.5f, sunTextH,
                               kTextR, kTextG, kTextB, 1.0f, sunset.data(), sunsetChars);
            }
        }
        y += headerH + 10.0f;

        const uint32_t plannedDays = std::min(snapshot.dailyCount, raised ? kWeatherDailyCount
                                                                   : density == WeatherDensity::Standard ? 5U
                                                                                                         : 3U);
        const float attributionReserve = kLabelFloorPx + 6.0f;
        const float alertReserve = snapshot.alertCount > 0 ? kRowFloorPx + 14.0f : 0.0f;
        const float metaProbe = kListFloorPx + 12.0f;
        const float forecastBudget = height - inset - pad - attributionReserve - y - alertReserve - metaProbe;
        const float minRow = kListFloorPx + 8.0f;
        uint32_t rows = plannedDays;
        if (forecastBudget < minRow)
        {
            rows = 0;
        }
        else
        {
            rows = std::min(rows, static_cast<uint32_t>(forecastBudget / minRow));
        }
        const float rowH = rows > 0 ? std::min(44.0f, forecastBudget / static_cast<float>(rows)) : 0.0f;
        const float listH = rows > 0 ? std::clamp(rowH * 0.72f, kListFloorPx, 34.0f) : kListFloorPx;
        const float metaH = listH + 4.0f;
        const float gap = 14.0f;
        float maxDayWidth = resources.MeasureText(snapshot.locationName.data(), locationChars, listH);
        float maxRangeWidth = resources.MeasureText(todayRange.data(), todayRangeChars, listH);
        std::array<std::array<wchar_t, 32>, kWeatherDailyCount> dayLabels{};
        std::array<std::array<wchar_t, 48>, kWeatherDailyCount> dayRanges{};
        for (uint32_t index = 0; index < rows; ++index)
        {
            (void)WeatherFormatForecastDay(snapshot.daily[index].dayFileTime100ns, 0, index, dayLabels[index].data(),
                                           static_cast<uint32_t>(dayLabels[index].size()));
            (void)WeatherFormatTemperatureRange(snapshot.daily[index].minimumCelsius,
                                                snapshot.daily[index].maximumCelsius, _configuration.temperatureUnit,
                                                dayRanges[index].data(),
                                                static_cast<uint32_t>(dayRanges[index].size()));
            maxDayWidth = std::max(
                maxDayWidth, resources.MeasureText(dayLabels[index].data(), WideCount(dayLabels[index].data()), listH));
            maxRangeWidth = std::max(maxRangeWidth, resources.MeasureText(dayRanges[index].data(),
                                                                          WideCount(dayRanges[index].data()), listH));
        }
        const float rangeX = contentLeft + maxDayWidth + gap;
        const float mark = listH * 1.15f;
        const float iconColumn = rangeX + maxRangeWidth + gap;

        AppendTextLeft(resources, list, contentLeft, y, listH, kTextR, kTextG, kTextB, 1.0f,
                       snapshot.locationName.data(), locationChars);
        AppendTextLeft(resources, list, rangeX, y, listH, kTextR, kTextG, kTextB, 1.0f, todayRange.data(),
                       todayRangeChars);
        if (showWind)
        {
            DrawWindMark(resources, list, iconColumn, y + (listH - mark) * 0.5f, mark, kTextR, kTextG, kTextB);
            AppendTextLeft(resources, list, iconColumn + mark + 6.0f, y, listH, kTextR, kTextG, kTextB, 1.0f,
                           wind.data(), windChars);
        }
        y += metaH + 8.0f;

        if (snapshot.alertCount > 0)
        {
            (void)list.AddFill(contentLeft, y, contentWidth, kRowFloorPx + 8.0f, alertColor.red, alertColor.green,
                               alertColor.blue, 0.22f, 4.0f);
            AppendTextLeft(resources, list, contentLeft + 6.0f, y + 4.0f, kLabelFloorPx, alertColor.red,
                           alertColor.green, alertColor.blue, 1.0f, snapshot.alerts[0].title.data(),
                           WideCount(snapshot.alerts[0].title.data()));
            y += kRowFloorPx + 14.0f;
        }

        for (uint32_t index = 0; index < rows; ++index)
        {
            const WeatherDailyForecast& day = snapshot.daily[index];
            const WeatherRgb dayColor = MixIconColor(day.condition);
            const float rowY = y + (rowH - listH) * 0.5f;
            AppendTextLeft(resources, list, contentLeft, rowY, listH, kTextR, kTextG, kTextB, 1.0f,
                           dayLabels[index].data(), WideCount(dayLabels[index].data()));
            AppendTextLeft(resources, list, rangeX, rowY, listH, kTextR, kTextG, kTextB, 1.0f, dayRanges[index].data(),
                           WideCount(dayRanges[index].data()));
            DrawConditionIcon(resources, list, iconColumn, y + (rowH - mark) * 0.5f, mark, day.condition, dayColor);
            y += rowH;
        }

        AppendTextLeft(resources, list, contentLeft, height - inset - pad - kLabelFloorPx, kLabelFloorPx, kTextR,
                       kTextG, kTextB, kMuted, L"MET Norway", 10);
        return S_OK;
    }

    wil::com_ptr_nothrow<IRedXeWidgetProvider> _providerOwner;
    WeatherConfiguration _configuration;
    IRedXeHost* _host = nullptr;
    std::array<char, 128> _instanceId{};
    WeatherSnapshot _snapshot{};
    SRWLOCK _lock = SRWLOCK_INIT;
    std::atomic<bool> _visible{false};
    std::atomic<bool> _raised{false};
    std::atomic<uint32_t> _nextFrameDelay{kWeatherDefaultRefreshMilliseconds};
    bool _hasSnapshot = false;
    std::atomic<bool> _gpuHeld{false};
};

class WeatherProvider final : public RedXeComObject<WeatherProvider, IRedXeWidgetProvider>
{
  public:
    WeatherProvider(const WeatherConfiguration& configuration, IRedXeHost* host) noexcept
        : _configuration(configuration), _host(host)
    {
        gLiveProviderCount.fetch_add(1, std::memory_order_relaxed);
    }

    ~WeatherProvider()
    {
        gLiveProviderCount.fetch_sub(1, std::memory_order_relaxed);
    }

    HRESULT STDMETHODCALLTYPE GetWidgetTypes(const RedXeWidgetTypeDescriptor** descriptors,
                                             uint32_t* count) noexcept override
    {
        if (descriptors)
        {
            *descriptors = nullptr;
        }
        if (count)
        {
            *count = 0;
        }
        if (!descriptors || !count)
        {
            return E_POINTER;
        }
        *descriptors = kWidgetTypes.data();
        *count = 1;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateWidget(const char* typeId, const char* instanceId,
                                           IRedXeWidget** widget) noexcept override
    {
        if (widget)
        {
            *widget = nullptr;
        }
        if (!widget)
        {
            return E_POINTER;
        }
        if (!typeId || !instanceId || instanceId[0] == '\0')
        {
            return E_INVALIDARG;
        }
        if (!RedXeAsciiEqualsIgnoreCase(typeId, kWidgetTypeId))
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
        wil::com_ptr_nothrow<IRedXeWidgetProvider> providerOwner;
        HRESULT result = QueryInterface(__uuidof(IRedXeWidgetProvider), reinterpret_cast<void**>(providerOwner.put()));
        if (FAILED(result))
        {
            return result;
        }
        auto* created = new (std::nothrow) WeatherWidget(std::move(providerOwner), _configuration, _host, instanceId);
        if (!created)
        {
            return E_OUTOFMEMORY;
        }
        *widget = static_cast<IRedXeWidget*>(created);
        return S_OK;
    }

  private:
    WeatherConfiguration _configuration;
    IRedXeHost* _host = nullptr;
};

HRESULT CreateWeatherProvider(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                              void** result) noexcept
{
    if (!result)
    {
        return E_POINTER;
    }
    *result = nullptr;
    if (interfaceId != __uuidof(IRedXeWidgetProvider))
    {
        return E_NOINTERFACE;
    }
    WeatherConfiguration configuration{};
    if (options)
    {
        if (options->sizeBytes != sizeof(RedXeFactoryOptions))
        {
            return E_INVALIDARG;
        }
        const HRESULT parsed =
            WeatherReadConfiguration(options->configurationJsonUtf8
                                         ? std::string_view(options->configurationJsonUtf8, options->configurationBytes)
                                         : std::string_view{},
                                     configuration);
        if (FAILED(parsed))
        {
            return parsed;
        }
    }
    auto* provider = new (std::nothrow) WeatherProvider(configuration, host);
    if (!provider)
    {
        return E_OUTOFMEMORY;
    }
    *result = static_cast<IRedXeWidgetProvider*>(provider);
    return S_OK;
}

constexpr std::array kFactoryEntries{
    RedXeFactoryEntry{&kMetadata[0], CreateWeatherProvider},
};
} // namespace

extern "C" HRESULT __stdcall RedXeCreate(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                                         const char* pluginId, void** result) noexcept
{
    return RedXeCreateFromFactoryEntries(kFactoryEntries.data(), static_cast<uint32_t>(kFactoryEntries.size()),
                                         interfaceId, options, host, pluginId, result);
}

extern "C" HRESULT __stdcall RedXeEnumeratePlugins(const RedXePluginMetadata** metadata, uint32_t* count) noexcept
{
    return RedXeEnumerateFactoryMetadata(kMetadata.data(), static_cast<uint32_t>(kMetadata.size()), metadata, count);
}

extern "C" HRESULT __stdcall RedXeGetPluginSettingsContract(const char* pluginId,
                                                            const RedXePluginSettingsContract** contract) noexcept
{
    return RedXeGetStaticPluginSettingsContract(kPluginId, pluginId, &kSettingsContract, contract);
}

extern "C" void __stdcall RedXePluginShutdown() noexcept
{
    WeatherHttpShutdown();
}

extern "C" HRESULT __stdcall RedXeWeatherGetTestDiagnostics(WeatherTestDiagnostics* diagnostics) noexcept
{
    if (!diagnostics)
    {
        return E_POINTER;
    }
    if (diagnostics->sizeBytes != sizeof(WeatherTestDiagnostics))
    {
        return E_INVALIDARG;
    }
    diagnostics->liveProviderCount = gLiveProviderCount.load(std::memory_order_relaxed);
    diagnostics->liveWidgetCount = gLiveWidgetCount.load(std::memory_order_relaxed);
    diagnostics->paintCount = gPaintCount.load(std::memory_order_relaxed);
    diagnostics->networkWorkCount = gNetworkWorkCount.load(std::memory_order_relaxed);
    diagnostics->httpGetCount = WeatherHttpGetCount();
    diagnostics->httpResponseSizeBytes = static_cast<uint32_t>(sizeof(WeatherHttpResponse));
    diagnostics->lastDelayMilliseconds = gLastDelay.load(std::memory_order_relaxed);
    diagnostics->dailyCount = gLastDailyCount.load(std::memory_order_relaxed);
    diagnostics->alertCount = gLastAlertCount.load(std::memory_order_relaxed);
    diagnostics->lastStatus = gLastStatus.load(std::memory_order_relaxed);
    diagnostics->lastTemperatureCelsius = gLastTemperature.load(std::memory_order_relaxed);
    AcquireSRWLockShared(&gLastLocationLock);
    WeatherCopyWide(gLastLocation.data(), diagnostics->lastLocation, 64);
    ReleaseSRWLockShared(&gLastLocationLock);
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeWeatherApplyTestSnapshot(const WeatherTestSnapshot* snapshot) noexcept
{
    if (!snapshot || snapshot->sizeBytes != sizeof(WeatherTestSnapshot))
    {
        return E_INVALIDARG;
    }
    WeatherSnapshot parsed{};
    if (snapshot->nominatimJson && snapshot->nominatimBytes != 0)
    {
        (void)WeatherParseNominatim(std::string_view(snapshot->nominatimJson, snapshot->nominatimBytes), parsed);
    }
    if (snapshot->locationForecastJson && snapshot->locationForecastBytes != 0)
    {
        const HRESULT result = WeatherParseLocationForecast(
            std::string_view(snapshot->locationForecastJson, snapshot->locationForecastBytes), parsed);
        if (FAILED(result))
        {
            return result;
        }
    }
    if (snapshot->sunriseJson && snapshot->sunriseBytes != 0)
    {
        (void)WeatherParseSunrise(std::string_view(snapshot->sunriseJson, snapshot->sunriseBytes), parsed);
    }
    if (snapshot->nwsJson && snapshot->nwsBytes != 0)
    {
        (void)WeatherParseNwsAlerts(std::string_view(snapshot->nwsJson, snapshot->nwsBytes), parsed);
    }
    else if (snapshot->meteoAlarmJson && snapshot->meteoAlarmBytes != 0)
    {
        (void)WeatherParseMeteoAlarm(std::string_view(snapshot->meteoAlarmJson, snapshot->meteoAlarmBytes), parsed);
    }
    AcquireSRWLockExclusive(&gTestSnapshotLock);
    gTestSnapshot = parsed;
    gUseTestSnapshot.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&gTestSnapshotLock);
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeWeatherParseTestForecast(const char* json, uint32_t bytes, float* temperatureCelsius,
                                                           uint32_t* dailyCount) noexcept
{
    if (!json || !temperatureCelsius || !dailyCount)
    {
        return E_POINTER;
    }
    WeatherSnapshot snapshot{};
    const HRESULT result = WeatherParseLocationForecast(std::string_view(json, bytes), snapshot);
    *temperatureCelsius = snapshot.temperatureCelsius;
    *dailyCount = snapshot.dailyCount;
    return result;
}

extern "C" HRESULT __stdcall RedXeWeatherParseTestAlerts(const char* json, uint32_t bytes, BOOL unitedStates,
                                                         uint32_t* alertCount, uint32_t* highestSeverity) noexcept
{
    if (!json || !alertCount || !highestSeverity)
    {
        return E_POINTER;
    }
    WeatherSnapshot snapshot{};
    const HRESULT result = unitedStates ? WeatherParseNwsAlerts(std::string_view(json, bytes), snapshot)
                                        : WeatherParseMeteoAlarm(std::string_view(json, bytes), snapshot);
    *alertCount = snapshot.alertCount;
    *highestSeverity = static_cast<uint32_t>(WeatherHighestAlertSeverity(snapshot));
    return result;
}

extern "C" HRESULT __stdcall RedXeWeatherFormatTestUnits(float celsius, float metersPerSecond, BOOL fahrenheit,
                                                         BOOL milesPerHour, wchar_t* temperature,
                                                         uint32_t temperatureCapacity, wchar_t* wind,
                                                         uint32_t windCapacity) noexcept
{
    if (!temperature || !wind)
    {
        return E_POINTER;
    }
    (void)WeatherFormatTemperature(celsius,
                                   fahrenheit ? WeatherTemperatureUnit::Fahrenheit : WeatherTemperatureUnit::Celsius,
                                   temperature, temperatureCapacity);
    (void)WeatherFormatWind(metersPerSecond,
                            milesPerHour ? WeatherWindUnit::MilesPerHour : WeatherWindUnit::KilometersPerHour, wind,
                            windCapacity);
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeWeatherFormatTestClockAndDay(uint64_t fileTime100ns, uint64_t nowFileTime100ns,
                                                               wchar_t* clock, uint32_t clockCapacity, wchar_t* day,
                                                               uint32_t dayCapacity) noexcept
{
    if (!clock || !day)
    {
        return E_POINTER;
    }
    (void)WeatherFormatClock(fileTime100ns, clock, clockCapacity);
    (void)WeatherFormatForecastDay(fileTime100ns, nowFileTime100ns, 0, day, dayCapacity);
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeWeatherParseTestIso8601(const char* text, uint64_t* fileTime100ns) noexcept
{
    if (!text || !fileTime100ns)
    {
        return E_POINTER;
    }
    return WeatherParseIso8601Utc(text, *fileTime100ns) ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

extern "C" HRESULT __stdcall RedXeWeatherConditionIntentColor(uint32_t condition, float* red, float* green,
                                                              float* blue) noexcept
{
    if (!red || !green || !blue)
    {
        return E_POINTER;
    }
    const WeatherRgb color = WeatherConditionColor(static_cast<WeatherCondition>(condition));
    *red = color.red;
    *green = color.green;
    *blue = color.blue;
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeWeatherParseTestLatLon(const char* text, double* latitude, double* longitude) noexcept
{
    if (!text || !latitude || !longitude)
    {
        return E_POINTER;
    }
    return WeatherParseLatLon(text, *latitude, *longitude) ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

extern "C" HRESULT __stdcall RedXeWeatherBuildTestLocationSearchUrl(const char* location, char* url,
                                                                   uint32_t capacity) noexcept
{
    return WeatherBuildLocationSearchUrl(location ? std::string_view(location) : std::string_view{}, url, capacity);
}

extern "C" HRESULT __stdcall RedXeWeatherAlertRegionForCountry(const char* country, uint32_t* region) noexcept
{
    if (!country || !region)
    {
        return E_POINTER;
    }
    *region = static_cast<uint32_t>(WeatherAlertRegionFromCountry(country));
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeWeatherTryAutomaticLocation(wchar_t* name, uint32_t nameCapacity, char* country,
                                                              uint32_t countryCapacity, double* latitude,
                                                              double* longitude) noexcept
{
    if (!name || !country || !latitude || !longitude)
    {
        return E_POINTER;
    }
    return WeatherTryAutomaticLocation(name, nameCapacity, country, countryCapacity, *latitude, *longitude)
               ? S_OK
               : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

namespace
{
struct HttpStackProbeContext final
{
    HRESULT result = E_FAIL;
};

DWORD WINAPI HttpStackProbeThread(void* parameter) noexcept
{
    auto* context = static_cast<HttpStackProbeContext*>(parameter);
    HANDLE cancel = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    if (!cancel)
    {
        context->result = HRESULT_FROM_WIN32(GetLastError());
        return 0;
    }
    WeatherHttpResponse response{};
    context->result = WeatherHttpGet("https://example.invalid/redxe-http-stack-probe", cancel, response);
    CloseHandle(cancel);
    return 0;
}
} // namespace

extern "C" HRESULT __stdcall RedXeWeatherProbeHttpGetOnSmallStack(uint32_t stackReserveBytes) noexcept
{
    if (stackReserveBytes < 64U * 1024U || stackReserveBytes > 1024U * 1024U)
    {
        return E_INVALIDARG;
    }
    HttpStackProbeContext context{};
    HANDLE thread = CreateThread(nullptr, stackReserveBytes, HttpStackProbeThread, &context,
                                 STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!thread)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const DWORD wait = WaitForSingleObject(thread, 15'000);
    CloseHandle(thread);
    if (wait != WAIT_OBJECT_0)
    {
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    return context.result;
}
