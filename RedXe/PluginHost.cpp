#include "PluginHost.h"

#include "PlugInterfaces/FactoryImpl.h"
#include "Settings.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <new>
#include <shlobj.h>
#include <strsafe.h>
#include <utility>

namespace
{
constexpr size_t kInitialPathCapacity = 512;
constexpr size_t kMaximumPathCapacity = 32768;

template <typename Function> [[nodiscard]] Function ResolveFunction(HMODULE module, const char* name) noexcept
{
    const FARPROC procedure = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(procedure));
    std::memcpy(&function, &procedure, sizeof(function));
    return function;
}

[[nodiscard]] size_t FindBundledPluginIndex(const char* pluginId) noexcept
{
    if (!RedXeIsValidMachineId(pluginId))
    {
        return kRedXeBundledPlugins.size();
    }
    for (size_t index = 0; index < kRedXeBundledPlugins.size(); ++index)
    {
        const RedXeBundledPluginSpec& candidate = kRedXeBundledPlugins[index];
        if (RedXeAsciiEqualsIgnoreCase(candidate.pluginId, pluginId))
        {
            return index;
        }
    }
    return kRedXeBundledPlugins.size();
}

[[nodiscard]] HRESULT BuildPluginPath(const wchar_t* moduleName, wchar_t* path, size_t capacity) noexcept
{
    if (!moduleName || moduleName[0] == L'\0' || !path || capacity == 0 || capacity > static_cast<size_t>(MAXDWORD))
    {
        return E_INVALIDARG;
    }

    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(capacity));
    if (length == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (length >= capacity - 1)
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    wchar_t* separator = std::wcsrchr(path, L'\\');
    if (!separator)
    {
        return HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
    }
    const size_t prefixLength = static_cast<size_t>(separator - path) + 1;
    HRESULT result = StringCchCopyW(separator + 1, capacity - prefixLength, L"Plugins\\");
    if (FAILED(result))
    {
        return result;
    }
    return StringCchCatW(separator + 1, capacity - prefixLength, moduleName);
}

[[nodiscard]] const char* LogLevelName(uint32_t level) noexcept
{
    switch (level)
    {
    case RedXeLogLevelError:
        return "error";
    case RedXeLogLevelWarning:
        return "warning";
    case RedXeLogLevelInfo:
        return "info";
    case RedXeLogLevelDebug:
        return "debug";
    default:
        return nullptr;
    }
}

[[nodiscard]] bool IsValidLogEventId(const char* eventId) noexcept
{
    if (!eventId || eventId[0] == '\0')
    {
        return false;
    }
    uint32_t length = 0;
    while (eventId[length] != '\0')
    {
        const unsigned char character = static_cast<unsigned char>(eventId[length]);
        if (length >= kRedXeMaximumLogEventBytes)
        {
            return false;
        }
        const bool allowed = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                             (character >= '0' && character <= '9') || character == '-' || character == '.';
        if (!allowed)
        {
            return false;
        }
        ++length;
    }
    return true;
}

size_t AppendLogText(char* destination, size_t capacity, size_t used, const char* text) noexcept
{
    if (!text)
    {
        return used;
    }
    while (*text != '\0' && used + 1 < capacity)
    {
        destination[used++] = *text++;
    }
    if (used < capacity)
    {
        destination[used] = '\0';
    }
    return used;
}

size_t AppendLogEscaped(char* destination, size_t capacity, size_t used, const char* text,
                        uint32_t maxCharacters) noexcept
{
    uint32_t copied = 0;
    while (text && copied < maxCharacters && *text != '\0')
    {
        const unsigned char character = static_cast<unsigned char>(*text);
        if (character == '"' || character == '\\')
        {
            if (used + 2 >= capacity)
            {
                break;
            }
            destination[used++] = '\\';
            destination[used++] = static_cast<char>(character);
            ++text;
            ++copied;
            continue;
        }
        if (character < 0x20)
        {
            if (used + 6 >= capacity)
            {
                break;
            }
            destination[used++] = '\\';
            destination[used++] = 'u';
            destination[used++] = '0';
            destination[used++] = '0';
            constexpr char kHex[] = "0123456789abcdef";
            destination[used++] = kHex[(character >> 4) & 0x0f];
            destination[used++] = kHex[character & 0x0f];
            ++text;
            ++copied;
            continue;
        }
        // Copy complete UTF-8 code points, including at the input and escaped-output limits. Invalid input is
        // replaced with one ASCII '?' so a diagnostic can never corrupt the JSONL stream.
        uint32_t bytes = character < 0x80                         ? 1U
                         : character >= 0xC2 && character <= 0xDF ? 2U
                         : character >= 0xE0 && character <= 0xEF ? 3U
                         : character >= 0xF0 && character <= 0xF4 ? 4U
                                                                  : 0U;
        if (bytes > maxCharacters - copied)
        {
            break;
        }
        bool valid = bytes != 0;
        for (uint32_t index = 1; valid && index < bytes; ++index)
        {
            const unsigned char next = static_cast<unsigned char>(text[index]);
            valid = next >= 0x80 && next <= 0xBF;
            if (index == 1)
            {
                valid = valid && !(character == 0xE0 && next < 0xA0) && !(character == 0xED && next >= 0xA0) &&
                        !(character == 0xF0 && next < 0x90) && !(character == 0xF4 && next >= 0x90);
            }
        }
        if (!valid)
        {
            if (used + 1 >= capacity)
                break;
            destination[used++] = '?';
            ++text;
            ++copied;
            continue;
        }
        if (used + bytes >= capacity)
        {
            break;
        }
        std::memcpy(destination + used, text, bytes);
        used += bytes;
        copied += bytes;
        text += bytes;
    }
    if (used < capacity)
    {
        destination[used] = '\0';
    }
    return used;
}

[[nodiscard]] uint32_t FormatLogLine(const RedXeLogRecord& record, char* destination, size_t capacity) noexcept
{
    if (!destination || capacity < 512)
    {
        return 0;
    }
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    char timestamp[32]{};
    if (sprintf_s(timestamp, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", utc.wYear, utc.wMonth, utc.wDay, utc.wHour,
                  utc.wMinute, utc.wSecond, utc.wMilliseconds) < 0)
    {
        return 0;
    }
    const char* level = LogLevelName(record.level);
    if (!level)
    {
        return 0;
    }

    size_t used = 0;
    used = AppendLogText(destination, capacity, used, "{\"ts\":\"");
    used = AppendLogText(destination, capacity, used, timestamp);
    used = AppendLogText(destination, capacity, used, "\",\"level\":\"");
    used = AppendLogText(destination, capacity, used, level);
    used = AppendLogText(destination, capacity, used, "\"");
    if (record.pluginId && record.pluginId[0] != '\0')
    {
        used = AppendLogText(destination, capacity, used, ",\"plugin\":\"");
        used = AppendLogEscaped(destination, std::min(capacity, used + 129), used, record.pluginId, 128);
        used = AppendLogText(destination, capacity, used, "\"");
    }
    if (record.instanceId && record.instanceId[0] != '\0')
    {
        used = AppendLogText(destination, capacity, used, ",\"instance\":\"");
        used = AppendLogEscaped(destination, std::min(capacity, used + 129), used, record.instanceId, 128);
        used = AppendLogText(destination, capacity, used, "\"");
    }
    used = AppendLogText(destination, capacity, used, ",\"event\":\"");
    used = AppendLogEscaped(destination, capacity, used, record.eventId, kRedXeMaximumLogEventBytes);
    used = AppendLogText(destination, capacity, used, "\",\"message\":\"");
    // Reserve the closing quote, optional HRESULT, closing brace, newline, and terminator before truncating text.
    used = AppendLogEscaped(destination, capacity - 32, used, record.messageUtf8, kRedXeMaximumLogMessageBytes);
    used = AppendLogText(destination, capacity, used, "\"");
    if (record.code != S_OK)
    {
        char code[16]{};
        if (sprintf_s(code, "0x%08X", static_cast<unsigned int>(record.code)) >= 0)
        {
            used = AppendLogText(destination, capacity, used, ",\"hr\":\"");
            used = AppendLogText(destination, capacity, used, code);
            used = AppendLogText(destination, capacity, used, "\"");
        }
    }
    used = AppendLogText(destination, capacity, used, "}\n");
    return used > 1 && used < capacity ? static_cast<uint32_t>(used) : 0;
}

[[nodiscard]] HRESULT EnsureLogDirectory(const wchar_t* directory) noexcept
{
    const int result = SHCreateDirectoryExW(nullptr, directory, nullptr);
    if (result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS)
    {
        return S_OK;
    }
    return HRESULT_FROM_WIN32(static_cast<DWORD>(result));
}

[[nodiscard]] HRESULT ValidateMetadata(const RedXePluginMetadata* metadata, uint32_t count,
                                       const char* expectedPluginId, uint32_t& capabilities) noexcept
{
    capabilities = RedXePluginCapabilityNone;
    if (!metadata || count == 0 || count > 256 || !RedXeIsValidMachineId(expectedPluginId))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    bool selected = false;
    for (uint32_t index = 0; index < count; ++index)
    {
        const RedXePluginMetadata& candidate = metadata[index];
        if (candidate.sizeBytes != sizeof(RedXePluginMetadata) || !RedXeIsValidMachineId(candidate.id) ||
            !candidate.displayName || !candidate.description || !candidate.author || !candidate.version ||
            (candidate.capabilities & ~(RedXePluginCapabilityWidgetProvider | RedXePluginCapabilityDataSource)) != 0)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        for (uint32_t previous = 0; previous < index; ++previous)
        {
            if (RedXeAsciiEqualsIgnoreCase(metadata[previous].id, candidate.id))
            {
                return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
            }
        }
        if (RedXeAsciiEqualsIgnoreCase(candidate.id, expectedPluginId))
        {
            capabilities = candidate.capabilities;
            selected = true;
        }
    }
    return selected ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

[[nodiscard]] int CompareDueDataSetIds(const char* left, const char* right) noexcept
{
    const bool leftStatus = left != nullptr && RedXeAsciiEqualsIgnoreCase(left, "source.status");
    const bool rightStatus = right != nullptr && RedXeAsciiEqualsIgnoreCase(right, "source.status");
    if (leftStatus != rightStatus)
    {
        return leftStatus ? 1 : -1;
    }
    if (!left)
    {
        return right ? -1 : 0;
    }
    if (!right)
    {
        return 1;
    }
    while (*left != '\0' && *right != '\0')
    {
        unsigned char leftValue = static_cast<unsigned char>(*left);
        unsigned char rightValue = static_cast<unsigned char>(*right);
        if (leftValue >= 'A' && leftValue <= 'Z')
        {
            leftValue = static_cast<unsigned char>(leftValue - 'A' + 'a');
        }
        if (rightValue >= 'A' && rightValue <= 'Z')
        {
            rightValue = static_cast<unsigned char>(rightValue - 'A' + 'a');
        }
        if (leftValue != rightValue)
        {
            return leftValue < rightValue ? -1 : 1;
        }
        ++left;
        ++right;
    }
    if (*left == *right)
    {
        return 0;
    }
    return *left == '\0' ? -1 : 1;
}

void SortDueDataSets(std::array<const char*, RedXeDataCollectMaximumDataSets>& ids,
                     std::array<uint32_t, RedXeDataCollectMaximumDataSets>& indices, uint32_t count) noexcept
{
    for (uint32_t index = 1; index < count; ++index)
    {
        const char* id = ids[index];
        const uint32_t dataSetIndex = indices[index];
        uint32_t insert = index;
        while (insert > 0 && CompareDueDataSetIds(ids[insert - 1], id) > 0)
        {
            ids[insert] = ids[insert - 1];
            indices[insert] = indices[insert - 1];
            --insert;
        }
        ids[insert] = id;
        indices[insert] = dataSetIndex;
    }
}

[[nodiscard]] bool CollectResultMatchesRequest(const RedXeDataCollectRequest& request,
                                               const RedXeDataCollectResult* result) noexcept
{
    if (!result || result->sizeBytes != sizeof(RedXeDataCollectResult) ||
        result->snapshotCount != request.dataSetCount || !result->snapshots)
    {
        return false;
    }
    for (uint32_t index = 0; index < request.dataSetCount; ++index)
    {
        const RedXeDataSnapshot* snapshot = result->snapshots[index];
        if (!snapshot || snapshot->sizeBytes != sizeof(RedXeDataSnapshot) || !snapshot->dataSetId ||
            !RedXeAsciiEqualsIgnoreCase(snapshot->dataSetId, request.dataSetIds[index]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] DWORD WaitMilliseconds(uint64_t now, uint64_t due) noexcept
{
    if (due <= now)
    {
        return 0;
    }
    return static_cast<DWORD>(std::min<uint64_t>(due - now, MAXDWORD - 1ULL));
}
} // namespace

class PluginHost::DataProvider final : public RedXeComObject<PluginHost::DataProvider, IRedXeDataProvider>
{
  public:
    DataProvider(PluginHost& host, size_t providerIndex) noexcept : _host(&host), _providerIndex(providerIndex) {}

    HRESULT STDMETHODCALLTYPE GetDataSets(const RedXeDataSetDescriptor** descriptors, uint32_t* count) noexcept override
    {
        return _host ? _host->GetDataSets(_providerIndex, descriptors, count) : E_UNEXPECTED;
    }

    HRESULT STDMETHODCALLTYPE Subscribe(const RedXeDataSubscriptionOptions* options, IRedXeDataSink* sink,
                                        IRedXeDataSubscription** subscription) noexcept override
    {
        return _host ? _host->Subscribe(_providerIndex, options, sink, subscription) : E_UNEXPECTED;
    }

  private:
    PluginHost* _host;
    size_t _providerIndex;
};

class PluginHost::Subscription final : public RedXeComObject<PluginHost::Subscription, IRedXeDataSubscription>
{
  public:
    Subscription(PluginHost& host, size_t index, uint64_t token) noexcept : _host(&host), _index(index), _token(token)
    {
    }

    ~Subscription()
    {
        if (_host)
        {
            _host->RemoveSubscription(_index, _token);
        }
    }

    HRESULT STDMETHODCALLTYPE SetActive(BOOL active) noexcept override
    {
        return _host ? _host->SetSubscriptionActive(_index, _token, active != FALSE) : E_UNEXPECTED;
    }

  private:
    PluginHost* _host;
    size_t _index;
    uint64_t _token;
};

PluginHost::~PluginHost()
{
    Shutdown();
}

PluginHost& PluginHost::Instance() noexcept
{
    static PluginHost instance;
    return instance;
}

void PluginHost::ShutdownProcessRuntime() noexcept
{
    Instance().Shutdown();
}

void PluginHost::Shutdown() noexcept
{
    SetUiInvalidateTarget(nullptr);
    _controlWork.Stop();
    StopNetworkService();
    AcquireSRWLockExclusive(&_widgetStatusLock);
    for (WidgetStatusSlot& slot : _widgetStatus)
    {
        slot = WidgetStatusSlot{};
    }
    ReleaseSRWLockExclusive(&_widgetStatusLock);
    StopDataService();
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_pendingSettingsLock);
        for (auto& pending : _pendingSettings)
            pending.reset();
        _hasPendingSettings.store(false, std::memory_order_release);
    }
    ShutdownModules();
    StopLogService();
    _shutdown = true;
}

IRedXeHost* PluginHost::Interface() noexcept
{
    return static_cast<IRedXeHost*>(this);
}

// PluginHost is the process runtime, not a heap-owned object, so it does not use RedXeComObject: its reference count
// is advisory and Release never destroys it. The optional settings queue shares its controlling IUnknown.
// Plugins borrow IRedXeHost for the lifetime of the runtime and must not outlive it.
HRESULT PluginHost::QueryInterface(REFIID interfaceId, void** result) noexcept
{
    if (!result)
    {
        return E_POINTER;
    }
    *result = nullptr;
    if (interfaceId == __uuidof(IUnknown) || interfaceId == __uuidof(IRedXeHost))
    {
        *result = static_cast<IRedXeHost*>(this);
    }
    else if (interfaceId == __uuidof(IRedXeSettingsQueue))
        *result = static_cast<IRedXeSettingsQueue*>(this);
    else
    {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG PluginHost::AddRef() noexcept
{
    return ++_references;
}

ULONG PluginHost::Release() noexcept
{
    return --_references;
}

void PluginHost::SetSettingsPersistHandler(SettingsPersistHandler handler, void* context) noexcept
{
    _settingsPersistHandler = handler;
    _settingsPersistContext = context;
}

HRESULT PluginHost::PersistWidgetSettings(const char* instanceId, const char* settingsJsonUtf8,
                                          uint32_t settingsBytes) noexcept
{
    if (!instanceId || instanceId[0] == '\0' || !settingsJsonUtf8 || settingsBytes == 0 ||
        settingsBytes > kPrivateConfigurationCapacity)
    {
        return E_INVALIDARG;
    }
    if (!_settingsPersistHandler)
    {
        return E_UNEXPECTED;
    }
    // Apply an older queued discovery before a newer interactive edit, preserving unrelated members and order.
    std::unique_ptr<PendingSettings> earlier;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_pendingSettingsLock);
        for (auto& pending : _pendingSettings)
        {
            if (pending && RedXeAsciiEqualsIgnoreCase(pending->instanceId.data(), instanceId))
            {
                earlier = std::move(pending);
                break;
            }
        }
    }
    if (earlier)
    {
        const HRESULT result =
            _settingsPersistHandler(_settingsPersistContext, instanceId, earlier->json.data(), earlier->bytes);
        if (FAILED(result))
            (void)RedXeHostLog(Interface(), RedXeLogLevelWarning, "host", instanceId, "settings-queue-failed",
                               "Queued settings could not be committed before the interactive edit.", result);
    }
    return _settingsPersistHandler(_settingsPersistContext, instanceId, settingsJsonUtf8, settingsBytes);
}

HRESULT PluginHost::QueueWidgetSettings(const char* instanceId, const char* jsonUtf8, uint32_t bytes) noexcept
{
    if (!instanceId || !instanceId[0] || strnlen_s(instanceId, 128) >= 128 || !jsonUtf8 || bytes == 0 || bytes > 4096)
        return E_INVALIDARG;
    std::unique_ptr<PendingSettings> pending{new (std::nothrow) PendingSettings};
    if (!pending)
        return E_OUTOFMEMORY;
    strcpy_s(pending->instanceId.data(), pending->instanceId.size(), instanceId);
    std::memcpy(pending->json.data(), jsonUtf8, bytes);
    pending->bytes = bytes;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_pendingSettingsLock);
        size_t selected = _pendingSettings.size();
        for (size_t index = 0; index < _pendingSettings.size(); ++index)
        {
            if (_pendingSettings[index] &&
                RedXeAsciiEqualsIgnoreCase(_pendingSettings[index]->instanceId.data(), instanceId))
            {
                selected = index;
                break;
            }
            if (!_pendingSettings[index] && selected == _pendingSettings.size())
                selected = index;
        }
        if (selected == _pendingSettings.size())
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        _pendingSettings[selected] = std::move(pending);
        _hasPendingSettings.store(true, std::memory_order_release);
    }
    RequestUiInvalidate();
    return S_OK;
}

HRESULT PluginHost::Log(const RedXeLogRecord* record) noexcept
{
    if (!record)
    {
        return E_POINTER;
    }
    if (record->sizeBytes != sizeof(RedXeLogRecord) || !IsValidLogEventId(record->eventId) || !record->messageUtf8 ||
        record->messageUtf8[0] == '\0' || !LogLevelName(record->level))
    {
        return E_INVALIDARG;
    }
#if !defined(_DEBUG)
    if (record->level == RedXeLogLevelDebug)
    {
        return S_OK;
    }
#endif
    if (_shutdown)
    {
        return E_UNEXPECTED;
    }

    std::array<char, kLogLineCapacity> line{};
    const uint32_t bytes = FormatLogLine(*record, line.data(), line.size());
    if (bytes == 0)
    {
        return E_FAIL;
    }
    return EnqueueLogLine(line.data(), bytes);
}

HRESULT PluginHost::SetLogDirectory(const wchar_t* directory) noexcept
{
    if (!directory || directory[0] == L'\0')
    {
        return E_INVALIDARG;
    }
    if (_shutdown)
    {
        return E_UNEXPECTED;
    }

    _logDirectory = directory;
    _logFile.reset();
    _logFilePath.clear();

    HRESULT result = EnsureLogDirectory(_logDirectory.c_str());
    if (FAILED(result))
    {
        return result;
    }
    result = EnsureLogWorker();
    if (FAILED(result))
    {
        return result;
    }
    const RedXeLogRecord opened{
        sizeof(RedXeLogRecord), RedXeLogLevelInfo, nullptr, nullptr, "log-open", "host JSONL log opened.", S_OK};
    return Log(&opened);
}

HRESULT PluginHost::SetLogRetentionDays(uint32_t days) noexcept
{
    if (days < kRedXeMinimumLogRetentionDays || days > kRedXeMaximumLogRetentionDays)
    {
        return E_INVALIDARG;
    }
    if (_shutdown)
    {
        return E_UNEXPECTED;
    }
    _logRetentionDays.store(days, std::memory_order_release);
    if (_logWakeEvent)
    {
        SetEvent(_logWakeEvent.get());
    }
    return S_OK;
}

HRESULT PluginHost::FlushLog(uint32_t timeoutMilliseconds) noexcept
{
    if (!_logWorker.joinable())
    {
        return S_OK;
    }
    if (_logWakeEvent)
    {
        SetEvent(_logWakeEvent.get());
    }
    if (!_logIdleEvent)
    {
        return E_UNEXPECTED;
    }
    const DWORD wait = WaitForSingleObject(_logIdleEvent.get(), timeoutMilliseconds);
    if (wait == WAIT_OBJECT_0)
    {
        return _logQueued.load(std::memory_order_acquire) == 0 ? S_OK : HRESULT_FROM_WIN32(ERROR_IO_PENDING);
    }
    if (wait == WAIT_TIMEOUT)
    {
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    return HRESULT_FROM_WIN32(GetLastError());
}

HRESULT PluginHost::EnqueueLogLine(const char* line, uint32_t bytes) noexcept
{
    if (!line || bytes == 0 || bytes >= kLogLineCapacity)
    {
        return E_INVALIDARG;
    }
    if (_logDirectory.empty())
    {
        return S_OK;
    }

    AcquireSRWLockExclusive(&_logLock);
    if (_logCount == kLogRingSlots)
    {
        _logTail = (_logTail + 1) % kLogRingSlots;
        --_logCount;
        _logQueued.store(static_cast<uint32_t>(_logCount), std::memory_order_release);
    }
    LogLineSlot& slot = _logRing[_logHead];
    std::memcpy(slot.line.data(), line, bytes);
    slot.bytes = bytes;
    _logHead = (_logHead + 1) % kLogRingSlots;
    ++_logCount;
    _logQueued.store(static_cast<uint32_t>(_logCount), std::memory_order_release);
    if (_logIdleEvent)
    {
        ResetEvent(_logIdleEvent.get());
    }
    ReleaseSRWLockExclusive(&_logLock);
    if (_logWakeEvent)
    {
        SetEvent(_logWakeEvent.get());
    }
    return S_OK;
}

HRESULT PluginHost::EnsureCurrentLogFile() noexcept
{
    if (_logDirectory.empty())
    {
        return E_UNEXPECTED;
    }
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    wchar_t name[kRedXeLogFileNameCapacity]{};
    if (!RedXeFormatLogFileName(name, kRedXeLogFileNameCapacity, utc))
    {
        return E_FAIL;
    }
    std::wstring path = _logDirectory;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
    {
        path.push_back(L'\\');
    }
    path.append(name);
    if (_logFile && _logFilePath == path)
    {
        PurgeExpiredLogs();
        return S_OK;
    }
    _logFile.reset();
    _logFilePath = std::move(path);
    _logFile.reset(CreateFileW(_logFilePath.c_str(), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                               nullptr));
    if (!_logFile)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    PurgeExpiredLogs();
    return S_OK;
}

void PluginHost::PurgeExpiredLogs() noexcept
{
    if (_logDirectory.empty())
    {
        return;
    }
    const uint32_t retention = _logRetentionDays.load(std::memory_order_acquire);
    SYSTEMTIME today{};
    GetSystemTime(&today);

    auto purgeWildcard = [&](const wchar_t* wildcard) noexcept
    {
        std::wstring pattern = _logDirectory;
        if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/')
        {
            pattern.push_back(L'\\');
        }
        pattern.append(wildcard);
        WIN32_FIND_DATAW found{};
        const HANDLE handle = FindFirstFileW(pattern.c_str(), &found);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return;
        }
        do
        {
            if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                continue;
            }
            SYSTEMTIME fileDate{};
            const bool dated = RedXeTryParseLogFileDate(found.cFileName, fileDate);
            const bool legacy = RedXeIsLegacyLogFileName(found.cFileName);
            if (!dated && !legacy)
            {
                continue;
            }
            const bool expired = legacy || RedXeUtcDateDayDifference(fileDate, today) >= retention;
            if (!expired)
            {
                continue;
            }
            std::wstring path = _logDirectory;
            if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
            {
                path.push_back(L'\\');
            }
            path.append(found.cFileName);
            DeleteFileW(path.c_str());
        } while (FindNextFileW(handle, &found) != FALSE);
        FindClose(handle);
    };

    purgeWildcard(L"*.jsonl");
    purgeWildcard(L"*.jsonl.1");
}

HRESULT PluginHost::EnsureLogWorker() noexcept
{
    if (_logWorker.joinable())
    {
        return S_OK;
    }
    _logStopEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    _logWakeEvent.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    _logIdleEvent.reset(CreateEventW(nullptr, TRUE, TRUE, nullptr));
    if (!_logStopEvent || !_logWakeEvent || !_logIdleEvent)
    {
        const DWORD error = GetLastError();
        _logIdleEvent.reset();
        _logWakeEvent.reset();
        _logStopEvent.reset();
        return error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    try
    {
        _logWorker = std::jthread([this](std::stop_token) noexcept { LogWorker(); });
    }
    catch (const std::bad_alloc&)
    {
        _logIdleEvent.reset();
        _logWakeEvent.reset();
        _logStopEvent.reset();
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        _logIdleEvent.reset();
        _logWakeEvent.reset();
        _logStopEvent.reset();
        return E_FAIL;
    }
    return S_OK;
}

void PluginHost::LogWorker() noexcept
{
    HANDLE handles[] = {_logStopEvent.get(), _logWakeEvent.get()};
    for (;;)
    {
        const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        const bool stopping = waitResult == WAIT_OBJECT_0;
        if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_OBJECT_0 + 1)
        {
            return;
        }

        (void)EnsureCurrentLogFile();

        for (;;)
        {
            LogLineSlot slot{};
            AcquireSRWLockExclusive(&_logLock);
            if (_logCount == 0)
            {
                ReleaseSRWLockExclusive(&_logLock);
                break;
            }
            slot = _logRing[_logTail];
            _logTail = (_logTail + 1) % kLogRingSlots;
            --_logCount;
            _logQueued.store(static_cast<uint32_t>(_logCount), std::memory_order_release);
            ReleaseSRWLockExclusive(&_logLock);

            if (FAILED(EnsureCurrentLogFile()))
            {
                continue;
            }
            if (_logFile && slot.bytes > 0)
            {
                DWORD written = 0;
                (void)WriteFile(_logFile.get(), slot.line.data(), slot.bytes, &written, nullptr);
            }
        }
        if (_logFile)
        {
            (void)FlushFileBuffers(_logFile.get());
        }

        // Pair the idle transition with EnqueueLogLine's reset under the same lock. Otherwise a producer can
        // enqueue/reset between the empty check and SetEvent, making FlushLog observe a stale idle signal.
        AcquireSRWLockExclusive(&_logLock);
        if (_logIdleEvent && _logCount == 0)
        {
            SetEvent(_logIdleEvent.get());
        }
        ReleaseSRWLockExclusive(&_logLock);
        if (stopping)
        {
            return;
        }
    }
}

void PluginHost::StopLogService() noexcept
{
    if (_logStopEvent)
    {
        SetEvent(_logStopEvent.get());
    }
    if (_logWakeEvent)
    {
        SetEvent(_logWakeEvent.get());
    }
    if (_logWorker.joinable())
    {
        _logWorker.join();
    }
    _logWorker = std::jthread{};
    _logFile.reset();
    _logIdleEvent.reset();
    _logWakeEvent.reset();
    _logStopEvent.reset();
    AcquireSRWLockExclusive(&_logLock);
    _logHead = 0;
    _logTail = 0;
    _logCount = 0;
    _logQueued.store(0, std::memory_order_release);
    ReleaseSRWLockExclusive(&_logLock);
}

HRESULT PluginHost::LoadModule(const RedXeBundledPluginSpec& spec, ModuleSlot& slot) noexcept
{
    if (slot.module || slot.create || slot.enumerate || slot.getSettingsContract || slot.shutdown)
    {
        return E_UNEXPECTED;
    }

    std::array<wchar_t, kInitialPathCapacity> shortPath{};
    std::unique_ptr<wchar_t[]> longPath;
    wchar_t* pluginPath = shortPath.data();
    size_t pathCapacity = shortPath.size();
    HRESULT result = BuildPluginPath(spec.moduleName, pluginPath, pathCapacity);
    if (result == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
    {
        longPath.reset(new (std::nothrow) wchar_t[kMaximumPathCapacity]);
        if (!longPath)
        {
            return E_OUTOFMEMORY;
        }
        pluginPath = longPath.get();
        pathCapacity = kMaximumPathCapacity;
        result = BuildPluginPath(spec.moduleName, pluginPath, pathCapacity);
    }
    if (FAILED(result))
    {
        return result;
    }

    wil::unique_hmodule module{
        LoadLibraryExW(pluginPath, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    if (!module)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const RedXeCreateFn create = ResolveFunction<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    const RedXeEnumeratePluginsFn enumerate =
        ResolveFunction<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    if (!create || !enumerate)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    const RedXePluginMetadata* metadata = nullptr;
    uint32_t metadataCount = 0;
    result = enumerate(&metadata, &metadataCount);
    uint32_t capabilities = RedXePluginCapabilityNone;
    if (SUCCEEDED(result))
    {
        result = ValidateMetadata(metadata, metadataCount, spec.pluginId, capabilities);
    }
    if (FAILED(result))
    {
        return result;
    }

    slot.create = create;
    slot.enumerate = enumerate;
    slot.getSettingsContract =
        ResolveFunction<RedXeGetPluginSettingsContractFn>(module.get(), kRedXeGetPluginSettingsContractExport);
    slot.shutdown = ResolveFunction<RedXePluginShutdownFn>(module.get(), kRedXePluginShutdownExport);
    slot.capabilities = capabilities;
    slot.module = std::move(module);
    return S_OK;
}

HRESULT PluginHost::AttachSharedModule(const ModuleSlot& owner, const char* pluginId, ModuleSlot& slot) noexcept
{
    if (!owner.enumerate || !owner.create || slot.module || slot.create)
    {
        return E_UNEXPECTED;
    }

    const RedXePluginMetadata* metadata = nullptr;
    uint32_t metadataCount = 0;
    HRESULT result = owner.enumerate(&metadata, &metadataCount);
    uint32_t capabilities = RedXePluginCapabilityNone;
    if (SUCCEEDED(result))
    {
        result = ValidateMetadata(metadata, metadataCount, pluginId, capabilities);
    }
    if (FAILED(result))
    {
        return result;
    }

    slot.create = owner.create;
    slot.enumerate = owner.enumerate;
    slot.getSettingsContract = owner.getSettingsContract;
    slot.shutdown = owner.shutdown;
    slot.capabilities = capabilities;
    return S_OK;
}

HRESULT PluginHost::BindModule(const RedXeBundledPluginSpec& spec, ModuleSlot& slot) noexcept
{
    if (slot.create)
    {
        return S_OK;
    }
    for (size_t index = 0; index < kRedXeBundledPlugins.size(); ++index)
    {
        const ModuleSlot& candidate = _modules[index];
        if (!candidate.module || !candidate.create || !candidate.enumerate)
        {
            continue;
        }
        if (std::wcscmp(kRedXeBundledPlugins[index].moduleName, spec.moduleName) != 0)
        {
            continue;
        }
        return AttachSharedModule(candidate, spec.pluginId, slot);
    }
    return LoadModule(spec, slot);
}

void PluginHost::SetUiInvalidateTarget(HWND window) noexcept
{
    _uiWindow.store(window, std::memory_order_release);
    if (!window)
    {
        _pendingInvalidate.store(0, std::memory_order_release);
    }
}

void PluginHost::AcknowledgeUiInvalidate() noexcept
{
    _pendingInvalidate.store(0, std::memory_order_release);
    _controlWork.DrainCompletions();
    if (!_hasPendingSettings.load(std::memory_order_acquire))
        return;
    decltype(_pendingSettings) pending;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_pendingSettingsLock);
        pending.swap(_pendingSettings);
        _hasPendingSettings.store(false, std::memory_order_release);
    }
    for (const auto& item : pending)
    {
        if (!item)
            continue;
        // This batch was detached before newer worker submissions; never drain those ahead of the older batch.
        const HRESULT result = _settingsPersistHandler
                                   ? _settingsPersistHandler(_settingsPersistContext, item->instanceId.data(),
                                                             item->json.data(), item->bytes)
                                   : E_UNEXPECTED;
        if (FAILED(result))
            (void)RedXeHostLog(Interface(), RedXeLogLevelWarning, "host", item->instanceId.data(),
                               "settings-queue-failed", "Queued settings could not be committed.", result);
    }
}

HRESULT PluginHost::QueueControlWork(IRedXeControlWork* work) noexcept
{
    if (!work)
        return E_POINTER;
    if (!_controlAccessEnabled)
        return E_ACCESSDENIED;
    const HRESULT started = _controlWork.Start([](void* context) noexcept
                                               { static_cast<PluginHost*>(context)->RequestUiInvalidate(); }, this);
    return FAILED(started) ? started : _controlWork.Enqueue(work);
}

void PluginHost::RequestUiInvalidate() noexcept
{
    const HWND window = _uiWindow.load(std::memory_order_acquire);
    if (!window)
    {
        return;
    }
    if (_pendingInvalidate.exchange(1, std::memory_order_acq_rel) != 0)
    {
        return;
    }
    if (!PostMessageW(window, kDataSnapshotInvalidateMessage, 0, 0))
    {
        _pendingInvalidate.store(0, std::memory_order_release);
    }
}

HRESULT PluginHost::GetPluginModule(const char* pluginId, uint32_t requiredCapabilities, ModuleView* module) noexcept
{
    if (module)
    {
        *module = ModuleView{};
    }
    if (!module)
    {
        return E_POINTER;
    }
    if (_shutdown)
    {
        return E_UNEXPECTED;
    }
    const size_t pluginIndex = FindBundledPluginIndex(pluginId);
    if (pluginIndex >= kRedXeBundledPlugins.size())
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    const RedXeBundledPluginSpec& spec = kRedXeBundledPlugins[pluginIndex];
    ModuleSlot& slot = _modules[pluginIndex];
    HRESULT result = S_OK;
    if (!slot.create)
    {
        result = BindModule(spec, slot);
    }
    if (FAILED(result))
    {
        return result;
    }
    if ((slot.capabilities & requiredCapabilities) != requiredCapabilities)
    {
        return E_NOINTERFACE;
    }
    module->create = slot.create;
    module->getSettingsContract = slot.getSettingsContract;
    return S_OK;
}

HRESULT PluginHost::ValidateDataSets(ProviderRuntime& runtime) noexcept
{
    if (!runtime.descriptors || runtime.descriptorCount == 0 || runtime.descriptorCount > runtime.dataSets.size())
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    for (uint32_t index = 0; index < runtime.descriptorCount; ++index)
    {
        const RedXeDataSetDescriptor& descriptor = runtime.descriptors[index];
        if (descriptor.sizeBytes != sizeof(RedXeDataSetDescriptor) || !RedXeIsValidMachineId(descriptor.dataSetId) ||
            !descriptor.displayName || !descriptor.description || !descriptor.columns || descriptor.columnCount == 0 ||
            descriptor.columnCount > 256 || descriptor.maximumRows == 0 ||
            descriptor.recommendedIntervalMilliseconds == 0 ||
            descriptor.recommendedIntervalMilliseconds > kMaximumSubscriptionIntervalMilliseconds)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        for (uint32_t previous = 0; previous < index; ++previous)
        {
            if (RedXeAsciiEqualsIgnoreCase(runtime.descriptors[previous].dataSetId, descriptor.dataSetId))
            {
                return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
            }
        }
        for (uint32_t columnIndex = 0; columnIndex < descriptor.columnCount; ++columnIndex)
        {
            const RedXeDataColumnDescriptor& column = descriptor.columns[columnIndex];
            if (column.sizeBytes != sizeof(RedXeDataColumnDescriptor) || !RedXeIsValidMachineId(column.columnId) ||
                !column.displayName || column.valueType <= RedXeDataValueTypeInvalid ||
                column.valueType > RedXeDataValueTypeUtf16)
            {
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            for (uint32_t previous = 0; previous < columnIndex; ++previous)
            {
                if (RedXeAsciiEqualsIgnoreCase(descriptor.columns[previous].columnId, column.columnId))
                {
                    return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
                }
            }
        }
        runtime.dataSets[index].descriptor = &descriptor;
    }
    return S_OK;
}

HRESULT PluginHost::EnsureProvider(const char* providerId, size_t& providerIndex) noexcept
{
    providerIndex = _providers.size();
    if (!RedXeIsValidMachineId(providerId))
    {
        return E_INVALIDARG;
    }

    AcquireSRWLockShared(&_subscriptionLock);
    for (size_t index = 0; index < _providerCount; ++index)
    {
        if (RedXeAsciiEqualsIgnoreCase(_providers[index].providerId, providerId))
        {
            providerIndex = index;
            break;
        }
    }
    ReleaseSRWLockShared(&_subscriptionLock);
    if (providerIndex != _providers.size())
    {
        return S_OK;
    }

    if (_providerCount >= _providers.size())
    {
        return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    }

    ModuleView module{};
    HRESULT result = GetPluginModule(providerId, RedXePluginCapabilityDataSource, &module);
    if (FAILED(result))
    {
        return result;
    }

    ProviderRuntime runtime{};
    void* sourceObject = nullptr;
    result = module.create(__uuidof(IRedXeDataSource), nullptr, Interface(), providerId, &sourceObject);
    if (FAILED(result) || !sourceObject)
    {
        return FAILED(result) ? result : E_UNEXPECTED;
    }
    runtime.source.attach(static_cast<IRedXeDataSource*>(sourceObject));
    const size_t pluginIndex = FindBundledPluginIndex(providerId);
    runtime.providerId = kRedXeBundledPlugins[pluginIndex].pluginId;
    result = runtime.source->GetDataSets(&runtime.descriptors, &runtime.descriptorCount);
    if (SUCCEEDED(result))
    {
        result = ValidateDataSets(runtime);
    }
    if (FAILED(result))
    {
        return result;
    }

    AcquireSRWLockExclusive(&_subscriptionLock);
    for (size_t index = 0; index < _providerCount; ++index)
    {
        if (RedXeAsciiEqualsIgnoreCase(_providers[index].providerId, providerId))
        {
            providerIndex = index;
            ReleaseSRWLockExclusive(&_subscriptionLock);
            return S_OK;
        }
    }
    if (_providerCount >= _providers.size())
    {
        ReleaseSRWLockExclusive(&_subscriptionLock);
        return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    }
    auto* provider = new (std::nothrow) DataProvider(*this, _providerCount);
    if (!provider)
    {
        ReleaseSRWLockExclusive(&_subscriptionLock);
        return E_OUTOFMEMORY;
    }
    runtime.provider.attach(provider);
    _providers[_providerCount] = std::move(runtime);
    providerIndex = _providerCount;
    ++_providerCount;
    ReleaseSRWLockExclusive(&_subscriptionLock);
    return S_OK;
}

HRESULT PluginHost::GetDataProvider(const char* providerId, IRedXeDataProvider** provider) noexcept
{
    if (provider)
    {
        *provider = nullptr;
    }
    if (!provider)
    {
        return E_POINTER;
    }

    size_t providerIndex = 0;
    const HRESULT result = EnsureProvider(providerId, providerIndex);
    if (FAILED(result))
    {
        return result;
    }
    return _providers[providerIndex].provider->QueryInterface(__uuidof(IRedXeDataProvider),
                                                              reinterpret_cast<void**>(provider));
}

HRESULT PluginHost::RequestFrame() noexcept
{
    // Thread-safe, allocation-free, and coalescing. It never forces a frame: the UI thread's frame policy still
    // decides whether a blocked or occluded host renders.
    RequestUiInvalidate();
    return S_OK;
}

HRESULT PluginHost::ReportWidgetStatus(const char* instanceId, const RedXeWidgetStatusReport* report) noexcept
{
    if (!RedXeIsValidMachineId(instanceId) || !report || report->sizeBytes != sizeof(RedXeWidgetStatusReport) ||
        report->status > RedXeWidgetStatusUnavailable)
    {
        return E_INVALIDARG;
    }

    std::array<wchar_t, kMaximumWidgetStatusReasonCharacters> reason{};
    if (report->reason)
    {
        // The reason pointer is borrowed only for this call, so copy it into bounded host storage before returning.
        (void)StringCchCopyNW(reason.data(), reason.size(), report->reason, reason.size() - 1);
    }

    bool changed = false;
    AcquireSRWLockExclusive(&_widgetStatusLock);
    WidgetStatusSlot* slot = nullptr;
    WidgetStatusSlot* free = nullptr;
    for (WidgetStatusSlot& candidate : _widgetStatus)
    {
        if (candidate.used && RedXeAsciiEqualsIgnoreCase(candidate.instanceId.data(), instanceId))
        {
            slot = &candidate;
            break;
        }
        if (!candidate.used && !free)
        {
            free = &candidate;
        }
    }
    if (!slot)
    {
        slot = free;
        if (slot)
        {
            slot->used = true;
            slot->status = RedXeWidgetStatusOk;
            (void)StringCchCopyNA(slot->instanceId.data(), slot->instanceId.size(), instanceId,
                                  slot->instanceId.size() - 1);
        }
    }
    if (slot)
    {
        changed =
            slot->status != report->status || std::wcsncmp(slot->reason.data(), reason.data(), reason.size()) != 0;
        slot->status = report->status;
        slot->reason = reason;
    }
    ReleaseSRWLockExclusive(&_widgetStatusLock);

    if (!slot)
    {
        return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    }
    if (changed)
    {
        RequestUiInvalidate();
    }
    return S_OK;
}

uint32_t PluginHost::WidgetStatus(const char* instanceId) const noexcept
{
    if (!instanceId || instanceId[0] == '\0')
    {
        return RedXeWidgetStatusOk;
    }
    uint32_t status = RedXeWidgetStatusOk;
    AcquireSRWLockShared(&_widgetStatusLock);
    for (const WidgetStatusSlot& candidate : _widgetStatus)
    {
        if (candidate.used && RedXeAsciiEqualsIgnoreCase(candidate.instanceId.data(), instanceId))
        {
            status = candidate.status;
            break;
        }
    }
    ReleaseSRWLockShared(&_widgetStatusLock);
    return status;
}

bool PluginHost::WidgetStatusReason(const char* instanceId, wchar_t* text, size_t capacity) const noexcept
{
    if (!instanceId || instanceId[0] == '\0' || !text || capacity == 0)
    {
        return false;
    }
    text[0] = L'\0';
    bool found = false;
    AcquireSRWLockShared(&_widgetStatusLock);
    for (const WidgetStatusSlot& candidate : _widgetStatus)
    {
        if (candidate.used && RedXeAsciiEqualsIgnoreCase(candidate.instanceId.data(), instanceId))
        {
            found = candidate.reason[0] != L'\0' &&
                    SUCCEEDED(StringCchCopyNW(text, capacity, candidate.reason.data(), capacity - 1));
            break;
        }
    }
    ReleaseSRWLockShared(&_widgetStatusLock);
    return found;
}

void PluginHost::ClearWidgetStatus(const char* instanceId) noexcept
{
    if (!instanceId || instanceId[0] == '\0')
    {
        return;
    }
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_pendingSettingsLock);
        for (auto& pending : _pendingSettings)
            if (pending && RedXeAsciiEqualsIgnoreCase(pending->instanceId.data(), instanceId))
                pending.reset();
    }
    AcquireSRWLockExclusive(&_widgetStatusLock);
    for (WidgetStatusSlot& candidate : _widgetStatus)
    {
        if (candidate.used && RedXeAsciiEqualsIgnoreCase(candidate.instanceId.data(), instanceId))
        {
            candidate = WidgetStatusSlot{};
            break;
        }
    }
    ReleaseSRWLockExclusive(&_widgetStatusLock);
}

HRESULT PluginHost::GetDataSets(size_t providerIndex, const RedXeDataSetDescriptor** descriptors,
                                uint32_t* count) noexcept
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
    if (providerIndex >= _providerCount || !_providers[providerIndex].source)
    {
        return E_UNEXPECTED;
    }
    *descriptors = _providers[providerIndex].descriptors;
    *count = _providers[providerIndex].descriptorCount;
    return S_OK;
}

HRESULT PluginHost::EnsureWorker() noexcept
{
    if (_worker.joinable())
    {
        return S_OK;
    }

    _stopEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    _changeEvent.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!_stopEvent || !_changeEvent)
    {
        const DWORD error = GetLastError();
        _changeEvent.reset();
        _stopEvent.reset();
        return error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    try
    {
        _worker = std::jthread([this](std::stop_token) noexcept { Worker(); });
    }
    catch (const std::bad_alloc&)
    {
        _changeEvent.reset();
        _stopEvent.reset();
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        _changeEvent.reset();
        _stopEvent.reset();
        return E_FAIL;
    }
    return S_OK;
}

HRESULT PluginHost::Subscribe(size_t providerIndex, const RedXeDataSubscriptionOptions* options, IRedXeDataSink* sink,
                              IRedXeDataSubscription** subscription) noexcept
{
    if (subscription)
    {
        *subscription = nullptr;
    }
    if (!subscription || !sink)
    {
        return E_POINTER;
    }
    if (!options || options->sizeBytes != sizeof(RedXeDataSubscriptionOptions) ||
        !RedXeIsValidMachineId(options->dataSetId) || options->requestedIntervalMilliseconds == 0 ||
        options->requestedIntervalMilliseconds > kMaximumSubscriptionIntervalMilliseconds ||
        providerIndex >= _providerCount)
    {
        return E_INVALIDARG;
    }

    ProviderRuntime& runtime = _providers[providerIndex];
    size_t dataSetIndex = runtime.descriptorCount;
    for (size_t index = 0; index < runtime.descriptorCount; ++index)
    {
        if (RedXeAsciiEqualsIgnoreCase(runtime.descriptors[index].dataSetId, options->dataSetId))
        {
            dataSetIndex = index;
            break;
        }
    }
    if (dataSetIndex == runtime.descriptorCount)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    HRESULT result = EnsureWorker();
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<IRedXeDataSink> sinkOwner;
    result = sink->QueryInterface(__uuidof(IRedXeDataSink), reinterpret_cast<void**>(sinkOwner.put()));
    if (FAILED(result))
    {
        return result;
    }

    AcquireSRWLockExclusive(&_subscriptionLock);
    size_t index = _subscriptions.size();
    for (size_t candidate = 0; candidate < _subscriptions.size(); ++candidate)
    {
        if (!_subscriptions[candidate].sink)
        {
            index = candidate;
            break;
        }
    }
    if (index == _subscriptions.size())
    {
        ReleaseSRWLockExclusive(&_subscriptionLock);
        return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    }

    uint64_t token = _nextToken++;
    if (token == 0)
    {
        token = _nextToken++;
    }
    auto* created = new (std::nothrow) Subscription(*this, index, token);
    if (!created)
    {
        ReleaseSRWLockExclusive(&_subscriptionLock);
        return E_OUTOFMEMORY;
    }

    SubscriptionSlot& slot = _subscriptions[index];
    slot.sink = std::move(sinkOwner);
    slot.token = token;
    slot.providerIndex = providerIndex;
    slot.dataSetIndex = dataSetIndex;
    slot.intervalMilliseconds = std::max(options->requestedIntervalMilliseconds,
                                         runtime.descriptors[dataSetIndex].recommendedIntervalMilliseconds);
    slot.active = false;
    ReleaseSRWLockExclusive(&_subscriptionLock);

    *subscription = static_cast<IRedXeDataSubscription*>(created);
    SetEvent(_changeEvent.get());
    return S_OK;
}

HRESULT PluginHost::SetSubscriptionActive(size_t index, uint64_t token, bool active) noexcept
{
    AcquireSRWLockExclusive(&_subscriptionLock);
    if (index >= _subscriptions.size() || !_subscriptions[index].sink || _subscriptions[index].token != token ||
        _subscriptions[index].removing)
    {
        ReleaseSRWLockExclusive(&_subscriptionLock);
        return E_UNEXPECTED;
    }
    const bool changed = _subscriptions[index].active != active;
    _subscriptions[index].active = active;
    while (!active && _subscriptions[index].token == token && _subscriptions[index].inFlight != 0)
    {
        SleepConditionVariableSRW(&_subscriptionDrained, &_subscriptionLock, INFINITE, 0);
    }
    ReleaseSRWLockExclusive(&_subscriptionLock);
    if (changed)
    {
        SetEvent(_changeEvent.get());
    }
    return S_OK;
}

void PluginHost::RemoveSubscription(size_t index, uint64_t token) noexcept
{
    wil::com_ptr_nothrow<IRedXeDataSink> sink;
    AcquireSRWLockExclusive(&_subscriptionLock);
    if (index < _subscriptions.size() && _subscriptions[index].token == token)
    {
        _subscriptions[index].active = false;
        _subscriptions[index].removing = true;
        while (_subscriptions[index].inFlight != 0)
        {
            SleepConditionVariableSRW(&_subscriptionDrained, &_subscriptionLock, INFINITE, 0);
        }
        sink = std::move(_subscriptions[index].sink);
        _subscriptions[index] = SubscriptionSlot{};
    }
    ReleaseSRWLockExclusive(&_subscriptionLock);
    sink.reset();
    if (_changeEvent)
    {
        SetEvent(_changeEvent.get());
    }
}

void PluginHost::Deliver(size_t providerIndex, size_t dataSetIndex, const RedXeDataSnapshot* snapshot) noexcept
{
    if (!snapshot)
    {
        return;
    }
    // Reserve deliveries under the lock so deactivation/release also drains callbacks already copied out but not
    // yet entered. Slot reuse is forbidden until every reserved reference has been released.
    std::array<wil::com_ptr_nothrow<IRedXeDataSink>, kMaximumSubscriptions> sinks{};
    std::array<size_t, kMaximumSubscriptions> slots{};
    size_t sinkCount = 0;
    AcquireSRWLockExclusive(&_subscriptionLock);
    for (size_t slotIndex = 0; slotIndex < _subscriptions.size(); ++slotIndex)
    {
        SubscriptionSlot& slot = _subscriptions[slotIndex];
        if (slot.sink && slot.active && slot.providerIndex == providerIndex && slot.dataSetIndex == dataSetIndex &&
            sinkCount < sinks.size())
        {
            sinks[sinkCount] = slot.sink;
            slots[sinkCount] = slotIndex;
            ++slot.inFlight;
            ++sinkCount;
        }
    }
    ReleaseSRWLockExclusive(&_subscriptionLock);

    bool delivered = false;
    for (size_t index = 0; index < sinkCount; ++index)
    {
        if (sinks[index])
        {
            (void)sinks[index]->OnDataSnapshot(snapshot);
            sinks[index].reset();
            AcquireSRWLockExclusive(&_subscriptionLock);
            --_subscriptions[slots[index]].inFlight;
            WakeAllConditionVariable(&_subscriptionDrained);
            ReleaseSRWLockExclusive(&_subscriptionLock);
            delivered = true;
        }
    }
    if (delivered)
    {
        RequestUiInvalidate();
    }
}

void PluginHost::Worker() noexcept
{
    HANDLE handles[] = {_stopEvent.get(), _changeEvent.get()};
    for (;;)
    {
        AcquireSRWLockExclusive(&_subscriptionLock);
        const size_t providerCount = _providerCount;
        for (size_t providerIndex = 0; providerIndex < providerCount; ++providerIndex)
        {
            ProviderRuntime& provider = _providers[providerIndex];
            for (size_t dataSetIndex = 0; dataSetIndex < provider.descriptorCount; ++dataSetIndex)
            {
                provider.dataSets[dataSetIndex].active = false;
                provider.dataSets[dataSetIndex].activeIntervalMilliseconds = kMaximumSubscriptionIntervalMilliseconds;
            }
        }
        for (const SubscriptionSlot& slot : _subscriptions)
        {
            if (!slot.sink || !slot.active || slot.providerIndex >= providerCount ||
                slot.dataSetIndex >= _providers[slot.providerIndex].descriptorCount)
            {
                continue;
            }
            DataSetRuntime& dataSet = _providers[slot.providerIndex].dataSets[slot.dataSetIndex];
            dataSet.active = true;
            dataSet.activeIntervalMilliseconds =
                std::min(dataSet.activeIntervalMilliseconds, slot.intervalMilliseconds);
        }
        ReleaseSRWLockExclusive(&_subscriptionLock);

        const uint64_t now = GetTickCount64();
        for (size_t providerIndex = 0; providerIndex < providerCount; ++providerIndex)
        {
            ProviderRuntime& provider = _providers[providerIndex];
            std::array<const char*, RedXeDataCollectMaximumDataSets> dueIds{};
            std::array<uint32_t, RedXeDataCollectMaximumDataSets> dueIndices{};
            uint32_t dueCount = 0;
            for (size_t dataSetIndex = 0; dataSetIndex < provider.descriptorCount; ++dataSetIndex)
            {
                DataSetRuntime& dataSet = provider.dataSets[dataSetIndex];
                if (!dataSet.active)
                {
                    dataSet.due = 0;
                    continue;
                }
                if ((dataSet.due == 0 || now >= dataSet.due) && dueCount < RedXeDataCollectMaximumDataSets &&
                    dataSet.descriptor && dataSet.descriptor->dataSetId)
                {
                    dueIds[dueCount] = dataSet.descriptor->dataSetId;
                    dueIndices[dueCount] = static_cast<uint32_t>(dataSetIndex);
                    ++dueCount;
                }
            }
            if (dueCount == 0)
            {
                continue;
            }
            SortDueDataSets(dueIds, dueIndices, dueCount);
            const RedXeDataCollectRequest request{
                sizeof(RedXeDataCollectRequest),
                dueIds.data(),
                dueCount,
            };
            const RedXeDataCollectResult* collectResult = nullptr;
            if (SUCCEEDED(provider.source->CollectSnapshots(&request, &collectResult)) &&
                CollectResultMatchesRequest(request, collectResult))
            {
                for (uint32_t index = 0; index < dueCount; ++index)
                {
                    Deliver(providerIndex, dueIndices[index], collectResult->snapshots[index]);
                }
            }
            const uint64_t afterCollection = GetTickCount64();
            for (uint32_t index = 0; index < dueCount; ++index)
            {
                DataSetRuntime& dataSet = provider.dataSets[dueIndices[index]];
                dataSet.due = afterCollection + dataSet.activeIntervalMilliseconds;
            }
        }

        DWORD timeout = INFINITE;
        const uint64_t afterCollection = GetTickCount64();
        for (size_t providerIndex = 0; providerIndex < providerCount; ++providerIndex)
        {
            ProviderRuntime& provider = _providers[providerIndex];
            for (size_t dataSetIndex = 0; dataSetIndex < provider.descriptorCount; ++dataSetIndex)
            {
                const DataSetRuntime& dataSet = provider.dataSets[dataSetIndex];
                if (dataSet.active)
                {
                    timeout = std::min(timeout, WaitMilliseconds(afterCollection, dataSet.due));
                }
            }
        }

        const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, timeout);
        if (waitResult == WAIT_OBJECT_0)
        {
            return;
        }
        if (waitResult == WAIT_OBJECT_0 + 1)
        {
            AcquireSRWLockExclusive(&_subscriptionLock);
            for (size_t providerIndex = 0; providerIndex < _providerCount; ++providerIndex)
            {
                ProviderRuntime& provider = _providers[providerIndex];
                for (size_t dataSetIndex = 0; dataSetIndex < provider.descriptorCount; ++dataSetIndex)
                {
                    provider.dataSets[dataSetIndex].due = 0;
                }
            }
            ReleaseSRWLockExclusive(&_subscriptionLock);
            continue;
        }
        if (waitResult != WAIT_TIMEOUT)
        {
            return;
        }
    }
}

void PluginHost::StopDataService() noexcept
{
    if (_stopEvent)
    {
        SetEvent(_stopEvent.get());
    }
    if (_worker.joinable())
    {
        _worker.join();
    }
    _worker = std::jthread{};

    for (SubscriptionSlot& slot : _subscriptions)
    {
        slot = SubscriptionSlot{};
    }
    for (size_t index = _providerCount; index > 0; --index)
    {
        ProviderRuntime& provider = _providers[index - 1];
        provider.provider.reset();
        provider.source.reset();
        provider = ProviderRuntime{};
    }
    _providerCount = 0;
    _changeEvent.reset();
    _stopEvent.reset();
}

void PluginHost::SetNetworkAccessEnabled(bool enabled) noexcept
{
    _networkAccessEnabled.store(enabled, std::memory_order_release);
    if (enabled)
    {
        return;
    }

    bool waitForIdle = false;
    AcquireSRWLockExclusive(&_networkLock);
    for (NetworkSlot& slot : _networkSlots)
    {
        slot.active = false;
        slot.due = 0;
        waitForIdle = waitForIdle || slot.inFlight;
    }
    ReleaseSRWLockExclusive(&_networkLock);
    if (_networkCancelEvent)
    {
        SetEvent(_networkCancelEvent.get());
    }
    if (waitForIdle && _networkIdleEvent)
    {
        (void)WaitForSingleObject(_networkIdleEvent.get(), INFINITE);
    }
    JoinNetworkWorker();
}

bool PluginHost::NetworkAccessEnabled() const noexcept
{
    return _networkAccessEnabled.load(std::memory_order_acquire);
}

bool PluginHost::NetworkWorkerRunning() const noexcept
{
    return _networkWorker.joinable();
}

HRESULT PluginHost::RegisterNetworkWidget(IRedXeNetworkWidget* widget) noexcept
{
    if (!widget)
    {
        return E_POINTER;
    }

    AcquireSRWLockExclusive(&_networkLock);
    size_t empty = kMaximumNetworkWidgets;
    for (size_t index = 0; index < kMaximumNetworkWidgets; ++index)
    {
        NetworkSlot& slot = _networkSlots[index];
        if (slot.widget.get() == widget)
        {
            ReleaseSRWLockExclusive(&_networkLock);
            return S_OK;
        }
        if (!slot.widget && empty == kMaximumNetworkWidgets)
        {
            empty = index;
        }
    }
    if (empty == kMaximumNetworkWidgets)
    {
        ReleaseSRWLockExclusive(&_networkLock);
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    }
    _networkSlots[empty].widget = widget;
    _networkSlots[empty].due = 0;
    _networkSlots[empty].active = false;
    _networkSlots[empty].inFlight = false;
    ReleaseSRWLockExclusive(&_networkLock);
    return S_OK;
}

void PluginHost::UnregisterNetworkWidget(IRedXeNetworkWidget* widget) noexcept
{
    if (!widget)
    {
        return;
    }
    SetNetworkWidgetActive(widget, false);
    AcquireSRWLockExclusive(&_networkLock);
    for (NetworkSlot& slot : _networkSlots)
    {
        if (slot.widget.get() == widget)
        {
            slot = NetworkSlot{};
            break;
        }
    }
    ReleaseSRWLockExclusive(&_networkLock);
}

void PluginHost::SetNetworkWidgetActive(IRedXeNetworkWidget* widget, bool active) noexcept
{
    if (!widget)
    {
        return;
    }

    bool waitForIdle = false;
    AcquireSRWLockExclusive(&_networkLock);
    NetworkSlot* selected = nullptr;
    for (NetworkSlot& slot : _networkSlots)
    {
        if (slot.widget.get() == widget)
        {
            selected = &slot;
            break;
        }
    }
    if (!selected)
    {
        ReleaseSRWLockExclusive(&_networkLock);
        return;
    }

    if (active)
    {
        const bool allow = _networkAccessEnabled.load(std::memory_order_acquire);
        selected->active = allow;
        selected->due = 0;
        ReleaseSRWLockExclusive(&_networkLock);
        if (allow && SUCCEEDED(EnsureNetworkWorker()) && _networkWakeEvent)
        {
            SetEvent(_networkWakeEvent.get());
        }
        return;
    }

    selected->active = false;
    selected->due = 0;
    waitForIdle = selected->inFlight;
    if (waitForIdle && _networkCancelEvent)
    {
        SetEvent(_networkCancelEvent.get());
    }
    ReleaseSRWLockExclusive(&_networkLock);
    if (waitForIdle && _networkIdleEvent)
    {
        (void)WaitForSingleObject(_networkIdleEvent.get(), INFINITE);
    }
}

HRESULT PluginHost::EnsureNetworkWorker() noexcept
{
    if (!_networkAccessEnabled.load(std::memory_order_acquire))
    {
        return HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE);
    }
    if (_networkWorker.joinable())
    {
        return S_OK;
    }

    _networkStopEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    _networkWakeEvent.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    _networkCancelEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    _networkIdleEvent.reset(CreateEventW(nullptr, TRUE, TRUE, nullptr));
    if (!_networkStopEvent || !_networkWakeEvent || !_networkCancelEvent || !_networkIdleEvent)
    {
        const DWORD error = GetLastError();
        _networkIdleEvent.reset();
        _networkCancelEvent.reset();
        _networkWakeEvent.reset();
        _networkStopEvent.reset();
        return error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    try
    {
        _networkWorker = std::jthread([this](std::stop_token) noexcept { NetworkWorker(); });
    }
    catch (const std::bad_alloc&)
    {
        _networkIdleEvent.reset();
        _networkCancelEvent.reset();
        _networkWakeEvent.reset();
        _networkStopEvent.reset();
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        _networkIdleEvent.reset();
        _networkCancelEvent.reset();
        _networkWakeEvent.reset();
        _networkStopEvent.reset();
        return E_FAIL;
    }
    return S_OK;
}

void PluginHost::NetworkWorker() noexcept
{
    HANDLE handles[] = {_networkStopEvent.get(), _networkWakeEvent.get()};
    for (;;)
    {
        DWORD timeout = INFINITE;
        const uint64_t now = GetTickCount64();
        AcquireSRWLockExclusive(&_networkLock);
        for (const NetworkSlot& slot : _networkSlots)
        {
            if (slot.widget && slot.active && !slot.inFlight)
            {
                timeout = std::min(timeout, WaitMilliseconds(now, slot.due));
            }
        }
        ReleaseSRWLockExclusive(&_networkLock);

        const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, timeout);
        if (waitResult == WAIT_OBJECT_0)
        {
            return;
        }
        if (waitResult != WAIT_OBJECT_0 + 1 && waitResult != WAIT_TIMEOUT)
        {
            return;
        }

        wil::com_ptr_nothrow<IRedXeNetworkWidget> selected;
        AcquireSRWLockExclusive(&_networkLock);
        const uint64_t afterWait = GetTickCount64();
        NetworkSlot* dueSlot = nullptr;
        for (NetworkSlot& slot : _networkSlots)
        {
            if (!slot.widget || !slot.active || slot.inFlight)
            {
                continue;
            }
            if (slot.due == 0 || afterWait >= slot.due)
            {
                dueSlot = &slot;
                break;
            }
        }
        if (dueSlot)
        {
            selected = dueSlot->widget;
            dueSlot->inFlight = true;
            if (_networkIdleEvent)
            {
                ResetEvent(_networkIdleEvent.get());
            }
            if (_networkCancelEvent)
            {
                ResetEvent(_networkCancelEvent.get());
            }
        }
        ReleaseSRWLockExclusive(&_networkLock);
        if (!selected)
        {
            continue;
        }

        uint32_t delayMilliseconds = 0;
        const HRESULT result = selected->RunNetworkWork(_networkCancelEvent.get(), &delayMilliseconds);
        AcquireSRWLockExclusive(&_networkLock);
        for (NetworkSlot& slot : _networkSlots)
        {
            if (slot.widget.get() != selected.get())
            {
                continue;
            }
            slot.inFlight = false;
            if (!slot.active)
            {
                slot.due = 0;
            }
            else if (result == S_OK && delayMilliseconds >= 1 &&
                     delayMilliseconds <= kRedXeMaximumScheduledFrameDelayMilliseconds)
            {
                slot.due = GetTickCount64() + delayMilliseconds;
            }
            else if (result == S_FALSE)
            {
                slot.active = false;
                slot.due = 0;
            }
            else
            {
                slot.due = GetTickCount64() + 60'000ULL;
            }
            break;
        }
        if (_networkIdleEvent)
        {
            SetEvent(_networkIdleEvent.get());
        }
        ReleaseSRWLockExclusive(&_networkLock);
    }
}

void PluginHost::JoinNetworkWorker() noexcept
{
    if (_networkStopEvent)
    {
        SetEvent(_networkStopEvent.get());
    }
    if (_networkWorker.joinable())
    {
        _networkWorker.join();
    }
    _networkWorker = std::jthread{};
    AcquireSRWLockExclusive(&_networkLock);
    for (NetworkSlot& slot : _networkSlots)
    {
        slot.inFlight = false;
    }
    ReleaseSRWLockExclusive(&_networkLock);
    if (_networkIdleEvent)
    {
        SetEvent(_networkIdleEvent.get());
    }
    _networkIdleEvent.reset();
    _networkCancelEvent.reset();
    _networkWakeEvent.reset();
    _networkStopEvent.reset();
}

void PluginHost::StopNetworkService() noexcept
{
    bool waitForIdle = false;
    AcquireSRWLockExclusive(&_networkLock);
    for (NetworkSlot& slot : _networkSlots)
    {
        slot.active = false;
        waitForIdle = waitForIdle || slot.inFlight;
    }
    ReleaseSRWLockExclusive(&_networkLock);
    if (_networkCancelEvent)
    {
        SetEvent(_networkCancelEvent.get());
    }
    if (waitForIdle && _networkIdleEvent)
    {
        (void)WaitForSingleObject(_networkIdleEvent.get(), INFINITE);
    }
    JoinNetworkWorker();
    AcquireSRWLockExclusive(&_networkLock);
    for (NetworkSlot& slot : _networkSlots)
    {
        slot = NetworkSlot{};
    }
    ReleaseSRWLockExclusive(&_networkLock);
}

void PluginHost::ShutdownModules() noexcept
{
    for (size_t index = _modules.size(); index > 0; --index)
    {
        ModuleSlot& slot = _modules[index - 1];
        if (slot.module && slot.shutdown)
        {
            slot.shutdown();
        }
        slot.shutdown = nullptr;
        slot.getSettingsContract = nullptr;
        slot.enumerate = nullptr;
        slot.create = nullptr;
        slot.capabilities = RedXePluginCapabilityNone;
        if (slot.module)
        {
            (void)slot.module.release();
        }
    }
}
