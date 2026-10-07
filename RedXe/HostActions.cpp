#include "HostActions.h"

#include "Actions/ActionTargets.h"
#include "Actions/WindowSelector.h"
#include "HostActionCatalog.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <powrprof.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace HostActions
{
namespace
{
using namespace RedXeActions;

// One input pressed by keys.down or mouse.down. `due` is its release deadline, or the next retry of a release that
// SendInput refused. `expired` marks a hold the deadline released, so the matching `up` that follows injects nothing.
struct Hold final
{
    ULONGLONG due = 0;
    uint32_t failedReleases = 0;
    bool held = false;
    bool deviceAccess = false;
    bool expired = false;
};

// UI-thread state: the main window, the log, the discovered XENEON, the counters, and whatever keys.down / mouse.down
// left pressed. The chord and button stay recorded after their release so an expired hold's own `up` can be
// recognized.
HWND g_hostWindow = nullptr;
IRedXeHost* g_log = nullptr;
RECT g_xeneonBounds{};
bool g_xeneonFound = false;
Counters g_counters{};
KeyChord g_heldChord{};
Hold g_chord{};
uint32_t g_heldMouseFlags = 0;
DWORD g_heldMouseData = 0;
Hold g_mouse{};
#if defined(REDXE_HOST_PLUGIN_TESTS)
HRESULT g_injectionFailure = S_OK;
#endif

void CountExecution(const RedXeActionDescriptor& descriptor) noexcept
{
    ++g_counters.executed;
    strncpy_s(g_counters.lastAction.data(), g_counters.lastAction.size(), descriptor.name, _TRUNCATE);
}

[[nodiscard]] bool ContainsIgnoreCase(const wchar_t* haystack, const wchar_t* needle) noexcept
{
    const size_t needleLength = wcslen(needle);
    const size_t haystackLength = wcslen(haystack);
    if (needleLength == 0 || needleLength > haystackLength)
    {
        return false;
    }
    for (size_t offset = 0; offset + needleLength <= haystackLength; ++offset)
    {
        if (CompareStringOrdinal(haystack + offset, static_cast<int>(needleLength), needle,
                                 static_cast<int>(needleLength), TRUE) == CSTR_EQUAL)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool Utf8ToWide(std::string_view text, wchar_t* buffer, int capacity) noexcept
{
    if (!buffer || capacity <= 0)
    {
        return false;
    }
    buffer[0] = L'\0';
    if (text.empty())
    {
        return true;
    }
    const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                            buffer, capacity - 1);
    if (written <= 0)
    {
        return false;
    }
    buffer[written] = L'\0';
    return true;
}

// Sends at most kMaximumInputBatch records per call and never sleeps between batches.
[[nodiscard]] HRESULT Inject(const INPUT* inputs, uint32_t count, bool deviceAccess) noexcept
{
    g_counters.injectedInputs += count;
#if defined(REDXE_HOST_PLUGIN_TESTS)
    if (FAILED(g_injectionFailure))
    {
        return g_injectionFailure;
    }
#endif
    if (!deviceAccess)
    {
        return S_OK;
    }
    uint32_t offset = 0;
    while (offset < count)
    {
        const uint32_t batch = std::min(kMaximumInputBatch, count - offset);
        const UINT sent = SendInput(batch, const_cast<INPUT*>(inputs + offset), sizeof(INPUT));
        if (sent != batch)
        {
            return HRESULT_FROM_WIN32(GetLastError() == 0 ? ERROR_ACCESS_DENIED : GetLastError());
        }
        offset += batch;
    }
    return S_OK;
}

void FillKey(INPUT& input, uint16_t virtualKey, bool extended, bool up) noexcept
{
    input = INPUT{};
    input.type = INPUT_KEYBOARD;
    const UINT scan = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);
    if (scan != 0)
    {
        input.ki.wScan = static_cast<WORD>(scan);
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
    }
    else
    {
        input.ki.wVk = virtualKey;
    }
    if (extended)
    {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    if (up)
    {
        input.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
}

// Down or up records for one chord: modifiers then key when pressing, key then modifiers when releasing.
[[nodiscard]] uint32_t FillChord(const KeyChord& chord, bool up, INPUT* inputs) noexcept
{
    struct Modifier final
    {
        uint32_t flag;
        uint16_t virtualKey;
    };
    constexpr std::array<Modifier, 4> kModifiers{{
        {ChordModifierControl, static_cast<uint16_t>(VK_CONTROL)},
        {ChordModifierShift, static_cast<uint16_t>(VK_SHIFT)},
        {ChordModifierAlt, static_cast<uint16_t>(VK_MENU)},
        {ChordModifierWin, static_cast<uint16_t>(VK_LWIN)},
    }};
    uint32_t count = 0;
    if (up)
    {
        FillKey(inputs[count++], chord.virtualKey, chord.extended, true);
    }
    for (const Modifier& modifier : kModifiers)
    {
        if ((chord.modifiers & modifier.flag) != 0)
        {
            FillKey(inputs[count++], modifier.virtualKey, modifier.virtualKey == VK_LWIN, up);
        }
    }
    if (!up)
    {
        FillKey(inputs[count++], chord.virtualKey, chord.extended, false);
    }
    return count;
}

void ArmHeldTimer() noexcept
{
    if (!g_hostWindow)
    {
        return;
    }
    (void)KillTimer(g_hostWindow, kHeldInputTimerId);
    if (!g_chord.held && !g_mouse.held)
    {
        return;
    }
    const ULONGLONG chordDue = g_chord.held ? g_chord.due : ULLONG_MAX;
    const ULONGLONG mouseDue = g_mouse.held ? g_mouse.due : ULLONG_MAX;
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG due = std::min(chordDue, mouseDue);
    const UINT delay = static_cast<UINT>(std::max<ULONGLONG>(1, due > now ? due - now : 1));
    (void)SetTimer(g_hostWindow, kHeldInputTimerId, delay, nullptr);
}

void BeginHold(Hold& hold, bool deviceAccess) noexcept
{
    hold = Hold{};
    hold.due = GetTickCount64() + kHeldReleaseMilliseconds;
    hold.held = true;
    hold.deviceAccess = deviceAccess;
}

// Settles one release attempt. SendInput refuses input while a UAC prompt or the lock screen owns the input desktop;
// such a hold stays tracked and is retried kHeldReleaseRetryMilliseconds later (never at the timer's minimum
// period), so an injected modifier is not left down for every application. `retry` false (shutdown), or the last
// allowed attempt, stops tracking it instead.
[[nodiscard]] HRESULT SettleRelease(Hold& hold, HRESULT result, bool expired, bool retry) noexcept
{
    if (FAILED(result) && retry && ++hold.failedReleases < kMaximumHeldReleaseAttempts)
    {
        if (hold.failedReleases == 1)
        {
            (void)RedXeHostLog(g_log, RedXeLogLevelWarning, nullptr, nullptr, "held-release-failed",
                               "a held key or button could not be released; RedXe retries the release.", result);
        }
        hold.due = GetTickCount64() + kHeldReleaseRetryMilliseconds;
        return result;
    }
    if (FAILED(result))
    {
        (void)RedXeHostLog(g_log, RedXeLogLevelWarning, nullptr, nullptr, "held-release-abandoned",
                           "a held key or button could not be released and is no longer tracked.", result);
    }
    else
    {
        ++g_counters.heldReleases;
    }
    hold.held = false;
    hold.expired = expired && SUCCEEDED(result);
    hold.failedReleases = 0;
    return result;
}

// `expired` is true when the deadline (or a retry the timer scheduled) releases the hold rather than an action.
[[nodiscard]] HRESULT ReleaseChord(bool expired, bool retry) noexcept
{
    if (!g_chord.held)
    {
        return S_FALSE;
    }
    std::array<INPUT, 5> inputs{};
    const uint32_t count = FillChord(g_heldChord, true, inputs.data());
    return SettleRelease(g_chord, Inject(inputs.data(), count, g_chord.deviceAccess), expired, retry);
}

[[nodiscard]] HRESULT ReleaseMouse(bool expired, bool retry) noexcept
{
    if (!g_mouse.held)
    {
        return S_FALSE;
    }
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = g_heldMouseFlags;
    input.mi.mouseData = g_heldMouseData;
    return SettleRelease(g_mouse, Inject(&input, 1, g_mouse.deviceAccess), expired, retry);
}

void ExpireHeld() noexcept
{
    const ULONGLONG now = GetTickCount64();
    if (g_chord.held && now >= g_chord.due)
    {
        (void)ReleaseChord(true, true);
    }
    if (g_mouse.held && now >= g_mouse.due)
    {
        (void)ReleaseMouse(true, true);
    }
    ArmHeldTimer();
}

[[nodiscard]] bool SameChord(const KeyChord& left, const KeyChord& right) noexcept
{
    return left.modifiers == right.modifiers && left.virtualKey == right.virtualKey && left.extended == right.extended;
}

[[nodiscard]] HRESULT PressChords(const ChordSequence& sequence, bool deviceAccess) noexcept
{
    // At most 8 chords × (4 modifiers + 1 key) × down/up = 80 records.
    std::array<INPUT, kMaximumChords * 10> inputs{};
    uint32_t count = 0;
    for (uint32_t index = 0; index < sequence.count; ++index)
    {
        count += FillChord(sequence.chords[index], false, inputs.data() + count);
        count += FillChord(sequence.chords[index], true, inputs.data() + count);
    }
    return Inject(inputs.data(), count, deviceAccess);
}

[[nodiscard]] HRESULT TypeText(std::string_view text, bool deviceAccess) noexcept
{
    std::array<wchar_t, kRedXeMaximumActionTargetBytes + 1> wide{};
    if (!Utf8ToWide(text, wide.data(), static_cast<int>(wide.size())))
    {
        return E_INVALIDARG;
    }
    std::array<INPUT, kMaximumInputBatch> inputs{};
    uint32_t count = 0;
    HRESULT result = S_OK;
    for (const wchar_t* cursor = wide.data(); *cursor != L'\0' && SUCCEEDED(result); ++cursor)
    {
        for (uint32_t phase = 0; phase < 2; ++phase)
        {
            INPUT& input = inputs[count++];
            input = INPUT{};
            input.type = INPUT_KEYBOARD;
            input.ki.wScan = static_cast<WORD>(*cursor);
            input.ki.dwFlags = KEYEVENTF_UNICODE | (phase == 1 ? KEYEVENTF_KEYUP : 0U);
        }
        if (count == inputs.size())
        {
            result = Inject(inputs.data(), count, deviceAccess);
            count = 0;
        }
    }
    if (SUCCEEDED(result) && count != 0)
    {
        result = Inject(inputs.data(), count, deviceAccess);
    }
    return result;
}

[[nodiscard]] HRESULT TapKey(uint16_t virtualKey, bool extended, bool deviceAccess) noexcept
{
    std::array<INPUT, 2> inputs{};
    FillKey(inputs[0], virtualKey, extended, false);
    FillKey(inputs[1], virtualKey, extended, true);
    return Inject(inputs.data(), 2, deviceAccess);
}

// Hands a validated, counted launch to the launch worker; S_FALSE once it is queued.
[[nodiscard]] HRESULT QueueLaunch(LaunchWorker::Request& request, std::string_view verb,
                                  LaunchWorker* launches) noexcept
{
    if (!launches)
    {
        return E_NOT_VALID_STATE;
    }
    (void)_snprintf_s(request.subject.data(), request.subject.size(), _TRUNCATE, "action \"system.%.*s\"",
                      static_cast<int>(verb.size()), verb.data());
    const HRESULT queued = launches->Enqueue(request);
    return SUCCEEDED(queued) ? S_FALSE : queued;
}

[[nodiscard]] HRESULT Launch(std::string_view verb, std::string_view target, bool deviceAccess,
                             LaunchWorker* launches) noexcept
{
    LaunchWorker::Request request{};
    if (!IsPathOrUri(target) || !Utf8ToWide(target, request.file.data(), static_cast<int>(request.file.size())))
    {
        return E_INVALIDARG;
    }
    ++g_counters.launches;
    if (!deviceAccess)
    {
        return S_FALSE;
    }
    // A file launches with its own directory as working directory, as Launcher always did; the worker probes it.
    request.fileFolder = IsAbsolutePath(target);
    return QueueLaunch(request, verb, launches);
}

[[nodiscard]] HRESULT Run(std::string_view verb, std::string_view target, bool deviceAccess,
                          LaunchWorker* launches) noexcept
{
    CommandLine parsed{};
    if (!ParseCommandLine(target, parsed))
    {
        return E_INVALIDARG;
    }
    LaunchWorker::Request request{};
    request.kind = LaunchWorker::Kind::Process;
    std::array<wchar_t, kRedXeMaximumActionTargetBytes + 1> arguments{};
    if (!Utf8ToWide(parsed.executable, request.file.data(), static_cast<int>(request.file.size())) ||
        !Utf8ToWide(parsed.arguments, arguments.data(), static_cast<int>(arguments.size())))
    {
        return E_INVALIDARG;
    }
    // CreateProcessW wants the executable quoted in the mutable command line so a path with spaces stays one token.
    swprintf_s(request.commandLine.data(), request.commandLine.size(), L"\"%s\"%s%s", request.file.data(),
               arguments[0] != L'\0' ? L" " : L"", arguments.data());
    request.directory = request.file;
    wchar_t* slash = wcsrchr(request.directory.data(), L'\\');
    if (slash)
    {
        *slash = L'\0';
    }
    else
    {
        request.directory[0] = L'\0';
    }
    ++g_counters.processes;
    if (!deviceAccess)
    {
        return S_FALSE;
    }
    return QueueLaunch(request, verb, launches);
}

[[nodiscard]] HRESULT EnableShutdownPrivilege() noexcept
{
    wil::unique_handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, token.addressof()))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (!AdjustTokenPrivileges(token.get(), FALSE, &privileges, 0, nullptr, nullptr))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return GetLastError() == ERROR_NOT_ALL_ASSIGNED ? E_ACCESSDENIED : S_OK;
}

[[nodiscard]] HRESULT Shutdown(std::string_view target, bool reboot, bool deviceAccess) noexcept
{
    NowOrSeconds delay{};
    if (!ParseNowOrSeconds(target, 0, 3600, delay))
    {
        return E_INVALIDARG;
    }
    ++g_counters.powerRequests;
    if (!deviceAccess)
    {
        return S_OK;
    }
    const HRESULT privilege = EnableShutdownPrivilege();
    if (FAILED(privilege))
    {
        return privilege;
    }
    if (!InitiateSystemShutdownExW(nullptr, nullptr, delay.seconds, TRUE, reboot ? TRUE : FALSE,
                                   SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

[[nodiscard]] HRESULT CloseProcessWindows(std::string_view target, bool deviceAccess) noexcept
{
    WindowSelector selector{};
    if (!ParseWindowSelector(target, selector))
    {
        return E_INVALIDARG;
    }
    if (!deviceAccess)
    {
        return S_OK;
    }
    SelectedWindows selected{};
    if (!SelectWindows(selector, selector.kind == WindowSelector::Kind::Executable, selected))
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    for (uint32_t index = 0; index < selected.count; ++index)
    {
        (void)PostMessageW(selected.windows[index], WM_CLOSE, 0, 0);
    }
    return S_OK;
}

[[nodiscard]] bool ParsePowerPlan(std::string_view target, GUID& plan) noexcept
{
    if (target == "balanced")
    {
        plan = GUID_TYPICAL_POWER_SAVINGS;
        return true;
    }
    if (target == "highPerformance")
    {
        plan = GUID_MIN_POWER_SAVINGS;
        return true;
    }
    if (target == "powerSaver")
    {
        plan = GUID_MAX_POWER_SAVINGS;
        return true;
    }
    // A scheme GUID with or without braces.
    std::array<wchar_t, 40> wide{};
    std::string_view text = target;
    if (text.size() == 38 && text.front() == '{' && text.back() == '}')
    {
        text = text.substr(1, 36);
    }
    if (text.size() != 36)
    {
        return false;
    }
    wide[0] = L'{';
    for (size_t index = 0; index < 36; ++index)
    {
        wide[index + 1] = static_cast<wchar_t>(text[index]);
    }
    wide[37] = L'}';
    return SUCCEEDED(IIDFromString(wide.data(), &plan));
}

[[nodiscard]] HRESULT SetPowerPlan(std::string_view target, bool deviceAccess) noexcept
{
    GUID plan{};
    if (!ParsePowerPlan(target, plan))
    {
        return E_INVALIDARG;
    }
    ++g_counters.powerRequests;
    if (!deviceAccess)
    {
        return S_OK;
    }
    const DWORD result = PowerSetActiveScheme(nullptr, &plan);
    return result == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(result);
}

[[nodiscard]] HRESULT SetTheme(std::string_view target, bool deviceAccess) noexcept
{
    uint32_t option = 0;
    if (!ParseEnum(target, "light|dark|toggle", option))
    {
        return E_INVALIDARG;
    }
    if (!deviceAccess)
    {
        return S_OK;
    }
    wil::unique_hkey key;
    LSTATUS status =
        RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                      KEY_QUERY_VALUE | KEY_SET_VALUE, key.addressof());
    if (status != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(status);
    }
    DWORD light = 1;
    if (option == 2)
    {
        DWORD size = sizeof(light);
        DWORD type = REG_DWORD;
        if (RegQueryValueExW(key.get(), L"AppsUseLightTheme", nullptr, &type, reinterpret_cast<BYTE*>(&light), &size) !=
            ERROR_SUCCESS)
        {
            light = 1;
        }
        light = light != 0 ? 0 : 1;
    }
    else
    {
        light = option == 0 ? 1 : 0;
    }
    for (const wchar_t* name : {L"AppsUseLightTheme", L"SystemUsesLightTheme"})
    {
        status = RegSetValueExW(key.get(), name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&light), sizeof(light));
        if (status != ERROR_SUCCESS)
        {
            return HRESULT_FROM_WIN32(status);
        }
    }
    // SendNotifyMessage returns without waiting for every top-level window to handle the broadcast.
    (void)SendNotifyMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
    return S_OK;
}

[[nodiscard]] HRESULT LaunchTaskManager(std::string_view verb, bool deviceAccess, LaunchWorker* launches) noexcept
{
    LaunchWorker::Request request{};
    const UINT length = GetSystemDirectoryW(request.file.data(), static_cast<UINT>(request.file.size()));
    if (length == 0 || length >= request.file.size())
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (wcscat_s(request.file.data(), request.file.size(), L"\\Taskmgr.exe") != 0)
    {
        return E_FAIL;
    }
    ++g_counters.launches;
    if (!deviceAccess)
    {
        return S_FALSE;
    }
    return QueueLaunch(request, verb, launches);
}

[[nodiscard]] HRESULT SwitchLayout(std::string_view target, bool deviceAccess) noexcept
{
    std::array<wchar_t, 86> wide{};
    if (target.empty() || target.size() > 84 || !Utf8ToWide(target, wide.data(), static_cast<int>(wide.size())))
    {
        return E_INVALIDARG;
    }
    std::array<wchar_t, KL_NAMELENGTH> klid{};
    bool allHex = target.size() == 8;
    for (const char character : target)
    {
        allHex = allHex && ((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
                            (character >= 'A' && character <= 'F'));
    }
    if (allHex)
    {
        wcscpy_s(klid.data(), klid.size(), wide.data());
    }
    else
    {
        const LCID locale = LocaleNameToLCID(wide.data(), 0);
        if (locale == 0)
        {
            return E_INVALIDARG;
        }
        swprintf_s(klid.data(), klid.size(), L"%08X", static_cast<unsigned>(locale));
    }
    if (!deviceAccess)
    {
        return S_OK;
    }
    const HKL layout = LoadKeyboardLayoutW(klid.data(), KLF_ACTIVATE);
    if (!layout)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const HWND foreground = GetForegroundWindow();
    if (foreground)
    {
        (void)PostMessageW(foreground, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(layout));
    }
    return S_OK;
}

struct MonitorSearch final
{
    const MonitorSelector* selector = nullptr;
    HMONITOR xeneon = nullptr;
    uint32_t index = 0;
    // The SecondaryMonitorRank of the display `rectangle` holds for a `secondary` selector.
    uint32_t secondaryRank = 0;
    RECT rectangle{};
    bool found = false;
};

BOOL CALLBACK EnumerateMonitors(HMONITOR monitor, HDC, LPRECT, LPARAM parameter) noexcept
{
    MonitorSearch& search = *reinterpret_cast<MonitorSearch*>(parameter);
    ++search.index;
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
    {
        return TRUE;
    }
    bool matched = false;
    switch (search.selector->kind)
    {
    case MonitorSelector::Kind::Primary:
        matched = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        break;
    case MonitorSelector::Kind::Secondary:
    {
        // The first display of the highest rank: a XENEON is kept only until another display that is not the
        // primary turns up, which ends the search.
        const uint32_t rank = SecondaryMonitorRank((info.dwFlags & MONITORINFOF_PRIMARY) != 0,
                                                   g_xeneonFound && EqualRect(&info.rcMonitor, &g_xeneonBounds));
        if (rank <= search.secondaryRank)
        {
            return TRUE;
        }
        search.secondaryRank = rank;
        search.rectangle = info.rcMonitor;
        search.found = true;
        return rank < kSecondaryMonitorTopRank ? TRUE : FALSE;
    }
    case MonitorSelector::Kind::Xeneon:
        matched = monitor == search.xeneon;
        break;
    case MonitorSelector::Kind::Index:
        matched = search.index == search.selector->index;
        break;
    case MonitorSelector::Kind::Name:
    {
        std::array<wchar_t, 129> needle{};
        matched = Utf8ToWide(search.selector->name, needle.data(), static_cast<int>(needle.size())) &&
                  ContainsIgnoreCase(info.szDevice, needle.data());
        break;
    }
    default:
        break;
    }
    if (!matched)
    {
        return TRUE;
    }
    search.rectangle = info.rcMonitor;
    search.found = true;
    return FALSE;
}

[[nodiscard]] bool ResolveMonitor(const MonitorSelector& selector, RECT& rectangle) noexcept
{
    MonitorSearch search{};
    search.selector = &selector;
    search.xeneon = g_hostWindow ? MonitorFromWindow(g_hostWindow, MONITOR_DEFAULTTOPRIMARY) : nullptr;
    if (selector.kind == MonitorSelector::Kind::Xeneon && !search.xeneon)
    {
        return false;
    }
    (void)EnumDisplayMonitors(nullptr, nullptr, EnumerateMonitors, reinterpret_cast<LPARAM>(&search));
    if (search.found)
    {
        rectangle = search.rectangle;
    }
    return search.found;
}

[[nodiscard]] HRESULT MoveCursor(std::string_view target, bool deviceAccess) noexcept
{
    Point point{};
    if (!ParsePoint(target, point))
    {
        return E_INVALIDARG;
    }
    INPUT input{};
    input.type = INPUT_MOUSE;
    if (point.relative)
    {
        input.mi.dx = point.x;
        input.mi.dy = point.y;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
    }
    else
    {
        RECT area{};
        if (point.hasMonitor || point.center)
        {
            MonitorSelector selector = point.monitor;
            if (!point.hasMonitor)
            {
                selector.kind = MonitorSelector::Kind::Primary;
            }
            if (!deviceAccess)
            {
                area = RECT{0, 0, 1920, 1080};
            }
            else if (!ResolveMonitor(selector, area))
            {
                return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            }
        }
        int32_t x = point.x;
        int32_t y = point.y;
        if (point.center)
        {
            x = area.left + (area.right - area.left) / 2;
            y = area.top + (area.bottom - area.top) / 2;
        }
        else if (point.hasMonitor)
        {
            x += area.left;
            y += area.top;
        }
        const int virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int virtualWidth = std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
        const int virtualHeight = std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
        input.mi.dx = static_cast<LONG>((static_cast<int64_t>(x - virtualLeft) * 65535) / virtualWidth);
        input.mi.dy = static_cast<LONG>((static_cast<int64_t>(y - virtualTop) * 65535) / virtualHeight);
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    }
    return Inject(&input, 1, deviceAccess);
}

[[nodiscard]] bool ButtonFlags(std::string_view target, uint32_t& down, uint32_t& up, DWORD& data) noexcept
{
    uint32_t index = 0;
    if (!ParseEnum(target, "left|right|middle|x1|x2", index))
    {
        return false;
    }
    data = 0;
    switch (index)
    {
    case 0:
        down = MOUSEEVENTF_LEFTDOWN;
        up = MOUSEEVENTF_LEFTUP;
        break;
    case 1:
        down = MOUSEEVENTF_RIGHTDOWN;
        up = MOUSEEVENTF_RIGHTUP;
        break;
    case 2:
        down = MOUSEEVENTF_MIDDLEDOWN;
        up = MOUSEEVENTF_MIDDLEUP;
        break;
    default:
        down = MOUSEEVENTF_XDOWN;
        up = MOUSEEVENTF_XUP;
        data = index == 3 ? XBUTTON1 : XBUTTON2;
        break;
    }
    return true;
}

[[nodiscard]] HRESULT ClickButton(std::string_view target, uint32_t clicks, bool deviceAccess) noexcept
{
    uint32_t down = 0;
    uint32_t up = 0;
    DWORD data = 0;
    if (!ButtonFlags(target, down, up, data))
    {
        return E_INVALIDARG;
    }
    std::array<INPUT, 4> inputs{};
    uint32_t count = 0;
    for (uint32_t click = 0; click < clicks; ++click)
    {
        for (const uint32_t flag : {down, up})
        {
            INPUT& input = inputs[count++];
            input = INPUT{};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = flag;
            input.mi.mouseData = data;
        }
    }
    return Inject(inputs.data(), count, deviceAccess);
}

[[nodiscard]] HRESULT HoldButton(std::string_view target, bool release, bool deviceAccess) noexcept
{
    uint32_t down = 0;
    uint32_t up = 0;
    DWORD data = 0;
    if (!ButtonFlags(target, down, up, data))
    {
        return E_INVALIDARG;
    }
    if (release && g_mouse.held)
    {
        const HRESULT released = ReleaseMouse(false, true);
        ArmHeldTimer();
        return released;
    }
    if (release && g_mouse.expired && up == g_heldMouseFlags && data == g_heldMouseData)
    {
        // The deadline already released this button; a second up would end a press the user makes meanwhile.
        g_mouse.expired = false;
        return S_FALSE;
    }
    if (!release && g_mouse.held)
    {
        // A refused release keeps its hold for the retry; the new button is not pressed over it.
        if (const HRESULT released = ReleaseMouse(false, true); FAILED(released))
        {
            ArmHeldTimer();
            return released;
        }
    }
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = release ? up : down;
    input.mi.mouseData = data;
    const HRESULT result = Inject(&input, 1, deviceAccess);
    if (SUCCEEDED(result) && !release)
    {
        g_heldMouseFlags = up;
        g_heldMouseData = data;
        BeginHold(g_mouse, deviceAccess);
    }
    ArmHeldTimer();
    return result;
}

[[nodiscard]] HRESULT Scroll(std::string_view target, bool horizontal, bool deviceAccess) noexcept
{
    Delta delta{};
    if (!ParseDelta(target, -100, 100, delta) || delta.value == 0)
    {
        return E_INVALIDARG;
    }
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(delta.value * WHEEL_DELTA);
    return Inject(&input, 1, deviceAccess);
}

[[nodiscard]] HRESULT HoldChord(std::string_view target, bool release, bool deviceAccess) noexcept
{
    ChordSequence sequence{};
    if (!ParseChords(target, sequence) || sequence.count != 1)
    {
        return E_INVALIDARG;
    }
    if (release && g_chord.held)
    {
        const HRESULT released = ReleaseChord(false, true);
        ArmHeldTimer();
        return released;
    }
    if (release && g_chord.expired && SameChord(sequence.chords[0], g_heldChord))
    {
        // The deadline already released this chord; its key-ups again would also lift a modifier the user is
        // physically holding meanwhile.
        g_chord.expired = false;
        return S_FALSE;
    }
    if (!release && g_chord.held)
    {
        // A refused release keeps its hold for the retry; the new chord is not pressed over it.
        if (const HRESULT released = ReleaseChord(false, true); FAILED(released))
        {
            ArmHeldTimer();
            return released;
        }
    }
    std::array<INPUT, 5> inputs{};
    const uint32_t count = FillChord(sequence.chords[0], release, inputs.data());
    const HRESULT result = Inject(inputs.data(), count, deviceAccess);
    if (SUCCEEDED(result) && !release)
    {
        g_heldChord = sequence.chords[0];
        BeginHold(g_chord, deviceAccess);
    }
    ArmHeldTimer();
    return result;
}

[[nodiscard]] HRESULT ExecuteSystem(std::string_view verb, std::string_view target, bool deviceAccess,
                                    LaunchWorker* launches) noexcept
{
    if (verb == "launch" || verb == "open")
    {
        return Launch(verb, target, deviceAccess, launches);
    }
    if (verb == "run")
    {
        return Run(verb, target, deviceAccess, launches);
    }
    if (verb == "lock")
    {
        ++g_counters.powerRequests;
        return !deviceAccess || LockWorkStation() ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
    if (verb == "sleep" || verb == "hibernate")
    {
        ++g_counters.powerRequests;
        if (!deviceAccess)
        {
            return S_OK;
        }
        return SetSuspendState(verb == "hibernate" ? TRUE : FALSE, FALSE, FALSE) ? S_OK
                                                                                 : HRESULT_FROM_WIN32(GetLastError());
    }
    if (verb == "logoff")
    {
        ++g_counters.powerRequests;
        return !deviceAccess || ExitWindowsEx(EWX_LOGOFF, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED)
                   ? S_OK
                   : HRESULT_FROM_WIN32(GetLastError());
    }
    if (verb == "shutdown" || verb == "restart")
    {
        return Shutdown(target, verb == "restart", deviceAccess);
    }
    if (verb == "shutdown.cancel")
    {
        ++g_counters.powerRequests;
        if (!deviceAccess)
        {
            return S_OK;
        }
        const HRESULT privilege = EnableShutdownPrivilege();
        if (FAILED(privilege))
        {
            return privilege;
        }
        return AbortSystemShutdownW(nullptr) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
    if (verb == "process.close")
    {
        return CloseProcessWindows(target, deviceAccess);
    }
    if (verb == "power.plan")
    {
        return SetPowerPlan(target, deviceAccess);
    }
    if (verb == "theme")
    {
        return SetTheme(target, deviceAccess);
    }
    if (verb == "taskManager")
    {
        return LaunchTaskManager(verb, deviceAccess, launches);
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

[[nodiscard]] HRESULT ExecuteKeys(std::string_view verb, std::string_view target, bool deviceAccess) noexcept
{
    if (verb == "press")
    {
        ChordSequence sequence{};
        if (!ParseChords(target, sequence))
        {
            return E_INVALIDARG;
        }
        return PressChords(sequence, deviceAccess);
    }
    if (verb == "down" || verb == "up")
    {
        return HoldChord(target, verb == "up", deviceAccess);
    }
    if (verb == "type")
    {
        return TypeText(target, deviceAccess);
    }
    if (verb == "media")
    {
        constexpr std::array<uint16_t, 7> kMediaKeys{VK_MEDIA_PLAY_PAUSE, VK_MEDIA_STOP, VK_MEDIA_NEXT_TRACK,
                                                     VK_MEDIA_PREV_TRACK, VK_VOLUME_UP,  VK_VOLUME_DOWN,
                                                     VK_VOLUME_MUTE};
        uint32_t index = 0;
        if (!ParseEnum(target, "play-pause|stop|next-track|previous-track|volume-up|volume-down|mute", index))
        {
            return E_INVALIDARG;
        }
        return TapKey(kMediaKeys[index], true, deviceAccess);
    }
    if (verb == "lock")
    {
        constexpr std::array<uint16_t, 3> kLockKeys{VK_CAPITAL, VK_NUMLOCK, VK_SCROLL};
        uint32_t index = 0;
        if (!ParseEnum(target, "caps|num|scroll", index))
        {
            return E_INVALIDARG;
        }
        return TapKey(kLockKeys[index], index == 1, deviceAccess);
    }
    if (verb == "layout")
    {
        return SwitchLayout(target, deviceAccess);
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

[[nodiscard]] HRESULT ExecuteMouse(std::string_view verb, std::string_view target, bool deviceAccess) noexcept
{
    if (verb == "move")
    {
        return MoveCursor(target, deviceAccess);
    }
    if (verb == "click")
    {
        return ClickButton(target, 1, deviceAccess);
    }
    if (verb == "doubleClick")
    {
        return ClickButton(target, 2, deviceAccess);
    }
    if (verb == "down" || verb == "up")
    {
        return HoldButton(target, verb == "up", deviceAccess);
    }
    if (verb == "scroll")
    {
        return Scroll(target, false, deviceAccess);
    }
    if (verb == "scroll.horizontal")
    {
        return Scroll(target, true, deviceAccess);
    }
    if (verb == "speed")
    {
        int32_t speed = 0;
        if (!ParseInteger(target, 1, 20, speed))
        {
            return E_INVALIDARG;
        }
        if (!deviceAccess)
        {
            return S_OK;
        }
        return SystemParametersInfoW(SPI_SETMOUSESPEED, 0, reinterpret_cast<PVOID>(static_cast<intptr_t>(speed)), 0)
                   ? S_OK
                   : HRESULT_FROM_WIN32(GetLastError());
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}
} // namespace

void SetHostWindow(HWND window, IRedXeHost* log) noexcept
{
    if (g_hostWindow)
    {
        (void)KillTimer(g_hostWindow, kHeldInputTimerId);
    }
    g_hostWindow = window;
    g_log = log;
    ArmHeldTimer();
}

void SetXeneonDisplay(const RECT& bounds, bool found) noexcept
{
    g_xeneonBounds = found ? bounds : RECT{};
    g_xeneonFound = found;
}

HRESULT ValidateExtra(const RedXeActionDescriptor& descriptor, std::string_view target) noexcept
{
    const std::string_view name{descriptor.name};
    if (name == "system.power.plan")
    {
        GUID plan{};
        return ParsePowerPlan(target, plan) ? S_OK : E_INVALIDARG;
    }
    if (name == "keys.down" || name == "keys.up")
    {
        ChordSequence sequence{};
        return ParseChords(target, sequence) && sequence.count == 1 ? S_OK : E_INVALIDARG;
    }
    if (name == "mouse.scroll" || name == "mouse.scroll.horizontal")
    {
        Delta delta{};
        return ParseDelta(target, -100, 100, delta) && delta.value != 0 ? S_OK : E_INVALIDARG;
    }
    return S_OK;
}

void ReleaseHeld(bool deviceAccess) noexcept
{
    (void)deviceAccess;
    // The last attempt: nothing is left to retry a refused release after this.
    (void)ReleaseChord(false, false);
    (void)ReleaseMouse(false, false);
    g_chord = Hold{};
    g_mouse = Hold{};
    ArmHeldTimer();
}

void OnHeldTimer() noexcept
{
    ExpireHeld();
}

HRESULT Execute(const RedXeActionDescriptor& descriptor, std::string_view target, bool deviceAccess,
                LaunchWorker* launches) noexcept
{
    if (descriptor.sizeBytes != sizeof(RedXeActionDescriptor) || !descriptor.name)
    {
        return E_INVALIDARG;
    }
    const std::string_view name{descriptor.name};
    const std::string_view space = HostActionCatalog::NamespaceOf(name);
    const std::string_view verb = name.substr(space.size() + 1);
    // Whatever an earlier keys.down / mouse.down left pressed past its deadline (or due for a release retry) is
    // released first, as the timer would. When this execution is that hold's own `up`, it then injects nothing.
    ExpireHeld();
    CountExecution(descriptor);
    if (space == "system")
    {
        return ExecuteSystem(verb, target, deviceAccess, launches);
    }
    if (space == "keys")
    {
        return ExecuteKeys(verb, target, deviceAccess);
    }
    if (space == "mouse")
    {
        return ExecuteMouse(verb, target, deviceAccess);
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

Counters CopyCounters() noexcept
{
    return g_counters;
}

void ResetCounters() noexcept
{
    g_counters = Counters{};
}

#if defined(REDXE_HOST_PLUGIN_TESTS)
void FailInjectionForTesting(HRESULT failure) noexcept
{
    g_injectionFailure = failure;
}
#endif
} // namespace HostActions
