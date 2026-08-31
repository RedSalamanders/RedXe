#include "../../RedXe/Settings.h"
#include "../../RedXe/SettingsWatcher.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

#include <yyjson.h>

namespace
{
using unique_yyjson_doc = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;

constexpr std::string_view kMatrixPrivate =
    R"json({"seed":1999,"glyphHeightDips":18,"densityPercent":70,"speedPercent":100,"trailLengthGlyphs":18,"mutationPerSecond":8,"headColor":"#D8FFE5","trailColor":"#00E65C","backgroundColor":"#010502","glowPercent":35})json";

constexpr std::string_view kMinimalDocument =
    R"json({"$schema":"RedXe.settings.schema.json","schemaVersion":3,"plugins":[{"id":"builtin.rotating-triangle","enabled":true,"private":{}},{"id":"builtin.gdi-orbit","enabled":false,"private":{}},{"id":"builtin.matrix-rain","enabled":true,"private":{}}],"dashboard":{"grid":{"columns":4,"rows":2},"activePageId":"page.one","pages":[{"id":"page.one","name":"One","widgets":[{"id":"triangle.one","pluginId":"builtin.rotating-triangle","typeId":"rotating-triangle","placement":{"column":0,"row":0,"columnSpan":2,"rowSpan":2},"private":{}}]},{"id":"page.two","name":"Two","widgets":[{"id":"matrix.two","pluginId":"builtin.matrix-rain","typeId":"matrix-rain","placement":{"column":0,"row":0,"columnSpan":4,"rowSpan":2},"private":{"seed":1999,"glyphHeightDips":18,"densityPercent":70,"speedPercent":100,"trailLengthGlyphs":18,"mutationPerSecond":8,"headColor":"#D8FFE5","trailColor":"#00E65C","backgroundColor":"#010502","glowPercent":35}}]}]}})json";

constexpr std::string_view kOverlappingDocument =
    R"json({"$schema":"RedXe.settings.schema.json","schemaVersion":3,"plugins":[{"id":"builtin.rotating-triangle","enabled":true,"private":{}}],"dashboard":{"grid":{"columns":4,"rows":2},"activePageId":"page.one","pages":[{"id":"page.one","name":"One","widgets":[{"id":"triangle.one","pluginId":"builtin.rotating-triangle","typeId":"rotating-triangle","placement":{"column":0,"row":0,"columnSpan":3,"rowSpan":2},"private":{}},{"id":"triangle.two","pluginId":"builtin.rotating-triangle","typeId":"rotating-triangle","placement":{"column":2,"row":0,"columnSpan":2,"rowSpan":2},"private":{}}]}]}})json";

constexpr std::string_view kTwoMatrixDocument =
    R"json({"$schema":"RedXe.settings.schema.json","schemaVersion":3,"plugins":[{"id":"builtin.matrix-rain","enabled":true,"private":{}}],"dashboard":{"grid":{"columns":4,"rows":2},"activePageId":"page.one","pages":[{"id":"page.one","name":"One","widgets":[{"id":"matrix.one","pluginId":"builtin.matrix-rain","typeId":"matrix-rain","placement":{"column":0,"row":0,"columnSpan":2,"rowSpan":2},"private":{"seed":1999,"glyphHeightDips":18,"densityPercent":70,"speedPercent":100,"trailLengthGlyphs":18,"mutationPerSecond":8,"headColor":"#D8FFE5","trailColor":"#00E65C","backgroundColor":"#010502","glowPercent":35}},{"id":"matrix.two","pluginId":"builtin.matrix-rain","typeId":"matrix-rain","placement":{"column":2,"row":0,"columnSpan":2,"rowSpan":2},"private":{"seed":2000,"glyphHeightDips":18,"densityPercent":70,"speedPercent":100,"trailLengthGlyphs":18,"mutationPerSecond":8,"headColor":"#D8FFE5","trailColor":"#00E65C","backgroundColor":"#010502","glowPercent":35}}]}]}})json";

constexpr std::string_view kEmptyRegistryDocument =
    R"json({"$schema":"RedXe.settings.schema.json","schemaVersion":3,"plugins":[],"dashboard":{"grid":{"columns":4,"rows":2},"activePageId":"page.one","pages":[]}})json";

constexpr std::string_view kEmptyWidgetsDocument =
    R"json({"$schema":"RedXe.settings.schema.json","schemaVersion":3,"plugins":[{"id":"builtin.rotating-triangle","enabled":true,"private":{}}],"dashboard":{"grid":{"columns":4,"rows":2},"activePageId":"page.one","pages":[{"id":"page.one","name":"One","widgets":[]}]}})json";

[[nodiscard]] HRESULT GetDeployedSettingsPath(const wchar_t* fileName, std::filesystem::path& path) noexcept
{
    try
    {
        std::array<wchar_t, 32768> executable{};
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size() - 1)
        {
            return length == 0 ? HRESULT_FROM_WIN32(GetLastError()) : HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
        path = std::filesystem::path(executable.data()).parent_path() / L"Settings" / fileName;
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT ReadBytes(const std::filesystem::path& path, std::string& bytes) noexcept
{
    try
    {
        wil::unique_hfile file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (!file)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0 || size.QuadPart > 1024 * 1024)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        if (!bytes.empty() && (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ||
                               read != bytes.size()))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] bool HasArrayBounds(yyjson_val* object, const char* key, std::uint64_t minimum,
                                  std::uint64_t maximum) noexcept
{
    yyjson_val* array = yyjson_is_obj(object) ? yyjson_obj_get(object, key) : nullptr;
    yyjson_val* schemaMinimum = yyjson_is_obj(array) ? yyjson_obj_get(array, "minItems") : nullptr;
    yyjson_val* schemaMaximum = yyjson_is_obj(array) ? yyjson_obj_get(array, "maxItems") : nullptr;
    return yyjson_is_uint(schemaMinimum) && yyjson_get_uint(schemaMinimum) == minimum &&
           yyjson_is_uint(schemaMaximum) && yyjson_get_uint(schemaMaximum) == maximum;
}

[[nodiscard]] HRESULT ValidateTemplatesAndSchema() noexcept
{
    std::filesystem::path debugPath;
    std::filesystem::path releasePath;
    HRESULT result = GetDeployedSettingsPath(kRedXeDebugSettingsFileName, debugPath);
    if (SUCCEEDED(result))
    {
        result = GetDeployedSettingsPath(kRedXeReleaseSettingsFileName, releasePath);
    }
    if (FAILED(result))
    {
        return result;
    }

    AppSettings debug{};
    AppSettings release{};
    result = LoadAppSettingsFile(debugPath.wstring(), debug);
    if (SUCCEEDED(result))
    {
        result = LoadAppSettingsFile(releasePath.wstring(), release);
    }
    const DashboardPageSettings* debugActive = FindActiveDashboardPage(debug);
    const DashboardPageSettings* releaseActive = FindActiveDashboardPage(release);
    const PluginSettings* releaseMatrix = FindPluginSettings(release, "BUILTIN.MATRIX-RAIN");
    if (FAILED(result) || debug.pluginCount != 3 || debug.dashboard.gridColumns != 32 ||
        debug.dashboard.gridRows != 9 || debug.dashboard.pageCount != 2 || !debugActive ||
        debugActive->widgetCount != 4 || release.pluginCount != 3 || release.dashboard.pageCount != 2 ||
        !releaseActive || releaseActive->widgetCount != 1 || !releaseMatrix || !releaseMatrix->enabled ||
        !SettingsIdEquals(releaseActive->widgets[0].pluginId.View(), "builtin.matrix-rain") ||
        FAILED(ValidateAppSettings(debug)) || FAILED(ValidateAppSettings(release)))
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    std::array<char, kFactoryConfigurationCapacity> envelope{};
    std::uint32_t envelopeBytes = 0;
    result = SerializeFactoryConfigurationJson(*releaseMatrix, releaseActive->widgets[0], envelope, envelopeBytes);
    yyjson_read_err envelopeError{};
    unique_yyjson_doc envelopeDocument{
        yyjson_read_opts(envelope.data(), envelopeBytes, YYJSON_READ_NOFLAG, nullptr, &envelopeError)};
    yyjson_val* envelopeRoot = envelopeDocument ? yyjson_doc_get_root(envelopeDocument.get()) : nullptr;
    yyjson_val* pluginPrivate = yyjson_is_obj(envelopeRoot) ? yyjson_obj_get(envelopeRoot, "plugin") : nullptr;
    yyjson_val* instancePrivate = yyjson_is_obj(envelopeRoot) ? yyjson_obj_get(envelopeRoot, "instance") : nullptr;
    if (FAILED(result) || envelopeBytes == 0 || envelope[envelopeBytes] != '\0' || !yyjson_is_obj(pluginPrivate) ||
        yyjson_obj_size(pluginPrivate) != 0 || !yyjson_is_obj(instancePrivate) ||
        yyjson_get_uint(yyjson_obj_get(instancePrivate, "seed")) != 1999)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    std::filesystem::path schemaPath;
    result = GetDeployedSettingsPath(kRedXeSettingsSchemaFileName, schemaPath);
    std::string schemaBytes;
    if (SUCCEEDED(result))
    {
        result = ReadBytes(schemaPath, schemaBytes);
    }
    yyjson_read_err schemaError{};
    unique_yyjson_doc schema{SUCCEEDED(result) ? yyjson_read_opts(schemaBytes.data(), schemaBytes.size(),
                                                                  YYJSON_READ_NOFLAG, nullptr, &schemaError)
                                               : nullptr};
    yyjson_val* root = schema ? yyjson_doc_get_root(schema.get()) : nullptr;
    yyjson_val* properties = yyjson_is_obj(root) ? yyjson_obj_get(root, "properties") : nullptr;
    yyjson_val* version = yyjson_is_obj(properties) ? yyjson_obj_get(properties, "schemaVersion") : nullptr;
    yyjson_val* definitions = yyjson_is_obj(root) ? yyjson_obj_get(root, "$defs") : nullptr;
    yyjson_val* dashboard = yyjson_is_obj(definitions) ? yyjson_obj_get(definitions, "dashboard") : nullptr;
    yyjson_val* dashboardProperties = yyjson_is_obj(dashboard) ? yyjson_obj_get(dashboard, "properties") : nullptr;
    yyjson_val* page = yyjson_is_obj(definitions) ? yyjson_obj_get(definitions, "page") : nullptr;
    yyjson_val* pageProperties = yyjson_is_obj(page) ? yyjson_obj_get(page, "properties") : nullptr;
    yyjson_val* grid = yyjson_is_obj(definitions) ? yyjson_obj_get(definitions, "grid") : nullptr;
    yyjson_val* gridProperties = yyjson_is_obj(grid) ? yyjson_obj_get(grid, "properties") : nullptr;
    yyjson_val* columns = yyjson_is_obj(gridProperties) ? yyjson_obj_get(gridProperties, "columns") : nullptr;
    yyjson_val* columnMaximum = yyjson_is_obj(columns) ? yyjson_obj_get(columns, "maximum") : nullptr;
    yyjson_val* versionConstant = yyjson_is_obj(version) ? yyjson_obj_get(version, "const") : nullptr;
    if (FAILED(result) || !yyjson_is_uint(versionConstant) || yyjson_get_uint(versionConstant) != 3 ||
        !HasArrayBounds(properties, "plugins", 1, kMaximumSettingsPlugins) ||
        !HasArrayBounds(dashboardProperties, "pages", 1, kMaximumDashboardPages) ||
        !HasArrayBounds(pageProperties, "widgets", 1, kMaximumWidgetsPerPage) || !yyjson_is_uint(columnMaximum) ||
        yyjson_get_uint(columnMaximum) != kMaximumDashboardGridDimension ||
        !yyjson_is_obj(yyjson_obj_get(definitions, "plugin")) ||
        !yyjson_is_obj(yyjson_obj_get(definitions, "widget")) ||
        !yyjson_is_obj(yyjson_obj_get(definitions, "matrixPrivate")))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

[[nodiscard]] bool ReplaceOnce(std::string& text, std::string_view from, std::string_view to) noexcept
{
    try
    {
        const std::size_t position = text.find(from);
        if (position == std::string::npos)
        {
            return false;
        }
        text.replace(position, from.size(), to);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

[[nodiscard]] HRESULT ExpectRejected(std::string_view document, const AppSettings& baseline) noexcept
{
    AppSettings candidate = baseline;
    const HRESULT result = ParseAppSettingsJson(document, candidate);
    return FAILED(result) && candidate == baseline ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

[[nodiscard]] HRESULT ExpectRejectedVariant(std::string_view from, std::string_view to,
                                            const AppSettings& baseline) noexcept
{
    try
    {
        std::string document(kMinimalDocument);
        return ReplaceOnce(document, from, to) ? ExpectRejected(document, baseline)
                                               : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT ValidateParserAndSemantics() noexcept
{
    AppSettings parsed{};
    HRESULT result = ParseAppSettingsJson(kMinimalDocument, parsed);
    const DashboardPageSettings* active = FindActiveDashboardPage(parsed);
    if (FAILED(result) || parsed.pluginCount != 3 || parsed.dashboard.pageCount != 2 || !active ||
        active->widgetCount != 1 || !SettingsIdEquals(active->widgets[0].id.View(), "TRIANGLE.ONE"))
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    const std::array<std::pair<std::string_view, std::string_view>, 26> variants{{
        {"\"schemaVersion\":3", "\"schemaVersion\":2"},
        {"\"schemaVersion\":3", "\"schemaVersion\":\"3\""},
        {"\"plugins\":[", "\"unknown\":1,\"plugins\":["},
        {"\"id\":\"builtin.gdi-orbit\"", "\"id\":\"BUILTIN.ROTATING-TRIANGLE\""},
        {"\"enabled\":true,\"private\":{}", "\"enabled\":true,\"private\":[]"},
        {"\"enabled\":true,\"private\":{}", "\"enabled\":true,\"private\":{\"unexpected\":1}"},
        {"\"activePageId\":\"page.one\"", "\"activePageId\":\"page.missing\""},
        {"\"id\":\"page.two\"", "\"id\":\"PAGE.ONE\""},
        {"\"name\":\"One\",", ""},
        {"\"id\":\"matrix.two\"", "\"id\":\"TRIANGLE.ONE\""},
        {"\"id\":\"builtin.rotating-triangle\",\"enabled\":true",
         "\"id\":\"builtin.rotating-triangle\",\"enabled\":false"},
        {"\"pluginId\":\"builtin.rotating-triangle\"", "\"pluginId\":\"missing.plugin\""},
        {"\"typeId\":\"rotating-triangle\"", "\"typeId\":\"matrix-rain\""},
        {"\"column\":0,\"row\":0,\"columnSpan\":2", "\"column\":3,\"row\":0,\"columnSpan\":2"},
        {"\"rowSpan\":2", "\"rowSpan\":0"},
        {"\"private\":{}}]},{\"id\":\"page.two\"", "\"private\":{},\"unknown\":1}]},{\"id\":\"page.two\""},
        {"\"seed\":1999", "\"seed\":4294967296"},
        {"\"glyphHeightDips\":18", "\"glyphHeightDips\":11"},
        {"\"densityPercent\":70", "\"densityPercent\":101"},
        {"\"speedPercent\":100", "\"speedPercent\":24"},
        {"\"trailLengthGlyphs\":18", "\"trailLengthGlyphs\":49"},
        {"\"mutationPerSecond\":8", "\"mutationPerSecond\":31"},
        {"\"headColor\":\"#D8FFE5\"", "\"headColor\":\"D8FFE5\""},
        {"\"trailColor\":\"#00E65C\"", "\"trailColor\":\"#00E65G\""},
        {"\"backgroundColor\":\"#010502\"", "\"backgroundColor\":\"#01050\""},
        {"\"glowPercent\":35", "\"glowPercent\":101"},
    }};
    for (const auto& [from, to] : variants)
    {
        result = ExpectRejectedVariant(from, to, parsed);
        if (FAILED(result))
        {
            return result;
        }
    }
    for (const std::string_view document : {std::string_view{"{}"}, kEmptyRegistryDocument, kEmptyWidgetsDocument,
                                            kOverlappingDocument, kTwoMatrixDocument})
    {
        result = ExpectRejected(document, parsed);
        if (FAILED(result))
        {
            return result;
        }
    }

    try
    {
        std::string duplicateKey(kMinimalDocument);
        if (!ReplaceOnce(duplicateKey, "\"schemaVersion\":3", "\"schemaVersion\":3,\"schemaVersion\":3") ||
            FAILED(ExpectRejected(duplicateKey, parsed)))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    catch (...)
    {
        return E_FAIL;
    }

    try
    {
        std::string oversized(kMinimalDocument);
        std::string privateObject = "\"private\":{\"pad\":\"";
        privateObject.append(kPrivateConfigurationCapacity, 'x');
        privateObject.append("\"}");
        if (!ReplaceOnce(oversized, "\"private\":{}", privateObject) || FAILED(ExpectRejected(oversized, parsed)))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    catch (...)
    {
        return E_FAIL;
    }

    std::unique_ptr<AppSettings> variant{new (std::nothrow) AppSettings{parsed}};
    if (!variant)
    {
        return E_OUTOFMEMORY;
    }
    variant->dashboard.pages[0].widgets[0].placement.columnSpan = variant->dashboard.gridColumns + 1;
    if (SUCCEEDED(ValidateAppSettings(*variant)))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    JsonObjectSettings privateSettings{};
    result = SetJsonObjectSettings(kMatrixPrivate, privateSettings);
    const JsonObjectSettings beforeInvalid = privateSettings;
    if (FAILED(result) || privateSettings.View().find(' ') != std::string_view::npos ||
        SUCCEEDED(SetJsonObjectSettings("[]", privateSettings)) || privateSettings != beforeInvalid)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    *variant = parsed;
    variant->dashboard.pages[1].name = variant->dashboard.pages[0].name;
    if (*variant == parsed || !ActiveDashboardRuntimeEquals(parsed, *variant))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    *variant = parsed;
    ++variant->dashboard.pages[0].widgets[0].placement.column;
    if (ActiveDashboardRuntimeEquals(parsed, *variant))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

struct LowStackParseContext final
{
    HRESULT result = E_FAIL;
};

DWORD WINAPI ParseSettingsOnLowStack(void* rawContext) noexcept
{
    auto* context = static_cast<LowStackParseContext*>(rawContext);
    std::unique_ptr<AppSettings> settings{new (std::nothrow) AppSettings{}};
    context->result = settings ? ParseAppSettingsJson(kMinimalDocument, *settings) : E_OUTOFMEMORY;
    return 0;
}

[[nodiscard]] HRESULT ValidateLowStackParsing() noexcept
{
    constexpr SIZE_T kStackReserveBytes = 128U * 1024U;
    std::unique_ptr<LowStackParseContext> context{new (std::nothrow) LowStackParseContext{}};
    if (!context)
    {
        return E_OUTOFMEMORY;
    }
    wil::unique_handle thread{CreateThread(nullptr, kStackReserveBytes, &ParseSettingsOnLowStack, context.get(),
                                           STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr)};
    if (!thread)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const DWORD waitResult = WaitForSingleObject(thread.get(), 5000);
    if (waitResult == WAIT_OBJECT_0)
    {
        return context->result;
    }
    // The worker may still reference the context on a timeout/failure path. Preserve it until process exit.
    (void)context.release();
    if (waitResult == WAIT_TIMEOUT)
    {
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    return HRESULT_FROM_WIN32(GetLastError());
}

[[nodiscard]] bool WaitForSettingsMessage(HWND window, DWORD timeoutMilliseconds) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + timeoutMilliseconds;
    for (;;)
    {
        MSG message{};
        if (PeekMessageW(&message, window, SettingsWatcher::kSettingsChangedMessage,
                         SettingsWatcher::kSettingsChangedMessage, PM_REMOVE))
        {
            return true;
        }
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
        {
            return false;
        }
        const DWORD remaining = static_cast<DWORD>(deadline - now);
        if (MsgWaitForMultipleObjectsEx(0, nullptr, remaining, QS_POSTMESSAGE, MWMO_INPUTAVAILABLE) == WAIT_TIMEOUT)
        {
            return false;
        }
    }
}

[[nodiscard]] HRESULT WriteTextFile(const std::filesystem::path& path, std::string_view text) noexcept
{
    wil::unique_hfile file{CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    DWORD written = 0;
    if (!WriteFile(file.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
        written != text.size() || !FlushFileBuffers(file.get()))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

[[nodiscard]] HRESULT ValidateWatcherAndStamps() noexcept
{
    try
    {
        std::array<wchar_t, MAX_PATH> temporaryRoot{};
        const DWORD rootLength = GetTempPathW(static_cast<DWORD>(temporaryRoot.size()), temporaryRoot.data());
        if (rootLength == 0 || rootLength >= temporaryRoot.size())
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        std::filesystem::path directory = temporaryRoot.data();
        directory /=
            L"RedXe.SettingsTests." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
        if (!CreateDirectoryW(directory.c_str(), nullptr))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        const std::filesystem::path settingsPath = directory / L"watched.json";
        const std::filesystem::path replacementPath = directory / L"replacement.json";
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                DeleteFileW(settingsPath.c_str());
                DeleteFileW(replacementPath.c_str());
                RemoveDirectoryW(directory.c_str());
            });

        wil::unique_hwnd window{
            CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr)};
        if (!window)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        SettingsWatcher watcher;
        HRESULT result = watcher.Start(window.get(), directory.wstring());
        if (FAILED(result) || !WaitForSettingsMessage(window.get(), 5000))
        {
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        watcher.AcknowledgeNotification();

        result = WriteTextFile(settingsPath, "one");
        if (FAILED(result) || !WaitForSettingsMessage(window.get(), 5000))
        {
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        watcher.AcknowledgeNotification();

        SettingsFileStamp first{};
        if (QuerySettingsFileStamp(settingsPath.wstring(), first) != S_OK)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        result = WriteTextFile(replacementPath, "two");
        if (FAILED(result) || !MoveFileExW(replacementPath.c_str(), settingsPath.c_str(),
                                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            return FAILED(result) ? result : HRESULT_FROM_WIN32(GetLastError());
        }
        SettingsFileStamp second{};
        if (QuerySettingsFileStamp(settingsPath.wstring(), second) != S_OK || first == second)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        watcher.Stop();
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}
} // namespace

int wmain()
{
    for (const HRESULT result : {ValidateTemplatesAndSchema(), ValidateParserAndSemantics(), ValidateLowStackParsing(),
                                 ValidateWatcherAndStamps()})
    {
        if (FAILED(result))
        {
            return static_cast<int>(result & 0xFF);
        }
    }
    return 0;
}
