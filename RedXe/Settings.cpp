#include "Settings.h"

#include "PlugInterfaces/Factory.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <shlobj.h>
#include <string>
#include <utility>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

#include <yyjson.h>

namespace
{
using unique_yyjson_doc = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;
using unique_malloc_string = wil::unique_any<char*, decltype(&free), free>;

constexpr size_t kMaximumSettingsBytes = 1024U * 1024U;
constexpr char kSchemaReference[] = "RedXe.settings.schema.json";
constexpr char kTrianglePluginId[] = "builtin.rotating-triangle";
constexpr char kTriangleTypeId[] = "rotating-triangle";
constexpr char kGdiPluginId[] = "builtin.gdi-orbit";
constexpr char kGdiTypeId[] = "gdi-orbit";
constexpr char kMatrixPluginId[] = "builtin.matrix-rain";
constexpr char kMatrixTypeId[] = "matrix-rain";
constexpr char kProcessViewerPluginId[] = "builtin.process-viewer";
constexpr char kProcessViewerTypeId[] = "process-viewer";
constexpr char kStudioClockPluginId[] = "builtin.studio-clock";
constexpr char kStudioClockTypeId[] = "studio-clock";
constexpr char kDeskClockPluginId[] = "builtin.desk-clock";
constexpr char kDeskClockTypeId[] = "desk-clock";

#if defined(_DEBUG)
constexpr const wchar_t* kSelectedSettingsFileName = kRedXeDebugSettingsFileName;
#else
constexpr const wchar_t* kSelectedSettingsFileName = kRedXeReleaseSettingsFileName;
constexpr wchar_t kLegacyReleaseSettingsFileName[] = L"RedXe-1.0.settings.json";
#endif

template <size_t Count>
[[nodiscard]] bool HasExactKeys(yyjson_val* object, const std::array<const char*, Count>& expected) noexcept
{
    if (!yyjson_is_obj(object) || yyjson_obj_size(object) != Count)
    {
        return false;
    }

    std::array<bool, Count> seen{};
    yyjson_obj_iter iterator = yyjson_obj_iter_with(object);
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
    {
        const char* text = yyjson_get_str(key);
        bool matched = false;
        for (size_t index = 0; index < expected.size(); ++index)
        {
            if (text && std::strcmp(text, expected[index]) == 0 && !seen[index])
            {
                seen[index] = true;
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsValidStoredText(const SettingsText& text, bool machineId) noexcept
{
    if (text.bytes == 0 || text.bytes > kMaximumSettingsTextBytes || text.utf8[text.bytes] != '\0')
    {
        return false;
    }
    const std::string_view view = text.View();
    if (view.find('\0') != std::string_view::npos)
    {
        return false;
    }
    return !machineId || RedXeIsValidMachineId(text.utf8.data());
}

[[nodiscard]] bool CopyText(yyjson_val* value, SettingsText& destination, bool machineId) noexcept
{
    if (!yyjson_is_str(value))
    {
        return false;
    }
    const size_t length = yyjson_get_len(value);
    const char* text = yyjson_get_str(value);
    if (!text || length == 0 || length > kMaximumSettingsTextBytes ||
        std::string_view(text, length).find('\0') != std::string_view::npos)
    {
        return false;
    }

    SettingsText copied{};
    std::memcpy(copied.utf8.data(), text, length);
    copied.bytes = static_cast<uint32_t>(length);
    if (machineId && !RedXeIsValidMachineId(copied.utf8.data()))
    {
        return false;
    }
    destination = copied;
    return true;
}

[[nodiscard]] bool ReadUnsigned(yyjson_val* object, const char* key, uint32_t minimum, uint32_t maximum,
                                uint32_t& value) noexcept
{
    yyjson_val* member = yyjson_obj_get(object, key);
    if (!yyjson_is_uint(member))
    {
        return false;
    }
    const uint64_t parsed = yyjson_get_uint(member);
    if (parsed < minimum || parsed > maximum)
    {
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

[[nodiscard]] bool IsColor(yyjson_val* object, const char* key) noexcept
{
    yyjson_val* value = yyjson_obj_get(object, key);
    if (!yyjson_is_str(value) || yyjson_get_len(value) != 7)
    {
        return false;
    }
    const char* text = yyjson_get_str(value);
    if (!text || text[0] != '#')
    {
        return false;
    }
    for (size_t index = 1; index < 7; ++index)
    {
        const char character = text[index];
        if (!((character >= '0' && character <= '9') || (character >= 'A' && character <= 'F') ||
              (character >= 'a' && character <= 'f')))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsValidMatrixPrivate(yyjson_val* object) noexcept
{
    constexpr std::array keys{
        "seed",      "glyphHeightDips", "densityPercent",  "speedPercent", "trailLengthGlyphs", "mutationPerSecond",
        "headColor", "trailColor",      "backgroundColor", "glowPercent",
    };
    uint32_t value = 0;
    return HasExactKeys(object, keys) && ReadUnsigned(object, "seed", 0, UINT32_MAX, value) &&
           ReadUnsigned(object, "glyphHeightDips", 12, 48, value) &&
           ReadUnsigned(object, "densityPercent", 10, 100, value) &&
           ReadUnsigned(object, "speedPercent", 25, 300, value) &&
           ReadUnsigned(object, "trailLengthGlyphs", 6, 48, value) &&
           ReadUnsigned(object, "mutationPerSecond", 0, 30, value) && IsColor(object, "headColor") &&
           IsColor(object, "trailColor") && IsColor(object, "backgroundColor") &&
           ReadUnsigned(object, "glowPercent", 0, 100, value);
}

[[nodiscard]] bool IsValidStudioClockPrivate(yyjson_val* object) noexcept
{
    constexpr std::array keys{
        "showSecondProgress", "externalDotsAlwaysOn", "showSeconds", "secondsColor",
        "showDate",           "dateFormat",           "timeColor",   "backgroundColor",
    };
    yyjson_val* dateFormatValue = yyjson_obj_get(object, "dateFormat");
    const char* dateFormat = yyjson_is_str(dateFormatValue) ? yyjson_get_str(dateFormatValue) : nullptr;
    const bool validDateFormat =
        dateFormat && (std::strcmp(dateFormat, "dd-mm-yyyy") == 0 || std::strcmp(dateFormat, "mm-dd-yyyy") == 0 ||
                       std::strcmp(dateFormat, "yyyy-mm-dd") == 0);
    return HasExactKeys(object, keys) && yyjson_is_bool(yyjson_obj_get(object, "showSecondProgress")) &&
           yyjson_is_bool(yyjson_obj_get(object, "externalDotsAlwaysOn")) &&
           yyjson_is_bool(yyjson_obj_get(object, "showSeconds")) &&
           yyjson_is_bool(yyjson_obj_get(object, "showDate")) && validDateFormat && IsColor(object, "secondsColor") &&
           IsColor(object, "timeColor") && IsColor(object, "backgroundColor");
}

[[nodiscard]] bool IsValidDeskClockPrivate(yyjson_val* object) noexcept
{
    constexpr std::array keys{
        "flipDurationMilliseconds", "backgroundColor", "cardColor", "digitColor", "dateColor",
    };
    uint32_t duration = 0;
    return HasExactKeys(object, keys) && ReadUnsigned(object, "flipDurationMilliseconds", 250, 800, duration) &&
           IsColor(object, "backgroundColor") && IsColor(object, "cardColor") && IsColor(object, "digitColor") &&
           IsColor(object, "dateColor");
}

[[nodiscard]] bool IsSupportedPluginType(std::string_view pluginId, std::string_view typeId) noexcept
{
    return (SettingsIdEquals(pluginId, kTrianglePluginId) && SettingsIdEquals(typeId, kTriangleTypeId)) ||
           (SettingsIdEquals(pluginId, kGdiPluginId) && SettingsIdEquals(typeId, kGdiTypeId)) ||
           (SettingsIdEquals(pluginId, kMatrixPluginId) && SettingsIdEquals(typeId, kMatrixTypeId)) ||
           (SettingsIdEquals(pluginId, kProcessViewerPluginId) && SettingsIdEquals(typeId, kProcessViewerTypeId)) ||
           (SettingsIdEquals(pluginId, kStudioClockPluginId) && SettingsIdEquals(typeId, kStudioClockTypeId)) ||
           (SettingsIdEquals(pluginId, kDeskClockPluginId) && SettingsIdEquals(typeId, kDeskClockTypeId));
}

[[nodiscard]] unique_yyjson_doc ParseStoredObject(const JsonObjectSettings& settings) noexcept
{
    if (settings.bytes < 2 || settings.bytes > kPrivateConfigurationCapacity || settings.utf8[settings.bytes] != '\0')
    {
        return {};
    }
    std::array<char, kPrivateConfigurationCapacity + 1> mutableJson = settings.utf8;
    yyjson_read_err error{};
    unique_yyjson_doc document{
        yyjson_read_opts(mutableJson.data(), settings.bytes, YYJSON_READ_NOFLAG, nullptr, &error)};
    if (!document || !yyjson_is_obj(yyjson_doc_get_root(document.get())))
    {
        return {};
    }
    return document;
}

[[nodiscard]] bool IsEmptyPrivate(const JsonObjectSettings& settings) noexcept
{
    unique_yyjson_doc document = ParseStoredObject(settings);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    return yyjson_is_obj(root) && yyjson_obj_size(root) == 0;
}

[[nodiscard]] bool IsMatrixPrivate(const JsonObjectSettings& settings) noexcept
{
    unique_yyjson_doc document = ParseStoredObject(settings);
    return document && IsValidMatrixPrivate(yyjson_doc_get_root(document.get()));
}

[[nodiscard]] bool IsProcessViewerPrivate(const JsonObjectSettings& settings) noexcept
{
    unique_yyjson_doc document = ParseStoredObject(settings);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    constexpr std::array keys{"topN"};
    uint32_t topN = 0;
    return root && HasExactKeys(root, keys) && ReadUnsigned(root, "topN", 1, 32, topN);
}

[[nodiscard]] bool IsStudioClockPrivate(const JsonObjectSettings& settings) noexcept
{
    unique_yyjson_doc document = ParseStoredObject(settings);
    return document && IsValidStudioClockPrivate(yyjson_doc_get_root(document.get()));
}

[[nodiscard]] bool IsDeskClockPrivate(const JsonObjectSettings& settings) noexcept
{
    unique_yyjson_doc document = ParseStoredObject(settings);
    return document && IsValidDeskClockPrivate(yyjson_doc_get_root(document.get()));
}

[[nodiscard]] HRESULT CopyObject(yyjson_val* object, JsonObjectSettings& destination) noexcept
{
    if (!yyjson_is_obj(object))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    yyjson_write_err error{};
    size_t length = 0;
    unique_malloc_string serialized{yyjson_val_write_opts(object, YYJSON_WRITE_NOFLAG, nullptr, &length, &error)};
    if (!serialized)
    {
        return E_OUTOFMEMORY;
    }
    if (length == 0 || length > kPrivateConfigurationCapacity)
    {
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    }

    JsonObjectSettings copied{};
    copied.utf8.fill('\0');
    std::memcpy(copied.utf8.data(), serialized.get(), length);
    copied.bytes = static_cast<uint32_t>(length);
    destination = copied;
    return S_OK;
}

[[nodiscard]] HRESULT ParsePlugin(yyjson_val* value, PluginSettings& plugin) noexcept
{
    constexpr std::array keys{"id", "enabled", "private"};
    yyjson_val* enabled = yyjson_is_obj(value) ? yyjson_obj_get(value, "enabled") : nullptr;
    if (!HasExactKeys(value, keys) || !CopyText(yyjson_obj_get(value, "id"), plugin.id, true) ||
        !yyjson_is_bool(enabled))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    plugin.enabled = yyjson_get_bool(enabled);
    return CopyObject(yyjson_obj_get(value, "private"), plugin.privateConfiguration);
}

[[nodiscard]] HRESULT ParsePlacement(yyjson_val* value, WidgetGridPlacement& placement) noexcept
{
    constexpr std::array keys{"column", "row", "columnSpan", "rowSpan"};
    if (!HasExactKeys(value, keys) ||
        !ReadUnsigned(value, "column", 0, kMaximumDashboardGridDimension - 1, placement.column) ||
        !ReadUnsigned(value, "row", 0, kMaximumDashboardGridDimension - 1, placement.row) ||
        !ReadUnsigned(value, "columnSpan", 1, kMaximumDashboardGridDimension, placement.columnSpan) ||
        !ReadUnsigned(value, "rowSpan", 1, kMaximumDashboardGridDimension, placement.rowSpan))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

[[nodiscard]] HRESULT ParseWidget(yyjson_val* value, WidgetInstanceSettings& widget) noexcept
{
    constexpr std::array keys{"id", "pluginId", "typeId", "placement", "private"};
    if (!HasExactKeys(value, keys) || !CopyText(yyjson_obj_get(value, "id"), widget.id, true) ||
        !CopyText(yyjson_obj_get(value, "pluginId"), widget.pluginId, true) ||
        !CopyText(yyjson_obj_get(value, "typeId"), widget.typeId, true))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    HRESULT result = ParsePlacement(yyjson_obj_get(value, "placement"), widget.placement);
    if (SUCCEEDED(result))
    {
        result = CopyObject(yyjson_obj_get(value, "private"), widget.privateConfiguration);
    }
    return result;
}

[[nodiscard]] HRESULT ParsePage(yyjson_val* value, DashboardPageSettings& page)
{
    constexpr std::array keys{"id", "name", "widgets"};
    yyjson_val* widgets = yyjson_is_obj(value) ? yyjson_obj_get(value, "widgets") : nullptr;
    if (!HasExactKeys(value, keys) || !CopyText(yyjson_obj_get(value, "id"), page.id, true) ||
        !CopyText(yyjson_obj_get(value, "name"), page.name, false) || !yyjson_is_arr(widgets))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    const size_t count = yyjson_arr_size(widgets);
    if (count == 0 || count > kMaximumWidgetsPerPage)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    page.widgets.resize(count);
    for (size_t index = 0; index < count; ++index)
    {
        const HRESULT result = ParseWidget(yyjson_arr_get(widgets, index), page.widgets[index]);
        if (FAILED(result))
        {
            return result;
        }
    }
    page.widgetCount = static_cast<uint32_t>(count);
    return S_OK;
}

[[nodiscard]] HRESULT GetModuleDirectory(std::filesystem::path& directory) noexcept
{
    try
    {
        std::vector<wchar_t> path(512);
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0)
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
            if (length < path.size() - 1)
            {
                directory = std::filesystem::path(path.data()).parent_path();
                return directory.empty() ? HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME) : S_OK;
            }
            if (path.size() >= 32768)
            {
                return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            }
            path.resize(path.size() * 2);
        }
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT EnsureDirectory(const std::filesystem::path& directory) noexcept
{
    const int result = SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);
    if (result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS)
    {
        return S_OK;
    }
    return HRESULT_FROM_WIN32(static_cast<DWORD>(result));
}

[[nodiscard]] HRESULT CopyFileAtomically(const std::filesystem::path& source, const std::filesystem::path& target,
                                         bool replaceExisting) noexcept
{
    try
    {
        std::wstring temporary = target.wstring();
        temporary.append(L".tmp.");
        temporary.append(std::to_wstring(GetCurrentProcessId()));
        temporary.push_back(L'.');
        temporary.append(std::to_wstring(GetTickCount64()));

        if (!CopyFileW(source.c_str(), temporary.c_str(), TRUE))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        const auto cleanup = wil::scope_exit([&temporary]() noexcept { DeleteFileW(temporary.c_str()); });
        const DWORD flags = MOVEFILE_WRITE_THROUGH | (replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0U);
        if (!MoveFileExW(temporary.c_str(), target.c_str(), flags))
        {
            const DWORD error = GetLastError();
            if (!replaceExisting && (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS))
            {
                return S_FALSE;
            }
            return HRESULT_FROM_WIN32(error);
        }
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT InstallIfMissing(const std::filesystem::path& source,
                                       const std::filesystem::path& target) noexcept
{
    const DWORD attributes = GetFileAttributesW(target.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES)
    {
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ? S_FALSE : HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    }
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
    {
        return HRESULT_FROM_WIN32(error);
    }
    return CopyFileAtomically(source, target, false);
}

#if !defined(_DEBUG)
[[nodiscard]] HRESULT MigrateLegacyReleaseSettingsName(const std::filesystem::path& directory,
                                                       const std::filesystem::path& target) noexcept
{
    const DWORD targetAttributes = GetFileAttributesW(target.c_str());
    if (targetAttributes != INVALID_FILE_ATTRIBUTES)
    {
        return (targetAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ? S_FALSE : HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    }
    const DWORD targetError = GetLastError();
    if (targetError != ERROR_FILE_NOT_FOUND && targetError != ERROR_PATH_NOT_FOUND)
    {
        return HRESULT_FROM_WIN32(targetError);
    }

    const std::filesystem::path legacy = directory / kLegacyReleaseSettingsFileName;
    const DWORD legacyAttributes = GetFileAttributesW(legacy.c_str());
    if (legacyAttributes == INVALID_FILE_ATTRIBUTES)
    {
        const DWORD legacyError = GetLastError();
        return legacyError == ERROR_FILE_NOT_FOUND || legacyError == ERROR_PATH_NOT_FOUND
                   ? S_FALSE
                   : HRESULT_FROM_WIN32(legacyError);
    }
    if ((legacyAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    }
    return MoveFileExW(legacy.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) ? S_OK
                                                                               : HRESULT_FROM_WIN32(GetLastError());
}
#endif

[[nodiscard]] HRESULT BackupInvalidSettings(const std::filesystem::path& path,
                                            std::filesystem::path& backupPath) noexcept
{
    try
    {
        SYSTEMTIME utc{};
        GetSystemTime(&utc);
        wchar_t suffix[64]{};
        const int written = swprintf_s(suffix, L".invalid-%04u-%02u-%02u_%02u-%02u-%02uZ", utc.wYear, utc.wMonth,
                                       utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
        if (written <= 0)
        {
            return E_FAIL;
        }
        const std::filesystem::path backup =
            path.parent_path() / (path.stem().wstring() + suffix + path.extension().wstring());
        if (!MoveFileExW(path.c_str(), backup.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        backupPath = backup;
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT ReadFileBytes(std::wstring_view path, std::vector<char>& bytes) noexcept
{
    try
    {
        const std::wstring pathText(path);
        wil::unique_hfile file{CreateFileW(pathText.c_str(), GENERIC_READ,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
        if (!file)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.get(), &size))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) > kMaximumSettingsBytes)
        {
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        }

        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD totalRead = 0;
        while (totalRead < bytes.size())
        {
            DWORD chunkRead = 0;
            const DWORD remaining = static_cast<DWORD>(bytes.size() - totalRead);
            if (!ReadFile(file.get(), bytes.data() + totalRead, remaining, &chunkRead, nullptr))
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
            if (chunkRead == 0)
            {
                return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
            }
            totalRead += chunkRead;
        }
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}
} // namespace

bool SettingsIdEquals(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (size_t index = 0; index < left.size(); ++index)
    {
        if (RedXeAsciiLower(left[index]) != RedXeAsciiLower(right[index]))
        {
            return false;
        }
    }
    return true;
}

const PluginSettings* FindPluginSettings(const AppSettings& settings, std::string_view pluginId) noexcept
{
    for (uint32_t index = 0; index < settings.pluginCount; ++index)
    {
        if (SettingsIdEquals(settings.plugins[index].id.View(), pluginId))
        {
            return &settings.plugins[index];
        }
    }
    return nullptr;
}

PluginSettings* FindPluginSettings(AppSettings& settings, std::string_view pluginId) noexcept
{
    return const_cast<PluginSettings*>(FindPluginSettings(static_cast<const AppSettings&>(settings), pluginId));
}

const DashboardPageSettings* FindDashboardPage(const AppSettings& settings, std::string_view pageId) noexcept
{
    for (uint32_t index = 0; index < settings.dashboard.pageCount; ++index)
    {
        if (SettingsIdEquals(settings.dashboard.pages[index].id.View(), pageId))
        {
            return &settings.dashboard.pages[index];
        }
    }
    return nullptr;
}

DashboardPageSettings* FindDashboardPage(AppSettings& settings, std::string_view pageId) noexcept
{
    return const_cast<DashboardPageSettings*>(FindDashboardPage(static_cast<const AppSettings&>(settings), pageId));
}

const DashboardPageSettings* FindActiveDashboardPage(const AppSettings& settings) noexcept
{
    return FindDashboardPage(settings, settings.dashboard.activePageId.View());
}

DashboardPageSettings* FindActiveDashboardPage(AppSettings& settings) noexcept
{
    return FindDashboardPage(settings, settings.dashboard.activePageId.View());
}

HRESULT MoveDashboardPage(AppSettings& settings, int direction) noexcept
{
    if (settings.dashboard.pageCount < 2 || settings.dashboard.pages.size() != settings.dashboard.pageCount ||
        settings.dashboard.activePageIndex >= settings.dashboard.pageCount || (direction != -1 && direction != 1))
    {
        return E_INVALIDARG;
    }
    const uint32_t current = settings.dashboard.activePageIndex;
    uint32_t selected = current;
    if (direction > 0)
    {
        if (current + 1U < settings.dashboard.pageCount)
            selected = current + 1U;
        else if (settings.dashboard.wrapPages)
            selected = 0;
        else
            return HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);
    }
    else
    {
        if (current > 0)
            selected = current - 1U;
        else if (settings.dashboard.wrapPages)
            selected = settings.dashboard.pageCount - 1U;
        else
            return HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);
    }
    settings.dashboard.activePageIndex = selected;
    settings.dashboard.activePageId = settings.dashboard.pages[selected].id;
    return S_OK;
}

bool ActiveDashboardRuntimeEquals(const AppSettings& left, const AppSettings& right) noexcept
{
    if (left.dashboard.gridColumns != right.dashboard.gridColumns ||
        left.dashboard.gridRows != right.dashboard.gridRows)
    {
        return false;
    }

    const DashboardPageSettings* leftPage = FindActiveDashboardPage(left);
    const DashboardPageSettings* rightPage = FindActiveDashboardPage(right);
    if (!leftPage || !rightPage || leftPage->widgetCount != rightPage->widgetCount)
    {
        return false;
    }

    for (uint32_t index = 0; index < leftPage->widgetCount; ++index)
    {
        const WidgetInstanceSettings& leftWidget = leftPage->widgets[index];
        const WidgetInstanceSettings& rightWidget = rightPage->widgets[index];
        if (leftWidget != rightWidget)
        {
            return false;
        }

        const PluginSettings* leftPlugin = FindPluginSettings(left, leftWidget.pluginId.View());
        const PluginSettings* rightPlugin = FindPluginSettings(right, rightWidget.pluginId.View());
        if (!leftPlugin || !rightPlugin || *leftPlugin != *rightPlugin)
        {
            return false;
        }
    }
    return true;
}

HRESULT SetJsonObjectSettings(std::string_view json, JsonObjectSettings& settings) noexcept
{
    if (json.empty() || json.size() > kPrivateConfigurationCapacity)
    {
        return E_INVALIDARG;
    }
    try
    {
        std::vector<char> mutableJson(json.begin(), json.end());
        yyjson_read_err error{};
        unique_yyjson_doc document{
            yyjson_read_opts(mutableJson.data(), mutableJson.size(), YYJSON_READ_NOFLAG, nullptr, &error)};
        return document ? CopyObject(yyjson_doc_get_root(document.get()), settings)
                        : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

HRESULT ValidateAppSettings(const AppSettings& settings) noexcept
{
    if (settings.pluginCount > kMaximumSettingsPlugins || settings.plugins.size() != settings.pluginCount ||
        settings.dashboard.gridColumns == 0 || settings.dashboard.gridRows == 0 ||
        settings.dashboard.gridColumns > kMaximumDashboardGridDimension ||
        settings.dashboard.gridRows > kMaximumDashboardGridDimension || settings.dashboard.pageCount == 0 ||
        settings.dashboard.pageCount > kMaximumDashboardPages ||
        !IsValidStoredText(settings.dashboard.activePageId, true))
    {
        return E_INVALIDARG;
    }

    for (uint32_t index = 0; index < settings.pluginCount; ++index)
    {
        const PluginSettings& plugin = settings.plugins[index];
        if (!IsValidStoredText(plugin.id, true) || !ParseStoredObject(plugin.privateConfiguration))
        {
            return E_INVALIDARG;
        }
        for (uint32_t previous = 0; previous < index; ++previous)
        {
            if (SettingsIdEquals(settings.plugins[previous].id.View(), plugin.id.View()))
            {
                return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
            }
        }
        if ((SettingsIdEquals(plugin.id.View(), kTrianglePluginId) ||
             SettingsIdEquals(plugin.id.View(), kGdiPluginId) || SettingsIdEquals(plugin.id.View(), kMatrixPluginId) ||
             SettingsIdEquals(plugin.id.View(), kProcessViewerPluginId) ||
             SettingsIdEquals(plugin.id.View(), kStudioClockPluginId) ||
             SettingsIdEquals(plugin.id.View(), kDeskClockPluginId)) &&
            !IsEmptyPrivate(plugin.privateConfiguration))
        {
            return E_INVALIDARG;
        }
    }

    bool activeFound = false;
    for (uint32_t pageIndex = 0; pageIndex < settings.dashboard.pageCount; ++pageIndex)
    {
        const DashboardPageSettings& page = settings.dashboard.pages[pageIndex];
        if (!IsValidStoredText(page.id, true) || !IsValidStoredText(page.name, false) ||
            page.widgetCount > kMaximumWidgetsPerPage || page.widgets.size() != page.widgetCount)
        {
            return E_INVALIDARG;
        }
        if (SettingsIdEquals(page.id.View(), settings.dashboard.activePageId.View()))
        {
            activeFound = true;
        }
        for (uint32_t previousPage = 0; previousPage < pageIndex; ++previousPage)
        {
            if (SettingsIdEquals(settings.dashboard.pages[previousPage].id.View(), page.id.View()))
            {
                return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
            }
        }

        std::array<bool, kMaximumDashboardGridDimension * kMaximumDashboardGridDimension> occupied{};
        uint32_t matrixCount = 0;
        for (uint32_t widgetIndex = 0; widgetIndex < page.widgetCount; ++widgetIndex)
        {
            const WidgetInstanceSettings& widget = page.widgets[widgetIndex];
            const WidgetGridPlacement& placement = widget.placement;
            if (!IsValidStoredText(widget.id, true) || !IsValidStoredText(widget.pluginId, true) ||
                !IsValidStoredText(widget.typeId, true) || !ParseStoredObject(widget.privateConfiguration) ||
                (!widget.usesAdaptivePlacement &&
                 (placement.column >= settings.dashboard.gridColumns || placement.row >= settings.dashboard.gridRows ||
                  placement.columnSpan == 0 || placement.rowSpan == 0 ||
                  placement.columnSpan > settings.dashboard.gridColumns - placement.column ||
                  placement.rowSpan > settings.dashboard.gridRows - placement.row)) ||
                (widget.usesAdaptivePlacement &&
                 (widget.adaptivePlacement.depth == 0 || widget.adaptivePlacement.depth > kMaximumLayoutDepth)))
            {
                return E_INVALIDARG;
            }
            if (widget.usesAdaptivePlacement)
            {
                for (uint32_t stepIndex = 0; stepIndex < widget.adaptivePlacement.depth; ++stepIndex)
                {
                    const LayoutSplitStep& step = widget.adaptivePlacement.steps[stepIndex];
                    if ((step.axis != LayoutAxis::LongSide && step.axis != LayoutAxis::ShortSide) ||
                        step.sizeRatio == 0 || step.sizeRatio > 1000 || step.totalRatio == 0 ||
                        step.precedingRatio >= step.totalRatio ||
                        step.sizeRatio > step.totalRatio - step.precedingRatio)
                    {
                        return E_INVALIDARG;
                    }
                }
            }

            for (uint32_t previousPage = 0; previousPage <= pageIndex; ++previousPage)
            {
                const DashboardPageSettings& earlierPage = settings.dashboard.pages[previousPage];
                const uint32_t limit = previousPage == pageIndex ? widgetIndex : earlierPage.widgetCount;
                for (uint32_t previousWidget = 0; previousWidget < limit; ++previousWidget)
                {
                    if (SettingsIdEquals(earlierPage.widgets[previousWidget].id.View(), widget.id.View()))
                    {
                        return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
                    }
                }
            }

            const PluginSettings* plugin = FindPluginSettings(settings, widget.pluginId.View());
            if (!plugin || !plugin->enabled || !IsSupportedPluginType(widget.pluginId.View(), widget.typeId.View()))
            {
                return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            }
            if (SettingsIdEquals(widget.pluginId.View(), kMatrixPluginId))
            {
                ++matrixCount;
                if (!IsMatrixPrivate(widget.privateConfiguration))
                {
                    return E_INVALIDARG;
                }
            }
            else if (SettingsIdEquals(widget.pluginId.View(), kProcessViewerPluginId))
            {
                if (!IsProcessViewerPrivate(widget.privateConfiguration))
                {
                    return E_INVALIDARG;
                }
            }
            else if (SettingsIdEquals(widget.pluginId.View(), kStudioClockPluginId))
            {
                if (!IsStudioClockPrivate(widget.privateConfiguration))
                {
                    return E_INVALIDARG;
                }
            }
            else if (SettingsIdEquals(widget.pluginId.View(), kDeskClockPluginId))
            {
                if (!IsDeskClockPrivate(widget.privateConfiguration))
                {
                    return E_INVALIDARG;
                }
            }
            else if (!IsEmptyPrivate(widget.privateConfiguration))
            {
                return E_INVALIDARG;
            }

            if (widget.usesAdaptivePlacement)
            {
                continue;
            }
            for (uint32_t row = placement.row; row < placement.row + placement.rowSpan; ++row)
            {
                for (uint32_t column = placement.column; column < placement.column + placement.columnSpan;
                     ++column)
                {
                    const size_t cell = static_cast<size_t>(row) * settings.dashboard.gridColumns + column;
                    if (occupied[cell])
                    {
                        return HRESULT_FROM_WIN32(ERROR_ALREADY_ASSIGNED);
                    }
                    occupied[cell] = true;
                }
            }
        }
        if (matrixCount > 1)
        {
            return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
        }
    }
    return activeFound ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

[[nodiscard]] HRESULT ParseAppSettingsJsonCandidate(std::string_view json,
                                                    std::unique_ptr<AppSettings>& settings) noexcept
{
    return ParseAppSettingsJsonV4(json, settings);
}

HRESULT ParseAppSettingsJson(std::string_view json, AppSettings& settings) noexcept
{
    std::unique_ptr<AppSettings> parsed;
    const HRESULT result = ParseAppSettingsJsonCandidate(json, parsed);
    if (SUCCEEDED(result))
    {
        settings = *parsed;
    }
    return result;
}

HRESULT ParseAppSettingsJsonDetailed(std::string_view json, AppSettings& settings,
                                     SettingsParseDiagnostic& diagnostic) noexcept
{
    std::unique_ptr<AppSettings> parsed;
    const HRESULT result = ParseAppSettingsJsonV4(json, parsed, &diagnostic);
    if (SUCCEEDED(result))
    {
        settings = *parsed;
    }
    return result;
}

HRESULT SerializeFactoryConfigurationJson(const PluginSettings& plugin, const WidgetInstanceSettings& instance,
                                          std::array<char, kFactoryConfigurationCapacity>& json,
                                          uint32_t& jsonBytes) noexcept
{
    json.fill('\0');
    jsonBytes = 0;
    if (plugin.privateConfiguration.bytes == 0 || instance.privateConfiguration.bytes == 0 ||
        plugin.privateConfiguration.bytes > kPrivateConfigurationCapacity ||
        instance.privateConfiguration.bytes > kPrivateConfigurationCapacity)
    {
        return E_INVALIDARG;
    }

    const int written =
        sprintf_s(json.data(), json.size(), "{\"plugin\":%.*s,\"instance\":%.*s}",
                  static_cast<int>(plugin.privateConfiguration.bytes), plugin.privateConfiguration.utf8.data(),
                  static_cast<int>(instance.privateConfiguration.bytes), instance.privateConfiguration.utf8.data());
    if (written <= 0 || static_cast<size_t>(written) >= json.size())
    {
        json.fill('\0');
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    jsonBytes = static_cast<uint32_t>(written);
    return S_OK;
}

HRESULT LoadAppSettingsFile(std::wstring_view path, AppSettings& settings) noexcept
{
    std::vector<char> bytes;
    const HRESULT result = ReadFileBytes(path, bytes);
    return SUCCEEDED(result) ? ParseAppSettingsJson(std::string_view(bytes.data(), bytes.size()), settings) : result;
}

[[nodiscard]] HRESULT LoadAppSettingsFileCandidate(std::wstring_view path, std::unique_ptr<AppSettings>& settings,
                                                   SettingsParseDiagnostic* diagnostic = nullptr) noexcept
{
    std::vector<char> bytes;
    const HRESULT result = ReadFileBytes(path, bytes);
    return SUCCEEDED(result)
               ? (diagnostic
                      ? ParseAppSettingsJsonV4(std::string_view(bytes.data(), bytes.size()), settings, diagnostic)
                      : ParseAppSettingsJsonCandidate(std::string_view(bytes.data(), bytes.size()), settings))
               : result;
}

[[nodiscard]] std::wstring FormatDiagnostic(const SettingsParseDiagnostic& diagnostic) noexcept
{
    try
    {
        std::wstring message = L"Line " + std::to_wstring(diagnostic.line) + L", column " +
                               std::to_wstring(diagnostic.column) + L", path ";
        const auto appendUtf8 = [&message](std::string_view text)
        {
            if (text.empty())
                return;
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                                  static_cast<int>(text.size()), nullptr, 0);
            if (count <= 0)
                return;
            const size_t offset = message.size();
            message.resize(offset + static_cast<size_t>(count));
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                message.data() + offset, count);
        };
        appendUtf8(diagnostic.path);
        message += L": ";
        appendUtf8(diagnostic.message);
        return message;
    }
    catch (...)
    {
        return L"The settings file is invalid.";
    }
}

HRESULT QuerySettingsFileStamp(std::wstring_view path, SettingsFileStamp& stamp) noexcept
{
    try
    {
        const std::wstring pathText(path);
        wil::unique_hfile file{CreateFileW(pathText.c_str(), FILE_READ_ATTRIBUTES,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (!file)
        {
            const DWORD error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? S_FALSE : HRESULT_FROM_WIN32(error);
        }

        BY_HANDLE_FILE_INFORMATION information{};
        if (!GetFileInformationByHandle(file.get(), &information))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        SettingsFileStamp queried{};
        queried.volumeSerialNumber = information.dwVolumeSerialNumber;
        queried.fileIndexHigh = information.nFileIndexHigh;
        queried.fileIndexLow = information.nFileIndexLow;
        queried.lastWriteTime = (static_cast<uint64_t>(information.ftLastWriteTime.dwHighDateTime) << 32U) |
                                information.ftLastWriteTime.dwLowDateTime;
        queried.fileSize = (static_cast<uint64_t>(information.nFileSizeHigh) << 32U) | information.nFileSizeLow;
        stamp = queried;
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

HRESULT SettingsStore::Initialize(bool selfTest, std::wstring_view selectedPath, std::unique_ptr<AppSettings>& settings,
                                  std::wstring_view localAppDataOverride) noexcept
{
    settings.reset();
    _usedInitialFallback = false;
    _initialNotice.clear();
    try
    {
        std::filesystem::path moduleDirectory;
        HRESULT result = GetModuleDirectory(moduleDirectory);
        if (FAILED(result))
        {
            return result;
        }

        const std::filesystem::path deployedSettings = moduleDirectory / L"Settings";
        const std::filesystem::path selectedTemplate = deployedSettings / kSelectedSettingsFileName;
        const std::filesystem::path deployedSchema = deployedSettings / kRedXeSettingsSchemaFileName;
        if (selfTest)
        {
            _settingsPath = selectedTemplate.wstring();
            _settingsDirectory = deployedSettings.wstring();
            _schemaPath = deployedSchema.wstring();
            result = LoadAppSettingsFileCandidate(_settingsPath, settings);
            if (FAILED(result))
            {
                return result;
            }
            SettingsFileStamp stamp{};
            if (QuerySettingsFileStamp(_settingsPath, stamp) == S_OK)
            {
                _lastAppliedStamp = stamp;
            }
            return S_OK;
        }

        if (!selectedPath.empty())
        {
            const std::filesystem::path externalPath = std::filesystem::absolute(std::filesystem::path(selectedPath));
            _settingsPath = externalPath.wstring();
            _settingsDirectory = externalPath.parent_path().wstring();
            _schemaPath = deployedSchema.wstring();

            result = LoadAppSettingsFileCandidate(_settingsPath, settings);
            if (FAILED(result))
            {
                _usedInitialFallback = true;
                _initialNotice = L"The selected settings file could not be loaded. RedXe is running with its "
                                 L"default configuration; the selected file was not changed.";
                result = LoadAppSettingsFileCandidate(selectedTemplate.wstring(), settings);
            }
            if (FAILED(result))
            {
                return result;
            }
            SettingsFileStamp stamp{};
            if (QuerySettingsFileStamp(_settingsPath, stamp) == S_OK && !_usedInitialFallback)
            {
                _lastAppliedStamp = stamp;
            }
            return S_OK;
        }

        wil::unique_cotaskmem_string localAppData;
        if (localAppDataOverride.empty())
        {
            result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, localAppData.put());
            if (FAILED(result))
            {
                return result;
            }
        }

        const std::filesystem::path settingsDirectory =
            std::filesystem::path(localAppDataOverride.empty() ? localAppData.get() : localAppDataOverride) / L"RedXe" /
            L"Settings";
        result = EnsureDirectory(settingsDirectory);
        if (FAILED(result))
        {
            return result;
        }

        const std::filesystem::path settingsPath = settingsDirectory / kSelectedSettingsFileName;
        const std::filesystem::path schemaPath = settingsDirectory / kRedXeSettingsSchemaFileName;
        _settingsPath = settingsPath.wstring();
        _settingsDirectory = settingsDirectory.wstring();
        _schemaPath = schemaPath.wstring();

        result = CopyFileAtomically(deployedSchema, schemaPath, true);
        if (FAILED(result))
        {
            return result;
        }

        std::filesystem::path initialSource = selectedTemplate;
#if !defined(_DEBUG)
        result = MigrateLegacyReleaseSettingsName(settingsDirectory, settingsPath);
        if (FAILED(result))
        {
            return result;
        }
#endif
        result = InstallIfMissing(initialSource, settingsPath);
        if (FAILED(result))
        {
            return result;
        }

        result = LoadAppSettingsFileCandidate(_settingsPath, settings);
        if (result == HRESULT_FROM_WIN32(ERROR_INVALID_DATA) || result == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE))
        {
            std::filesystem::path backupPath;
            const HRESULT backupResult = BackupInvalidSettings(settingsPath, backupPath);
            if (FAILED(backupResult))
            {
                return backupResult;
            }
            result = CopyFileAtomically(selectedTemplate, settingsPath, true);
            if (SUCCEEDED(result))
            {
                result = LoadAppSettingsFileCandidate(_settingsPath, settings);
            }
            if (SUCCEEDED(result))
            {
                _usedInitialFallback = true;
                _initialNotice = L"The default settings file was incompatible or invalid. It was preserved as:\n" +
                                 backupPath.wstring() + L"\n\nA fresh default configuration was installed.";
            }
        }
        if (FAILED(result))
        {
            return result;
        }

        SettingsFileStamp stamp{};
        result = QuerySettingsFileStamp(_settingsPath, stamp);
        if (result != S_OK)
        {
            return result == S_FALSE ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : result;
        }
        _lastAppliedStamp = stamp;
        _lastRejectedStamp.reset();
        _missingObserved = false;
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

HRESULT SettingsStore::TryLoadChanged(std::unique_ptr<AppSettings>& settings, SettingsFileStamp& stamp,
                                      SettingsReloadStatus& status) noexcept
{
    settings.reset();
    status = SettingsReloadStatus::Unchanged;
    SettingsFileStamp currentStamp{};
    const HRESULT stampResult = QuerySettingsFileStamp(_settingsPath, currentStamp);
    if (stampResult == S_FALSE)
    {
        if (!_missingObserved)
        {
            _missingObserved = true;
            status = SettingsReloadStatus::Missing;
        }
        return S_OK;
    }
    if (FAILED(stampResult))
    {
        return stampResult;
    }
    _missingObserved = false;

    if ((_lastAppliedStamp && *_lastAppliedStamp == currentStamp) ||
        (_lastRejectedStamp && *_lastRejectedStamp == currentStamp))
    {
        return S_OK;
    }

    std::unique_ptr<AppSettings> candidate;
    SettingsParseDiagnostic diagnostic;
    const HRESULT loadResult = LoadAppSettingsFileCandidate(_settingsPath, candidate, &diagnostic);
    if (loadResult == HRESULT_FROM_WIN32(ERROR_INVALID_DATA) || loadResult == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE))
    {
        _lastRejectedStamp = currentStamp;
        _lastDiagnosticText = FormatDiagnostic(diagnostic);
        stamp = currentStamp;
        status = SettingsReloadStatus::Invalid;
        return S_OK;
    }
    if (FAILED(loadResult))
    {
        return loadResult;
    }

    settings = std::move(candidate);
    _lastDiagnosticText.clear();
    stamp = currentStamp;
    status = SettingsReloadStatus::Loaded;
    return S_OK;
}

void SettingsStore::MarkApplied(const SettingsFileStamp& stamp) noexcept
{
    _lastAppliedStamp = stamp;
    _lastRejectedStamp.reset();
}

void SettingsStore::MarkRejected(const SettingsFileStamp& stamp) noexcept
{
    _lastRejectedStamp = stamp;
}

const std::wstring& SettingsStore::SettingsPath() const noexcept
{
    return _settingsPath;
}

const std::wstring& SettingsStore::SettingsDirectory() const noexcept
{
    return _settingsDirectory;
}

const std::wstring& SettingsStore::SchemaPath() const noexcept
{
    return _schemaPath;
}

bool SettingsStore::UsedInitialFallback() const noexcept
{
    return _usedInitialFallback;
}

const std::wstring& SettingsStore::InitialNotice() const noexcept
{
    return _initialNotice;
}

const std::wstring& SettingsStore::LastDiagnosticText() const noexcept
{
    return _lastDiagnosticText;
}
