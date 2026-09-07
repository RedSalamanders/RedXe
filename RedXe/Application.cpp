#include "Application.h"
#include "AccessibilityHost.h"
#include <UIAutomation.h>

#include "CrashHandler.h"
#include "FluentIcons.h"
#include "FrameScheduler.h"
#include "PageNavigation.h"
#include "PlugInterfaces/Widget.h"
#include "Settings.h"
#include "TextInputValidation.h"
#include "WidgetRaise.h"
#include "WidgetTextClient.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <ole2.h>
#include <shellapi.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windowsx.h>

namespace
{
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

struct TouchContact final
{
    UINT32 id = 0;
    POINT client{};
    UINT64 qpc = 0;
};

[[nodiscard]] uint32_t PointerKindFromId(UINT32 pointerId) noexcept
{
    POINTER_INFO information{};
    if (GetPointerInfo(pointerId, &information) && information.pointerType == PT_PEN)
    {
        return RedXePointerKindPen;
    }
    return RedXePointerKindTouch;
}

[[nodiscard]] uint32_t CollectInContactTouches(HWND window, UINT32 pointerId,
                                               std::array<TouchContact, 10>& contacts) noexcept
{
    std::array<POINTER_TOUCH_INFO, 10> frame{};
    UINT32 count = static_cast<UINT32>(frame.size());
    BOOL ok = GetPointerFrameTouchInfo(pointerId, &count, frame.data());
    if (!ok && count > 0 && count <= frame.size())
    {
        ok = GetPointerFrameTouchInfo(pointerId, &count, frame.data());
    }
    if (ok)
    {
        uint32_t written = 0;
        for (UINT32 index = 0; index < count && written < contacts.size(); ++index)
        {
            const POINTER_INFO& information = frame[index].pointerInfo;
            if (information.pointerType != PT_TOUCH || (information.pointerFlags & POINTER_FLAG_INCONTACT) == 0)
            {
                continue;
            }
            POINT position = information.ptPixelLocation;
            if (!ScreenToClient(window, &position))
            {
                continue;
            }
            contacts[written++] = TouchContact{information.pointerId, position, information.PerformanceCount};
        }
        if (written > 0)
        {
            return written;
        }
    }

    POINTER_INFO information{};
    if (!GetPointerInfo(pointerId, &information) || information.pointerType != PT_TOUCH ||
        (information.pointerFlags & POINTER_FLAG_INCONTACT) == 0)
    {
        return 0;
    }
    POINT position = information.ptPixelLocation;
    if (!ScreenToClient(window, &position))
    {
        return 0;
    }
    contacts[0] = TouchContact{pointerId, position, information.PerformanceCount};
    return 1;
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

class ApplicationDropTarget final : public IDropTarget
{
  public:
    explicit ApplicationDropTarget(Application* owner) noexcept : _owner(owner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId != IID_IUnknown && interfaceId != IID_IDropTarget)
        {
            return E_NOINTERFACE;
        }
        *result = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++_references;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        return --_references;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keyState, POINTL screen, DWORD* effect) override
    {
        (void)data;
        return DragOver(keyState, screen, effect);
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD /*keyState*/, POINTL screen, DWORD* effect) override
    {
        if (!_owner || !effect)
        {
            return E_POINTER;
        }
        POINT client{screen.x, screen.y};
        if (_owner->_window)
        {
            ScreenToClient(_owner->_window.get(), &client);
        }
        return _owner->HandleOleDragOver(client, effect);
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override
    {
        if (_owner)
        {
            _owner->HandleOleDragLeave();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD /*keyState*/, POINTL screen, DWORD* effect) override
    {
        if (!_owner || !data || !effect)
        {
            return E_POINTER;
        }
        POINT client{screen.x, screen.y};
        if (_owner->_window)
        {
            ScreenToClient(_owner->_window.get(), &client);
        }
        std::array<std::array<wchar_t, 520>, 8> storage{};
        std::array<const wchar_t*, 8> targets{};
        uint32_t count = 0;
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        if (SUCCEEDED(data->GetData(&format, &medium)) && medium.hGlobal)
        {
            const HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
            if (drop)
            {
                const UINT files = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                for (UINT index = 0; index < files && count < targets.size(); ++index)
                {
                    const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                    if (length == 0 || length >= storage[count].size())
                    {
                        continue;
                    }
                    if (DragQueryFileW(drop, index, storage[count].data(), static_cast<UINT>(storage[count].size())) !=
                        0)
                    {
                        targets[count] = storage[count].data();
                        ++count;
                    }
                }
                GlobalUnlock(medium.hGlobal);
            }
            ReleaseStgMedium(&medium);
        }
        FORMATETC textFormat{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM textMedium{};
        if (count < targets.size() && SUCCEEDED(data->GetData(&textFormat, &textMedium)) && textMedium.hGlobal)
        {
            const auto* text = static_cast<const wchar_t*>(GlobalLock(textMedium.hGlobal));
            if (text && text[0] != L'\0')
            {
                size_t length = 0;
                while (text[length] != L'\0' && length + 1 < storage[count].size())
                {
                    ++length;
                }
                if (length >= 3)
                {
                    bool url = (text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z');
                    size_t cursor = 1;
                    while (url && cursor < length)
                    {
                        const wchar_t value = text[cursor];
                        if (!((value >= L'A' && value <= L'Z') || (value >= L'a' && value <= L'z') ||
                              (value >= L'0' && value <= L'9') || value == L'+' || value == L'.' || value == L'-'))
                        {
                            break;
                        }
                        ++cursor;
                    }
                    if (url && cursor >= 2 && cursor < length && text[cursor] == L':')
                    {
                        std::memcpy(storage[count].data(), text, (length + 1) * sizeof(wchar_t));
                        targets[count] = storage[count].data();
                        ++count;
                    }
                }
            }
            if (text)
            {
                GlobalUnlock(textMedium.hGlobal);
            }
            ReleaseStgMedium(&textMedium);
        }
        return _owner->HandleOleDrop(client, targets.data(), count, effect);
    }

  private:
    Application* _owner = nullptr;
    ULONG _references = 1;
};

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
    _dropTarget.reset();
    if (_oleInitialized)
    {
        OleUninitialize();
        _oleInitialized = false;
    }
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
    if (!_settingsStore.LogsDirectory().empty())
    {
        (void)PluginHost::Instance().SetLogDirectory(_settingsStore.LogsDirectory().c_str());
        (void)PluginHost::Instance().SetLogRetentionDays(_settings->logRetentionDays);
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

    result = OleInitialize(nullptr);
    if (FAILED(result))
    {
        OutputDebugStringW(L"OleInitialize failed.\n");
        return 2;
    }
    _oleInitialized = true;
    _dropTarget.reset(new (std::nothrow) ApplicationDropTarget(this));
    if (!_dropTarget)
    {
        return 2;
    }
    result = RegisterDragDrop(_window.get(), _dropTarget.get());
    if (FAILED(result))
    {
        OutputDebugStringW(L"RegisterDragDrop failed.\n");
        return 2;
    }
    _dropRegistered = true;
    PluginHost::Instance().SetSettingsPersistHandler(&Application::SettingsPersistThunk, this);

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
            if (_textServices && (message.message == WM_KEYDOWN || message.message == WM_KEYUP ||
                                  message.message == WM_SYSKEYDOWN || message.message == WM_SYSKEYUP))
            {
                RefreshTextServices();
                bool handled = false;
                (void)_textServices->PreTranslate(message, handled);
                if (handled)
                {
                    RefreshTextServices();
                    continue;
                }
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
        if (_raiseSettleActive)
        {
            TickRaiseSettle();
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
            OverlayMotionInProgress(),
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
                OverlayMotionInProgress(),
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

        if (!WaitForFrameLatency())
        {
            // Input arrived while a back buffer was still in flight: dispatch it before building the frame.
            continue;
        }

        const std::chrono::duration<float> elapsed = std::chrono::steady_clock::now() - startTime;
        const float elapsedSeconds = elapsed.count();
        const float deltaSeconds = elapsedSeconds - previousElapsedSeconds;
        previousElapsedSeconds = elapsedSeconds;
        bool focusedViewPrepared = false;
        uint64_t preparedWidgets = 0;
        result = _renderer.PrepareWidgets(_keyboardWidgetIndex, &focusedViewPrepared,
                                          _accessibility ? &preparedWidgets : nullptr);
        if (focusedViewPrepared || result == S_FALSE)
            RefreshTextServices(focusedViewPrepared);
        RefreshAccessibility(preparedWidgets);
        if (SUCCEEDED(result))
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
// It MUST NOT call SetLogDirectory: diagnostics stay off `%LocalAppData%` and the deployed tree.
//
// UI_XeneonDisplayWindowing.md owns this mode: skip display discovery and prompts, create the titled window hidden,
// validate its DPI-adjusted client dimensions, render one frame, and exit.
int Application::RunSelfTest(std::wstring_view settingsPath) noexcept
{
    PluginHost::Instance().SetNetworkAccessEnabled(false);
    PluginHost::Instance().SetControlAccessEnabled(false);
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

    _persistSettingsToDisk = false;
    (void)SetEnvironmentVariableW(L"REDXE_AUTOMATED_HOST", L"1");
    result = OleInitialize(nullptr);
    if (FAILED(result))
    {
        OutputDebugStringW(L"OleInitialize failed.\n");
        return 2;
    }
    _oleInitialized = true;
    _dropTarget.reset(new (std::nothrow) ApplicationDropTarget(this));
    if (!_dropTarget)
    {
        return 2;
    }
    result = RegisterDragDrop(_window.get(), _dropTarget.get());
    if (FAILED(result))
    {
        OutputDebugStringW(L"RegisterDragDrop failed.\n");
        return 2;
    }
    _dropRegistered = true;
    PluginHost::Instance().SetSettingsPersistHandler(&Application::SettingsPersistThunk, this);

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
    result = _renderer.PrepareWidgets();
    if (SUCCEEDED(result))
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
            result = _renderer.PrepareWidgets();
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
    RefreshAppearance();
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
    DismissWidgetRaise(false);
    CancelPageNavigation();
    if (*settings == *_settings)
    {
        return S_FALSE;
    }
    if (ActiveDashboardRuntimeEquals(*settings, *_settings))
    {
        _settings = std::move(settings);
        (void)PluginHost::Instance().SetLogRetentionDays(_settings->logRetentionDays);
        return S_OK;
    }

    _renderer.Shutdown();
    _rendererReady = false;
    _dashboardHost->Shutdown(false);

    HRESULT applyResult = _pluginManager->Reconfigure(*settings);
    if (SUCCEEDED(applyResult))
    {
        applyResult = InitializeDashboardRuntime();
    }
    if (SUCCEEDED(applyResult))
    {
        _settings = std::move(settings);
        (void)PluginHost::Instance().SetLogRetentionDays(_settings->logRetentionDays);
        return S_OK;
    }

    _renderer.Shutdown();
    _rendererReady = false;
    _dashboardHost->Shutdown(false);
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
                                   static_cast<UINT>(client.bottom), dpi, false);
    if (SUCCEEDED(result))
        result = dashboard->SetHorizontalOffset(_pageCurrentOffset + direction * client.right);
    if (SUCCEEDED(result))
        result = _renderer.SetTransitionDashboard(dashboard.get());
    if (SUCCEEDED(result))
        result = dashboard->SetWidgetsVisible(visible);
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
    if (_accessibility)
        _accessibility->ClearViews();
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
            // A child created under the cursor does not receive WM_SETCURSOR until the mouse moves, so the parent
            // still shows IDC_ARROW. Force the hand as soon as the band exists.
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
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
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
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
        if (index < _pageEdges.size() && !IsPointerSynthesizedMouseMessage() && _window)
        {
            // Navigate after this WndProc returns. Destroying the band here, or from NavigateToAdjacentPage's
            // affordance refresh, would tear down the HWND that is still dispatching this click.
            (void)PostMessageW(_window.get(), kPageEdgeNavigateMessage, static_cast<WPARAM>(direction), 0);
        }
        return 0;
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
    case WM_POINTERCAPTURECHANGED:
        // Touch and pen keep the host pointer contract. One-finger contacts stay with the widget; two or three
        // fingers start page pan. Forward to the top-level window exactly as host-owned native containers do.
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
    ClearKeyboardFocus();
    if (_window)
    {
        ReleasePagePointerCaptures(_window.get());
    }
    _pagePointerCaptured = false;
    _pagePointerActive = false;
    _pagePanStarted = false;
    _pageGestureIgnored = false;
    _pageSettleActive = false;
    _pageSettleCommit = false;
    _pageTouchCount = 0;
    _pageTouches = {};
    _pageCentroidX = 0;
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

void Application::ReleasePagePointerCaptures(HWND window) noexcept
{
    if (!window)
    {
        return;
    }
    for (uint32_t index = 0; index < _pageTouchCount; ++index)
    {
        if (_pageTouches[index].captured && _pageTouches[index].id != 0)
        {
            HostReleasePointerCapture(window, _pageTouches[index].id);
            _pageTouches[index].captured = false;
        }
    }
}

void Application::AdoptPageTouches(const UINT32* ids, const POINT* positions, uint32_t count,
                                   bool grabbingSettle) noexcept
{
    count = std::min(count, kPageSwipeMaxTouches);
    std::array<PageSwipeTouch, kPageSwipeMaxTouches> next{};
    for (uint32_t index = 0; index < count; ++index)
    {
        next[index].id = ids[index];
        next[index].x = positions[index].x;
        next[index].y = positions[index].y;
        bool found = false;
        for (uint32_t existing = 0; existing < _pageTouchCount; ++existing)
        {
            if (_pageTouches[existing].id == ids[index])
            {
                next[index].startX = _pageTouches[existing].startX;
                next[index].startY = _pageTouches[existing].startY;
                next[index].captured = _pageTouches[existing].captured;
                found = true;
                break;
            }
        }
        if (!found)
        {
            next[index].startX = grabbingSettle ? positions[index].x - _pageCurrentOffset : positions[index].x;
            next[index].startY = positions[index].y;
        }
    }
    _pageTouches = next;
    _pageTouchCount = count;
    _pagePointerActive = count > 0;
    const POINT centroid = [&]() noexcept -> POINT
    {
        if (count == 0)
        {
            return {};
        }
        LONG x = 0;
        LONG y = 0;
        for (uint32_t index = 0; index < count; ++index)
        {
            x += positions[index].x;
            y += positions[index].y;
        }
        return {x / static_cast<LONG>(count), y / static_cast<LONG>(count)};
    }();
    _pageCentroidX = centroid.x;
}

LONG Application::PageTouchDeltaX() const noexcept
{
    if (_pageTouchCount == 0)
    {
        return 0;
    }
    LONG total = 0;
    for (uint32_t index = 0; index < _pageTouchCount; ++index)
    {
        total += _pageTouches[index].x - _pageTouches[index].startX;
    }
    return total / static_cast<LONG>(_pageTouchCount);
}

LONG Application::PageTouchDeltaY() const noexcept
{
    if (_pageTouchCount == 0)
    {
        return 0;
    }
    LONG total = 0;
    for (uint32_t index = 0; index < _pageTouchCount; ++index)
    {
        total += _pageTouches[index].y - _pageTouches[index].startY;
    }
    return total / static_cast<LONG>(_pageTouchCount);
}

bool Application::PageTouchesContain(UINT32 pointerId) const noexcept
{
    for (uint32_t index = 0; index < _pageTouchCount; ++index)
    {
        if (_pageTouches[index].id == pointerId)
        {
            return true;
        }
    }
    return false;
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
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    POINT position{};
    UINT64 qpc = 0;
    if (!TryPointerClientPosition(window, pointerId, position, qpc))
    {
        return;
    }

    POINTER_INFO information{};
    const bool isTouch = GetPointerInfo(pointerId, &information) && information.pointerType == PT_TOUCH;
    std::array<TouchContact, 10> contacts{};
    const uint32_t touchCount = isTouch ? CollectInContactTouches(window, pointerId, contacts) : 0;

    if (!_raisedActive && PageSwipeAcceptsFingerCount(touchCount))
    {
        CancelInteractivePointer();
        std::array<UINT32, kPageSwipeMaxTouches> ids{};
        std::array<POINT, kPageSwipeMaxTouches> points{};
        const uint32_t use = std::min(touchCount, kPageSwipeMaxTouches);
        for (uint32_t index = 0; index < use; ++index)
        {
            ids[index] = contacts[index].id;
            points[index] = contacts[index].client;
        }
        const bool grabbingSettle = _pageSettleActive || _pageCurrentOffset != 0;
        if (_pageSettleActive)
        {
            _pageSettleActive = false;
            _pagePanStarted = _pageCurrentOffset != 0;
            _pageGestureIgnored = false;
        }
        else if (!_pagePointerActive)
        {
            _pagePanStarted = false;
            _pageGestureIgnored = false;
            if (!grabbingSettle)
            {
                _pageCurrentOffset = 0;
            }
        }
        AdoptPageTouches(ids.data(), points.data(), use, grabbingSettle);
        _pagePointerQpc = contacts[0].qpc;
        _pageVelocityPxPerSec = 0.0f;
        _pagePointerCaptured = false;
        _frameInvalidated = true;
        return;
    }
    if (_pagePointerActive)
    {
        return;
    }
    if (_interactiveOwnsPointer)
    {
        return;
    }

    const uint32_t kind = (!isTouch && information.pointerType == PT_PEN) ? RedXePointerKindPen : RedXePointerKindTouch;
    bool consumed = false;
    (void)ForwardInteractivePointer(position, pointerId, kind, RedXePointerPhaseDown, &consumed);
    (void)consumed;
}

void Application::OnPointerUpdate(HWND window, WPARAM wParam) noexcept
{
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    if (_interactiveOwnsPointer && !_pagePointerActive)
    {
        if (pointerId != _interactivePointerId)
            return;
        POINT position{};
        UINT64 qpc = 0;
        if (!TryPointerClientPosition(window, pointerId, position, qpc))
            CancelInteractivePointer();
        else
            (void)ForwardInteractivePointer(position, pointerId, _interactivePointerKind, RedXePointerPhaseMove,
                                            nullptr);
        return;
    }
    if (_pagePointerActive)
    {
        if (_pageGestureIgnored || !PageTouchesContain(pointerId))
        {
            return;
        }
        std::array<TouchContact, 10> contacts{};
        const uint32_t touchCount = CollectInContactTouches(window, pointerId, contacts);
        for (uint32_t index = 0; index < _pageTouchCount; ++index)
        {
            for (uint32_t contact = 0; contact < touchCount; ++contact)
            {
                if (contacts[contact].id == _pageTouches[index].id)
                {
                    _pageTouches[index].x = contacts[contact].client.x;
                    _pageTouches[index].y = contacts[contact].client.y;
                    break;
                }
            }
        }
        POINT position{};
        UINT64 qpc = 0;
        if (!TryPointerClientPosition(window, pointerId, position, qpc))
        {
            CancelInteractivePointer();
            CancelPageNavigation();
            return;
        }
        RECT client{};
        if (!GetClientRect(window, &client) || client.right <= 0)
        {
            return;
        }

        const LONG deltaX = PageTouchDeltaX();
        const LONG deltaY = PageTouchDeltaY();
        const UINT dpi = GetDpiForWindow(window);
        const LONG threshold = PageSwipeThresholdPixels(dpi);
        if (!_pagePanStarted)
        {
            if (PageSwipeRejectsAsVertical(deltaX, deltaY, threshold))
            {
                _pageGestureIgnored = true;
                ReleasePagePointerCaptures(window);
                _pagePointerActive = false;
                _pageTouchCount = 0;
                ResumePageSettleIfNeeded();
                return;
            }
            if (!PageSwipeLocksHorizontal(deltaX, deltaY, threshold))
            {
                return;
            }
            CancelInteractivePointer();
            _pagePanStarted = true;
            if (_accessibility)
                _accessibility->ClearViews();
            _pageSettleActive = false;
            bool capturedAny = false;
            for (uint32_t index = 0; index < _pageTouchCount; ++index)
            {
                if (HostSetPointerCapture(window, _pageTouches[index].id))
                {
                    _pageTouches[index].captured = true;
                    capturedAny = true;
                }
                POINTER_INFO target{};
                if (GetPointerInfo(_pageTouches[index].id, &target) && target.hwndTarget && target.hwndTarget != window)
                {
                    SendMessageW(target.hwndTarget, WM_CANCELMODE, 0, 0);
                }
            }
            _pagePointerCaptured = capturedAny;
        }

        LONG centroidX = 0;
        for (uint32_t index = 0; index < _pageTouchCount; ++index)
        {
            centroidX += _pageTouches[index].x;
        }
        centroidX /= static_cast<LONG>(_pageTouchCount);
        if (_pagePointerQpc != 0 && qpc > _pagePointerQpc && _qpcFrequency != 0)
        {
            const double dt = static_cast<double>(qpc - _pagePointerQpc) / static_cast<double>(_qpcFrequency);
            if (dt > 0.0005 && dt < 0.08)
            {
                const float instant = static_cast<float>(static_cast<double>(centroidX - _pageCentroidX) / dt);
                _pageVelocityPxPerSec = _pageVelocityPxPerSec * 0.55f + instant * 0.45f;
            }
        }
        _pageCentroidX = centroidX;
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
        return;
    }
    if (_raisedActive)
    {
        POINT position{};
        UINT64 qpc = 0;
        if (TryPointerClientPosition(window, pointerId, position, qpc))
        {
            (void)ForwardInteractivePointer(position, pointerId, PointerKindFromId(pointerId), RedXePointerPhaseMove,
                                            nullptr);
        }
        return;
    }

    POINT position{};
    UINT64 qpc = 0;
    if (TryPointerClientPosition(window, pointerId, position, qpc))
    {
        const uint32_t kind =
            _interactivePointerWidget != SIZE_MAX ? _interactivePointerKind : PointerKindFromId(pointerId);
        (void)ForwardInteractivePointer(position, pointerId, kind, RedXePointerPhaseMove, nullptr);
    }
}

void Application::OnPointerUp(HWND window, WPARAM wParam) noexcept
{
    POINT position{};
    UINT64 qpc = 0;
    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    const bool havePosition = TryPointerClientPosition(window, pointerId, position, qpc);
    if (_interactiveOwnsPointer && !_pagePointerActive)
    {
        if (pointerId != _interactivePointerId)
            return;
        if (havePosition)
            (void)ForwardInteractivePointer(position, _interactivePointerId, _interactivePointerKind,
                                            RedXePointerPhaseUp, nullptr);
        else
            CancelInteractivePointer();
        return;
    }
    if (_pagePointerActive && PageTouchesContain(pointerId))
    {
        std::array<TouchContact, 10> contacts{};
        uint32_t remaining = CollectInContactTouches(window, pointerId, contacts);
        if (!PageSwipeAcceptsFingerCount(remaining))
        {
            remaining = 0;
            for (uint32_t index = 0; index < _pageTouchCount && remaining < contacts.size(); ++index)
            {
                if (_pageTouches[index].id == pointerId)
                {
                    continue;
                }
                POINTER_INFO information{};
                if (!GetPointerInfo(_pageTouches[index].id, &information) || information.pointerType != PT_TOUCH ||
                    (information.pointerFlags & POINTER_FLAG_INCONTACT) == 0)
                {
                    continue;
                }
                POINT tracked = information.ptPixelLocation;
                if (!ScreenToClient(window, &tracked))
                {
                    continue;
                }
                contacts[remaining++] = TouchContact{_pageTouches[index].id, tracked, information.PerformanceCount};
            }
        }
        if (PageSwipeAcceptsFingerCount(remaining))
        {
            std::array<UINT32, kPageSwipeMaxTouches> ids{};
            std::array<POINT, kPageSwipeMaxTouches> points{};
            const uint32_t use = std::min(remaining, kPageSwipeMaxTouches);
            for (uint32_t index = 0; index < use; ++index)
            {
                ids[index] = contacts[index].id;
                points[index] = contacts[index].client;
            }
            AdoptPageTouches(ids.data(), points.data(), use, false);
            return;
        }
        ReleasePagePointerCaptures(window);
        _pagePointerCaptured = false;
        _pagePointerActive = false;
        const bool panStarted = _pagePanStarted;
        _pagePanStarted = false;
        _pageTouchCount = 0;
        if (!panStarted)
        {
            _pageGestureIgnored = false;
            if (_pageCurrentOffset != 0 || _pageTransitionDirection != 0)
            {
                ResumePageSettleIfNeeded();
            }
            return;
        }

        CancelInteractivePointer();
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
        return;
    }
    if (_raisedActive)
    {
        bool consumed = false;
        if (havePosition)
        {
            (void)ForwardInteractivePointer(position, pointerId, PointerKindFromId(pointerId), RedXePointerPhaseUp,
                                            &consumed);
        }
        if (!consumed && havePosition)
        {
            OnRaisedContentActivateAttempt(window, position, GetTickCount64());
        }
        return;
    }
    bool consumed = false;
    if (havePosition)
    {
        const uint32_t kind =
            _interactivePointerWidget != SIZE_MAX ? _interactivePointerKind : PointerKindFromId(pointerId);
        (void)ForwardInteractivePointer(position, pointerId, kind, RedXePointerPhaseUp, &consumed);
    }
    if (!consumed && havePosition)
    {
        OnClientActivateAttempt(window, position, GetTickCount64());
    }
}

void Application::OnMouseButtonDown(HWND window, LPARAM lParam) noexcept
{
    if (_pagePointerActive || _pagePanStarted || IsPointerSynthesizedMouseMessage())
    {
        return;
    }
    const POINT position{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (PointInPageEdgeBand(window, position))
    {
        return;
    }
    bool consumed = false;
    (void)ForwardInteractivePointer(position, 1, RedXePointerKindMouse, RedXePointerPhaseDown, &consumed);
}

void Application::OnMouseButtonUp(HWND window, LPARAM lParam) noexcept
{
    if (_pagePointerActive || _pagePanStarted || IsPointerSynthesizedMouseMessage())
    {
        return;
    }
    const POINT position{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (!_interactiveOwnsPointer && TryNavigateFromPageEdge(window, position))
    {
        CancelInteractivePointer();
        return;
    }
    bool consumed = false;
    (void)ForwardInteractivePointer(position, 1, RedXePointerKindMouse, RedXePointerPhaseUp, &consumed);
    if (consumed)
    {
        _activateTick = 0;
        _activateWidgetIndex = SIZE_MAX;
        return;
    }
    if (_raisedActive)
    {
        OnRaisedContentActivateAttempt(window, position, GetTickCount64());
        return;
    }
    OnClientActivateAttempt(window, position, GetTickCount64());
}

bool Application::PointInPageEdgeBand(HWND window, POINT position) const noexcept
{
    if (!_window || window != _window.get() || _raisedActive)
    {
        return false;
    }
    const RECT reachable = ReachableClientRect();
    const UINT dpi = GetDpiForWindow(window);
    for (size_t index = 0; index < _pageEdges.size(); ++index)
    {
        const int direction = index == 0 ? kPageEdgeDirectionPrevious : kPageEdgeDirectionNext;
        const RECT band = PageEdgeBandRectIn(reachable, direction, dpi);
        if (PageEdgeBandContains(band, position))
        {
            return true;
        }
    }
    return false;
}

HRESULT Application::ForwardInteractivePointer(POINT client, uint32_t pointerId, uint32_t kind, uint32_t phase,
                                               bool* consumed, uint32_t modifiers, float wheelDelta) noexcept
{
    const auto refreshText = wil::scope_exit(
        [this, phase]() noexcept
        {
            if (_textClient || phase == RedXePointerPhaseDown || phase == RedXePointerPhaseUp)
                RefreshTextServices();
        });
    if (consumed)
    {
        *consumed = false;
    }
    if (!_dashboardHost)
    {
        return S_FALSE;
    }

    const auto send = [&](size_t index, float localX, float localY) noexcept -> HRESULT
    {
        IRedXeInteractiveWidget* widget = _dashboardHost->InteractiveWidgetAt(index);
        if (!widget)
        {
            return S_FALSE;
        }
        RECT bounds{};
        const bool raised = _raisedActive && index == _raisedWidgetIndex;
        if (raised)
            bounds = _raisedLayout.content;
        else if (_window)
        {
            RECT clientBounds{};
            if (GetClientRect(_window.get(), &clientBounds))
                bounds = _dashboardHost->PixelBoundsAt(index, static_cast<UINT>(clientBounds.right),
                                                       static_cast<UINT>(clientBounds.bottom));
        }
        const RedXePointerEvent event{sizeof(RedXePointerEvent),
                                      pointerId,
                                      kind,
                                      phase,
                                      localX,
                                      localY,
                                      raised ? 1U : 0U,
                                      static_cast<uint32_t>(std::max(0L, bounds.right - bounds.left)),
                                      static_cast<uint32_t>(std::max(0L, bounds.bottom - bounds.top)),
                                      _window ? GetDpiForWindow(_window.get()) : 96U,
                                      modifiers,
                                      wheelDelta};
        return widget->OnPointer(&event);
    };

    const auto releaseCapture = [&]() noexcept
    {
        if (!_interactiveOwnsPointer)
            return;
        _interactiveOwnsPointer = false;
        if (_interactivePointerKind == RedXePointerKindMouse)
        {
            if (_window && GetCapture() == _window.get())
                (void)ReleaseCapture();
        }
        else if (_window)
            HostReleasePointerCapture(_window.get(), _interactivePointerId);
    };

    if (phase == RedXePointerPhaseCancel)
    {
        const size_t index = _interactivePointerWidget;
        const bool wasConsumed = _interactivePointerConsumed;
        _interactivePointerWidget = SIZE_MAX;
        _interactivePointerConsumed = false;
        releaseCapture();
        if (index == SIZE_MAX)
        {
            return S_FALSE;
        }
        const HRESULT result = send(index, 0.0f, 0.0f);
        if (consumed)
        {
            *consumed = wasConsumed;
        }
        return result;
    }

    size_t index = SIZE_MAX;
    if ((phase == RedXePointerPhaseMove || phase == RedXePointerPhaseUp) && _interactivePointerWidget != SIZE_MAX &&
        pointerId != _interactivePointerId)
        return S_FALSE;
    float localX = 0.0f;
    float localY = 0.0f;
    bool haveLocal = false;
    if ((phase == RedXePointerPhaseMove || phase == RedXePointerPhaseUp) && _interactivePointerWidget != SIZE_MAX)
    {
        index = _interactivePointerWidget;
        RECT bounds{};
        if (_raisedActive && index == _raisedWidgetIndex)
        {
            bounds = _raisedLayout.content;
        }
        else if (_window)
        {
            RECT clientRect{};
            if (GetClientRect(_window.get(), &clientRect) && clientRect.right > 0 && clientRect.bottom > 0)
            {
                bounds = _dashboardHost->PixelBoundsAt(index, static_cast<UINT>(clientRect.right),
                                                       static_cast<UINT>(clientRect.bottom));
            }
        }
        localX = static_cast<float>(client.x - bounds.left);
        localY = static_cast<float>(client.y - bounds.top);
        haveLocal = true;
    }
    else
    {
        haveLocal = HitInteractiveLocal(client, index, localX, localY);
    }
    if (!haveLocal)
    {
        if (phase == RedXePointerPhaseDown)
            ClearKeyboardFocus();
        return S_FALSE;
    }

    const HRESULT result = send(index, localX, localY);
    if (phase == RedXePointerPhaseDown)
    {
        _interactivePointerWidget = index;
        _interactivePointerId = pointerId;
        _interactivePointerKind = kind;
        _interactivePointerConsumed = result == S_OK || result == RedXePointerCapture;
        if (_interactivePointerConsumed)
        {
            (void)FocusKeyboardWidget(index);
        }
        if (result == RedXePointerCapture && _window)
        {
            if (kind == RedXePointerKindMouse)
            {
                (void)SetCapture(_window.get());
                _interactiveOwnsPointer = GetCapture() == _window.get();
            }
            else
                _interactiveOwnsPointer = HostSetPointerCapture(_window.get(), pointerId);
            if (!_interactiveOwnsPointer)
            {
                CancelInteractivePointer();
                // The canceled Down was still consumed; its later Up must not become a raise gesture.
                _interactivePointerWidget = index;
                _interactivePointerConsumed = true;
            }
        }
        if (consumed)
        {
            *consumed = _interactivePointerConsumed;
        }
        return result;
    }
    if (phase == RedXePointerPhaseUp)
    {
        const bool gestureConsumed = _interactivePointerConsumed || result == S_OK || result == RedXePointerCapture ||
                                     result == RedXePointerRaise || result == RedXePointerDismiss;
        _interactivePointerWidget = SIZE_MAX;
        _interactivePointerConsumed = false;
        releaseCapture();
        if (result == RedXePointerRaise && !_raisedActive && _window)
            (void)TryRaiseWidgetAt(_window.get(), index);
        else if (result == RedXePointerDismiss && _raisedActive && index == _raisedWidgetIndex)
            DismissWidgetRaise(true);
        if (consumed)
        {
            *consumed = gestureConsumed;
        }
        return result;
    }
    if (consumed)
    {
        *consumed = result == S_OK || result == RedXePointerCapture;
    }
    return result;
}

bool Application::HitInteractiveLocal(POINT client, size_t& widgetIndex, float& localX, float& localY) const noexcept
{
    widgetIndex = SIZE_MAX;
    localX = 0.0f;
    localY = 0.0f;
    if (!_dashboardHost || !_window)
    {
        return false;
    }

    const auto accept = [&](size_t index, const RECT& bounds) noexcept -> bool
    {
        if (!_dashboardHost->GpuWidgetAt(index) || !_dashboardHost->InteractiveWidgetAt(index) ||
            _dashboardHost->WindowWidgetAt(index) || _dashboardHost->RequiresPlaceholderAt(index))
        {
            return false;
        }
        if (!PointInRectInclusive(bounds, client))
        {
            return false;
        }
        widgetIndex = index;
        localX = static_cast<float>(client.x - bounds.left);
        localY = static_cast<float>(client.y - bounds.top);
        return true;
    };

    if (_raisedActive)
    {
        return accept(_raisedWidgetIndex, _raisedLayout.content);
    }

    RECT clientRect{};
    if (!GetClientRect(_window.get(), &clientRect) || clientRect.right <= 0 || clientRect.bottom <= 0)
    {
        return false;
    }
    std::array<RECT, PluginManager::kMaximumWidgetInstances> bounds{};
    const size_t count = _dashboardHost->WidgetCount();
    for (size_t index = 0; index < count; ++index)
    {
        bounds[index] = _dashboardHost->PixelBoundsAt(index, static_cast<UINT>(clientRect.right),
                                                      static_cast<UINT>(clientRect.bottom));
    }
    const size_t hit = HitTestTopmostWidget(client, bounds.data(), count);
    return hit != SIZE_MAX && accept(hit, bounds[hit]);
}

void Application::CancelInteractivePointer() noexcept
{
    if (_interactivePointerWidget != SIZE_MAX)
    {
        bool consumed = false;
        (void)ForwardInteractivePointer({}, 0, RedXePointerKindMouse, RedXePointerPhaseCancel, &consumed);
        (void)consumed;
        return;
    }
    _interactivePointerWidget = SIZE_MAX;
    _interactivePointerConsumed = false;
}

void Application::RefreshAppearance() noexcept
{
    RedXeAppearance appearance;
    DWORD light = 0, bytes = sizeof(light);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes) == ERROR_SUCCESS &&
        light)
        appearance.flags &= ~RedXeAppearanceDark;
    HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON))
        appearance.flags |= RedXeAppearanceHighContrast;
    const auto color = [](int index) noexcept -> uint32_t
    {
        const COLORREF value = GetSysColor(index);
        return 0xff000000U | uint32_t(GetRValue(value)) << 16 | uint32_t(GetGValue(value)) << 8 |
               uint32_t(GetBValue(value));
    };
    appearance.window = color(COLOR_WINDOW);
    appearance.windowText = color(COLOR_WINDOWTEXT);
    appearance.highlight = color(COLOR_HIGHLIGHT);
    appearance.highlightText = color(COLOR_HIGHLIGHTTEXT);
    appearance.button = color(COLOR_BTNFACE);
    appearance.buttonText = color(COLOR_BTNTEXT);
    appearance.disabledText = color(COLOR_GRAYTEXT);
    _renderer.SetAppearance(appearance);
    _frameInvalidated = true;
}
void Application::ClearTextServices() noexcept
{
    if (_textServices)
        _textServices->ClearClient();
    if (_textClient)
        _textClient->Disconnect();
    _textClient.reset();
}
void Application::RefreshAccessibility(uint64_t preparedWidgets) noexcept
{
    if (!_accessibility)
        return;
    if (!_dashboardHost || !_window || !_windowVisible || !_displayPoweredOn || _renderer.IsSuspended() ||
        _renderer.IsOccluded() || PageNavigationInProgress() || OverlayMotionInProgress() || _settingsErrorDialog)
    {
        _accessibility->ClearViews();
        return;
    }
    RECT client{};
    if (!GetClientRect(_window.get(), &client) || client.right <= 0 || client.bottom <= 0)
    {
        _accessibility->ClearViews();
        return;
    }
    std::array<AccessibleWidgetView, PluginManager::kMaximumWidgetInstances> views{};
    size_t count = 0;
    for (size_t index = 0; index < _dashboardHost->WidgetCount() && count < views.size(); ++index)
    {
        if (_raisedActive && index != _raisedWidgetIndex)
            continue;
        auto* widget = _dashboardHost->AccessibilityWidgetAt(index);
        if (!widget || _dashboardHost->RequiresPlaceholderAt(index))
            continue;
        const RECT bounds = _raisedActive ? _raisedLayout.content
                                          : _dashboardHost->PixelBoundsAt(index, static_cast<UINT>(client.right),
                                                                          static_cast<UINT>(client.bottom));
        POINT top{bounds.left, bounds.top}, bottom{bounds.right, bounds.bottom};
        if (!ClientToScreen(_window.get(), &top) || !ClientToScreen(_window.get(), &bottom))
            continue;
        views[count++] = {index,
                          widget,
                          _raisedActive ? 1U : 0U,
                          {top.x, top.y, bottom.x, bottom.y},
                          _keyboardWidgetIndex == index && GetFocus() == _window.get(),
                          (preparedWidgets & (uint64_t{1} << index)) != 0};
    }
    (void)_accessibility->Update(std::span<const AccessibleWidgetView>(views.data(), count));
}
void Application::HandleAccessibilityRequests() noexcept
{
    if (!_accessibility)
        return;
    RefreshAccessibility();
    AccessibilityRequest request;
    while (_accessibility->TakeRequest(request))
    {
        if (!_dashboardHost || (_raisedActive && request.index != _raisedWidgetIndex) ||
            request.viewId != (_raisedActive ? 1U : 0U))
            continue;
        if (request.focus)
        {
            ::SetFocus(_window.get());
            (void)FocusKeyboardWidget(request.index);
        }
        if (request.action == RedXePointerRaise)
            (void)TryRaiseWidgetAt(_window.get(), request.index);
        else if (request.action == RedXePointerDismiss && _raisedActive)
            DismissWidgetRaise();
        _frameInvalidated = true;
    }
    RefreshTextServices();
    RefreshAccessibility();
}
void Application::RefreshTextServices(bool layoutPrepared) noexcept
{
    if (!_keyboardWidget || !_dashboardHost || !_window || GetFocus() != _window.get() || !_windowVisible ||
        !_displayPoweredOn || _renderer.IsSuspended() || _renderer.IsOccluded() || PageNavigationInProgress() ||
        OverlayMotionInProgress() || (_raisedActive && _keyboardWidgetIndex != _raisedWidgetIndex))
    {
        ClearTextServices();
        return;
    }
    auto* widget = _dashboardHost->TextInputWidgetAt(_keyboardWidgetIndex);
    RedXeTextState state;
    if (!widget || widget->ReadTextState(_keyboardView, &state) != S_OK || !IsValidRedXeTextState(state))
    {
        ClearTextServices();
        return;
    }
    RECT client{};
    if (!GetClientRect(_window.get(), &client) || client.right <= 0 || client.bottom <= 0)
    {
        ClearTextServices();
        return;
    }
    const RECT bounds = _keyboardView
                            ? _raisedLayout.content
                            : _dashboardHost->PixelBoundsAt(_keyboardWidgetIndex, static_cast<UINT>(client.right),
                                                            static_cast<UINT>(client.bottom));
    POINT origin{bounds.left, bounds.top};
    if (!ClientToScreen(_window.get(), &origin))
    {
        ClearTextServices();
        return;
    }
    try
    {
        if (!_textServices)
        {
            auto service = std::make_unique<DxUi::TextInputServices>();
            if (FAILED(service->Attach(_window.get())))
                return;
            _textServices = std::move(service);
        }
        if (!_textClient || !_textClient->Matches(widget, _keyboardView, state.focusId))
        {
            ClearTextServices();
            _textClient = std::make_shared<WidgetTextClient>(widget, _keyboardView, state.focusId, origin);
        }
        _textClient->SetScreenOrigin(origin);
        if (!_textServices->HasClient() && _textServices->SetClient(_textClient) != S_OK)
        {
            ClearTextServices();
            return;
        }
        _textServices->NotifyChanged();
        if (layoutPrepared)
            _textServices->NotifyLayoutChanged();
    }
    catch (const std::bad_alloc&)
    {
        ClearTextServices();
    }
}
void Application::ClearKeyboardFocus() noexcept
{
    ClearTextServices();
    auto previous = std::move(_keyboardWidget);
    const uint32_t view = _keyboardView;
    _keyboardWidgetIndex = SIZE_MAX;
    _keyboardView = 0;
    if (previous)
        (void)previous->SetKeyboardFocus(FALSE, view);
}
bool Application::FocusKeyboardWidget(size_t index) noexcept
{
    auto* target = _dashboardHost ? _dashboardHost->KeyboardWidgetAt(index) : nullptr;
    const uint32_t view = _raisedActive && index == _raisedWidgetIndex ? 1U : 0U;
    if (_keyboardWidget.get() == target && _keyboardWidgetIndex == index && _keyboardView == view)
        return target != nullptr;
    ClearKeyboardFocus();
    if (!target || _dashboardHost->RequiresPlaceholderAt(index))
        return false;
    _keyboardWidget = target;
    _keyboardWidgetIndex = index;
    _keyboardView = view;
    if (FAILED(target->SetKeyboardFocus(TRUE, view)))
    {
        ClearKeyboardFocus();
        return false;
    }
    return true;
}
bool Application::AdvanceKeyboardWidget(bool reverse) noexcept
{
    const auto refreshText = wil::scope_exit([this]() noexcept { RefreshTextServices(); });
    if (!_dashboardHost || !_dashboardHost->WidgetCount())
        return false;
    const size_t count = _dashboardHost->WidgetCount();
    const size_t previous = _keyboardWidgetIndex;
    ClearKeyboardFocus();
    for (size_t offset = 1; offset <= count; ++offset)
    {
        const size_t candidate = previous >= count ? (reverse ? count - offset : offset - 1)
                                 : reverse         ? (previous + count - offset) % count
                                                   : (previous + offset) % count;
        if (_raisedActive && candidate != _raisedWidgetIndex)
            continue;
        if (!FocusKeyboardWidget(candidate))
            continue;
        const RedXeKeyEvent event{sizeof(RedXeKeyEvent), VK_TAB, reverse ? MK_SHIFT : 0U, _keyboardView, TRUE};
        (void)_keyboardWidget->OnKey(&event);
        return true;
    }
    return false;
}
bool Application::HandleKeyboardResult(HRESULT result) noexcept
{
    if (result == RedXePointerRaise)
    {
        const size_t index = _keyboardWidgetIndex;
        if (!_raisedActive && _window)
            (void)TryRaiseWidgetAt(_window.get(), index);
        return true;
    }
    if (result == RedXePointerDismiss)
    {
        if (_raisedActive)
            DismissWidgetRaise(true);
        return true;
    }
    return result == S_OK;
}
bool Application::ForwardWidgetKey(uint32_t key, bool down) noexcept
{
    const auto refreshText = wil::scope_exit([this]() noexcept { RefreshTextServices(); });
    if (!_windowVisible || !_displayPoweredOn || !_dashboardHost)
        return false;
    const uint32_t modifiers = ((GetKeyState(VK_SHIFT) & 0x8000) ? MK_SHIFT : 0U) |
                               ((GetKeyState(VK_CONTROL) & 0x8000) ? MK_CONTROL : 0U) |
                               ((GetKeyState(VK_MENU) & 0x8000) ? 0x20U : 0U);
    if (_keyboardWidget && _dashboardHost->KeyboardWidgetAt(_keyboardWidgetIndex) != _keyboardWidget.get())
        ClearKeyboardFocus();
    if (!_keyboardWidget)
        return down && key == VK_TAB && AdvanceKeyboardWidget((modifiers & MK_SHIFT) != 0);
    const RedXeKeyEvent event{sizeof(RedXeKeyEvent), key, modifiers, _keyboardView, down ? TRUE : FALSE};
    const HRESULT result = _keyboardWidget->OnKey(&event);
    if (result == RedXeKeyboardBoundary && down && key == VK_TAB)
        return AdvanceKeyboardWidget((modifiers & MK_SHIFT) != 0);
    return HandleKeyboardResult(result);
}
bool Application::ForwardWidgetCharacter(uint32_t character) noexcept
{
    const auto refreshText = wil::scope_exit([this]() noexcept { RefreshTextServices(); });
    if (!_windowVisible || !_displayPoweredOn || !_dashboardHost || !_keyboardWidget)
        return false;
    if (_dashboardHost->KeyboardWidgetAt(_keyboardWidgetIndex) != _keyboardWidget.get())
    {
        ClearKeyboardFocus();
        return false;
    }
    const uint32_t modifiers =
        ((GetKeyState(VK_SHIFT) & 0x8000) ? MK_SHIFT : 0U) | ((GetKeyState(VK_CONTROL) & 0x8000) ? MK_CONTROL : 0U);
    return HandleKeyboardResult(_keyboardWidget->OnCharacter(character, modifiers, _keyboardView));
}

bool Application::TryNavigateFromPageEdge(HWND window, POINT position) noexcept
{
    if (_raisedActive || OverlayMotionInProgress() || !_window || window != _window.get())
    {
        return false;
    }
    const PageEdgeState state = CurrentPageEdgeState();
    const RECT reachable = ReachableClientRect();
    const UINT dpi = GetDpiForWindow(window);
    for (size_t index = 0; index < _pageEdges.size(); ++index)
    {
        const int direction = index == 0 ? kPageEdgeDirectionPrevious : kPageEdgeDirectionNext;
        const RECT band = PageEdgeBandRectIn(reachable, direction, dpi);
        if (PageEdgeClickNavigates(state, direction, band, position))
        {
            return SUCCEEDED(NavigateToAdjacentPage(direction));
        }
    }
    return false;
}

void Application::OnRaisedContentActivateAttempt(HWND window, POINT position, ULONGLONG tick) noexcept
{
    if (!_raisedActive)
    {
        return;
    }
    if (PointInRectInclusive(_raisedLayout.close, position))
    {
        return;
    }
    if (!PointInRectInclusive(_raisedLayout.content, position))
    {
        return;
    }
    const UINT dpi = GetDpiForWindow(window);
    const UINT interval = GetDoubleClickTime();
    if (_activateWidgetIndex == _raisedWidgetIndex &&
        IsDoubleActivate(_activateTick, _activatePoint, tick, position, interval, DoubleActivateSlopPixels(dpi)))
    {
        _activateWidgetIndex = SIZE_MAX;
        DismissWidgetRaise(true);
        return;
    }
    _activateTick = tick;
    _activatePoint = position;
    _activateWidgetIndex = _raisedWidgetIndex;
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
    const RaisedLayout target = MakeRaisedLayout(clientWidth, clientHeight, extent, dpi, &tile);
    const RaisedLayout start = RaisedLayoutFromTile(tile, clientWidth, clientHeight, dpi);
    if (target.content.right <= target.content.left || target.content.bottom <= target.content.top ||
        start.content.right <= start.content.left || start.content.bottom <= start.content.top)
    {
        return E_UNEXPECTED;
    }

    CancelPageNavigation();
    HRESULT result = raisedWidget->SetRaised(TRUE);
    if (FAILED(result))
    {
        return result;
    }

    const HWND overlay =
        CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_NOPARENTNOTIFY | WS_EX_LAYERED, kRaiseOverlayClassName, L"",
                        WS_CHILD | WS_CLIPSIBLINGS, 0, 0, static_cast<int>(clientWidth), static_cast<int>(clientHeight),
                        window, nullptr, _instance, this);
    if (!overlay)
    {
        (void)raisedWidget->SetRaised(FALSE);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    _raiseOverlay.reset(overlay);
    _raiseTile = tile;
    _raiseTargetPixels = SIZE{target.content.right - target.content.left, target.content.bottom - target.content.top};
    _raisedWidgetIndex = widgetIndex;
    _raisedActive = true;
    if (_accessibility)
        _accessibility->ClearViews();
    _activateTick = 0;
    _activateWidgetIndex = SIZE_MAX;
    _raisedLayout = target;
    EnsureRaiseOverlayChrome(dpi);
    ShowWindow(overlay, SW_SHOWNA);

    result = ApplyRaiseVisual(start, 0);
    if (FAILED(result))
    {
        CompleteDismissImmediate();
        RefreshPageEdgeAffordances();
        return result;
    }
    BeginRaiseSettle(start, target, 0, kRaiseOverlayDimAlpha, false);
    (void)FocusKeyboardWidget(widgetIndex);
    RefreshPageEdgeAffordances();
    return S_OK;
}

void Application::DismissWidgetRaise(bool animate) noexcept
{
    const auto refreshEdges = wil::scope_exit([this]() noexcept { RefreshPageEdgeAffordances(); });
    if (!_raisedActive)
    {
        _raiseOverlay.reset();
        return;
    }
    if (_raiseSettleActive && _raiseDismissing && animate)
    {
        return;
    }
    if (!animate || !_window || !_qpcFrequency)
    {
        CompleteDismissImmediate();
        return;
    }

    RECT client{};
    if (!GetClientRect(_window.get(), &client) || client.right <= 0 || client.bottom <= 0)
    {
        CompleteDismissImmediate();
        return;
    }
    const UINT dpi = GetDpiForWindow(_window.get());
    const RaisedLayout tileLayout =
        RaisedLayoutFromTile(_raiseTile, static_cast<UINT>(client.right), static_cast<UINT>(client.bottom), dpi);
    if (tileLayout.content.right <= tileLayout.content.left)
    {
        CompleteDismissImmediate();
        return;
    }
    BeginRaiseSettle(_raisedLayout, tileLayout, _raiseDimAlpha, 0, true);
}

HRESULT Application::ApplyRaiseVisual(const RaisedLayout& layout, BYTE dimAlpha) noexcept
{
    _raisedLayout = layout;
    _raiseDimAlpha = dimAlpha;
    if (!_window || !_raiseOverlay || !_dashboardHost || _raisedWidgetIndex == SIZE_MAX)
    {
        return E_UNEXPECTED;
    }
    RECT client{};
    if (!GetClientRect(_window.get(), &client) || client.right <= 0 || client.bottom <= 0)
    {
        return E_UNEXPECTED;
    }
    const UINT clientWidth = static_cast<UINT>(client.right);
    const UINT clientHeight = static_cast<UINT>(client.bottom);
    const BYTE windowAlpha = dimAlpha == 0 ? 1 : dimAlpha;
    if (!SetLayeredWindowAttributes(_raiseOverlay.get(), 0, windowAlpha, LWA_ALPHA))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const HRGN region = CreateRaisedOverlayRegion(clientWidth, clientHeight, layout.content, layout.close);
    if (!region || !SetWindowRgn(_raiseOverlay.get(), region, FALSE))
    {
        if (region)
        {
            DeleteObject(region);
        }
        return HRESULT_FROM_WIN32(GetLastError());
    }
    HRESULT result = _renderer.SetRaisedOverlay(_raisedWidgetIndex, layout.content, _raiseTargetPixels);
    if (FAILED(result))
    {
        return result;
    }
    const UINT dpi = GetDpiForWindow(_window.get());
    if (dpi != 0)
    {
        result = _dashboardHost->ApplyRaisedNativeLayout(_raisedWidgetIndex, layout.content, dpi);
        if (FAILED(result))
        {
            return result;
        }
    }
    SetWindowPos(_raiseOverlay.get(), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    InvalidateRect(_raiseOverlay.get(), nullptr, FALSE);
    _frameInvalidated = true;
    return S_OK;
}

void Application::BeginRaiseSettle(const RaisedLayout& from, const RaisedLayout& to, BYTE dimFrom, BYTE dimTo,
                                   bool dismissing) noexcept
{
    if (_accessibility)
        _accessibility->ClearViews();
    _raiseSettleFrom = from;
    _raiseSettleTo = to;
    _raiseDimFrom = dimFrom;
    _raiseDimTo = dimTo;
    _raiseDismissing = dismissing;
    if (!_qpcFrequency)
    {
        (void)ApplyRaiseVisual(to, dimTo);
        CompleteRaiseSettle();
        return;
    }
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now))
    {
        (void)ApplyRaiseVisual(to, dimTo);
        CompleteRaiseSettle();
        return;
    }
    _raiseSettleStartQpc = static_cast<UINT64>(now.QuadPart);
    const UINT durationMs = RaiseSettleDurationMilliseconds(from.content, to.content);
    _raiseSettleDurationQpc = static_cast<UINT64>(durationMs) * _qpcFrequency / 1000ULL;
    if (_raiseSettleDurationQpc == 0)
    {
        _raiseSettleDurationQpc = _qpcFrequency / 10ULL;
    }
    _raiseSettleActive = true;
    _frameInvalidated = true;
}

void Application::TickRaiseSettle() noexcept
{
    if (!_raiseSettleActive || !_window)
    {
        return;
    }

    float t = 1.0f;
    LARGE_INTEGER now{};
    if (_raiseSettleDurationQpc != 0 && QueryPerformanceCounter(&now))
    {
        const UINT64 elapsed = static_cast<UINT64>(now.QuadPart) - _raiseSettleStartQpc;
        t = elapsed >= _raiseSettleDurationQpc
                ? 1.0f
                : static_cast<float>(elapsed) / static_cast<float>(_raiseSettleDurationQpc);
    }
    const RaisedLayout layout = InterpolateRaisedLayout(_raiseSettleFrom, _raiseSettleTo, t);
    const BYTE dim = InterpolateRaisedAlpha(_raiseDimFrom, _raiseDimTo, t);
    if (FAILED(ApplyRaiseVisual(layout, dim)) || t >= 1.0f)
    {
        CompleteRaiseSettle();
    }
}

void Application::CompleteRaiseSettle() noexcept
{
    _raiseSettleActive = false;
    if (_raiseDismissing)
    {
        CompleteDismissImmediate();
        RefreshPageEdgeAffordances();
        return;
    }
    (void)ApplyRaiseVisual(_raiseSettleTo, _raiseDimTo);
}

void Application::CompleteDismissImmediate() noexcept
{
    if (_accessibility)
        _accessibility->ClearViews();
    const size_t previousKeyboardWidget = _keyboardWidgetIndex;
    ClearKeyboardFocus();
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
    _raiseTile = {};
    _raiseTargetPixels = {};
    _raiseDimAlpha = 0;
    _raiseSettleActive = false;
    _raiseDismissing = false;
    _raiseSettleFrom = {};
    _raiseSettleTo = {};
    _raiseCloseHovered = false;
    _raiseCloseMouseTracking = false;
    _frameInvalidated = true;
    if (_window && GetFocus() == _window.get() && previousKeyboardWidget != SIZE_MAX)
        (void)FocusKeyboardWidget(previousKeyboardWidget);
}

void Application::SetRaiseCloseHovered(bool hovered) noexcept
{
    if (_raiseCloseHovered == hovered)
    {
        return;
    }
    _raiseCloseHovered = hovered;
    if (_raiseOverlay)
    {
        InvalidateRect(_raiseOverlay.get(), nullptr, FALSE);
    }
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
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
    case WM_SETTINGCHANGE:
        RefreshAppearance();
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            POINT cursor{};
            if (GetCursorPos(&cursor) && ScreenToClient(overlay, &cursor) &&
                PointInRectInclusive(_raisedLayout.close, cursor))
            {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                SetRaiseCloseHovered(true);
            }
            else
            {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                SetRaiseCloseHovered(false);
            }
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
    {
        if (!_raiseCloseMouseTracking)
        {
            TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, overlay, 0};
            _raiseCloseMouseTracking = TrackMouseEvent(&track) != FALSE;
        }
        const POINT position{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        SetRaiseCloseHovered(PointInRectInclusive(_raisedLayout.close, position));
        return 0;
    }
    case WM_MOUSELEAVE:
        _raiseCloseMouseTracking = false;
        SetRaiseCloseHovered(false);
        return 0;
    case WM_PAINT:
        PaintRaiseOverlay(overlay);
        return 0;
    case WM_LBUTTONUP:
    {
        const POINT position{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (PointInRectInclusive(_raisedLayout.close, position))
        {
            DismissWidgetRaise(true);
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
            DismissWidgetRaise(true);
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
    if (!_raiseCloseHoverBrush)
    {
        _raiseCloseHoverBrush.reset(CreateSolidBrush(RGB(52, 62, 84)));
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
        if (_raiseCloseHovered && _raiseCloseHoverBrush)
        {
            FillRect(deviceContext, &close, _raiseCloseHoverBrush.get());
        }
        const HGDIOBJ previousFont = SelectObject(deviceContext, _raiseCloseFont.get());
        const int previousMode = SetBkMode(deviceContext, TRANSPARENT);
        const COLORREF glyphColor = _raiseCloseHovered ? RGB(255, 255, 255) : RGB(214, 220, 230);
        const COLORREF previousColor = SetTextColor(deviceContext, glyphColor);
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

    _settingsStore.SuppressDocumentWrites(true);
    const auto resumeWrites = wil::scope_exit([&]() noexcept { _settingsStore.SuppressDocumentWrites(false); });
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
        constexpr int width = 600;
        constexpr int height = 280;
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
        if (_accessibility)
            _accessibility->ClearViews();
        const HWND text = CreateWindowExW(0, L"STATIC", std::wstring(message).c_str(),
                                          WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_EDITCONTROL, 24, 20,
                                          width - 48, 170, _settingsErrorDialog, reinterpret_cast<HMENU>(100),
                                          _instance, nullptr);
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

bool Application::WaitForFrameLatency() noexcept
{
    const HANDLE waitable = _renderer.FrameLatencyWaitableObject();
    if (!waitable)
    {
        return true;
    }
    // Handles are checked before the message queue, so a free buffer always wins over pending input; pending input
    // only defers the frame while the previous one is still being consumed.
    const DWORD waitResult = MsgWaitForMultipleObjectsEx(1, &waitable, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (waitResult == WAIT_OBJECT_0)
    {
        return true;
    }
    if (waitResult == WAIT_OBJECT_0 + 1)
    {
        return false;
    }
    const DWORD error = waitResult == WAIT_FAILED ? GetLastError() : ERROR_INVALID_DATA;
    _runtimeFailure = error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    CloseMainWindow();
    return false;
}

void Application::CheckDeviceAdapter() noexcept
{
    if (!_rendererReady || !_window)
    {
        return;
    }
    const HRESULT result = _renderer.EnsureDeviceForWindowMonitor();
    if (result == S_OK)
    {
        ClearScheduledFrameDeadline();
        _frameInvalidated = true;
        return;
    }
    if (FAILED(result))
    {
        _runtimeFailure = result;
        OutputDebugStringW(L"Direct3D device could not follow the window to its new adapter.\n");
        PostMessageW(_window.get(), WM_CLOSE, 0, 0);
    }
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

bool Application::OverlayMotionInProgress() const noexcept
{
    return _raiseSettleActive;
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
        DashboardRequiresContinuousFrames() || PageNavigationInProgress() || OverlayMotionInProgress())
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
    if (!visible && _accessibility)
        _accessibility->ClearViews();
    if (!visible)
        ClearKeyboardFocus();
    if (!visible)
        CancelInteractivePointer();
    HRESULT result = _dashboardHost->SetWidgetsVisible(visible);
    if (SUCCEEDED(result) && _transitionDashboardHost)
    {
        result = _transitionDashboardHost->SetWidgetsVisible(visible);
    }
    return result;
}

void Application::CloseMainWindow() noexcept
{
    if (_accessibility)
        _accessibility->Disconnect();
    _accessibility.reset();
    ClearKeyboardFocus();
    _textServices.reset();
    PluginHost::Instance().SetUiInvalidateTarget(nullptr);
    if (_dropRegistered && _window)
    {
        (void)RevokeDragDrop(_window.get());
        _dropRegistered = false;
    }
    _pageEdgeMouseTracking = false;
    DestroyPageEdgeAffordances();
    _settingsWatcher.Stop();
    DismissWidgetRaise(false);
    CancelPageNavigation();
    _renderer.Shutdown();
    _rendererReady = false;
    if (_transitionDashboardHost)
    {
        _transitionDashboardHost->Shutdown();
    }
    if (_dashboardHost)
    {
        _dashboardHost->Shutdown();
    }
    PluginHost::Instance().SetSettingsPersistHandler(nullptr, nullptr);
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
    const auto refreshAccessibility = wil::scope_exit(
        [&]() noexcept
        {
            if (message == WM_SIZE || message == WM_DPICHANGED || message == WM_SHOWWINDOW ||
                message == WM_POWERBROADCAST || message == WM_MOVE || message == WM_SETFOCUS || message == WM_KILLFOCUS)
                RefreshAccessibility();
        });
    if (_accessibility && _accessibility->IsMessage(message, wParam))
    {
        HandleAccessibilityRequests();
        return 0;
    }
    if (_textServices)
    {
        bool handled = false;
        (void)_textServices->HandleMessage(message, wParam, lParam, handled);
        if (handled)
        {
            RefreshTextServices();
            return 0;
        }
    }
    switch (message)
    {
    case WM_GETOBJECT:
        if (static_cast<LONG>(lParam) == UiaRootObjectId)
        {
            if (!_accessibility && FAILED(AccessibilityHost::Create(window, _accessibility)))
                break;
            RefreshAccessibility();
            return UiaReturnRawElementProvider(window, wParam, lParam, _accessibility->Provider());
        }
        break;
    case WM_SIZE:
        return OnSize(window, LOWORD(lParam), HIWORD(lParam));
    case WM_DPICHANGED:
    {
        const LRESULT dpiResult = OnDpiChanged(window, LOWORD(wParam), reinterpret_cast<const RECT*>(lParam));
        CheckDeviceAdapter();
        return dpiResult;
    }
    case WM_EXITSIZEMOVE:
    case WM_DISPLAYCHANGE:
        CheckDeviceAdapter();
        break;
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
        if (_interactiveOwnsPointer && GET_POINTERID_WPARAM(wParam) == _interactivePointerId)
            CancelInteractivePointer();
        if (PageTouchesContain(GET_POINTERID_WPARAM(wParam)))
        {
            CancelPageNavigation();
        }
        return 0;
    case WM_LBUTTONDOWN:
        OnMouseButtonDown(window, lParam);
        return 0;
    case WM_LBUTTONUP:
        OnMouseButtonUp(window, lParam);
        return 0;
    case WM_MOUSEWHEEL:
    {
        if (!_windowVisible || !_displayPoweredOn || _interactiveOwnsPointer || _pagePanStarted)
            return 0;
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        bool consumed = false;
        if (ScreenToClient(window, &point))
            (void)ForwardInteractivePointer(point, 1, RedXePointerKindMouse, RedXePointerPhaseWheel, &consumed,
                                            GET_KEYSTATE_WPARAM(wParam),
                                            static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)));
        if (consumed)
            return 0;
        break;
    }
    case WM_MOUSEMOVE:
        // The top-level window owns edge-band hover: a band is created only while the pointer is inside its zone.
        if (!IsPointerSynthesizedMouseMessage())
        {
            (void)ForwardInteractivePointer({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, 1, RedXePointerKindMouse,
                                            RedXePointerPhaseMove, nullptr);
            if (_interactiveOwnsPointer)
                return 0;
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
    case kPageEdgeNavigateMessage:
        (void)NavigateToAdjacentPage(static_cast<int>(wParam));
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            POINT cursor{};
            if (GetCursorPos(&cursor) && ScreenToClient(window, &cursor))
            {
                const PageEdgeState state = CurrentPageEdgeState();
                const RECT reachable = ReachableClientRect();
                const UINT dpi = GetDpiForWindow(window);
                for (size_t index = 0; index < _pageEdges.size(); ++index)
                {
                    const int direction = index == 0 ? kPageEdgeDirectionPrevious : kPageEdgeDirectionNext;
                    const RECT band = PageEdgeBandRectIn(reachable, direction, dpi);
                    if (PageEdgeClickNavigates(state, direction, band, cursor))
                    {
                        SetCursor(LoadCursorW(nullptr, IDC_HAND));
                        return TRUE;
                    }
                }
            }
        }
        break;
    case WM_MOUSELEAVE:
        _pageEdgeMouseTracking = false;
        // Entering a band is also a leave for the parent, so re-test the cursor instead of hiding unconditionally.
        UpdatePageEdgeHover();
        return 0;
    case WM_CANCELMODE:
    case WM_KILLFOCUS:
        CancelInteractivePointer();
        ClearKeyboardFocus();
        break;
    case WM_CAPTURECHANGED:
        if (_interactiveOwnsPointer && _interactivePointerKind == RedXePointerKindMouse &&
            reinterpret_cast<HWND>(lParam) != window)
            CancelInteractivePointer();
        break;
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
    case WM_MOVE:
        if (_textClient)
            RefreshTextServices(true);
        CheckDeviceAdapter();
        break;
    case WM_KEYDOWN:
        if (ForwardWidgetKey(static_cast<uint32_t>(wParam), true))
            return 0;
        if (wParam == VK_ESCAPE)
        {
            if (_raisedActive)
            {
                DismissWidgetRaise(true);
                return 0;
            }
            CloseMainWindow();
            return 0;
        }
        break;
    case WM_KEYUP:
        if (ForwardWidgetKey(static_cast<uint32_t>(wParam), false))
            return 0;
        break;
    case WM_CHAR:
        if (ForwardWidgetCharacter(static_cast<uint32_t>(wParam)))
            return 0;
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
    ClearKeyboardFocus();
    CancelInteractivePointer();
    const auto refreshEdges = wil::scope_exit([this]() noexcept { RefreshPageEdgeAffordances(); });
    if (!_rendererReady)
    {
        return 0;
    }
    DismissWidgetRaise(false);
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
    ClearKeyboardFocus();
    CancelInteractivePointer();
    const auto refreshEdges = wil::scope_exit([this]() noexcept { RefreshPageEdgeAffordances(); });
    if (!suggestedBounds)
    {
        return 0;
    }

    DismissWidgetRaise(false);
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

HRESULT Application::HandleOleDragOver(POINT client, DWORD* effect) noexcept
{
    if (!effect)
    {
        return E_POINTER;
    }
    *effect = DROPEFFECT_NONE;
    size_t index = SIZE_MAX;
    float localX = 0.0f;
    float localY = 0.0f;
    if (!HitInteractiveLocal(client, index, localX, localY))
    {
        HandleOleDragLeave();
        return S_OK;
    }
    IRedXeInteractiveWidget* widget = _dashboardHost->InteractiveWidgetAt(index);
    if (!widget)
    {
        HandleOleDragLeave();
        return S_OK;
    }
    if (_dragWidgetIndex != SIZE_MAX && _dragWidgetIndex != index)
    {
        IRedXeInteractiveWidget* previous = _dashboardHost->InteractiveWidgetAt(_dragWidgetIndex);
        if (previous)
        {
            (void)previous->OnDragLeave();
        }
    }
    _dragWidgetIndex = index;
    if (widget->OnDragOver(localX, localY) == S_OK)
    {
        *effect = DROPEFFECT_COPY;
    }
    return S_OK;
}

void Application::HandleOleDragLeave() noexcept
{
    if (_dragWidgetIndex == SIZE_MAX || !_dashboardHost)
    {
        _dragWidgetIndex = SIZE_MAX;
        return;
    }
    IRedXeInteractiveWidget* widget = _dashboardHost->InteractiveWidgetAt(_dragWidgetIndex);
    _dragWidgetIndex = SIZE_MAX;
    if (widget)
    {
        (void)widget->OnDragLeave();
    }
}

HRESULT Application::HandleOleDrop(POINT client, const wchar_t* const* targets, uint32_t count, DWORD* effect) noexcept
{
    if (!effect)
    {
        return E_POINTER;
    }
    *effect = DROPEFFECT_NONE;
    size_t index = SIZE_MAX;
    float localX = 0.0f;
    float localY = 0.0f;
    IRedXeInteractiveWidget* widget = nullptr;
    if (HitInteractiveLocal(client, index, localX, localY) && _dashboardHost)
    {
        widget = _dashboardHost->InteractiveWidgetAt(index);
    }
    HandleOleDragLeave();
    if (!widget || !targets || count == 0)
    {
        return S_OK;
    }
    const uint32_t itemCount = (std::min)(count, kRedXeMaximumDropItems);
    std::array<RedXeDropItem, kRedXeMaximumDropItems> items{};
    for (uint32_t itemIndex = 0; itemIndex < itemCount; ++itemIndex)
    {
        items[itemIndex].sizeBytes = sizeof(RedXeDropItem);
        items[itemIndex].target = targets[itemIndex];
    }
    const RedXeDropEvent event{sizeof(RedXeDropEvent), localX, localY, itemCount, items.data()};
    if (widget->OnDrop(&event) == S_OK)
    {
        *effect = DROPEFFECT_COPY;
    }
    return S_OK;
}

HRESULT Application::ApplyWidgetSettingsPersist(const char* instanceId, const char* settingsJsonUtf8,
                                                uint32_t settingsBytes) noexcept
{
    if (!_settings || !instanceId || !settingsJsonUtf8 || settingsBytes == 0)
    {
        return E_INVALIDARG;
    }
    if (!_persistSettingsToDisk)
    {
        return PatchWidgetInstanceSettings(*_settings, instanceId, std::string_view(settingsJsonUtf8, settingsBytes));
    }
    return _settingsStore.PersistWidgetSettings(*_settings, instanceId,
                                                std::string_view(settingsJsonUtf8, settingsBytes));
}

HRESULT Application::SettingsPersistThunk(void* context, const char* instanceId, const char* settingsJsonUtf8,
                                          uint32_t settingsBytes) noexcept
{
    auto* application = static_cast<Application*>(context);
    return application ? application->ApplyWidgetSettingsPersist(instanceId, settingsJsonUtf8, settingsBytes)
                       : E_POINTER;
}
