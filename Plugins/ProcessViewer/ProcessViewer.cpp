#define REDXE_PLUGIN_EXPORTS
#include "PlugInterfaces/Data.h"
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Host.h"
#include "PlugInterfaces/Widget.h"
#include "ProcessViewerTestContract.h"
#include "ViewerGpu.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include <string_view>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr char kSystemDataPluginId[] = "builtin.system-data";
constexpr char kEmptySchema[] = R"json({"type":"object","additionalProperties":false})json";
constexpr char kEmptyDefaults[] = R"json({})json";
constexpr char kTopN32Schema[] =
    R"json({"type":"object","additionalProperties":false,"required":["topN"],"properties":{"topN":{"type":"integer","minimum":1,"maximum":32}}})json";
constexpr char kTopN32Defaults[] = R"json({"topN":10})json";
constexpr char kTopN16Schema[] =
    R"json({"type":"object","additionalProperties":false,"required":["topN"],"properties":{"topN":{"type":"integer","minimum":1,"maximum":16}}})json";
constexpr char kTopN16Defaults[] = R"json({"topN":8})json";
constexpr float kEaseMilliseconds = 320.0f;
constexpr float kPulseMilliseconds = 420.0f;
constexpr float kHighBand = 85.0f;
constexpr uint32_t kMaximumNameCharacters = 95;
constexpr uint32_t kMaximumRows = 32;
constexpr uint32_t kSparkCapacity = 60;
constexpr uint32_t kHeatCapacity = 64;

constexpr float kPanelR = 17.0f / 255.0f;
constexpr float kPanelG = 17.0f / 255.0f;
constexpr float kPanelB = 17.0f / 255.0f;
constexpr float kTrackR = 0.10f;
constexpr float kTrackG = 0.10f;
constexpr float kTrackB = 0.11f;
constexpr float kTroughR = 0.048f;
constexpr float kTroughG = 0.048f;
constexpr float kTroughB = 0.050f;
constexpr float kFillR = 0.82f;
constexpr float kFillG = 0.84f;
constexpr float kFillB = 0.88f;
constexpr float kWarnR = 1.0f;
constexpr float kWarnG = 0.62f;
constexpr float kWarnB = 0.18f;
constexpr float kAccentR = 1.0f;
constexpr float kAccentG = 22.0f / 255.0f;
constexpr float kAccentB = 22.0f / 255.0f;
constexpr float kTextR = 0.92f;
constexpr float kTextG = 0.92f;
constexpr float kTextB = 0.94f;
constexpr float kMuted = 0.62f;
constexpr float kHairline = 0.22f;
constexpr float kOkR = 0.38f;
constexpr float kOkG = 0.82f;
constexpr float kOkB = 0.64f;
constexpr float kPanelInset = 4.0f;
constexpr float kPadFloorPx = 12.0f;
constexpr float kNetworkIdleBytes = 1.0f;
constexpr uint32_t kNetworkIdleHideSamples = 8;
constexpr wchar_t kDegreeCelsius[] = L"\u00B0C";
constexpr float kLabelFloorPx = 16.0f;
constexpr float kRowFloorPx = 18.0f;
constexpr float kKpiFloorPx = 30.0f;
constexpr float kHeroFloorPx = 48.0f;
constexpr float kTitleFloorPx = 26.0f;
constexpr float kRowMinFloorPx = 28.0f;
constexpr float kHeatMinCellPx = 6.0f;
constexpr float kHeatIdle = 0.22f;
constexpr double kDisplayUnit = 1000.0;
constexpr float kLogRateFloorBytes = 1024.0f;
constexpr float kLogRateFallbackCeilingBytes = 1024.0f * 1024.0f * 1024.0f;
constexpr float kThermalCoolC = 25.0f;
constexpr float kThermalWarnC = 70.0f;
constexpr float kThermalHotC = 85.0f;
constexpr uint32_t kIfOperStatusUp = 1;
constexpr uint32_t kMediaConnectStateConnected = 1;
constexpr uint32_t kRowFlagSoftware = 1u;
constexpr uint32_t kRowFlagIntegrated = 2u;

enum class ViewerDensity : uint32_t
{
    Hero = 0,
    Compact,
    Standard,
};

enum class ViewerKind : uint32_t
{
    ProcessViewer = 0,
    SystemPulse,
    CpuMeter,
    MemoryMeter,
    NetworkMeter,
    StorageMeter,
    GpuMeter,
    GpuProcesses,
    PowerMeter,
    ThermalMeter,
    Count,
};

[[nodiscard]] RedXeRaisedExtent RaisedExtentForKind(ViewerKind kind) noexcept
{
    switch (kind)
    {
    case ViewerKind::ProcessViewer:
    case ViewerKind::GpuProcesses:
    case ViewerKind::NetworkMeter:
    case ViewerKind::StorageMeter:
    case ViewerKind::ThermalMeter:
        return RedXeRaisedExtentHalf;
    case ViewerKind::SystemPulse:
        return RedXeRaisedExtentQuarter;
    case ViewerKind::CpuMeter:
    case ViewerKind::MemoryMeter:
    case ViewerKind::GpuMeter:
    case ViewerKind::PowerMeter:
        return RedXeRaisedExtentThird;
    default:
        return RedXeRaisedExtentQuarter;
    }
}

struct ViewerCatalogEntry final
{
    ViewerKind kind;
    const char* pluginId;
    const char* typeId;
    const wchar_t* pluginName;
    const wchar_t* pluginDescription;
    const wchar_t* typeName;
    const wchar_t* typeDescription;
    const char* dataSet0;
    const char* dataSet1;
    uint32_t interval0;
    uint32_t interval1;
    uint32_t topNMax;
    uint32_t topNDefault;
    float defaultWidth;
    float defaultHeight;
    float minimumWidth;
    float minimumHeight;
};

constexpr std::array kCatalog{
    ViewerCatalogEntry{ViewerKind::ProcessViewer, "builtin.process-viewer", "process-viewer", L"Process Viewer",
                       L"Ranks local processes by CPU from the RedXe system-data provider.", L"Process Viewer",
                       L"Two-column rows with working-set units, CPU percent, and heatmap-style load bars.",
                       "process.list", nullptr, 2000, 0, 32, 10, 1280.0f, 720.0f, 240.0f, 96.0f},
    ViewerCatalogEntry{ViewerKind::SystemPulse, "builtin.system-pulse", "system-pulse", L"System Pulse",
                       L"Machine CPU, memory, and count chips from system.summary.", L"System Pulse",
                       L"CPU hero, summary chips, and raised RAM plus CPU history from system.summary.",
                       "system.summary", nullptr, 1000, 0, 0, 0, 960.0f, 360.0f, 160.0f, 72.0f},
    ViewerCatalogEntry{ViewerKind::CpuMeter, "builtin.cpu-meter", "cpu-meter", L"CPU Meter",
                       L"Total CPU load and a logical-processor heatmap.", L"CPU Meter",
                       L"CPU percent, core heatmap, and a scrolling sample-driven history with recency fade.",
                       "cpu.summary", "cpu.logical", 1000, 1000, 0, 0, 960.0f, 540.0f, 160.0f, 72.0f},
    ViewerCatalogEntry{ViewerKind::MemoryMeter, "builtin.memory-meter", "memory-meter", L"Memory Meter",
                       L"Physical and commit memory gauges.", L"Memory Meter",
                       L"Capacity bars with a recency-faded paging history when height allows.", "memory.summary",
                       nullptr, 1000, 0, 0, 0, 960.0f, 420.0f, 160.0f, 72.0f},
    ViewerCatalogEntry{ViewerKind::NetworkMeter, "builtin.network-meter", "network-meter", L"Network Meter",
                       L"Top interface rates and protocol KPIs.", L"Network Meter",
                       L"Active NICs with a recency-faded throughput history; idle zero-rate adapters stay hidden.",
                       "network.interface", "network.protocol", 1000, 1000, 16, 8, 960.0f, 540.0f, 160.0f, 72.0f},
    ViewerCatalogEntry{ViewerKind::StorageMeter, "builtin.storage-meter", "storage-meter", L"Storage Meter",
                       L"Volume capacity and disk activity.", L"Storage Meter",
                       L"Volume cards sorted by used percent; used/total stays above the capacity track.",
                       "storage.volume", "storage.disk", 5000, 1000, 0, 0, 960.0f, 540.0f, 160.0f, 72.0f},
    ViewerCatalogEntry{ViewerKind::GpuMeter, "builtin.gpu-meter", "gpu-meter", L"GPU Meter",
                       L"Adapter cards from DXGI and D3DKMT sensors.", L"GPU Meter",
                       L"Discrete GPU first; software adapters collapsed.", "gpu.adapter", nullptr, 1000, 0, 0, 0,
                       960.0f, 540.0f, 160.0f, 72.0f},
    ViewerCatalogEntry{ViewerKind::GpuProcesses, "builtin.gpu-processes", "gpu-processes", L"GPU Processes",
                       L"Top GPU engine clients.", L"GPU Processes",
                       L"Two-column GPU clients with engine and percent; heatmap-style load bars.", "gpu.process",
                       "process.list", 2000, 2000, 16, 8, 960.0f, 540.0f, 160.0f, 72.0f},
    ViewerCatalogEntry{ViewerKind::PowerMeter, "builtin.power-meter", "power-meter", L"Power Meter",
                       L"AC/DC state, charge, and battery rows.", L"Power Meter",
                       L"Charge ring on battery hosts; compact AC status when no battery is present.", "power.summary",
                       "battery.list", 5000, 5000, 0, 0, 720.0f, 420.0f, 120.0f, 48.0f},
    ViewerCatalogEntry{ViewerKind::ThermalMeter, "builtin.thermal-meter", "thermal-meter", L"Thermal Meter",
                       L"Temperatures and fan RPM.", L"Thermal Meter",
                       L"Hottest sensors as °C cards with COOL/OK/WARM/HOT and a level track.", "thermal.sensor",
                       "fan.sensor", 10000, 10000, 0, 0, 720.0f, 540.0f, 160.0f, 72.0f},
};
static_assert(kCatalog.size() == static_cast<size_t>(ViewerKind::Count));

constexpr RedXePluginSettingsContract kEmptyContract{
    sizeof(RedXePluginSettingsContract), kEmptySchema, sizeof(kEmptySchema) - 1, kEmptyDefaults,
    sizeof(kEmptyDefaults) - 1,
};
constexpr RedXePluginSettingsContract kTopN32Contract{
    sizeof(RedXePluginSettingsContract), kTopN32Schema, sizeof(kTopN32Schema) - 1, kTopN32Defaults,
    sizeof(kTopN32Defaults) - 1,
};
constexpr RedXePluginSettingsContract kTopN16Contract{
    sizeof(RedXePluginSettingsContract), kTopN16Schema, sizeof(kTopN16Schema) - 1, kTopN16Defaults,
    sizeof(kTopN16Defaults) - 1,
};

std::atomic<uint32_t> g_liveProviderCount{0};
std::atomic<uint32_t> g_liveWidgetCount{0};
std::atomic<uint32_t> g_liveSubscriptionCount{0};
std::atomic<uint32_t> g_sampleCount{0};
std::atomic<uint32_t> g_paintCount{0};
std::atomic<uint32_t> g_lastPublishedRowCount{0};
std::atomic<uint32_t> g_configuredTopN{0};

[[nodiscard]] const ViewerCatalogEntry& Catalog(ViewerKind kind) noexcept
{
    return kCatalog[static_cast<size_t>(kind)];
}

[[nodiscard]] HRESULT ReadTopN(const RedXeFactoryOptions* options, uint32_t maximum, uint32_t fallback,
                               uint32_t& topN) noexcept
{
    topN = fallback;
    if (!options)
    {
        return S_OK;
    }
    if (options->sizeBytes != sizeof(RedXeFactoryOptions))
    {
        return E_INVALIDARG;
    }
    if (!options->configurationJsonUtf8 && options->configurationBytes == 0)
    {
        return S_OK;
    }
    if (!options->configurationJsonUtf8 || options->configurationBytes == 0 ||
        options->configurationBytes > kRedXeMaximumFactoryConfigurationBytes)
    {
        return E_INVALIDARG;
    }
    constexpr std::string_view prefix = R"json({"plugin":{},"instance":{"topN":)json";
    constexpr std::string_view suffix = "}}";
    const std::string_view json(options->configurationJsonUtf8, options->configurationBytes);
    if (!json.starts_with(prefix) || !json.ends_with(suffix) || json.size() <= prefix.size() + suffix.size())
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    const std::string_view number = json.substr(prefix.size(), json.size() - prefix.size() - suffix.size());
    uint32_t parsed = 0;
    const auto parsedResult = std::from_chars(number.data(), number.data() + number.size(), parsed);
    if (parsedResult.ec != std::errc{} || parsedResult.ptr != number.data() + number.size() || parsed == 0 ||
        parsed > maximum)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    topN = parsed;
    return S_OK;
}

[[nodiscard]] bool SnapshotMatches(const RedXeDataSnapshot* snapshot, const char* dataSetId,
                                   uint32_t minimumColumns) noexcept
{
    return snapshot && snapshot->sizeBytes == sizeof(RedXeDataSnapshot) && snapshot->dataSetId &&
           RedXeAsciiEqualsIgnoreCase(snapshot->dataSetId, dataSetId) && snapshot->columnCount >= minimumColumns &&
           (snapshot->rowCount == 0 || snapshot->rows);
}

[[nodiscard]] const RedXeDataValue* RowValues(const RedXeDataRow& row, uint32_t minimum) noexcept
{
    if (row.sizeBytes != sizeof(RedXeDataRow) || !row.values || row.valueCount < minimum)
    {
        return nullptr;
    }
    for (uint32_t index = 0; index < minimum; ++index)
    {
        if (row.values[index].sizeBytes != sizeof(RedXeDataValue))
        {
            return nullptr;
        }
    }
    return row.values;
}

[[nodiscard]] bool TakeU64(const RedXeDataValue& value, uint64_t& out) noexcept
{
    if (value.valueType != RedXeDataValueTypeUInt64 || value.quality != RedXeDataQualityGood)
    {
        return false;
    }
    out = value.uint64Value;
    return true;
}

[[nodiscard]] bool TakeF64(const RedXeDataValue& value, double& out) noexcept
{
    if (value.valueType != RedXeDataValueTypeFloat64 || value.quality != RedXeDataQualityGood ||
        !std::isfinite(value.float64Value))
    {
        return false;
    }
    out = value.float64Value;
    return true;
}

void CopyName(std::array<wchar_t, kMaximumNameCharacters + 1>& name, uint32_t& characters, const RedXeDataValue& value,
              const wchar_t* fallback) noexcept
{
    characters = 0;
    name[0] = L'\0';
    if (value.valueType == RedXeDataValueTypeUtf16 && value.quality == RedXeDataQualityGood && value.utf16Value &&
        value.utf16Characters > 0)
    {
        characters = std::min(value.utf16Characters, kMaximumNameCharacters);
        std::wmemcpy(name.data(), value.utf16Value, characters);
        name[characters] = L'\0';
        return;
    }
    if (fallback)
    {
        characters = static_cast<uint32_t>(wcsnlen(fallback, kMaximumNameCharacters));
        std::wmemcpy(name.data(), fallback, characters);
        name[characters] = L'\0';
    }
}

void CopyDetail(std::array<wchar_t, 32>& detail, uint32_t& characters, const RedXeDataValue& value,
                const wchar_t* fallback) noexcept
{
    characters = 0;
    detail[0] = L'\0';
    if (value.valueType == RedXeDataValueTypeUtf16 && value.quality == RedXeDataQualityGood && value.utf16Value &&
        value.utf16Characters > 0)
    {
        characters = std::min(value.utf16Characters, 31u);
        std::wmemcpy(detail.data(), value.utf16Value, characters);
        detail[characters] = L'\0';
        return;
    }
    if (fallback)
    {
        characters = static_cast<uint32_t>(wcsnlen(fallback, 31));
        std::wmemcpy(detail.data(), fallback, characters);
        detail[characters] = L'\0';
    }
}

[[nodiscard]] float SmootherStep(float t) noexcept
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

[[nodiscard]] float Lerp(float from, float to, float t) noexcept
{
    return from + (to - from) * t;
}

[[nodiscard]] wchar_t FoldAscii(wchar_t value) noexcept
{
    if (value >= L'A' && value <= L'Z')
    {
        return static_cast<wchar_t>(value - L'A' + L'a');
    }
    return value;
}

[[nodiscard]] bool ContainsInsensitive(const wchar_t* text, uint32_t characters, const wchar_t* needle) noexcept
{
    if (!text || !needle || needle[0] == L'\0')
    {
        return false;
    }
    const uint32_t needleLength = static_cast<uint32_t>(wcsnlen(needle, 64));
    if (needleLength == 0 || characters < needleLength)
    {
        return false;
    }
    for (uint32_t start = 0; start + needleLength <= characters; ++start)
    {
        uint32_t matched = 0;
        while (matched < needleLength && FoldAscii(text[start + matched]) == FoldAscii(needle[matched]))
        {
            ++matched;
        }
        if (matched == needleLength)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool IsFilterAdapterName(const wchar_t* text, uint32_t characters) noexcept
{
    return ContainsInsensitive(text, characters, L"WFP") || ContainsInsensitive(text, characters, L"QoS Packet") ||
           ContainsInsensitive(text, characters, L"Native MAC") ||
           ContainsInsensitive(text, characters, L"Lightweight Filter") ||
           ContainsInsensitive(text, characters, L"LightWeight Filter") ||
           ContainsInsensitive(text, characters, L"Kernel Debug");
}

[[nodiscard]] float LogRateFill(double bytesPerSecond, uint64_t linkBitsPerSecond) noexcept
{
    const double floorBytes = static_cast<double>(kLogRateFloorBytes);
    double ceilingBytes = static_cast<double>(kLogRateFallbackCeilingBytes);
    if (linkBitsPerSecond > 8)
    {
        ceilingBytes = static_cast<double>(linkBitsPerSecond) / 8.0;
    }
    if (ceilingBytes <= floorBytes * 2.0)
    {
        ceilingBytes = floorBytes * 2.0;
    }
    const double value = std::max(bytesPerSecond, floorBytes);
    const float fill = static_cast<float>(std::log10(value / floorBytes) / std::log10(ceilingBytes / floorBytes));
    return std::clamp(fill, 0.0f, 1.0f);
}

[[nodiscard]] float ThermalFill(float celsius) noexcept
{
    if (celsius <= kThermalCoolC)
    {
        return 0.0f;
    }
    if (celsius < kThermalWarnC)
    {
        return 0.35f * (celsius - kThermalCoolC) / (kThermalWarnC - kThermalCoolC);
    }
    if (celsius < kThermalHotC)
    {
        return 0.35f + 0.40f * (celsius - kThermalWarnC) / (kThermalHotC - kThermalWarnC);
    }
    return std::clamp(0.75f + 0.25f * (celsius - kThermalHotC) / 15.0f, 0.0f, 1.0f);
}

void SignalColor(float unit, float& red, float& green, float& blue) noexcept
{
    const float t = std::clamp(unit, 0.0f, 1.0f);
    if (t >= kHighBand / 100.0f)
    {
        red = kAccentR;
        green = kAccentG;
        blue = kAccentB;
        return;
    }
    if (t >= 0.70f)
    {
        red = kWarnR;
        green = kWarnG;
        blue = kWarnB;
        return;
    }
    red = kFillR;
    green = kFillG;
    blue = kFillB;
}

void ThermalColor(float celsius, float& red, float& green, float& blue) noexcept
{
    if (celsius >= kThermalHotC)
    {
        red = kAccentR;
        green = kAccentG;
        blue = kAccentB;
        return;
    }
    if (celsius >= kThermalWarnC)
    {
        red = kWarnR;
        green = kWarnG;
        blue = kWarnB;
        return;
    }
    red = kOkR;
    green = kOkG;
    blue = kOkB;
}

void IntentTextColor(float unit01, bool available, float& red, float& green, float& blue) noexcept
{
    if (!available)
    {
        red = kMuted;
        green = kMuted;
        blue = kMuted;
        return;
    }
    const float t = std::clamp(unit01, 0.0f, 1.0f);
    if (t >= kHighBand / 100.0f)
    {
        red = kAccentR;
        green = kAccentG;
        blue = kAccentB;
        return;
    }
    if (t >= 0.70f)
    {
        red = kWarnR;
        green = kWarnG;
        blue = kWarnB;
        return;
    }
    red = kOkR;
    green = kOkG;
    blue = kOkB;
}

void IntentThermalTextColor(float celsius, bool available, float& red, float& green, float& blue) noexcept
{
    if (!available)
    {
        red = kMuted;
        green = kMuted;
        blue = kMuted;
        return;
    }
    ThermalColor(celsius, red, green, blue);
}

[[nodiscard]] const wchar_t* CapacityBandText(float percent, bool available) noexcept
{
    if (!available)
    {
        return L"--";
    }
    if (percent >= kHighBand)
    {
        return L"FULL";
    }
    if (percent >= 70.0f)
    {
        return L"HIGH";
    }
    return L"OK";
}

[[nodiscard]] const wchar_t* ThermalBandText(float celsius, bool available) noexcept
{
    if (!available)
    {
        return L"--";
    }
    if (celsius >= kThermalHotC)
    {
        return L"HOT";
    }
    if (celsius >= kThermalWarnC)
    {
        return L"WARM";
    }
    if (celsius <= kThermalCoolC)
    {
        return L"COOL";
    }
    return L"OK";
}

[[nodiscard]] ViewerDensity DensityForInner(float innerHeight) noexcept
{
    if (innerHeight < 64.0f)
    {
        return ViewerDensity::Hero;
    }
    if (innerHeight < 150.0f)
    {
        return ViewerDensity::Compact;
    }
    return ViewerDensity::Standard;
}

[[nodiscard]] uint32_t FitVisibleCount(float innerHeight, float rowMin, uint32_t available) noexcept
{
    if (available == 0 || innerHeight < rowMin)
    {
        return available == 0 ? 0 : (innerHeight >= rowMin * 0.6f ? 1 : 0);
    }
    const uint32_t maxFit = std::max(1u, static_cast<uint32_t>(innerHeight / rowMin));
    return std::min(available, maxFit);
}

[[nodiscard]] float FittedRowHeight(float innerHeight, uint32_t visible, float rowMin) noexcept
{
    if (visible == 0)
    {
        return rowMin;
    }
    return std::max(rowMin, innerHeight / static_cast<float>(visible));
}

[[nodiscard]] float ClampOrdered(float value, float boundA, float boundB) noexcept
{
    return std::clamp(value, std::min(boundA, boundB), std::max(boundA, boundB));
}

[[nodiscard]] float TypeFromRow(float rowHeight, float floorPx, float ceilingPx) noexcept
{
    return ClampOrdered(rowHeight * 0.62f, floorPx, ceilingPx);
}

void FitListLayout(float innerHeight, float rowMin, float overflowReserve, uint32_t available, uint32_t& visible,
                   float& rowHeight, float& listHeight) noexcept
{
    visible = FitVisibleCount(innerHeight, rowMin, available);
    listHeight = innerHeight;
    if (available > visible && overflowReserve > 0.0f && innerHeight > rowMin + overflowReserve)
    {
        listHeight = innerHeight - overflowReserve;
        visible = FitVisibleCount(listHeight, rowMin, available);
    }
    rowHeight = FittedRowHeight(listHeight, visible == 0 ? 1 : visible, rowMin);
}

[[nodiscard]] uint32_t GridColumns(float innerWidth, float minColWidth) noexcept
{
    return innerWidth >= minColWidth * 2.0f + 12.0f ? 2u : 1u;
}

void FitGridLayout(float innerHeight, float innerWidth, float rowMin, float overflowReserve, float minColWidth,
                   uint32_t available, uint32_t& columns, uint32_t& visible, uint32_t& rows, float& rowHeight,
                   float& listHeight, float& colWidth) noexcept
{
    columns = GridColumns(innerWidth, minColWidth);
    const float gap = columns > 1 ? 12.0f : 0.0f;
    colWidth = (innerWidth - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    uint32_t rowBudget = FitVisibleCount(innerHeight, rowMin, (available + columns - 1) / columns);
    listHeight = innerHeight;
    uint32_t capacity = rowBudget * columns;
    if (available > capacity && overflowReserve > 0.0f && innerHeight > rowMin + overflowReserve)
    {
        listHeight = innerHeight - overflowReserve;
        rowBudget = FitVisibleCount(listHeight, rowMin, (available + columns - 1) / columns);
        capacity = rowBudget * columns;
    }
    visible = available == 0 ? 0 : std::min(available, std::max(1u, capacity));
    rows = std::max(1u, visible == 0 ? 1u : (visible + columns - 1) / columns);
    rowHeight = FittedRowHeight(listHeight, rows, rowMin);
}

void CellOrigin(float originX, float originY, uint32_t index, uint32_t columns, float colWidth, float rowHeight,
                float displayY, bool slide, float& x, float& y) noexcept
{
    const uint32_t col = columns == 0 ? 0 : index % columns;
    const float row = (slide && columns == 1) ? displayY : static_cast<float>(index / std::max(1u, columns));
    const float gap = columns > 1 ? 12.0f : 0.0f;
    x = originX + static_cast<float>(col) * (colWidth + gap);
    y = originY + row * rowHeight;
}

void FormatPercent(wchar_t* buffer, uint32_t capacity, float value, bool available) noexcept
{
    if (!buffer || capacity == 0)
    {
        return;
    }
    if (!available)
    {
        (void)swprintf_s(buffer, capacity, L"--");
        return;
    }
    (void)swprintf_s(buffer, capacity, L"%.0f%%", static_cast<double>(std::clamp(value, 0.0f, 100.0f)));
}

void FormatCount(wchar_t* buffer, uint32_t capacity, uint64_t value, bool available) noexcept
{
    if (!buffer || capacity == 0)
    {
        return;
    }
    if (!available)
    {
        (void)swprintf_s(buffer, capacity, L"--");
        return;
    }
    if (value >= 1'000'000)
    {
        (void)swprintf_s(buffer, capacity, L"%.1fM", static_cast<double>(value) / 1'000'000.0);
    }
    else if (value >= 10'000)
    {
        (void)swprintf_s(buffer, capacity, L"%.1fK", static_cast<double>(value) / 1'000.0);
    }
    else
    {
        (void)swprintf_s(buffer, capacity, L"%llu", static_cast<unsigned long long>(value));
    }
}

[[nodiscard]] float CpuUnit(float percent) noexcept
{
    return std::clamp(percent, 0.0f, 100.0f) / 100.0f;
}

[[nodiscard]] float CpuHeatLuminance(float unit) noexcept
{
    const float t = std::clamp(unit, 0.0f, 1.0f);
    return t * t;
}

void FormatScaled(wchar_t* buffer, uint32_t capacity, double value, bool available, bool perSecond) noexcept
{
    if (!buffer || capacity == 0)
    {
        return;
    }
    if (!available)
    {
        (void)swprintf_s(buffer, capacity, L"--");
        return;
    }
    value = std::max(value, 0.0);
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    uint32_t unit = 0;
    while (value >= kDisplayUnit && unit + 1 < 5)
    {
        value /= kDisplayUnit;
        ++unit;
    }
    const wchar_t* suffix = perSecond ? L"/s" : L"";
    if (unit == 0)
    {
        (void)swprintf_s(buffer, capacity, L"%.0f %s%s", value, units[unit], suffix);
        return;
    }
    (void)swprintf_s(buffer, capacity, L"%.1f %s%s", value, units[unit], suffix);
}

void FormatBytes(wchar_t* buffer, uint32_t capacity, uint64_t bytes, bool available) noexcept
{
    FormatScaled(buffer, capacity, static_cast<double>(bytes), available, false);
}

void FormatRate(wchar_t* buffer, uint32_t capacity, double bytesPerSecond, bool available) noexcept
{
    FormatScaled(buffer, capacity, bytesPerSecond, available, true);
}

[[nodiscard]] uint64_t HashUtf16(const wchar_t* text, uint32_t characters) noexcept
{
    uint64_t hash = 14695981039346656037ull;
    if (!text)
    {
        return hash;
    }
    for (uint32_t index = 0; index < characters; ++index)
    {
        hash ^= static_cast<uint64_t>(text[index]);
        hash *= 1099511628211ull;
    }
    return hash;
}

void FormatPid(wchar_t* buffer, uint32_t capacity, uint64_t pid) noexcept
{
    if (!buffer || capacity == 0)
    {
        return;
    }
    (void)swprintf_s(buffer, capacity, L"%llu", static_cast<unsigned long long>(pid));
}

void FormatCelsius(wchar_t* buffer, uint32_t capacity, float celsius, bool available) noexcept
{
    if (!buffer || capacity == 0)
    {
        return;
    }
    if (!available)
    {
        (void)swprintf_s(buffer, capacity, L"--");
        return;
    }
    (void)swprintf_s(buffer, capacity, L"%.0f %s", static_cast<double>(celsius), kDegreeCelsius);
}

void FormatBytesPair(wchar_t* buffer, uint32_t capacity, uint64_t used, uint64_t total, bool available) noexcept
{
    if (!available)
    {
        FormatBytes(buffer, capacity, 0, false);
        return;
    }
    wchar_t usedText[24]{};
    wchar_t totalText[24]{};
    FormatBytes(usedText, 24, used, true);
    FormatBytes(totalText, 24, total, true);
    (void)swprintf_s(buffer, capacity, L"%s / %s", usedText, totalText);
}

HRESULT AppendClippedText(ViewerGpuResources& resources, ViewerDrawList& list, float x, float y, float height,
                          float maxWidth, float red, float green, float blue, float alpha, const wchar_t* text,
                          uint32_t characters) noexcept
{
    if (!text || characters == 0 || maxWidth <= 1.0f)
    {
        return S_OK;
    }
    if (resources.MeasureText(text, characters, height) <= maxWidth)
    {
        return resources.AppendText(list, x, y, height, red, green, blue, alpha, text, characters);
    }
    wchar_t clipped[kMaximumNameCharacters + 4]{};
    uint32_t keep = characters;
    while (keep > 0)
    {
        --keep;
        const uint32_t copy = std::min(keep, kMaximumNameCharacters);
        if (copy == 0)
        {
            break;
        }
        std::wmemcpy(clipped, text, copy);
        clipped[copy] = L'.';
        clipped[copy + 1] = L'.';
        clipped[copy + 2] = L'.';
        clipped[copy + 3] = L'\0';
        if (resources.MeasureText(clipped, copy + 3, height) <= maxWidth)
        {
            return resources.AppendText(list, x, y, height, red, green, blue, alpha, clipped, copy + 3);
        }
    }
    return S_OK;
}

void FormatUptime(wchar_t* buffer, uint32_t capacity, uint64_t milliseconds, bool available) noexcept
{
    if (!available)
    {
        (void)swprintf_s(buffer, capacity, L"--");
        return;
    }
    const uint64_t seconds = milliseconds / 1000ull;
    const uint64_t days = seconds / 86400ull;
    const uint64_t hours = (seconds % 86400ull) / 3600ull;
    const uint64_t minutes = (seconds % 3600ull) / 60ull;
    if (days > 0)
    {
        (void)swprintf_s(buffer, capacity, L"%llud %02llu:%02llu", static_cast<unsigned long long>(days),
                         static_cast<unsigned long long>(hours), static_cast<unsigned long long>(minutes));
    }
    else
    {
        (void)swprintf_s(buffer, capacity, L"%02llu:%02llu", static_cast<unsigned long long>(hours),
                         static_cast<unsigned long long>(minutes));
    }
}

struct RankedRow final
{
    uint64_t identity = 0;
    uint64_t pid = 0;
    std::array<wchar_t, kMaximumNameCharacters + 1> name{};
    uint32_t nameCharacters = 0;
    std::array<wchar_t, 32> detail{};
    uint32_t detailCharacters = 0;
    float primary = 0.0f;
    float displayPrimary = 0.0f;
    float displayY = 0.0f;
    uint64_t secondary = 0;
    uint32_t tertiary = 0;
    uint32_t flags = 0;
    bool primaryAvailable = false;
};

struct NetIdleWatch final
{
    uint64_t identity = 0;
    uint32_t idleStreak = 0;
};

struct PidNameEntry final
{
    uint64_t pid = 0;
    std::array<wchar_t, kMaximumNameCharacters + 1> name{};
    uint32_t characters = 0;
};

struct ViewerSample final
{
    std::array<float, 8> values{};
    std::array<float, 8> display{};
    std::array<uint64_t, 8> counts{};
    std::array<bool, 8> available{};
    std::array<RankedRow, kMaximumRows> rows{};
    uint32_t rowCount = 0;
    std::array<float, kHeatCapacity> heat{};
    std::array<float, kHeatCapacity> heatDisplay{};
    uint32_t heatCount = 0;
    uint32_t heatOverflow = 0;
    std::array<float, kSparkCapacity> spark{};
    uint32_t sparkCount = 0;
    uint32_t batteryCount = 0;
    uint32_t hiddenCount = 0;
    bool acOnline = true;
    bool charging = false;
    bool batteryPresent = false;
};

void DrawHistoryArea(ViewerDrawList& list, float x, float y, float width, float height, const ViewerSample& sample,
                     float latest, bool latestAvailable, bool percentScale) noexcept
{
    if (width <= 4.0f || height <= 4.0f)
    {
        return;
    }
    (void)list.AddFill(x, y, width, height, kTrackR, kTrackG, kTrackB, 0.55f, 6.0f);
    uint32_t count = sample.sparkCount;
    if (count == 0 && latestAvailable)
    {
        count = 1;
    }
    if (count == 0)
    {
        return;
    }
    float maxValue = percentScale ? 100.0f : 1.0f;
    for (uint32_t index = 0; index < sample.sparkCount; ++index)
    {
        maxValue = std::max(maxValue, sample.spark[index]);
    }
    if (latestAvailable)
    {
        maxValue = std::max(maxValue, latest);
    }
    maxValue = std::max(maxValue, 1.0f);
    const float colW = width / static_cast<float>(kSparkCapacity);
    const uint32_t start = kSparkCapacity - count;
    for (uint32_t index = 0; index < count; ++index)
    {
        float value = index < sample.sparkCount ? sample.spark[index] : 0.0f;
        if (index + 1 == count && latestAvailable)
        {
            value = latest;
        }
        const float recency = count == 1 ? 1.0f : static_cast<float>(index) / static_cast<float>(count - 1);
        const float unit =
            percentScale ? std::clamp(value, 0.0f, 100.0f) / 100.0f : std::clamp(value / maxValue, 0.0f, 1.0f);
        float red = kFillR;
        float green = kFillG;
        float blue = kFillB;
        if (percentScale)
        {
            SignalColor(unit, red, green, blue);
        }
        const float barH = std::max(2.0f, height * unit);
        const float bx = x + static_cast<float>(start + index) * colW + 0.4f;
        const float by = y + height - barH;
        const float bw = std::max(1.2f, colW - 0.8f);
        const float fade = 0.08f + recency * recency * 0.42f;
        (void)list.AddFill(bx, by, bw, barH, red, green, blue, fade, 1.5f);
        const float midH = std::max(2.0f, barH * 0.45f);
        (void)list.AddFill(bx, by, bw, midH, red, green, blue, fade + 0.16f, 1.5f);
        const float capH = std::max(2.0f, barH * 0.16f);
        (void)list.AddFill(bx, by, bw, capH, red, green, blue, 0.40f + recency * 0.50f, 1.5f);
        if (index + 1 == count)
        {
            (void)list.AddGlow(bx - 2.0f, by - 2.0f, bw + 4.0f, barH + 4.0f, red, green, blue, 0.18f + recency * 0.20f,
                               8.0f);
        }
    }
}

class ViewerWidget;

class ViewerSink final : public RedXeComObject<ViewerSink, IRedXeDataSink>
{
  public:
    ViewerSink(ViewerWidget& widget, uint32_t dataSetIndex) noexcept : _widget(&widget), _dataSetIndex(dataSetIndex) {}

    HRESULT STDMETHODCALLTYPE OnDataSnapshot(const RedXeDataSnapshot* snapshot) noexcept override;

    // Detaches the sink from its widget. The host drains an in-flight OnDataSnapshot before a subscription release
    // returns, so calling this after every subscription is released makes the back-pointer unreachable rather than
    // merely unused.
    void Detach() noexcept
    {
        _widget = nullptr;
    }

  private:
    // Borrowed, never owned: the widget owns this sink, so an owning reference would be a cycle.
    ViewerWidget* _widget;
    uint32_t _dataSetIndex;
};

class ViewerWidget final : public RedXeComObject<ViewerWidget, IRedXeWidget, IRedXeGpuWidget, IRedXeScheduledWidget, IRedXeRaisedWidget>
{
  public:
    ViewerWidget(wil::com_ptr_nothrow<IRedXeWidgetProvider>&& providerOwner, ViewerKind kind, uint32_t topN) noexcept
        : _providerOwner(std::move(providerOwner)), _kind(kind), _topN(topN)
    {
        g_liveWidgetCount.fetch_add(1, std::memory_order_relaxed);
        if (kind == ViewerKind::ProcessViewer)
        {
            g_configuredTopN.store(topN, std::memory_order_relaxed);
        }
        _restDelay = Catalog(kind).interval0;
        if (Catalog(kind).dataSet1 && Catalog(kind).interval1 != 0)
        {
            _restDelay = std::min(_restDelay, Catalog(kind).interval1);
        }
    }

    ~ViewerWidget()
    {
        // Order matters and is enforced here rather than by member declaration order: release every subscription
        // first so the host drains any in-flight OnDataSnapshot, then detach the sinks so a sink the host still holds
        // can never reach this widget, and only then drop the widget's own sink references.
        for (uint32_t index = 0; index < _subscriptionCount; ++index)
        {
            _subscriptions[index].reset();
            g_liveSubscriptionCount.fetch_sub(1, std::memory_order_relaxed);
        }
        for (wil::com_ptr_nothrow<IRedXeDataSink>& sink : _sinks)
        {
            if (sink)
            {
                static_cast<ViewerSink*>(sink.get())->Detach();
            }
        }
        _sinks[0].reset();
        _sinks[1].reset();
        if (_gpuHeld)
        {
            ViewerGpuRelease();
            _gpuHeld = false;
        }
        g_liveWidgetCount.fetch_sub(1, std::memory_order_relaxed);
    }

    [[nodiscard]] HRESULT InitializeSubscriptions(IRedXeDataProvider& provider) noexcept
    {
        const ViewerCatalogEntry& entry = Catalog(_kind);
        const char* dataSets[2] = {entry.dataSet0, entry.dataSet1};
        const uint32_t intervals[2] = {entry.interval0, entry.interval1};
        for (uint32_t index = 0; index < 2; ++index)
        {
            if (!dataSets[index])
            {
                continue;
            }
            auto* sink = new (std::nothrow) ViewerSink(*this, index);
            if (!sink)
            {
                return E_OUTOFMEMORY;
            }
            _sinks[index].attach(sink);
            const RedXeDataSubscriptionOptions options{
                sizeof(RedXeDataSubscriptionOptions),
                dataSets[index],
                intervals[index],
            };
            const HRESULT result = provider.Subscribe(&options, _sinks[index].get(), _subscriptions[index].put());
            if (FAILED(result))
            {
                _sinks[index].reset();
                return result;
            }
            ++_subscriptionCount;
            g_liveSubscriptionCount.fetch_add(1, std::memory_order_relaxed);
        }
        return S_OK;
    }

    HRESULT Publish(uint32_t dataSetIndex, const RedXeDataSnapshot* snapshot) noexcept
    {
        AcquireSRWLockExclusive(&_lock);
        const HRESULT result = Ingest(dataSetIndex, snapshot);
        if (SUCCEEDED(result))
        {
            BeginEase();
            g_sampleCount.fetch_add(1, std::memory_order_relaxed);
        }
        ReleaseSRWLockExclusive(&_lock);
        return result;
    }

    HRESULT STDMETHODCALLTYPE SetVisible(BOOL visible) noexcept override
    {
        const bool show = visible != FALSE;
        _visible.store(show, std::memory_order_release);
        for (uint32_t index = 0; index < _subscriptionCount; ++index)
        {
            if (_subscriptions[index])
            {
                const HRESULT result = _subscriptions[index]->SetActive(show ? TRUE : FALSE);
                if (FAILED(result))
                {
                    return result;
                }
            }
        }
        if (!show)
        {
            AcquireSRWLockExclusive(&_lock);
            _easing = false;
            _pulse = 0.0f;
            ReleaseSRWLockExclusive(&_lock);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRaisedExtent(RedXeRaisedExtent* extent) noexcept override
    {
        if (!extent)
        {
            return E_POINTER;
        }
        *extent = RaisedExtentForKind(_kind);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetRaised(BOOL raised) noexcept override
    {
        _raised = raised != FALSE;
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
        const HRESULT result = ViewerGpuAcquire(context->device);
        if (SUCCEEDED(result))
        {
            _gpuHeld = true;
        }
        return result;
    }

    void STDMETHODCALLTYPE OnDeviceLost() noexcept override
    {
        if (_gpuHeld)
        {
            ViewerGpuRelease();
            _gpuHeld = false;
        }
        AcquireSRWLockExclusive(&_lock);
        _easing = false;
        _pulse = 0.0f;
        ReleaseSRWLockExclusive(&_lock);
    }

    HRESULT STDMETHODCALLTYPE OnTargetSizeChanged(const RedXeGpuTargetSizeContext* context) noexcept override
    {
        if (!context || context->sizeBytes != sizeof(RedXeGpuTargetSizeContext))
        {
            return E_INVALIDARG;
        }
        // Deliberately no rebuild. Layout already derives every size from the per-frame widget context, so nothing
        // here depends on being told the size in advance.
        //
        // The glyph atlas is the one resolution-dependent resource, and it cannot follow a single widget's size: it
        // is one shared store acquired by every System Data viewer on the page, each with a different tile, and it is
        // populated lazily as new characters appear. Growing its 48px cell to raise the rasterization size would also
        // cut the slot count from 441 to about 100, which the process- and adapter-name character set can exceed.
        // Making it resolution-adaptive needs a per-size store or a larger atlas, which is its own change.
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
        if (frame.sizeBytes != sizeof(RedXeWidgetFrameContext))
        {
            return E_INVALIDARG;
        }
        if (frame.widthPixels == 0 || frame.heightPixels == 0)
        {
            return S_OK;
        }
        ViewerGpuResources* resources = ViewerGpuGet();
        if (!resources)
        {
            return E_UNEXPECTED;
        }

        AcquireSRWLockExclusive(&_lock);
        const float deltaMs = frame.deltaSeconds * 1000.0f;
        if (_easing)
        {
            _easeElapsed += deltaMs;
            const float t = SmootherStep(_easeElapsed / kEaseMilliseconds);
            ApplyEase(t);
            if (_easeElapsed >= kEaseMilliseconds)
            {
                SnapDisplayed();
                _easing = false;
            }
        }
        if (_pulse > 0.0f)
        {
            _pulse = std::max(0.0f, _pulse - deltaMs / kPulseMilliseconds);
        }
        ViewerSample sample = _sample;
        const bool easing = _easing;
        const float pulse = _pulse;
        ReleaseSRWLockExclusive(&_lock);

        ViewerDrawList list;
        HRESULT result = BuildScene(*resources, list, sample, static_cast<float>(frame.widthPixels),
                                    static_cast<float>(frame.heightPixels), pulse);
        if (FAILED(result))
        {
            return result;
        }
        result = resources->Render(context->deviceContext, static_cast<float>(frame.widthPixels),
                                   static_cast<float>(frame.heightPixels), list);
        if (SUCCEEDED(result))
        {
            g_paintCount.fetch_add(1, std::memory_order_relaxed);
            (void)easing;
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
            *delayMilliseconds = kRedXeMaximumScheduledFrameDelayMilliseconds;
            return S_OK;
        }
        AcquireSRWLockShared(&_lock);
        const bool busy = _easing || _pulse > 0.0f;
        ReleaseSRWLockShared(&_lock);
        *delayMilliseconds = busy ? 1U : std::max(1U, _restDelay);
        return S_OK;
    }

  private:
    void BeginEase() noexcept
    {
        _easeElapsed = 0.0f;
        _easing = true;
        if (HighBandActive(_sample))
        {
            _pulse = 1.0f;
        }
    }

    [[nodiscard]] bool HighBandActive(const ViewerSample& sample) const noexcept
    {
        switch (_kind)
        {
        case ViewerKind::SystemPulse:
        case ViewerKind::CpuMeter:
            return sample.available[0] && sample.values[0] >= kHighBand;
        case ViewerKind::MemoryMeter:
            return (sample.available[0] && sample.values[0] >= kHighBand) ||
                   (sample.available[1] && sample.values[1] >= kHighBand);
        case ViewerKind::ProcessViewer:
        case ViewerKind::GpuProcesses:
            return sample.rowCount > 0 && sample.rows[0].primaryAvailable && sample.rows[0].primary >= kHighBand;
        case ViewerKind::StorageMeter:
            for (uint32_t index = 0; index < sample.rowCount; ++index)
            {
                if (sample.rows[index].primaryAvailable && sample.rows[index].primary >= kHighBand)
                {
                    return true;
                }
            }
            return false;
        case ViewerKind::NetworkMeter:
            return sample.available[7] && sample.values[7] >= kHighBand;
        default:
            return false;
        }
    }

    void ApplyEase(float t) noexcept
    {
        for (uint32_t index = 0; index < 8; ++index)
        {
            _sample.display[index] = Lerp(_sample.display[index], _sample.values[index], t);
        }
        for (uint32_t index = 0; index < _sample.rowCount; ++index)
        {
            _sample.rows[index].displayPrimary =
                Lerp(_sample.rows[index].displayPrimary, _sample.rows[index].primary, t);
            const float targetY = static_cast<float>(index);
            _sample.rows[index].displayY = Lerp(_sample.rows[index].displayY, targetY, t);
        }
        for (uint32_t index = 0; index < _sample.heatCount; ++index)
        {
            _sample.heatDisplay[index] = Lerp(_sample.heatDisplay[index], _sample.heat[index], t);
        }
    }

    void SnapDisplayed() noexcept
    {
        _sample.display = _sample.values;
        for (uint32_t index = 0; index < _sample.rowCount; ++index)
        {
            _sample.rows[index].displayPrimary = _sample.rows[index].primary;
            _sample.rows[index].displayY = static_cast<float>(index);
        }
        _sample.heatDisplay = _sample.heat;
    }

    [[nodiscard]] HRESULT Ingest(uint32_t dataSetIndex, const RedXeDataSnapshot* snapshot) noexcept
    {
        switch (_kind)
        {
        case ViewerKind::ProcessViewer:
            return IngestProcess(snapshot);
        case ViewerKind::SystemPulse:
            return IngestSummary(snapshot);
        case ViewerKind::CpuMeter:
            return dataSetIndex == 0 ? IngestCpuSummary(snapshot) : IngestCpuLogical(snapshot);
        case ViewerKind::MemoryMeter:
            return IngestMemory(snapshot);
        case ViewerKind::NetworkMeter:
            return dataSetIndex == 0 ? IngestNetworkInterfaces(snapshot) : IngestNetworkProtocols(snapshot);
        case ViewerKind::StorageMeter:
            return dataSetIndex == 0 ? IngestVolumes(snapshot) : IngestDisks(snapshot);
        case ViewerKind::GpuMeter:
            return IngestGpuAdapters(snapshot);
        case ViewerKind::GpuProcesses:
            return dataSetIndex == 0 ? IngestGpuProcesses(snapshot) : IngestGpuProcessNames(snapshot);
        case ViewerKind::PowerMeter:
            return dataSetIndex == 0 ? IngestPower(snapshot) : IngestBatteries(snapshot);
        case ViewerKind::ThermalMeter:
            return dataSetIndex == 0 ? IngestThermal(snapshot) : IngestFans(snapshot);
        default:
            return E_UNEXPECTED;
        }
    }

    void PushSpark(float value) noexcept
    {
        if (_sample.sparkCount < kSparkCapacity)
        {
            _sample.spark[_sample.sparkCount++] = value;
            return;
        }
        for (uint32_t index = 1; index < kSparkCapacity; ++index)
        {
            _sample.spark[index - 1] = _sample.spark[index];
        }
        _sample.spark[kSparkCapacity - 1] = value;
    }

    static bool RankByPrimary(const RankedRow& left, const RankedRow& right) noexcept
    {
        if (left.primaryAvailable != right.primaryAvailable)
        {
            return left.primaryAvailable;
        }
        if (left.primary != right.primary)
        {
            return left.primary > right.primary;
        }
        if (left.secondary != right.secondary)
        {
            return left.secondary > right.secondary;
        }
        return left.identity < right.identity;
    }

    [[nodiscard]] HRESULT IngestProcess(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "process.list", 7))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        ViewerSample next = _sample;
        next.rowCount = 0;
        for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
        {
            const RedXeDataValue* values = RowValues(snapshot->rows[rowIndex], 7);
            if (!values || values[0].valueType != RedXeDataValueTypeUInt64 || values[0].uint64Value > UINT32_MAX)
            {
                continue;
            }
            RankedRow row{};
            row.identity = values[0].uint64Value;
            row.pid = values[0].uint64Value;
            CopyName(row.name, row.nameCharacters, values[1], L"(unnamed)");
            double cpu = 0.0;
            row.primaryAvailable = TakeF64(values[2], cpu);
            row.primary = row.primaryAvailable ? static_cast<float>(std::clamp(cpu, 0.0, 100.0)) : 0.0f;
            (void)TakeU64(values[3], row.secondary);
            uint64_t threads = 0;
            if (TakeU64(values[5], threads))
            {
                row.tertiary = static_cast<uint32_t>(std::min(threads, static_cast<uint64_t>(UINT32_MAX)));
            }
            InsertInto(next, row);
        }
        PreserveRowMotion(_sample, next);
        _sample.rowCount = next.rowCount;
        _sample.rows = next.rows;
        g_lastPublishedRowCount.store(next.rowCount, std::memory_order_relaxed);
        return S_OK;
    }

    void InsertInto(ViewerSample& sample, RankedRow candidate) noexcept
    {
        InsertBounded(sample, candidate, _topN == 0 ? 8u : _topN);
    }

    void InsertBounded(ViewerSample& sample, RankedRow candidate, uint32_t limit) noexcept
    {
        if (limit == 0)
        {
            return;
        }
        if (sample.rowCount < limit)
        {
            sample.rows[sample.rowCount++] = candidate;
        }
        else if (!RankByPrimary(candidate, sample.rows[sample.rowCount - 1]))
        {
            return;
        }
        else
        {
            sample.rows[sample.rowCount - 1] = candidate;
        }
        for (uint32_t index = sample.rowCount - 1;
             index > 0 && RankByPrimary(sample.rows[index], sample.rows[index - 1]); --index)
        {
            std::swap(sample.rows[index], sample.rows[index - 1]);
        }
    }

    static void PreserveRowMotion(const ViewerSample& previous, ViewerSample& next) noexcept
    {
        for (uint32_t index = 0; index < next.rowCount; ++index)
        {
            RankedRow& row = next.rows[index];
            row.displayPrimary = row.primary;
            row.displayY = static_cast<float>(index);
            for (uint32_t previousIndex = 0; previousIndex < previous.rowCount; ++previousIndex)
            {
                if (previous.rows[previousIndex].identity == row.identity)
                {
                    row.displayPrimary = previous.rows[previousIndex].displayPrimary;
                    row.displayY = previous.rows[previousIndex].displayY;
                    break;
                }
            }
        }
    }

    [[nodiscard]] HRESULT IngestSummary(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "system.summary", 10) || snapshot->rowCount == 0)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        const RedXeDataValue* values = RowValues(snapshot->rows[0], 10);
        if (!values)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        double cpu = 0.0;
        _sample.available[0] = TakeF64(values[0], cpu);
        _sample.values[0] = static_cast<float>(std::clamp(cpu, 0.0, 100.0));
        _sample.available[1] = TakeU64(values[2], _sample.counts[1]);
        _sample.available[2] = TakeU64(values[3], _sample.counts[2]);
        _sample.available[3] = TakeU64(values[4], _sample.counts[3]);
        _sample.available[4] = TakeU64(values[7], _sample.counts[4]);
        _sample.available[5] = TakeU64(values[9], _sample.counts[5]);
        _sample.available[6] = TakeU64(values[1], _sample.counts[6]);
        _sample.available[7] = TakeU64(values[8], _sample.counts[7]);
        uint64_t total = 0;
        if (TakeU64(values[5], total) && total > 0)
        {
            _sample.counts[0] = total;
            if (_sample.available[4])
            {
                _sample.values[4] = static_cast<float>(std::clamp(
                    100.0 * static_cast<double>(_sample.counts[4]) / static_cast<double>(total), 0.0, 100.0));
            }
        }
        PushSpark(_sample.values[0]);
        return S_OK;
    }

    [[nodiscard]] HRESULT IngestCpuSummary(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "cpu.summary", 4) || snapshot->rowCount == 0)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        const RedXeDataValue* values = RowValues(snapshot->rows[0], 4);
        if (!values)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        double total = 0.0;
        double user = 0.0;
        double kernel = 0.0;
        double idle = 0.0;
        _sample.available[0] = TakeF64(values[0], total);
        _sample.available[1] = TakeF64(values[1], user);
        _sample.available[2] = TakeF64(values[2], kernel);
        _sample.available[3] = TakeF64(values[3], idle);
        _sample.values[0] = static_cast<float>(std::clamp(total, 0.0, 100.0));
        _sample.values[1] = static_cast<float>(std::clamp(user, 0.0, 100.0));
        _sample.values[2] = static_cast<float>(std::clamp(kernel, 0.0, 100.0));
        _sample.values[3] = static_cast<float>(std::clamp(idle, 0.0, 100.0));
        PushSpark(_sample.values[0]);
        return S_OK;
    }

    [[nodiscard]] HRESULT IngestCpuLogical(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "cpu.logical", 10))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        _sample.heatCount = 0;
        _sample.heatOverflow = snapshot->rowCount > kHeatCapacity ? snapshot->rowCount - kHeatCapacity : 0;
        const uint32_t limit = std::min(snapshot->rowCount, kHeatCapacity);
        for (uint32_t index = 0; index < limit; ++index)
        {
            const RedXeDataValue* values = RowValues(snapshot->rows[index], 10);
            double total = 0.0;
            _sample.heat[index] =
                values && TakeF64(values[9], total) ? static_cast<float>(std::clamp(total, 0.0, 100.0)) : 0.0f;
        }
        _sample.heatCount = limit;
        return S_OK;
    }

    [[nodiscard]] HRESULT IngestMemory(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "memory.summary", 12) || snapshot->rowCount == 0)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        const RedXeDataValue* values = RowValues(snapshot->rows[0], 12);
        if (!values)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        uint64_t total = 0;
        uint64_t used = 0;
        uint64_t commit = 0;
        uint64_t commitLimit = 0;
        uint64_t cache = 0;
        _sample.available[0] = TakeU64(values[0], total) && TakeU64(values[2], used) && total > 0;
        _sample.available[1] = TakeU64(values[3], commit) && TakeU64(values[4], commitLimit) && commitLimit > 0;
        _sample.available[2] = TakeU64(values[6], cache);
        _sample.counts[0] = used;
        _sample.counts[1] = total;
        _sample.counts[2] = commit;
        _sample.counts[3] = commitLimit;
        _sample.counts[4] = cache;
        _sample.values[0] = _sample.available[0]
                                ? static_cast<float>(100.0 * static_cast<double>(used) / static_cast<double>(total))
                                : 0.0f;
        _sample.values[1] =
            _sample.available[1]
                ? static_cast<float>(100.0 * static_cast<double>(commit) / static_cast<double>(commitLimit))
                : 0.0f;
        double pageIn = 0.0;
        if (TakeF64(values[10], pageIn))
        {
            PushSpark(static_cast<float>(std::max(pageIn, 0.0)));
        }
        return S_OK;
    }

    [[nodiscard]] HRESULT IngestNetworkInterfaces(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "network.interface", 13))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        ViewerSample next = _sample;
        next.rowCount = 0;
        next.hiddenCount = 0;
        double spark = 0.0;
        double maxUtil = 0.0;
        bool utilAvailable = false;
        const bool haveStatus = snapshot->columnCount >= 6;
        const bool haveLink = snapshot->columnCount >= 9;
        const bool haveUtil = snapshot->columnCount >= 22;
        std::array<NetIdleWatch, kMaximumRows> nextIdle{};
        uint32_t nextIdleCount = 0;
        auto previousIdle = [this](uint64_t identity, bool& found) noexcept -> uint32_t
        {
            found = false;
            for (uint32_t index = 0; index < _netIdleCount; ++index)
            {
                if (_netIdle[index].identity == identity)
                {
                    found = true;
                    return _netIdle[index].idleStreak;
                }
            }
            return 0;
        };
        auto rememberIdle = [&](uint64_t identity, uint32_t streak) noexcept
        {
            if (nextIdleCount >= nextIdle.size())
            {
                return;
            }
            nextIdle[nextIdleCount++] = NetIdleWatch{identity, streak};
        };
        for (uint32_t pass = 0; pass < 2 && next.rowCount == 0; ++pass)
        {
            spark = 0.0;
            next.hiddenCount = 0;
            nextIdleCount = 0;
            for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
            {
                const RedXeDataValue* values = RowValues(snapshot->rows[rowIndex], 13);
                if (!values)
                {
                    continue;
                }
                if ((values[1].valueType == RedXeDataValueTypeUtf16 && values[1].quality == RedXeDataQualityGood &&
                     IsFilterAdapterName(values[1].utf16Value, values[1].utf16Characters)) ||
                    (values[2].valueType == RedXeDataValueTypeUtf16 && values[2].quality == RedXeDataQualityGood &&
                     IsFilterAdapterName(values[2].utf16Value, values[2].utf16Characters)))
                {
                    ++next.hiddenCount;
                    continue;
                }
                if (pass == 0 && haveStatus)
                {
                    uint64_t oper = 0;
                    uint64_t media = 0;
                    const bool operOk = TakeU64(values[4], oper);
                    const bool mediaOk = TakeU64(values[5], media);
                    if (operOk && oper != kIfOperStatusUp)
                    {
                        continue;
                    }
                    if (mediaOk && media != 0 && media != kMediaConnectStateConnected)
                    {
                        continue;
                    }
                }
                RankedRow row{};
                (void)TakeU64(values[0], row.identity);
                CopyName(row.name, row.nameCharacters, values[1], L"Interface");
                double inRate = 0.0;
                double outRate = 0.0;
                const bool inOk = TakeF64(values[11], inRate);
                const bool outOk = TakeF64(values[12], outRate);
                row.primaryAvailable = inOk || outOk;
                row.primary = static_cast<float>(std::max(inRate, 0.0) + std::max(outRate, 0.0));
                uint64_t rxLink = 0;
                uint64_t txLink = 0;
                if (haveLink)
                {
                    (void)TakeU64(values[7], rxLink);
                    (void)TakeU64(values[8], txLink);
                }
                row.secondary = txLink != 0 ? txLink : rxLink;
                bool hadWatch = false;
                uint32_t idleStreak = previousIdle(row.identity, hadWatch);
                if (!row.primaryAvailable || row.primary < kNetworkIdleBytes)
                {
                    idleStreak = hadWatch ? idleStreak + 1 : kNetworkIdleHideSamples;
                    rememberIdle(row.identity, idleStreak);
                    if (idleStreak >= kNetworkIdleHideSamples)
                    {
                        ++next.hiddenCount;
                        continue;
                    }
                }
                else
                {
                    rememberIdle(row.identity, 0);
                }
                if (haveUtil)
                {
                    double util = 0.0;
                    if (TakeF64(values[21], util))
                    {
                        maxUtil = std::max(maxUtil, util);
                        utilAvailable = true;
                    }
                }
                InsertInto(next, row);
                if (row.primaryAvailable)
                {
                    spark += static_cast<double>(row.primary);
                }
            }
        }
        PreserveRowMotion(_sample, next);
        _sample.rowCount = next.rowCount;
        _sample.rows = next.rows;
        _sample.hiddenCount = next.hiddenCount;
        _netIdle = nextIdle;
        _netIdleCount = nextIdleCount;
        _sample.available[7] = utilAvailable;
        _sample.values[7] = static_cast<float>(std::clamp(maxUtil, 0.0, 100.0));
        PushSpark(static_cast<float>(spark));
        return S_OK;
    }

    [[nodiscard]] HRESULT IngestNetworkProtocols(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "network.protocol", 6))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        for (uint32_t index = 0; index < 6 && index < snapshot->rowCount; ++index)
        {
            const RedXeDataValue* values = RowValues(snapshot->rows[index], 6);
            double inRate = 0.0;
            _sample.available[index] = values && TakeF64(values[4], inRate);
            _sample.values[index] = static_cast<float>(std::max(inRate, 0.0));
        }
        return S_OK;
    }

    [[nodiscard]] HRESULT IngestVolumes(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "storage.volume", 5))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        ViewerSample next = _sample;
        next.rowCount = 0;
        const uint32_t limit = _topN == 0 ? 8u : std::min(_topN, 8u);
        for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
        {
            const RedXeDataValue* values = RowValues(snapshot->rows[rowIndex], 5);
            if (!values)
            {
                continue;
            }
            RankedRow row{};
            CopyName(row.name, row.nameCharacters, values[1], L"Volume");
            uint64_t total = 0;
            uint64_t freeBytes = 0;
            row.primaryAvailable = TakeU64(values[3], total) && TakeU64(values[4], freeBytes) && total > 0;
            row.primary = row.primaryAvailable
                              ? static_cast<float>(100.0 * static_cast<double>(total - std::min(freeBytes, total)) /
                                                   static_cast<double>(total))
                              : 0.0f;
            row.secondary = total;
            row.pid = total - std::min(freeBytes, total);
            row.identity = HashUtf16(row.name.data(), row.nameCharacters);
            InsertBounded(next, row, limit);
        }
        PreserveRowMotion(_sample, next);
        _sample.rowCount = next.rowCount;
        _sample.rows = next.rows;
        return S_OK;
    }

    [[nodiscard]] HRESULT IngestDisks(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "storage.disk", 10))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        double activity = 0.0;
        double throughput = 0.0;
        bool available = false;
        bool rateAvailable = false;
        for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
        {
            const RedXeDataValue* values = RowValues(snapshot->rows[rowIndex], 10);
            double active = 0.0;
            if (values && TakeF64(values[5], active))
            {
                activity = std::max(activity, active);
                available = true;
            }
            double readRate = 0.0;
            double writeRate = 0.0;
            const bool readOk = values && TakeF64(values[8], readRate);
            const bool writeOk = values && TakeF64(values[9], writeRate);
            if (readOk || writeOk)
            {
                throughput = std::max(throughput, std::max(readRate, 0.0) + std::max(writeRate, 0.0));
                rateAvailable = true;
            }
        }
        _sample.available[7] = available;
        _sample.values[7] = static_cast<float>(std::clamp(activity, 0.0, 100.0));
        _sample.available[6] = rateAvailable;
        _sample.counts[6] = static_cast<uint64_t>(std::max(throughput, 0.0));
        _sample.values[6] = static_cast<float>(throughput);
        return S_OK;
    }

    [[nodiscard]] HRESULT IngestGpuAdapters(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "gpu.adapter", 15))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        ViewerSample next = _sample;
        next.rowCount = 0;
        next.hiddenCount = 0;
        for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
        {
            const RedXeDataValue* values = RowValues(snapshot->rows[rowIndex], 15);
            if (!values)
            {
                continue;
            }
            RankedRow row{};
            (void)TakeU64(values[0], row.identity);
            CopyName(row.name, row.nameCharacters, values[1], L"Adapter");
            uint64_t software = 0;
            uint64_t integrated = 0;
            (void)TakeU64(values[4], software);
            (void)TakeU64(values[5], integrated);
            if (software != 0)
            {
                row.flags |= kRowFlagSoftware;
                ++next.hiddenCount;
                continue;
            }
            if (integrated != 0)
            {
                row.flags |= kRowFlagIntegrated;
            }
            double temp = 0.0;
            row.primaryAvailable = TakeF64(values[14], temp);
            row.primary = static_cast<float>(temp);
            (void)TakeU64(values[6], row.secondary);
            uint32_t insertAt = next.rowCount;
            if (next.rowCount < 8)
            {
                next.rows[next.rowCount++] = row;
            }
            else
            {
                continue;
            }
            while (insertAt > 0)
            {
                const RankedRow& left = next.rows[insertAt];
                const RankedRow& right = next.rows[insertAt - 1];
                const bool leftDiscrete = (left.flags & kRowFlagIntegrated) == 0;
                const bool rightDiscrete = (right.flags & kRowFlagIntegrated) == 0;
                if (leftDiscrete != rightDiscrete)
                {
                    if (!leftDiscrete)
                    {
                        break;
                    }
                }
                else if (left.secondary <= right.secondary)
                {
                    break;
                }
                std::swap(next.rows[insertAt], next.rows[insertAt - 1]);
                --insertAt;
            }
        }
        PreserveRowMotion(_sample, next);
        _sample.rowCount = next.rowCount;
        _sample.rows = next.rows;
        _sample.hiddenCount = next.hiddenCount;
        if (next.rowCount > 0)
        {
            _sample.available[0] = next.rows[0].primaryAvailable;
            _sample.values[0] = next.rows[0].primary;
        }
        return S_OK;
    }

    [[nodiscard]] HRESULT IngestGpuProcesses(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "gpu.process", 6))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        ViewerSample next = _sample;
        next.rowCount = 0;
        for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
        {
            const RedXeDataValue* values = RowValues(snapshot->rows[rowIndex], 6);
            if (!values)
            {
                continue;
            }
            RankedRow row{};
            (void)TakeU64(values[0], row.pid);
            uint64_t engineOrdinal = 0;
            (void)TakeU64(values[3], engineOrdinal);
            row.identity = (row.pid << 16) | (engineOrdinal & 0xffffull);
            CopyDetail(row.detail, row.detailCharacters, values[4], L"engine");
            ApplyPidName(row);
            double util = 0.0;
            row.primaryAvailable = TakeF64(values[5], util);
            row.primary = row.primaryAvailable ? static_cast<float>(std::clamp(util, 0.0, 100.0)) : 0.0f;
            InsertInto(next, row);
        }
        PreserveRowMotion(_sample, next);
        _sample.rowCount = next.rowCount;
        _sample.rows = next.rows;
        return S_OK;
    }

    [[nodiscard]] HRESULT IngestGpuProcessNames(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "process.list", 2))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
        {
            const RedXeDataValue* values = RowValues(snapshot->rows[rowIndex], 2);
            if (!values || values[0].valueType != RedXeDataValueTypeUInt64)
            {
                continue;
            }
            const uint64_t pid = values[0].uint64Value;
            bool needed = false;
            for (uint32_t gpuIndex = 0; gpuIndex < _sample.rowCount; ++gpuIndex)
            {
                if (_sample.rows[gpuIndex].pid == pid)
                {
                    needed = true;
                    break;
                }
            }
            if (!needed)
            {
                continue;
            }
            RememberPidName(pid, values[1]);
        }
        for (uint32_t index = 0; index < _sample.rowCount; ++index)
        {
            ApplyPidName(_sample.rows[index]);
        }
        return S_OK;
    }

    void RememberPidName(uint64_t pid, const RedXeDataValue& value) noexcept
    {
        if (pid == 0)
        {
            return;
        }
        for (uint32_t index = 0; index < _pidNameCount; ++index)
        {
            if (_pidNames[index].pid == pid)
            {
                CopyName(_pidNames[index].name, _pidNames[index].characters, value, nullptr);
                return;
            }
        }
        if (_pidNameCount >= _pidNames.size())
        {
            return;
        }
        PidNameEntry& entry = _pidNames[_pidNameCount++];
        entry.pid = pid;
        CopyName(entry.name, entry.characters, value, nullptr);
    }

    void ApplyPidName(RankedRow& row) noexcept
    {
        for (uint32_t index = 0; index < _pidNameCount; ++index)
        {
            if (_pidNames[index].pid == row.pid && _pidNames[index].characters > 0)
            {
                row.name = _pidNames[index].name;
                row.nameCharacters = _pidNames[index].characters;
                return;
            }
        }
        if (row.nameCharacters == 0)
        {
            FormatPid(row.name.data(), static_cast<uint32_t>(row.name.size()), row.pid);
            row.nameCharacters = static_cast<uint32_t>(wcsnlen(row.name.data(), row.name.size()));
        }
    }

    [[nodiscard]] HRESULT IngestPower(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "power.summary", 5) || snapshot->rowCount == 0)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        const RedXeDataValue* values = RowValues(snapshot->rows[0], 5);
        if (!values)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        uint64_t ac = 0;
        uint64_t present = 0;
        uint64_t charging = 0;
        _sample.acOnline = TakeU64(values[0], ac) && ac != 0;
        _sample.batteryPresent = TakeU64(values[1], present) && present != 0;
        _sample.charging = TakeU64(values[2], charging) && charging != 0;
        double charge = 0.0;
        _sample.available[0] = TakeF64(values[4], charge);
        _sample.values[0] = static_cast<float>(std::clamp(charge, 0.0, 100.0));
        return S_OK;
    }

    [[nodiscard]] HRESULT IngestBatteries(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "battery.list", 2))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        _sample.batteryCount = snapshot->rowCount;
        if (snapshot->rowCount > 0)
        {
            const RedXeDataValue* values = RowValues(snapshot->rows[0], 2);
            if (values)
            {
                CopyName(_sample.rows[0].name, _sample.rows[0].nameCharacters, values[1], L"Battery");
                _sample.rowCount = 1;
            }
        }
        return S_OK;
    }

    [[nodiscard]] HRESULT IngestThermal(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "thermal.sensor", 4))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        ViewerSample next = _sample;
        next.rowCount = 0;
        for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
        {
            const RedXeDataValue* values = RowValues(snapshot->rows[rowIndex], 4);
            if (!values)
            {
                continue;
            }
            RankedRow row{};
            CopyName(row.name, row.nameCharacters, values[2], L"Sensor");
            double temp = 0.0;
            row.primaryAvailable = TakeF64(values[3], temp);
            row.primary = static_cast<float>(temp);
            row.identity = static_cast<uint64_t>(rowIndex);
            if (values[0].valueType == RedXeDataValueTypeUtf16 && values[0].utf16Value && values[0].utf16Characters > 0)
            {
                uint64_t hash = 14695981039346656037ull;
                for (uint32_t i = 0; i < values[0].utf16Characters; ++i)
                {
                    hash ^= values[0].utf16Value[i];
                    hash *= 1099511628211ull;
                }
                row.identity = hash;
            }
            InsertBounded(next, row, 8);
        }
        PreserveRowMotion(_sample, next);
        _sample.rowCount = next.rowCount;
        _sample.rows = next.rows;
        return S_OK;
    }

    [[nodiscard]] HRESULT IngestFans(const RedXeDataSnapshot* snapshot) noexcept
    {
        if (!SnapshotMatches(snapshot, "fan.sensor", 4))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        uint64_t rpm = 0;
        uint64_t maxRpm = 0;
        bool available = false;
        for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
        {
            const RedXeDataValue* values = RowValues(snapshot->rows[rowIndex], 4);
            uint64_t current = 0;
            uint64_t maximum = 0;
            if (values && TakeU64(values[2], current))
            {
                rpm = std::max(rpm, current);
                available = true;
            }
            if (values && TakeU64(values[3], maximum))
            {
                maxRpm = std::max(maxRpm, maximum);
            }
        }
        _sample.available[7] = available;
        _sample.counts[6] = rpm;
        _sample.counts[7] = maxRpm;
        _sample.values[7] = (available && maxRpm > 0)
                                ? static_cast<float>(100.0 * static_cast<double>(rpm) / static_cast<double>(maxRpm))
                                : 0.0f;
        return S_OK;
    }

    struct PanelMetrics final
    {
        float pad = 0.0f;
        float radius = 6.0f;
        float titleH = 0.0f;
        float contentTop = 0.0f;
        float innerW = 0.0f;
        float innerH = 0.0f;
        float labelPx = kLabelFloorPx;
        float rowPx = kRowFloorPx;
        float kpiPx = kKpiFloorPx;
        float heroPx = kHeroFloorPx;
        float titlePx = kTitleFloorPx;
        float rowMin = kRowMinFloorPx;
        ViewerDensity density = ViewerDensity::Standard;
        bool showTitle = true;
    };

    [[nodiscard]] PanelMetrics MakePanel(float width, float height) const noexcept
    {
        PanelMetrics metrics;
        metrics.pad = std::clamp(std::min(width, height) * 0.04f, kPadFloorPx, 18.0f);
        const float scale = std::clamp(std::min(width, height) / 220.0f, 1.0f, 1.85f);
        const float titleScale = std::clamp(width / 280.0f, 1.0f, 1.85f);
        metrics.labelPx = std::clamp(kLabelFloorPx * scale, kLabelFloorPx, 26.0f);
        metrics.rowPx = std::clamp(kRowFloorPx * scale, kRowFloorPx, 28.0f);
        metrics.kpiPx = std::clamp(kKpiFloorPx * scale, kKpiFloorPx, 56.0f);
        metrics.heroPx = std::clamp(kHeroFloorPx * scale, kHeroFloorPx, 88.0f);
        metrics.titlePx = std::clamp(kTitleFloorPx * titleScale, kTitleFloorPx, 44.0f);
        metrics.rowMin = std::max(metrics.rowPx + 10.0f, kRowMinFloorPx);
        metrics.showTitle = height >= 52.0f;
        metrics.titleH = metrics.showTitle ? metrics.titlePx : 0.0f;
        metrics.contentTop = metrics.pad + (metrics.showTitle ? metrics.titleH + 6.0f : 0.0f);
        metrics.innerW = std::max(8.0f, width - metrics.pad * 2.0f);
        metrics.innerH = std::max(0.0f, height - metrics.contentTop - metrics.pad);
        metrics.density = _raised ? ViewerDensity::Standard : DensityForInner(metrics.innerH);
        return metrics;
    }

    [[nodiscard]] HRESULT BuildScene(ViewerGpuResources& resources, ViewerDrawList& list, const ViewerSample& sample,
                                     float width, float height, float pulse) noexcept
    {
        const PanelMetrics panel = MakePanel(width, height);
        const float chromeW = std::max(1.0f, width - kPanelInset * 2.0f);
        const float chromeH = std::max(1.0f, height - kPanelInset * 2.0f);
        (void)list.AddFill(kPanelInset, kPanelInset, chromeW, chromeH, kPanelR, kPanelG, kPanelB, 1.0f, panel.radius);
        (void)list.AddStroke(kPanelInset + 0.5f, kPanelInset + 0.5f, chromeW - 1.0f, chromeH - 1.0f, kHairline,
                             kHairline, kHairline, 0.9f, panel.radius, 1.0f);
        if (pulse > 0.0f)
        {
            (void)list.AddFill(kPanelInset, kPanelInset, chromeW, 2.0f, kAccentR, kAccentG, kAccentB,
                               0.35f + pulse * 0.65f, 0.0f);
        }
        const ViewerCatalogEntry& entry = Catalog(_kind);
        const HRESULT glyphs = EnsureSceneGlyphs(resources, sample, entry);
        if (FAILED(glyphs))
        {
            return glyphs;
        }
        if (panel.showTitle)
        {
            (void)AppendClippedText(resources, list, panel.pad, panel.pad, panel.titlePx, panel.innerW, 0.88f, 0.88f,
                                    0.90f, 1.0f, entry.typeName, static_cast<uint32_t>(wcsnlen(entry.typeName, 64)));
        }
        switch (_kind)
        {
        case ViewerKind::ProcessViewer:
            return DrawProcessTable(resources, list, sample, panel, width, height);
        case ViewerKind::GpuProcesses:
            return DrawGpuProcessTable(resources, list, sample, panel, width, height);
        case ViewerKind::NetworkMeter:
            return DrawNetwork(resources, list, sample, panel, width, height);
        case ViewerKind::SystemPulse:
            return DrawPulse(resources, list, sample, panel, width, height);
        case ViewerKind::CpuMeter:
            return DrawCpu(resources, list, sample, panel, width, height);
        case ViewerKind::MemoryMeter:
            return DrawMemory(resources, list, sample, panel, width, height);
        case ViewerKind::StorageMeter:
            return DrawStorage(resources, list, sample, panel, width, height);
        case ViewerKind::GpuMeter:
            return DrawGpu(resources, list, sample, panel, width, height);
        case ViewerKind::PowerMeter:
            return DrawPower(resources, list, sample, panel, width, height);
        case ViewerKind::ThermalMeter:
            return DrawThermal(resources, list, sample, panel, width, height);
        default:
            return S_OK;
        }
    }

    [[nodiscard]] HRESULT EnsureSceneGlyphs(ViewerGpuResources& resources, const ViewerSample& sample,
                                            const ViewerCatalogEntry& entry) noexcept
    {
        HRESULT result = resources.EnsureGlyphs(entry.typeName, static_cast<uint32_t>(wcsnlen(entry.typeName, 64)));
        if (FAILED(result))
        {
            return result;
        }
        result = resources.EnsureGlyphs(kDegreeCelsius, 2);
        if (FAILED(result))
        {
            return result;
        }
        for (uint32_t index = 0; index < sample.rowCount; ++index)
        {
            result = resources.EnsureGlyphs(sample.rows[index].name.data(), sample.rows[index].nameCharacters);
            if (FAILED(result))
            {
                return result;
            }
            if (sample.rows[index].detailCharacters > 0)
            {
                result = resources.EnsureGlyphs(sample.rows[index].detail.data(), sample.rows[index].detailCharacters);
                if (FAILED(result))
                {
                    return result;
                }
            }
        }
        return S_OK;
    }

    void DrawTrack(ViewerDrawList& list, float x, float y, float width, float height, float fill01, bool available,
                   bool thermal, float celsius) noexcept
    {
        (void)list.AddFill(x, y, width, height, kTroughR, kTroughG, kTroughB, 1.0f, height * 0.5f);
        if (!available)
        {
            return;
        }
        float red = kOkR;
        float green = kOkG;
        float blue = kOkB;
        if (thermal)
        {
            ThermalColor(celsius, red, green, blue);
        }
        else
        {
            IntentTextColor(fill01, true, red, green, blue);
        }
        const float fillWidth = std::max(2.0f, width * std::clamp(fill01, 0.0f, 1.0f));
        (void)list.AddFill(x, y, fillWidth, height, red, green, blue, 1.0f, height * 0.5f);
    }

    void DrawCpuTrack(ViewerDrawList& list, float x, float y, float width, float height, float percent,
                      bool available) noexcept
    {
        (void)list.AddFill(x, y, width, height, kTroughR, kTroughG, kTroughB, 1.0f, height * 0.5f);
        if (!available)
        {
            return;
        }
        const float t = CpuUnit(percent);
        if (t <= 0.0f)
        {
            return;
        }
        const float lum = CpuHeatLuminance(t);
        const float mix = std::max(lum, std::sqrt(t));
        float red = kFillR;
        float green = kFillG;
        float blue = kFillB;
        SignalColor(t, red, green, blue);
        red = Lerp(kHeatIdle, red, mix);
        green = Lerp(kHeatIdle, green, mix);
        blue = Lerp(kHeatIdle, blue, mix);
        const float fillWidth = std::max(height, width * std::sqrt(t));
        (void)list.AddFill(x, y, fillWidth, height, red, green, blue, 1.0f, height * 0.5f);
    }

    void DrawThermalLevel(ViewerDrawList& list, float x, float y, float width, float height, float celsius,
                          bool available) noexcept
    {
        const float fill = available ? ThermalFill(celsius) : 0.0f;
        DrawTrack(list, x, y, width, height, fill, available, true, celsius);
        if (!available || width < 24.0f)
        {
            return;
        }
        const float warnX = x + width * ThermalFill(kThermalWarnC);
        const float hotX = x + width * ThermalFill(kThermalHotC);
        (void)list.AddFill(warnX, y, 1.5f, height, kWarnR, kWarnG, kWarnB, 0.75f, 0.0f);
        (void)list.AddFill(hotX, y, 1.5f, height, kAccentR, kAccentG, kAccentB, 0.85f, 0.0f);
    }

    void DrawOverflow(ViewerGpuResources& resources, ViewerDrawList& list, float x, float y, float size,
                      uint32_t hidden) noexcept
    {
        if (hidden == 0)
        {
            return;
        }
        wchar_t extra[16]{};
        (void)swprintf_s(extra, 16, L"+%u", hidden);
        (void)resources.AppendText(list, x, y, size, kMuted, kMuted, kMuted, 1.0f, extra,
                                   static_cast<uint32_t>(wcsnlen(extra, 16)));
    }

    [[nodiscard]] HRESULT DrawProcessTable(ViewerGpuResources& resources, ViewerDrawList& list,
                                           const ViewerSample& sample, const PanelMetrics& panel, float width,
                                           float height) noexcept
    {
        const float y0 = panel.contentTop;
        if (sample.rowCount == 0)
        {
            return resources.AppendText(list, panel.pad, y0, panel.kpiPx, kMuted, kMuted, kMuted, 1.0f, L"--", 2);
        }
        if (panel.density == ViewerDensity::Hero)
        {
            wchar_t cpu[16]{};
            FormatPercent(cpu, 16, sample.rows[0].displayPrimary, sample.rows[0].primaryAvailable);
            float ir = kTextR;
            float ig = kTextG;
            float ib = kTextB;
            IntentTextColor(sample.rows[0].displayPrimary / 100.0f, sample.rows[0].primaryAvailable, ir, ig, ib);
            (void)resources.AppendText(list, panel.pad, y0, panel.heroPx, ir, ig, ib, 1.0f, cpu,
                                       static_cast<uint32_t>(wcsnlen(cpu, 16)));
            (void)AppendClippedText(resources, list, panel.pad, y0 + panel.heroPx + 4.0f, panel.labelPx, panel.innerW,
                                    kMuted, kMuted, kMuted, 1.0f, sample.rows[0].name.data(),
                                    sample.rows[0].nameCharacters);
            DrawOverflow(resources, list, panel.pad, height - panel.pad - panel.labelPx, panel.labelPx,
                         sample.rowCount > 1 ? sample.rowCount - 1 : 0);
            (void)width;
            return S_OK;
        }
        uint32_t columns = 1;
        uint32_t visible = 0;
        uint32_t rows = 0;
        float rowHeight = 0.0f;
        float listHeight = 0.0f;
        float colWidth = 0.0f;
        const float rowMin = std::max(panel.rowMin, 36.0f);
        auto layout = [&](float minCol) noexcept
        {
            FitGridLayout(panel.innerH, panel.innerW, rowMin, panel.labelPx + 4.0f, minCol, sample.rowCount, columns,
                          visible, rows, rowHeight, listHeight, colWidth);
        };
        layout(300.0f);
        float rowPx = TypeFromRow(rowHeight, panel.rowPx, 32.0f);
        float cpuReserve = std::max(52.0f, resources.MeasureText(L"100%", 4, rowPx) + 14.0f);
        float wsReserve = std::max(80.0f, resources.MeasureText(L"999.9 TB", 8, rowPx) + 14.0f);
        if (columns > 1 && cpuReserve + wsReserve + 72.0f > colWidth)
        {
            layout(panel.innerW);
            rowPx = TypeFromRow(rowHeight, panel.rowPx, 32.0f);
        }
        const float trackH = ClampOrdered(rowHeight * 0.22f, 8.0f, 14.0f);
        for (uint32_t index = 0; index < visible; ++index)
        {
            const RankedRow& row = sample.rows[index];
            float cellX = 0.0f;
            float cellY = 0.0f;
            CellOrigin(panel.pad, y0, index, columns, colWidth, rowHeight, row.displayY, columns == 1, cellX, cellY);
            const float trackY = std::min(cellY + 2.0f + rowPx + 4.0f, cellY + rowHeight - trackH - 2.0f);
            DrawCpuTrack(list, cellX, trackY, colWidth, trackH, row.displayPrimary, row.primaryAvailable);
            wchar_t ws[16]{};
            FormatBytes(ws, 16, row.secondary, true);
            wchar_t cpu[16]{};
            FormatPercent(cpu, 16, row.displayPrimary, row.primaryAvailable);
            const float cpuW = resources.MeasureText(cpu, static_cast<uint32_t>(wcsnlen(cpu, 16)), rowPx) + 2.0f;
            const float wsW = resources.MeasureText(ws, static_cast<uint32_t>(wcsnlen(ws, 16)), rowPx) + 12.0f;
            const float cpuX = cellX + colWidth - cpuW;
            const float wsX = cpuX - wsW;
            const float nameWidth = std::max(24.0f, wsX - cellX - 6.0f);
            (void)AppendClippedText(resources, list, cellX, cellY + 2.0f, rowPx, nameWidth, kTextR, kTextG, kTextB,
                                    1.0f, row.name.data(), row.nameCharacters);
            (void)resources.AppendText(list, wsX, cellY + 2.0f, rowPx, kMuted, kMuted, kMuted, 1.0f, ws,
                                       static_cast<uint32_t>(wcsnlen(ws, 16)));
            float ir = kTextR;
            float ig = kTextG;
            float ib = kTextB;
            IntentTextColor(row.displayPrimary / 100.0f, row.primaryAvailable, ir, ig, ib);
            (void)resources.AppendText(list, cpuX, cellY + 2.0f, rowPx, ir, ig, ib, 1.0f, cpu,
                                       static_cast<uint32_t>(wcsnlen(cpu, 16)));
        }
        DrawOverflow(resources, list, panel.pad, y0 + listHeight, panel.labelPx, sample.rowCount - visible);
        (void)width;
        return S_OK;
    }

    [[nodiscard]] HRESULT DrawGpuProcessTable(ViewerGpuResources& resources, ViewerDrawList& list,
                                              const ViewerSample& sample, const PanelMetrics& panel, float width,
                                              float height) noexcept
    {
        const float y0 = panel.contentTop;
        if (sample.rowCount == 0)
        {
            return resources.AppendText(list, panel.pad, y0, panel.kpiPx, kMuted, kMuted, kMuted, 1.0f, L"--", 2);
        }
        if (panel.density == ViewerDensity::Hero)
        {
            wchar_t gpu[16]{};
            FormatPercent(gpu, 16, sample.rows[0].displayPrimary, sample.rows[0].primaryAvailable);
            float ir = kTextR;
            float ig = kTextG;
            float ib = kTextB;
            IntentTextColor(sample.rows[0].displayPrimary / 100.0f, sample.rows[0].primaryAvailable, ir, ig, ib);
            (void)resources.AppendText(list, panel.pad, y0, panel.heroPx, ir, ig, ib, 1.0f, gpu,
                                       static_cast<uint32_t>(wcsnlen(gpu, 16)));
            (void)AppendClippedText(resources, list, panel.pad, y0 + panel.heroPx + 4.0f, panel.labelPx, panel.innerW,
                                    kMuted, kMuted, kMuted, 1.0f, sample.rows[0].name.data(),
                                    sample.rows[0].nameCharacters);
            (void)width;
            return S_OK;
        }
        uint32_t columns = 1;
        uint32_t visible = 0;
        uint32_t rows = 0;
        float rowHeight = 0.0f;
        float listHeight = 0.0f;
        float colWidth = 0.0f;
        const float rowMin = std::max(panel.rowMin, 36.0f);
        auto layout = [&](float minCol) noexcept
        {
            FitGridLayout(panel.innerH, panel.innerW, rowMin, panel.labelPx + 4.0f, minCol, sample.rowCount, columns,
                          visible, rows, rowHeight, listHeight, colWidth);
        };
        layout(280.0f);
        float rowPx = TypeFromRow(rowHeight, panel.rowPx, 32.0f);
        float gpuReserve = std::max(52.0f, resources.MeasureText(L"100%", 4, rowPx) + 14.0f);
        if (columns > 1 && gpuReserve + 96.0f > colWidth)
        {
            layout(panel.innerW);
            rowPx = TypeFromRow(rowHeight, panel.rowPx, 32.0f);
            gpuReserve = std::max(52.0f, resources.MeasureText(L"100%", 4, rowPx) + 14.0f);
        }
        const float trackH = ClampOrdered(rowHeight * 0.22f, 8.0f, 14.0f);
        const bool showEngine = columns == 1 && colWidth >= gpuReserve + 160.0f;
        for (uint32_t index = 0; index < visible; ++index)
        {
            const RankedRow& row = sample.rows[index];
            float cellX = 0.0f;
            float cellY = 0.0f;
            CellOrigin(panel.pad, y0, index, columns, colWidth, rowHeight, row.displayY, columns == 1, cellX, cellY);
            const float trackY = std::min(cellY + 2.0f + rowPx + 4.0f, cellY + rowHeight - trackH - 2.0f);
            DrawCpuTrack(list, cellX, trackY, colWidth, trackH, row.displayPrimary, row.primaryAvailable);
            wchar_t gpu[16]{};
            FormatPercent(gpu, 16, row.displayPrimary, row.primaryAvailable);
            const float gpuW = resources.MeasureText(gpu, static_cast<uint32_t>(wcsnlen(gpu, 16)), rowPx) + 2.0f;
            const float gpuX = cellX + colWidth - gpuW;
            const float engineW = showEngine && row.detailCharacters > 0 ? 80.0f : 0.0f;
            const float engineX = gpuX - engineW;
            const float nameWidth = std::max(24.0f, engineX - cellX - 6.0f);
            (void)AppendClippedText(resources, list, cellX, cellY + 2.0f, rowPx, nameWidth, kTextR, kTextG, kTextB,
                                    1.0f, row.name.data(), row.nameCharacters);
            if (engineW > 0.0f)
            {
                (void)AppendClippedText(resources, list, engineX, cellY + 2.0f, rowPx, engineW - 8.0f, kMuted, kMuted,
                                        kMuted, 1.0f, row.detail.data(), row.detailCharacters);
            }
            float ir = kTextR;
            float ig = kTextG;
            float ib = kTextB;
            IntentTextColor(row.displayPrimary / 100.0f, row.primaryAvailable, ir, ig, ib);
            (void)resources.AppendText(list, gpuX, cellY + 2.0f, rowPx, ir, ig, ib, 1.0f, gpu,
                                       static_cast<uint32_t>(wcsnlen(gpu, 16)));
        }
        DrawOverflow(resources, list, panel.pad, y0 + listHeight, panel.labelPx, sample.rowCount - visible);
        (void)width;
        (void)height;
        return S_OK;
    }

    [[nodiscard]] HRESULT DrawNetwork(ViewerGpuResources& resources, ViewerDrawList& list, const ViewerSample& sample,
                                      const PanelMetrics& panel, float width, float height) noexcept
    {
        float y = panel.contentTop;
        float remaining = height - panel.pad - y;
        if (panel.density != ViewerDensity::Hero && remaining > panel.rowMin * 2.4f)
        {
            const float sparkHeight = ClampOrdered(remaining * 0.36f, 48.0f, remaining * 0.44f);
            DrawHistoryArea(list, panel.pad, y, panel.innerW, sparkHeight, sample, 0.0f, false, false);
            y += sparkHeight + 8.0f;
            remaining = height - panel.pad - y;
        }
        if (sample.rowCount == 0)
        {
            if (sample.hiddenCount > 0)
            {
                wchar_t idle[24]{};
                (void)swprintf_s(idle, 24, L"+%u idle", sample.hiddenCount);
                return resources.AppendText(list, panel.pad, y, panel.kpiPx, kMuted, kMuted, kMuted, 1.0f, idle,
                                            static_cast<uint32_t>(wcsnlen(idle, 24)));
            }
            return resources.AppendText(list, panel.pad, y, panel.kpiPx, kMuted, kMuted, kMuted, 1.0f, L"--", 2);
        }
        if (panel.density == ViewerDensity::Hero)
        {
            wchar_t rate[32]{};
            FormatRate(rate, 32, sample.rows[0].displayPrimary, sample.rows[0].primaryAvailable);
            const float fill =
                LogRateFill(static_cast<double>(sample.rows[0].displayPrimary), sample.rows[0].secondary);
            float ir = kTextR;
            float ig = kTextG;
            float ib = kTextB;
            IntentTextColor(fill, sample.rows[0].primaryAvailable, ir, ig, ib);
            (void)resources.AppendText(list, panel.pad, y, panel.heroPx, ir, ig, ib, 1.0f, rate,
                                       static_cast<uint32_t>(wcsnlen(rate, 32)));
            (void)AppendClippedText(resources, list, panel.pad, y + panel.heroPx + 4.0f, panel.labelPx, panel.innerW,
                                    kMuted, kMuted, kMuted, 1.0f, sample.rows[0].name.data(),
                                    sample.rows[0].nameCharacters);
            return S_OK;
        }
        uint32_t columns = 1;
        uint32_t visible = 0;
        uint32_t rows = 0;
        float rowHeight = 0.0f;
        float listHeight = 0.0f;
        float colWidth = 0.0f;
        FitGridLayout(remaining, panel.innerW, panel.rowMin, panel.labelPx + 4.0f, 240.0f, sample.rowCount, columns,
                      visible, rows, rowHeight, listHeight, colWidth);
        const float rowPx = TypeFromRow(rowHeight, panel.rowPx, 32.0f);
        const float trackH = ClampOrdered(rowHeight * 0.18f, 8.0f, 12.0f);
        const float textGap = 6.0f;
        for (uint32_t index = 0; index < visible; ++index)
        {
            const RankedRow& row = sample.rows[index];
            float cellX = 0.0f;
            float cellY = 0.0f;
            CellOrigin(panel.pad, y, index, columns, colWidth, rowHeight, row.displayY, columns == 1, cellX, cellY);
            const float fill = LogRateFill(static_cast<double>(row.displayPrimary), row.secondary);
            wchar_t rate[32]{};
            FormatRate(rate, 32, row.displayPrimary, row.primaryAvailable);
            const float rateWidth = resources.MeasureText(rate, static_cast<uint32_t>(wcsnlen(rate, 32)), rowPx);
            float ir = kTextR;
            float ig = kTextG;
            float ib = kTextB;
            IntentTextColor(fill, row.primaryAvailable, ir, ig, ib);
            (void)AppendClippedText(resources, list, cellX, cellY + 2.0f, rowPx, colWidth - rateWidth - 8.0f, kTextR,
                                    kTextG, kTextB, 1.0f, row.name.data(), row.nameCharacters);
            (void)resources.AppendText(list, cellX + colWidth - rateWidth, cellY + 2.0f, rowPx, ir, ig, ib, 1.0f, rate,
                                       static_cast<uint32_t>(wcsnlen(rate, 32)));
            const float trackY = std::min(cellY + 2.0f + rowPx + textGap, cellY + rowHeight - trackH - 2.0f);
            DrawTrack(list, cellX, trackY, colWidth, trackH, fill, row.primaryAvailable, false, 0.0f);
        }
        const float packedBottom = static_cast<float>(rows) * std::min(rowHeight, rowPx + textGap + trackH + 10.0f);
        DrawOverflow(resources, list, panel.pad, y + packedBottom, panel.labelPx,
                     (sample.rowCount - visible) + sample.hiddenCount);
        (void)width;
        (void)listHeight;
        return S_OK;
    }

    [[nodiscard]] HRESULT DrawPulse(ViewerGpuResources& resources, ViewerDrawList& list, const ViewerSample& sample,
                                    const PanelMetrics& panel, float width, float height) noexcept
    {
        wchar_t cpu[16]{};
        FormatPercent(cpu, 16, sample.display[0], sample.available[0]);
        float cpuR = kTextR;
        float cpuG = kTextG;
        float cpuB = kTextB;
        IntentTextColor(sample.display[0] / 100.0f, sample.available[0], cpuR, cpuG, cpuB);
        float y = panel.contentTop;
        const float remainingAll = std::max(0.0f, height - panel.pad - y);
        if (panel.density == ViewerDensity::Hero)
        {
            return resources.AppendText(list, panel.pad, y, panel.heroPx, cpuR, cpuG, cpuB, 1.0f, cpu,
                                        static_cast<uint32_t>(wcsnlen(cpu, 16)));
        }
        wchar_t ram[32]{};
        wchar_t procs[16]{};
        wchar_t up[32]{};
        wchar_t threads[16]{};
        wchar_t handles[16]{};
        wchar_t cores[16]{};
        wchar_t commit[32]{};
        wchar_t freeRam[32]{};
        FormatBytes(ram, 32, sample.counts[4], sample.available[4]);
        FormatCount(procs, 16, sample.counts[1], sample.available[1]);
        FormatUptime(up, 32, sample.counts[5], sample.available[5]);
        FormatCount(threads, 16, sample.counts[2], sample.available[2]);
        FormatCount(handles, 16, sample.counts[3], sample.available[3]);
        FormatCount(cores, 16, sample.counts[6], sample.available[6]);
        FormatBytes(commit, 32, sample.counts[7], sample.available[7]);
        const uint64_t freeBytes = sample.counts[0] > sample.counts[4] ? sample.counts[0] - sample.counts[4] : 0;
        FormatBytes(freeRam, 32, freeBytes, sample.counts[0] != 0);
        const wchar_t* labels[] = {L"RAM", L"Procs", L"Threads", L"Handles", L"Cores", L"Up", L"Commit", L"Free"};
        const uint32_t labelLens[] = {3, 5, 7, 7, 5, 2, 6, 4};
        const wchar_t* values[] = {ram, procs, threads, handles, cores, up, commit, freeRam};
        const bool expanded = _raised && remainingAll > 280.0f;
        const uint32_t chipMax = expanded ? 8u : (panel.density == ViewerDensity::Compact ? 4u : 7u);
        const bool split = panel.innerW >= 300.0f;
        const float headerH = expanded ? std::min(remainingAll * 0.40f, 260.0f) : remainingAll;
        const float leftW = split ? std::clamp(panel.innerW * 0.32f, 96.0f, 168.0f) : panel.innerW;
        const float cpuPx = ClampOrdered(split ? headerH * 0.42f : headerH * 0.32f, 44.0f, panel.heroPx);
        const float cpuY = split ? y + std::max(0.0f, (headerH - cpuPx) * 0.12f) : y;
        (void)resources.AppendText(list, panel.pad, cpuY, cpuPx, cpuR, cpuG, cpuB, 1.0f, cpu,
                                   static_cast<uint32_t>(wcsnlen(cpu, 16)));
        float chipX = panel.pad;
        float chipY = y;
        float chipW = panel.innerW;
        float chipH = headerH;
        if (split)
        {
            chipX = panel.pad + leftW + 12.0f;
            chipW = std::max(80.0f, panel.innerW - leftW - 12.0f);
        }
        else
        {
            chipY = cpuY + cpuPx + 8.0f;
            chipH = std::max(0.0f, y + headerH - chipY);
        }
        uint32_t columns = 1;
        uint32_t visible = 0;
        uint32_t rows = 0;
        float rowHeight = 0.0f;
        float listHeight = 0.0f;
        float colWidth = 0.0f;
        FitGridLayout(chipH, chipW, panel.rowMin * 0.75f, 0.0f, 130.0f, chipMax, columns, visible, rows, rowHeight,
                      listHeight, colWidth);
        visible = std::min(visible, chipMax);
        const float valuePx = TypeFromRow(rowHeight, panel.rowPx, 30.0f);
        const float labelPx = std::min(panel.labelPx + 4.0f, valuePx);
        const float labelCol = std::max(64.0f, resources.MeasureText(L"Threads", 7, labelPx) + 8.0f);
        for (uint32_t index = 0; index < visible; ++index)
        {
            float cellX = 0.0f;
            float cellY = 0.0f;
            CellOrigin(chipX, chipY, index, columns, colWidth, rowHeight, 0.0f, false, cellX, cellY);
            (void)resources.AppendText(list, cellX, cellY, labelPx, kMuted, kMuted, kMuted, 1.0f, labels[index],
                                       labelLens[index]);
            (void)AppendClippedText(resources, list, cellX + labelCol, cellY, valuePx,
                                    std::max(24.0f, colWidth - labelCol), kTextR, kTextG, kTextB, 1.0f, values[index],
                                    static_cast<uint32_t>(wcsnlen(values[index], 32)));
        }
        if (expanded)
        {
            float extraY = std::max(cpuY + cpuPx, chipY + static_cast<float>(rows) * rowHeight) + 18.0f;
            float extraH = std::max(0.0f, height - panel.pad - extraY);
            if (extraH >= 56.0f && sample.counts[0] != 0)
            {
                (void)resources.EnsureGlyphs(L"Physical", 8);
                const float bandH = std::min(72.0f, extraH * 0.32f);
                DrawCapacityBar(resources, list, panel.pad, extraY, panel.innerW, L"Physical", sample.display[4],
                                sample.counts[0] != 0, sample.counts[4], sample.counts[0], panel.labelPx + 2.0f, 12.0f,
                                bandH);
                extraY += bandH + 10.0f;
                extraH = std::max(0.0f, height - panel.pad - extraY);
            }
            if (extraH >= 48.0f)
            {
                DrawHistoryArea(list, panel.pad, extraY, panel.innerW, extraH, sample, sample.display[0],
                                sample.available[0], true);
            }
        }
        (void)width;
        return S_OK;
    }

    [[nodiscard]] HRESULT DrawCpu(ViewerGpuResources& resources, ViewerDrawList& list, const ViewerSample& sample,
                                  const PanelMetrics& panel, float width, float height) noexcept
    {
        wchar_t value[16]{};
        FormatPercent(value, 16, sample.display[0], sample.available[0]);
        float ir = kTextR;
        float ig = kTextG;
        float ib = kTextB;
        IntentTextColor(sample.display[0] / 100.0f, sample.available[0], ir, ig, ib);
        float y = panel.contentTop;
        const float remainingAll = std::max(0.0f, height - panel.pad - y);
        if (panel.density == ViewerDensity::Hero)
        {
            return resources.AppendText(list, panel.pad, y, panel.heroPx, ir, ig, ib, 1.0f, value,
                                        static_cast<uint32_t>(wcsnlen(value, 16)));
        }
        const float hero = ClampOrdered(remainingAll * 0.18f, 32.0f, std::min(52.0f, remainingAll * 0.26f));
        (void)resources.AppendText(list, panel.pad, y, hero, ir, ig, ib, 1.0f, value,
                                   static_cast<uint32_t>(wcsnlen(value, 16)));
        y += hero + 4.0f;
        const float barH = ClampOrdered((height - panel.pad - y) * 0.07f, 8.0f, 14.0f);
        {
            const float user = CpuUnit(sample.display[1]);
            const float kernel = CpuUnit(sample.display[2]);
            const float load = CpuUnit(sample.display[0]);
            const float lum = CpuHeatLuminance(load);
            const float mix = std::max(lum, std::sqrt(load));
            float red = kFillR;
            float green = kFillG;
            float blue = kFillB;
            SignalColor(load, red, green, blue);
            red = Lerp(kHeatIdle, red, mix);
            green = Lerp(kHeatIdle, green, mix);
            blue = Lerp(kHeatIdle, blue, mix);
            (void)list.AddFill(panel.pad, y, panel.innerW, barH, kTroughR, kTroughG, kTroughB, 1.0f, 4.0f);
            (void)list.AddFill(panel.pad, y, panel.innerW * user, barH, red, green, blue, 0.95f, 4.0f);
            (void)list.AddFill(panel.pad + panel.innerW * user, y, panel.innerW * kernel, barH, kWarnR, kWarnG, kWarnB,
                               0.95f, 4.0f);
        }
        y += barH + 8.0f;
        const float areaH = std::max(0.0f, height - panel.pad - y);
        if (sample.heatCount == 0)
        {
            DrawHistoryArea(list, panel.pad, y, panel.innerW, areaH, sample, sample.display[0], sample.available[0],
                            true);
            return S_OK;
        }
        const uint32_t heatCols = std::max(1U, static_cast<uint32_t>(std::ceil(std::sqrt(sample.heatCount))));
        const uint32_t heatRows = (sample.heatCount + heatCols - 1) / heatCols;
        const float gap = 2.0f;
        const float histMin = 80.0f;
        const float heatBudgetW =
            areaH >= 48.0f && panel.innerW > histMin + 24.0f ? std::min(panel.innerW * 0.46f, areaH) : panel.innerW;
        float cell = std::min((heatBudgetW - gap * static_cast<float>(heatCols - 1)) / static_cast<float>(heatCols),
                              (areaH - gap * static_cast<float>(heatRows - 1)) / static_cast<float>(heatRows));
        if (cell < kHeatMinCellPx)
        {
            DrawHistoryArea(list, panel.pad, y, panel.innerW, areaH, sample, sample.display[0], sample.available[0],
                            true);
            return S_OK;
        }
        const float gridW = static_cast<float>(heatCols) * cell + gap * static_cast<float>(heatCols - 1);
        const float gridH = static_cast<float>(heatRows) * cell + gap * static_cast<float>(heatRows - 1);
        for (uint32_t index = 0; index < sample.heatCount; ++index)
        {
            const float heatT = CpuUnit(sample.heatDisplay[index]);
            const float heatLum = CpuHeatLuminance(heatT);
            float red = kFillR;
            float green = kFillG;
            float blue = kFillB;
            SignalColor(heatT, red, green, blue);
            const float x = panel.pad + static_cast<float>(index % heatCols) * (cell + gap);
            const float cellY = y + static_cast<float>(index / heatCols) * (cell + gap);
            (void)list.AddFill(x, cellY, cell, cell, Lerp(kHeatIdle, red, heatLum), Lerp(kHeatIdle, green, heatLum),
                               Lerp(kHeatIdle, blue, heatLum), 1.0f, 2.0f);
        }
        const float histX = panel.pad + gridW + 12.0f;
        const float histW = panel.innerW - gridW - 12.0f;
        if (histW >= 72.0f)
        {
            DrawHistoryArea(list, histX, y, histW, std::max(gridH, areaH), sample, sample.display[0],
                            sample.available[0], true);
        }
        if (sample.heatOverflow > 0)
        {
            DrawOverflow(resources, list, width - panel.pad - 40.0f, height - panel.pad - panel.labelPx, panel.labelPx,
                         sample.heatOverflow);
        }
        return S_OK;
    }

    [[nodiscard]] HRESULT DrawMemory(ViewerGpuResources& resources, ViewerDrawList& list, const ViewerSample& sample,
                                     const PanelMetrics& panel, float width, float height) noexcept
    {
        float y = panel.contentTop;
        if (panel.density == ViewerDensity::Hero)
        {
            wchar_t used[32]{};
            FormatBytes(used, 32, sample.counts[0], sample.available[0]);
            return resources.AppendText(list, panel.pad, y, panel.heroPx, kTextR, kTextG, kTextB, 1.0f, used,
                                        static_cast<uint32_t>(wcsnlen(used, 32)));
        }
        const float remaining = std::max(0.0f, height - panel.pad - y);
        uint32_t bands = 1;
        if (remaining >= panel.rowMin * 2.2f)
        {
            bands = 2;
        }
        if (panel.density == ViewerDensity::Standard && sample.sparkCount > 1 && remaining >= panel.rowMin * 3.2f)
        {
            bands = 3;
        }
        const float bandH = remaining / static_cast<float>(bands);
        const float barH = std::clamp(bandH * 0.32f, 10.0f, 22.0f);
        const float typePx = TypeFromRow(bandH, 20.0f, 36.0f);
        DrawCapacityBar(resources, list, panel.pad, y, panel.innerW, L"Physical", sample.display[0],
                        sample.available[0], sample.counts[0], sample.counts[1], typePx, barH, bandH);
        if (bands >= 2)
        {
            DrawCapacityBar(resources, list, panel.pad, y + bandH, panel.innerW, L"Commit", sample.display[1],
                            sample.available[1], sample.counts[2], sample.counts[3], typePx, barH, bandH);
        }
        if (bands >= 3)
        {
            DrawHistoryArea(list, panel.pad, y + bandH * 2.0f, panel.innerW, bandH - 4.0f, sample, 0.0f, false, false);
        }
        (void)width;
        return S_OK;
    }

    void DrawCapacityBar(ViewerGpuResources& resources, ViewerDrawList& list, float x, float y, float width,
                         const wchar_t* label, float percent, bool available, uint64_t used, uint64_t total,
                         float labelPx, float barH, float bandH) noexcept
    {
        wchar_t value[40]{};
        FormatBytesPair(value, 40, used, total, available && total != 0);
        const float textY = y + 2.0f;
        (void)resources.AppendText(list, x, textY, labelPx, kMuted, kMuted, kMuted, 1.0f, label,
                                   static_cast<uint32_t>(wcsnlen(label, 32)));
        const float valueWidth = resources.MeasureText(value, static_cast<uint32_t>(wcsnlen(value, 40)), labelPx);
        float ir = kTextR;
        float ig = kTextG;
        float ib = kTextB;
        IntentTextColor(percent / 100.0f, available, ir, ig, ib);
        (void)resources.AppendText(list, x + width - valueWidth, textY, labelPx, ir, ig, ib, 1.0f, value,
                                   static_cast<uint32_t>(wcsnlen(value, 40)));
        const float barY = textY + labelPx + 6.0f;
        const float fitBarH = std::min(barH, std::max(6.0f, y + bandH - barY - 2.0f));
        DrawTrack(list, x, barY, width, fitBarH, std::clamp(percent, 0.0f, 100.0f) / 100.0f, available, false, 0.0f);
    }

    [[nodiscard]] HRESULT DrawStorage(ViewerGpuResources& resources, ViewerDrawList& list, const ViewerSample& sample,
                                      const PanelMetrics& panel, float width, float height) noexcept
    {
        float y = panel.contentTop;
        if (sample.rowCount == 0)
        {
            return resources.AppendText(list, panel.pad, y, panel.kpiPx, kMuted, kMuted, kMuted, 1.0f, L"--", 2);
        }
        if (panel.density == ViewerDensity::Hero)
        {
            wchar_t used[16]{};
            FormatPercent(used, 16, sample.rows[0].displayPrimary, sample.rows[0].primaryAvailable);
            float ir = kTextR;
            float ig = kTextG;
            float ib = kTextB;
            IntentTextColor(sample.rows[0].displayPrimary / 100.0f, sample.rows[0].primaryAvailable, ir, ig, ib);
            (void)resources.AppendText(list, panel.pad, y, panel.heroPx, ir, ig, ib, 1.0f, used,
                                       static_cast<uint32_t>(wcsnlen(used, 16)));
            const wchar_t* band = CapacityBandText(sample.rows[0].displayPrimary, sample.rows[0].primaryAvailable);
            (void)resources.AppendText(list, panel.pad, y + panel.heroPx + 4.0f, panel.labelPx, ir, ig, ib, 1.0f, band,
                                       static_cast<uint32_t>(wcsnlen(band, 8)));
            (void)AppendClippedText(resources, list, panel.pad, y + panel.heroPx + panel.labelPx + 8.0f, panel.labelPx,
                                    panel.innerW, kMuted, kMuted, kMuted, 1.0f, sample.rows[0].name.data(),
                                    sample.rows[0].nameCharacters);
            return S_OK;
        }
        const float footer = panel.density == ViewerDensity::Standard ? panel.rowPx + 12.0f : 0.0f;
        const float remaining = std::max(0.0f, height - panel.pad - y - footer);
        uint32_t columns = 1;
        uint32_t visible = 0;
        uint32_t rows = 0;
        float cardH = 0.0f;
        float listHeight = 0.0f;
        float cardW = 0.0f;
        FitGridLayout(remaining, panel.innerW, 84.0f, panel.labelPx + 4.0f, 176.0f, sample.rowCount, columns, visible,
                      rows, cardH, listHeight, cardW);
        for (uint32_t index = 0; index < visible; ++index)
        {
            const RankedRow& row = sample.rows[index];
            float cellX = 0.0f;
            float cellY = 0.0f;
            CellOrigin(panel.pad, y, index, columns, cardW, cardH, row.displayY, columns == 1, cellX, cellY);
            const float fill = std::clamp(row.displayPrimary, 0.0f, 100.0f) / 100.0f;
            const float pad = 8.0f;
            const float trackH = ClampOrdered(cardH * 0.14f, 8.0f, 12.0f);
            const float trackY = cellY + cardH - pad - trackH;
            const float namePx = TypeFromRow(cardH * 0.28f, panel.rowPx, 26.0f);
            const float valuePx = TypeFromRow(cardH * 0.30f, panel.kpiPx, 32.0f);
            const float metaPx = TypeFromRow(cardH * 0.20f, panel.labelPx, 16.0f);
            (void)list.AddFill(cellX, cellY + 2.0f, cardW, cardH - 6.0f, kTrackR, kTrackG, kTrackB, 1.0f, 6.0f);
            float ir = kTextR;
            float ig = kTextG;
            float ib = kTextB;
            IntentTextColor(fill, row.primaryAvailable, ir, ig, ib);
            wchar_t value[16]{};
            FormatPercent(value, 16, row.displayPrimary, row.primaryAvailable);
            const float valueWidth = resources.MeasureText(value, static_cast<uint32_t>(wcsnlen(value, 16)), valuePx);
            const float nameY = cellY + pad;
            (void)AppendClippedText(resources, list, cellX + pad, nameY, namePx,
                                    std::max(20.0f, cardW - valueWidth - pad * 2.0f - 8.0f), kTextR, kTextG, kTextB,
                                    1.0f, row.name.data(), row.nameCharacters);
            (void)resources.AppendText(list, cellX + cardW - valueWidth - pad, nameY, valuePx, ir, ig, ib, 1.0f, value,
                                       static_cast<uint32_t>(wcsnlen(value, 16)));
            wchar_t pair[40]{};
            FormatBytesPair(pair, 40, row.pid, row.secondary, row.primaryAvailable && row.secondary != 0);
            const wchar_t* band = CapacityBandText(row.displayPrimary, row.primaryAvailable);
            const float bandWidth = resources.MeasureText(band, static_cast<uint32_t>(wcsnlen(band, 8)), metaPx);
            const float metaY = nameY + std::max(namePx, valuePx) + 4.0f;
            if (metaY + metaPx <= trackY - 4.0f)
            {
                (void)AppendClippedText(resources, list, cellX + pad, metaY, metaPx,
                                        std::max(20.0f, cardW - bandWidth - pad * 2.0f - 8.0f), kMuted, kMuted, kMuted,
                                        1.0f, pair, static_cast<uint32_t>(wcsnlen(pair, 40)));
                (void)resources.AppendText(list, cellX + cardW - bandWidth - pad, metaY, metaPx, ir, ig, ib, 1.0f, band,
                                           static_cast<uint32_t>(wcsnlen(band, 8)));
            }
            DrawTrack(list, cellX + pad, trackY, cardW - pad * 2.0f, trackH, fill, row.primaryAvailable, false, 0.0f);
        }
        DrawOverflow(resources, list, panel.pad, y + listHeight, panel.labelPx, sample.rowCount - visible);
        if (footer > 0.0f)
        {
            wchar_t disk[48]{};
            wchar_t rate[32]{};
            FormatRate(rate, 32, sample.values[6], sample.available[6]);
            wchar_t active[16]{};
            FormatPercent(active, 16, sample.display[7], sample.available[7]);
            (void)swprintf_s(disk, 48, L"%s · %s active", rate, active);
            (void)resources.AppendText(list, panel.pad, height - panel.pad - panel.rowPx, panel.rowPx, kMuted, kMuted,
                                       kMuted, 1.0f, disk, static_cast<uint32_t>(wcsnlen(disk, 48)));
        }
        (void)width;
        return S_OK;
    }

    [[nodiscard]] HRESULT DrawGpu(ViewerGpuResources& resources, ViewerDrawList& list, const ViewerSample& sample,
                                  const PanelMetrics& panel, float width, float height) noexcept
    {
        float y = panel.contentTop;
        if (sample.rowCount == 0)
        {
            if (sample.hiddenCount > 0)
            {
                wchar_t more[32]{};
                (void)swprintf_s(more, 32, L"+%u software", sample.hiddenCount);
                return resources.AppendText(list, panel.pad, y, panel.rowPx, kMuted, kMuted, kMuted, 1.0f, more,
                                            static_cast<uint32_t>(wcsnlen(more, 32)));
            }
            return resources.AppendText(list, panel.pad, y, panel.kpiPx, kMuted, kMuted, kMuted, 1.0f, L"--", 2);
        }
        const float remainingAll = std::max(0.0f, height - panel.pad - y);
        const float hiddenReserve = sample.hiddenCount > 0 ? panel.labelPx + 4.0f : 0.0f;
        const float remaining = std::max(0.0f, remainingAll - hiddenReserve);
        const float minCard = panel.density == ViewerDensity::Hero ? std::max(remaining, 1.0f) : 56.0f;
        uint32_t visible = 0;
        float cardH = 0.0f;
        float listHeight = 0.0f;
        FitListLayout(remaining, minCard, panel.labelPx + 4.0f, sample.rowCount, visible, cardH, listHeight);
        visible = std::max(1u, visible);
        cardH = listHeight / static_cast<float>(visible);
        const float namePx = TypeFromRow(cardH * 0.42f, panel.rowPx, 32.0f);
        const float detailPx = TypeFromRow(cardH * 0.28f, panel.labelPx, 24.0f);
        const float trackH = std::clamp(cardH * 0.12f, 4.0f, 8.0f);
        for (uint32_t index = 0; index < visible; ++index)
        {
            const RankedRow& row = sample.rows[index];
            const float cardY = y + row.displayY * cardH;
            (void)AppendClippedText(resources, list, panel.pad, cardY + 4.0f, namePx, panel.innerW, kTextR, kTextG,
                                    kTextB, 1.0f, row.name.data(), row.nameCharacters);
            wchar_t temp[16]{};
            FormatCelsius(temp, 16, row.displayPrimary, row.primaryAvailable);
            wchar_t memory[32]{};
            FormatBytes(memory, 32, row.secondary, row.secondary != 0);
            float ir = kMuted;
            float ig = kMuted;
            float ib = kMuted;
            IntentThermalTextColor(row.displayPrimary, row.primaryAvailable, ir, ig, ib);
            const float tempWidth = resources.MeasureText(temp, static_cast<uint32_t>(wcsnlen(temp, 16)), detailPx);
            (void)resources.AppendText(list, panel.pad, cardY + 8.0f + namePx, detailPx, ir, ig, ib, 1.0f, temp,
                                       static_cast<uint32_t>(wcsnlen(temp, 16)));
            wchar_t rest[40]{};
            (void)swprintf_s(rest, 40, L" · %s", memory);
            (void)resources.AppendText(list, panel.pad + tempWidth, cardY + 8.0f + namePx, detailPx, kMuted, kMuted,
                                       kMuted, 1.0f, rest, static_cast<uint32_t>(wcsnlen(rest, 40)));
            DrawThermalLevel(list, panel.pad, cardY + cardH - trackH - 4.0f, panel.innerW, trackH, row.displayPrimary,
                             row.primaryAvailable);
        }
        const uint32_t extra = (sample.rowCount > visible ? sample.rowCount - visible : 0) + sample.hiddenCount;
        if (extra > 0)
        {
            wchar_t more[32]{};
            (void)swprintf_s(more, 32, sample.hiddenCount > 0 ? L"+%u hidden" : L"+%u", extra);
            (void)resources.AppendText(list, panel.pad, y + listHeight, panel.labelPx, kMuted, kMuted, kMuted, 1.0f,
                                       more, static_cast<uint32_t>(wcsnlen(more, 32)));
        }
        (void)width;
        return S_OK;
    }

    [[nodiscard]] HRESULT DrawPower(ViewerGpuResources& resources, ViewerDrawList& list, const ViewerSample& sample,
                                    const PanelMetrics& panel, float width, float height) noexcept
    {
        const float y = panel.contentTop;
        const float innerH = std::max(0.0f, height - panel.pad - y);
        if (!sample.batteryPresent && sample.batteryCount == 0)
        {
            const float hero = ClampOrdered(innerH * 0.42f, 44.0f, 88.0f);
            const float sub = panel.density == ViewerDensity::Hero ? 0.0f : panel.labelPx + 8.0f;
            const float blockH = hero + sub;
            const float blockY = y + std::max(0.0f, (innerH - blockH) * 0.5f);
            const float acW = resources.MeasureText(L"AC", 2, hero);
            (void)resources.AppendText(list, panel.pad + (panel.innerW - acW) * 0.5f, blockY, hero, kTextR, kTextG,
                                       kTextB, 1.0f, L"AC", 2);
            if (sub > 0.0f)
            {
                const float nbW = resources.MeasureText(L"no battery", 10, panel.labelPx);
                (void)resources.AppendText(list, panel.pad + (panel.innerW - nbW) * 0.5f, blockY + hero + 4.0f,
                                           panel.labelPx, kMuted, kMuted, kMuted, 1.0f, L"no battery", 10);
            }
            return S_OK;
        }
        if (panel.density == ViewerDensity::Hero || std::min(width, height) < 140.0f)
        {
            wchar_t value[16]{};
            FormatPercent(value, 16, sample.display[0], sample.available[0]);
            const float hero = std::min(panel.heroPx, innerH * 0.55f);
            float ir = kTextR;
            float ig = kTextG;
            float ib = kTextB;
            IntentTextColor(1.0f - std::clamp(sample.display[0], 0.0f, 100.0f) / 100.0f, sample.available[0], ir, ig,
                            ib);
            (void)resources.AppendText(list, panel.pad, y, hero, ir, ig, ib, 1.0f, value,
                                       static_cast<uint32_t>(wcsnlen(value, 16)));
            const wchar_t* status = sample.charging ? L"Charging" : (sample.acOnline ? L"AC" : L"Battery");
            return resources.AppendText(list, panel.pad, y + hero + 4.0f, panel.labelPx, kMuted, kMuted, kMuted, 1.0f,
                                        status, static_cast<uint32_t>(wcsnlen(status, 16)));
        }
        const float statusH = panel.labelPx + 10.0f;
        const float size = std::min(panel.innerW, std::max(48.0f, innerH - statusH));
        const float blockH = size + statusH;
        const float blockY = y + std::max(0.0f, (innerH - blockH) * 0.5f);
        const float x = panel.pad + (panel.innerW - size) * 0.5f;
        (void)list.AddRing(x, blockY, size, size, kTrackR, kTrackG, kTrackB, 1.0f, size * 0.36f);
        const float charge = std::clamp(sample.display[0], 0.0f, 100.0f);
        float red = kFillR;
        float green = kFillG;
        float blue = kFillB;
        IntentTextColor(1.0f - charge / 100.0f, sample.available[0], red, green, blue);
        (void)list.AddRing(x, blockY, size, size, red, green, blue, sample.available[0] ? 0.95f : 0.25f,
                           size * (0.36f + (1.0f - charge / 100.0f) * 0.12f));
        wchar_t value[16]{};
        FormatPercent(value, 16, charge, sample.available[0]);
        const float kpi = ClampOrdered(size * 0.22f, panel.kpiPx, panel.heroPx);
        const float textW = resources.MeasureText(value, static_cast<uint32_t>(wcsnlen(value, 16)), kpi);
        (void)resources.AppendText(list, x + (size - textW) * 0.5f, blockY + size * 0.38f, kpi, red, green, blue, 1.0f,
                                   value, static_cast<uint32_t>(wcsnlen(value, 16)));
        const wchar_t* status = sample.charging ? L"Charging" : (sample.acOnline ? L"AC" : L"Battery");
        const float statusW = resources.MeasureText(status, static_cast<uint32_t>(wcsnlen(status, 16)), panel.labelPx);
        return resources.AppendText(list, panel.pad + (panel.innerW - statusW) * 0.5f, blockY + size + 6.0f,
                                    panel.labelPx, kMuted, kMuted, kMuted, 1.0f, status,
                                    static_cast<uint32_t>(wcsnlen(status, 16)));
    }

    [[nodiscard]] HRESULT DrawThermal(ViewerGpuResources& resources, ViewerDrawList& list, const ViewerSample& sample,
                                      const PanelMetrics& panel, float width, float height) noexcept
    {
        float y = panel.contentTop;
        if (sample.rowCount == 0)
        {
            return resources.AppendText(list, panel.pad, y, panel.kpiPx, kMuted, kMuted, kMuted, 1.0f, L"--", 2);
        }
        const float fanReserve = sample.available[7] && panel.density == ViewerDensity::Standard ? panel.rowMin : 0.0f;
        const float remaining = std::max(0.0f, height - panel.pad - y - fanReserve);
        const uint32_t available = panel.density == ViewerDensity::Hero ? 1u : sample.rowCount;
        uint32_t columns = 1;
        uint32_t visible = 0;
        uint32_t rows = 0;
        float cardH = 0.0f;
        float listHeight = 0.0f;
        float cardW = 0.0f;
        FitGridLayout(remaining, panel.innerW, 84.0f, panel.labelPx + 4.0f, 168.0f, available, columns, visible, rows,
                      cardH, listHeight, cardW);
        visible = std::min(visible, sample.rowCount);
        for (uint32_t index = 0; index < visible; ++index)
        {
            const RankedRow& row = sample.rows[index];
            float cellX = 0.0f;
            float cellY = 0.0f;
            CellOrigin(panel.pad, y, index, columns, cardW, cardH, row.displayY, columns == 1, cellX, cellY);
            const float pad = 8.0f;
            const float trackH = ClampOrdered(cardH * 0.14f, 8.0f, 12.0f);
            const float trackY = cellY + cardH - pad - trackH;
            const float tempPx = TypeFromRow(cardH * 0.36f, panel.kpiPx, 40.0f);
            const float bandPx = TypeFromRow(cardH * 0.18f, panel.labelPx, 20.0f);
            const float namePx = TypeFromRow(cardH * 0.18f, panel.labelPx, 18.0f);
            (void)list.AddFill(cellX, cellY + 2.0f, cardW, cardH - 6.0f, kTrackR, kTrackG, kTrackB, 0.9f, 6.0f);
            float ir = kTextR;
            float ig = kTextG;
            float ib = kTextB;
            IntentThermalTextColor(row.displayPrimary, row.primaryAvailable, ir, ig, ib);
            wchar_t temp[16]{};
            FormatCelsius(temp, 16, row.displayPrimary, row.primaryAvailable);
            const wchar_t* band = ThermalBandText(row.displayPrimary, row.primaryAvailable);
            const float bandWidth = resources.MeasureText(band, static_cast<uint32_t>(wcsnlen(band, 8)), bandPx);
            const float tempY = cellY + pad;
            (void)resources.AppendText(list, cellX + pad, tempY, tempPx, ir, ig, ib, 1.0f, temp,
                                       static_cast<uint32_t>(wcsnlen(temp, 16)));
            (void)resources.AppendText(list, cellX + cardW - bandWidth - pad, tempY + 2.0f, bandPx, ir, ig, ib, 1.0f,
                                       band, static_cast<uint32_t>(wcsnlen(band, 8)));
            const float nameY = tempY + tempPx + 4.0f;
            if (nameY + namePx <= trackY - 4.0f)
            {
                (void)AppendClippedText(resources, list, cellX + pad, nameY, namePx, cardW - pad * 2.0f, kMuted, kMuted,
                                        kMuted, 1.0f, row.name.data(), row.nameCharacters);
            }
            DrawThermalLevel(list, cellX + pad, trackY, cardW - pad * 2.0f, trackH, row.displayPrimary,
                             row.primaryAvailable);
        }
        DrawOverflow(resources, list, panel.pad, y + listHeight, panel.labelPx, sample.rowCount - visible);
        if (fanReserve > 0.0f)
        {
            wchar_t fan[32]{};
            (void)swprintf_s(fan, 32, L"%llu RPM", static_cast<unsigned long long>(sample.counts[6]));
            (void)resources.AppendText(list, panel.pad, height - panel.pad - panel.rowPx - 8.0f, panel.labelPx, kMuted,
                                       kMuted, kMuted, 1.0f, fan, static_cast<uint32_t>(wcsnlen(fan, 32)));
            DrawTrack(list, panel.pad, height - panel.pad - 6.0f, panel.innerW, 4.0f,
                      std::clamp(sample.display[7], 0.0f, 100.0f) / 100.0f, sample.available[7], false,
                      sample.display[7] / 100.0f);
        }
        (void)width;
        return S_OK;
    }

    wil::com_ptr_nothrow<IRedXeWidgetProvider> _providerOwner;
    wil::com_ptr_nothrow<IRedXeDataSink> _sinks[2];
    wil::com_ptr_nothrow<IRedXeDataSubscription> _subscriptions[2];
    uint32_t _subscriptionCount = 0;
    ViewerKind _kind;
    uint32_t _topN;
    uint32_t _restDelay = 1000;
    std::atomic<bool> _visible{false};
    bool _raised = false;
    bool _gpuHeld = false;
    mutable SRWLOCK _lock = SRWLOCK_INIT;
    ViewerSample _sample{};
    std::array<NetIdleWatch, kMaximumRows> _netIdle{};
    uint32_t _netIdleCount = 0;
    std::array<PidNameEntry, kMaximumRows> _pidNames{};
    uint32_t _pidNameCount = 0;
    bool _easing = false;
    float _easeElapsed = 0.0f;
    float _pulse = 0.0f;
};

HRESULT ViewerSink::OnDataSnapshot(const RedXeDataSnapshot* snapshot) noexcept
{
    return _widget ? _widget->Publish(_dataSetIndex, snapshot) : E_UNEXPECTED;
}

class ViewerProvider final : public RedXeComObject<ViewerProvider, IRedXeWidgetProvider>
{
  public:
    ViewerProvider(wil::com_ptr_nothrow<IRedXeDataProvider>&& dataProvider, ViewerKind kind, uint32_t topN) noexcept
        : _dataProvider(std::move(dataProvider)), _kind(kind), _topN(topN)
    {
        const ViewerCatalogEntry& entry = Catalog(kind);
        _types[0] = RedXeWidgetTypeDescriptor{
            sizeof(RedXeWidgetTypeDescriptor),
            entry.typeId,
            entry.typeName,
            entry.typeDescription,
            entry.defaultWidth,
            entry.defaultHeight,
            entry.minimumWidth,
            entry.minimumHeight,
            RedXeWidgetFlagNone,
        };
        g_liveProviderCount.fetch_add(1, std::memory_order_relaxed);
    }

    ~ViewerProvider()
    {
        g_liveProviderCount.fetch_sub(1, std::memory_order_relaxed);
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
        *descriptors = _types.data();
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
        if (!RedXeAsciiEqualsIgnoreCase(typeId, Catalog(_kind).typeId))
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
        wil::com_ptr_nothrow<IRedXeWidgetProvider> providerOwner;
        HRESULT result = QueryInterface(__uuidof(IRedXeWidgetProvider), reinterpret_cast<void**>(providerOwner.put()));
        if (FAILED(result))
        {
            return result;
        }
        auto* created = new (std::nothrow) ViewerWidget(std::move(providerOwner), _kind, _topN);
        if (!created)
        {
            return E_OUTOFMEMORY;
        }
        result = created->InitializeSubscriptions(*_dataProvider);
        if (FAILED(result))
        {
            delete created;
            return result;
        }
        *widget = static_cast<IRedXeWidget*>(created);
        return S_OK;
    }

  private:
    wil::com_ptr_nothrow<IRedXeDataProvider> _dataProvider;
    ViewerKind _kind;
    uint32_t _topN;
    std::array<RedXeWidgetTypeDescriptor, 1> _types{};
};

HRESULT CreateViewerProviderFor(ViewerKind kind, REFIID interfaceId, const RedXeFactoryOptions* options,
                                IRedXeHost* host, void** result) noexcept
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
    if (!host)
    {
        return E_POINTER;
    }
    const ViewerCatalogEntry& entry = Catalog(kind);
    uint32_t topN = entry.topNDefault;
    HRESULT configurationResult = S_OK;
    if (entry.topNMax == 0)
    {
        configurationResult = RedXeValidateEmptyNormalizedConfiguration(options);
    }
    else
    {
        configurationResult = ReadTopN(options, entry.topNMax, entry.topNDefault, topN);
    }
    if (FAILED(configurationResult))
    {
        return configurationResult;
    }
    wil::com_ptr_nothrow<IRedXeDataProvider> dataProvider;
    configurationResult = host->GetDataProvider(kSystemDataPluginId, dataProvider.put());
    if (FAILED(configurationResult))
    {
        return configurationResult;
    }
    auto* provider = new (std::nothrow) ViewerProvider(std::move(dataProvider), kind, topN);
    if (!provider)
    {
        return E_OUTOFMEMORY;
    }
    *result = static_cast<IRedXeWidgetProvider*>(provider);
    return S_OK;
}

template <ViewerKind Kind>
HRESULT CreateViewerProvider(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                             void** result) noexcept
{
    return CreateViewerProviderFor(Kind, interfaceId, options, host, result);
}

constexpr std::array kMetadata{
    RedXePluginMetadata{sizeof(RedXePluginMetadata), kCatalog[0].pluginId, kCatalog[0].pluginName,
                        kCatalog[0].pluginDescription, L"RedXe", L"1.0.0", RedXePluginCapabilityWidgetProvider},
    RedXePluginMetadata{sizeof(RedXePluginMetadata), kCatalog[1].pluginId, kCatalog[1].pluginName,
                        kCatalog[1].pluginDescription, L"RedXe", L"1.0.0", RedXePluginCapabilityWidgetProvider},
    RedXePluginMetadata{sizeof(RedXePluginMetadata), kCatalog[2].pluginId, kCatalog[2].pluginName,
                        kCatalog[2].pluginDescription, L"RedXe", L"1.0.0", RedXePluginCapabilityWidgetProvider},
    RedXePluginMetadata{sizeof(RedXePluginMetadata), kCatalog[3].pluginId, kCatalog[3].pluginName,
                        kCatalog[3].pluginDescription, L"RedXe", L"1.0.0", RedXePluginCapabilityWidgetProvider},
    RedXePluginMetadata{sizeof(RedXePluginMetadata), kCatalog[4].pluginId, kCatalog[4].pluginName,
                        kCatalog[4].pluginDescription, L"RedXe", L"1.0.0", RedXePluginCapabilityWidgetProvider},
    RedXePluginMetadata{sizeof(RedXePluginMetadata), kCatalog[5].pluginId, kCatalog[5].pluginName,
                        kCatalog[5].pluginDescription, L"RedXe", L"1.0.0", RedXePluginCapabilityWidgetProvider},
    RedXePluginMetadata{sizeof(RedXePluginMetadata), kCatalog[6].pluginId, kCatalog[6].pluginName,
                        kCatalog[6].pluginDescription, L"RedXe", L"1.0.0", RedXePluginCapabilityWidgetProvider},
    RedXePluginMetadata{sizeof(RedXePluginMetadata), kCatalog[7].pluginId, kCatalog[7].pluginName,
                        kCatalog[7].pluginDescription, L"RedXe", L"1.0.0", RedXePluginCapabilityWidgetProvider},
    RedXePluginMetadata{sizeof(RedXePluginMetadata), kCatalog[8].pluginId, kCatalog[8].pluginName,
                        kCatalog[8].pluginDescription, L"RedXe", L"1.0.0", RedXePluginCapabilityWidgetProvider},
    RedXePluginMetadata{sizeof(RedXePluginMetadata), kCatalog[9].pluginId, kCatalog[9].pluginName,
                        kCatalog[9].pluginDescription, L"RedXe", L"1.0.0", RedXePluginCapabilityWidgetProvider},
};

constexpr std::array kFactoryEntries{
    RedXeFactoryEntry{&kMetadata[0], CreateViewerProvider<ViewerKind::ProcessViewer>},
    RedXeFactoryEntry{&kMetadata[1], CreateViewerProvider<ViewerKind::SystemPulse>},
    RedXeFactoryEntry{&kMetadata[2], CreateViewerProvider<ViewerKind::CpuMeter>},
    RedXeFactoryEntry{&kMetadata[3], CreateViewerProvider<ViewerKind::MemoryMeter>},
    RedXeFactoryEntry{&kMetadata[4], CreateViewerProvider<ViewerKind::NetworkMeter>},
    RedXeFactoryEntry{&kMetadata[5], CreateViewerProvider<ViewerKind::StorageMeter>},
    RedXeFactoryEntry{&kMetadata[6], CreateViewerProvider<ViewerKind::GpuMeter>},
    RedXeFactoryEntry{&kMetadata[7], CreateViewerProvider<ViewerKind::GpuProcesses>},
    RedXeFactoryEntry{&kMetadata[8], CreateViewerProvider<ViewerKind::PowerMeter>},
    RedXeFactoryEntry{&kMetadata[9], CreateViewerProvider<ViewerKind::ThermalMeter>},
};

constexpr std::array kSettingsEntries{
    RedXeSettingsContractEntry{kCatalog[0].pluginId, &kTopN32Contract},
    RedXeSettingsContractEntry{kCatalog[1].pluginId, &kEmptyContract},
    RedXeSettingsContractEntry{kCatalog[2].pluginId, &kEmptyContract},
    RedXeSettingsContractEntry{kCatalog[3].pluginId, &kEmptyContract},
    RedXeSettingsContractEntry{kCatalog[4].pluginId, &kTopN16Contract},
    RedXeSettingsContractEntry{kCatalog[5].pluginId, &kEmptyContract},
    RedXeSettingsContractEntry{kCatalog[6].pluginId, &kEmptyContract},
    RedXeSettingsContractEntry{kCatalog[7].pluginId, &kTopN16Contract},
    RedXeSettingsContractEntry{kCatalog[8].pluginId, &kEmptyContract},
    RedXeSettingsContractEntry{kCatalog[9].pluginId, &kEmptyContract},
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
    return RedXeGetPluginSettingsContractFromEntries(
        kSettingsEntries.data(), static_cast<uint32_t>(kSettingsEntries.size()), pluginId, contract);
}

extern "C" HRESULT __stdcall RedXeProcessViewerGetTestDiagnostics(ProcessViewerTestDiagnostics* diagnostics) noexcept
{
    if (!diagnostics)
    {
        return E_POINTER;
    }
    if (diagnostics->sizeBytes != sizeof(ProcessViewerTestDiagnostics))
    {
        return E_INVALIDARG;
    }
    diagnostics->liveProviderCount = g_liveProviderCount.load(std::memory_order_relaxed);
    diagnostics->liveWidgetCount = g_liveWidgetCount.load(std::memory_order_relaxed);
    diagnostics->liveSubscriptionCount = g_liveSubscriptionCount.load(std::memory_order_relaxed);
    diagnostics->sampleCount = g_sampleCount.load(std::memory_order_relaxed);
    diagnostics->paintCount = g_paintCount.load(std::memory_order_relaxed);
    diagnostics->lastPublishedRowCount = g_lastPublishedRowCount.load(std::memory_order_relaxed);
    diagnostics->configuredTopN = g_configuredTopN.load(std::memory_order_relaxed);
    return S_OK;
}
