#include "WindowSelector.h"

#include <array>
#include <cstring>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace RedXeActions
{
namespace
{
struct EnumerationState final
{
    const WindowSelector* selector = nullptr;
    SelectedWindows* selected = nullptr;
    bool all = false;
};

[[nodiscard]] bool Utf8ToWide(std::string_view text, wchar_t* buffer, int capacity) noexcept
{
    if (text.empty() || !buffer || capacity <= 0)
    {
        return false;
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

[[nodiscard]] bool ImageNameMatches(HWND window, const wchar_t* imageName) noexcept
{
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0)
    {
        return false;
    }
    const wil::unique_handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId)};
    if (!process)
    {
        return false;
    }
    std::array<wchar_t, MAX_PATH> path{};
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &length) || length == 0)
    {
        return false;
    }
    const wchar_t* file = wcsrchr(path.data(), L'\\');
    file = file ? file + 1 : path.data();
    return CompareStringOrdinal(file, -1, imageName, -1, TRUE) == CSTR_EQUAL;
}

BOOL CALLBACK EnumerateWindows(HWND window, LPARAM parameter) noexcept
{
    EnumerationState& state = *reinterpret_cast<EnumerationState*>(parameter);
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr)
    {
        return TRUE;
    }
    const LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) != 0)
    {
        return TRUE;
    }
    std::array<wchar_t, 261> needle{};
    if (!Utf8ToWide(state.selector->value, needle.data(), static_cast<int>(needle.size())))
    {
        return FALSE;
    }
    bool matched = false;
    switch (state.selector->kind)
    {
    case WindowSelector::Kind::Executable:
        matched = ImageNameMatches(window, needle.data());
        break;
    case WindowSelector::Kind::Class:
    {
        std::array<wchar_t, 257> className{};
        const int length = GetClassNameW(window, className.data(), static_cast<int>(className.size()));
        matched = length > 0 && wcscmp(className.data(), needle.data()) == 0;
        break;
    }
    case WindowSelector::Kind::Title:
    {
        std::array<wchar_t, 513> title{};
        const int length = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
        matched = length > 0 && ContainsIgnoreCase(title.data(), needle.data());
        break;
    }
    default:
        break;
    }
    if (!matched)
    {
        return TRUE;
    }
    state.selected->windows[state.selected->count] = window;
    ++state.selected->count;
    return state.all && state.selected->count < kMaximumSelectedWindows ? TRUE : FALSE;
}
} // namespace

bool SelectWindows(const WindowSelector& selector, bool all, SelectedWindows& selected) noexcept
{
    selected = SelectedWindows{};
    if (selector.kind == WindowSelector::Kind::Foreground)
    {
        const HWND foreground = GetForegroundWindow();
        if (!foreground)
        {
            return false;
        }
        selected.windows[0] = foreground;
        selected.count = 1;
        return true;
    }
    EnumerationState state{};
    state.selector = &selector;
    state.selected = &selected;
    state.all = all;
    (void)EnumWindows(EnumerateWindows, reinterpret_cast<LPARAM>(&state));
    return selected.count != 0;
}

HRESULT BringToForeground(HWND window, bool deviceAccess) noexcept
{
    if (!window || !IsWindow(window))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    }
    if (!deviceAccess)
    {
        return S_OK;
    }
    if (GetForegroundWindow() == window)
    {
        return S_OK;
    }
    // A process that did not receive the last input event cannot take the foreground; a synthetic Alt tap makes
    // this process the last input source without changing any key state the user sees.
    std::array<INPUT, 2> alt{};
    alt[0].type = INPUT_KEYBOARD;
    alt[0].ki.wVk = VK_MENU;
    alt[1].type = INPUT_KEYBOARD;
    alt[1].ki.wVk = VK_MENU;
    alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
    (void)SendInput(static_cast<UINT>(alt.size()), alt.data(), sizeof(INPUT));
    if (IsIconic(window))
    {
        (void)ShowWindowAsync(window, SW_RESTORE);
    }
    (void)SetForegroundWindow(window);
    return GetForegroundWindow() == window ? S_OK : E_ACCESSDENIED;
}
} // namespace RedXeActions
