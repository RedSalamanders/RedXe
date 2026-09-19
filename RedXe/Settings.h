#pragma once

#include "DockPlacement.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

inline constexpr uint32_t kRedXeSettingsVersionMajor = 5;
// Minor 1 added the optional additive `services` root member (headless service plugins such as Logicon); minor 2
// added the optional additive `dock` root member (the screen-edge bar).
inline constexpr uint32_t kRedXeSettingsVersionMinor = 2;
// Removed with the v3 parser; retained temporarily so the transition remains buildable between slices.
inline constexpr wchar_t kRedXeDebugSettingsFileName[] = L"RedXe-debug.settings.json";
inline constexpr wchar_t kRedXeReleaseSettingsFileName[] = L"RedXe.settings.json";
inline constexpr wchar_t kRedXeSettingsSchemaFileName[] = L"RedXe.settings.schema.json";
inline constexpr wchar_t kRedXeLogsDirectoryName[] = L"Logs";
inline constexpr uint32_t kRedXeDefaultLogRetentionDays = 15;
inline constexpr uint32_t kRedXeMinimumLogRetentionDays = 1;
inline constexpr uint32_t kRedXeMaximumLogRetentionDays = 365;
inline constexpr size_t kRedXeLogFileNameCapacity = 64;
// Document-level dashboard background when the file omits backgroundColor, as 0xRRGGBB.
inline constexpr uint32_t kRedXeDefaultBackgroundRgb = 0x000000;
#if defined(_DEBUG)
inline constexpr wchar_t kRedXeLogFileNamePrefix[] = L"RedXe-debug-";
#else
inline constexpr wchar_t kRedXeLogFileNamePrefix[] = L"RedXe-";
#endif

[[nodiscard]] inline bool RedXeFormatLogFileName(wchar_t* buffer, size_t capacity, const SYSTEMTIME& utcDate) noexcept
{
    if (!buffer)
    {
        return false;
    }
    return swprintf_s(buffer, capacity, L"%s%04u-%02u-%02u.jsonl", kRedXeLogFileNamePrefix, utcDate.wYear,
                      utcDate.wMonth, utcDate.wDay) > 0;
}

[[nodiscard]] inline bool RedXeTryParseLogFileDate(const wchar_t* fileName, SYSTEMTIME& utcDate) noexcept
{
    if (!fileName)
    {
        return false;
    }
    const wchar_t* prefixes[] = {L"RedXe-debug-", L"RedXe-"};
    for (const wchar_t* prefix : prefixes)
    {
        const size_t prefixLength = std::wcslen(prefix);
        if (std::wcsncmp(fileName, prefix, prefixLength) != 0)
        {
            continue;
        }
        unsigned year = 0;
        unsigned month = 0;
        unsigned day = 0;
        wchar_t extra = 0;
        if (swscanf_s(fileName + prefixLength, L"%4u-%2u-%2u.jsonl%c", &year, &month, &day, &extra, 1) != 3 ||
            year < 2000 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31)
        {
            continue;
        }
        utcDate = {};
        utcDate.wYear = static_cast<WORD>(year);
        utcDate.wMonth = static_cast<WORD>(month);
        utcDate.wDay = static_cast<WORD>(day);
        return true;
    }
    return false;
}

[[nodiscard]] inline bool RedXeIsLegacyLogFileName(const wchar_t* fileName) noexcept
{
    return fileName &&
           (std::wcscmp(fileName, L"RedXe.jsonl") == 0 || std::wcscmp(fileName, L"RedXe-debug.jsonl") == 0 ||
            std::wcscmp(fileName, L"RedXe.jsonl.1") == 0 || std::wcscmp(fileName, L"RedXe-debug.jsonl.1") == 0);
}

[[nodiscard]] inline uint32_t RedXeUtcDateDayDifference(const SYSTEMTIME& earlier, const SYSTEMTIME& later) noexcept
{
    SYSTEMTIME start = earlier;
    SYSTEMTIME end = later;
    start.wHour = 0;
    start.wMinute = 0;
    start.wSecond = 0;
    start.wMilliseconds = 0;
    end.wHour = 0;
    end.wMinute = 0;
    end.wSecond = 0;
    end.wMilliseconds = 0;
    FILETIME startFile{};
    FILETIME endFile{};
    if (!SystemTimeToFileTime(&start, &startFile) || !SystemTimeToFileTime(&end, &endFile))
    {
        return 0;
    }
    ULARGE_INTEGER startTicks{};
    ULARGE_INTEGER endTicks{};
    startTicks.LowPart = startFile.dwLowDateTime;
    startTicks.HighPart = startFile.dwHighDateTime;
    endTicks.LowPart = endFile.dwLowDateTime;
    endTicks.HighPart = endFile.dwHighDateTime;
    if (endTicks.QuadPart < startTicks.QuadPart)
    {
        return 0;
    }
    constexpr uint64_t kDay = 86'400ULL * 10'000'000ULL;
    const uint64_t days = (endTicks.QuadPart - startTicks.QuadPart) / kDay;
    return days > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(days);
}

inline constexpr size_t kMaximumSettingsPlugins = 64;
inline constexpr size_t kMaximumSettingsServices = 8;
inline constexpr size_t kMaximumDashboardPages = 16;
inline constexpr size_t kMaximumWidgetsPerPage = 32;
inline constexpr size_t kMaximumSettingsDeclarations = 128;
inline constexpr size_t kMaximumLayoutDepth = 8;
inline constexpr size_t kMaximumLayoutAreasPerPage = 127;
inline constexpr size_t kMaximumSettingsTextBytes = 512;
inline constexpr size_t kPrivateConfigurationCapacity = 4096;
inline constexpr size_t kFactoryConfigurationCapacity = 8192;
inline constexpr uint32_t kMaximumDashboardGridDimension = 64;

struct SettingsText final
{
    std::array<char, kMaximumSettingsTextBytes + 1> utf8{};
    uint32_t bytes = 0;

    [[nodiscard]] std::string_view View() const noexcept
    {
        return std::string_view(utf8.data(), bytes);
    }

    bool operator==(const SettingsText&) const noexcept = default;
};

struct JsonObjectSettings final
{
    std::array<char, kPrivateConfigurationCapacity + 1> utf8{'{', '}'};
    uint32_t bytes = 2;

    [[nodiscard]] std::string_view View() const noexcept
    {
        return std::string_view(utf8.data(), bytes);
    }

    bool operator==(const JsonObjectSettings&) const noexcept = default;
};

struct PluginSettings final
{
    SettingsText id;
    bool enabled = false;
    JsonObjectSettings privateConfiguration;

    bool operator==(const PluginSettings&) const noexcept = default;
};

struct WidgetGridPlacement final
{
    uint32_t column = 0;
    uint32_t row = 0;
    uint32_t columnSpan = 1;
    uint32_t rowSpan = 1;

    bool operator==(const WidgetGridPlacement&) const noexcept = default;
};

enum class LayoutAxis : std::uint8_t
{
    LongSide,
    ShortSide,
};

struct LayoutSplitStep final
{
    LayoutAxis axis = LayoutAxis::LongSide;
    uint32_t precedingRatio = 0;
    uint32_t sizeRatio = 1;
    uint32_t totalRatio = 1;

    bool operator==(const LayoutSplitStep&) const noexcept = default;
};

struct AdaptiveWidgetPlacement final
{
    std::array<LayoutSplitStep, kMaximumLayoutDepth> steps{};
    uint32_t depth = 0;

    bool operator==(const AdaptiveWidgetPlacement&) const noexcept = default;
};

struct WidgetInstanceSettings final
{
    SettingsText id;
    SettingsText pluginId;
    SettingsText typeId;
    WidgetGridPlacement placement;
    AdaptiveWidgetPlacement adaptivePlacement;
    bool usesAdaptivePlacement = false;
    // Host-reserved widget key: this instance paints backgroundRgb instead of the document background. It is never
    // part of privateConfiguration, so plugin persist cannot copy the document color into the instance.
    bool overridesBackground = false;
    uint32_t backgroundRgb = kRedXeDefaultBackgroundRgb;
    JsonObjectSettings privateConfiguration;

    bool operator==(const WidgetInstanceSettings&) const noexcept = default;
};

struct DashboardPageSettings final
{
    SettingsText id;
    SettingsText name;
    std::vector<WidgetInstanceSettings> widgets;
    uint32_t widgetCount = 0;

    bool operator==(const DashboardPageSettings&) const noexcept = default;
};

struct DashboardSettings final
{
    uint32_t gridColumns = 32;
    uint32_t gridRows = 9;
    SettingsText activePageId;
    std::vector<DashboardPageSettings> pages;
    uint32_t pageCount = 0;
    uint32_t activePageIndex = 0;
    bool wrapPages = false;

    bool operator==(const DashboardSettings&) const noexcept = default;
};

// One configured headless service from the `services` root: the authored member name, the catalogued service plugin,
// and its complete effective settings object (defaults merged, validated, compact).
struct ServiceSettings final
{
    SettingsText name;
    SettingsText pluginId;
    JsonObjectSettings privateConfiguration;

    bool operator==(const ServiceSettings&) const noexcept = default;
};

// The `dock` root member (Core_Settings.md): RedXe as a bar on one edge of one monitor. Defaults are merged by the
// parser, so an omitted object and `{ "edge": "none" }` are the same value. The window kind is decided once at
// startup from the effective dock (this object with the --dock* command-line overrides applied).
struct DockSettings final
{
    DockEdge edge = DockEdge::None;
    // Monitor selector text: primary, xeneon, <n>, or name:<substring> (never all).
    SettingsText monitor;
    uint32_t thicknessDips = kDockDefaultThicknessDips;
    DockMode mode = DockMode::Fixed;
    bool reserveWorkArea = true;
    uint32_t peekPixels = kDockDefaultPeekPixels;
    uint32_t revealDelayMilliseconds = kDockDefaultRevealDelayMilliseconds;
    uint32_t hideDelayMilliseconds = kDockDefaultHideDelayMilliseconds;

    bool operator==(const DockSettings&) const noexcept = default;
};

[[nodiscard]] inline DockSettings DefaultDockSettings() noexcept
{
    DockSettings dock{};
    dock.monitor.bytes = static_cast<uint32_t>(kDockDefaultMonitor.size());
    kDockDefaultMonitor.copy(dock.monitor.utf8.data(), kDockDefaultMonitor.size());
    return dock;
}

struct AppSettings final
{
    uint32_t versionMajor = kRedXeSettingsVersionMajor;
    uint32_t versionMinor = kRedXeSettingsVersionMinor;
    uint32_t logRetentionDays = kRedXeDefaultLogRetentionDays;
    uint32_t backgroundRgb = kRedXeDefaultBackgroundRgb;
    DockSettings dock = DefaultDockSettings();
    std::string sourceDocument;
    std::vector<PluginSettings> plugins;
    uint32_t pluginCount = 0;
    std::vector<ServiceSettings> services;
    uint32_t serviceCount = 0;
    DashboardSettings dashboard;

    bool operator==(const AppSettings&) const noexcept = default;
};

inline constexpr size_t kMaximumAppSettingsStorageBytes = 64U * 1024U;
static_assert(sizeof(AppSettings) <= kMaximumAppSettingsStorageBytes);

struct SettingsFileStamp final
{
    uint32_t volumeSerialNumber = 0;
    uint32_t fileIndexHigh = 0;
    uint32_t fileIndexLow = 0;
    uint64_t lastWriteTime = 0;
    uint64_t fileSize = 0;

    bool operator==(const SettingsFileStamp&) const noexcept = default;
};

struct SettingsParseDiagnostic final
{
    uint64_t byteOffset = 0;
    uint32_t line = 1;
    uint32_t column = 1;
    bool hasLocation = false;
    std::string path = "$";
    std::string message;
};

enum class SettingsReloadStatus : std::uint8_t
{
    Unchanged,
    Loaded,
    Missing,
    Invalid,
};

[[nodiscard]] bool SettingsIdEquals(std::string_view left, std::string_view right) noexcept;
[[nodiscard]] const PluginSettings* FindPluginSettings(const AppSettings& settings, std::string_view pluginId) noexcept;
[[nodiscard]] PluginSettings* FindPluginSettings(AppSettings& settings, std::string_view pluginId) noexcept;
[[nodiscard]] const DashboardPageSettings* FindDashboardPage(const AppSettings& settings,
                                                             std::string_view pageId) noexcept;
[[nodiscard]] DashboardPageSettings* FindDashboardPage(AppSettings& settings, std::string_view pageId) noexcept;
[[nodiscard]] const DashboardPageSettings* FindActiveDashboardPage(const AppSettings& settings) noexcept;
[[nodiscard]] DashboardPageSettings* FindActiveDashboardPage(AppSettings& settings) noexcept;
[[nodiscard]] HRESULT MoveDashboardPage(AppSettings& settings, int direction) noexcept;
[[nodiscard]] HRESULT PreserveActiveDashboardPage(const AppSettings& previous, AppSettings& candidate) noexcept;
[[nodiscard]] bool ActiveDashboardRuntimeEquals(const AppSettings& left, const AppSettings& right) noexcept;
// The 0xRRGGBB background one widget instance paints: its own override, else the document background.
[[nodiscard]] inline uint32_t EffectiveWidgetBackgroundRgb(const AppSettings& settings,
                                                           const WidgetInstanceSettings& widget) noexcept
{
    return widget.overridesBackground ? widget.backgroundRgb : settings.backgroundRgb;
}
[[nodiscard]] HRESULT SetJsonObjectSettings(std::string_view json, JsonObjectSettings& settings) noexcept;
[[nodiscard]] HRESULT ValidateAppSettings(const AppSettings& settings) noexcept;
[[nodiscard]] HRESULT ParseAppSettingsJson(std::string_view json, AppSettings& settings) noexcept;
[[nodiscard]] HRESULT ParseAppSettingsJsonV4(std::string_view json, std::unique_ptr<AppSettings>& settings,
                                             SettingsParseDiagnostic* diagnostic = nullptr) noexcept;
[[nodiscard]] HRESULT ParseAppSettingsJsonV5(std::string_view json, std::unique_ptr<AppSettings>& settings,
                                             SettingsParseDiagnostic* diagnostic = nullptr) noexcept;
[[nodiscard]] HRESULT ParseAppSettingsJsonDetailed(std::string_view json, AppSettings& settings,
                                                   SettingsParseDiagnostic& diagnostic) noexcept;
[[nodiscard]] HRESULT LoadAppSettingsFile(std::wstring_view path, AppSettings& settings) noexcept;
[[nodiscard]] HRESULT QuerySettingsFileStamp(std::wstring_view path, SettingsFileStamp& stamp) noexcept;
[[nodiscard]] HRESULT SerializeFactoryConfigurationJson(const PluginSettings& plugin,
                                                        const WidgetInstanceSettings& instance,
                                                        std::array<char, kFactoryConfigurationCapacity>& json,
                                                        uint32_t& jsonBytes) noexcept;
// The same compact {"plugin":{},"instance":<effective-settings>} envelope for a configured service.
[[nodiscard]] HRESULT SerializeServiceConfigurationJson(const ServiceSettings& service,
                                                        std::array<char, kFactoryConfigurationCapacity>& json,
                                                        uint32_t& jsonBytes) noexcept;
[[nodiscard]] const ServiceSettings* FindServiceSettings(const AppSettings& settings,
                                                         std::string_view pluginId) noexcept;
[[nodiscard]] HRESULT PatchWidgetInstanceSettings(AppSettings& settings, std::string_view instanceId,
                                                  std::string_view settingsJson) noexcept;
// Sets `dock.thickness` in the typed settings and the retained source document (creating `dock`, and raising
// `version.minor` to 2 when lower); a dragged bar edge persists through this. The formatting contract applies.
[[nodiscard]] HRESULT PatchDockThickness(AppSettings& settings, uint32_t thicknessDips) noexcept;

class SettingsStore final
{
  public:
    [[nodiscard]] HRESULT Initialize(bool selfTest, std::wstring_view selectedPath,
                                     std::unique_ptr<AppSettings>& settings,
                                     std::wstring_view localAppDataOverride = {}) noexcept;
    [[nodiscard]] HRESULT TryLoadChanged(std::unique_ptr<AppSettings>& settings, SettingsFileStamp& stamp,
                                         SettingsReloadStatus& status) noexcept;
    void MarkApplied(const SettingsFileStamp& stamp) noexcept;
    void MarkRejected(const SettingsFileStamp& stamp) noexcept;
    // Forgets the applied and rejected stamps so the next TryLoadChanged re-reads the file even when it did not
    // change (redxe.settings.reload).
    void ForgetStamps() noexcept;
    void SuppressDocumentWrites(bool suppress) noexcept;

    [[nodiscard]] const std::wstring& SettingsPath() const noexcept;
    [[nodiscard]] const std::wstring& SettingsDirectory() const noexcept;
    [[nodiscard]] const std::wstring& LogsDirectory() const noexcept;
    [[nodiscard]] const std::wstring& SchemaPath() const noexcept;
    [[nodiscard]] bool UsedInitialFallback() const noexcept;
    [[nodiscard]] const std::wstring& InitialNotice() const noexcept;
    [[nodiscard]] const std::wstring& LastDiagnosticText() const noexcept;
    [[nodiscard]] HRESULT PersistPatchedDocument(const AppSettings& settings) noexcept;
    [[nodiscard]] HRESULT PersistWidgetSettings(AppSettings& settings, std::string_view instanceId,
                                                std::string_view settingsJson) noexcept;
    // PatchDockThickness plus the atomic document write; rolls both back on failure.
    [[nodiscard]] HRESULT PersistDockThickness(AppSettings& settings, uint32_t thicknessDips) noexcept;

  private:
    std::wstring _settingsPath;
    std::wstring _settingsDirectory;
    std::wstring _logsDirectory;
    std::wstring _schemaPath;
    std::optional<SettingsFileStamp> _lastAppliedStamp;
    std::optional<SettingsFileStamp> _lastRejectedStamp;
    bool _missingObserved = false;
    bool _usedInitialFallback = false;
    bool _selfTest = false;
    bool _suppressDocumentWrites = false;
    std::wstring _lastDiagnosticText;
    std::wstring _initialNotice;
};
