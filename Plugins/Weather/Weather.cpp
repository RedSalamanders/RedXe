#define REDXE_PLUGIN_EXPORTS
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Host.h"
#include "PlugInterfaces/Widget.h"
#include "WeatherGpu.h"
#include "WeatherHttp.h"
#include "WeatherIcons.h"
#include "WeatherLocation.h"
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
constexpr float kTextR = 0.90f;
constexpr float kTextG = 0.90f;
constexpr float kTextB = 0.92f;
constexpr float kPanelInset = 4.0f;
constexpr float kPadFloorPx = 12.0f;

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
std::atomic<uint32_t> gDeviceCallbacksWhileVisible{0};
std::atomic<uint32_t> gNetworkWorkCount{0};
std::atomic<uint32_t> gLastDelay{0};
std::atomic<uint32_t> gLastStatus{RedXeWidgetStatusInitializing};
std::atomic<float> gLastTemperature{0.0f};
std::atomic<uint32_t> gLastDailyCount{0};
std::atomic<uint32_t> gLastAlertCount{0};
std::atomic<uint32_t> gLastHourlyDrawn{0};
std::atomic<uint32_t> gLastDailyDrawn{0};
std::atomic<uint32_t> gLastOverflowCount{0};
std::atomic<bool> gLastPrecipitationNotice{false};
std::atomic<uint64_t> gTestNow{0};
std::atomic<uint32_t> gTestHelperMode{0};
std::atomic<uint32_t> gLocationHelperRuns{0};
std::array<wchar_t, 64> gLastLocation{};
SRWLOCK gLastLocationLock = SRWLOCK_INIT;
std::array<wchar_t, 64> gLastAttribution{}; // Protected by gLastLocationLock; test diagnostics only.
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

// Keep complete glyphs at the requested readable size; truncate only the end of a label.
void AppendTextFit(WeatherGpuResources& resources, WeatherDrawList& list, float x, float y, float width, float height,
                   const wchar_t* text, float alpha = 1.0f, float red = kTextR, float green = kTextG,
                   float blue = kTextB) noexcept
{
    const uint32_t count = WideCount(text);
    if (width <= 0.0f || count == 0)
        return;
    if (resources.MeasureText(text, count, height) <= width + 0.05f)
    {
        AppendTextLeft(resources, list, x, y, height, red, green, blue, alpha, text, count);
        return;
    }
    std::array<wchar_t, 192> shortened{};
    uint32_t keep = std::min(count, static_cast<uint32_t>(shortened.size()) - 4);
    while (keep > 0)
    {
        std::memcpy(shortened.data(), text, keep * sizeof(wchar_t));
        shortened[keep] = shortened[keep + 1] = shortened[keep + 2] = L'.';
        shortened[keep + 3] = L'\0';
        if (resources.MeasureText(shortened.data(), keep + 3, height) <= width)
        {
            AppendTextLeft(resources, list, x, y, height, red, green, blue, alpha, shortened.data(), keep + 3);
            return;
        }
        --keep;
    }
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

void DrawConditionIconIn(WeatherGpuResources& resources, WeatherDrawList& list, float x, float y, float width,
                         float height, WeatherCondition condition, const WeatherRgb& color) noexcept
{
    const float red = color.red;
    const float green = color.green;
    const float blue = color.blue;
    if (resources.AppendIconFit(list, x, y, width, height, WeatherIconForCondition(condition), red, green, blue,
                                1.0f) == S_OK)
    {
        return;
    }
    const float size = std::min(width, height);
    x += (width - size) * 0.5f;
    y += (height - size) * 0.5f;
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

void DrawConditionIcon(WeatherGpuResources& resources, WeatherDrawList& list, float x, float y, float size,
                       WeatherCondition condition, const WeatherRgb& color) noexcept
{
    DrawConditionIconIn(resources, list, x, y, size, size, condition, color);
}

void DrawWarningMark(WeatherGpuResources& resources, WeatherDrawList& list, float x, float y, float size,
                     const WeatherRgb& color) noexcept
{
    if (resources.AppendIcon(list, x, y, size, kWeatherIconStormWarning, color.red, color.green, color.blue, 1.0f) ==
        S_OK)
    {
        return;
    }
    const float bangH = size * 0.72f;
    const float bangW = resources.MeasureText(L"!", 1, bangH);
    AppendTextLeft(resources, list, x + std::max(0.0f, (size - bangW) * 0.5f), y + (size - bangH) * 0.5f, bangH,
                   color.red, color.green, color.blue, 1.0f, L"!", 1);
}

void DrawNoticeBanner(WeatherGpuResources& resources, WeatherDrawList& list, float left, float y, float width,
                      float height, const WeatherRgb& color, WeatherCondition condition, bool officialAlert,
                      const wchar_t* text) noexcept
{
    if (width <= 0.0f || height <= 0.0f)
        return;
    (void)list.AddFill(left, y, width, height, color.red, color.green, color.blue, 0.16f, 8.0f);
    (void)list.AddFill(left, y, 6.0f, height, color.red, color.green, color.blue, 1.0f, 3.0f);
    const float badge = std::min({height - 8.0f, width < 360.0f ? 30.0f : 52.0f, 52.0f});
    const float badgeX = left + 14.0f;
    const float badgeY = y + (height - badge) * 0.5f;
    (void)list.AddFill(badgeX, badgeY, badge, badge, color.red, color.green, color.blue, 1.0f, badge * 0.22f);
    const WeatherRgb onBadge{0.08f, 0.08f, 0.10f};
    if (officialAlert)
        DrawWarningMark(resources, list, badgeX, badgeY, badge, onBadge);
    else
        DrawConditionIcon(resources, list, badgeX, badgeY, badge, condition, onBadge);
    const float textX = badgeX + badge + 14.0f;
    const float textW = std::max(1.0f, left + width - textX - 14.0f);
    const float textH = std::min(height - 12.0f, width >= 400.0f ? 36.0f : 28.0f);
    const uint32_t count = WideCount(text);
    if (resources.MeasureText(text, count, textH) <= textW + 0.05f)
    {
        AppendTextFit(resources, list, textX, y + (height - textH) * 0.5f, textW, textH, text, 1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }
    // A message that does not fit one line wraps once at a word boundary before the tail is ellipsized.
    constexpr float lineGap = 2.0f;
    const float lineH = std::min(textH, (height - 8.0f - lineGap) * 0.5f);
    uint32_t split = 0;
    if (lineH >= 18.0f)
    {
        for (uint32_t index = 1; index + 1 < count; ++index)
        {
            if (text[index] != L' ')
                continue;
            if (resources.MeasureText(text, index, lineH) > textW + 0.05f)
                break;
            split = index;
        }
    }
    if (split == 0)
    {
        AppendTextFit(resources, list, textX, y + (height - textH) * 0.5f, textW, textH, text, 1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }
    const float top = y + (height - 2.0f * lineH - lineGap) * 0.5f;
    AppendTextLeft(resources, list, textX, top, lineH, 1.0f, 1.0f, 1.0f, 1.0f, text, split);
    AppendTextFit(resources, list, textX, top + lineH + lineGap, textW, lineH, text + split + 1, 1.0f, 1.0f, 1.0f,
                  1.0f);
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
                                                  IRedXeRaisedWidget, IRedXeNetworkWidget, IRedXeInteractiveWidget>
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
        if (!writtenBytes)
            return E_POINTER;
        *writtenBytes = 0;
        const auto guard = wil::AcquireSRWLockShared(&_lock);
        if (_locationSettingsBytes == 0)
            return S_FALSE;
        if (!jsonUtf8 || capacityBytes <= _locationSettingsBytes)
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        std::memcpy(jsonUtf8, _locationSettings.data(), _locationSettingsBytes + 1);
        *writtenBytes = _locationSettingsBytes;
        return S_OK;
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

    HRESULT STDMETHODCALLTYPE OnPointer(const RedXePointerEvent* event) noexcept override
    {
        if (!event)
        {
            return E_POINTER;
        }
        if (event->sizeBytes != sizeof(RedXePointerEvent))
        {
            return E_INVALIDARG;
        }
        const uint32_t pageCount = (std::max)(1u, _pageCount.load(std::memory_order_relaxed));
        if (event->phase == RedXePointerPhaseCancel)
        {
            _pointerDown = false;
            _pagePan = false;
            return S_FALSE;
        }
        if (event->phase == RedXePointerPhaseWheel)
        {
            if (pageCount <= 1 || event->wheelDelta == 0.0f)
            {
                return S_FALSE;
            }
            uint32_t page = _overflowPage.load(std::memory_order_relaxed);
            if (event->wheelDelta < 0.0f)
            {
                if (page + 1 < pageCount)
                {
                    _overflowPage.store(page + 1, std::memory_order_relaxed);
                }
            }
            else if (page > 0)
            {
                _overflowPage.store(page - 1, std::memory_order_relaxed);
            }
            if (_host)
            {
                (void)_host->RequestFrame();
            }
            return S_OK;
        }
        if (event->phase == RedXePointerPhaseDown)
        {
            _pointerDown = true;
            _pagePan = false;
            _pointerStartX = event->x;
            _pointerStartY = event->y;
            return S_FALSE;
        }
        if (event->phase == RedXePointerPhaseMove)
        {
            if (!_pointerDown || pageCount <= 1)
            {
                return S_FALSE;
            }
            const float threshold = 48.0f * static_cast<float>(event->dpi ? event->dpi : 96) / 96.0f;
            const float dx = event->x - _pointerStartX;
            const float dy = event->y - _pointerStartY;
            if (!_pagePan && (std::fabs(dx) >= threshold || std::fabs(dy) >= threshold))
            {
                _pagePan = true;
            }
            return _pagePan ? S_OK : S_FALSE;
        }
        if (event->phase == RedXePointerPhaseUp)
        {
            const bool panned = _pagePan;
            const float dx = event->x - _pointerStartX;
            const float dy = event->y - _pointerStartY;
            _pointerDown = false;
            _pagePan = false;
            if (!panned || pageCount <= 1)
            {
                return S_FALSE;
            }
            uint32_t page = _overflowPage.load(std::memory_order_relaxed);
            const float delta = std::fabs(dx) >= std::fabs(dy) ? dx : dy;
            if (delta < 0.0f && page + 1 < pageCount)
            {
                _overflowPage.store(page + 1, std::memory_order_relaxed);
            }
            else if (delta > 0.0f && page > 0)
            {
                _overflowPage.store(page - 1, std::memory_order_relaxed);
            }
            if (_host)
            {
                (void)_host->RequestFrame();
            }
            return S_OK;
        }
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE OnDragOver(float, float) noexcept override
    {
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE OnDragLeave() noexcept override
    {
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE OnDrop(const RedXeDropEvent*) noexcept override
    {
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceCreated(const RedXeGpuDeviceContext* context) noexcept override
    {
        if (_visible.load(std::memory_order_acquire))
            gDeviceCallbacksWhileVisible.fetch_add(1, std::memory_order_relaxed);
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
        if (_visible.load(std::memory_order_acquire))
            gDeviceCallbacksWhileVisible.fetch_add(1, std::memory_order_relaxed);
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
            uint32_t overflow = 0;
            for (uint32_t index = 0; gTestNow.load(std::memory_order_relaxed) != 0 && index < list.Count(); ++index)
            {
                const auto& rect = list.Data()[index].rect;
                if (rect[0] < -0.1f || rect[1] < -0.1f || rect[0] + rect[2] > frame.widthPixels + 0.1f ||
                    rect[1] + rect[3] > frame.heightPixels + 0.1f)
                    ++overflow;
            }
            gLastOverflowCount.store(overflow, std::memory_order_relaxed);
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
        if (_locationResolved)
        {
            snapshot.latitude = _resolvedLatitude;
            snapshot.longitude = _resolvedLongitude;
            snapshot.locationName = _resolvedCity;
            snapshot.countryCode = _resolvedCountry;
            snapshot.hasCoordinates = true;
        }
        if (!snapshot.hasCoordinates && _configuration.location[0] != '\0')
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
                    ReportFetchFailure(L"Location lookup failed.");
                    return fetch;
                }
                delay = std::max(delay, http.expiresDelayMilliseconds);
                if (FAILED(WeatherParseNominatim(WeatherHttpBody(http), snapshot)))
                {
                    ReportFetchFailure(L"Location was not found.");
                    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
                }
            }
        }
        if (!snapshot.hasCoordinates)
        {
            if (!_locationAttempted)
            {
                gLocationHelperRuns.fetch_add(1, std::memory_order_relaxed);
                const uint32_t mode = gTestHelperMode.load(std::memory_order_relaxed);
                _locationResult = WeatherLocateWithHelper(cancelEvent, snapshot.latitude, snapshot.longitude,
                                                          mode == 1   ? L"--test-result"
                                                          : mode == 2 ? L"--test-unavailable"
                                                          : mode == 3 ? L"--test-coarse"
                                                          : mode == 4 ? L"--test-cached"
                                                          : mode == 5 ? L"--test-default"
                                                                      : nullptr);
                _locationAttempted = _locationResult != HRESULT_FROM_WIN32(ERROR_CANCELLED);
            }
            if (FAILED(_locationResult))
            {
                ReportFetchFailure(L"Location unavailable. Enter a city in widget settings.");
                return _locationResult;
            }
            snapshot.hasCoordinates = true;
        }

        WeatherHttpResponse http{};
        if (snapshot.hasCoordinates && snapshot.locationName[0] == L'\0')
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

        const bool firstResolution = !_locationResolved;
        _resolvedLatitude = snapshot.latitude;
        _resolvedLongitude = snapshot.longitude;
        _resolvedCity = snapshot.locationName;
        _resolvedCountry = snapshot.countryCode;
        _locationResolved = true;
        if (firstResolution && _configuration.location[0] == '\0')
        {
            std::array<char, 1024> json{};
            uint32_t written = 0;
            if (SUCCEEDED(
                    WeatherWriteLocationSettings(snapshot, json.data(), static_cast<uint32_t>(json.size()), written)))
            {
                {
                    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
                    _locationSettings = json;
                    _locationSettingsBytes = written;
                }
                wil::com_ptr_nothrow<IRedXeSettingsQueue> queue;
                if (_host && SUCCEEDED(_host->QueryInterface(IID_PPV_ARGS(queue.put()))))
                    (void)queue->QueueWidgetSettings(_instanceId.data(), json.data(), written);
            }
        }

        std::array<char, kWeatherMaximumUrlBytes> forecastUrl{};
        sprintf_s(forecastUrl.data(), forecastUrl.size(),
                  "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=%.4f&lon=%.4f", snapshot.latitude,
                  snapshot.longitude);
        HRESULT fetch = WeatherHttpGet(forecastUrl.data(), cancelEvent, http);
        if (FAILED(fetch))
        {
            ReportFetchFailure(L"Forecast request failed.");
            return fetch;
        }
        delay = std::max(delay, http.expiresDelayMilliseconds);
        fetch = WeatherParseLocationForecast(WeatherHttpBody(http), snapshot);
        if (FAILED(fetch))
        {
            ReportFetchFailure(L"Forecast data was unusable.");
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

        // At most one alert document per refresh, chosen by the reverse-geocoded country (Plugins_Weather.md).
        const WeatherAlertRegion region = WeatherAlertRegionFromCountry(snapshot.countryCode.data());
        std::array<char, kWeatherMaximumUrlBytes> alertUrl{};
        if (WeatherBuildAlertUrl(region, snapshot.countryCode.data(), snapshot.latitude, snapshot.longitude,
                                 alertUrl.data(), static_cast<uint32_t>(alertUrl.size())) == S_OK &&
            SUCCEEDED(WeatherHttpGet(alertUrl.data(), cancelEvent, http)))
        {
            delay = std::max(delay, http.expiresDelayMilliseconds);
            (void)WeatherParseAlerts(region, WeatherHttpBody(http), snapshot);
        }
        return ApplySnapshot(snapshot, L"", false);
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

    // A failed refresh keeps the last good snapshot on screen as a cached forecast (Degraded). The tile only hands
    // itself to the host placeholder (Unavailable) while nothing has ever been drawn.
    void ReportFetchFailure(const wchar_t* reason) noexcept
    {
        AcquireSRWLockExclusive(&_lock);
        const bool cached = _hasSnapshot;
        _snapshot.stale = cached;
        ReleaseSRWLockExclusive(&_lock);
        ReportStatus(_host, _instanceId.data(), cached ? RedXeWidgetStatusDegraded : RedXeWidgetStatusUnavailable,
                     reason);
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
        gLastHourlyDrawn.store(0, std::memory_order_relaxed);
        gLastDailyDrawn.store(0, std::memory_order_relaxed);
        gLastPrecipitationNotice.store(false, std::memory_order_relaxed);
        _pageCount.store(1, std::memory_order_relaxed);
        const float inset = kPanelInset;
        (void)list.AddFill(inset, inset, width - 2 * inset, height - 2 * inset, _configuration.panelColor.red,
                           _configuration.panelColor.green, _configuration.panelColor.blue, 1.0f, 10.0f);
        if (width < 48.0f || height < 40.0f)
            return S_OK;
        const auto severity = WeatherHighestAlertSeverity(snapshot);
        const auto alertColor = WeatherAlertColor(severity);
        if (severity != WeatherAlertSeverity::None)
            (void)list.AddFill(inset, inset, 4.0f, height - 2 * inset, alertColor.red, alertColor.green,
                               alertColor.blue, 1.0f, 0.0f);
        const float left = inset + kPadFloorPx + (severity != WeatherAlertSeverity::None ? 6.0f : 0.0f);
        const float right = width - inset - kPadFloorPx;
        const float available = right - left;
        const float bottom = height - inset - kPadFloorPx;
        float y = inset + kPadFloorPx;
        const bool raised = _raised.load(std::memory_order_acquire);
        const auto density = DensityForSize(width, height, raised);
        if (!hasSnapshot)
        {
            AppendTextFit(resources, list, left, y, available, std::min(30.0f, bottom - y), L"Weather");
            if (bottom - y >= 62.0f)
                AppendTextFit(resources, list, left, y + 34.0f, available, 22.0f, L"Set a city in widget settings",
                              0.7f);
            return S_OK;
        }
        FILETIME fileTime{};
        GetSystemTimeAsFileTime(&fileTime);
        const uint64_t testNow = gTestNow.load(std::memory_order_relaxed);
        const uint64_t now =
            testNow != 0 ? testNow : (static_cast<uint64_t>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
        std::array<wchar_t, 32> temperature{}, range{}, wind{};
        (void)WeatherFormatTemperature(snapshot.temperatureCelsius, _configuration.temperatureUnit, temperature.data(),
                                       static_cast<uint32_t>(temperature.size()));
        (void)WeatherFormatWind(snapshot.windMetersPerSecond, _configuration.windUnit, wind.data(),
                                static_cast<uint32_t>(wind.size()));
        std::array<wchar_t, 96> precipitation{};
        const bool havePrecipitation =
            WeatherFormatPrecipitationNotice(snapshot, now, precipitation.data(),
                                             static_cast<uint32_t>(precipitation.size())) != 0;
        const auto conditionColor = MixIconColor(snapshot.currentCondition);

        if (density == WeatherDensity::Tiny || height < 180.0f)
        {
            const bool locationFits = height >= 100.0f;
            if (locationFits)
            {
                AppendTextFit(resources, list, left, y, available, 22.0f, snapshot.locationName.data());
                y += 25.0f;
            }
            const float icon = std::min(52.0f, bottom - y);
            const float textSpace = std::max(1.0f, available - icon - 8.0f);
            const float measured = resources.MeasureText(temperature.data(), WideCount(temperature.data()), 1.0f);
            const float hero = std::min({58.0f, bottom - y, textSpace / std::max(0.01f, measured)});
            AppendTextFit(resources, list, left, y, textSpace, hero, temperature.data());
            DrawConditionIcon(resources, list, right - icon, y, icon, snapshot.currentCondition, conditionColor);
            y += std::max(hero, icon) + 4.0f;
            if (havePrecipitation && bottom - y >= 22.0f)
            {
                uint32_t noticeHour = 0;
                const WeatherCondition noticeCondition = WeatherSelectPrecipitationNotice(snapshot, now, noticeHour)
                                                             ? snapshot.hourly[noticeHour].condition
                                                             : WeatherCondition::Rain;
                const float mark = 22.0f;
                DrawConditionIcon(resources, list, left, y, mark, noticeCondition,
                                  WeatherAlertColor(WeatherAlertSeverity::Yellow));
                AppendTextFit(resources, list, left + mark + 8.0f, y, std::max(1.0f, available - mark - 8.0f), 22.0f,
                              precipitation.data());
                gLastPrecipitationNotice.store(true, std::memory_order_relaxed);
            }
            return S_OK;
        }

        const float labelH = width >= 400.0f ? 32.0f : 26.0f;
        AppendTextFit(resources, list, left, y, available, labelH, snapshot.locationName.data());
        y += labelH + 4.0f;
        const float headerH = std::clamp(height * 0.18f, 62.0f, 112.0f);
        const float heroH = std::min(104.0f, headerH);
        constexpr float headerGap = 12.0f;
        constexpr float minIcon = 52.0f;
        constexpr float maxIcon = 80.0f;
        std::array<wchar_t, 8> rise{}, set{};
        (void)WeatherFormatClock(snapshot.sunriseFileTime100ns, rise.data(), static_cast<uint32_t>(rise.size()));
        (void)WeatherFormatClock(snapshot.sunsetFileTime100ns, set.data(), static_cast<uint32_t>(set.size()));
        constexpr float sunH = 24.0f;
        const float sunWidth = sunH + 8.0f +
                               std::max(resources.MeasureText(rise.data(), WideCount(rise.data()), sunH),
                                        resources.MeasureText(set.data(), WideCount(set.data()), sunH));
        const float temperatureWidth = resources.MeasureText(temperature.data(), WideCount(temperature.data()), heroH);
        const bool showSun =
            (rise[0] != L'\0' || set[0] != L'\0') && available >= temperatureWidth + minIcon + sunWidth + 40.0f;
        const float sunReserve = showSun ? sunWidth + headerGap : 0.0f;
        const float maxTempWidth = std::max(1.0f, available - sunReserve - minIcon - headerGap);
        const float fittedHero = std::min(heroH, heroH * maxTempWidth / std::max(1.0f, temperatureWidth));
        const float fittedTempWidth = std::min(
            maxTempWidth, resources.MeasureText(temperature.data(), WideCount(temperature.data()), fittedHero));
        AppendTextFit(resources, list, left, y + (headerH - fittedHero) * 0.5f, fittedTempWidth, fittedHero,
                      temperature.data());
        const float iconLeft = left + fittedTempWidth + headerGap;
        const float iconRight = right - sunReserve;
        const float iconSpan = std::max(0.0f, iconRight - iconLeft);
        const float icon = std::min({maxIcon, headerH, iconSpan});
        const float iconX = iconLeft + std::max(0.0f, (iconSpan - icon) * 0.5f);
        DrawConditionIcon(resources, list, iconX, y + (headerH - icon) * 0.5f, icon, snapshot.currentCondition,
                          conditionColor);
        if (showSun)
        {
            const float sunX = right - sunWidth;
            const float sunY = y + (headerH - 2 * sunH - 8.0f) * 0.5f;
            if (rise[0] != L'\0')
            {
                DrawHorizonSun(resources, list, sunX, sunY, sunH, kTextR, kTextG, kTextB, true);
                AppendTextFit(resources, list, sunX + sunH + 8.0f, sunY, sunWidth - sunH - 8.0f, sunH, rise.data(),
                              0.8f);
            }
            if (set[0] != L'\0')
            {
                DrawHorizonSun(resources, list, sunX, sunY + sunH + 8.0f, sunH, kTextR, kTextG, kTextB, false);
                AppendTextFit(resources, list, sunX + sunH + 8.0f, sunY + sunH + 8.0f, sunWidth - sunH - 8.0f, sunH,
                              set.data(), 0.8f);
            }
        }
        y += headerH + 4.0f;
        std::array<wchar_t, 64> today{};
        for (uint32_t index = 0; index < snapshot.dailyCount; ++index)
        {
            const auto& day = snapshot.daily[index];
            if (!WeatherSameLocalDay(day.dayFileTime100ns, now))
                continue;
            (void)WeatherFormatTemperatureRange(day.minimumCelsius, day.maximumCelsius, _configuration.temperatureUnit,
                                                range.data(), static_cast<uint32_t>(range.size()));
            (void)swprintf_s(today.data(), today.size(), L"Today   %s", range.data());
            break;
        }
        const float metaH = width >= 400.0f ? 30.0f : 24.0f;
        const float windWidth = resources.MeasureText(wind.data(), WideCount(wind.data()), metaH) + metaH + 8.0f;
        const float todayWidth = resources.MeasureText(today.data(), WideCount(today.data()), metaH);
        const bool showWind = todayWidth + windWidth + 24.0f <= available;
        AppendTextFit(resources, list, left, y, showWind ? available - windWidth - 24.0f : available, metaH,
                      today.data(), 0.8f);
        if (showWind)
        {
            DrawWindMark(resources, list, right - windWidth, y, metaH, kTextR, kTextG, kTextB);
            AppendTextFit(resources, list, right - windWidth + metaH + 8.0f, y, windWidth - metaH - 8.0f, metaH,
                          wind.data(), 0.8f);
        }
        y += metaH + 6.0f;
        constexpr float footerH = 18.0f;
        const float contentBottom = bottom - footerH - 12.0f;
        constexpr uint64_t hourTicks = 36000000000ULL;
        std::array<uint32_t, kWeatherHourlyCount> upcoming{};
        uint32_t upcomingCount = 0;
        for (uint32_t index = 0; index < snapshot.hourlyCount; ++index)
            if (snapshot.hourly[index].timeFileTime100ns + hourTicks > now &&
                snapshot.hourly[index].timeFileTime100ns < now + 24 * hourTicks)
                upcoming[upcomingCount++] = index;
        // Block sizes below are the 1.0 stretch. Leftover height grows the hourly body (bounded by its column width
        // so every temperature stays complete) and then the day rows, instead of leaving the tile bottom empty.
        constexpr float hoursHeadingH = 26.0f;
        constexpr float hoursBodyH = 106.0f;
        constexpr float hoursBlock = hoursHeadingH + hoursBodyH;
        constexpr float hoursColumnMin = 76.0f;
        constexpr float daysHeadingH = 28.0f;
        constexpr float maxStretch = 1.5f;
        const bool canHours = upcomingCount > 0 && available >= 210.0f;
        const uint32_t columnsFit = std::max(
            1u, std::min({std::max(1u, upcomingCount), 12U, static_cast<uint32_t>(available / hoursColumnMin)}));
        uint32_t eligibleDays = 0;
        for (uint32_t index = 0; index < snapshot.dailyCount; ++index)
        {
            const auto& day = snapshot.daily[index];
            if (day.dayFileTime100ns > now && !WeatherSameLocalDay(day.dayFileTime100ns, now))
                ++eligibleDays;
        }
        const float rowBaseH = width >= 400.0f ? 32.0f : 30.0f;
        const bool haveAlert = snapshot.alertCount > 0;
        uint32_t noticeHour = 0;
        const bool haveNoticeHour = WeatherSelectPrecipitationNotice(snapshot, now, noticeHour);
        const WeatherCondition noticeCondition =
            haveNoticeHour ? snapshot.hourly[noticeHour].condition : WeatherCondition::Rain;
        const WeatherRgb noticeColor = WeatherAlertColor(WeatherAlertSeverity::Yellow);
        constexpr float noticeMin = 56.0f;
        constexpr float noticeMax = 64.0f;
        constexpr float noticeGap = 6.0f;
        const float leftover = contentBottom - y;
        uint32_t noticeRows = 0;
        if (haveAlert && leftover >= noticeMin)
            ++noticeRows;
        if (havePrecipitation && leftover - static_cast<float>(noticeRows) * (noticeMin + noticeGap) >= noticeMin)
            ++noticeRows;
        const float noticeMinCost = static_cast<float>(noticeRows) * (noticeMin + noticeGap);
        const bool showHours = canHours && leftover - noticeMinCost >= hoursBlock;
        const float hoursCost = showHours ? hoursBlock : 0.0f;
        const float dayBudget = leftover - noticeMinCost - hoursCost;
        uint32_t daysFit = 0;
        if (dayBudget >= daysHeadingH + rowBaseH)
            daysFit = std::min(eligibleDays, static_cast<uint32_t>((dayBudget - daysHeadingH) / rowBaseH));
        const float daysCost = daysFit > 0 ? daysHeadingH + static_cast<float>(daysFit) * rowBaseH : 0.0f;
        float slack = std::max(0.0f, leftover - noticeMinCost - hoursCost - daysCost);
        const float extraNotice =
            noticeRows > 0 ? std::min(noticeMax - noticeMin, slack / static_cast<float>(noticeRows)) : 0.0f;
        const float noticeH = noticeMin + extraNotice;
        slack -= extraNotice * static_cast<float>(noticeRows);
        float hoursStretch = 1.0f;
        if (showHours)
        {
            const float columnWidth = available / static_cast<float>(columnsFit);
            hoursStretch =
                std::clamp(std::min(1.0f + slack / hoursBodyH, columnWidth / hoursColumnMin), 1.0f, maxStretch);
            slack -= (hoursStretch - 1.0f) * hoursBodyH;
        }
        float rowStretch = 1.0f;
        if (daysFit > 0)
            rowStretch = std::clamp(1.0f + slack / (static_cast<float>(daysFit) * rowBaseH), 1.0f, maxStretch);
        const float rowH = rowBaseH * rowStretch;
        if (haveAlert && leftover >= noticeMin)
        {
            DrawNoticeBanner(resources, list, left, y, available, noticeH, alertColor, WeatherCondition::Unknown, true,
                             snapshot.alerts[0].title.data());
            y += noticeH + noticeGap;
        }
        if (havePrecipitation && contentBottom - y >= noticeMin)
        {
            DrawNoticeBanner(resources, list, left, y, available, noticeH, noticeColor, noticeCondition, false,
                             precipitation.data());
            y += noticeH + noticeGap;
            gLastPrecipitationNotice.store(true, std::memory_order_relaxed);
        }

        uint32_t hidden = 0;
        uint32_t hourPages = 1;
        if (showHours)
        {
            AppendTextFit(resources, list, left, y, available, 22.0f, L"Upcoming hours", 0.65f);
            y += hoursHeadingH;
            const float s = hoursStretch;
            hourPages = (upcomingCount + columnsFit - 1) / columnsFit;
            uint32_t page = _overflowPage.load(std::memory_order_relaxed);
            if (hourPages > 1)
            {
                if (page >= hourPages)
                {
                    page = hourPages - 1;
                    _overflowPage.store(page, std::memory_order_relaxed);
                }
                _pageCount.store(hourPages, std::memory_order_relaxed);
            }
            const uint32_t hourStart = hourPages > 1 ? page * columnsFit : 0;
            const uint32_t columns = std::min(columnsFit, upcomingCount - hourStart);
            hidden += upcomingCount - hourStart - columns;
            const float columnWidth = available / static_cast<float>(columns);
            for (uint32_t column = 0; column < columns; ++column)
            {
                const auto& hour = snapshot.hourly[upcoming[hourStart + column]];
                std::array<wchar_t, 16> time{}, value{}, amount{};
                (void)WeatherFormatClock(hour.timeFileTime100ns, time.data(), static_cast<uint32_t>(time.size()));
                if (hour.timeFileTime100ns <= now)
                    WeatherCopyWide(L"Now", time.data(), time.size());
                (void)WeatherFormatTemperature(hour.temperatureCelsius, _configuration.temperatureUnit, value.data(),
                                               static_cast<uint32_t>(value.size()));
                const float x = left + column * columnWidth;
                const auto centered = [&](const wchar_t* text, float textY, float size, float alpha) noexcept
                {
                    const float measured = resources.MeasureText(text, WideCount(text), size);
                    AppendTextFit(resources, list, x + std::max(0.0f, (columnWidth - measured) * 0.5f), textY,
                                  std::min(columnWidth, measured), size, text, alpha);
                };
                centered(time.data(), y, 22.0f * s, 0.75f);
                const float hourIcon = 34.0f * s;
                DrawConditionIcon(resources, list, x + (columnWidth - hourIcon) * 0.5f, y + 24.0f * s, hourIcon,
                                  hour.condition, MixIconColor(hour.condition));
                centered(value.data(), y + 60.0f * s, 27.0f * s, 1.0f);
                if (hour.hasPrecipitation && hour.precipitationMillimeters > 0.0f)
                {
                    if (hour.precipitationMillimeters < 0.1f)
                        WeatherCopyWide(L"<0.1 mm", amount.data(), amount.size());
                    else
                        (void)swprintf_s(amount.data(), amount.size(), L"%.1f mm",
                                         static_cast<double>(hour.precipitationMillimeters));
                    centered(amount.data(), y + 87.0f * s, 18.0f * s, 0.65f);
                }
            }
            gLastHourlyDrawn.store(columns, std::memory_order_relaxed);
            y += hoursBodyH * s;
        }

        const float rowText = rowH - 6.0f;
        uint32_t drawn = 0;
        bool heading = false;
        uint32_t skipped = 0;
        if (hourPages <= 1 && daysFit > 0)
        {
            const uint32_t dayPages = (eligibleDays + daysFit - 1) / daysFit;
            uint32_t page = _overflowPage.load(std::memory_order_relaxed);
            if (page >= dayPages)
            {
                page = dayPages - 1;
                _overflowPage.store(page, std::memory_order_relaxed);
            }
            _pageCount.store((std::max)(_pageCount.load(std::memory_order_relaxed), dayPages),
                             std::memory_order_relaxed);
            skipped = page * daysFit;
        }
        uint32_t seenEligible = 0;
        for (uint32_t index = 0; index < snapshot.dailyCount && drawn < daysFit; ++index)
        {
            const auto& day = snapshot.daily[index];
            if (day.dayFileTime100ns <= now || WeatherSameLocalDay(day.dayFileTime100ns, now))
                continue;
            if (seenEligible++ < skipped)
                continue;
            if (contentBottom - y < rowH + (heading ? 0.0f : daysHeadingH) - 0.5f)
                break;
            if (!heading)
            {
                AppendTextFit(resources, list, left, y, available, 22.0f, L"Next days", 0.65f);
                y += daysHeadingH;
                heading = true;
            }
            std::array<wchar_t, 32> label{};
            (void)WeatherFormatForecastDay(day.dayFileTime100ns, now, index, label.data(),
                                           static_cast<uint32_t>(label.size()));
            (void)WeatherFormatTemperatureRange(day.minimumCelsius, day.maximumCelsius, _configuration.temperatureUnit,
                                                range.data(), static_cast<uint32_t>(range.size()));
            const float rangeWidth = resources.MeasureText(range.data(), WideCount(range.data()), rowText);
            const float mark = rowText;
            const float rangeX = right - mark - 16.0f - rangeWidth;
            AppendTextFit(resources, list, left, y, std::max(0.0f, rangeX - left - 16.0f), rowText, label.data());
            AppendTextFit(resources, list, rangeX, y, rangeWidth, rowText, range.data(), 0.85f);
            DrawConditionIcon(resources, list, right - mark, y, mark, day.condition, MixIconColor(day.condition));
            y += rowH;
            ++drawn;
        }
        gLastDailyDrawn.store(drawn, std::memory_order_relaxed);
        if (eligibleDays > skipped + drawn)
        {
            hidden += eligibleDays - skipped - drawn;
        }
        // Forecast source, then the body whose alerts are on screen, then the cached-forecast state. All ASCII, so
        // the static atlas already holds every glyph.
        const wchar_t* body = snapshot.alertCount > 0 ? WeatherAlertProviderName(snapshot.alertProvider) : L"";
        std::array<wchar_t, 64> attribution{};
        (void)swprintf_s(attribution.data(), attribution.size(), L"MET Norway%s%s%s", body[0] != L'\0' ? L"  |  " : L"",
                         body, snapshot.stale ? L"  |  Cached forecast" : L"");
        if (gTestNow.load(std::memory_order_relaxed) != 0)
        {
            AcquireSRWLockExclusive(&gLastLocationLock);
            gLastAttribution = attribution;
            ReleaseSRWLockExclusive(&gLastLocationLock);
        }
        if (hidden > 0)
        {
            wchar_t extra[16]{};
            (void)swprintf_s(extra, 16, L"+%u", hidden);
            const float extraW = resources.MeasureText(extra, WideCount(extra), footerH);
            AppendTextFit(resources, list, left, bottom - footerH, std::max(0.0f, available - extraW - 12.0f), footerH,
                          attribution.data(), 0.5f);
            AppendTextFit(resources, list, right - extraW, bottom - footerH, extraW, footerH, extra, 0.65f);
        }
        else
        {
            AppendTextFit(resources, list, left, bottom - footerH, available, footerH, attribution.data(), 0.5f);
        }
        return S_OK;
    }

    wil::com_ptr_nothrow<IRedXeWidgetProvider> _providerOwner;
    WeatherConfiguration _configuration;
    // Network-worker-owned location cache, without another copy of the forecast/alerts.
    double _resolvedLatitude = 0.0;
    double _resolvedLongitude = 0.0;
    std::array<wchar_t, kWeatherMaximumNameCharacters> _resolvedCity{};
    std::array<char, 8> _resolvedCountry{};
    bool _locationResolved = false;
    bool _locationAttempted = false;
    HRESULT _locationResult = E_PENDING;
    std::array<char, 1024> _locationSettings{}; // Protected by _lock; collected on the UI thread.
    uint32_t _locationSettingsBytes = 0;
    IRedXeHost* _host = nullptr;
    std::array<char, 128> _instanceId{};
    WeatherSnapshot _snapshot{};
    SRWLOCK _lock = SRWLOCK_INIT;
    std::atomic<bool> _visible{false};
    std::atomic<bool> _raised{false};
    std::atomic<uint32_t> _nextFrameDelay{kWeatherDefaultRefreshMilliseconds};
    bool _hasSnapshot = false;
    std::atomic<bool> _gpuHeld{false};
    std::atomic<uint32_t> _overflowPage{0};
    std::atomic<uint32_t> _pageCount{1};
    float _pointerStartX = 0.0f;
    float _pointerStartY = 0.0f;
    bool _pointerDown = false;
    bool _pagePan = false;
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
        const uint32_t rgb = RedXeBackgroundRgb(options);
        configuration.panelColor = {static_cast<float>((rgb >> 16U) & 0xFFU) / 255.0f,
                                    static_cast<float>((rgb >> 8U) & 0xFFU) / 255.0f,
                                    static_cast<float>(rgb & 0xFFU) / 255.0f};
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
    diagnostics->deviceCallbacksWhileVisible = gDeviceCallbacksWhileVisible.load(std::memory_order_relaxed);
    diagnostics->httpGetCount = WeatherHttpGetCount();
    diagnostics->httpResponseSizeBytes = static_cast<uint32_t>(sizeof(WeatherHttpResponse));
    diagnostics->lastDelayMilliseconds = gLastDelay.load(std::memory_order_relaxed);
    diagnostics->dailyCount = gLastDailyCount.load(std::memory_order_relaxed);
    diagnostics->alertCount = gLastAlertCount.load(std::memory_order_relaxed);
    diagnostics->lastStatus = gLastStatus.load(std::memory_order_relaxed);
    diagnostics->hourlyDrawn = gLastHourlyDrawn.load(std::memory_order_relaxed);
    diagnostics->dailyDrawn = gLastDailyDrawn.load(std::memory_order_relaxed);
    diagnostics->overflowingQuads = gLastOverflowCount.load(std::memory_order_relaxed);
    diagnostics->precipitationNotice = gLastPrecipitationNotice.load(std::memory_order_relaxed);
    diagnostics->locationHelperRuns = gLocationHelperRuns.load(std::memory_order_relaxed);
    diagnostics->heroTwinCells = WeatherGpuHeroTwinCount();
    diagnostics->iconCells = WeatherGpuIconCellCount();
    diagnostics->lastTemperatureCelsius = gLastTemperature.load(std::memory_order_relaxed);
    AcquireSRWLockShared(&gLastLocationLock);
    WeatherCopyWide(gLastLocation.data(), diagnostics->lastLocation, 64);
    WeatherCopyWide(gLastAttribution.data(), diagnostics->lastAttribution, 64);
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
    else if (snapshot->environmentCanadaJson && snapshot->environmentCanadaBytes != 0)
    {
        (void)WeatherParseEnvironmentCanadaAlerts(
            std::string_view(snapshot->environmentCanadaJson, snapshot->environmentCanadaBytes), parsed);
    }
    else if (snapshot->hongKongJson && snapshot->hongKongBytes != 0)
    {
        (void)WeatherParseHongKongWarnings(std::string_view(snapshot->hongKongJson, snapshot->hongKongBytes), parsed);
    }
    AcquireSRWLockExclusive(&gTestSnapshotLock);
    gTestSnapshot = parsed;
    gUseTestSnapshot.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&gTestSnapshotLock);
    return S_OK;
}

extern "C" void __stdcall RedXeWeatherSetTestTime(uint64_t now) noexcept
{
    gTestNow.store(now, std::memory_order_relaxed);
}

extern "C" void __stdcall RedXeWeatherSetTestServices(WeatherHttpTestResponseFn response, uint32_t helperMode) noexcept
{
    WeatherHttpSetTestResponse(response);
    gTestHelperMode.store(helperMode, std::memory_order_relaxed);
    gUseTestSnapshot.store(false, std::memory_order_release);
}

extern "C" HRESULT __stdcall RedXeWeatherTestLocationHelper(HANDLE cancelEvent, uint32_t mode) noexcept
{
    double latitude = 0.0, longitude = 0.0;
    return WeatherLocateWithHelper(cancelEvent, latitude, longitude,
                                   mode == 1   ? L"--test-wait"
                                   : mode == 2 ? L"--test-unavailable"
                                   : mode == 3 ? L"--test-coarse"
                                   : mode == 4 ? L"--test-cached"
                                   : mode == 5 ? L"--test-default"
                                               : L"--test-result");
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

extern "C" HRESULT __stdcall RedXeWeatherParseTestAlerts(const char* json, uint32_t bytes, uint32_t region,
                                                         uint32_t* alertCount, uint32_t* highestSeverity,
                                                         uint32_t* provider) noexcept
{
    if (!json || !alertCount || !highestSeverity || !provider)
    {
        return E_POINTER;
    }
    WeatherSnapshot snapshot{};
    const HRESULT result =
        WeatherParseAlerts(static_cast<WeatherAlertRegion>(region), std::string_view(json, bytes), snapshot);
    *alertCount = snapshot.alertCount;
    *highestSeverity = static_cast<uint32_t>(WeatherHighestAlertSeverity(snapshot));
    *provider = static_cast<uint32_t>(snapshot.alertProvider);
    return result;
}

extern "C" HRESULT __stdcall RedXeWeatherBuildTestAlertUrl(uint32_t region, const char* country, double latitude,
                                                           double longitude, char* url, uint32_t capacity) noexcept
{
    return WeatherBuildAlertUrl(static_cast<WeatherAlertRegion>(region),
                                country ? std::string_view(country) : std::string_view{}, latitude, longitude, url,
                                capacity);
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
