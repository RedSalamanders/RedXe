#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <windows.h>

inline constexpr std::uint32_t kRedXeSettingsSchemaVersion = 3;
inline constexpr wchar_t kRedXeDebugSettingsFileName[] = L"RedXe-debug.settings.json";
inline constexpr wchar_t kRedXeReleaseSettingsFileName[] = L"RedXe-1.0.settings.json";
inline constexpr wchar_t kRedXeSettingsSchemaFileName[] = L"RedXe.settings.schema.json";

inline constexpr std::size_t kMaximumSettingsPlugins = 16;
inline constexpr std::size_t kMaximumDashboardPages = 8;
inline constexpr std::size_t kMaximumWidgetsPerPage = 16;
inline constexpr std::size_t kMaximumSettingsTextBytes = 128;
inline constexpr std::size_t kPrivateConfigurationCapacity = 1024;
inline constexpr std::size_t kFactoryConfigurationCapacity = 4096;
inline constexpr std::uint32_t kMaximumDashboardGridDimension = 64;

struct SettingsText final
{
    std::array<char, kMaximumSettingsTextBytes + 1> utf8{};
    std::uint32_t bytes = 0;

    [[nodiscard]] std::string_view View() const noexcept
    {
        return std::string_view(utf8.data(), bytes);
    }

    bool operator==(const SettingsText&) const noexcept = default;
};

struct JsonObjectSettings final
{
    std::array<char, kPrivateConfigurationCapacity + 1> utf8{'{', '}'};
    std::uint32_t bytes = 2;

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
    std::uint32_t column = 0;
    std::uint32_t row = 0;
    std::uint32_t columnSpan = 1;
    std::uint32_t rowSpan = 1;

    bool operator==(const WidgetGridPlacement&) const noexcept = default;
};

struct WidgetInstanceSettings final
{
    SettingsText id;
    SettingsText pluginId;
    SettingsText typeId;
    WidgetGridPlacement placement;
    JsonObjectSettings privateConfiguration;

    bool operator==(const WidgetInstanceSettings&) const noexcept = default;
};

struct DashboardPageSettings final
{
    SettingsText id;
    SettingsText name;
    std::array<WidgetInstanceSettings, kMaximumWidgetsPerPage> widgets{};
    std::uint32_t widgetCount = 0;

    bool operator==(const DashboardPageSettings&) const noexcept = default;
};

struct DashboardSettings final
{
    std::uint32_t gridColumns = 32;
    std::uint32_t gridRows = 9;
    SettingsText activePageId;
    std::array<DashboardPageSettings, kMaximumDashboardPages> pages{};
    std::uint32_t pageCount = 0;

    bool operator==(const DashboardSettings&) const noexcept = default;
};

struct AppSettings final
{
    std::array<PluginSettings, kMaximumSettingsPlugins> plugins{};
    std::uint32_t pluginCount = 0;
    DashboardSettings dashboard;

    bool operator==(const AppSettings&) const noexcept = default;
};

inline constexpr std::size_t kMaximumAppSettingsStorageBytes = 256U * 1024U;
static_assert(sizeof(AppSettings) <= kMaximumAppSettingsStorageBytes);

struct SettingsFileStamp final
{
    std::uint32_t volumeSerialNumber = 0;
    std::uint32_t fileIndexHigh = 0;
    std::uint32_t fileIndexLow = 0;
    std::uint64_t lastWriteTime = 0;
    std::uint64_t fileSize = 0;

    bool operator==(const SettingsFileStamp&) const noexcept = default;
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
[[nodiscard]] bool ActiveDashboardRuntimeEquals(const AppSettings& left, const AppSettings& right) noexcept;
[[nodiscard]] HRESULT SetJsonObjectSettings(std::string_view json, JsonObjectSettings& settings) noexcept;
[[nodiscard]] HRESULT ValidateAppSettings(const AppSettings& settings) noexcept;
[[nodiscard]] HRESULT ParseAppSettingsJson(std::string_view json, AppSettings& settings) noexcept;
[[nodiscard]] HRESULT LoadAppSettingsFile(std::wstring_view path, AppSettings& settings) noexcept;
[[nodiscard]] HRESULT QuerySettingsFileStamp(std::wstring_view path, SettingsFileStamp& stamp) noexcept;
[[nodiscard]] HRESULT SerializeFactoryConfigurationJson(const PluginSettings& plugin,
                                                        const WidgetInstanceSettings& instance,
                                                        std::array<char, kFactoryConfigurationCapacity>& json,
                                                        std::uint32_t& jsonBytes) noexcept;

class SettingsStore final
{
  public:
    [[nodiscard]] HRESULT Initialize(bool selfTest, std::unique_ptr<AppSettings>& settings) noexcept;
    [[nodiscard]] HRESULT TryLoadChanged(std::unique_ptr<AppSettings>& settings, SettingsFileStamp& stamp,
                                         SettingsReloadStatus& status) noexcept;
    void MarkApplied(const SettingsFileStamp& stamp) noexcept;
    void MarkRejected(const SettingsFileStamp& stamp) noexcept;

    [[nodiscard]] const std::wstring& SettingsPath() const noexcept;
    [[nodiscard]] const std::wstring& SettingsDirectory() const noexcept;
    [[nodiscard]] const std::wstring& SchemaPath() const noexcept;

  private:
    std::wstring _settingsPath;
    std::wstring _settingsDirectory;
    std::wstring _schemaPath;
    std::optional<SettingsFileStamp> _lastAppliedStamp;
    std::optional<SettingsFileStamp> _lastRejectedStamp;
    bool _missingObserved = false;
};
