#include "LogiconService.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <tlhelp32.h>

#include <yyjson.h>

namespace Logicon
{
namespace
{
SRWLOCK g_currentLock = SRWLOCK_INIT;
LogiconService* g_current = nullptr;

constexpr uint32_t kBatchPauseMilliseconds = 10;
constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

void Hash(uint64_t& hash, const void* data, size_t bytes) noexcept
{
    const auto* source = static_cast<const uint8_t*>(data);
    for (size_t index = 0; index < bytes; ++index)
    {
        hash ^= source[index];
        hash *= kFnvPrime;
    }
}

template <typename Value> void HashValue(uint64_t& hash, const Value& value) noexcept
{
    Hash(hash, &value, sizeof(value));
}

[[nodiscard]] uint32_t MinuteOfDay() noexcept
{
    SYSTEMTIME local{};
    GetLocalTime(&local);
    return static_cast<uint32_t>(local.wHour) * 60U + local.wMinute;
}

// Bit n set = dialpad button n has a binding (even "none"), so it is diverted; unbound buttons stay native.
[[nodiscard]] uint32_t BoundDialButtons(const Settings& settings) noexcept
{
    uint32_t mask = 0;
    for (uint32_t button = 0; button < kDialpadButtons; ++button)
    {
        if (settings.dialpad.Button(button))
        {
            mask |= 1U << button;
        }
    }
    return mask;
}

[[nodiscard]] bool IsDeviceGone(HRESULT result) noexcept
{
    return result == HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED) ||
           result == HRESULT_FROM_WIN32(ERROR_GEN_FAILURE) || result == HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED) ||
           result == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE) || result == HRESULT_FROM_WIN32(ERROR_TIMEOUT);
}

[[nodiscard]] uint32_t Utf8ToWide(std::string_view utf8, wchar_t* wide, uint32_t capacity) noexcept
{
    if (!wide || capacity == 0)
    {
        return 0;
    }
    wide[0] = L'\0';
    if (utf8.empty())
    {
        return 0;
    }
    const int converted = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide,
                                              static_cast<int>(capacity - 1));
    if (converted <= 0)
    {
        wide[0] = L'\0';
        return 0;
    }
    wide[converted] = L'\0';
    return static_cast<uint32_t>(converted);
}
} // namespace

LogiconService* LogiconService::Current() noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&g_currentLock);
    return g_current;
}

LogiconService::LogiconService(IRedXeHost* host, uint32_t backgroundRgb) noexcept
    : _host(host), _backgroundRgb(backgroundRgb & 0x00FFFFFFU)
{
    const auto guard = wil::AcquireSRWLockExclusive(&g_currentLock);
    if (!g_current)
    {
        g_current = this;
    }
}

LogiconService::~LogiconService()
{
    if (_systemFeed)
    {
        _systemFeed->Stop();
        _systemFeed.reset();
    }
    const auto guard = wil::AcquireSRWLockExclusive(&g_currentLock);
    if (g_current == this)
    {
        g_current = nullptr;
    }
}

void LogiconService::Log(uint32_t level, const char* eventId, const char* message, HRESULT code) noexcept
{
    (void)RedXeHostLog(_host, level, kPluginId, nullptr, eventId, message, code);
}

HRESULT LogiconService::ParseConfiguration(const char* jsonUtf8, uint32_t bytes) noexcept
{
    if (!jsonUtf8 || bytes == 0)
    {
        // No configuration selects the defaults.
        Settings defaults{};
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _settings = defaults;
        return S_OK;
    }
    yyjson_doc* document = yyjson_read(jsonUtf8, bytes, YYJSON_READ_NOFLAG);
    if (!document)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    yyjson_val* root = yyjson_doc_get_root(document);
    yyjson_val* instance = yyjson_is_obj(root) ? yyjson_obj_get(root, "instance") : nullptr;
    Settings parsed{};
    std::array<char, 160> diagnostic{};
    const HRESULT result = instance ? ParseSettings(instance, parsed, diagnostic.data(), diagnostic.size())
                                    : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    yyjson_doc_free(document);
    if (FAILED(result))
    {
        Log(RedXeLogLevelError, "settings-rejected", diagnostic[0] != '\0' ? diagnostic.data() : "invalid envelope.",
            result);
        return result;
    }
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    _settings = parsed;
    ++_settingsGeneration;
    return S_OK;
}

HRESULT LogiconService::Start(const RedXeServiceStartContext* context) noexcept
{
    if (!context)
    {
        return E_POINTER;
    }
    if (context->sizeBytes != sizeof(RedXeServiceStartContext))
    {
        return E_INVALIDARG;
    }
    _deviceAccess.store((context->flags & RedXeServiceFlagDeviceAccessDisabled) == 0, std::memory_order_release);
    _started.store(true, std::memory_order_release);
    UpdateSystemFeed();
    PublishSnapshot();
    return S_OK;
}

void LogiconService::UpdateSystemFeed() noexcept
{
    bool wanted = false;
    {
        const auto guard = wil::AcquireSRWLockShared(&_lock);
        wanted = _settings.UsesSystemFaces();
    }
    if (!wanted)
    {
        if (_systemFeed)
        {
            _systemFeed->SetActive(false);
        }
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _systemFeedActive = false;
        return;
    }
    if (!_systemFeed)
    {
        wil::com_ptr_nothrow<SystemDataFeed> feed;
        feed.attach(new (std::nothrow) SystemDataFeed(*this));
        const HRESULT started = feed ? feed->Start(_host) : E_OUTOFMEMORY;
        if (FAILED(started))
        {
            if (!_systemFeedFailureLogged)
            {
                Log(RedXeLogLevelWarning, "system-data-unavailable",
                    "builtin.system-data is unavailable; cpu, memory, and gpu faces show no value.", started);
                _systemFeedFailureLogged = true;
            }
            return;
        }
        _systemFeed = std::move(feed);
    }
    _systemFeed->SetActive(true);
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    _systemFeedActive = true;
}

void LogiconService::OnSystemValues(const SystemValues& values) noexcept
{
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        if (_systemValues == values)
        {
            return;
        }
        _systemValues = values;
        ++_systemGeneration;
    }
    WakeLane();
}

HRESULT LogiconService::ApplySettings(const char* settingsJsonUtf8, uint32_t settingsBytes) noexcept
{
    if (!settingsJsonUtf8)
    {
        return E_POINTER;
    }
    if (settingsBytes == 0)
    {
        return E_INVALIDARG;
    }
    Settings parsed{};
    std::array<char, 160> diagnostic{};
    const HRESULT result = ParseSettingsJson(std::string_view(settingsJsonUtf8, settingsBytes), parsed,
                                             diagnostic.data(), diagnostic.size());
    if (FAILED(result))
    {
        Log(RedXeLogLevelWarning, "settings-rejected", diagnostic.data(), result);
        return result;
    }
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _settings = parsed;
        ++_settingsGeneration;
        _keyPage = std::min(_keyPage, parsed.KeyPageCount() - 1U);
        _overrides = {};
        ++_overrideGeneration;
    }
    if (Started())
    {
        UpdateSystemFeed();
    }
    WakeLane();
    return S_OK;
}

HRESULT LogiconService::OnHostState(const RedXeHostState* state) noexcept
{
    if (!state)
    {
        return E_POINTER;
    }
    if (state->sizeBytes != sizeof(RedXeHostState))
    {
        return E_INVALIDARG;
    }
    HostStateCopy copy{};
    copy.flags = state->flags;
    copy.pageIndex = state->pageIndex;
    copy.pageCount = state->pageCount;
    copy.widgetCount = state->widgetCount;
    copy.raisedWidgetOrdinal = state->raisedWidgetOrdinal;
    if (state->pageId)
    {
        strncpy_s(copy.pageId.data(), copy.pageId.size(), state->pageId, _TRUNCATE);
    }
    if (state->pageName)
    {
        wcsncpy_s(copy.pageName.data(), copy.pageName.size(), state->pageName, _TRUNCATE);
        copy.pageNameLength = static_cast<uint32_t>(wcsnlen_s(copy.pageName.data(), copy.pageName.size()));
    }
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _hostState = copy;
        ++_hostGeneration;
    }
    WakeLane();
    return S_OK;
}

HRESULT LogiconService::Stop() noexcept
{
    _started.store(false, std::memory_order_release);
    if (_systemFeed)
    {
        // Draining the subscriptions here ends every callback into this object before it can be released.
        _systemFeed->Stop();
        _systemFeed.reset();
    }
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _systemFeedActive = false;
    }
    PublishSnapshot();
    return S_OK;
}

bool LogiconService::Started() const noexcept
{
    return _started.load(std::memory_order_acquire);
}

void LogiconService::WakeLane() noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    if (_wakeEvent)
    {
        SetEvent(_wakeEvent);
    }
}

void LogiconService::AttachMonitor(bool attached) noexcept
{
    if (attached)
    {
        _monitorAttached.fetch_add(1, std::memory_order_acq_rel);
    }
    else if (_monitorAttached.load(std::memory_order_acquire) != 0)
    {
        _monitorAttached.fetch_sub(1, std::memory_order_acq_rel);
    }
    WakeLane();
}

void LogiconService::CopyMonitorSnapshot(MonitorSnapshot& snapshot) const noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    snapshot = _snapshot;
}

bool LogiconService::CopyFaceSurface(uint32_t* bgra, uint32_t capacityPixels, uint32_t& generation) const noexcept
{
    if (!bgra || capacityPixels < kGridPixels)
    {
        return false;
    }
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    if (!_grid || _faceGeneration == generation)
    {
        return false;
    }
    std::memcpy(bgra, _grid.get(), sizeof(uint32_t) * kGridPixels);
    generation = _faceGeneration;
    return true;
}

HRESULT LogiconService::InjectControl(uint32_t kind, uint32_t index, bool down) noexcept
{
    if (kind > kInjectKindDialButton || (kind == kInjectKindKey && index >= kKeyCount) ||
        (kind == kInjectKindPageButton && index >= 2) ||
        (kind == kInjectKindDialButton && index >= kDialpadButtonCount))
    {
        return E_INVALIDARG;
    }
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        if (_injectedCount >= _injected.size())
        {
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        }
        InjectedControl& control = _injected[_injectedCount++];
        control.kind = static_cast<uint8_t>(kind);
        control.index = static_cast<uint8_t>(index);
        control.down = down;
    }
    WakeLane();
    return S_OK;
}

HRESULT LogiconService::SetFaceOverride(uint32_t slot, OverrideKind kind, uint32_t colorRgb) noexcept
{
    if (slot >= kKeyCount)
    {
        return E_INVALIDARG;
    }
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _overrides[slot].kind = kind;
        _overrides[slot].colorRgb = colorRgb & 0x00FFFFFFU;
        ++_overrideGeneration;
    }
    WakeLane();
    return S_OK;
}

HRESULT LogiconService::SetBrightness(uint32_t percent) noexcept
{
    if (percent < kMinimumBrightness || percent > kMaximumBrightness)
    {
        return E_INVALIDARG;
    }
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _brightnessRequest = percent;
    }
    WakeLane();
    return S_OK;
}

HRESULT LogiconService::SetSynthetic(bool enabled) noexcept
{
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _syntheticRequested = enabled;
    }
    WakeLane();
    return S_OK;
}

HRESULT LogiconService::InjectSyntheticReport(const uint8_t* report, uint32_t bytes) noexcept
{
    return _synthetic.InjectReport(report, bytes);
}

void LogiconService::RequestAction(uint32_t action, int32_t argument, const char* target) noexcept
{
    RedXeHostActionRequest request{};
    request.sizeBytes = sizeof(request);
    request.action = action;
    request.argument = argument;
    request.targetUtf8 = target && target[0] != '\0' ? target : nullptr;
    const HRESULT result = _host ? _host->RequestHostAction(&request) : E_POINTER;
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    if (SUCCEEDED(result))
    {
        ++_actionsRequested;
        _lastAction = action;
    }
}

void LogiconService::SendMediaKey(MediaKey key) noexcept
{
    if (!_deviceAccess.load(std::memory_order_acquire))
    {
        return;
    }
    WORD virtualKey = 0;
    switch (key)
    {
    case MediaKey::VolumeUp:
        virtualKey = VK_VOLUME_UP;
        break;
    case MediaKey::VolumeDown:
        virtualKey = VK_VOLUME_DOWN;
        break;
    case MediaKey::Mute:
        virtualKey = VK_VOLUME_MUTE;
        break;
    case MediaKey::PlayPause:
        virtualKey = VK_MEDIA_PLAY_PAUSE;
        break;
    case MediaKey::NextTrack:
        virtualKey = VK_MEDIA_NEXT_TRACK;
        break;
    case MediaKey::PreviousTrack:
        virtualKey = VK_MEDIA_PREV_TRACK;
        break;
    default:
        return;
    }
    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = virtualKey;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = virtualKey;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    (void)SendInput(2, inputs, sizeof(INPUT));
}

void LogiconService::ChangeKeyPage(int direction) noexcept
{
    const uint32_t count = std::max(1U, _laneSettings.KeyPageCount());
    const uint32_t next = (_laneKeyPage + count + static_cast<uint32_t>(direction < 0 ? count - 1 : 1)) % count;
    if (next == _laneKeyPage)
    {
        return;
    }
    _laneKeyPage = next;
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    _keyPage = next;
}

void LogiconService::DispatchSlot(uint32_t slot) noexcept
{
    DispatchBinding(_laneSettings.Find(_laneKeyPage, slot));
}

void LogiconService::DispatchDialButton(uint32_t button) noexcept
{
    ++_dialButtonPresses;
    DispatchBinding(_laneSettings.dialpad.Button(button));
}

void LogiconService::DispatchBinding(const KeyBinding* binding) noexcept
{
    if (!binding || !TargetIsValid(*binding))
    {
        return;
    }
    switch (binding->action)
    {
    case KeyAction::PageNext:
        RequestAction(RedXeHostActionPageNext, 0, nullptr);
        break;
    case KeyAction::PagePrevious:
        RequestAction(RedXeHostActionPagePrevious, 0, nullptr);
        break;
    case KeyAction::PageGoTo:
        RequestAction(RedXeHostActionPageGoTo, -1, binding->target.data());
        break;
    case KeyAction::WidgetRaise:
        RequestAction(RedXeHostActionWidgetRaise, -1, binding->target.data());
        break;
    case KeyAction::WidgetDismiss:
        RequestAction(RedXeHostActionWidgetDismiss, 0, nullptr);
        break;
    case KeyAction::WidgetToggle:
        RequestAction(RedXeHostActionWidgetToggle, -1, binding->target.data());
        break;
    case KeyAction::Launch:
        RequestAction(RedXeHostActionLaunch, 0, binding->target.data());
        break;
    case KeyAction::Keys:
    {
        MediaKey key = MediaKey::None;
        if (MediaKeyFromName(binding->Target(), key))
        {
            SendMediaKey(key);
        }
        break;
    }
    case KeyAction::KeyPageNext:
        ChangeKeyPage(1);
        break;
    case KeyAction::KeyPagePrevious:
        ChangeKeyPage(-1);
        break;
    default:
        break;
    }
}

void LogiconService::DispatchPageButton(uint32_t button) noexcept
{
    if (_laneSettings.pageButtons == PageButtons::DashboardPages)
    {
        RequestAction(button == 0 ? RedXeHostActionPagePrevious : RedXeHostActionPageNext, 0, nullptr);
        return;
    }
    ChangeKeyPage(button == 0 ? -1 : 1);
}

void LogiconService::DispatchEdges(const ControlEdges& edges) noexcept
{
    for (uint32_t slot = 0; slot < kKeyCount; ++slot)
    {
        if ((edges.keysDown & (1U << slot)) != 0)
        {
            DispatchSlot(slot);
        }
    }
    if ((edges.pageButtonsDown & 1U) != 0)
    {
        DispatchPageButton(0);
    }
    if ((edges.pageButtonsDown & 2U) != 0)
    {
        DispatchPageButton(1);
    }
    for (uint32_t button = 0; button < kDialpadButtonCount; ++button)
    {
        if ((edges.dialButtonsDown & (1U << button)) != 0)
        {
            DispatchDialButton(button);
        }
    }
}

void LogiconService::DispatchWheel(DialAction action, int32_t& accumulator, int32_t deltaRaw) noexcept
{
    accumulator += deltaRaw;
    // One step per detent in either direction; the remainder carries over to the next packet.
    while (accumulator >= static_cast<int32_t>(kWheelDetentUnits) ||
           accumulator <= -static_cast<int32_t>(kWheelDetentUnits))
    {
        const int direction = accumulator > 0 ? 1 : -1;
        accumulator -= direction * static_cast<int32_t>(kWheelDetentUnits);
        ++_wheelSteps;
        switch (action)
        {
        case DialAction::Volume:
            SendMediaKey(direction > 0 ? MediaKey::VolumeUp : MediaKey::VolumeDown);
            break;
        case DialAction::Page:
            RequestAction(direction > 0 ? RedXeHostActionPageNext : RedXeHostActionPagePrevious, 0, nullptr);
            break;
        case DialAction::KeyPage:
            ChangeKeyPage(direction);
            break;
        case DialAction::Brightness:
        {
            const uint32_t current = _brightnessOverride != 0 ? _brightnessOverride : _laneSettings.brightness;
            const int next = std::clamp(static_cast<int>(current) + direction * static_cast<int>(kWheelBrightnessStep),
                                        static_cast<int>(kMinimumBrightness), static_cast<int>(kMaximumBrightness));
            _brightnessOverride = static_cast<uint32_t>(next);
            break;
        }
        default:
            break;
        }
    }
}

bool LogiconService::ClockFaceVisible() const noexcept
{
    for (uint32_t slot = 0; slot < kKeyCount; ++slot)
    {
        const KeyBinding* binding = _laneSettings.Find(_laneKeyPage, slot);
        if (binding && binding->face == KeyFace::Clock)
        {
            return true;
        }
    }
    return false;
}

void LogiconService::DetectCompetingWriter() noexcept
{
    _competingWriter = false;
    wil::unique_handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (!snapshot || snapshot.get() == INVALID_HANDLE_VALUE)
    {
        (void)snapshot.release();
        return;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry))
    {
        return;
    }
    do
    {
        if (_wcsicmp(entry.szExeFile, L"logioptionsplus_agent.exe") == 0 ||
            _wcsicmp(entry.szExeFile, L"logioptionsplus.exe") == 0 ||
            _wcsicmp(entry.szExeFile, L"logipluginservice.exe") == 0)
        {
            _competingWriter = true;
            break;
        }
    } while (Process32NextW(snapshot.get(), &entry));
    if (_competingWriter)
    {
        Log(RedXeLogLevelWarning, "service-degraded",
            "Logi Options+ is running and may repaint the keypad; quit it or remove the keypad from its profile.");
    }
}

HRESULT LogiconService::TryOpenDevice(HANDLE stopEvent) noexcept
{
    ++_connectAttempts;
    HRESULT result = S_OK;
    if (_syntheticActive)
    {
        result = _synthetic.Initialize();
        if (SUCCEEDED(result))
        {
            std::unique_ptr<HidPort> port{new (std::nothrow) SyntheticHidPort(_synthetic)};
            result = port ? _session.AttachPort(std::move(port)) : E_OUTOFMEMORY;
        }
    }
    else
    {
        HidCollections collections{};
        result = EnumerateVendorCollections(kVendorId, kKeypadProductId, kVendorUsagePage, collections);
        if (SUCCEEDED(result) && collections.count == 0)
        {
            result = HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
        }
        for (uint32_t index = 0; SUCCEEDED(result) && index < collections.count; ++index)
        {
            const HidCollectionInfo& info = collections.items[index];
            std::array<char, 200> line{};
            (void)sprintf_s(line.data(), line.size(),
                            "collection %u: usage 0x%04X in %u out %u feature %u in-ids 0x%08X out-ids 0x%08X "
                            "feature-ids 0x%08X.",
                            index, info.usage, info.inputReportBytes, info.outputReportBytes, info.featureReportBytes,
                            info.inputReportMask, info.outputReportMask, info.featureReportMask);
            Log(RedXeLogLevelDebug, "device-collection", line.data());
            std::unique_ptr<WindowsHidPort> port{new (std::nothrow) WindowsHidPort()};
            result = port ? port->Open(info) : E_OUTOFMEMORY;
            if (SUCCEEDED(result))
            {
                result = _session.AttachPort(std::move(port));
            }
        }
    }
    if (SUCCEEDED(result))
    {
        result = _session.Connect(stopEvent);
    }
    if (FAILED(result))
    {
        _lastConnectFailure = result;
        if (result != HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED) && result != _lastLoggedConnectFailure)
        {
            const LookupRecord& lookup = _session.LastLookup();
            std::array<char, 200> message{};
            (void)sprintf_s(message.data(), message.size(),
                            "the keypad could not be opened or initialized (last root lookup 0x%04X -> hr 0x%08X, "
                            "%u bytes: %02X %02X %02X %02X).",
                            lookup.featureId, static_cast<unsigned>(lookup.result), lookup.paramBytes, lookup.params[0],
                            lookup.params[1], lookup.params[2], lookup.params[3]);
            Log(RedXeLogLevelWarning, "connect-failed", message.data(), result);
            _lastLoggedConnectFailure = result;
        }
        _session.Detach();
        return result;
    }
    _lastConnectFailure = S_OK;
    _lastLoggedConnectFailure = S_OK;
    _appliedBrightness = 0;
    for (SlotState& slot : _slots)
    {
        slot.composed = false;
    }
    if (!_syntheticActive)
    {
        DetectCompetingWriter();
    }
    std::array<char, 160> message{};
    (void)sprintf_s(message.data(), message.size(),
                    "keypad connected (%s): display 0x%02X%s, controls 0x%02X, brightness 0x%02X, %u collection(s).",
                    _syntheticActive ? "synthetic" : "usb", _session.Features().display,
                    _session.Features().displayAssumed ? " (assumed)" : "", _session.Features().reprogControls,
                    _session.Features().brightness, _session.PortCount());
    Log(RedXeLogLevelInfo, "device-connected", message.data());
    return S_OK;
}

void LogiconService::CloseDevice(HANDLE stopEvent, bool restore) noexcept
{
    if (restore && _session.HasPorts())
    {
        (void)_session.Restore(stopEvent, _laneSettings.restoreLogoOnExit);
    }
    _session.Detach();
    _appliedBrightness = 0;
}

HRESULT LogiconService::TryOpenDialpad(HANDLE stopEvent) noexcept
{
    ++_dialpadAttempts;
    HidCollections collections{};
    HRESULT result = EnumerateVendorCollections(kVendorId, kDialpadProductId, kVendorUsagePage, collections);
    if (SUCCEEDED(result) && collections.count == 0)
    {
        result = HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
    }
    for (uint32_t index = 0; SUCCEEDED(result) && index < collections.count; ++index)
    {
        const HidCollectionInfo& info = collections.items[index];
        std::array<char, 200> line{};
        (void)sprintf_s(line.data(), line.size(),
                        "dialpad collection %u: usage 0x%04X in %u out %u in-ids 0x%08X out-ids 0x%08X.", index,
                        info.usage, info.inputReportBytes, info.outputReportBytes, info.inputReportMask,
                        info.outputReportMask);
        Log(RedXeLogLevelDebug, "device-collection", line.data());
        std::unique_ptr<WindowsHidPort> port{new (std::nothrow) WindowsHidPort()};
        result = port ? port->Open(info) : E_OUTOFMEMORY;
        if (SUCCEEDED(result))
        {
            result = _dialpad.AttachPort(std::move(port));
        }
    }
    if (SUCCEEDED(result))
    {
        result = _dialpad.ConnectDialpad(stopEvent, BoundDialButtons(_laneSettings));
    }
    if (FAILED(result))
    {
        _lastDialpadFailure = result;
        if (result != HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED) && result != _lastLoggedDialpadFailure)
        {
            const LookupRecord& lookup = _dialpad.LastLookup();
            std::array<char, 200> message{};
            (void)sprintf_s(message.data(), message.size(),
                            "the dialpad could not be opened or initialized (last root lookup 0x%04X -> hr 0x%08X).",
                            lookup.featureId, static_cast<unsigned>(lookup.result));
            Log(RedXeLogLevelWarning, "dialpad-connect-failed", message.data(), result);
            _lastLoggedDialpadFailure = result;
        }
        _dialpad.Detach();
        return result;
    }
    _lastDialpadFailure = S_OK;
    _lastLoggedDialpadFailure = S_OK;
    std::array<char, 160> message{};
    (void)sprintf_s(
        message.data(), message.size(),
        "dialpad connected (bluetooth): controls 0x%02X, %u collection(s), buttons diverted 0x%X; wheels %s.",
        _dialpad.Features().reprogControls, _dialpad.PortCount(), _dialpad.DivertedDialButtons(),
        _wheels.Running() ? "through raw input" : "unavailable");
    Log(RedXeLogLevelInfo, "dialpad-connected", message.data());
    return S_OK;
}

void LogiconService::CloseDialpad(HANDLE stopEvent, bool restore) noexcept
{
    if (restore && _dialpad.HasPorts())
    {
        (void)_dialpad.Restore(stopEvent, false);
    }
    _dialpad.Detach();
}

uint64_t LogiconService::SlotSignature(uint32_t slot, const KeyBinding* binding, const FaceOverride& override,
                                       const HostStateCopy& host, uint32_t minuteOfDay) noexcept
{
    uint64_t hash = kFnvOffset;
    HashValue(hash, slot);
    HashValue(hash, _laneKeyPage);
    HashValue(hash, _backgroundRgb);
    HashValue(hash, override.kind);
    HashValue(hash, override.colorRgb);
    const bool iconFont = _renderer.HasIconFont();
    HashValue(hash, iconFont);
    if (!binding)
    {
        return hash;
    }
    HashValue(hash, binding->action);
    HashValue(hash, binding->face);
    HashValue(hash, binding->hasColor);
    HashValue(hash, binding->colorRgb);
    Hash(hash, binding->target.data(), binding->targetBytes);
    Hash(hash, binding->label.data(), binding->labelBytes);
    Hash(hash, binding->icon.data(), binding->iconBytes);
    const bool valid = TargetIsValid(*binding);
    HashValue(hash, valid);
    if (binding->face == KeyFace::Clock)
    {
        HashValue(hash, minuteOfDay);
    }
    if (binding->face == KeyFace::PageIndicator)
    {
        HashValue(hash, host.pageIndex);
        HashValue(hash, host.pageCount);
        Hash(hash, host.pageName.data(), host.pageNameLength * sizeof(wchar_t));
    }
    if (binding->face == KeyFace::Cpu)
    {
        HashValue(hash, _laneSystem.cpuPercent);
    }
    else if (binding->face == KeyFace::Memory)
    {
        HashValue(hash, _laneSystem.memoryPercent);
    }
    else if (binding->face == KeyFace::Gpu)
    {
        HashValue(hash, _laneSystem.gpuPercent);
    }
    if (binding->action == KeyAction::WidgetRaise || binding->action == KeyAction::WidgetToggle ||
        binding->action == KeyAction::PageGoTo)
    {
        HashValue(hash, host.flags & RedXeHostStateRaised);
        HashValue(hash, host.raisedWidgetOrdinal);
        Hash(hash, host.pageId.data(), strnlen_s(host.pageId.data(), host.pageId.size()));
    }
    return hash;
}

HRESULT LogiconService::ComposeSlot(uint32_t slot, const KeyBinding* binding, const FaceOverride& override,
                                    const HostStateCopy& host, uint32_t* tile) noexcept
{
    if (override.kind == OverrideKind::Picture)
    {
        // A generated test picture that needs no fonts: diagonal gradient with a slot-tinted blue channel.
        for (uint32_t y = 0; y < kFaceSize; ++y)
        {
            for (uint32_t x = 0; x < kFaceSize; ++x)
            {
                const uint32_t red = x * 255U / (kFaceSize - 1);
                const uint32_t green = y * 255U / (kFaceSize - 1);
                const uint32_t blue = (slot * 28U) & 0xFFU;
                tile[y * kFaceSize + x] = 0xFF000000U | (red << 16U) | (green << 8U) | blue;
            }
        }
        return S_OK;
    }
    FaceSpec spec{};
    spec.backgroundRgb = _backgroundRgb;
    spec.foregroundRgb = 0xF2F2F2;
    std::array<wchar_t, kMaximumLabelCodePoints * 2 + 1> label{};
    std::array<wchar_t, kMaximumFaceBigCharacters + 1> big{};
    std::array<wchar_t, kMaximumIconBytes + 1> iconPath{};
    if (override.kind == OverrideKind::Color)
    {
        spec.backgroundRgb = override.colorRgb;
    }
    else if (binding)
    {
        if (binding->hasColor)
        {
            spec.backgroundRgb = binding->colorRgb;
        }
        spec.invalid = !TargetIsValid(*binding);
        spec.labelLength = Utf8ToWide(binding->Label(), label.data(), static_cast<uint32_t>(label.size()));
        spec.label = label.data();
        const std::string_view icon = binding->Icon();
        if (icon.starts_with("png:"))
        {
            if (Utf8ToWide(icon.substr(4), iconPath.data(), static_cast<uint32_t>(iconPath.size())) != 0)
            {
                uint32_t width = 0;
                uint32_t height = 0;
                if (SUCCEEDED(_renderer.DecodeImage(iconPath.data(), kFaceIconPixels, _iconScratch.data(),
                                                    static_cast<uint32_t>(_iconScratch.size()), width, height)))
                {
                    spec.image = _iconScratch.data();
                    spec.imageWidth = width;
                    spec.imageHeight = height;
                }
                else
                {
                    spec.icon = FluentGlyphFromName("Photo", 5);
                }
            }
        }
        else if (!icon.empty())
        {
            spec.icon = FluentGlyphFromName(icon.data(), static_cast<uint32_t>(icon.size()));
            if (spec.icon == 0)
            {
                spec.icon = FluentGlyphFromName("Help", 4);
            }
        }
        if (binding->face == KeyFace::Clock)
        {
            SYSTEMTIME local{};
            GetLocalTime(&local);
            const int written = swprintf_s(big.data(), big.size(), L"%02u:%02u", static_cast<unsigned>(local.wHour),
                                           static_cast<unsigned>(local.wMinute));
            spec.big = big.data();
            spec.bigLength = written > 0 ? static_cast<uint32_t>(written) : 0;
        }
        else if (binding->face == KeyFace::PageIndicator)
        {
            const int written =
                swprintf_s(big.data(), big.size(), L"%u/%u", host.pageIndex + 1U, std::max(host.pageCount, 1U));
            spec.big = big.data();
            spec.bigLength = written > 0 ? static_cast<uint32_t>(written) : 0;
            if (spec.labelLength == 0 && host.pageNameLength != 0)
            {
                spec.label = host.pageName.data();
                spec.labelLength = std::min(host.pageNameLength, kMaximumLabelCodePoints);
            }
        }
        else if (IsSystemFace(binding->face))
        {
            // Percentage as the big text, the metric name as the label unless the binding names it.
            const int32_t percent = binding->face == KeyFace::Cpu      ? _laneSystem.cpuPercent
                                    : binding->face == KeyFace::Memory ? _laneSystem.memoryPercent
                                                                       : _laneSystem.gpuPercent;
            const int written = percent >= 0 ? swprintf_s(big.data(), big.size(), L"%d%%", percent)
                                             : swprintf_s(big.data(), big.size(), L"--");
            spec.big = big.data();
            spec.bigLength = written > 0 ? static_cast<uint32_t>(written) : 0;
            if (spec.labelLength == 0)
            {
                const wchar_t* name = binding->face == KeyFace::Cpu      ? L"CPU"
                                      : binding->face == KeyFace::Memory ? L"MEM"
                                                                         : L"GPU";
                spec.label = name;
                spec.labelLength = 3;
            }
        }
        if (!spec.invalid)
        {
            if (binding->action == KeyAction::WidgetRaise || binding->action == KeyAction::WidgetToggle)
            {
                std::string_view pageId;
                uint32_t ordinal = 0;
                if (ParseWidgetTarget(binding->Target(), pageId, ordinal) && (host.flags & RedXeHostStateRaised) != 0 &&
                    host.raisedWidgetOrdinal == ordinal &&
                    (pageId.empty() || pageId == std::string_view(host.pageId.data())))
                {
                    spec.accentRing = true;
                }
            }
            else if (binding->action == KeyAction::PageGoTo &&
                     binding->Target() == std::string_view(host.pageId.data()))
            {
                spec.accentRing = true;
            }
        }
    }
    if (!_renderer.Ready())
    {
        const uint32_t packed = 0xFF000000U | (spec.backgroundRgb & 0x00FFFFFFU);
        std::fill_n(tile, kFacePixels, packed);
        return S_FALSE;
    }
    return _renderer.ComposeKey(spec, tile);
}

HRESULT LogiconService::ComposeAndWrite(HANDLE stopEvent, bool forceAll) noexcept
{
    if (!_grid)
    {
        _grid.reset(new (std::nothrow) uint32_t[kGridPixels]);
        if (!_grid)
        {
            return E_OUTOFMEMORY;
        }
        const uint32_t packed = 0xFF000000U | _backgroundRgb;
        std::fill_n(_grid.get(), kGridPixels, packed);
        forceAll = true;
    }
    if (!_jpeg)
    {
        _jpeg.reset(new (std::nothrow) uint8_t[kMaximumJpegBytes]);
        if (!_jpeg)
        {
            return E_OUTOFMEMORY;
        }
    }
    std::array<FaceOverride, kKeyCount> overrides{};
    {
        const auto guard = wil::AcquireSRWLockShared(&_lock);
        overrides = _overrides;
    }
    const uint32_t minute = MinuteOfDay();
    std::array<bool, kKeyCount> dirty{};
    uint32_t dirtyCount = 0;
    for (uint32_t slot = 0; slot < kKeyCount; ++slot)
    {
        const KeyBinding* binding = _laneSettings.Find(_laneKeyPage, slot);
        const uint64_t signature = SlotSignature(slot, binding, overrides[slot], _laneHost, minute);
        if (forceAll || !_slots[slot].composed || _slots[slot].signature != signature)
        {
            dirty[slot] = true;
            ++dirtyCount;
            _slots[slot].signature = signature;
        }
    }
    if (dirtyCount == 0)
    {
        return S_OK;
    }
    for (uint32_t slot = 0; slot < kKeyCount; ++slot)
    {
        if (!dirty[slot])
        {
            continue;
        }
        const KeyBinding* binding = _laneSettings.Find(_laneKeyPage, slot);
        (void)ComposeSlot(slot, binding, overrides[slot], _laneHost, _tile.data());
        const uint32_t originX = KeyColumn(slot) * (kKeySize + kKeyGap);
        const uint32_t originY = KeyRow(slot) * (kKeySize + kKeyGap);
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        for (uint32_t y = 0; y < kKeySize; ++y)
        {
            std::memcpy(_grid.get() + static_cast<size_t>(originY + y) * kGridSize + originX,
                        _tile.data() + static_cast<size_t>(y) * kKeySize, kKeySize * sizeof(uint32_t));
        }
        _slots[slot].composed = true;
    }
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        ++_faceGeneration;
    }
    if (!_session.Connected() || !_renderer.Ready())
    {
        return S_OK;
    }

    HRESULT result = S_OK;
    if (forceAll || dirtyCount >= 3)
    {
        uint32_t bytes = 0;
        result =
            _renderer.EncodeJpeg(_grid.get(), kGridSize, kGridSize, kGridSize, _jpeg.get(), kMaximumJpegBytes, bytes);
        if (SUCCEEDED(result))
        {
            result = _session.WriteImage(GridRegion(), _jpeg.get(), bytes, false, stopEvent);
        }
    }
    else
    {
        uint32_t remaining = dirtyCount;
        for (uint32_t slot = 0; SUCCEEDED(result) && slot < kKeyCount; ++slot)
        {
            if (!dirty[slot])
            {
                continue;
            }
            --remaining;
            const uint32_t originX = KeyColumn(slot) * (kKeySize + kKeyGap);
            const uint32_t originY = KeyRow(slot) * (kKeySize + kKeyGap);
            uint32_t bytes = 0;
            result = _renderer.EncodeJpeg(_grid.get() + static_cast<size_t>(originY) * kGridSize + originX, kKeySize,
                                          kKeySize, kGridSize, _jpeg.get(), kMaximumJpegBytes, bytes);
            if (SUCCEEDED(result))
            {
                result = _session.WriteImage(KeyRegion(slot), _jpeg.get(), bytes, remaining != 0, stopEvent);
            }
        }
    }
    if (FAILED(result))
    {
        // Re-send everything once the device is back or the next change arrives.
        for (SlotState& slot : _slots)
        {
            slot.composed = false;
        }
        Log(RedXeLogLevelWarning, "face-write-failed", "a key face could not be written to the keypad.", result);
        return result;
    }
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        ++_facesWritten;
    }
    // The reference implementations pause briefly after a burst so the panel does not skip a draw.
    if (stopEvent)
    {
        (void)WaitForSingleObject(stopEvent, kBatchPauseMilliseconds);
    }
    return S_OK;
}

void LogiconService::PublishSnapshot() noexcept
{
    uint64_t visibleHash = kFnvOffset;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        FillSnapshot();
        const MonitorSnapshot& snapshot = _snapshot;
        HashValue(visibleHash, snapshot.laneRunning);
        HashValue(visibleHash, snapshot.connected);
        HashValue(visibleHash, snapshot.synthetic);
        HashValue(visibleHash, snapshot.keys);
        HashValue(visibleHash, snapshot.pageButtons);
        HashValue(visibleHash, snapshot.keyPage);
        HashValue(visibleHash, snapshot.brightness);
        HashValue(visibleHash, snapshot.faceGeneration);
        HashValue(visibleHash, snapshot.facesWritten);
        HashValue(visibleHash, snapshot.actionsRequested);
        HashValue(visibleHash, snapshot.counters.reportsIn);
        HashValue(visibleHash, snapshot.counters.reportsOut);
        HashValue(visibleHash, snapshot.lastFailure);
        HashValue(visibleHash, snapshot.competingWriter);
        HashValue(visibleHash, snapshot.dialpad.connected);
        HashValue(visibleHash, snapshot.dialpad.wheelsListening);
        HashValue(visibleHash, snapshot.dialpad.buttons);
        HashValue(visibleHash, snapshot.dialpad.buttonPresses);
        HashValue(visibleHash, snapshot.dialpad.lastFailure);
        HashValue(visibleHash, snapshot.dialpad.counters.reportsIn);
        HashValue(visibleHash, snapshot.dialpad.wheels);
        HashValue(visibleHash, snapshot.dialpad.wheelSteps);
        HashValue(visibleHash, snapshot.systemFeed);
        HashValue(visibleHash, snapshot.system);
    }
    // A monitor tile redraws only when something it shows changed; RequestFrame is any-thread and coalescing.
    if (visibleHash != _publishedHash)
    {
        _publishedHash = visibleHash;
        if (_host && _monitorAttached.load(std::memory_order_acquire) != 0)
        {
            (void)_host->RequestFrame();
        }
    }
}

void LogiconService::FillSnapshot() noexcept
{
    MonitorSnapshot& snapshot = _snapshot;
    snapshot.laneRunning = _laneRunning.load(std::memory_order_acquire);
    snapshot.deviceAccess = _deviceAccess.load(std::memory_order_acquire);
    snapshot.connected = _session.Connected();
    snapshot.synthetic = _syntheticActive;
    snapshot.rendererReady = _renderer.Ready();
    snapshot.iconFont = _renderer.HasIconFont();
    snapshot.competingWriter = _competingWriter;
    snapshot.keys = _session.Controls().keys;
    snapshot.pageButtons = _session.Controls().pageButtons;
    snapshot.keyPage = _laneKeyPage;
    snapshot.keyPageCount = _laneSettings.KeyPageCount();
    snapshot.brightness = _appliedBrightness != 0 ? _appliedBrightness : _laneSettings.brightness;
    snapshot.portCount = _session.PortCount();
    snapshot.faceGeneration = _faceGeneration;
    snapshot.facesWritten = _facesWritten;
    snapshot.actionsRequested = _actionsRequested;
    snapshot.lastAction = _lastAction;
    snapshot.connectAttempts = _connectAttempts;
    snapshot.lastFailure = _lastConnectFailure;
    snapshot.features = _session.Features();
    snapshot.counters = _session.Counters();
    snapshot.host = _laneHost;
    snapshot.overrides = _overrides;
    for (uint32_t slot = 0; slot < kKeyCount; ++slot)
    {
        const KeyBinding* binding = _laneSettings.Find(_laneKeyPage, slot);
        snapshot.bound[slot] = binding != nullptr;
        snapshot.actions[slot] = binding ? binding->action : KeyAction::None;
        snapshot.faces[slot] = binding ? binding->face : KeyFace::None;
        snapshot.invalid[slot] = binding && !TargetIsValid(*binding);
    }
    snapshot.traceCount = _session.CopyTrace(snapshot.trace.data(), static_cast<uint32_t>(snapshot.trace.size()));
    DialpadSnapshot& dialpad = snapshot.dialpad;
    dialpad.connected = _dialpad.Connected();
    dialpad.wheelsListening = _wheels.Running();
    dialpad.buttons = _dialpad.Controls().dialButtons;
    dialpad.divertedButtons = _dialpad.DivertedDialButtons();
    dialpad.portCount = _dialpad.PortCount();
    dialpad.connectAttempts = _dialpadAttempts;
    dialpad.buttonPresses = _dialButtonPresses;
    dialpad.wheelSteps = _wheelSteps;
    dialpad.lastFailure = _lastDialpadFailure;
    dialpad.features = _dialpad.Features();
    dialpad.counters = _dialpad.Counters();
    dialpad.wheels = _wheels.State();
    dialpad.dialAction = _laneSettings.dialpad.dial;
    dialpad.rollerAction = _laneSettings.dialpad.roller;
    dialpad.traceCount = _dialpad.CopyTrace(dialpad.trace.data(), static_cast<uint32_t>(dialpad.trace.size()));
    snapshot.systemFeed = _systemFeedActive;
    snapshot.system = _systemValues;
}

HRESULT LogiconService::RunDeviceWork(HANDLE stopEvent, HANDLE wakeEvent) noexcept
{
    if (!stopEvent || !wakeEvent)
    {
        return E_POINTER;
    }
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _wakeEvent = wakeEvent;
    }
    _laneRunning.store(true, std::memory_order_release);
    const bool deviceAccess = _deviceAccess.load(std::memory_order_acquire);
    Log(RedXeLogLevelInfo, "lane-started",
        deviceAccess ? "device lane running." : "device lane running (no device access).");
    const HRESULT rendererResult = _renderer.Initialize();
    if (FAILED(rendererResult))
    {
        Log(RedXeLogLevelWarning, "faces-unavailable", "DirectWrite or WIC is unavailable; key faces are disabled.",
            rendererResult);
    }
    HotplugWatcher hotplug;
    if (deviceAccess)
    {
        const HRESULT watching = hotplug.Start(wakeEvent);
        if (FAILED(watching))
        {
            Log(RedXeLogLevelWarning, "hotplug-unavailable", "device arrival notifications are unavailable.", watching);
        }
        // The dialpad's dial and roller are the wheels of a mouse collection Windows keeps exclusive; Raw Input is
        // the only non-exclusive reader, and it needs a window on this thread.
        const HRESULT listening = _wheels.Start(kVendorId, kDialpadProductId);
        if (FAILED(listening))
        {
            Log(RedXeLogLevelWarning, "rawinput-unavailable", "the dialpad's wheels cannot be read (raw input).",
                listening);
        }
    }

    // Discovery state per device: pending until the device is found or absent, then again on every hotplug change.
    struct Link final
    {
        bool discoverPending = false;
        uint64_t retryDue = 0;
        uint32_t reconnectStep = 0;

        void Rediscover() noexcept
        {
            discoverPending = true;
            retryDue = 0;
            reconnectStep = 0;
        }

        // Applies an open attempt's outcome: found, absent (wait for arrival), or failed (bounded backoff).
        void Note(HRESULT opened, uint64_t now) noexcept
        {
            if (SUCCEEDED(opened))
            {
                discoverPending = false;
                reconnectStep = 0;
                retryDue = 0;
            }
            else if (opened == HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED) ||
                     opened == HRESULT_FROM_WIN32(ERROR_CANCELLED))
            {
                // Not present: wait for a hotplug notification rather than polling.
                discoverPending = false;
            }
            else if (reconnectStep < kReconnectBackoffSteps)
            {
                retryDue = now + (1000ULL << reconnectStep);
                ++reconnectStep;
            }
            else
            {
                discoverPending = false;
            }
        }
    };
    Link keypadLink{};
    Link dialpadLink{};
    keypadLink.discoverPending = deviceAccess;
    dialpadLink.discoverPending = deviceAccess;
    bool forceAll = false;
    bool laneFacesDirty = true;
    ControlState virtualControls{};
    std::array<HANDLE, 2 + 2 * kMaximumHidCollections> handles{};
    for (;;)
    {
        bool syntheticWanted = false;
        {
            const auto guard = wil::AcquireSRWLockShared(&_lock);
            syntheticWanted = _syntheticRequested;
        }
        if (syntheticWanted != _syntheticActive)
        {
            CloseDevice(stopEvent, true);
            _syntheticActive = syntheticWanted;
            _synthetic.Reset();
            keypadLink.Rediscover();
        }

        // Adopt new settings and requests first so discovery below connects with the current bindings.
        std::array<InjectedControl, kMaximumInjectedControls> injected{};
        uint32_t injectedCount = 0;
        uint32_t brightnessRequest = 0;
        {
            const auto guard = wil::AcquireSRWLockExclusive(&_lock);
            if (_laneSettingsGeneration != _settingsGeneration)
            {
                _laneSettings = _settings;
                _laneSettingsGeneration = _settingsGeneration;
                _laneKeyPage = std::min(_laneKeyPage, _laneSettings.KeyPageCount() - 1U);
                // A new document wins over a monitor/test brightness override, and restarts detent folding.
                _brightnessOverride = 0;
                _dialAccumulator = 0;
                _rollerAccumulator = 0;
                laneFacesDirty = true;
            }
            if (_laneHostGeneration != _hostGeneration)
            {
                _laneHost = _hostState;
                _laneHostGeneration = _hostGeneration;
                laneFacesDirty = true;
            }
            if (_laneOverrideGeneration != _overrideGeneration)
            {
                _laneOverrideGeneration = _overrideGeneration;
                laneFacesDirty = true;
            }
            if (_laneSystemGeneration != _systemGeneration)
            {
                _laneSystem = _systemValues;
                _laneSystemGeneration = _systemGeneration;
                laneFacesDirty = true;
            }
            if (_laneKeyPage != _keyPage)
            {
                _laneKeyPage = std::min(_keyPage, _laneSettings.KeyPageCount() - 1U);
                _keyPage = _laneKeyPage;
                laneFacesDirty = true;
            }
            injected = _injected;
            injectedCount = _injectedCount;
            _injectedCount = 0;
            brightnessRequest = _brightnessRequest;
            _brightnessRequest = 0;
        }

        if (hotplug.TakeChange())
        {
            // An interface came or went: look again for whatever is not open, and re-resolve raw-input handles.
            if (!_session.HasPorts())
            {
                keypadLink.Rediscover();
            }
            if (!_dialpad.HasPorts())
            {
                dialpadLink.Rediscover();
            }
            _wheels.ForgetDevices();
        }
        if (_session.HasPorts() && _session.Disconnected())
        {
            Log(RedXeLogLevelInfo, "device-disconnected", "the keypad went away.");
            CloseDevice(stopEvent, false);
            keypadLink.Rediscover();
        }
        if (_dialpad.HasPorts() && _dialpad.Disconnected())
        {
            Log(RedXeLogLevelInfo, "dialpad-disconnected", "the dialpad went away.");
            CloseDialpad(stopEvent, false);
            dialpadLink.Rediscover();
        }
        uint64_t now = GetTickCount64();
        if (!_session.HasPorts() && keypadLink.discoverPending && now >= keypadLink.retryDue &&
            (deviceAccess || _syntheticActive))
        {
            const HRESULT opened = TryOpenDevice(stopEvent);
            forceAll = forceAll || SUCCEEDED(opened);
            keypadLink.Note(opened, now);
        }
        if (!_dialpad.HasPorts() && dialpadLink.discoverPending && now >= dialpadLink.retryDue && deviceAccess)
        {
            dialpadLink.Note(TryOpenDialpad(stopEvent), now);
        }

        // A new document may bind different dialpad buttons: restore the current diversion and reconnect at once.
        if (_dialpad.Connected() && _dialpad.DivertedDialButtons() != BoundDialButtons(_laneSettings))
        {
            CloseDialpad(stopEvent, true);
            dialpadLink.Rediscover();
            if (deviceAccess)
            {
                dialpadLink.Note(TryOpenDialpad(stopEvent), GetTickCount64());
            }
        }

        if (brightnessRequest != 0)
        {
            _brightnessOverride = brightnessRequest;
        }
        const uint32_t brightness = _brightnessOverride != 0 ? _brightnessOverride : _laneSettings.brightness;
        if (_session.Connected() && brightness != _appliedBrightness)
        {
            if (SUCCEEDED(_session.SetBrightness(brightness, stopEvent)) || _session.Features().brightness == 0)
            {
                _appliedBrightness = brightness;
            }
        }

        for (uint32_t index = 0; index < injectedCount; ++index)
        {
            const InjectedControl& control = injected[index];
            ControlState next = virtualControls;
            const uint32_t bit = 1U << control.index;
            uint32_t& mask = control.kind == kInjectKindKey          ? next.keys
                             : control.kind == kInjectKindPageButton ? next.pageButtons
                                                                     : next.dialButtons;
            mask = control.down ? (mask | bit) : (mask & ~bit);
            ControlEdges edges{};
            edges.keysDown = next.keys & ~virtualControls.keys;
            edges.pageButtonsDown = next.pageButtons & ~virtualControls.pageButtons;
            edges.dialButtonsDown = next.dialButtons & ~virtualControls.dialButtons;
            const uint32_t previousPage = _laneKeyPage;
            DispatchEdges(edges);
            virtualControls = next;
            laneFacesDirty = laneFacesDirty || previousPage != _laneKeyPage;
        }

        const uint32_t minute = MinuteOfDay();
        if (minute != _laneMinute)
        {
            _laneMinute = minute;
            laneFacesDirty = laneFacesDirty || ClockFaceVisible();
        }

        const bool wantFaces = _session.Connected() || _monitorAttached.load(std::memory_order_acquire) != 0;
        if (wantFaces && (laneFacesDirty || forceAll))
        {
            (void)ComposeAndWrite(stopEvent, forceAll);
            forceAll = false;
            laneFacesDirty = false;
        }
        PublishSnapshot();

        now = GetTickCount64();
        DWORD timeout = INFINITE;
        for (const Link* link : {&keypadLink, &dialpadLink})
        {
            if (link->discoverPending && link->retryDue > now)
            {
                timeout = std::min(timeout, static_cast<DWORD>(std::min<uint64_t>(link->retryDue - now, 60'000ULL)));
            }
        }
        if (wantFaces && ClockFaceVisible())
        {
            SYSTEMTIME local{};
            GetLocalTime(&local);
            const DWORD untilMinute = (60U - local.wSecond) * 1000U - local.wMilliseconds + 50U;
            timeout = std::min(timeout, untilMinute);
        }
        uint32_t handleCount = 0;
        handles[handleCount++] = stopEvent;
        handles[handleCount++] = wakeEvent;
        handleCount +=
            _session.ReadEvents(handles.data() + handleCount, static_cast<uint32_t>(handles.size()) - handleCount);
        handleCount +=
            _dialpad.ReadEvents(handles.data() + handleCount, static_cast<uint32_t>(handles.size()) - handleCount);
        // Message-aware so the raw-input window on this thread is served; without a window the queue stays empty.
        const DWORD waited =
            MsgWaitForMultipleObjectsEx(handleCount, handles.data(), timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (waited == WAIT_OBJECT_0)
        {
            break;
        }
        if (waited == WAIT_FAILED)
        {
            Log(RedXeLogLevelError, "lane-wait-failed", "the device lane wait failed.",
                HRESULT_FROM_WIN32(GetLastError()));
            break;
        }
        if (waited == WAIT_OBJECT_0 + handleCount)
        {
            // Window messages: raw-input packets fold into the wheel state and run the dial and roller actions per
            // detent; the snapshot publishes on the next turn.
            if (_wheels.Pump())
            {
                const WheelDeltas deltas = _wheels.TakeDeltas();
                const uint32_t previousPage = _laneKeyPage;
                DispatchWheel(_laneSettings.dialpad.dial, _dialAccumulator, deltas.dial);
                DispatchWheel(_laneSettings.dialpad.roller, _rollerAccumulator, deltas.roller);
                laneFacesDirty = laneFacesDirty || previousPage != _laneKeyPage;
            }
        }
        else if (waited >= WAIT_OBJECT_0 + 2 && waited < WAIT_OBJECT_0 + handleCount)
        {
            bool changed = false;
            ControlEdges edges{};
            HRESULT pumped = _session.Pump(changed, edges);
            if (FAILED(pumped) && !IsDeviceGone(pumped))
            {
                Log(RedXeLogLevelWarning, "input-read-failed", "reading the keypad failed.", pumped);
            }
            if (_dialpad.HasPorts())
            {
                bool dialChanged = false;
                ControlEdges dialEdges{};
                pumped = _dialpad.Pump(dialChanged, dialEdges);
                if (FAILED(pumped) && !IsDeviceGone(pumped))
                {
                    Log(RedXeLogLevelWarning, "input-read-failed", "reading the dialpad failed.", pumped);
                }
                edges.dialButtonsDown |= dialEdges.dialButtonsDown;
            }
            if (edges.keysDown != 0 || edges.pageButtonsDown != 0 || edges.dialButtonsDown != 0)
            {
                const uint32_t previousPage = _laneKeyPage;
                DispatchEdges(edges);
                laneFacesDirty = laneFacesDirty || previousPage != _laneKeyPage;
            }
        }
    }

    // Leave the devices as found: the stop event is already signaled, so restore runs with plain timeouts.
    CloseDevice(nullptr, true);
    CloseDialpad(nullptr, true);
    _wheels.Stop();
    hotplug.Stop();
    _renderer.Reset();
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _wakeEvent = nullptr;
        _grid.reset();
        _jpeg.reset();
    }
    for (SlotState& slot : _slots)
    {
        slot.composed = false;
    }
    _laneRunning.store(false, std::memory_order_release);
    PublishSnapshot();
    Log(RedXeLogLevelInfo, "lane-stopped", "device lane stopped.");
    return S_OK;
}
} // namespace Logicon
