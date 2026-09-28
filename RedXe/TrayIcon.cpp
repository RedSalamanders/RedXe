#include "TrayIcon.h"

#include "resource.h"

#include <utility>
#include <windowsx.h>

TrayIcon::~TrayIcon()
{
    Hide();
}

HRESULT TrayIcon::Show(HINSTANCE instance, HWND commandTarget) noexcept
{
    _commandTarget = commandTarget;
    if (_window)
    {
        return _iconAdded || AddIcon() ? S_OK : S_FALSE;
    }
    _instance = instance;
    if (!_classRegistered)
    {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kWindowClassName;
        if (!RegisterClassExW(&windowClass))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        _classRegistered = true;
    }
    // TaskbarCreated is broadcast to top-level windows, so the owner is a hidden popup rather than a message-only
    // window, which would neither hear it nor be able to take the foreground for the menu.
    _taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    if (!CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClassName, L"RedXe", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance,
                         this) ||
        !_window)
    {
        const DWORD error = GetLastError();
        Hide();
        return error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    if (_taskbarCreatedMessage != 0)
    {
        // An elevated RedXe still hears the shell announce a new taskbar.
        (void)ChangeWindowMessageFilterEx(_window.get(), _taskbarCreatedMessage, MSGFLT_ALLOW, nullptr);
    }
    return AddIcon() ? S_OK : S_FALSE;
}

void TrayIcon::Hide() noexcept
{
    RemoveIcon();
    _window.reset();
    _icon.reset();
    if (_classRegistered)
    {
        (void)UnregisterClassW(kWindowClassName, _instance);
        _classRegistered = false;
    }
    _commandTarget = nullptr;
    _lastEditTick = 0;
}

bool TrayIcon::AddIcon() noexcept
{
    if (!_window)
    {
        return false;
    }
    if (!_icon)
    {
        _icon = LoadIconForDpi();
        if (!_icon)
        {
            return false;
        }
    }
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = _window.get();
    data.uID = kIconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = kCallbackMessage;
    data.hIcon = _icon.get();
#if defined(_DEBUG)
    (void)wcscpy_s(data.szTip, L"RedXe (Debug)");
#else
    (void)wcscpy_s(data.szTip, L"RedXe");
#endif
    // A taskbar that still shows the icon (TaskbarCreated without a new Explorer) refuses the add; it takes an update.
    _iconAdded = Shell_NotifyIconW(NIM_ADD, &data) != FALSE || Shell_NotifyIconW(NIM_MODIFY, &data) != FALSE;
    if (_iconAdded)
    {
        // Version 4: WM_CONTEXTMENU for the menu (mouse and keyboard alike) with the anchor point in wParam.
        data.uVersion = NOTIFYICON_VERSION_4;
        (void)Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
    return _iconAdded;
}

void TrayIcon::RemoveIcon() noexcept
{
    // Deleted even when the last add looked refused, so no taskbar keeps the icon after RedXe is gone.
    if (_window)
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = _window.get();
        data.uID = kIconId;
        (void)Shell_NotifyIconW(NIM_DELETE, &data);
    }
    _iconAdded = false;
}

wil::unique_hicon TrayIcon::LoadIconForDpi() const noexcept
{
    UINT dpi = _window ? GetDpiForWindow(_window.get()) : 0;
    if (dpi == 0)
    {
        dpi = GetDpiForSystem();
    }
    return wil::unique_hicon{static_cast<HICON>(LoadImageW(_instance, MAKEINTRESOURCEW(IDI_REDXE), IMAGE_ICON,
                                                           GetSystemMetricsForDpi(SM_CXSMICON, dpi),
                                                           GetSystemMetricsForDpi(SM_CYSMICON, dpi), LR_DEFAULTCOLOR))};
}

void TrayIcon::ShowMenu(POINT anchor) noexcept
{
    if (_menuOpen || !_window)
    {
        return;
    }
    wil::unique_hmenu menu{CreatePopupMenu()};
    if (!menu ||
        !AppendMenuW(menu.get(), MF_STRING, static_cast<UINT_PTR>(TrayCommand::EditSettings), L"&Edit settings") ||
        !AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr) ||
        !AppendMenuW(menu.get(), MF_STRING, static_cast<UINT_PTR>(TrayCommand::Exit), L"E&xit"))
    {
        return;
    }
    // The double-click action, drawn bold.
    (void)SetMenuDefaultItem(menu.get(), static_cast<UINT>(TrayCommand::EditSettings), FALSE);
    // A notification-area menu must belong to the foreground window, or it stays open when the person clicks
    // elsewhere; the WM_NULL afterwards is the documented companion. The menu runs the system's modal menu loop, so
    // the dashboard presents no frame while it is open.
    (void)SetForegroundWindow(_window.get());
    UINT flags = TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY;
    flags |= GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0 ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    _menuOpen = true;
    const UINT chosen =
        static_cast<UINT>(TrackPopupMenuEx(menu.get(), flags, anchor.x, anchor.y, _window.get(), nullptr));
    _menuOpen = false;
    // A settings reload during the menu may have hidden the icon; the owner is then gone.
    if (!_window)
    {
        return;
    }
    (void)PostMessageW(_window.get(), WM_NULL, 0, 0);
    if (chosen == static_cast<UINT>(TrayCommand::EditSettings))
    {
        _lastEditTick = GetTickCount64();
        PostCommand(TrayCommand::EditSettings);
    }
    else if (chosen == static_cast<UINT>(TrayCommand::Exit))
    {
        PostCommand(TrayCommand::Exit);
    }
}

void TrayIcon::PostCommand(TrayCommand command) noexcept
{
    // Posted, never called: the main window acts outside this window's procedure, which Exit destroys.
    if (_commandTarget)
    {
        (void)PostMessageW(_commandTarget, kCommandMessage, static_cast<WPARAM>(command), 0);
    }
}

LRESULT CALLBACK TrayIcon::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    auto* tray = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        tray = static_cast<TrayIcon*>(reinterpret_cast<const CREATESTRUCTW*>(lParam)->lpCreateParams);
        tray->_window.reset(window);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tray));
    }
    return tray ? tray->HandleMessage(window, message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TrayIcon::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    if (message == kCallbackMessage)
    {
        const ULONGLONG now = GetTickCount64();
        switch (TrayIconActionFor(LOWORD(lParam), now, _lastEditTick, GetDoubleClickTime()))
        {
        case TrayIconAction::EditSettings:
            _lastEditTick = now;
            PostCommand(TrayCommand::EditSettings);
            break;
        case TrayIconAction::ShowMenu:
            ShowMenu(POINT{GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
            break;
        default:
            break;
        }
        return 0;
    }
    if (_taskbarCreatedMessage != 0 && message == _taskbarCreatedMessage)
    {
        // A new taskbar knows no icon: Explorer restarted, or started after RedXe at sign-in. The icon is loaded again
        // for the display scale the new taskbar has.
        _iconAdded = false;
        _icon.reset();
        (void)AddIcon();
        return 0;
    }
    switch (message)
    {
    case WM_DPICHANGED:
        // The taskbar's display changed scale: the icon follows at the new small-icon size. The window stays hidden
        // and is not moved.
        if (wil::unique_hicon icon = LoadIconForDpi())
        {
            if (_iconAdded)
            {
                NOTIFYICONDATAW data{};
                data.cbSize = sizeof(data);
                data.hWnd = window;
                data.uID = kIconId;
                data.uFlags = NIF_ICON;
                data.hIcon = icon.get();
                (void)Shell_NotifyIconW(NIM_MODIFY, &data);
            }
            _icon = std::move(icon);
        }
        return 0;
    case WM_NCDESTROY:
        if (_window.get() == window)
        {
            (void)_window.release();
        }
        _iconAdded = false;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
