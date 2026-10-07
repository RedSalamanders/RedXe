#pragma once

#include <cstdint>
#include <windows.h>

// After windows.h, which it needs; a block of its own so include sorting keeps that order.
#include <shellapi.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Notification-area icon (UI_XeneonDisplayWindowing.md "Notification-area icon"). A hidden top-level owner window holds
// one Shell_NotifyIcon entry, adds it again whenever the taskbar is created (Explorer restarted, or started after
// RedXe at sign-in), and turns the shell's callbacks into commands posted to the main window. The owner exists because
// a notification-area menu must belong to the foreground window: foregrounding the dashboard instead would reveal an
// autohide dock and take the focus to a fullscreen XENEON window. Everything runs on the UI thread; there is no thread
// or hook, and the only timer is the bounded one-shot retry of an add a running taskbar refused.

// What the main window is asked to do (TrayIcon::kCommandMessage, in wParam); the values are also the menu item IDs.
enum class TrayCommand : uint32_t
{
    EditSettings = 1,
    Exit = 2,
};

enum class TrayIconAction : uint8_t
{
    None = 0,
    EditSettings,
    ShowMenu,
};

// One NOTIFYICON_VERSION_4 callback (`event` is LOWORD(lParam)) mapped to what the icon does: a double-click, or Enter
// or Space on the keyboard-focused icon (NIN_KEYSELECT), edits the settings file; the context-menu request
// (right-click, Shift+F10, the menu key) opens the menu; single clicks, hover, and balloon events do nothing. An edit
// within `repeatMilliseconds` of the previous one (`lastEditTick`, 0 for none) is dropped: Enter reports NIN_KEYSELECT
// twice, and a quick second double-click would start a second editor.
[[nodiscard]] constexpr TrayIconAction TrayIconActionFor(UINT event, ULONGLONG nowTick, ULONGLONG lastEditTick,
                                                         UINT repeatMilliseconds) noexcept
{
    if (event == WM_CONTEXTMENU)
    {
        return TrayIconAction::ShowMenu;
    }
    if (event != WM_LBUTTONDBLCLK && event != NIN_KEYSELECT)
    {
        return TrayIconAction::None;
    }
    const bool repeat = lastEditTick != 0 && nowTick >= lastEditTick && nowTick - lastEditTick < repeatMilliseconds;
    return repeat ? TrayIconAction::None : TrayIconAction::EditSettings;
}

// Delay before retry `retry` (0-based) of an add that a running taskbar refused or timed out on: four retries with
// doubling delays from 1 s, then 0 (no further retry until the taskbar is created again or Show is called).
[[nodiscard]] constexpr UINT TrayIconAddRetryDelayMilliseconds(uint32_t retry) noexcept
{
    constexpr uint32_t kRetries = 4;
    return retry < kRetries ? 1000u << retry : 0u;
}

class TrayIcon final
{
  public:
    // Posted to the command target with a TrayCommand in wParam.
    static constexpr UINT kCommandMessage = WM_APP + 8;
    // Posted to the command target with the outcome of an add made outside Show (the re-add after the taskbar is
    // created, or a retry): S_OK in wParam once the icon is added, S_FALSE while the shell refuses it.
    static constexpr UINT kAddResultMessage = WM_APP + 10;

    TrayIcon() noexcept = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;
    TrayIcon(TrayIcon&&) = delete;
    TrayIcon& operator=(TrayIcon&&) = delete;

    // Creates the owner window and adds the icon, whose commands go to `commandTarget`; idempotent, and an icon already
    // added costs no shell call. S_FALSE: the owner exists but the shell refused the icon; it is tried again on the
    // TrayIconAddRetryDelayMilliseconds schedule while a taskbar exists, and added when a taskbar announces itself.
    [[nodiscard]] HRESULT Show(HINSTANCE instance, HWND commandTarget) noexcept;
    // Destroys the owner window, which removes the icon, and unregisters its class; idempotent.
    void Hide() noexcept;
    [[nodiscard]] bool Shown() const noexcept
    {
        return static_cast<bool>(_window);
    }

  private:
#if defined(REDXE_HOST_PLUGIN_TESTS)
    friend struct PluginHostTestAccess;
#endif
    static constexpr wchar_t kWindowClassName[] = L"RedXe.TrayIcon";
    // The shell's callback to the owner window; private to that window.
    static constexpr UINT kCallbackMessage = WM_APP + 1;
    static constexpr UINT kIconId = 1;
    static constexpr UINT_PTR kAddRetryTimerId = 1;
    using ShellNotify = BOOL(STDAPICALLTYPE*)(DWORD message, PNOTIFYICONDATAW data);
    using TaskbarProbe = bool (*)() noexcept;

    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    [[nodiscard]] static bool TaskbarExists() noexcept;
    [[nodiscard]] bool AddIcon() noexcept;
    // AddIcon; a refusal while a taskbar exists arms the next one-shot retry, and a success ends the retries.
    [[nodiscard]] bool AddIconOrRetry() noexcept;
    void RemoveIcon() noexcept;
    // The product icon at the small-icon size for the owner window's DPI (the taskbar's display).
    [[nodiscard]] wil::unique_hicon LoadIconForDpi() const noexcept;
    void ShowMenu(POINT anchor) noexcept;
    void PostCommand(TrayCommand command) noexcept;
    void PostAddResult(bool added) noexcept;

    HINSTANCE _instance = nullptr;
    HWND _commandTarget = nullptr;
    wil::unique_hwnd _window;
    wil::unique_hicon _icon;
    UINT _taskbarCreatedMessage = 0;
    ULONGLONG _lastEditTick = 0;
    // Retries armed since the last Show, TaskbarCreated, or success.
    uint32_t _addRetries = 0;
    bool _classRegistered = false;
    // True only once NIM_SETVERSION has followed the add or update.
    bool _iconAdded = false;
    bool _menuOpen = false;
    // Every Shell_NotifyIconW call and the taskbar probe; tests substitute fakes that never reach the shell.
    ShellNotify _shellNotify = &Shell_NotifyIconW;
    TaskbarProbe _taskbarExists = &TrayIcon::TaskbarExists;
};
