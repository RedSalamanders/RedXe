#include "HostActions.h"

#include "Actions/ActionTargets.h"
#include "Actions/WindowSelector.h"
#include "HostActionCatalog.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <powrprof.h>
#include <shellapi.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace HostActions
{
namespace
{
using namespace RedXeActions;

// UI-thread state: the main window, the counters, and whatever keys.down / mouse.down left pressed.
HWND g_hostWindow = nullptr;
Counters g_counters{};
KeyChord g_heldChord{};
bool g_chordHeld = false;
uint32_t g_heldMouseFlags = 0;
DWORD g_heldMouseData = 0;
bool g_mouseHeld = false;
ULONGLONG g_chordSince = 0;
ULONGLONG g_mouseSince = 0;
bool g_chordDeviceAccess = false;
bool g_mouseDeviceAccess = false;

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
    if (!g_chordHeld && !g_mouseHeld)
    {
        return;
    }
    const ULONGLONG chordDue = g_chordHeld ? g_chordSince + kHeldReleaseMilliseconds : ULLONG_MAX;
    const ULONGLONG mouseDue = g_mouseHeld ? g_mouseSince + kHeldReleaseMilliseconds : ULLONG_MAX;
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG due = std::min(chordDue, mouseDue);
    const UINT delay = static_cast<UINT>(std::max<ULONGLONG>(1, due > now ? due - now : 1));
    (void)SetTimer(g_hostWindow, kHeldInputTimerId, delay, nullptr);
}

void ReleaseChord() noexcept
{
    if (!g_chordHeld)
    {
        return;
    }
    std::array<INPUT, 5> inputs{};
    const uint32_t count = FillChord(g_heldChord, true, inputs.data());
    (void)Inject(inputs.data(), count, g_chordDeviceAccess);
    g_chordHeld = false;
    ++g_counters.heldReleases;
}

void ReleaseMouse() noexcept
{
    if (!g_mouseHeld)
    {
        return;
    }
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = g_heldMouseFlags;
    input.mi.mouseData = g_heldMouseData;
    (void)Inject(&input, 1, g_mouseDeviceAccess);
    g_mouseHeld = false;
    ++g_counters.heldReleases;
}

void ExpireHeld() noexcept
{
    const ULONGLONG now = GetTickCount64();
    if (g_chordHeld && now - g_chordSince >= kHeldReleaseMilliseconds)
    {
        ReleaseChord();
    }
    if (g_mouseHeld && now - g_mouseSince >= kHeldReleaseMilliseconds)
    {
        ReleaseMouse();
    }
    ArmHeldTimer();
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

[[nodiscard]] HRESULT Launch(std::string_view target, bool deviceAccess) noexcept
{
    std::array<wchar_t, kRedXeMaximumActionTargetBytes + 1> wide{};
    if (!IsPathOrUri(target) || !Utf8ToWide(target, wide.data(), static_cast<int>(wide.size())))
    {
        return E_INVALIDARG;
    }
    ++g_counters.launches;
    if (!deviceAccess)
    {
        return S_OK;
    }
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_FLAG_NO_UI;
    info.lpFile = wide.data();
    info.nShow = SW_SHOWNORMAL;
    // A file launches with its own directory as working directory, as Launcher always did.
    std::array<wchar_t, kRedXeMaximumActionTargetBytes + 1> directory{};
    if (IsAbsolutePath(target))
    {
        const DWORD attributes = GetFileAttributesW(wide.data());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            wcscpy_s(directory.data(), directory.size(), wide.data());
            wchar_t* slash = wcsrchr(directory.data(), L'\\');
            if (!slash)
            {
                slash = wcsrchr(directory.data(), L'/');
            }
            if (slash)
            {
                *slash = L'\0';
                info.lpDirectory = directory.data();
            }
        }
    }
    if (!ShellExecuteExW(&info))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

[[nodiscard]] HRESULT Run(std::string_view target, bool deviceAccess) noexcept
{
    CommandLine parsed{};
    if (!ParseCommandLine(target, parsed))
    {
        return E_INVALIDARG;
    }
    std::array<wchar_t, kRedXeMaximumActionTargetBytes + 1> executable{};
    std::array<wchar_t, kRedXeMaximumActionTargetBytes + 4> commandLine{};
    std::array<wchar_t, kRedXeMaximumActionTargetBytes + 1> arguments{};
    if (!Utf8ToWide(parsed.executable, executable.data(), static_cast<int>(executable.size())) ||
        !Utf8ToWide(parsed.arguments, arguments.data(), static_cast<int>(arguments.size())))
    {
        return E_INVALIDARG;
    }
    // CreateProcessW wants the executable quoted in the mutable command line so a path with spaces stays one token.
    swprintf_s(commandLine.data(), commandLine.size(), L"\"%s\"%s%s", executable.data(),
               arguments[0] != L'\0' ? L" " : L"", arguments.data());
    std::array<wchar_t, kRedXeMaximumActionTargetBytes + 1> directory{};
    wcscpy_s(directory.data(), directory.size(), executable.data());
    wchar_t* slash = wcsrchr(directory.data(), L'\\');
    if (slash)
    {
        *slash = L'\0';
    }
    ++g_counters.processes;
    if (!deviceAccess)
    {
        return S_OK;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.data(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_DEFAULT_ERROR_MODE,
                        nullptr, slash ? directory.data() : nullptr, &startup, &process))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return S_OK;
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

[[nodiscard]] HRESULT LaunchTaskManager(bool deviceAccess) noexcept
{
    std::array<wchar_t, MAX_PATH> path{};
    const UINT length = GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (wcscat_s(path.data(), path.size(), L"\\Taskmgr.exe") != 0)
    {
        return E_FAIL;
    }
    ++g_counters.launches;
    if (!deviceAccess)
    {
        return S_OK;
    }
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_FLAG_NO_UI;
    info.lpFile = path.data();
    info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&info) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
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
    if (release && g_mouseHeld)
    {
        ReleaseMouse();
        ArmHeldTimer();
        return S_OK;
    }
    if (!release && g_mouseHeld)
    {
        ReleaseMouse();
    }
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = release ? up : down;
    input.mi.mouseData = data;
    const HRESULT result = Inject(&input, 1, deviceAccess);
    if (SUCCEEDED(result))
    {
        g_mouseHeld = !release;
        if (!release)
        {
            g_heldMouseFlags = up;
            g_heldMouseData = data;
            g_mouseSince = GetTickCount64();
            g_mouseDeviceAccess = deviceAccess;
        }
        ArmHeldTimer();
    }
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
    if (release && g_chordHeld)
    {
        ReleaseChord();
        ArmHeldTimer();
        return S_OK;
    }
    if (!release && g_chordHeld)
    {
        ReleaseChord();
    }
    std::array<INPUT, 5> inputs{};
    const uint32_t count = FillChord(sequence.chords[0], release, inputs.data());
    const HRESULT result = Inject(inputs.data(), count, deviceAccess);
    if (SUCCEEDED(result))
    {
        g_chordHeld = !release;
        if (!release)
        {
            g_heldChord = sequence.chords[0];
            g_chordSince = GetTickCount64();
            g_chordDeviceAccess = deviceAccess;
        }
        ArmHeldTimer();
    }
    return result;
}

[[nodiscard]] HRESULT ExecuteSystem(std::string_view verb, std::string_view target, bool deviceAccess) noexcept
{
    if (verb == "launch" || verb == "open")
    {
        return Launch(target, deviceAccess);
    }
    if (verb == "run")
    {
        return Run(target, deviceAccess);
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
        return LaunchTaskManager(deviceAccess);
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

void SetHostWindow(HWND window) noexcept
{
    if (g_hostWindow)
    {
        (void)KillTimer(g_hostWindow, kHeldInputTimerId);
    }
    g_hostWindow = window;
    ArmHeldTimer();
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
    ReleaseChord();
    ReleaseMouse();
    ArmHeldTimer();
}

void OnHeldTimer() noexcept
{
    ExpireHeld();
}

HRESULT Execute(const RedXeActionDescriptor& descriptor, std::string_view target, bool deviceAccess) noexcept
{
    if (descriptor.sizeBytes != sizeof(RedXeActionDescriptor) || !descriptor.name)
    {
        return E_INVALIDARG;
    }
    const std::string_view name{descriptor.name};
    const std::string_view space = HostActionCatalog::NamespaceOf(name);
    const std::string_view verb = name.substr(space.size() + 1);
    // Anything left pressed by an earlier keys.down / mouse.down is released once it is older than the budget,
    // unless this execution is the matching release itself.
    ExpireHeld();
    CountExecution(descriptor);
    if (space == "system")
    {
        return ExecuteSystem(verb, target, deviceAccess);
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
} // namespace HostActions
