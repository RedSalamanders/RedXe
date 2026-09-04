#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

inline constexpr uint32_t kRedXeSettingsVersionMajor = 4;
inline constexpr uint32_t kRedXeSettingsVersionMinor = 0;
// Removed with the v3 parser; retained temporarily so the transition remains buildable between slices.
inline constexpr wchar_t kRedXeDebugSettingsFileName[] = L"RedXe-debug.settings.json";
inline constexpr wchar_t kRedXeReleaseSettingsFileName[] = L"RedXe.settings.json";
inline constexpr wchar_t kRedXeSettingsSchemaFileName[] = L"RedXe.settings.schema.json";

inline constexpr size_t kMaximumSettingsPlugins = 64;
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

struct AppSettings final
{
    uint32_t versionMajor = kRedXeSettingsVersionMajor;
    uint32_t versionMinor = kRedXeSettingsVersionMinor;
    std::string sourceDocument;
    std::vector<PluginSettings> plugins;
    uint32_t pluginCount = 0;
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
[[nodiscard]] HRESULT SetJsonObjectSettings(std::string_view json, JsonObjectSettings& settings) noexcept;
[[nodiscard]] HRESULT ValidateAppSettings(const AppSettings& settings) noexcept;
[[nodiscard]] HRESULT ParseAppSettingsJson(std::string_view json, AppSettings& settings) noexcept;
[[nodiscard]] HRESULT ParseAppSettingsJsonV4(std::string_view json, std::unique_ptr<AppSettings>& settings,
                                             SettingsParseDiagnostic* diagnostic = nullptr) noexcept;
[[nodiscard]] HRESULT ParseAppSettingsJsonDetailed(std::string_view json, AppSettings& settings,
                                                   SettingsParseDiagnostic& diagnostic) noexcept;
[[nodiscard]] HRESULT LoadAppSettingsFile(std::wstring_view path, AppSettings& settings) noexcept;
[[nodiscard]] HRESULT QuerySettingsFileStamp(std::wstring_view path, SettingsFileStamp& stamp) noexcept;
[[nodiscard]] HRESULT SerializeFactoryConfigurationJson(const PluginSettings& plugin,
                                                        const WidgetInstanceSettings& instance,
                                                        std::array<char, kFactoryConfigurationCapacity>& json,
                                                        uint32_t& jsonBytes) noexcept;

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

    [[nodiscard]] const std::wstring& SettingsPath() const noexcept;
    [[nodiscard]] const std::wstring& SettingsDirectory() const noexcept;
    [[nodiscard]] const std::wstring& SchemaPath() const noexcept;
    [[nodiscard]] bool UsedInitialFallback() const noexcept;
    [[nodiscard]] const std::wstring& InitialNotice() const noexcept;
    [[nodiscard]] const std::wstring& LastDiagnosticText() const noexcept;

  private:
    std::wstring _settingsPath;
    std::wstring _settingsDirectory;
    std::wstring _schemaPath;
    std::optional<SettingsFileStamp> _lastAppliedStamp;
    std::optional<SettingsFileStamp> _lastRejectedStamp;
    bool _missingObserved = false;
    bool _usedInitialFallback = false;
    std::wstring _lastDiagnosticText;
    std::wstring _initialNotice;
};
