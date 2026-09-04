#include "Application.h"

#include "CrashHandler.h"
#include "FluentIcons.h"
#include "FrameScheduler.h"
#include "PageNavigation.h"
#include "Settings.h"
#include "WidgetRaise.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <shellapi.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windowsx.h>

namespace
{
// Windows tags mouse messages synthesized from a touch or pen contact. Those belong to the WM_POINTER swipe path, so
// the mouse edge affordance ignores them.
[[nodiscard]] bool IsPointerSynthesizedMouseMessage() noexcept
{
    constexpr ULONG_PTR kPointerSignatureMask = 0xFFFFFF00;
    constexpr ULONG_PTR kPointerSignature = 0xFF515700;
    return (static_cast<ULONG_PTR>(GetMessageExtraInfo()) & kPointerSignatureMask) == kPointerSignature;
}
constexpr LONG kXeneonEdgeClientWidth = 2560;
constexpr LONG kXeneonEdgeClientHeight = 720;

[[nodiscard]] BOOL HostSetPointerCapture(HWND window, UINT32 pointerId) noexcept
{
    using Function = BOOL(WINAPI*)(HWND, UINT32);
    static const Function function =
        reinterpret_cast<Function>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetPointerCapture"));
    if (function)
    {
        return function(window, pointerId);
    }
    return SetCapture(window) != nullptr;
}

void HostReleasePointerCapture(HWND window, UINT32 pointerId) noexcept
{
    using Function = BOOL(WINAPI*)(HWND, UINT32);
    static const Function function =
        reinterpret_cast<Function>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "ReleasePointerCapture"));
    if (function)
    {
        (void)function(window, pointerId);
        return;
    }
    if (GetCapture() == window)
    {
        ReleaseCapture();
    }
}

[[nodiscard]] HRESULT ValidateExecutableShellIcon() noexcept
{
    std::array<wchar_t, 1024> executablePath{};
    const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
    if (length == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (length >= executablePath.size())
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    HICON extractedLarge = nullptr;
    HICON extractedSmall = nullptr;
    const UINT count = ExtractIconExW(executablePath.data(), 0, &extractedLarge, &extractedSmall, 1);
    wil::unique_hicon largeIcon{extractedLarge};
    wil::unique_hicon smallIcon{extractedSmall};
    return count != 0 && largeIcon && smallIcon ? S_OK : HRESULT_FROM_WIN32(ERROR_RESOURCE_DATA_NOT_FOUND);
}

SIZE ScaleXeneonClientSize(UINT dpi) noexcept
{
    return SIZE{MulDiv(kXeneonEdgeClientWidth, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI),
                MulDiv(kXeneonEdgeClientHeight, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI)};
}

[[nodiscard]] LONG DoubleActivateSlopPixels(UINT dpi) noexcept
{
    int systemSlop = 0;
    if (dpi != 0)
    {
        systemSlop = GetSystemMetricsForDpi(SM_CXDOUBLECLK, dpi) / 2;
    }
    if (systemSlop <= 0)
    {
        systemSlop = GetSystemMetrics(SM_CXDOUBLECLK) / 2;
    }
    return std::max(static_cast<LONG>(systemSlop), RaisedDipPixels(16, dpi));
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

[[nodiscard]] size_t CountGpuWidgets(const PluginManager& plugins) noexcept
{
    size_t count = 0;
    for (size_t index = 0; index < plugins.WidgetCount(); ++index)
    {
        count += plugins.GpuWidgetAt(index) ? 1U : 0U;
    }
    return count;
}

[[nodiscard]] bool PluginEnabled(const AppSettings& settings, std::string_view pluginId) noexcept
{
    const PluginSettings* plugin = FindPluginSettings(settings, pluginId);
    return plugin && plugin->enabled;
}

[[nodiscard]] HRESULT DisablePluginAndRemoveWidgets(AppSettings& settings, std::string_view pluginId) noexcept
{
    PluginSettings* plugin = FindPluginSettings(settings, pluginId);
    if (!plugin)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    plugin->enabled = false;

    uint32_t pageWrite = 0;
    for (uint32_t pageIndex = 0; pageIndex < settings.dashboard.pageCount; ++pageIndex)
    {
        DashboardPageSettings page = settings.dashboard.pages[pageIndex];
        uint32_t widgetWrite = 0;
        for (uint32_t widgetIndex = 0; widgetIndex < page.widgetCount; ++widgetIndex)
        {
            if (!SettingsIdEquals(page.widgets[widgetIndex].pluginId.View(), pluginId))
            {
                page.widgets[widgetWrite++] = page.widgets[widgetIndex];
            }
        }
        page.widgetCount = widgetWrite;
        page.widgets.resize(widgetWrite);
        if (page.widgetCount == 0)
        {
            if (SettingsIdEquals(page.id.View(), settings.dashboard.activePageId.View()))
            {
                return E_INVALIDARG;
            }
            continue;
        }
        settings.dashboard.pages[pageWrite++] = page;
    }
    settings.dashboard.pageCount = pageWrite;
    settings.dashboard.pages.resize(pageWrite);
    return ValidateAppSettings(settings);
}

} // namespace

Application::Application(HINSTANCE instance, bool forceWarp) noexcept : _instance(instance), _forceWarp(forceWarp)
{
    _pluginManager.reset(new (std::nothrow) PluginManager());
    _dashboardHost.reset(new (std::nothrow) DashboardHost());
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
    {
        _qpcFrequency = static_cast<UINT64>(frequency.QuadPart);
    }
}

Application::~Application()
{
    _displayPowerNotification.reset();
    CloseSettingsError();
    CloseMainWindow();
    if (_classRegistered)
    {
        UnregisterClassW(kSettingsDialogClassName, _instance);
        UnregisterClassW(kWindowClassName, _instance);
        UnregisterClassW(kRaiseOverlayClassName, _instance);
    }
    if (_pageEdgeClassRegistered)
    {
        UnregisterClassW(kPageEdgeClassName, _instance);
    }
}

int Application::Run(int showCommand, std::wstring_view settingsPath) noexcept
{
    if (!_pluginManager || !_dashboardHost)
    {
        OutputDebugStringW(L"Dashboard host allocation failed.\n");
        return 1;
    }

    HRESULT result = _settingsStore.Initialize(false, settingsPath, _settings);
    if (FAILED(result) || !_settings)
    {
        OutputDebugStringW(L"Settings initialization or validation failed.\n");
        return 1;
    }
    if (_settingsStore.UsedInitialFallback())
    {
        MessageBoxW(nullptr, _settingsStore.InitialNotice().c_str(), L"RedXe settings", MB_OK | MB_ICONERROR);
    }

    RECT xeneonBounds{};
    const RECT* requestedTargetBounds = nullptr;
    bool requestedFullscreen = false;
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
        // Owned by UI_XeneonDisplayWindowing.md: Release prompts for a windowed fallback, Debug never does.
        const int choice = MessageBoxW(
            nullptr,
            L"A CORSAIR XENEON display was not found.\n\nDo you want to display RedXe anyway in a standard "
            L"window with a title bar?",
            L"RedXe \u2014 XENEON display missing", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_SETFOREGROUND);
        if (choice != IDYES)
        {
            return 0;
        }
    }
#endif

    result = RegisterWindowClass();
    if (FAILED(result))
    {
        OutputDebugStringW(L"RegisterWindowClass failed.\n");
        return 1;
    }

    result = CreateMainWindow(true, requestedTargetBounds, requestedFullscreen);
    if (FAILED(result))
    {
        OutputDebugStringW(L"CreateMainWindow failed.\n");
        return 2;
    }

    result = _pluginManager->Initialize(*_settings);
    if (FAILED(result))
    {
        OutputDebugStringW(L"Bundled plugin initialization failed.\n");
        return 3;
    }

    result = InitializeDashboardRuntime();
    if (FAILED(result))
    {
        OutputDebugStringW(L"Dashboard or renderer initialization failed.\n");
        return 5;
    }

    ShowWindow(_window.get(), showCommand);
    UpdateWindow(_window.get());
    _windowVisible = IsWindowVisible(_window.get()) != FALSE;
    CrashHandler::ShowPreviousCrashUiIfPresent(_window.get());
    if (!_window)
    {
        return FAILED(_runtimeFailure) ? 5 : 0;
    }

    result = _settingsWatcher.Start(_window.get(), _settingsStore.SettingsDirectory());
    if (FAILED(result))
    {
        OutputDebugStringW(L"Settings watcher initialization failed.\n");
        return 7;
    }

    const auto startTime = std::chrono::steady_clock::now();
    float previousElapsedSeconds = 0.0f;
    MSG message{};
    while (_window)
    {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
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

        if (_pageSettleActive)
        {
            TickPageSettle();
        }

        if (!_renderer.IsOccluded())
        {
            _occlusionStatusChanged = false;
        }

        HostFrameAction frameAction = SelectHostFrameAction(HostFrameState{
            _windowVisible,
            _displayPoweredOn,
            _renderer.IsSuspended(),
            _renderer.IsOccluded(),
            _occlusionStatusChanged,
            DashboardRequiresContinuousFrames(),
            _frameInvalidated,
            PageNavigationInProgress(),
        });
        if (frameAction == HostFrameAction::ProbeOcclusion)
        {
            _occlusionStatusChanged = false;
            result = _renderer.ProbeOcclusion();
            if (FAILED(result))
            {
                _runtimeFailure = result;
                OutputDebugStringW(L"Swap-chain occlusion probe failed.\n");
                CloseMainWindow();
                continue;
            }
            if (!_renderer.IsOccluded())
            {
                _frameInvalidated = true;
            }
            result = UpdateDashboardVisibility();
            if (FAILED(result))
            {
                _runtimeFailure = result;
                CloseMainWindow();
                continue;
            }
            frameAction = SelectHostFrameAction(HostFrameState{
                _windowVisible,
                _displayPoweredOn,
                _renderer.IsSuspended(),
                _renderer.IsOccluded(),
                false,
                DashboardRequiresContinuousFrames(),
                _frameInvalidated,
                PageNavigationInProgress(),
            });
        }

        if (frameAction == HostFrameAction::WaitForMessage)
        {
            result = UpdateDashboardVisibility();
            if (FAILED(result))
            {
                _runtimeFailure = result;
                CloseMainWindow();
                continue;
            }
            (void)WaitUntilMessage();
            continue;
        }

        const std::chrono::duration<float> elapsed = std::chrono::steady_clock::now() - startTime;
        const float elapsedSeconds = elapsed.count();
        const float deltaSeconds = elapsedSeconds - previousElapsedSeconds;
        previousElapsedSeconds = elapsedSeconds;
        result = _renderer.Render(elapsedSeconds, deltaSeconds);
        if (FAILED(result))
        {
            _runtimeFailure = result;
            OutputDebugStringW(L"Frame rendering failed.\n");
            CloseMainWindow();
            continue;
        }
        if (result == S_OK)
        {
            _frameInvalidated = false;
            RefreshScheduledFrameDeadline();
        }
        FlushPendingTransitionStage();
        result = UpdateDashboardVisibility();
        if (FAILED(result))
        {
            _runtimeFailure = result;
            CloseMainWindow();
        }
    }

    return FAILED(_runtimeFailure) ? 5 : 0;
}

// Hidden startup validation for `--self-test`. It shares Application's startup steps but never shows a window and
// never enters the frame loop, so the production Run above carries no test branches and no `selfTest` parameter.
//
// UI_XeneonDisplayWindowing.md owns this mode: skip display discovery and prompts, create the titled window hidden,
// validate its DPI-adjusted client dimensions, render one frame, and exit.
int Application::RunSelfTest(std::wstring_view settingsPath) noexcept
{
    if (!_pluginManager || !_dashboardHost)
    {
        OutputDebugStringW(L"Dashboard host allocation failed.\n");
        return 1;
    }

    HRESULT result = _settingsStore.Initialize(true, settingsPath, _settings);
    if (FAILED(result) || !_settings)
    {
        OutputDebugStringW(L"Settings initialization or validation failed.\n");
        return 1;
    }
    if (FAILED(ValidateExecutableShellIcon()))
    {
        OutputDebugStringW(L"The executable does not expose extractable large and small shell icons.\n");
        return 1;
    }

    result = RegisterWindowClass();
    if (FAILED(result))
    {
        OutputDebugStringW(L"RegisterWindowClass failed.\n");
        return 1;
    }

    result = CreateMainWindow(false, nullptr, false);
    if (FAILED(result))
    {
        OutputDebugStringW(L"CreateMainWindow failed.\n");
        return 2;
    }

    RECT clientBounds{};
    const UINT windowDpi = GetDpiForWindow(_window.get());
    const SIZE expectedClientSize = ScaleXeneonClientSize(windowDpi);
    const DWORD windowStyle = static_cast<DWORD>(GetWindowLongPtrW(_window.get(), GWL_STYLE));
    if (windowDpi == 0 || (windowStyle & WS_CLIPCHILDREN) == 0 || !GetClientRect(_window.get(), &clientBounds) ||
        clientBounds.right - clientBounds.left != expectedClientSize.cx ||
        clientBounds.bottom - clientBounds.top != expectedClientSize.cy)
    {
        OutputDebugStringW(L"The default client area does not match the current monitor DPI.\n");
        return 2;
    }

#if defined(_DEBUG)
    {
        std::unique_ptr<AppSettings> matrixDisabled{new (std::nothrow) AppSettings{*_settings}};
        if (!matrixDisabled)
        {
            return 3;
        }
        result = DisablePluginAndRemoveWidgets(*matrixDisabled, "builtin.matrix-rain");
        PluginManager disabledMatrixManager;
        if (SUCCEEDED(result))
        {
            result = disabledMatrixManager.Initialize(*matrixDisabled);
        }
        if (FAILED(result) || GetModuleHandleW(L"MatrixRain.dll"))
        {
            OutputDebugStringW(L"A disabled Matrix Rain plugin was loaded during the conditional-load test.\n");
            return 3;
        }
    }
#endif

    result = _pluginManager->Initialize(*_settings);
    if (FAILED(result))
    {
        OutputDebugStringW(L"Bundled plugin initialization failed.\n");
        return 3;
    }

    result = InitializeDashboardRuntime();
    if (FAILED(result))
    {
        OutputDebugStringW(L"Dashboard or renderer initialization failed.\n");
        return 5;
    }

    const size_t expectedGpuWidgetCount = CountGpuWidgets(*_pluginManager);
    result = _renderer.Render(0.0f, 0.0f);
    if (FAILED(result) || _renderer.LastFrameWidgetCount() != expectedGpuWidgetCount ||
        _renderer.LastFrameSuccessfulWidgetCount() != expectedGpuWidgetCount ||
        (!PluginEnabled(*_settings, "builtin.rotating-triangle") && GetModuleHandleW(L"RotatingTriangle.dll")) ||
        (!PluginEnabled(*_settings, "builtin.gdi-orbit") && GetModuleHandleW(L"GdiOrbit.dll")) ||
        (!PluginEnabled(*_settings, "builtin.matrix-rain") && GetModuleHandleW(L"MatrixRain.dll")))
    {
        OutputDebugStringW(L"The plugin smoke frame did not render every GPU-widget instance.\n");
        return 6;
    }

    const uint32_t pageCount = _settings->dashboard.pageCount;
    for (uint32_t page = 1; page < pageCount; ++page)
    {
        std::unique_ptr<AppSettings> changed{new (std::nothrow) AppSettings{*_settings}};
        if (!changed)
        {
            return 6;
        }
        result = MoveDashboardPage(*changed, 1);
        if (SUCCEEDED(result))
            result = ApplySettings(std::move(changed));
        if (SUCCEEDED(result))
            result = _renderer.Render(0.0f, 0.0f);
        const size_t changedGpuWidgetCount = CountGpuWidgets(*_pluginManager);
        if (FAILED(result) || _renderer.LastFrameWidgetCount() != changedGpuWidgetCount ||
            _renderer.LastFrameSuccessfulWidgetCount() != changedGpuWidgetCount)
        {
            OutputDebugStringW(L"The dashboard page/private settings reconfiguration smoke test failed.\n");
            return 6;
        }
    }

    std::unique_ptr<AppSettings> rejected{new (std::nothrow) AppSettings{*_settings}};
    if (!rejected || SUCCEEDED(ParseAppSettingsJson("{}", *rejected)) || *rejected != *_settings)
    {
        OutputDebugStringW(L"Invalid settings changed the active typed configuration.\n");
        return 6;
    }
    return 0;
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
    WNDCLASSEXW dialogClass{};
    dialogClass.cbSize = sizeof(dialogClass);
    dialogClass.lpfnWndProc = SettingsDialogProcedure;
    dialogClass.hInstance = _instance;
    dialogClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    dialogClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    dialogClass.lpszClassName = kSettingsDialogClassName;
    if (!RegisterClassExW(&dialogClass))
    {
        UnregisterClassW(kWindowClassName, _instance);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    WNDCLASSEXW overlayClass{};
    overlayClass.cbSize = sizeof(overlayClass);
    overlayClass.lpfnWndProc = RaiseOverlayProcedure;
    overlayClass.hInstance = _instance;
    overlayClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    overlayClass.lpszClassName = kRaiseOverlayClassName;
    if (!RegisterClassExW(&overlayClass))
    {
        UnregisterClassW(kSettingsDialogClassName, _instance);
        UnregisterClassW(kWindowClassName, _instance);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    WNDCLASSEXW pageEdgeClass{};
    pageEdgeClass.cbSize = sizeof(pageEdgeClass);
    pageEdgeClass.lpfnWndProc = PageEdgeProcedure;
    pageEdgeClass.hInstance = _instance;
    pageEdgeClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    pageEdgeClass.lpszClassName = kPageEdgeClassName;
    if (!RegisterClassExW(&pageEdgeClass))
    {
        UnregisterClassW(kRaiseOverlayClassName, _instance);
        UnregisterClassW(kSettingsDialogClassName, _instance);
        UnregisterClassW(kWindowClassName, _instance);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    _pageEdgeClassRegistered = true;
    _classRegistered = true;
    return S_OK;
}

HRESULT Application::CreateMainWindow(bool visible, const RECT* targetBounds, bool fullscreen) noexcept
{
    constexpr DWORD extendedStyle = WS_EX_APPWINDOW;
    const DWORD windowStyle = (fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW) | WS_CLIPCHILDREN;

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
        const UINT flags = SWP_NOACTIVATE | SWP_NOZORDER | (placeOnTarget ? 0U : SWP_NOMOVE);
        const int targetX = placeOnTarget ? targetBounds->left : 0;
        const int targetY = placeOnTarget ? targetBounds->top : 0;
        if (!SetWindowPos(window, nullptr, targetX, targetY, windowSize.cx, windowSize.cy, flags))
        {
            return HRESULT_FROM_WIN32(GetLastError());
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

HRESULT Application::InitializeDashboardRuntime() noexcept
{
    if (!_window || _rendererReady || !_pluginManager || !_dashboardHost)
    {
        return E_UNEXPECTED;
    }

    RECT clientBounds{};
    const UINT dpi = GetDpiForWindow(_window.get());
    if (dpi == 0 || !GetClientRect(_window.get(), &clientBounds))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const UINT width = static_cast<UINT>(clientBounds.right - clientBounds.left);
    const UINT height = static_cast<UINT>(clientBounds.bottom - clientBounds.top);
    if (width == 0 || height == 0)
    {
        return E_UNEXPECTED;
    }

    HRESULT result = _dashboardHost->Initialize(*_pluginManager, _window.get(), width, height, dpi, false);
    if (FAILED(result))
    {
        return result;
    }

    result = _renderer.Initialize(_window.get(), _forceWarp, *_dashboardHost);
    if (FAILED(result))
    {
        _renderer.Shutdown();
        _dashboardHost->Shutdown();
        return result;
    }
    _rendererReady = true;
    PluginHost::Instance().SetUiInvalidateTarget(_window.get());
    RefreshPageEdgeAffordances();
    result = UpdateDashboardVisibility();
    if (SUCCEEDED(result))
    {
        _frameInvalidated = true;
    }
    return result;
}

HRESULT Application::ApplySettings(std::unique_ptr<AppSettings> settings) noexcept
{
    if (!settings || !_settings || !_pluginManager || !_dashboardHost)
    {
        return E_POINTER;
    }
    DismissWidgetRaise();
    CancelPageNavigation();
    if (*settings == *_settings)
    {
        return S_FALSE;
    }
    if (ActiveDashboardRuntimeEquals(*settings, *_settings))
    {
        _settings = std::move(settings);
        return S_OK;
    }

    _renderer.Shutdown();
    _rendererReady = false;
    _dashboardHost->Shutdown();

    HRESULT applyResult = _pluginManager->Reconfigure(*settings);
    if (SUCCEEDED(applyResult))
    {
        applyResult = InitializeDashboardRuntime();
    }
    if (SUCCEEDED(applyResult))
    {
        _settings = std::move(settings);
        return S_OK;
    }

    _renderer.Shutdown();
    _rendererReady = false;
    _dashboardHost->Shutdown();
    HRESULT rollbackResult = _pluginManager->Reconfigure(*_settings);
    if (SUCCEEDED(rollbackResult))
    {
        rollbackResult = InitializeDashboardRuntime();
    }
    if (FAILED(rollbackResult))
    {
        return rollbackResult;
    }
    return applyResult;
}

HRESULT Application::StageTransitionPage(int direction) noexcept
{
    if (!_settings || !_rendererReady || !_window || (direction != -1 && direction != 1))
    {
        return E_INVALIDARG;
    }
    if (_pageTransitionDirection == direction && _transitionDashboardHost)
    {
        return S_OK;
    }
    ClearTransitionPage();
    auto settings = std::unique_ptr<AppSettings>(new (std::nothrow) AppSettings(*_settings));
    if (!settings)
    {
        return E_OUTOFMEMORY;
    }
    HRESULT result = MoveDashboardPage(*settings, direction);
    if (FAILED(result))
    {
        return result;
    }
    auto plugins = std::unique_ptr<PluginManager>(new (std::nothrow) PluginManager());
    if (!plugins)
    {
        return E_OUTOFMEMORY;
    }
    result = plugins->Initialize(*settings);
    if (FAILED(result))
    {
        return result;
    }
    RECT client{};
    const UINT dpi = GetDpiForWindow(_window.get());
    if (dpi == 0 || !GetClientRect(_window.get(), &client) || client.right <= 0 || client.bottom <= 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    auto dashboard = std::unique_ptr<DashboardHost>(new (std::nothrow) DashboardHost());
    if (!dashboard)
    {
        return E_OUTOFMEMORY;
    }
    const bool visible = _windowVisible && _displayPoweredOn && !_renderer.IsSuspended() && !_renderer.IsOccluded();
    result = dashboard->Initialize(*plugins, _window.get(), static_cast<UINT>(client.right),
                                   static_cast<UINT>(client.bottom), dpi, visible);
    if (SUCCEEDED(result))
        result = dashboard->SetHorizontalOffset(_pageCurrentOffset + direction * client.right);
    if (SUCCEEDED(result))
        result = _renderer.SetTransitionDashboard(dashboard.get());
    if (FAILED(result))
    {
        dashboard->Shutdown();
        return result;
    }
    _transitionSettings = std::move(settings);
    _transitionPluginManager = std::move(plugins);
    _transitionDashboardHost = std::move(dashboard);
    _pageTransitionDirection = direction;
    return S_OK;
}

void Application::ClearTransitionPage() noexcept
{
    (void)_renderer.SetTransitionDashboard(nullptr);
    if (_transitionDashboardHost)
    {
        _transitionDashboardHost->Shutdown();
    }
    _transitionDashboardHost.reset();
    _transitionPluginManager.reset();
    _transitionSettings.reset();
    _pageTransitionDirection = 0;
    _pageStagePendingDirection = 0;
}

void Application::ApplyPageOffset(LONG offset, LONG clientWidth) noexcept
{
    if (!_dashboardHost)
    {
        return;
    }
    _pageCurrentOffset = offset;
    HRESULT layoutResult = _dashboardHost->SetHorizontalOffset(offset);
    if (SUCCEEDED(layoutResult) && _transitionDashboardHost && _pageTransitionDirection != 0)
    {
        layoutResult = _transitionDashboardHost->SetHorizontalOffset(offset + _pageTransitionDirection * clientWidth);
    }
    if (SUCCEEDED(layoutResult))
    {
        (void)_renderer.RefreshLayout();
    }
    _frameInvalidated = true;
}

void Application::FlushPendingTransitionStage() noexcept
{
    const int direction = _pageStagePendingDirection;
    if (direction == 0)
    {
        return;
    }
    _pageStagePendingDirection = 0;
    if (FAILED(StageTransitionPage(direction)))
    {
        return;
    }
    RECT client{};
    if (_window && GetClientRect(_window.get(), &client) && client.right > 0)
    {
        ApplyPageOffset(_pageCurrentOffset, client.right);
    }
}

HRESULT Application::PromoteTransitionPage() noexcept
{
    if (!_transitionDashboardHost || !_transitionPluginManager || !_transitionSettings || !_pluginManager ||
        !_dashboardHost)
    {
        return E_UNEXPECTED;
    }

    DashboardHost& incoming = *_transitionDashboardHost;
    HRESULT result = _renderer.AdoptPrimaryDashboard(incoming);
    if (FAILED(result))
    {
        return result;
    }

    std::unique_ptr<DashboardHost> retiringDashboard = std::move(_dashboardHost);
    std::unique_ptr<PluginManager> retiringPlugins = std::move(_pluginManager);
    _dashboardHost = std::move(_transitionDashboardHost);
    _pluginManager = std::move(_transitionPluginManager);
    _settings = std::move(_transitionSettings);
    _pageTransitionDirection = 0;
    _pageStagePendingDirection = 0;
    _pageCurrentOffset = 0;
    if (_dashboardHost)
    {
        (void)_dashboardHost->SetHorizontalOffset(0);
    }
    result = _renderer.RefreshLayout();
    RefreshPageEdgeAffordances();
    if (retiringDashboard)
    {
        retiringDashboard->Shutdown();
    }
    retiringPlugins.reset();
    retiringDashboard.reset();
    if (SUCCEEDED(result))
    {
        result = UpdateDashboardVisibility();
    }
    _frameInvalidated = true;
    return result;
}

void Application::BeginPageSettle(LONG targetOffset, bool commit) noexcept
{
    RECT client{};
    if (!_window || !GetClientRect(_window.get(), &client) || client.right <= 0)
    {
        CancelPageNavigation();
        return;
    }
    if (commit && !_transitionDashboardHost)
    {
        commit = false;
        targetOffset = 0;
    }

    _pageSettleActive = true;
    _pageSettleCommit = commit;
    _pageSettleStart = _pageCurrentOffset;
    _pageSettleTarget = targetOffset;
    _pagePanStarted = false;
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now) || _qpcFrequency == 0)
    {
        ApplyPageOffset(targetOffset, client.right);
        _pageSettleActive = false;
        if (commit)
        {
            if (FAILED(PromoteTransitionPage()))
            {
                ApplyPageOffset(0, client.right);
                ClearTransitionPage();
            }
        }
        else
        {
            ClearTransitionPage();
            ApplyPageOffset(0, client.right);
        }
        return;
    }
    _pageSettleStartQpc = static_cast<UINT64>(now.QuadPart);
    const UINT durationMs = PageSettleDurationMilliseconds(targetOffset - _pageCurrentOffset, _pageVelocityPxPerSec);
    _pageSettleDurationQpc = static_cast<UINT64>(durationMs) * _qpcFrequency / 1000ULL;
    if (_pageSettleDurationQpc == 0)
    {
        _pageSettleDurationQpc = _qpcFrequency / 10ULL;
    }
    _frameInvalidated = true;
}

void Application::TickPageSettle() noexcept
{
    if (!_pageSettleActive || !_window)
    {
        return;
    }
    RECT client{};
    if (!GetClientRect(_window.get(), &client) || client.right <= 0)
    {
        CancelPageNavigation();
        return;
    }

    float t = 1.0f;
    LARGE_INTEGER now{};
    if (_pageSettleDurationQpc != 0 && QueryPerformanceCounter(&now))
    {
        const UINT64 elapsed = static_cast<UINT64>(now.QuadPart) - _pageSettleStartQpc;
        t = elapsed >= _pageSettleDurationQpc
                ? 1.0f
                : static_cast<float>(elapsed) / static_cast<float>(_pageSettleDurationQpc);
    }
    const LONG offset = InterpolatePageOffset(_pageSettleStart, _pageSettleTarget, t);
    ApplyPageOffset(offset, client.right);
    if (t < 1.0f)
    {
        return;
    }

    _pageSettleActive = false;
    if (_pageSettleCommit)
    {
        if (FAILED(PromoteTransitionPage()))
        {
            ApplyPageOffset(0, client.right);
            ClearTransitionPage();
        }
        return;
    }
    ClearTransitionPage();
    ApplyPageOffset(0, client.right);
}

PageEdgeState Application::CurrentPageEdgeState() const noexcept
{
    PageEdgeState state{};
    if (!_settings)
    {
        return state;
    }
    state.rendererReady = _rendererReady;
    state.windowVisible = _windowVisible;
    state.displayPoweredOn = _displayPoweredOn;
    state.rendererSuspended = _renderer.IsSuspended();
    state.rendererOccluded = _renderer.IsOccluded();
    state.widgetRaised = _raisedActive;
    state.pointerNavigationActive = _pagePointerActive || _pagePanStarted;
    state.settleActive = _pageSettleActive;
    state.transitionStaged = _pageTransitionDirection != 0 || _pageStagePendingDirection != 0;
    state.wrapPages = _settings->dashboard.wrapPages;
    state.pageCount = _settings->dashboard.pageCount;
    state.atFirstPage = _settings->dashboard.activePageIndex == 0;
    state.atLastPage = state.pageCount == 0 || _settings->dashboard.activePageIndex + 1 >= state.pageCount;
    return state;
}

size_t Application::PageEdgeIndex(HWND window) const noexcept
{
    for (size_t index = 0; index < _pageEdges.size(); ++index)
    {
        if (_pageEdges[index].get() == window)
        {
            return index;
        }
    }
    return _pageEdges.size();
}

void Application::DestroyPageEdgeAffordances() noexcept
{
    _pageEdgeIconFont.reset();
    _pageEdgeIconFontDpi = 0;
    for (size_t index = 0; index < _pageEdges.size(); ++index)
    {
        _pageEdges[index].reset();
        _pageEdgeRevealed[index] = false;
        _pageEdgeBands[index] = RECT{};
    }
    _pageEdgeApplyValid = false;
}

void Application::RefreshPageEdgeAffordances() noexcept
{
    if (!_window)
    {
        DestroyPageEdgeAffordances();
        return;
    }
    RECT client{};
    if (!GetClientRect(_window.get(), &client) || client.right <= 0 || client.bottom <= 0)
    {
        DestroyPageEdgeAffordances();
        return;
    }

    const PageEdgeState state = CurrentPageEdgeState();
    const UINT dpi = GetDpiForWindow(_window.get());
    const SIZE clientSize{client.right, client.bottom};
    const RECT reachable = ReachableClientRect();
    if (_pageEdgeApplyValid && _pageEdgeApplied == state && _pageEdgeAppliedDpi == dpi &&
        _pageEdgeAppliedClient.cx == clientSize.cx && _pageEdgeAppliedClient.cy == clientSize.cy &&
        EqualRect(&_pageEdgeAppliedReachable, &reachable))
    {
        return;
    }
    _pageEdgeApplied = state;
    _pageEdgeAppliedDpi = dpi;
    _pageEdgeAppliedClient = clientSize;
    _pageEdgeAppliedReachable = reachable;
    _pageEdgeApplyValid = true;

    for (size_t index = 0; index < _pageEdges.size(); ++index)
    {
        const int direction = index == 0 ? kPageEdgeDirectionPrevious : kPageEdgeDirectionNext;
        const RECT band = PageEdgeBandRectIn(reachable, direction, dpi);
        const bool allowed = ShouldShowEdgeAffordance(state, direction) && band.right > band.left;
        _pageEdgeBands[index] = allowed ? band : RECT{};
        if (!allowed)
        {
            _pageEdgeRevealed[index] = false;
        }

        // The band window exists only while the pointer is inside its zone, the same way the raise overlay HWND
        // exists only while a widget is raised. It is created opaque: a layered child at zero alpha is transparent to
        // hit testing, so an always-present invisible band would never receive a mouse message at all.
        if (!allowed || !_pageEdgeRevealed[index])
        {
            _pageEdges[index].reset();
            continue;
        }

        if (!_pageEdges[index])
        {
            const HWND edge =
                CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_NOPARENTNOTIFY | WS_EX_LAYERED, kPageEdgeClassName, L"",
                                WS_CHILD | WS_CLIPSIBLINGS, band.left, band.top, band.right - band.left,
                                band.bottom - band.top, _window.get(), nullptr, _instance, this);
            if (!edge)
            {
                _pageEdgeRevealed[index] = false;
                continue;
            }
            _pageEdges[index].reset(edge);
            (void)SetLayeredWindowAttributes(edge, 0, kPageEdgeRevealedAlpha, LWA_ALPHA);
            ShowWindow(edge, SW_SHOWNA);
            // Leave tracking is armed from the band's first WM_MOUSEMOVE, not here. TrackMouseEvent posts
            // WM_MOUSELEAVE immediately when the cursor is not already inside the window, and a child created under
            // the cursor has not been hit-tested onto yet, so arming here makes the band destroy itself at once.
        }
        else if (!EqualRect(&_pageEdgeBands[index], &band))
        {
            (void)SetWindowPos(_pageEdges[index].get(), nullptr, band.left, band.top, band.right - band.left,
                               band.bottom - band.top, SWP_NOACTIVATE | SWP_NOZORDER);
        }
        // Bands stay above host-owned native containers so they reveal and accept clicks over a window widget too.
        (void)SetWindowPos(_pageEdges[index].get(), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

RECT Application::ReachableClientRect() const noexcept
{
    RECT client{};
    if (!_window || !GetClientRect(_window.get(), &client))
    {
        return RECT{};
    }
    RECT windowBounds{};
    if (!GetWindowRect(_window.get(), &windowBounds))
    {
        return client;
    }

    // Collect the work area of every display the window touches, converted to client coordinates. The union and
    // intersection rule itself lives in PageEdgeAffordance.h so it can be tested without a display configuration.
    struct WorkAreaCollector final
    {
        HWND window = nullptr;
        std::array<RECT, kPageEdgeMaximumWorkAreas> areas{};
        size_t count = 0;
    } collector;
    collector.window = _window.get();

    const auto collect = [](HMONITOR monitor, HDC, LPRECT, LPARAM data) noexcept -> BOOL
    {
        auto* target = reinterpret_cast<WorkAreaCollector*>(data);
        if (target->count >= target->areas.size())
        {
            return FALSE;
        }
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (!GetMonitorInfoW(monitor, &monitorInfo))
        {
            return TRUE;
        }
        POINT topLeft{monitorInfo.rcWork.left, monitorInfo.rcWork.top};
        POINT bottomRight{monitorInfo.rcWork.right, monitorInfo.rcWork.bottom};
        if (!ScreenToClient(target->window, &topLeft) || !ScreenToClient(target->window, &bottomRight))
        {
            return TRUE;
        }
        target->areas[target->count] = RECT{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
        ++target->count;
        return TRUE;
    };
    (void)EnumDisplayMonitors(nullptr, &windowBounds, collect, reinterpret_cast<LPARAM>(&collector));
    return PageEdgeReachableClient(client, collector.areas.data(), collector.count);
}

void Application::UpdatePageEdgeHover() noexcept
{
    if (!_window)
    {
        return;
    }
    POINT cursor{};
    if (!GetCursorPos(&cursor) || !ScreenToClient(_window.get(), &cursor))
    {
        ClearPageEdgeHover();
        return;
    }

    const PageEdgeState state = CurrentPageEdgeState();
    const RECT reachable = ReachableClientRect();
    if (reachable.right <= reachable.left || reachable.bottom <= reachable.top)
    {
        ClearPageEdgeHover();
        return;
    }
    const UINT dpi = GetDpiForWindow(_window.get());

    bool changed = false;
    for (size_t index = 0; index < _pageEdges.size(); ++index)
    {
        const int direction = index == 0 ? kPageEdgeDirectionPrevious : kPageEdgeDirectionNext;
        const RECT band = PageEdgeBandRectIn(reachable, direction, dpi);
        const bool reveal = ShouldShowEdgeAffordance(state, direction) && PageEdgeBandContains(band, cursor);
        if (_pageEdgeRevealed[index] != reveal)
        {
            _pageEdgeRevealed[index] = reveal;
            changed = true;
        }
    }
    if (changed)
    {
        // Reveal state is not part of the cached apply key, so force the next refresh to act on it.
        _pageEdgeApplyValid = false;
        RefreshPageEdgeAffordances();
    }
}

void Application::ClearPageEdgeHover() noexcept
{
    bool changed = false;
    for (bool& revealed : _pageEdgeRevealed)
    {
        changed = changed || revealed;
        revealed = false;
    }
    if (changed)
    {
        _pageEdgeApplyValid = false;
        RefreshPageEdgeAffordances();
    }
}

void Application::SetPageEdgeRevealed(size_t index, bool revealed) noexcept
{
    if (index >= _pageEdgeRevealed.size() || _pageEdgeRevealed[index] == revealed)
    {
        return;
    }
    // Instant reveal, by creating or destroying the band window. A cross-fade would need a timer, and
    // Core_PerformanceAndResources.md prohibits wake-ups that visible content does not require.
    _pageEdgeRevealed[index] = revealed;
    _pageEdgeApplyValid = false;
    RefreshPageEdgeAffordances();
}

HRESULT Application::NavigateToAdjacentPage(int direction) noexcept
{
    if (!_window || !_rendererReady || _raisedActive || _pageSettleActive || _pagePointerActive)
    {
        return E_UNEXPECTED;
    }
    RECT client{};
    if (!GetClientRect(_window.get(), &client) || client.right <= 0)
    {
        return E_UNEXPECTED;
    }
    if (!ShouldShowEdgeAffordance(CurrentPageEdgeState(), direction))
    {
        return S_FALSE;
    }

    const HRESULT staged = StageTransitionPage(direction);
    if (FAILED(staged))
    {
        return staged;
    }
    ApplyPageOffset(0, client.right);
    // A click has no follow-finger phase, so the settle runs from rest. Zero velocity puts
    // PageSettleDurationMilliseconds at its clamped upper bound, giving one ease-out slide.
    _pageVelocityPxPerSec = 0.0f;
    // A navigation click must never count as half of a double-activate raise gesture.
    _activateTick = 0;
    _activateWidgetIndex = SIZE_MAX;
    BeginPageSettle(PageEdgeSettleTarget(direction, client.right), true);
    RefreshPageEdgeAffordances();
    return S_OK;
}

LRESULT CALLBACK Application::PageEdgeProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    Application* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        application = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    }
    if (application)
    {
        return application->HandlePageEdgeMessage(window, application->PageEdgeIndex(window), message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::HandlePageEdgeMessage(HWND edge, size_t index, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    const int direction = index == 0 ? kPageEdgeDirectionPrevious : kPageEdgeDirectionNext;
    switch (message)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintPageEdge(edge, index);
        return 0;
    case WM_MOUSEMOVE:
    {
        if (index >= _pageEdges.size())
        {
            break;
        }
        // The cursor is genuinely inside the band here, so arming leave tracking now cannot fire spuriously.
        TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, edge, 0};
        (void)TrackMouseEvent(&track);
        UpdatePageEdgeHover();
        return 0;
    }
    case WM_MOUSELEAVE:
        UpdatePageEdgeHover();
        return 0;
    case WM_LBUTTONUP:
        if (index < _pageEdges.size() && !IsPointerSynthesizedMouseMessage())
        {
            SetPageEdgeRevealed(index, false);
            (void)NavigateToAdjacentPage(direction);
        }
        return 0;
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
    case WM_POINTERCAPTURECHANGED:
        // Touch and pen keep the swipe contract. Forward to the top-level window exactly as host-owned native
        // containers do, so a pan that starts inside a band behaves like one that starts anywhere else.
        if (_window)
        {
            return SendMessageW(_window.get(), message, wParam, lParam);
        }
        break;
    case WM_NCDESTROY:
        SetWindowLongPtrW(edge, GWLP_USERDATA, 0);
        for (wil::unique_hwnd& candidate : _pageEdges)
        {
            if (candidate.get() == edge)
            {
                (void)candidate.release();
            }
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(edge, message, wParam, lParam);
}

void Application::EnsurePageEdgeIconFont(UINT dpi) noexcept
{
    const UINT effectiveDpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
    if (_pageEdgeIconFont && _pageEdgeIconFontDpi == effectiveDpi)
    {
        return;
    }
    FluentIcons::IconFont kind = FluentIcons::IconFont::TextFallback;
    wil::unique_hfont font{FluentIcons::CreateIconFont(PageEdgeChevronPixelHeight(effectiveDpi), kind)};
    if (!font)
    {
        return;
    }
    _pageEdgeIconFont = std::move(font);
    _pageEdgeIconFontDpi = effectiveDpi;
    _pageEdgeIconFontKind = kind;
}

void Application::PaintPageEdge(HWND edge, size_t index) noexcept
{
    PAINTSTRUCT paint{};
    const HDC deviceContext = BeginPaint(edge, &paint);
    if (!deviceContext)
    {
        return;
    }
    RECT client{};
    GetClientRect(edge, &client);
    if (index < _pageEdges.size())
    {
        const UINT dpi = GetDpiForWindow(edge);
        if (!_pageEdgeWashBrush)
        {
            _pageEdgeWashBrush.reset(CreateSolidBrush(RGB(10, 14, 26)));
        }
        if (_pageEdgeWashBrush)
        {
            FillRect(deviceContext, &client, _pageEdgeWashBrush.get());
        }
        EnsurePageEdgeIconFont(dpi);
        const int direction = index == 0 ? kPageEdgeDirectionPrevious : kPageEdgeDirectionNext;
        const wchar_t glyph = PageEdgeChevronGlyph(direction, _pageEdgeIconFontKind);
        if (_pageEdgeIconFont && glyph != L'\0')
        {
            RECT cell = PageEdgeChevronCell(client, dpi);
            const HGDIOBJ previousFont = SelectObject(deviceContext, _pageEdgeIconFont.get());
            const int previousMode = SetBkMode(deviceContext, TRANSPARENT);
            const COLORREF previousColor = SetTextColor(deviceContext, RGB(228, 236, 248));
            (void)DrawTextW(deviceContext, &glyph, 1, &cell,
                            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX);
            (void)SetTextColor(deviceContext, previousColor);
            (void)SetBkMode(deviceContext, previousMode);
            SelectObject(deviceContext, previousFont);
        }
    }
    EndPaint(edge, &paint);
}

void Application::CancelPageNavigation() noexcept
{
    if (_pagePointerCaptured && _window && _pagePointerId != 0)
    {
        HostReleasePointerCapture(_window.get(), _pagePointerId);
    }
    _pagePointerCaptured = false;
    _pagePointerActive = false;
    _pagePanStarted = false;
    _pageGestureIgnored = false;
    _pageSettleActive = false;
    _pageSettleCommit = false;
    _pagePointerId = 0;
    _pagePointerQpc = 0;
    _pageVelocityPxPerSec = 0.0f;
    _pageCurrentOffset = 0;
    _pageStagePendingDirection = 0;
    ClearTransitionPage();
    if (_dashboardHost)
    {
        (void)_dashboardHost->SetHorizontalOffset(0);
    }
    if (_rendererReady)
    {
        (void)_renderer.RefreshLayout();
    }
    _frameInvalidated = true;
    RefreshPageEdgeAffordances();
}

bool Application::TryPointerClientPosition(HWND window, UINT32 pointerId, POINT& position, UINT64& qpc) const noexcept
{
    POINTER_INFO information{};
    if (!GetPointerInfo(pointerId, &information) ||
        (information.pointerType != PT_TOUCH && information.pointerType != PT_PEN))
    {
        return false;
    }
    if ((information.pointerFlags & POINTER_FLAG_CANCELED) != 0)
    {
        return false;
    }
    position = information.ptPixelLocation;
    if (!ScreenToClient(window, &position))
    {
        return false;
    }
    qpc = information.PerformanceCount;
    return true;
}

void Application::OnPointerDown(HWND window, WPARAM wParam) noexcept
{
    if (_raisedActive)
    {
        return;
    }
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    POINT position{};
    UINT64 qpc = 0;
    if (_pagePointerActive || !TryPointerClientPosition(window, pointerId, position, qpc))
    {
        return;
    }

    if (_pageSettleActive)
    {
        _pageSettleActive = false;
        _pagePointerStartX = position.x - _pageCurrentOffset;
        _pagePointerStartY = position.y;
        _pagePanStarted = _pageCurrentOffset != 0;
        _pageGestureIgnored = false;
    }
    else
    {
        _pagePointerStartX = position.x;
        _pagePointerStartY = position.y;
        _pagePanStarted = false;
        _pageGestureIgnored = false;
        _pageCurrentOffset = 0;
    }

    _pagePointerId = pointerId;
    _pagePointerX = position.x;
    _pagePointerQpc = qpc;
    _pageVelocityPxPerSec = 0.0f;
    _pagePointerActive = true;
    _pagePointerCaptured = false;
    _frameInvalidated = true;
}

void Application::OnPointerUpdate(HWND window, WPARAM wParam) noexcept
{
    if (_raisedActive)
    {
        return;
    }
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    if (!_pagePointerActive || pointerId != _pagePointerId || _pageGestureIgnored)
    {
        return;
    }
    POINT position{};
    UINT64 qpc = 0;
    if (!TryPointerClientPosition(window, pointerId, position, qpc))
    {
        CancelPageNavigation();
        return;
    }
    RECT client{};
    if (!GetClientRect(window, &client) || client.right <= 0)
    {
        return;
    }

    const LONG deltaX = position.x - _pagePointerStartX;
    const LONG deltaY = position.y - _pagePointerStartY;
    const UINT dpi = GetDpiForWindow(window);
    const LONG threshold = PageSwipeThresholdPixels(dpi);
    if (!_pagePanStarted)
    {
        if (PageSwipeRejectsAsVertical(deltaX, deltaY, threshold))
        {
            _pageGestureIgnored = true;
            _pagePointerActive = false;
            ResumePageSettleIfNeeded();
            return;
        }
        if (!PageSwipeLocksHorizontal(deltaX, deltaY, threshold))
        {
            _pagePointerX = position.x;
            return;
        }
        _pagePanStarted = true;
        _pageSettleActive = false;
        if (HostSetPointerCapture(window, pointerId))
        {
            _pagePointerCaptured = true;
        }
        POINTER_INFO information{};
        if (GetPointerInfo(pointerId, &information) && information.hwndTarget && information.hwndTarget != window)
        {
            SendMessageW(information.hwndTarget, WM_CANCELMODE, 0, 0);
        }
    }

    if (_pagePointerQpc != 0 && qpc > _pagePointerQpc && _qpcFrequency != 0)
    {
        const double dt = static_cast<double>(qpc - _pagePointerQpc) / static_cast<double>(_qpcFrequency);
        if (dt > 0.0005 && dt < 0.08)
        {
            const float instant = static_cast<float>(static_cast<double>(position.x - _pagePointerX) / dt);
            _pageVelocityPxPerSec = _pageVelocityPxPerSec * 0.55f + instant * 0.45f;
        }
    }
    _pagePointerX = position.x;
    _pagePointerQpc = qpc;

    const bool atFirst = _settings && _settings->dashboard.activePageIndex == 0;
    const bool atLast = _settings && _settings->dashboard.activePageIndex + 1U >= _settings->dashboard.pageCount;
    const bool wrapPages = _settings && _settings->dashboard.wrapPages;
    const bool blocked = PageSwipeBlocksDirection(deltaX, wrapPages, atFirst, atLast);
    const LONG offset = ApplyPageEdgeResistance(deltaX, client.right, blocked);
    const int direction = blocked ? 0 : PageSwipeDirection(offset);
    if (direction != 0 && direction != _pageTransitionDirection && direction != _pageStagePendingDirection)
    {
        if (_pageTransitionDirection != 0)
        {
            ClearTransitionPage();
        }
        _pageStagePendingDirection = direction;
    }
    ApplyPageOffset(offset, client.right);
}

void Application::OnPointerUp(HWND window, WPARAM wParam) noexcept
{
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    if (!_pagePointerActive || pointerId != _pagePointerId)
    {
        return;
    }
    if (_pagePointerCaptured)
    {
        HostReleasePointerCapture(window, pointerId);
        _pagePointerCaptured = false;
    }
    _pagePointerActive = false;
    const bool panStarted = _pagePanStarted;
    _pagePanStarted = false;
    _pagePointerId = 0;
    if (!panStarted)
    {
        _pageGestureIgnored = false;
        if (_pageCurrentOffset != 0 || _pageTransitionDirection != 0)
        {
            ResumePageSettleIfNeeded();
            return;
        }
        POINT position{};
        UINT64 qpc = 0;
        if (TryPointerClientPosition(window, pointerId, position, qpc))
        {
            OnClientActivateAttempt(window, position, GetTickCount64());
        }
        return;
    }

    FlushPendingTransitionStage();
    RECT client{};
    if (!_settings || !GetClientRect(window, &client) || client.right <= 0)
    {
        CancelPageNavigation();
        return;
    }
    const bool atFirst = _settings->dashboard.activePageIndex == 0;
    const bool atLast = _settings->dashboard.activePageIndex + 1U >= _settings->dashboard.pageCount;
    const UINT dpi = GetDpiForWindow(window);
    const bool commit = ShouldCommitPageSwipe(_pageCurrentOffset, client.right, _pageVelocityPxPerSec, dpi,
                                              _settings->dashboard.wrapPages, atFirst, atLast) &&
                        _transitionDashboardHost;
    const LONG target = commit ? -_pageTransitionDirection * client.right : 0;
    BeginPageSettle(target, commit);
}

void Application::OnMouseButtonUp(HWND window, LPARAM lParam) noexcept
{
    if (_raisedActive || _pagePointerActive || _pagePanStarted)
    {
        return;
    }
    const POINT position{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    OnClientActivateAttempt(window, position, GetTickCount64());
}

void Application::OnClientActivateAttempt(HWND window, POINT position, ULONGLONG tick) noexcept
{
    if (_raisedActive || !_dashboardHost || !_rendererReady)
    {
        return;
    }
    RECT client{};
    if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0)
    {
        return;
    }

    std::array<RECT, PluginManager::kMaximumWidgetInstances> bounds{};
    const size_t widgetCount = _dashboardHost->WidgetCount();
    for (size_t index = 0; index < widgetCount; ++index)
    {
        bounds[index] =
            _dashboardHost->PixelBoundsAt(index, static_cast<UINT>(client.right), static_cast<UINT>(client.bottom));
    }
    const size_t hit = HitTestTopmostWidget(position, bounds.data(), widgetCount);
    if (hit == SIZE_MAX)
    {
        _activateWidgetIndex = SIZE_MAX;
        return;
    }

    const UINT dpi = GetDpiForWindow(window);
    const UINT interval = GetDoubleClickTime();
    if (_activateWidgetIndex == hit &&
        IsDoubleActivate(_activateTick, _activatePoint, tick, position, interval, DoubleActivateSlopPixels(dpi)))
    {
        _activateWidgetIndex = SIZE_MAX;
        (void)TryRaiseWidgetAt(window, hit);
        return;
    }
    _activateTick = tick;
    _activatePoint = position;
    _activateWidgetIndex = hit;
}

HRESULT Application::TryRaiseWidgetAt(HWND window, size_t widgetIndex) noexcept
{
    if (_raisedActive || !_dashboardHost || !_rendererReady || widgetIndex >= _dashboardHost->WidgetCount())
    {
        return E_UNEXPECTED;
    }
    RECT client{};
    if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0)
    {
        return E_UNEXPECTED;
    }
    const UINT clientWidth = static_cast<UINT>(client.right);
    const UINT clientHeight = static_cast<UINT>(client.bottom);
    const RECT tile = _dashboardHost->PixelBoundsAt(widgetIndex, clientWidth, clientHeight);
    IRedXeRaisedWidget* raisedWidget = _dashboardHost->RaisedWidgetAt(widgetIndex);
    if (!raisedWidget)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
    RedXeRaisedExtent extent{};
    const HRESULT extentResult = raisedWidget->GetRaisedExtent(&extent);
    if (FAILED(extentResult) || !CanRaiseWidget(tile, clientWidth, clientHeight, extent))
    {
        return FAILED(extentResult) ? extentResult : HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }

    const UINT dpi = GetDpiForWindow(window);
    const RaisedLayout layout = MakeRaisedLayout(clientWidth, clientHeight, extent, dpi, &tile);
    if (layout.content.right <= layout.content.left || layout.content.bottom <= layout.content.top)
    {
        return E_UNEXPECTED;
    }

    CancelPageNavigation();
    HRESULT result = raisedWidget->SetRaised(TRUE);
    if (FAILED(result))
    {
        return result;
    }
    result = _dashboardHost->ApplyRaisedNativeLayout(widgetIndex, layout.content, dpi);
    if (FAILED(result))
    {
        (void)raisedWidget->SetRaised(FALSE);
        return result;
    }
    result = _renderer.SetRaisedOverlay(widgetIndex, layout.content);
    if (FAILED(result))
    {
        (void)_dashboardHost->ClearRaisedNativeLayout(dpi);
        (void)raisedWidget->SetRaised(FALSE);
        return result;
    }

    const HWND overlay =
        CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_NOPARENTNOTIFY | WS_EX_LAYERED, kRaiseOverlayClassName, L"",
                        WS_CHILD | WS_CLIPSIBLINGS, 0, 0, static_cast<int>(clientWidth), static_cast<int>(clientHeight),
                        window, nullptr, _instance, this);
    if (!overlay)
    {
        _renderer.ClearRaisedOverlay();
        (void)_dashboardHost->ClearRaisedNativeLayout(dpi);
        (void)raisedWidget->SetRaised(FALSE);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    _raiseOverlay.reset(overlay);
    if (!SetLayeredWindowAttributes(overlay, 0, 148, LWA_ALPHA))
    {
        _raiseOverlay.reset();
        _renderer.ClearRaisedOverlay();
        (void)_dashboardHost->ClearRaisedNativeLayout(dpi);
        (void)raisedWidget->SetRaised(FALSE);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const HRGN region = CreateRaisedOverlayRegion(clientWidth, clientHeight, layout.content, layout.close);
    if (!region || !SetWindowRgn(overlay, region, TRUE))
    {
        if (region)
        {
            DeleteObject(region);
        }
        _raiseOverlay.reset();
        _renderer.ClearRaisedOverlay();
        (void)_dashboardHost->ClearRaisedNativeLayout(dpi);
        (void)raisedWidget->SetRaised(FALSE);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    SetWindowPos(overlay, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(overlay, SW_SHOWNA);

    _raisedLayout = layout;
    _raisedWidgetIndex = widgetIndex;
    _raisedActive = true;
    EnsureRaiseOverlayChrome(dpi);
    RefreshPageEdgeAffordances();
    _frameInvalidated = true;
    return S_OK;
}

void Application::DismissWidgetRaise() noexcept
{
    const auto refreshEdges = wil::scope_exit([this]() noexcept { RefreshPageEdgeAffordances(); });
    if (!_raisedActive)
    {
        _raiseOverlay.reset();
        return;
    }

    IRedXeRaisedWidget* raisedWidget =
        _dashboardHost && _raisedWidgetIndex != SIZE_MAX ? _dashboardHost->RaisedWidgetAt(_raisedWidgetIndex) : nullptr;
    if (raisedWidget)
    {
        (void)raisedWidget->SetRaised(FALSE);
    }
    _renderer.ClearRaisedOverlay();
    if (_dashboardHost && _window)
    {
        const UINT dpi = GetDpiForWindow(_window.get());
        if (dpi != 0)
        {
            (void)_dashboardHost->ClearRaisedNativeLayout(dpi);
        }
    }
    _raiseOverlay.reset();
    _raisedActive = false;
    _raisedWidgetIndex = SIZE_MAX;
    _raisedLayout = {};
    _frameInvalidated = true;
}

LRESULT CALLBACK Application::RaiseOverlayProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    Application* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        application = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    }
    if (application)
    {
        return application->HandleRaiseOverlayMessage(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::HandleRaiseOverlayMessage(HWND overlay, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (message)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintRaiseOverlay(overlay);
        return 0;
    case WM_LBUTTONUP:
    {
        const POINT position{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (PointInRectInclusive(_raisedLayout.close, position))
        {
            DismissWidgetRaise();
        }
        return 0;
    }
    case WM_POINTERUP:
    {
        POINT position{};
        UINT64 qpc = 0;
        if (TryPointerClientPosition(_window.get(), GET_POINTERID_WPARAM(wParam), position, qpc) &&
            PointInRectInclusive(_raisedLayout.close, position))
        {
            DismissWidgetRaise();
        }
        return 0;
    }
    case WM_NCDESTROY:
        SetWindowLongPtrW(overlay, GWLP_USERDATA, 0);
        if (_raiseOverlay.get() == overlay)
        {
            (void)_raiseOverlay.release();
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(overlay, message, wParam, lParam);
}

void Application::EnsureRaiseOverlayChrome(UINT dpi) noexcept
{
    const UINT effectiveDpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
    if (!_raiseDimBrush)
    {
        _raiseDimBrush.reset(CreateSolidBrush(RGB(8, 10, 16)));
    }
    if (!_raiseShadowBrush)
    {
        _raiseShadowBrush.reset(CreateSolidBrush(RGB(0, 0, 0)));
    }
    if (_raiseCloseFont && _raiseCloseFontDpi == effectiveDpi)
    {
        return;
    }
    const LONG closeHeight = std::max(1L, _raisedLayout.close.bottom - _raisedLayout.close.top);
    FluentIcons::IconFont kind = FluentIcons::IconFont::TextFallback;
    wil::unique_hfont font{FluentIcons::CreateIconFont(static_cast<int>(closeHeight), kind)};
    if (!font)
    {
        return;
    }
    _raiseCloseFont = std::move(font);
    _raiseCloseFontDpi = effectiveDpi;
    _raiseCloseFontKind = kind;
}

void Application::PaintRaiseOverlay(HWND overlay) noexcept
{
    PAINTSTRUCT paint{};
    const HDC deviceContext = BeginPaint(overlay, &paint);
    if (!deviceContext)
    {
        return;
    }
    RECT client{};
    GetClientRect(overlay, &client);
    EnsureRaiseOverlayChrome(GetDpiForWindow(overlay));
    if (_raiseDimBrush)
    {
        FillRect(deviceContext, &client, _raiseDimBrush.get());
    }
    if (_raiseShadowBrush && _raisedLayout.shadow.right > _raisedLayout.shadow.left)
    {
        FillRect(deviceContext, &_raisedLayout.shadow, _raiseShadowBrush.get());
    }
    const wchar_t glyph =
        FluentIcons::SelectGlyph(_raiseCloseFontKind, FluentIcons::kClear, FluentIcons::kFallbackClear);
    if (_raiseCloseFont && glyph != L'\0' && _raisedLayout.close.right > _raisedLayout.close.left)
    {
        RECT close = _raisedLayout.close;
        const HGDIOBJ previousFont = SelectObject(deviceContext, _raiseCloseFont.get());
        const int previousMode = SetBkMode(deviceContext, TRANSPARENT);
        const COLORREF previousColor = SetTextColor(deviceContext, RGB(240, 240, 240));
        (void)DrawTextW(deviceContext, &glyph, 1, &close,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX);
        (void)SetTextColor(deviceContext, previousColor);
        (void)SetBkMode(deviceContext, previousMode);
        SelectObject(deviceContext, previousFont);
    }
    EndPaint(overlay, &paint);
}

void Application::OnSettingsChanged() noexcept
{
    _settingsWatcher.AcknowledgeNotification();

    std::unique_ptr<AppSettings> candidate;
    SettingsFileStamp stamp{};
    SettingsReloadStatus status = SettingsReloadStatus::Unchanged;
    const HRESULT loadResult = _settingsStore.TryLoadChanged(candidate, stamp, status);
    if (FAILED(loadResult))
    {
        ShowSettingsError(L"The settings file could not be read. The current dashboard remains active.");
        return;
    }

    switch (status)
    {
    case SettingsReloadStatus::Unchanged:
        return;
    case SettingsReloadStatus::Missing:
        ShowSettingsError(L"The settings file is missing. The current dashboard remains active.");
        return;
    case SettingsReloadStatus::Invalid:
        ShowSettingsError(_settingsStore.LastDiagnosticText().empty()
                              ? L"The settings file is invalid. The current dashboard remains active."
                              : _settingsStore.LastDiagnosticText());
        return;
    case SettingsReloadStatus::Loaded:
        break;
    }

    if (!candidate || !_settings)
    {
        OutputDebugStringW(L"Settings reload returned no candidate; the current settings remain active.\n");
        return;
    }

    if (FAILED(PreserveActiveDashboardPage(*_settings, *candidate)))
    {
        ShowSettingsError(L"The changed settings could not be applied. The previous dashboard was restored.");
        return;
    }

    const HRESULT applyResult = ApplySettings(std::move(candidate));
    if (SUCCEEDED(applyResult))
    {
        _settingsStore.MarkApplied(stamp);
        CloseSettingsError();
        OutputDebugStringW(L"RedXe settings were reloaded live.\n");
        return;
    }

    _settingsStore.MarkRejected(stamp);
    ShowSettingsError(L"The changed settings could not be applied. The previous dashboard was restored.");
    if (!_rendererReady)
    {
        _runtimeFailure = applyResult;
        CloseMainWindow();
    }
}

void Application::ShowSettingsError(std::wstring_view message) noexcept
{
    try
    {
        if (_settingsErrorDialog && IsWindow(_settingsErrorDialog))
        {
            const HWND text = GetDlgItem(_settingsErrorDialog, 100);
            if (text)
                SetWindowTextW(text, std::wstring(message).c_str());
            return;
        }
        RECT owner{};
        GetWindowRect(_window.get(), &owner);
        constexpr int width = 560;
        constexpr int height = 230;
        const int x = owner.left + ((owner.right - owner.left) - width) / 2;
        const int y = owner.top + ((owner.bottom - owner.top) - height) / 2;
        _settingsErrorDialog = CreateWindowExW(WS_EX_DLGMODALFRAME, kSettingsDialogClassName, L"RedXe settings error",
                                               WS_CAPTION | WS_SYSMENU | WS_VISIBLE, x, y, width, height, _window.get(),
                                               nullptr, _instance, this);
        if (!_settingsErrorDialog)
        {
            return;
        }
        EnableWindow(_window.get(), FALSE);
        const HWND text =
            CreateWindowExW(0, L"STATIC", std::wstring(message).c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT, 24, 24,
                            width - 48, 120, _settingsErrorDialog, reinterpret_cast<HMENU>(100), _instance, nullptr);
        const HWND button =
            CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, width - 120, height - 78, 80,
                            28, _settingsErrorDialog, reinterpret_cast<HMENU>(IDOK), _instance, nullptr);
        const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (text)
            SendMessageW(text, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (button)
        {
            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SetFocus(button);
        }
    }
    catch (...)
    {
        OutputDebugStringW(L"The settings error dialog could not be created.\n");
    }
}

void Application::CloseSettingsError() noexcept
{
    const HWND dialog = _settingsErrorDialog;
    _settingsErrorDialog = nullptr;
    if (dialog && IsWindow(dialog))
    {
        DestroyWindow(dialog);
    }
    if (_window && IsWindow(_window.get()))
    {
        EnableWindow(_window.get(), TRUE);
    }
}

bool Application::WaitUntilMessage() noexcept
{
    if (!_windowVisible || !_displayPoweredOn || !_rendererReady || _renderer.IsSuspended() || _renderer.IsOccluded() ||
        DashboardRequiresContinuousFrames())
    {
        ClearScheduledFrameDeadline();
    }

    DWORD timeoutMilliseconds = INFINITE;
    if (_scheduledFrameDeadlineTick != 0)
    {
        const ULONGLONG now = GetTickCount64();
        if (now >= _scheduledFrameDeadlineTick)
        {
            _scheduledFrameDeadlineTick = 0;
            _frameInvalidated = true;
            return true;
        }
        const ULONGLONG remaining = _scheduledFrameDeadlineTick - now;
        timeoutMilliseconds = static_cast<DWORD>(remaining);
    }

    const DWORD waitResult =
        MsgWaitForMultipleObjectsEx(0, nullptr, timeoutMilliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (waitResult == WAIT_OBJECT_0)
    {
        return true;
    }
    if (waitResult == WAIT_TIMEOUT)
    {
        _scheduledFrameDeadlineTick = 0;
        _frameInvalidated = true;
        return true;
    }

    const DWORD error = waitResult == WAIT_FAILED ? GetLastError() : ERROR_INVALID_DATA;
    _runtimeFailure = error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    CloseMainWindow();
    return false;
}

bool Application::DashboardRequiresContinuousFrames() const noexcept
{
    return (_dashboardHost && _dashboardHost->RequiresContinuousFrames()) ||
           (_transitionDashboardHost && _transitionDashboardHost->RequiresContinuousFrames());
}

bool Application::PageNavigationInProgress() const noexcept
{
    return _pagePointerActive || _pagePanStarted || _pageSettleActive || _pageTransitionDirection != 0 ||
           _pageStagePendingDirection != 0;
}

void Application::ResumePageSettleIfNeeded() noexcept
{
    if (_pageSettleActive || _pagePointerActive || _pagePanStarted)
    {
        return;
    }
    if (_pageCurrentOffset == 0 && _pageTransitionDirection == 0)
    {
        return;
    }
    RECT client{};
    if (!_window || !GetClientRect(_window.get(), &client) || client.right <= 0)
    {
        CancelPageNavigation();
        return;
    }
    const bool commit = _pageSettleCommit && _transitionDashboardHost;
    const LONG target = commit ? -_pageTransitionDirection * client.right : 0;
    BeginPageSettle(target, commit);
}

void Application::RefreshScheduledFrameDeadline() noexcept
{
    ClearScheduledFrameDeadline();
    if (!_windowVisible || !_displayPoweredOn || !_rendererReady || _renderer.IsSuspended() || _renderer.IsOccluded() ||
        DashboardRequiresContinuousFrames() || PageNavigationInProgress())
    {
        return;
    }

    uint32_t earliest = 0;
    uint32_t candidate = 0;
    if (_dashboardHost && _dashboardHost->GetNextFrameDelayMilliseconds(&candidate) == S_OK)
    {
        earliest = candidate;
    }
    if (_transitionDashboardHost && _transitionDashboardHost->GetNextFrameDelayMilliseconds(&candidate) == S_OK &&
        (earliest == 0 || candidate < earliest))
    {
        earliest = candidate;
    }
    if (earliest != 0)
    {
        _scheduledFrameDeadlineTick = GetTickCount64() + earliest;
    }
}

void Application::ClearScheduledFrameDeadline() noexcept
{
    _scheduledFrameDeadlineTick = 0;
}

HRESULT Application::UpdateDashboardVisibility() noexcept
{
    RefreshPageEdgeAffordances();
    if (!_dashboardHost)
    {
        return S_OK;
    }

    const bool visible =
        _windowVisible && _displayPoweredOn && _rendererReady && !_renderer.IsSuspended() && !_renderer.IsOccluded();
    HRESULT result = _dashboardHost->SetWidgetsVisible(visible);
    if (SUCCEEDED(result) && _transitionDashboardHost)
    {
        result = _transitionDashboardHost->SetWidgetsVisible(visible);
    }
    return result;
}

void Application::CloseMainWindow() noexcept
{
    PluginHost::Instance().SetUiInvalidateTarget(nullptr);
    _pageEdgeMouseTracking = false;
    DestroyPageEdgeAffordances();
    _settingsWatcher.Stop();
    DismissWidgetRaise();
    CancelPageNavigation();
    _renderer.Shutdown();
    _rendererReady = false;
    if (_dashboardHost)
    {
        _dashboardHost->Shutdown();
    }
    _window.reset();
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

LRESULT CALLBACK Application::SettingsDialogProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    Application* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        application = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    }
    if (application && (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == IDOK)))
    {
        application->CloseSettingsError();
        return 0;
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
        ClearScheduledFrameDeadline();
        if (_windowVisible)
        {
            _frameInvalidated = true;
        }
        if (const HRESULT result = UpdateDashboardVisibility(); FAILED(result))
        {
            _runtimeFailure = result;
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_POWERSETTINGCHANGE && lParam != 0)
        {
            const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
            if (IsEqualGUID(setting->PowerSetting, GUID_SESSION_DISPLAY_STATUS) && setting->DataLength >= sizeof(DWORD))
            {
                const bool wasDisplayPoweredOn = _displayPoweredOn;
                DWORD displayState = PowerMonitorOn;
                std::memcpy(&displayState, setting->Data, sizeof(displayState));
                _displayPoweredOn = displayState != PowerMonitorOff;
                ClearScheduledFrameDeadline();
                if (!wasDisplayPoweredOn && _displayPoweredOn)
                {
                    _frameInvalidated = true;
                }
                if (const HRESULT result = UpdateDashboardVisibility(); FAILED(result))
                {
                    _runtimeFailure = result;
                    PostMessageW(window, WM_CLOSE, 0, 0);
                }
            }
        }
        return TRUE;
    case WM_TIMECHANGE:
        ClearScheduledFrameDeadline();
        _frameInvalidated = true;
        return 0;
    case Renderer::kOcclusionStatusMessage:
        _occlusionStatusChanged = true;
        return 0;
    case PluginHost::kDataSnapshotInvalidateMessage:
        PluginHost::Instance().AcknowledgeUiInvalidate();
        _frameInvalidated = true;
        return 0;
    case SettingsWatcher::kSettingsChangedMessage:
        OnSettingsChanged();
        return 0;
    case WM_POINTERDOWN:
        OnPointerDown(window, wParam);
        return 0;
    case WM_POINTERUPDATE:
        OnPointerUpdate(window, wParam);
        return _pagePanStarted ? 1 : 0;
    case WM_POINTERUP:
        OnPointerUp(window, wParam);
        return 0;
    case WM_POINTERCAPTURECHANGED:
        if (_pagePointerActive || _pagePointerCaptured)
        {
            CancelPageNavigation();
        }
        return 0;
    case WM_LBUTTONUP:
        OnMouseButtonUp(window, lParam);
        return 0;
    case WM_MOUSEMOVE:
        // The top-level window owns edge-band hover: a band is created only while the pointer is inside its zone.
        if (!IsPointerSynthesizedMouseMessage())
        {
            if (!_pageEdgeMouseTracking)
            {
                TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
                _pageEdgeMouseTracking = TrackMouseEvent(&track) != FALSE;
            }
            UpdatePageEdgeHover();
        }
        return 0;
    case kPageEdgeHoverMessage:
        UpdatePageEdgeHover();
        return 0;
    case WM_MOUSELEAVE:
        _pageEdgeMouseTracking = false;
        // Entering a band is also a leave for the parent, so re-test the cursor instead of hiding unconditionally.
        UpdatePageEdgeHover();
        return 0;
    case WM_GETMINMAXINFO:
    {
        auto* minimums = reinterpret_cast<MINMAXINFO*>(lParam);
        minimums->ptMinTrackSize = POINT{480, 320};
        const UINT dpi = GetDpiForWindow(window);
        SIZE defaultWindowSize{};
        const DWORD windowStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
        const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
        if (SUCCEEDED(CalculateWindowSizeForDpi(windowStyle, extendedStyle, dpi, defaultWindowSize)))
        {
            minimums->ptMaxTrackSize.x = std::max(minimums->ptMaxTrackSize.x, defaultWindowSize.cx);
            minimums->ptMaxTrackSize.y = std::max(minimums->ptMaxTrackSize.y, defaultWindowSize.cy);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            if (_raisedActive)
            {
                DismissWidgetRaise();
                return 0;
            }
            CloseMainWindow();
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        _frameInvalidated = true;
        ValidateRect(window, nullptr);
        return 0;
    case WM_CLOSE:
        CloseMainWindow();
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
    const auto refreshEdges = wil::scope_exit([this]() noexcept { RefreshPageEdgeAffordances(); });
    if (!_rendererReady)
    {
        return 0;
    }
    DismissWidgetRaise();
    if (_pagePointerActive || _pagePanStarted || _pageSettleActive || _pageCurrentOffset != 0)
    {
        CancelPageNavigation();
    }

    ClearScheduledFrameDeadline();

    const UINT dpi = GetDpiForWindow(window);
    HRESULT result =
        dpi != 0 && _dashboardHost ? _dashboardHost->Resize(width, height, dpi) : HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(result))
    {
        result = _renderer.Resize(width, height);
    }
    if (SUCCEEDED(result))
    {
        result = UpdateDashboardVisibility();
    }
    if (SUCCEEDED(result) && width != 0 && height != 0)
    {
        _frameInvalidated = true;
    }
    if (FAILED(result))
    {
        _runtimeFailure = result;
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    return 0;
}

LRESULT Application::OnDpiChanged(HWND window, UINT dpi, const RECT* suggestedBounds) noexcept
{
    const auto refreshEdges = wil::scope_exit([this]() noexcept { RefreshPageEdgeAffordances(); });
    if (!suggestedBounds)
    {
        return 0;
    }

    DismissWidgetRaise();
    if (_rendererReady)
    {
        const HRESULT result = _renderer.SetDpi(dpi);
        if (FAILED(result))
        {
            _runtimeFailure = result;
            PostMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
        _frameInvalidated = true;
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
