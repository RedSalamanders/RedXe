#include "Application.h"
#include "Settings.h"
#include "resource.h"

#include <chrono>
#include <cstring>
#include <new>
#include <string_view>
#include <vector>
#include <windowsx.h>

namespace
{
constexpr LONG kXeneonEdgeClientWidth = 2560;
constexpr LONG kXeneonEdgeClientHeight = 720;

SIZE ScaleXeneonClientSize(UINT dpi) noexcept
{
    return SIZE{MulDiv(kXeneonEdgeClientWidth, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI),
                MulDiv(kXeneonEdgeClientHeight, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI)};
}

HRESULT CalculateWindowSizeForDpi(DWORD windowStyle, DWORD extendedStyle, UINT dpi, SIZE& windowSize) noexcept
{
    if (dpi == 0)
    {
        return E_INVALIDARG;
    }

    const SIZE clientSize = ScaleXeneonClientSize(dpi);
    RECT bounds{0, 0, clientSize.cx, clientSize.cy};
    if (!AdjustWindowRectExForDpi(&bounds, windowStyle, FALSE, extendedStyle, dpi))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    windowSize = SIZE{bounds.right - bounds.left, bounds.bottom - bounds.top};
    return S_OK;
}

struct MonitorLookup final
{
    const wchar_t* deviceName = nullptr;
    RECT bounds{};
    bool found = false;
};

bool ContainsOrdinalIgnoreCase(std::wstring_view value, std::wstring_view expected) noexcept
{
    if (expected.empty() || expected.size() > value.size())
    {
        return false;
    }

    for (size_t offset = 0; offset <= value.size() - expected.size(); ++offset)
    {
        if (CompareStringOrdinal(value.data() + offset, static_cast<int>(expected.size()), expected.data(),
                                 static_cast<int>(expected.size()), TRUE) == CSTR_EQUAL)
        {
            return true;
        }
    }
    return false;
}

BOOL CALLBACK FindMonitorByDeviceName(HMONITOR monitor, HDC, LPRECT, LPARAM context) noexcept
{
    auto* lookup = reinterpret_cast<MonitorLookup*>(context);
    MONITORINFOEXW information{};
    information.cbSize = sizeof(information);
    if (GetMonitorInfoW(monitor, &information) &&
        CompareStringOrdinal(information.szDevice, -1, lookup->deviceName, -1, TRUE) == CSTR_EQUAL)
    {
        lookup->bounds = information.rcMonitor;
        lookup->found = true;
        return FALSE;
    }
    return TRUE;
}

HRESULT FindXeneonDisplay(RECT& bounds, bool& found) noexcept
{
    found = false;

    try
    {
        UINT32 pathCount = 0;
        UINT32 modeCount = 0;
        LONG status = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
        if (status != ERROR_SUCCESS)
        {
            return HRESULT_FROM_WIN32(status);
        }

        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
        if (status != ERROR_SUCCESS)
        {
            return HRESULT_FROM_WIN32(status);
        }

        for (UINT32 index = 0; index < pathCount; ++index)
        {
            const DISPLAYCONFIG_PATH_INFO& path = paths[index];
            DISPLAYCONFIG_TARGET_DEVICE_NAME targetName{};
            targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
            targetName.header.size = sizeof(targetName);
            targetName.header.adapterId = path.targetInfo.adapterId;
            targetName.header.id = path.targetInfo.id;
            if (DisplayConfigGetDeviceInfo(&targetName.header) != ERROR_SUCCESS)
            {
                continue;
            }

            const std::wstring_view friendlyName{targetName.monitorFriendlyDeviceName};
            if (!ContainsOrdinalIgnoreCase(friendlyName, L"XENEON") &&
                !ContainsOrdinalIgnoreCase(friendlyName, L"CORSAIR"))
            {
                continue;
            }

            DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
            sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            sourceName.header.size = sizeof(sourceName);
            sourceName.header.adapterId = path.sourceInfo.adapterId;
            sourceName.header.id = path.sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS)
            {
                continue;
            }

            MonitorLookup lookup{sourceName.viewGdiDeviceName};
            EnumDisplayMonitors(nullptr, nullptr, FindMonitorByDeviceName, reinterpret_cast<LPARAM>(&lookup));
            if (lookup.found)
            {
                bounds = lookup.bounds;
                found = true;
                return S_OK;
            }
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

    return S_OK;
}
} // namespace

Application::Application(HINSTANCE instance, bool forceWarp) noexcept : _instance(instance), _forceWarp(forceWarp) {}

Application::~Application()
{
    _displayPowerNotification.reset();
    _window.reset();
    if (_classRegistered)
    {
        UnregisterClassW(kWindowClassName, _instance);
    }
}

int Application::Run(int showCommand, bool selfTest) noexcept
{
    AppSettings settings{};
    HRESULT result = LoadDefaultSettings(settings);
    if (FAILED(result))
    {
        OutputDebugStringW(L"Default JSON settings are invalid.\n");
        return 1;
    }

    RECT xeneonBounds{};
    const RECT* requestedTargetBounds = nullptr;
    bool requestedFullscreen = false;
    if (!selfTest)
    {
        bool xeneonFound = false;
        result = FindXeneonDisplay(xeneonBounds, xeneonFound);
        if (FAILED(result))
        {
#if defined(_DEBUG)
            OutputDebugStringW(L"XENEON display discovery failed; using default window placement.\n");
#else
            OutputDebugStringW(L"XENEON display discovery failed; offering windowed fallback.\n");
#endif
        }

        if (xeneonFound)
        {
            requestedTargetBounds = &xeneonBounds;
#if !defined(_DEBUG)
            requestedFullscreen = true;
#endif
        }
#if !defined(_DEBUG)
        else
        {
            const int choice = MessageBoxW(
                nullptr,
                L"A CORSAIR XENEON display was not found.\n\nDo you want to display RedXe anyway in a standard "
                L"window with a title bar?",
                L"RedXe — XENEON display missing", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_SETFOREGROUND);
            if (choice != IDYES)
            {
                return 0;
            }
        }
#endif
    }

    result = RegisterWindowClass();
    if (FAILED(result))
    {
        OutputDebugStringW(L"RegisterWindowClass failed.\n");
        return 1;
    }

    result = CreateMainWindow(!selfTest, requestedTargetBounds, requestedFullscreen);
    if (FAILED(result))
    {
        OutputDebugStringW(L"CreateMainWindow failed.\n");
        return 2;
    }

    if (selfTest)
    {
        RECT clientBounds{};
        const UINT windowDpi = GetDpiForWindow(_window.get());
        const SIZE expectedClientSize = ScaleXeneonClientSize(windowDpi);
        if (windowDpi == 0 || !GetClientRect(_window.get(), &clientBounds) ||
            clientBounds.right - clientBounds.left != expectedClientSize.cx ||
            clientBounds.bottom - clientBounds.top != expectedClientSize.cy)
        {
            OutputDebugStringW(L"The default client area does not match the current monitor DPI.\n");
            return 2;
        }
    }

    result = _pluginManager.Initialize(settings.rotatingTriangleInstances);
    if (FAILED(result))
    {
        OutputDebugStringW(L"Bundled rotating-triangle plugin initialization failed.\n");
        return 3;
    }

    result = _dashboardHost.Initialize(_pluginManager);
    if (FAILED(result))
    {
        OutputDebugStringW(L"Dashboard initialization failed.\n");
        return 4;
    }

    result = _renderer.Initialize(_window.get(), _forceWarp, _dashboardHost);
    if (FAILED(result))
    {
        OutputDebugStringW(L"Renderer initialization failed.\n");
        return 5;
    }
    _rendererReady = true;

    if (selfTest)
    {
        result = _renderer.Render(0.0f, 0.0f);
        if (FAILED(result) || _renderer.LastFrameWidgetCount() != settings.rotatingTriangleInstances ||
            _renderer.LastFrameSuccessfulWidgetCount() != settings.rotatingTriangleInstances)
        {
            OutputDebugStringW(L"The plugin smoke frame did not render every GPU-widget instance.\n");
            return 6;
        }
        return 0;
    }

    ShowWindow(_window.get(), showCommand);
    UpdateWindow(_window.get());
    _windowVisible = IsWindowVisible(_window.get()) != FALSE;

    const auto startTime = std::chrono::steady_clock::now();
    float previousElapsedSeconds = 0.0f;
    bool renderedFrame = false;
    MSG message{};
    while (_window)
    {
        bool dispatchedMessage = false;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            dispatchedMessage = true;
            if (message.message == WM_QUIT)
            {
                return FAILED(_runtimeFailure) ? 5 : static_cast<int>(message.wParam);
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (!_window)
        {
            break;
        }

        if (!_windowVisible || !_displayPoweredOn || _renderer.IsSuspended())
        {
            (void)WaitUntilMessage();
            continue;
        }

        if (!_renderer.IsOccluded())
        {
            _occlusionStatusChanged = false;
        }
        else
        {
            if (_occlusionStatusChanged)
            {
                _occlusionStatusChanged = false;
                result = _renderer.ProbeOcclusion();
                if (FAILED(result))
                {
                    _runtimeFailure = result;
                    OutputDebugStringW(L"Swap-chain occlusion probe failed.\n");
                    _window.reset();
                    continue;
                }
            }

            if (_renderer.IsOccluded())
            {
                (void)WaitUntilMessage();
                continue;
            }
        }

        if (!_dashboardHost.RequiresContinuousFrames() && renderedFrame && !dispatchedMessage)
        {
            (void)WaitUntilMessage();
            continue;
        }

        const std::chrono::duration<float> elapsed = std::chrono::steady_clock::now() - startTime;
        const float elapsedSeconds = elapsed.count();
        const float deltaSeconds = elapsedSeconds - previousElapsedSeconds;
        previousElapsedSeconds = elapsedSeconds;
        result = _renderer.Render(elapsedSeconds, deltaSeconds);
        renderedFrame = true;
        if (FAILED(result))
        {
            _runtimeFailure = result;
            OutputDebugStringW(L"Frame rendering failed.\n");
            _window.reset();
        }
    }

    return FAILED(_runtimeFailure) ? 5 : 0;
}

HRESULT Application::RegisterWindowClass() noexcept
{
    const auto largeIcon =
        static_cast<HICON>(LoadImageW(_instance, MAKEINTRESOURCEW(IDI_REDXE), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                      GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR | LR_SHARED));
    if (!largeIcon)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const auto smallIcon =
        static_cast<HICON>(LoadImageW(_instance, MAKEINTRESOURCEW(IDI_REDXE), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                      GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR | LR_SHARED));
    if (!smallIcon)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = 0;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = _instance;
    windowClass.hIcon = largeIcon;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClassName;
    windowClass.hIconSm = smallIcon;

    if (!RegisterClassExW(&windowClass))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    _classRegistered = true;
    return S_OK;
}

HRESULT Application::CreateMainWindow(bool visible, const RECT* targetBounds, bool fullscreen) noexcept
{
    constexpr DWORD extendedStyle = WS_EX_APPWINDOW;
    const DWORD windowStyle = fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;

    if ((fullscreen && !targetBounds) ||
        (targetBounds && (targetBounds->right <= targetBounds->left || targetBounds->bottom <= targetBounds->top)))
    {
        return E_INVALIDARG;
    }

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int width = 0;
    int height = 0;
    if (fullscreen)
    {
        x = targetBounds->left;
        y = targetBounds->top;
        width = targetBounds->right - targetBounds->left;
        height = targetBounds->bottom - targetBounds->top;
    }
    else
    {
        if (targetBounds)
        {
            x = targetBounds->left;
            y = targetBounds->top;
        }

        const UINT dpi = GetDpiForSystem();
        SIZE windowSize{};
        const HRESULT result = CalculateWindowSizeForDpi(windowStyle, extendedStyle, dpi, windowSize);
        if (FAILED(result))
        {
            return result;
        }
        width = windowSize.cx;
        height = windowSize.cy;
    }

    const HWND window = CreateWindowExW(extendedStyle, kWindowClassName, L"RedXe — CORSAIR XENEON", windowStyle, x, y,
                                        width, height, nullptr, nullptr, _instance, this);
    if (!window)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (!fullscreen)
    {
        const UINT windowDpi = GetDpiForWindow(window);
        SIZE windowSize{};
        const HRESULT result = CalculateWindowSizeForDpi(windowStyle, extendedStyle, windowDpi, windowSize);
        if (FAILED(result))
        {
            return result;
        }

        const bool placeOnTarget = targetBounds != nullptr;
        if (placeOnTarget || windowSize.cx != width || windowSize.cy != height)
        {
            const UINT flags = SWP_NOACTIVATE | SWP_NOZORDER | (placeOnTarget ? 0U : SWP_NOMOVE);
            const int targetX = placeOnTarget ? targetBounds->left : 0;
            const int targetY = placeOnTarget ? targetBounds->top : 0;
            if (!SetWindowPos(window, nullptr, targetX, targetY, windowSize.cx, windowSize.cy, flags))
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
        }
    }

    _displayPowerNotification.reset(
        RegisterPowerSettingNotification(window, &GUID_SESSION_DISPLAY_STATUS, DEVICE_NOTIFY_WINDOW_HANDLE));
    if (!_displayPowerNotification)
    {
        const DWORD error = GetLastError();
        return error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }

    if (!visible)
    {
        ShowWindow(window, SW_HIDE);
    }
    return S_OK;
}

bool Application::WaitUntilMessage() noexcept
{
    if (WaitMessage())
    {
        return true;
    }

    const DWORD error = GetLastError();
    _runtimeFailure = error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    _window.reset();
    return false;
}

LRESULT CALLBACK Application::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    Application* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        application = static_cast<Application*>(create->lpCreateParams);
        application->_window.reset(window);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    }

    if (application)
    {
        return application->HandleMessage(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (message)
    {
    case WM_SIZE:
        return OnSize(window, LOWORD(lParam), HIWORD(lParam));
    case WM_DPICHANGED:
        return OnDpiChanged(window, LOWORD(wParam), reinterpret_cast<const RECT*>(lParam));
    case WM_SHOWWINDOW:
        _windowVisible = wParam != FALSE;
        return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_POWERSETTINGCHANGE && lParam != 0)
        {
            const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
            if (IsEqualGUID(setting->PowerSetting, GUID_SESSION_DISPLAY_STATUS) && setting->DataLength >= sizeof(DWORD))
            {
                DWORD displayState = PowerMonitorOn;
                std::memcpy(&displayState, setting->Data, sizeof(displayState));
                _displayPoweredOn = displayState != PowerMonitorOff;
            }
        }
        return TRUE;
    case Renderer::kOcclusionStatusMessage:
        _occlusionStatusChanged = true;
        return 0;
    case WM_GETMINMAXINFO:
    {
        auto* minimums = reinterpret_cast<MINMAXINFO*>(lParam);
        minimums->ptMinTrackSize = POINT{480, 320};
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            _window.reset();
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        ValidateRect(window, nullptr);
        return 0;
    case WM_CLOSE:
        _window.reset();
        return 0;
    case WM_DESTROY:
        _displayPowerNotification.reset();
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
    {
        if (_window.get() == window)
        {
            (void)_window.release();
        }
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    }
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::OnSize(HWND window, UINT width, UINT height) noexcept
{
    if (!_rendererReady)
    {
        return 0;
    }

    const HRESULT result = _renderer.Resize(width, height);
    if (FAILED(result))
    {
        _runtimeFailure = result;
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    return 0;
}

LRESULT Application::OnDpiChanged(HWND window, UINT dpi, const RECT* suggestedBounds) noexcept
{
    if (!suggestedBounds)
    {
        return 0;
    }

    if (_rendererReady)
    {
        const HRESULT result = _renderer.SetDpi(dpi);
        if (FAILED(result))
        {
            _runtimeFailure = result;
            PostMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
    }

    int width = suggestedBounds->right - suggestedBounds->left;
    int height = suggestedBounds->bottom - suggestedBounds->top;
    const DWORD windowStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
    if ((windowStyle & WS_CAPTION) != 0)
    {
        const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
        SIZE windowSize{};
        if (SUCCEEDED(CalculateWindowSizeForDpi(windowStyle, extendedStyle, dpi, windowSize)))
        {
            width = windowSize.cx;
            height = windowSize.cy;
        }
    }

    SetWindowPos(window, nullptr, suggestedBounds->left, suggestedBounds->top, width, height,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    return 0;
}
