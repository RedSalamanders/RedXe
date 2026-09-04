#include "DashboardHost.h"
#include "DeskClockTestContract.h"
#include "FrameScheduler.h"
#include "MatrixRainTestContract.h"
#include "PageEdgeAffordance.h"
#include "PageNavigation.h"
#include "PluginHost.h"
#include "PluginManager.h"
#include "ProcessViewerTestContract.h"
#include "Renderer.h"
#include "Settings.h"
#include "StudioClockTestContract.h"
#include "WidgetRaise.h"

#include <tlhelp32.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>

#include <psapi.h>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr UINT kHostWidth = 2560;
constexpr UINT kHostHeight = 720;

// Thread count for this process. The shared plugin runtime owns exactly one acquisition worker no matter how many
// PluginManager instances are live, so staging an adjacent page must not raise this.
[[nodiscard]] DWORD CountProcessThreads() noexcept
{
    const wil::unique_handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)};
    if (!snapshot)
    {
        return 0;
    }
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    const DWORD processId = GetCurrentProcessId();
    DWORD count = 0;
    if (Thread32First(snapshot.get(), &entry))
    {
        do
        {
            if (entry.th32OwnerProcessID == processId)
            {
                ++count;
            }
        } while (Thread32Next(snapshot.get(), &entry));
    }
    return count;
}

void Check(bool condition, std::wstring_view message, bool& success) noexcept
{
    if (condition)
    {
        std::wcout << L"[       OK ] " << message << L'\n';
        return;
    }
    std::wcerr << L"[  FAILED  ] " << message << L'\n';
    success = false;
}

template <typename Function> [[nodiscard]] Function ResolveFunction(HMODULE module, const char* name) noexcept
{
    const FARPROC procedure = module ? GetProcAddress(module, name) : nullptr;
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(procedure));
    std::memcpy(&function, &procedure, sizeof(function));
    return function;
}

class AttachedHostWindow final
{
  public:
    AttachedHostWindow() = default;
    ~AttachedHostWindow() = default;

    AttachedHostWindow(const AttachedHostWindow&) = delete;
    AttachedHostWindow& operator=(const AttachedHostWindow&) = delete;
    AttachedHostWindow(AttachedHostWindow&&) = delete;
    AttachedHostWindow& operator=(AttachedHostWindow&&) = delete;

    [[nodiscard]] HRESULT Initialize(UINT width, UINT height) noexcept
    {
        if (_window || width == 0 || height == 0)
        {
            return E_INVALIDARG;
        }
        if (EnsureWindowClass() == 0)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        const HWND window =
            CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kWindowClassName, L"RedXe host test",
                            WS_POPUP | WS_CLIPCHILDREN, -32000, -32000, static_cast<int>(width),
                            static_cast<int>(height), nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        _window.reset(window);
        return S_OK;
    }

    [[nodiscard]] HWND Get() const noexcept
    {
        return _window.get();
    }

    [[nodiscard]] UINT Dpi() const noexcept
    {
        return _window ? GetDpiForWindow(_window.get()) : 0;
    }

    void PumpMessages() const noexcept
    {
        MSG message{};
        uint32_t processed = 0;
        while (processed < 4096 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            ++processed;
        }
    }

  private:
    static constexpr wchar_t kWindowClassName[] = L"RedXe.Tests.AttachedHostWindow";

    static ATOM EnsureWindowClass() noexcept
    {
        static const ATOM atom = []() noexcept
        {
            WNDCLASSW windowClass{};
            windowClass.lpfnWndProc = DefWindowProcW;
            windowClass.hInstance = GetModuleHandleW(nullptr);
            windowClass.lpszClassName = kWindowClassName;
            return RegisterClassW(&windowClass);
        }();
        return atom;
    }

    wil::unique_hwnd _window;
};

struct ProcessMemorySnapshot final
{
    uint64_t privateBytes = 0;
    uint64_t workingSetBytes = 0;
};

[[nodiscard]] HRESULT QueryProcessMemorySnapshot(ProcessMemorySnapshot& snapshot) noexcept
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters)))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    snapshot.privateBytes = counters.PrivateUsage;
    snapshot.workingSetBytes = counters.WorkingSetSize;
    return S_OK;
}

[[nodiscard]] HRESULT LoadDeployedSettings(const wchar_t* fileName, AppSettings& settings) noexcept
{
    try
    {
        std::array<wchar_t, 32768> executable{};
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size() - 1)
        {
            return length == 0 ? HRESULT_FROM_WIN32(GetLastError()) : HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
        const std::filesystem::path path =
            std::filesystem::path(executable.data()).parent_path() / L"Settings" / fileName;
        return LoadAppSettingsFile(path.wstring(), settings);
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] MatrixRainGetTestDiagnosticsFn GetMatrixDiagnosticsFunction() noexcept
{
    return ResolveFunction<MatrixRainGetTestDiagnosticsFn>(GetModuleHandleW(L"MatrixRain.dll"),
                                                           kMatrixRainGetTestDiagnosticsExport);
}

[[nodiscard]] HRESULT ReadMatrixDiagnostics(MatrixRainTestDiagnostics& diagnostics) noexcept
{
    const MatrixRainGetTestDiagnosticsFn getDiagnostics = GetMatrixDiagnosticsFunction();
    if (!getDiagnostics)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    diagnostics = MatrixRainTestDiagnostics{sizeof(MatrixRainTestDiagnostics), 0, 0, 0};
    return getDiagnostics(&diagnostics);
}

[[nodiscard]] ProcessViewerGetTestDiagnosticsFn GetProcessViewerDiagnosticsFunction() noexcept
{
    return ResolveFunction<ProcessViewerGetTestDiagnosticsFn>(GetModuleHandleW(L"ProcessViewer.dll"),
                                                              kProcessViewerGetTestDiagnosticsExport);
}

[[nodiscard]] HRESULT ReadProcessViewerDiagnostics(ProcessViewerTestDiagnostics& diagnostics) noexcept
{
    const ProcessViewerGetTestDiagnosticsFn getDiagnostics = GetProcessViewerDiagnosticsFunction();
    if (!getDiagnostics)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    diagnostics = ProcessViewerTestDiagnostics{sizeof(ProcessViewerTestDiagnostics)};
    return getDiagnostics(&diagnostics);
}

[[nodiscard]] StudioClockGetTestDiagnosticsFn GetStudioClockDiagnosticsFunction() noexcept
{
    return ResolveFunction<StudioClockGetTestDiagnosticsFn>(GetModuleHandleW(L"StudioClock.dll"),
                                                            kStudioClockGetTestDiagnosticsExport);
}

[[nodiscard]] HRESULT ReadStudioClockDiagnostics(StudioClockTestDiagnostics& diagnostics) noexcept
{
    const StudioClockGetTestDiagnosticsFn getDiagnostics = GetStudioClockDiagnosticsFunction();
    if (!getDiagnostics)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    diagnostics = StudioClockTestDiagnostics{sizeof(StudioClockTestDiagnostics)};
    return getDiagnostics(&diagnostics);
}

[[nodiscard]] DeskClockGetTestDiagnosticsFn GetDeskClockDiagnosticsFunction() noexcept
{
    return ResolveFunction<DeskClockGetTestDiagnosticsFn>(GetModuleHandleW(L"DeskClock.dll"),
                                                          kDeskClockGetTestDiagnosticsExport);
}

[[nodiscard]] HRESULT ReadDeskClockDiagnostics(DeskClockTestDiagnostics& diagnostics) noexcept
{
    const DeskClockGetTestDiagnosticsFn getDiagnostics = GetDeskClockDiagnosticsFunction();
    if (!getDiagnostics)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    diagnostics = DeskClockTestDiagnostics{sizeof(DeskClockTestDiagnostics)};
    return getDiagnostics(&diagnostics);
}

void TestFrameScheduler(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] host frame scheduler decisions\n";
    HostFrameState state{};
    state.windowVisible = true;
    state.displayPoweredOn = true;
    state.rendererSuspended = false;
    state.continuousFramesRequired = true;
    Check(SelectHostFrameAction(state) == HostFrameAction::Render, L"visible continuous host renders", success);

    state.windowVisible = false;
    Check(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage, L"hidden host waits", success);
    state.windowVisible = true;
    state.displayPoweredOn = false;
    Check(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage, L"display-off host waits", success);
    state.displayPoweredOn = true;
    state.rendererSuspended = true;
    Check(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage, L"minimized host waits", success);
    state.rendererSuspended = false;
    state.rendererOccluded = true;
    Check(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage,
          L"occluded host waits without a DXGI notification", success);
    state.occlusionStatusChanged = true;
    Check(SelectHostFrameAction(state) == HostFrameAction::ProbeOcclusion,
          L"occluded host probes only after a DXGI notification", success);

    state.rendererOccluded = false;
    state.occlusionStatusChanged = false;
    state.continuousFramesRequired = false;
    state.frameInvalidated = false;
    Check(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage, L"clean static host waits", success);
    Check(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage,
          L"an unrelated message leaves a static host waiting", success);
    state.frameInvalidated = true;
    Check(SelectHostFrameAction(state) == HostFrameAction::Render, L"invalidated static host renders", success);

    state.frameInvalidated = false;
    state.pageNavigationActive = true;
    Check(SelectHostFrameAction(state) == HostFrameAction::Render,
          L"an active page swipe keeps presenting so widget animation continues", success);
    state.rendererOccluded = true;
    Check(SelectHostFrameAction(state) == HostFrameAction::Render,
          L"page navigation keeps presenting when DXGI reports the swap chain occluded", success);
    state.pageNavigationActive = false;
    Check(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage,
          L"occluded host without page navigation waits", success);
}

void TestPageSwipePolicy(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] page swipe axis lock, rubber-band, commit, and settle\n";
    Check(PageSwipeLocksHorizontal(20, 4, 16), L"horizontal delta past the threshold locks page navigation", success);
    Check(!PageSwipeLocksHorizontal(10, 4, 16), L"sub-threshold contact does not lock page navigation", success);
    Check(!PageSwipeLocksHorizontal(20, 24, 16), L"a diagonal with larger Y does not lock page navigation", success);
    Check(PageSwipeRejectsAsVertical(8, 20, 16), L"vertical-dominant movement is rejected for page navigation",
          success);
    Check(!PageSwipeRejectsAsVertical(20, 8, 16), L"horizontal-dominant movement is not rejected as vertical", success);

    Check(ApplyPageEdgeResistance(-400, 1000, false) == -400, L"unblocked travel follows the pointer 1:1", success);
    Check(ApplyPageEdgeResistance(100, 400, true) == 25, L"blocked ends rubber-band at one quarter travel", success);
    Check(ApplyPageEdgeResistance(400, 400, true) == 50, L"blocked rubber-band is capped at one eighth of the page",
          success);
    Check(!PageSwipeBlocksDirection(-10, true, true, false), L"wrap allows travel off the first page", success);
    Check(PageSwipeBlocksDirection(10, false, true, false), L"no-wrap blocks travel before the first page", success);

    Check(ShouldCommitPageSwipe(-250, 1000, 0.0f, 96, false, false, false),
          L"travel of one quarter width commits the page", success);
    Check(!ShouldCommitPageSwipe(-100, 1000, 0.0f, 96, false, false, false),
          L"a short slow swipe returns to the current page", success);
    Check(ShouldCommitPageSwipe(-80, 1000, -2400.0f, 96, false, false, false),
          L"a same-direction flick commits below the distance threshold", success);
    Check(!ShouldCommitPageSwipe(80, 1000, 2400.0f, 96, false, true, false),
          L"a flick at a blocked first page does not commit", success);

    Check(EaseOutCubic(0.0f) == 0.0f && EaseOutCubic(1.0f) == 1.0f, L"ease-out cubic starts at 0 and ends at 1",
          success);
    Check(InterpolatePageOffset(0, 100, 0.0f) == 0 && InterpolatePageOffset(0, 100, 1.0f) == 100,
          L"settle interpolation preserves start and end offsets", success);
    Check(InterpolatePageOffset(0, 100, 0.5f) == 88, L"settle interpolation uses ease-out cubic", success);
    Check(PageSettleDurationMilliseconds(2000, 0.0f) == 280 && PageSettleDurationMilliseconds(10, 20000.0f) == 140,
          L"settle duration stays within the 140-280 ms window", success);
}

void TestWidgetRaisePolicy(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] raised overlay geometry, hit-test, and double-activate\n";
    Check(RedXeRaisedExtentIsValid(RedXeRaisedExtentQuarter) && RedXeRaisedExtentIsValid(RedXeRaisedExtentFull) &&
              !RedXeRaisedExtentIsValid(static_cast<RedXeRaisedExtent>(0)),
          L"raised extent enumerators are the four client fractions", success);
    Check(RedXeRaisedExtentScale(RedXeRaisedExtentQuarter) == 0.25f &&
              RedXeRaisedExtentScale(RedXeRaisedExtentThird) > 0.33f &&
              RedXeRaisedExtentScale(RedXeRaisedExtentThird) < 0.34f &&
              RedXeRaisedExtentScale(RedXeRaisedExtentHalf) == 0.5f &&
              RedXeRaisedExtentScale(RedXeRaisedExtentFull) == 1.0f,
          L"raised extents map to 1/4, 1/3, 1/2, and 1/1", success);
    Check(RaisedDipPixels(28, 96) == 28 && RaisedDipPixels(28, 192) == 56, L"raised chrome scales with DPI", success);

    const RECT full{0, 0, 2560, 720};
    const RECT tile{100, 40, 700, 400};
    Check(WidgetFillsClient(full, 2560, 720) && !WidgetFillsClient(tile, 2560, 720), L"a full-client tile cannot raise",
          success);
    Check(!CanRaiseWidget(full, 2560, 720, RedXeRaisedExtentHalf) &&
              CanRaiseWidget(tile, 2560, 720, RedXeRaisedExtentHalf),
          L"raise requires a valid extent and a tile that is not already full-client", success);

    const RaisedLayout half = MakeRaisedLayout(2560, 720, RedXeRaisedExtentHalf, 96);
    const LONG halfWidth = half.overlay.right - half.overlay.left;
    const LONG halfHeight = half.overlay.bottom - half.overlay.top;
    Check(half.overlay.left == 0 && half.content.top == half.overlay.top && half.content.bottom == 720 &&
              halfWidth == 1280 && halfHeight == 720 && half.close.right == half.overlay.right - 8 &&
              half.close.top == half.overlay.top + 8 && half.shadow.left == half.overlay.right,
          L"half raise is a full-height half-width slice with a corner close control", success);

    const RECT pulseTile{0, 0, 590, 240};
    const RaisedLayout pulse = MakeRaisedLayout(2560, 720, RedXeRaisedExtentQuarter, 96, &pulseTile);
    Check(pulse.overlay.left == 0 && pulse.overlay.right - pulse.overlay.left == 640 &&
              pulse.overlay.bottom - pulse.overlay.top == 720,
          L"quarter raise is a full-height 1/4-width slice covering the original column", success);

    const RECT clockTile{900, 0, 1300, 360};
    const RaisedLayout raisedClock = MakeRaisedLayout(2560, 720, RedXeRaisedExtentHalf, 96, &clockTile);
    Check(raisedClock.overlay.left == 900 && raisedClock.content.right - raisedClock.content.left == 1280 &&
              raisedClock.content.bottom - raisedClock.content.top == 720,
          L"raising a stacked clock uses a full-height slice instead of a letterboxed card", success);

    Check(PointInRectInclusive(half.close, POINT{half.close.left, half.close.top}) &&
              !PointInRectInclusive(half.close, POINT{half.close.right, half.close.top}),
          L"close hit-test uses a half-open rectangle", success);

    const RaisedLayout fullLayout = MakeRaisedLayout(2560, 720, RedXeRaisedExtentFull, 96);
    Check(fullLayout.overlay.left == 0 && fullLayout.overlay.right == 2560 &&
              fullLayout.content.right - fullLayout.content.left == 2560,
          L"full raise fills the client", success);

    wil::unique_hrgn region{CreateRaisedOverlayRegion(2560, 720, half.content, half.close)};
    Check(region &&
              !PtInRegion(region.get(), (half.content.left + half.content.right) / 2,
                          (half.content.top + half.content.bottom) / 2) &&
              PtInRegion(region.get(), half.close.left + 1, half.close.top + 1) &&
              PtInRegion(region.get(), half.shadow.left + 1, half.shadow.top + 1),
          L"overlay region punches a hole over plugin content", success);

    const RECT tiles[] = {{0, 0, 200, 200}, {150, 50, 400, 300}};
    Check(HitTestTopmostWidget(POINT{160, 60}, tiles, 2) == 1 && HitTestTopmostWidget(POINT{10, 10}, tiles, 2) == 0 &&
              HitTestTopmostWidget(POINT{500, 500}, tiles, 2) == SIZE_MAX,
          L"widget hit-test prefers the later overlapping tile", success);

    Check(IsDoubleActivate(1000, POINT{10, 10}, 1300, POINT{12, 11}, 500, 16) &&
              !IsDoubleActivate(1000, POINT{10, 10}, 1600, POINT{12, 11}, 500, 16) &&
              !IsDoubleActivate(1000, POINT{10, 10}, 1100, POINT{40, 10}, 500, 16) &&
              !IsDoubleActivate(1000, POINT{10, 10}, 1100, POINT{12, 11}, 0, 16),
          L"double-activate requires the system interval and slop", success);
}

void TestWidgetRaiseHost(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] raised overlay host composition\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":4},"pages":[{"name":"System","layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":3,"arrangeAlong":"short-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.system-pulse"}},{"sizeRatio":1,"widget":{"plugin":"builtin.cpu-meter"}},{"sizeRatio":1,"widget":{"plugin":"builtin.memory-meter"}}]},{"sizeRatio":4,"arrangeAlong":"short-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.process-viewer"}},{"sizeRatio":1,"widget":{"plugin":"builtin.gpu-processes"}}]},{"sizeRatio":3,"arrangeAlong":"short-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.network-meter"}},{"sizeRatio":1,"widget":{"plugin":"builtin.storage-meter"}}]},{"sizeRatio":3,"arrangeAlong":"short-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.gpu-meter"}},{"sizeRatio":1,"widget":{"plugin":"builtin.thermal-meter"}},{"sizeRatio":1,"widget":{"plugin":"builtin.power-meter"}}]}]}}]})json";

    AttachedHostWindow window;
    HRESULT result = window.Initialize(kHostWidth, kHostHeight);
    AppSettings settings{};
    if (SUCCEEDED(result))
    {
        result = ParseAppSettingsJson(settingsJson, settings);
    }
    PluginManager plugins;
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(settings);
    }
    Check(SUCCEEDED(result) && plugins.WidgetCount() == 10, L"System page creates ten raised-capable viewers", success);
    if (FAILED(result))
    {
        return;
    }

    constexpr std::array expectedExtents{
        RedXeRaisedExtentQuarter, RedXeRaisedExtentThird, RedXeRaisedExtentThird, RedXeRaisedExtentHalf,
        RedXeRaisedExtentHalf,    RedXeRaisedExtentHalf,  RedXeRaisedExtentHalf,  RedXeRaisedExtentThird,
        RedXeRaisedExtentHalf,    RedXeRaisedExtentThird,
    };
    bool extentsMatch = true;
    for (size_t index = 0; index < plugins.WidgetCount(); ++index)
    {
        IRedXeRaisedWidget* raised = plugins.RaisedWidgetAt(index);
        RedXeRaisedExtent extent = static_cast<RedXeRaisedExtent>(0);
        extentsMatch = extentsMatch && raised && raised->GetRaisedExtent(&extent) == S_OK &&
                       extent == expectedExtents[index] && raised->GetRaisedExtent(nullptr) == E_POINTER;
    }
    Check(extentsMatch, L"host asks each System viewer for its raised extent", success);

    DashboardHost dashboard;
    result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    Renderer renderer;
    if (SUCCEEDED(result))
    {
        result = renderer.Initialize(window.Get(), true, dashboard);
    }
    Check(SUCCEEDED(result), L"raised-overlay host initializes a hidden WARP swap chain", success);
    if (FAILED(result))
    {
        dashboard.Shutdown();
        return;
    }

    result = dashboard.SetWidgetsVisible(true);
    const RECT processTile = dashboard.PixelBoundsAt(3, kHostWidth, kHostHeight);
    Check(SUCCEEDED(result) && !WidgetFillsClient(processTile, kHostWidth, kHostHeight),
          L"Process Viewer is not already full-client on the System page", success);

    IRedXeRaisedWidget* processRaised = dashboard.RaisedWidgetAt(3);
    const RaisedLayout layout =
        MakeRaisedLayout(kHostWidth, kHostHeight, RedXeRaisedExtentHalf, window.Dpi(), &processTile);
    Check(processRaised && SUCCEEDED(processRaised->SetRaised(TRUE)) &&
              SUCCEEDED(dashboard.ApplyRaisedNativeLayout(3, layout.content, window.Dpi())) &&
              SUCCEEDED(renderer.SetRaisedOverlay(3, layout.content)),
          L"host raises Process Viewer to the plugin-requested half overlay", success);

    result = renderer.Render(0.5f, 1.0f / 60.0f);
    Check(SUCCEEDED(result) && renderer.HasRaisedOverlay() && renderer.RaisedOverlayIndex() == 3 &&
              renderer.LastFrameWidgetCount() == 11 && renderer.LastFrameSuccessfulWidgetCount() == 11 &&
              renderer.RaisedContentRect().left == layout.content.left &&
              renderer.RaisedContentRect().top == layout.content.top,
          L"raised Process Viewer keeps every tile drawing and draws itself again at overlay content", success);
    Check(!dashboard.RequiresContinuousFrames(), L"raising Process Viewer does not start continuous frames", success);
    uint32_t delay = 0;
    Check(dashboard.GetNextFrameDelayMilliseconds(&delay) == S_OK && delay != 0,
          L"settled raised Process Viewer still publishes a scheduled delay", success);

    Check(SUCCEEDED(processRaised->SetRaised(FALSE)), L"Process Viewer accepts SetRaised(FALSE)", success);
    renderer.ClearRaisedOverlay();
    Check(SUCCEEDED(dashboard.ClearRaisedNativeLayout(window.Dpi())), L"native raise layout restores", success);
    result = renderer.Render(0.6f, 1.0f / 60.0f);
    Check(SUCCEEDED(result) && !renderer.HasRaisedOverlay() && renderer.LastFrameWidgetCount() == 10 &&
              renderer.LastFrameSuccessfulWidgetCount() == 10,
          L"dismissing the overlay restores every System Data GPU tile", success);

    renderer.Shutdown();
    dashboard.Shutdown();
}

void TestWidgetRaiseNative(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] raised native-window overlay\n";
    AttachedHostWindow window;
    HRESULT result = window.Initialize(kHostWidth, kHostHeight);
    Check(SUCCEEDED(result), L"hidden host window initializes for native raise", success);
    if (FAILED(result))
    {
        return;
    }

    PluginManager plugins;
    AppSettings settings{};
    result = LoadDeployedSettings(kRedXeDebugSettingsFileName, settings);
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(settings);
    }
    DashboardHost dashboard;
    if (SUCCEEDED(result))
    {
        result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), true);
    }
    Renderer renderer;
    if (SUCCEEDED(result))
    {
        result = renderer.Initialize(window.Get(), true, dashboard);
    }
    Check(SUCCEEDED(result) && dashboard.WindowWidgetAt(2) != nullptr && dashboard.RaisedWidgetAt(2) != nullptr,
          L"Debug GDI Orbit exposes the raised sibling", success);
    if (FAILED(result))
    {
        return;
    }

    const RECT tile = dashboard.PixelBoundsAt(2, kHostWidth, kHostHeight);
    RedXeRaisedExtent extent = static_cast<RedXeRaisedExtent>(0);
    Check(CanRaiseWidget(tile, kHostWidth, kHostHeight, RedXeRaisedExtentQuarter) &&
              dashboard.RaisedWidgetAt(2)->GetRaisedExtent(&extent) == S_OK && extent == RedXeRaisedExtentQuarter,
          L"GDI Orbit requests a quarter overlay", success);

    const RaisedLayout layout =
        MakeRaisedLayout(kHostWidth, kHostHeight, RedXeRaisedExtentQuarter, window.Dpi(), &tile);
    Check(SUCCEEDED(dashboard.RaisedWidgetAt(2)->SetRaised(TRUE)) &&
              SUCCEEDED(dashboard.ApplyRaisedNativeLayout(2, layout.content, window.Dpi())) &&
              SUCCEEDED(renderer.SetRaisedOverlay(2, layout.content)),
          L"host raises the native GDI Orbit container into overlay content", success);

    HWND gdiContainer = GetWindow(window.Get(), GW_CHILD);
    RECT gdiBounds{};
    if (gdiContainer && GetWindowRect(gdiContainer, &gdiBounds))
    {
        MapWindowPoints(HWND_DESKTOP, window.Get(), reinterpret_cast<POINT*>(&gdiBounds), 2);
    }
    Check(gdiContainer && gdiBounds.left == layout.content.left && gdiBounds.top == layout.content.top &&
              gdiBounds.right == layout.content.right && gdiBounds.bottom == layout.content.bottom &&
              dashboard.RaisedNativeIndex() == 2,
          L"raised GDI container matches overlay content", success);

    result = renderer.Render(0.25f, 1.0f / 60.0f);
    Check(SUCCEEDED(result) && renderer.LastFrameWidgetCount() == 3,
          L"raising a window widget still draws every GPU tile under the dim", success);

    Check(SUCCEEDED(dashboard.RaisedWidgetAt(2)->SetRaised(FALSE)) &&
              SUCCEEDED(dashboard.ClearRaisedNativeLayout(window.Dpi())),
          L"dismissing GDI Orbit restores tile layout", success);
    renderer.ClearRaisedOverlay();
    if (gdiContainer && GetWindowRect(gdiContainer, &gdiBounds))
    {
        MapWindowPoints(HWND_DESKTOP, window.Get(), reinterpret_cast<POINT*>(&gdiBounds), 2);
    }
    Check(gdiBounds.left == 640 && gdiBounds.top == 360 && gdiBounds.right == 1280 && gdiBounds.bottom == 720,
          L"native GDI container returns to its adaptive tile", success);

    result = renderer.Render(0.5f, 1.0f / 60.0f);
    Check(SUCCEEDED(result) && renderer.LastFrameWidgetCount() == 3,
          L"dismissed Debug page renders every GPU widget again", success);

    renderer.Shutdown();
    dashboard.Shutdown();
}

void TestReleaseHostIntegration(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] Release Matrix plugin and production host integration\n";
    MatrixRainGetTestDiagnosticsFn getDiagnostics = nullptr;
    {
        AttachedHostWindow window;
        HRESULT result = window.Initialize(kHostWidth, kHostHeight);
        Check(SUCCEEDED(result), L"hidden 2560x720 host window initializes", success);
        if (FAILED(result))
        {
            return;
        }

        PluginManager plugins;
        AppSettings releaseSettings{};
        result = LoadDeployedSettings(kRedXeReleaseSettingsFileName, releaseSettings);
        if (SUCCEEDED(result))
        {
            result = plugins.Initialize(releaseSettings);
        }
        Check(SUCCEEDED(result), L"Release plugin composition initializes", success);
        Check(plugins.ProviderCount() == 1 && plugins.WidgetCount() == 1,
              L"Release composition contains one active-page provider and widget", success);
        const AdaptiveWidgetPlacement releasePlacement = plugins.AdaptivePlacementAt(0);
        Check(plugins.UsesAdaptivePlacementAt(0) && releasePlacement.depth == 1 &&
                  releasePlacement.steps[0] == LayoutSplitStep{LayoutAxis::LongSide, 0, 1, 1},
              L"Release manager stages only the active page and preserves its full-display adaptive instance", success);
        Check(GetModuleHandleW(L"RotatingTriangle.dll") != nullptr,
              L"Release static discovery maps the gallery triangle DLL without creating its page", success);
        Check(GetModuleHandleW(L"GdiOrbit.dll") != nullptr,
              L"Release static discovery maps the gallery GDI DLL without creating its page", success);
        Check(GetModuleHandleW(L"ProcessViewer.dll") != nullptr,
              L"Release static discovery maps the process viewer DLL without creating its page", success);
        Check(GetModuleHandleW(L"StudioClock.dll") != nullptr,
              L"Release static discovery maps the Studio Clock DLL without creating its page", success);
        Check(GetModuleHandleW(L"DeskClock.dll") != nullptr,
              L"Release static discovery maps the Desk Clock DLL without creating its page", success);
        StudioClockTestDiagnostics inactiveClock{};
        Check(SUCCEEDED(ReadStudioClockDiagnostics(inactiveClock)) && inactiveClock.liveProviderCount == 0 &&
                  inactiveClock.liveWidgetCount == 0 && inactiveClock.liveSharedDeviceResourceSetCount == 0,
              L"inactive gallery Studio Clock owns no provider, widget, or device resources", success);
        DeskClockTestDiagnostics inactiveDeskClock{};
        Check(SUCCEEDED(ReadDeskClockDiagnostics(inactiveDeskClock)) && inactiveDeskClock.liveProviders == 0 &&
                  inactiveDeskClock.liveWidgets == 0 && inactiveDeskClock.liveDeviceResourceSets == 0,
              L"inactive gallery Desk Clock owns no provider, widget, or device resources", success);
        if (FAILED(result))
        {
            return;
        }

        getDiagnostics = GetMatrixDiagnosticsFunction();
        Check(getDiagnostics != nullptr, L"Matrix lifetime diagnostics export resolves", success);
        if (!getDiagnostics)
        {
            return;
        }
        Check(getDiagnostics(nullptr) == E_POINTER, L"Matrix diagnostics rejects a null record", success);
        MatrixRainTestDiagnostics shortDiagnostics{sizeof(uint32_t), 0, 0, 0};
        Check(getDiagnostics(&shortDiagnostics) == E_INVALIDARG, L"Matrix diagnostics rejects a short record", success);

        MatrixRainTestDiagnostics diagnostics{};
        result = ReadMatrixDiagnostics(diagnostics);
        Check(SUCCEEDED(result) && diagnostics.liveProviderCount == 1 && diagnostics.liveWidgetCount == 1 &&
                  diagnostics.liveDeviceResourceSetCount == 0,
              L"plugin manager owns one Matrix provider/widget before device initialization", success);

        DashboardHost dashboard;
        result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
        Check(SUCCEEDED(result), L"production dashboard host initializes without showing a window", success);
        const WidgetPlacement placement = dashboard.PlacementAt(0);
        Check(placement.x == 0.0f && placement.y == 0.0f && placement.width == 2560.0f && placement.height == 720.0f,
              L"sole Matrix widget fills the design canvas", success);
        Check(dashboard.RequiresContinuousFrames(), L"Matrix descriptor makes the host continuous", success);
        uint32_t scheduledDelay = 123;
        Check(dashboard.GetNextFrameDelayMilliseconds(nullptr) == E_POINTER,
              L"scheduled-frame aggregation rejects a null output", success);
        Check(dashboard.GetNextFrameDelayMilliseconds(&scheduledDelay) == S_FALSE && scheduledDelay == 0,
              L"a dashboard without scheduled widgets publishes no deadline", success);
        if (FAILED(result))
        {
            return;
        }

        Renderer renderer;
        result = renderer.Initialize(window.Get(), true, dashboard);
        Check(SUCCEEDED(result), L"production renderer initializes a hidden WARP swap chain", success);
        if (FAILED(result))
        {
            return;
        }
        result = ReadMatrixDiagnostics(diagnostics);
        Check(SUCCEEDED(result) && diagnostics.liveDeviceResourceSetCount == 1,
              L"renderer creates exactly one Matrix device-resource set", success);

        result = renderer.Render(0.0f, 0.0f);
        Check(SUCCEEDED(result) && renderer.LastFrameWidgetCount() == 1 &&
                  renderer.LastFrameSuccessfulWidgetCount() == 1,
              L"hidden production host renders and presents the Matrix widget", success);

        result = dashboard.Resize(0, 0, window.Dpi());
        if (SUCCEEDED(result))
        {
            result = renderer.Resize(0, 0);
        }
        if (SUCCEEDED(result))
        {
            result = renderer.Render(1.0f, 1.0f / 60.0f);
        }
        Check(SUCCEEDED(result) && renderer.IsSuspended() && renderer.LastFrameWidgetCount() == 0,
              L"zero-sized host suspends without invoking a widget", success);

        result = dashboard.Resize(kHostWidth, kHostHeight, window.Dpi());
        if (SUCCEEDED(result))
        {
            result = renderer.Resize(kHostWidth, kHostHeight);
        }
        if (SUCCEEDED(result))
        {
            result = renderer.Render(2.0f, 1.0f / 60.0f);
        }
        Check(SUCCEEDED(result) && !renderer.IsSuspended() && renderer.LastFrameSuccessfulWidgetCount() == 1,
              L"restored host resumes Matrix rendering", success);

        renderer.Shutdown();
        dashboard.Shutdown();
        result = ReadMatrixDiagnostics(diagnostics);
        Check(SUCCEEDED(result) && diagnostics.liveProviderCount == 1 && diagnostics.liveWidgetCount == 1 &&
                  diagnostics.liveDeviceResourceSetCount == 0,
              L"host shutdown releases Matrix device resources before plugin objects", success);

        AppSettings changed = releaseSettings;
        constexpr std::string_view changedMatrix =
            R"json({"seed":2000,"glyphHeightDips":18,"densityPercent":80,"speedPercent":100,"trailLengthGlyphs":18,"mutationPerSecond":8,"headColor":"#D8FFE5","trailColor":"#00E65C","backgroundColor":"#010502","glowPercent":35})json";
        result = SetJsonObjectSettings(changedMatrix, changed.dashboard.pages[0].widgets[0].privateConfiguration);
        if (SUCCEEDED(result))
        {
            result = plugins.Reconfigure(changed);
        }
        Check(SUCCEEDED(result) && plugins.WidgetCount() == 1,
              L"host transactionally applies changed effective widget settings", success);
        result = ReadMatrixDiagnostics(diagnostics);
        Check(SUCCEEDED(result) && diagnostics.liveProviderCount == 1 && diagnostics.liveWidgetCount == 1 &&
                  diagnostics.liveDeviceResourceSetCount == 0,
              L"transactional reconfigure leaves one live provider and widget", success);

        AppSettings invalid = changed;
        PluginSettings* matrixPlugin = FindPluginSettings(invalid, "builtin.matrix-rain");
        if (matrixPlugin)
        {
            matrixPlugin->enabled = false;
        }
        const HRESULT invalidResult = plugins.Reconfigure(invalid);
        Check(FAILED(invalidResult) && plugins.WidgetCount() == 1,
              L"invalid plugin references preserve the active page transactionally", success);

        result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
        if (SUCCEEDED(result))
        {
            result = renderer.Initialize(window.Get(), true, dashboard);
        }
        if (SUCCEEDED(result))
        {
            result = renderer.Render(3.0f, 1.0f / 60.0f);
        }
        Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 1,
              L"reconfigured Matrix renders through the production host", success);
        renderer.Shutdown();
        dashboard.Shutdown();
    }

    MatrixRainTestDiagnostics finalDiagnostics{sizeof(MatrixRainTestDiagnostics), 0, 0, 0};
    const HRESULT result = getDiagnostics ? getDiagnostics(&finalDiagnostics) : E_UNEXPECTED;
    Check(SUCCEEDED(result) && finalDiagnostics.liveProviderCount == 0 && finalDiagnostics.liveWidgetCount == 0 &&
              finalDiagnostics.liveDeviceResourceSetCount == 0,
          L"Release host teardown leaves no Matrix objects or device-resource sets", success);
}

void TestStudioClockScheduling(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] Studio Clock scheduled production host integration\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":4},"pages":[{"name":"Clock","layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.studio-clock"}}]}}]})json";
    constexpr std::string_view changedConfiguration =
        R"json({"showSecondProgress":true,"externalDotsAlwaysOn":true,"showSeconds":true,"secondsColor":"#00EE44","showDate":true,"dateFormat":"yyyy-mm-dd","timeColor":"#E0E0FF","backgroundColor":"#050607"})json";

    AttachedHostWindow window;
    HRESULT result = window.Initialize(kHostWidth, kHostHeight);
    AppSettings settings{};
    if (SUCCEEDED(result))
    {
        result = ParseAppSettingsJson(settingsJson, settings);
    }
    PluginManager plugins;
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(settings);
    }
    Check(SUCCEEDED(result) && plugins.ProviderCount() == 1 && plugins.WidgetCount() == 1,
          L"Studio Clock production composition initializes", success);
    if (FAILED(result))
    {
        return;
    }

    const StudioClockSetTestTimeFn setTime =
        ResolveFunction<StudioClockSetTestTimeFn>(GetModuleHandleW(L"StudioClock.dll"), kStudioClockSetTestTimeExport);
    Check(setTime != nullptr, L"Studio Clock deterministic-time export resolves", success);
    if (!setTime)
    {
        return;
    }
    const StudioClockTestTime initialTime{sizeof(StudioClockTestTime), 2024, 12, 31, 23, 59, 46, 0};
    result = setTime(&initialTime);

    StudioClockTestDiagnostics diagnostics{};
    if (SUCCEEDED(result))
    {
        result = ReadStudioClockDiagnostics(diagnostics);
    }
    Check(SUCCEEDED(result) && diagnostics.liveProviderCount == 1 && diagnostics.liveWidgetCount == 1 &&
              diagnostics.liveSharedDeviceResourceSetCount == 0 && diagnostics.liveConstantBufferCount == 0,
          L"Studio Clock manager owns CPU objects before renderer initialization", success);

    AppSettings invalid = settings;
    result = SetJsonObjectSettings(R"json({"showSeconds":true})json",
                                   invalid.dashboard.pages[0].widgets[0].privateConfiguration);
    const HRESULT invalidResult = SUCCEEDED(result) ? plugins.Reconfigure(invalid) : result;
    Check(FAILED(invalidResult) && plugins.WidgetCount() == 1,
          L"invalid Studio Clock live settings preserve the active widget", success);

    AppSettings changed = settings;
    result = SetJsonObjectSettings(changedConfiguration, changed.dashboard.pages[0].widgets[0].privateConfiguration);
    if (SUCCEEDED(result))
    {
        result = plugins.Reconfigure(changed);
    }
    Check(SUCCEEDED(result) && plugins.ProviderCount() == 1 && plugins.WidgetCount() == 1,
          L"valid Studio Clock live settings replace the widget transactionally", success);
    if (FAILED(result))
    {
        return;
    }

    DashboardHost dashboard;
    result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    Check(SUCCEEDED(result) && !dashboard.RequiresContinuousFrames(),
          L"Studio Clock remains a scheduled static widget instead of continuous animation", success);
    uint32_t scheduledDelay = 0;
    if (SUCCEEDED(result))
    {
        result = dashboard.GetNextFrameDelayMilliseconds(&scheduledDelay);
    }
    Check(SUCCEEDED(result) && scheduledDelay == 1001,
          L"Studio Clock publishes the guarded next-second boundary through DashboardHost", success);
    if (FAILED(result))
    {
        return;
    }

    Renderer renderer;
    result = renderer.Initialize(window.Get(), true, dashboard);
    if (SUCCEEDED(result))
    {
        result = renderer.Render(0.0f, 0.0f);
    }
    if (SUCCEEDED(result))
    {
        result = ReadStudioClockDiagnostics(diagnostics);
    }
    Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 1 && diagnostics.lastMapCount == 1 &&
              diagnostics.lastDrawCount == 2 && diagnostics.lastInstanceCount == 402 &&
              diagnostics.liveSharedDeviceResourceSetCount == 1 && diagnostics.liveConstantBufferCount == 1,
          L"Studio Clock renders configured maximum content within its resource budgets", success);

    if (SUCCEEDED(result))
    {
        result = renderer.Render(0.1f, 0.1f);
    }
    if (SUCCEEDED(result))
    {
        result = ReadStudioClockDiagnostics(diagnostics);
    }
    Check(SUCCEEDED(result) && diagnostics.lastMapCount == 0, L"an unrelated host frame reuses Studio Clock constants",
          success);

    const StudioClockTestTime rolledTime{sizeof(StudioClockTestTime), 2024, 12, 31, 23, 59, 47, 0};
    if (SUCCEEDED(result))
    {
        result = setTime(&rolledTime);
    }
    if (SUCCEEDED(result))
    {
        result = dashboard.GetNextFrameDelayMilliseconds(&scheduledDelay);
    }
    Check(SUCCEEDED(result) && scheduledDelay == 1,
          L"a changed wall-clock bucket requests one immediate corrective frame", success);
    if (SUCCEEDED(result))
    {
        result = renderer.Render(1.0f, 0.9f);
    }
    if (SUCCEEDED(result))
    {
        result = dashboard.GetNextFrameDelayMilliseconds(&scheduledDelay);
    }
    Check(SUCCEEDED(result) && scheduledDelay == 1001, L"the corrective frame establishes a fresh second deadline",
          success);

    renderer.Shutdown();
    dashboard.Shutdown();
    result = ReadStudioClockDiagnostics(diagnostics);
    Check(SUCCEEDED(result) && diagnostics.liveProviderCount == 1 && diagnostics.liveWidgetCount == 1 &&
              diagnostics.liveSharedDeviceResourceSetCount == 0 && diagnostics.liveConstantBufferCount == 0,
          L"Studio Clock host shutdown releases device resources before plugin CPU objects", success);
    (void)setTime(nullptr);
}

void TestDeskClockScheduling(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] Desk Clock scheduled flip production host integration\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":4},"pages":[{"name":"Clock","layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.desk-clock"}}]}}]})json";
    constexpr std::string_view changedConfiguration =
        R"json({"flipDurationMilliseconds":300,"backgroundColor":"#050607","cardColor":"#D02030","digitColor":"#F0F0FF","dateColor":"#C0C0D0"})json";

    AttachedHostWindow window;
    HRESULT result = window.Initialize(kHostWidth, kHostHeight);
    AppSettings settings{};
    if (SUCCEEDED(result))
    {
        result = ParseAppSettingsJson(settingsJson, settings);
    }
    PluginManager plugins;
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(settings);
    }
    Check(SUCCEEDED(result) && plugins.ProviderCount() == 1 && plugins.WidgetCount() == 1,
          L"Desk Clock production composition initializes", success);
    if (FAILED(result))
    {
        return;
    }

    const HMODULE module = GetModuleHandleW(L"DeskClock.dll");
    const DeskClockSetTestTimeFn setTime = ResolveFunction<DeskClockSetTestTimeFn>(module, kDeskClockSetTestTimeExport);
    const DeskClockGetTestDiagnosticsFn getDiagnostics =
        ResolveFunction<DeskClockGetTestDiagnosticsFn>(module, kDeskClockGetTestDiagnosticsExport);
    Check(setTime != nullptr && getDiagnostics != nullptr, L"Desk Clock deterministic test exports resolve", success);
    if (!setTime || !getDiagnostics)
    {
        return;
    }
    Check(getDiagnostics(nullptr) == E_POINTER, L"Desk Clock diagnostics rejects a null record", success);
    DeskClockTestDiagnostics shortDiagnostics{sizeof(uint32_t)};
    Check(getDiagnostics(&shortDiagnostics) == E_INVALIDARG, L"Desk Clock diagnostics rejects a short record", success);

    const DeskClockTestTime initialTime{sizeof(DeskClockTestTime), 2024, 8, 6, 31, 19, 59, 59, 500};
    result = setTime(&initialTime);
    DeskClockTestDiagnostics diagnostics{};
    if (SUCCEEDED(result))
    {
        result = ReadDeskClockDiagnostics(diagnostics);
    }
    Check(SUCCEEDED(result) && diagnostics.liveProviders == 1 && diagnostics.liveWidgets == 1 &&
              diagnostics.liveDeviceResourceSets == 0,
          L"Desk Clock manager owns bounded CPU objects before renderer initialization", success);

    AppSettings invalid = settings;
    result = SetJsonObjectSettings(R"json({"flipDurationMilliseconds":100})json",
                                   invalid.dashboard.pages[0].widgets[0].privateConfiguration);
    const HRESULT invalidResult = SUCCEEDED(result) ? plugins.Reconfigure(invalid) : result;
    Check(FAILED(invalidResult) && plugins.WidgetCount() == 1,
          L"invalid Desk Clock live settings preserve the active widget", success);

    AppSettings changed = settings;
    result = SetJsonObjectSettings(changedConfiguration, changed.dashboard.pages[0].widgets[0].privateConfiguration);
    if (SUCCEEDED(result))
    {
        result = plugins.Reconfigure(changed);
    }
    Check(SUCCEEDED(result) && plugins.ProviderCount() == 1 && plugins.WidgetCount() == 1,
          L"valid Desk Clock live settings replace the widget transactionally", success);
    if (FAILED(result))
    {
        (void)setTime(nullptr);
        return;
    }

    DashboardHost dashboard;
    result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    Check(SUCCEEDED(result) && !dashboard.RequiresContinuousFrames(),
          L"Desk Clock is scheduled and remains idle between displayed seconds", success);
    uint32_t scheduledDelay = 0;
    if (SUCCEEDED(result))
    {
        result = dashboard.GetNextFrameDelayMilliseconds(&scheduledDelay);
    }
    Check(SUCCEEDED(result) && scheduledDelay == 1,
          L"an uninitialized Desk Clock requests one immediate bootstrap frame", success);
    if (FAILED(result))
    {
        (void)setTime(nullptr);
        return;
    }

    Renderer renderer;
    result = renderer.Initialize(window.Get(), true, dashboard);
    DeskClockTestDiagnostics beforeRender{};
    if (SUCCEEDED(result))
    {
        result = ReadDeskClockDiagnostics(beforeRender);
    }
    if (SUCCEEDED(result))
    {
        result = renderer.Render(0.0f, 0.0f);
    }
    DeskClockTestDiagnostics afterStatic{};
    if (SUCCEEDED(result))
    {
        result = ReadDeskClockDiagnostics(afterStatic);
    }
    Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 1 &&
              afterStatic.liveDeviceResourceSets == 1 &&
              afterStatic.constantUploads == beforeRender.constantUploads + 1 &&
              afterStatic.drawCalls == beforeRender.drawCalls + 3,
          L"Desk Clock renders one static upload and three batched draws at 2560x720", success);

    if (SUCCEEDED(result))
    {
        result = dashboard.GetNextFrameDelayMilliseconds(&scheduledDelay);
    }
    Check(SUCCEEDED(result) && scheduledDelay == 500,
          L"Desk Clock publishes the exact remaining delay to the next second", success);

    const DeskClockTestTime rolledTime{sizeof(DeskClockTestTime), 2024, 8, 6, 31, 20, 0, 0, 0};
    if (SUCCEEDED(result))
    {
        result = setTime(&rolledTime);
    }
    if (SUCCEEDED(result))
    {
        result = dashboard.GetNextFrameDelayMilliseconds(&scheduledDelay);
    }
    Check(SUCCEEDED(result) && scheduledDelay == 1,
          L"a changed Desk Clock sample requests one immediate corrective frame", success);
    if (SUCCEEDED(result))
    {
        result = renderer.Render(1.0f, 0.001f);
    }
    DeskClockTestDiagnostics duringFlip{};
    if (SUCCEEDED(result))
    {
        result = ReadDeskClockDiagnostics(duringFlip);
    }
    Check(SUCCEEDED(result) && duringFlip.constantUploads == afterStatic.constantUploads + 1 &&
              duringFlip.drawCalls == afterStatic.drawCalls + 4,
          L"an active half-flap uses one upload and four bounded draws", success);
    if (SUCCEEDED(result))
    {
        result = dashboard.GetNextFrameDelayMilliseconds(&scheduledDelay);
    }
    Check(SUCCEEDED(result) && scheduledDelay == 1, L"Desk Clock requests smooth frames only while its flap moves",
          success);

    if (SUCCEEDED(result))
    {
        result = renderer.Render(1.351f, 0.350f);
    }
    DeskClockTestDiagnostics afterFlip{};
    if (SUCCEEDED(result))
    {
        result = ReadDeskClockDiagnostics(afterFlip);
    }
    if (SUCCEEDED(result))
    {
        result = dashboard.GetNextFrameDelayMilliseconds(&scheduledDelay);
    }
    Check(SUCCEEDED(result) && afterFlip.drawCalls == duringFlip.drawCalls + 3 && scheduledDelay >= 640 &&
              scheduledDelay <= 650,
          L"Desk Clock returns to three static draws and a blocked deadline after the flip", success);

    const uint64_t hiddenSamples = afterFlip.timeSamples;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (SUCCEEDED(result))
    {
        result = ReadDeskClockDiagnostics(afterFlip);
    }
    Check(SUCCEEDED(result) && afterFlip.timeSamples == hiddenSamples,
          L"an inactive Desk Clock interval performs no plugin-owned clock work", success);

    renderer.Shutdown();
    dashboard.Shutdown();
    result = ReadDeskClockDiagnostics(afterFlip);
    Check(SUCCEEDED(result) && afterFlip.liveDeviceResourceSets == 0,
          L"Desk Clock host shutdown releases all device resources", success);
    (void)setTime(nullptr);
}

void TestDataProviderLookup(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] Shared host data-provider lookup\n";
    PluginHost host;

    Check(host.GetDataProvider("builtin.system-data", nullptr) == E_POINTER,
          L"host data-provider lookup rejects a null output", success);

    IRedXeDataProvider* invalid = reinterpret_cast<IRedXeDataProvider*>(1);
    HRESULT result = host.GetDataProvider("", &invalid);
    Check(result == E_INVALIDARG && !invalid, L"host data-provider lookup rejects an invalid ID", success);

    IRedXeDataProvider* uncleared = reinterpret_cast<IRedXeDataProvider*>(1);
    result = host.GetDataProvider("missing", &uncleared);
    Check(result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && !uncleared,
          L"unknown host data-provider lookup clears its output", success);

    wil::com_ptr_nothrow<IRedXeDataProvider> first;
    wil::com_ptr_nothrow<IRedXeDataProvider> second;
    result = host.GetDataProvider("builtin.system-data", first.put());
    if (SUCCEEDED(result))
    {
        result = host.GetDataProvider("builtin.system-data", second.put());
    }

    wil::com_ptr_nothrow<IUnknown> firstIdentity;
    wil::com_ptr_nothrow<IUnknown> secondIdentity;
    if (SUCCEEDED(result))
    {
        result = first.query_to(firstIdentity.put());
    }
    if (SUCCEEDED(result))
    {
        result = second.query_to(secondIdentity.put());
    }
    Check(SUCCEEDED(result) && firstIdentity.get() == secondIdentity.get(),
          L"repeated host lookup shares one provider runtime", success);

    const RedXeDataSetDescriptor* descriptors = nullptr;
    uint32_t descriptorCount = 0;
    if (SUCCEEDED(result))
    {
        result = first->GetDataSets(&descriptors, &descriptorCount);
    }
    Check(SUCCEEDED(result) && descriptors && descriptorCount == 22, L"host data provider exposes its source datasets",
          success);

    wil::com_ptr_nothrow<IRedXeDataProvider> unsupported;
    result = host.GetDataProvider("builtin.rotating-triangle", unsupported.put());
    Check(result == E_NOINTERFACE && !unsupported, L"host data-provider lookup rejects a widget-only plugin", success);
}

void TestProcessViewerSubscription(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] Process Viewer data subscription\n";
    AttachedHostWindow window;
    HRESULT result = window.Initialize(kHostWidth, kHostHeight);
    AppSettings settings{};
    if (SUCCEEDED(result))
    {
        constexpr std::string_view processViewerComposition =
            R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.process-viewer"}},{"sizeRatio":1,"widget":{"plugin":"builtin.process-viewer"}}]}}]})json";
        result = ParseAppSettingsJson(processViewerComposition, settings);
    }
    Check(SUCCEEDED(result), L"Process Viewer isolated composition is parsed", success);
    if (FAILED(result))
    {
        return;
    }

    ProcessViewerGetTestDiagnosticsFn getDiagnostics = nullptr;
    {
        PluginManager plugins;
        result = plugins.Initialize(settings);
        Check(SUCCEEDED(result) && plugins.ProviderCount() == 1 && plugins.WidgetCount() == 2 &&
                  plugins.GpuWidgetAt(0) != nullptr && plugins.GpuWidgetAt(1) != nullptr &&
                  plugins.ScheduledWidgetAt(0) != nullptr && plugins.ScheduledWidgetAt(1) != nullptr &&
                  plugins.WindowWidgetAt(0) == nullptr && plugins.WindowWidgetAt(1) == nullptr,
              L"two Process Viewers share one widget provider as GPU scheduled widgets", success);
        if (FAILED(result))
        {
            return;
        }

        getDiagnostics = GetProcessViewerDiagnosticsFunction();
        Check(getDiagnostics != nullptr, L"Process Viewer diagnostics export resolves", success);
        if (!getDiagnostics)
        {
            return;
        }
        Check(getDiagnostics(nullptr) == E_POINTER, L"Process Viewer diagnostics rejects a null record", success);
        ProcessViewerTestDiagnostics shortDiagnostics{sizeof(uint32_t)};
        Check(getDiagnostics(&shortDiagnostics) == E_INVALIDARG, L"Process Viewer diagnostics rejects a short record",
              success);

        ProcessViewerTestDiagnostics diagnostics{};
        result = ReadProcessViewerDiagnostics(diagnostics);
        Check(SUCCEEDED(result) && diagnostics.liveProviderCount == 1 && diagnostics.liveWidgetCount == 2 &&
                  diagnostics.liveSubscriptionCount == 2 && diagnostics.configuredTopN == 10 &&
                  diagnostics.sampleCount == 0,
              L"both Process Viewer subscriptions are inactive until shown", success);

        DashboardHost dashboard;
        if (SUCCEEDED(result))
        {
            result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
        }
        Check(SUCCEEDED(result) && !dashboard.RequiresContinuousFrames(),
              L"hidden Process Viewer attaches without requesting GPU frames", success);
        if (SUCCEEDED(result))
        {
            result = dashboard.SetWidgetsVisible(true);
        }

        const ULONGLONG deadline = GetTickCount64() + 5000;
        while (SUCCEEDED(result) && GetTickCount64() < deadline)
        {
            window.PumpMessages();
            result = ReadProcessViewerDiagnostics(diagnostics);
            if (FAILED(result) || (diagnostics.sampleCount > 0 && diagnostics.lastPublishedRowCount > 0))
            {
                break;
            }
            (void)MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        Check(SUCCEEDED(result) && diagnostics.sampleCount > 0 && diagnostics.lastPublishedRowCount > 0 &&
                  diagnostics.lastPublishedRowCount <= diagnostics.configuredTopN,
              L"visible Process Viewers receive and bound a shared live process snapshot", success);

        if (SUCCEEDED(result))
        {
            result = dashboard.SetWidgetsVisible(false);
        }
        if (SUCCEEDED(result))
        {
            result = ReadProcessViewerDiagnostics(diagnostics);
        }
        const uint32_t quiescedSampleCount = diagnostics.sampleCount;
        std::this_thread::sleep_for(std::chrono::milliseconds(2200));
        if (SUCCEEDED(result))
        {
            result = ReadProcessViewerDiagnostics(diagnostics);
        }
        Check(SUCCEEDED(result) && diagnostics.sampleCount == quiescedSampleCount,
              L"hiding Process Viewers synchronously quiesces both subscriptions", success);
        dashboard.Shutdown();
    }

    ProcessViewerTestDiagnostics finalDiagnostics{};
    const HRESULT finalResult = getDiagnostics ? ReadProcessViewerDiagnostics(finalDiagnostics) : E_UNEXPECTED;
    Check(SUCCEEDED(finalResult) && finalDiagnostics.liveProviderCount == 0 && finalDiagnostics.liveWidgetCount == 0 &&
              finalDiagnostics.liveSubscriptionCount == 0,
          L"Process Viewer teardown releases provider, widget, and subscription objects", success);
}

void TestSystemDataViewers(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] System Data GPU viewer family\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":4},"pages":[{"name":"System","layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":3,"arrangeAlong":"short-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.system-pulse"}},{"sizeRatio":1,"widget":{"plugin":"builtin.cpu-meter"}},{"sizeRatio":1,"widget":{"plugin":"builtin.memory-meter"}}]},{"sizeRatio":4,"arrangeAlong":"short-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.process-viewer"}},{"sizeRatio":1,"widget":{"plugin":"builtin.gpu-processes"}}]},{"sizeRatio":3,"arrangeAlong":"short-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.network-meter"}},{"sizeRatio":1,"widget":{"plugin":"builtin.storage-meter"}}]},{"sizeRatio":3,"arrangeAlong":"short-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.gpu-meter"}},{"sizeRatio":1,"widget":{"plugin":"builtin.thermal-meter"}},{"sizeRatio":1,"widget":{"plugin":"builtin.power-meter"}}]}]}}]})json";

    AttachedHostWindow window;
    HRESULT result = window.Initialize(kHostWidth, kHostHeight);
    AppSettings settings{};
    if (SUCCEEDED(result))
    {
        result = ParseAppSettingsJson(settingsJson, settings);
    }
    PluginManager plugins;
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(settings);
    }
    Check(SUCCEEDED(result) && plugins.ProviderCount() == 10 && plugins.WidgetCount() == 10,
          L"System page creates ten GPU viewer providers", success);
    if (FAILED(result))
    {
        return;
    }
    bool allGpu = true;
    bool anyWindow = false;
    for (size_t index = 0; index < plugins.WidgetCount(); ++index)
    {
        allGpu = allGpu && plugins.GpuWidgetAt(index) != nullptr && plugins.ScheduledWidgetAt(index) != nullptr;
        anyWindow = anyWindow || plugins.WindowWidgetAt(index) != nullptr;
    }
    Check(allGpu && !anyWindow, L"every System Data viewer exposes GPU and scheduled interfaces", success);

    DashboardHost dashboard;
    result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    Check(SUCCEEDED(result) && !dashboard.RequiresContinuousFrames(),
          L"System page attaches without continuous animation", success);
    if (FAILED(result))
    {
        return;
    }

    Renderer renderer;
    result = renderer.Initialize(window.Get(), true, dashboard);
    Check(SUCCEEDED(result), L"System page initializes a hidden WARP swap chain", success);
    if (FAILED(result))
    {
        dashboard.Shutdown();
        return;
    }

    result = dashboard.SetWidgetsVisible(true);
    ProcessViewerTestDiagnostics diagnostics{};
    const ULONGLONG deadline = GetTickCount64() + 5000;
    while (SUCCEEDED(result) && GetTickCount64() < deadline)
    {
        window.PumpMessages();
        result = ReadProcessViewerDiagnostics(diagnostics);
        if (FAILED(result) || diagnostics.sampleCount > 0)
        {
            break;
        }
        (void)MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    Check(SUCCEEDED(result) && diagnostics.sampleCount > 0 && diagnostics.liveSubscriptionCount >= 10,
          L"visible System page delivers snapshots on shared ProcessViewer subscriptions", success);

    LARGE_INTEGER frequency{};
    LARGE_INTEGER start{};
    LARGE_INTEGER stop{};
    QueryPerformanceFrequency(&frequency);
    for (uint32_t frame = 0; frame < 8; ++frame)
    {
        result = renderer.Render(static_cast<float>(frame) / 60.0f, 1.0f / 60.0f);
        if (FAILED(result))
        {
            break;
        }
    }
    Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 10,
          L"WARP renders every System Data GPU widget", success);

    renderer.Shutdown();
    if (SUCCEEDED(result))
    {
        result = renderer.Initialize(window.Get(), true, dashboard);
    }
    if (SUCCEEDED(result))
    {
        result = renderer.Render(0.25f, 1.0f / 60.0f);
    }
    Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 10,
          L"System Data widgets rebuild after device loss", success);

    QueryPerformanceCounter(&start);
    constexpr uint32_t kMeasuredFrames = 32;
    for (uint32_t frame = 0; frame < kMeasuredFrames; ++frame)
    {
        result = renderer.Render(0.5f + static_cast<float>(frame) / 60.0f, 1.0f / 60.0f);
        if (FAILED(result))
        {
            break;
        }
    }
    QueryPerformanceCounter(&stop);
    const double meanMs =
        (static_cast<double>(stop.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart)) /
        static_cast<double>(kMeasuredFrames);
    Check(SUCCEEDED(result) && meanMs < 50.0, L"System-page WARP CPU submit stays interactive", success);
    std::wcout << L"[       -- ] System page WARP mean frame " << meanMs << L" ms\n";

    uint32_t delay = 0;
    Check(dashboard.GetNextFrameDelayMilliseconds(&delay) == S_OK && delay > 1U,
          L"settled System page publishes a rest interval rather than a 1 ms ease poll", success);

    if (SUCCEEDED(result))
    {
        result = dashboard.SetWidgetsVisible(false);
    }
    if (SUCCEEDED(result))
    {
        result = ReadProcessViewerDiagnostics(diagnostics);
    }
    const uint32_t quiescedSampleCount = diagnostics.sampleCount;
    std::this_thread::sleep_for(std::chrono::milliseconds(2200));
    if (SUCCEEDED(result))
    {
        result = ReadProcessViewerDiagnostics(diagnostics);
    }
    Check(SUCCEEDED(result) && diagnostics.sampleCount == quiescedSampleCount,
          L"hiding the System page drains every data subscription", success);

    renderer.Shutdown();
    dashboard.Shutdown();
}

void TestDebugHostComposition(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] Debug multi-plugin production host integration\n";
    AttachedHostWindow window;
    HRESULT result = window.Initialize(kHostWidth, kHostHeight);
    Check(SUCCEEDED(result), L"Debug hidden host window initializes", success);
    if (FAILED(result))
    {
        return;
    }

    PluginManager plugins;
    AppSettings settings{};
    result = LoadDeployedSettings(kRedXeDebugSettingsFileName, settings);
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(settings);
    }
    Check(SUCCEEDED(result) && plugins.ProviderCount() == 3 && plugins.WidgetCount() == 4,
          L"Debug composition shares one normalized provider across identical triangle instances", success);
    if (FAILED(result))
    {
        return;
    }

    DashboardHost dashboard;
    result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    Check(SUCCEEDED(result), L"Debug dashboard attaches the native and GPU widgets while hidden", success);
    Check(dashboard.WindowWidgetAt(2) != nullptr && dashboard.GpuWidgetAt(0) != nullptr &&
              dashboard.GpuWidgetAt(1) != nullptr && dashboard.GpuWidgetAt(3) != nullptr,
          L"Debug dashboard exposes two triangles, GDI Orbit, and Matrix mechanisms", success);
    Check(dashboard.PlacementAt(0) == WidgetPlacement{0.0f, 0.0f, 640.0f, 720.0f} &&
              dashboard.PlacementAt(1) == WidgetPlacement{640.0f, 0.0f, 640.0f, 360.0f} &&
              dashboard.PlacementAt(2) == WidgetPlacement{640.0f, 360.0f, 640.0f, 360.0f} &&
              dashboard.PlacementAt(3) == WidgetPlacement{1280.0f, 0.0f, 1280.0f, 720.0f},
          L"Debug dashboard compiles the nested adaptive layout onto the design canvas", success);
    HWND gdiContainer = GetWindow(window.Get(), GW_CHILD);
    RECT gdiBounds{};
    if (gdiContainer && GetWindowRect(gdiContainer, &gdiBounds))
    {
        MapWindowPoints(HWND_DESKTOP, window.Get(), reinterpret_cast<POINT*>(&gdiBounds), 2);
    }
    Check(gdiContainer && gdiBounds.left == 640 && gdiBounds.top == 360 && gdiBounds.right == 1280 &&
              gdiBounds.bottom == 720,
          L"native GDI container uses the same adaptive bounds", success);
    if (FAILED(result))
    {
        return;
    }

    result = dashboard.SetWidgetsVisible(true);
    if (SUCCEEDED(result))
    {
        result = dashboard.SetWidgetsVisible(false);
    }
    Check(SUCCEEDED(result), L"production host propagates native-widget resume and quiesce transitions", success);

    Renderer renderer;
    result = renderer.Initialize(window.Get(), true, dashboard);
    if (SUCCEEDED(result))
    {
        result = renderer.Render(0.5f, 1.0f / 60.0f);
    }
    Check(SUCCEEDED(result) && renderer.LastFrameWidgetCount() == 3 && renderer.LastFrameSuccessfulWidgetCount() == 3,
          L"Debug WARP frame renders every GPU widget beside the attached GDI widget", success);

    renderer.Shutdown();
    dashboard.Shutdown();
    window.PumpMessages();
}

void TestGpuTargetSizeNotification(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] GPU widget target-size notification\n";
    // Desk Clock is the widget whose resources depend on how highTier it is drawn: its glyph atlas is rasterized at a
    // resolution tier chosen from the reported target size. That makes it the honest end-to-end probe for the
    // callback -- a no-op implementation would leave the tier stuck at 1.
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[)json"
        R"json({"sizeRatio":1,"widget":{"plugin":"builtin.desk-clock"}}]}}]})json";

    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(settingsJson, settings);
    AttachedHostWindow window;
    if (SUCCEEDED(result))
    {
        result = window.Initialize(kHostWidth, kHostHeight);
    }
    PluginManager plugins;
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(settings);
    }
    DashboardHost dashboard;
    if (SUCCEEDED(result))
    {
        result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), true);
    }
    Renderer renderer;
    if (SUCCEEDED(result))
    {
        result = renderer.Initialize(window.Get(), true, dashboard);
    }
    Check(SUCCEEDED(result) && plugins.WidgetCount() == 1, L"a Desk Clock page is composed for the size probe",
          success);
    if (FAILED(result))
    {
        return;
    }

    const DeskClockGetTestDiagnosticsFn getDiagnostics = ResolveFunction<DeskClockGetTestDiagnosticsFn>(
        GetModuleHandleW(L"DeskClock.dll"), kDeskClockGetTestDiagnosticsExport);
    Check(getDiagnostics != nullptr, L"Desk Clock diagnostics are reachable", success);
    if (!getDiagnostics)
    {
        return;
    }
    const auto read = [&](DeskClockTestDiagnostics& out) noexcept
    {
        out = DeskClockTestDiagnostics{};
        out.sizeBytes = sizeof(out);
        return SUCCEEDED(getDiagnostics(&out));
    };

    DeskClockTestDiagnostics afterCreate{};
    Check(read(afterCreate) && afterCreate.targetSizeChanges >= 1,
          L"the host reports a target size once device resources exist", success);

    // A baseTier viewport keeps the atlas on the base tier.
    result = renderer.Resize(720, 300);
    DeskClockTestDiagnostics baseTier{};
    Check(SUCCEEDED(result) && read(baseTier) && baseTier.atlasScale == 1 && baseTier.atlasEdgePixels == 1024,
          L"a viewport below the design size keeps the base atlas tier", success);

    // Frames must not re-report: the size did not change.
    const uint64_t beforeFrames = baseTier.targetSizeChanges;
    for (uint32_t frame = 0; frame < 8; ++frame)
    {
        result = renderer.Render(static_cast<float>(frame) / 60.0f, 1.0f / 60.0f);
        if (FAILED(result))
        {
            break;
        }
    }
    DeskClockTestDiagnostics afterFrames{};
    Check(SUCCEEDED(result) && read(afterFrames) && afterFrames.targetSizeChanges == beforeFrames,
          L"rendering frames at an unchanged size reports nothing", success);

    // A viewport well above the design size must move the atlas up a tier, which is the whole point of the callback.
    result = renderer.Resize(3840, 1080);
    DeskClockTestDiagnostics highTier{};
    Check(SUCCEEDED(result) && read(highTier) && highTier.atlasScale == 2 && highTier.atlasEdgePixels == 2048,
          L"a viewport above the design size rasterizes the atlas at a higher tier", success);
    Check(highTier.targetSizeChanges > beforeFrames, L"growing the viewport reports a new target size", success);
    Check(highTier.atlasBytes > baseTier.atlasBytes && highTier.atlasBytes < 6U * 1024U * 1024U,
          L"the higher tier costs more coverage memory and stays inside its bound", success);

    // Coming back down returns to the base tier rather than holding the larger atlas.
    result = renderer.Resize(720, 300);
    DeskClockTestDiagnostics back{};
    Check(SUCCEEDED(result) && read(back) && back.atlasScale == 1 && back.atlasBytes == baseTier.atlasBytes,
          L"shrinking the viewport releases the higher tier", success);

    // The widget still draws correctly at both tiers.
    result = renderer.Resize(3840, 1080);
    if (SUCCEEDED(result))
    {
        result = renderer.Render(1.0f, 1.0f / 60.0f);
    }
    Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 1,
          L"the widget renders after a tier change", success);
}

void TestSharedPluginRuntime(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] one process plugin runtime across current and staged pages\n";
    // Two pages that both place a System Data viewer. Before the runtime became process scoped, staging the adjacent
    // page built a second PluginHost with its own module map, its own IRedXeDataSource, and its own acquisition
    // thread beside the current one.
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":4},"pages":[)json"
        R"json({"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.cpu-meter"}}]}},)json"
        R"json({"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.memory-meter"}}]}}]})json";

    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(settingsJson, settings);
    AttachedHostWindow window;
    if (SUCCEEDED(result))
    {
        result = window.Initialize(kHostWidth, kHostHeight);
    }
    Check(SUCCEEDED(result), L"shared-runtime settings and host window are ready", success);
    if (FAILED(result))
    {
        return;
    }

    const DWORD threadsBefore = CountProcessThreads();

    PluginManager currentPlugins;
    result = currentPlugins.Initialize(settings);
    DashboardHost currentDashboard;
    if (SUCCEEDED(result))
    {
        result = currentDashboard.Initialize(currentPlugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), true);
    }
    Check(SUCCEEDED(result) && currentPlugins.WidgetCount() == 1, L"current page creates its System Data viewer",
          success);
    if (FAILED(result))
    {
        return;
    }

    // The current page's viewer subscribes, so the shared acquisition worker exists from here on.
    const DWORD threadsWithCurrent = CountProcessThreads();

    AppSettings stagedSettings = settings;
    result = MoveDashboardPage(stagedSettings, 1);
    PluginManager stagedPlugins;
    if (SUCCEEDED(result))
    {
        result = stagedPlugins.Initialize(stagedSettings);
    }
    DashboardHost stagedDashboard;
    if (SUCCEEDED(result))
    {
        result = stagedDashboard.Initialize(stagedPlugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), true);
    }
    Check(SUCCEEDED(result) && stagedPlugins.WidgetCount() == 1, L"staged adjacent page creates its System Data viewer",
          success);
    if (FAILED(result))
    {
        return;
    }

    const DWORD threadsWithStaged = CountProcessThreads();
    Check(threadsWithStaged <= threadsWithCurrent, L"staging an adjacent page adds no second acquisition thread",
          success);
    Check(threadsWithCurrent >= threadsBefore, L"the shared acquisition worker starts with the first subscription",
          success);

    // Both managers resolve the same data provider identity from the one runtime.
    wil::com_ptr_nothrow<IRedXeDataProvider> first;
    wil::com_ptr_nothrow<IRedXeDataProvider> second;
    const HRESULT firstResult = PluginHost::Instance().Interface()->GetDataProvider("builtin.system-data", first.put());
    const HRESULT secondResult =
        PluginHost::Instance().Interface()->GetDataProvider("builtin.system-data", second.put());
    wil::com_ptr_nothrow<IUnknown> firstIdentity;
    wil::com_ptr_nothrow<IUnknown> secondIdentity;
    if (SUCCEEDED(firstResult) && SUCCEEDED(secondResult))
    {
        (void)first.query_to(firstIdentity.put());
        (void)second.query_to(secondIdentity.put());
    }
    Check(SUCCEEDED(firstResult) && SUCCEEDED(secondResult) && firstIdentity && firstIdentity == secondIdentity,
          L"repeated provider lookup returns one shared controlling identity", success);
}

void TestHostRequestFrameAndWidgetStatus(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] host frame request and widget status reporting\n";
    IRedXeHost* host = PluginHost::Instance().Interface();

    // RequestFrame is safe with no UI target: it coalesces and does nothing rather than failing.
    Check(host->RequestFrame() == S_OK && host->RequestFrame() == S_OK,
          L"RequestFrame succeeds and coalesces without a UI target", success);

    RedXeWidgetStatusReport report{sizeof(RedXeWidgetStatusReport), RedXeWidgetStatusUnavailable,
                                   L"sensor unavailable"};
    Check(host->ReportWidgetStatus("status.test.instance", &report) == S_OK, L"a widget can report itself unavailable",
          success);
    Check(PluginHost::Instance().WidgetStatus("status.test.instance") == RedXeWidgetStatusUnavailable,
          L"the host records the reported status", success);

    std::array<wchar_t, 64> reason{};
    Check(PluginHost::Instance().WidgetStatusReason("status.test.instance", reason.data(), reason.size()) &&
              std::wcscmp(reason.data(), L"sensor unavailable") == 0,
          L"the host copies the reported reason into bounded storage", success);

    // Recovery clears the placeholder condition.
    report.status = RedXeWidgetStatusOk;
    report.reason = nullptr;
    Check(host->ReportWidgetStatus("status.test.instance", &report) == S_OK &&
              PluginHost::Instance().WidgetStatus("status.test.instance") == RedXeWidgetStatusOk,
          L"a recovered widget clears its unavailable status", success);

    // An instance that never reported is healthy, and a cleared instance returns to healthy.
    Check(PluginHost::Instance().WidgetStatus("status.test.never-reported") == RedXeWidgetStatusOk,
          L"an unreported instance reads back as healthy", success);
    PluginHost::Instance().ClearWidgetStatus("status.test.instance");
    Check(PluginHost::Instance().WidgetStatus("status.test.instance") == RedXeWidgetStatusOk,
          L"clearing an instance status returns it to healthy", success);

    // Malformed reports are rejected rather than recorded.
    RedXeWidgetStatusReport malformed{sizeof(RedXeWidgetStatusReport) + 4, RedXeWidgetStatusOk, nullptr};
    Check(host->ReportWidgetStatus("status.test.instance", &malformed) == E_INVALIDARG,
          L"a stale-sized status record is rejected", success);
    RedXeWidgetStatusReport outOfRange{sizeof(RedXeWidgetStatusReport), RedXeWidgetStatusUnavailable + 1, nullptr};
    Check(host->ReportWidgetStatus("status.test.instance", &outOfRange) == E_INVALIDARG,
          L"an unknown status value is rejected", success);
    Check(host->ReportWidgetStatus(nullptr, &report) == E_INVALIDARG &&
              host->ReportWidgetStatus("status.test.instance", nullptr) == E_INVALIDARG,
          L"null status arguments are rejected", success);
}

void TestHostOwnedPlaceholderTiles(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] host-owned placeholder tiles\n";
    // Note on coverage: PluginManager's construction-failure branch is defensive. ValidateAppSettings rejects the
    // documents that would provoke a bundled plugin into refusing an instance, so no valid document can reach it;
    // it exists for runtime failures such as exhausted memory or subscription slots. What is reachable, and what is
    // covered here, is that a valid page produces no placeholders and that a constructed widget which reports itself
    // unavailable hands its tile to the host.
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[)json"
        R"json({"sizeRatio":1,"widget":{"plugin":"builtin.rotating-triangle"}},)json"
        R"json({"sizeRatio":1,"widget":{"plugin":"builtin.cpu-meter"}},)json"
        R"json({"sizeRatio":1,"widget":{"plugin":"builtin.memory-meter"}}]}}]})json";

    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(settingsJson, settings);
    AttachedHostWindow window;
    if (SUCCEEDED(result))
    {
        result = window.Initialize(kHostWidth, kHostHeight);
    }
    PluginManager plugins;
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(settings);
    }
    Check(SUCCEEDED(result) && plugins.WidgetCount() == 3, L"a valid page constructs every authored instance", success);
    if (FAILED(result))
    {
        return;
    }

    bool anyPlaceholder = false;
    bool anyFailure = false;
    for (size_t index = 0; index < plugins.WidgetCount(); ++index)
    {
        anyPlaceholder = anyPlaceholder || plugins.IsPlaceholderAt(index);
        anyFailure = anyFailure || FAILED(plugins.PlaceholderFailureAt(index));
    }
    Check(!anyPlaceholder && !anyFailure, L"a valid page produces no placeholder tiles", success);

    DashboardHost dashboard;
    result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), true);
    Renderer renderer;
    if (SUCCEEDED(result))
    {
        result = renderer.Initialize(window.Get(), true, dashboard);
    }
    if (SUCCEEDED(result))
    {
        result = renderer.Render(0.0f, 1.0f / 60.0f);
    }
    Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 3,
          L"every constructed widget draws before any status is reported", success);
    if (FAILED(result))
    {
        return;
    }

    // A constructed widget that reports itself unavailable hands its tile to the host.
    const char* unavailableId = plugins.WidgetInstanceIdAt(1);
    RedXeWidgetStatusReport report{sizeof(RedXeWidgetStatusReport), RedXeWidgetStatusUnavailable, L"sensor offline"};
    const HRESULT reported =
        unavailableId ? PluginHost::Instance().Interface()->ReportWidgetStatus(unavailableId, &report) : E_FAIL;
    Check(SUCCEEDED(reported) && dashboard.RequiresPlaceholderAt(1) && !dashboard.RequiresPlaceholderAt(0) &&
              !dashboard.RequiresPlaceholderAt(2),
          L"the host owns exactly the tile whose widget reported unavailable", success);

    result = renderer.Render(0.1f, 1.0f / 60.0f);
    Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 2,
          L"the frame draws the healthy widgets and the host draws the failed tile", success);

    // Degraded and initializing are reported states, not host-owned tiles: the widget keeps drawing.
    report.status = RedXeWidgetStatusDegraded;
    Check(unavailableId && SUCCEEDED(PluginHost::Instance().Interface()->ReportWidgetStatus(unavailableId, &report)) &&
              !dashboard.RequiresPlaceholderAt(1),
          L"a degraded widget keeps its own tile", success);

    report.status = RedXeWidgetStatusOk;
    report.reason = nullptr;
    Check(unavailableId && SUCCEEDED(PluginHost::Instance().Interface()->ReportWidgetStatus(unavailableId, &report)) &&
              !dashboard.RequiresPlaceholderAt(1),
          L"a recovered widget takes its tile back", success);
    result = renderer.Render(0.2f, 1.0f / 60.0f);
    Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 3,
          L"every widget draws again after recovery", success);
}

void TestPageEdgeAffordancePolicy(bool& success) noexcept
{
    // A live multi-page dashboard in the middle of the document offers both directions.
    PageEdgeState middle{};
    middle.rendererReady = true;
    middle.windowVisible = true;
    middle.displayPoweredOn = true;
    middle.rendererSuspended = false;
    middle.rendererOccluded = false;
    middle.wrapPages = false;
    middle.pageCount = 3;
    middle.atFirstPage = false;
    middle.atLastPage = false;
    Check(ShouldShowEdgeAffordance(middle, kPageEdgeDirectionPrevious) &&
              ShouldShowEdgeAffordance(middle, kPageEdgeDirectionNext),
          L"A middle page offers both edge affordances.", success);

    // Non-wrapping ends drop the band for the blocked direction only.
    PageEdgeState first = middle;
    first.atFirstPage = true;
    Check(!ShouldShowEdgeAffordance(first, kPageEdgeDirectionPrevious) &&
              ShouldShowEdgeAffordance(first, kPageEdgeDirectionNext),
          L"The first page drops only the previous-page band.", success);

    PageEdgeState last = middle;
    last.atLastPage = true;
    Check(ShouldShowEdgeAffordance(last, kPageEdgeDirectionPrevious) &&
              !ShouldShowEdgeAffordance(last, kPageEdgeDirectionNext),
          L"The last page drops only the next-page band.", success);

    // Wrapping keeps both ends navigable.
    PageEdgeState wrapped = middle;
    wrapped.wrapPages = true;
    wrapped.atFirstPage = true;
    wrapped.atLastPage = false;
    Check(ShouldShowEdgeAffordance(wrapped, kPageEdgeDirectionPrevious) &&
              ShouldShowEdgeAffordance(wrapped, kPageEdgeDirectionNext),
          L"Wrapping keeps both edge affordances at a document end.", success);

    // A single-page document has nothing to navigate to.
    PageEdgeState single = middle;
    single.pageCount = 1;
    single.atFirstPage = true;
    single.atLastPage = true;
    Check(!ShouldShowEdgeAffordance(single, kPageEdgeDirectionPrevious) &&
              !ShouldShowEdgeAffordance(single, kPageEdgeDirectionNext),
          L"A single-page document offers no edge affordance.", success);

    // Every suppressed host state removes both bands.
    const auto suppressed = [&](PageEdgeState state, const wchar_t* label) noexcept
    {
        Check(!ShouldShowEdgeAffordance(state, kPageEdgeDirectionPrevious) &&
                  !ShouldShowEdgeAffordance(state, kPageEdgeDirectionNext),
              label, success);
    };
    PageEdgeState blocked = middle;
    blocked.rendererReady = false;
    suppressed(blocked, L"A dashboard without a renderer offers no edge affordance.");
    blocked = middle;
    blocked.windowVisible = false;
    suppressed(blocked, L"A hidden window offers no edge affordance.");
    blocked = middle;
    blocked.displayPoweredOn = false;
    suppressed(blocked, L"A powered-off display offers no edge affordance.");
    blocked = middle;
    blocked.rendererSuspended = true;
    suppressed(blocked, L"A suspended renderer offers no edge affordance.");
    blocked = middle;
    blocked.rendererOccluded = true;
    suppressed(blocked, L"An occluded renderer offers no edge affordance.");
    blocked = middle;
    blocked.widgetRaised = true;
    suppressed(blocked, L"A raised widget offers no edge affordance.");
    blocked = middle;
    blocked.pointerNavigationActive = true;
    suppressed(blocked, L"An active pointer pan offers no edge affordance.");
    blocked = middle;
    blocked.settleActive = true;
    suppressed(blocked, L"An active settle offers no edge affordance.");
    blocked = middle;
    blocked.transitionStaged = true;
    suppressed(blocked, L"A staged neighbor offers no edge affordance.");

    Check(!ShouldShowEdgeAffordance(middle, 0) && !ShouldShowEdgeAffordance(middle, 2),
          L"Only the two page-navigation directions select an edge affordance.", success);
}

void TestPageEdgeAffordanceGeometry(bool& success) noexcept
{
    constexpr UINT clientWidth = 2560;
    constexpr UINT clientHeight = 720;

    const RECT left = PageEdgeBandRect(kPageEdgeDirectionPrevious, clientWidth, clientHeight, 96);
    const RECT right = PageEdgeBandRect(kPageEdgeDirectionNext, clientWidth, clientHeight, 96);
    Check(left.left == 0 && left.top == 0 && left.right == kPageEdgeBandWidthDips && left.bottom == 720,
          L"The previous-page band hugs the left edge at full client height.", success);
    Check(right.right == static_cast<LONG>(clientWidth) && right.top == 0 && right.bottom == 720 &&
              right.left == static_cast<LONG>(clientWidth) - kPageEdgeBandWidthDips,
          L"The next-page band hugs the right edge at full client height.", success);
    Check(left.right < right.left, L"The two bands never meet on a wide client.", success);

    // The band scales with DPI.
    const RECT scaled = PageEdgeBandRect(kPageEdgeDirectionPrevious, clientWidth, clientHeight, 192);
    Check(scaled.right == kPageEdgeBandWidthDips * 2, L"Band width scales with the destination-monitor DPI.", success);

    // A narrow client keeps a usable centre instead of letting the bands meet.
    const RECT narrowLeft = PageEdgeBandRect(kPageEdgeDirectionPrevious, 90, 200, 96);
    const RECT narrowRight = PageEdgeBandRect(kPageEdgeDirectionNext, 90, 200, 96);
    Check(narrowLeft.right == 30 && narrowRight.left == 60 && narrowLeft.right < narrowRight.left,
          L"A narrow client clamps each band to a third of the client width.", success);

    // Degenerate inputs produce an empty band rather than a bad rectangle.
    const RECT empty = PageEdgeBandRect(kPageEdgeDirectionNext, 0, 0, 96);
    Check(empty.right == empty.left && empty.bottom == empty.top, L"A zero client produces no band.", success);
    const RECT invalid = PageEdgeBandRect(0, clientWidth, clientHeight, 96);
    Check(invalid.right == invalid.left, L"An invalid direction produces no band.", success);

    // Hit testing is inclusive at the near edge and exclusive at the far edge.
    Check(PageEdgeBandContains(left, POINT{0, 0}) && PageEdgeBandContains(left, POINT{left.right - 1, 719}) &&
              !PageEdgeBandContains(left, POINT{left.right, 10}) && !PageEdgeBandContains(left, POINT{5, 720}),
          L"Band hit testing is inclusive at the near edge and exclusive at the far edge.", success);

    // The chevron is a Segoe Fluent Icons glyph, with a Unicode stand-in when no icon font is installed.
    Check(PageEdgeChevronGlyph(kPageEdgeDirectionPrevious, FluentIcons::IconFont::Fluent) ==
                  FluentIcons::kChevronLeft &&
              PageEdgeChevronGlyph(kPageEdgeDirectionNext, FluentIcons::IconFont::Fluent) == FluentIcons::kChevronRight,
          L"The chevron uses the Fluent chevron glyph for each travel direction.", success);
    Check(PageEdgeChevronGlyph(kPageEdgeDirectionPrevious, FluentIcons::IconFont::Legacy) ==
                  FluentIcons::kChevronLeft &&
              PageEdgeChevronGlyph(kPageEdgeDirectionNext, FluentIcons::IconFont::Legacy) == FluentIcons::kChevronRight,
          L"Segoe MDL2 Assets shares the Fluent chevron code points.", success);
    Check(PageEdgeChevronGlyph(kPageEdgeDirectionPrevious, FluentIcons::IconFont::TextFallback) ==
                  FluentIcons::kFallbackChevronLeft &&
              PageEdgeChevronGlyph(kPageEdgeDirectionNext, FluentIcons::IconFont::TextFallback) ==
                  FluentIcons::kFallbackChevronRight,
          L"Without an icon font the chevron falls back to a standard Unicode glyph.", success);
    Check(PageEdgeChevronGlyph(0, FluentIcons::IconFont::Fluent) == L'\0',
          L"An invalid direction produces no chevron glyph.", success);

    // The glyph cell is centred in its band and never escapes it.
    const RECT cell = PageEdgeChevronCell(right, 96);
    Check(cell.left >= right.left && cell.right <= right.right && cell.top >= right.top && cell.bottom <= right.bottom,
          L"The chevron cell stays inside its band.", success);
    Check((cell.left + cell.right) / 2 == (right.left + right.right) / 2 &&
              (cell.top + cell.bottom) / 2 == (right.top + right.bottom) / 2,
          L"The chevron cell is centred in its band.", success);
    Check(PageEdgeChevronPixelHeight(192) == PageEdgeChevronPixelHeight(96) * 2,
          L"The chevron scales with the destination-monitor DPI.", success);
    const RECT degenerateCell = PageEdgeChevronCell(RECT{}, 96);
    Check(degenerateCell.right == degenerateCell.left, L"An empty band produces no chevron cell.", success);

    // Multi-display reach. One work area covering the whole client leaves it unchanged.
    const RECT wholeClientRect{0, 0, static_cast<LONG>(clientWidth), static_cast<LONG>(clientHeight)};
    const std::array oneBigDisplay{RECT{-100, -100, 4000, 2000}};
    const RECT unclipped = PageEdgeReachableClient(wholeClientRect, oneBigDisplay.data(), oneBigDisplay.size());
    Check(EqualRect(&unclipped, &wholeClientRect), L"A client inside one work area is entirely reachable.", success);

    // A single display narrower than the client clips it, which is the case that hid the band off-screen.
    const std::array oneSmallDisplay{RECT{0, 0, 1280, 1000}};
    const RECT clippedReach = PageEdgeReachableClient(wholeClientRect, oneSmallDisplay.data(), oneSmallDisplay.size());
    Check(clippedReach.right == 1280 && clippedReach.left == 0,
          L"A client wider than its single display is reachable only to the display edge.", success);

    // Two side-by-side displays: a window straddling them is reachable across both, not clamped to one.
    const std::array twoDisplays{RECT{-1280, 0, 0, 1000}, RECT{0, 0, 1280, 1000}};
    const RECT straddling{-600, 0, 1000, static_cast<LONG>(clientHeight)};
    const RECT straddleReach = PageEdgeReachableClient(straddling, twoDisplays.data(), twoDisplays.size());
    Check(straddleReach.left == -600 && straddleReach.right == 1000,
          L"A window straddling two displays stays reachable across both.", success);
    const RECT straddleLeft = PageEdgeBandRectIn(straddleReach, kPageEdgeDirectionPrevious, 96);
    const RECT straddleRight = PageEdgeBandRectIn(straddleReach, kPageEdgeDirectionNext, 96);
    Check(straddleLeft.left == -600 && straddleRight.right == 1000,
          L"Straddling bands sit at the outer edges, not at the monitor seam.", success);

    // Degenerate inputs fall back to the client rather than removing every band.
    const RECT noAreas = PageEdgeReachableClient(wholeClientRect, nullptr, 0);
    Check(EqualRect(&noAreas, &wholeClientRect), L"Absent display information leaves the whole client reachable.",
          success);
    const std::array offscreenDisplay{RECT{10000, 10000, 12000, 12000}};
    const RECT disjoint = PageEdgeReachableClient(wholeClientRect, offscreenDisplay.data(), offscreenDisplay.size());
    Check(EqualRect(&disjoint, &wholeClientRect),
          L"A client disjoint from every work area keeps its bands rather than losing them.", success);

    // A work area inset by a taskbar still yields a full-client-height band. Horizontal clipping stays so a window
    // wider than its monitor keeps a reachable edge.
    const std::array taskbarInset{RECT{0, 40, 1280, 680}};
    const RECT tallReach = PageEdgeReachableClient(wholeClientRect, taskbarInset.data(), taskbarInset.size());
    Check(tallReach.top == 0 && tallReach.bottom == static_cast<LONG>(clientHeight) && tallReach.right == 1280,
          L"A work-area taskbar does not shorten the band; width still clips to the display.", success);
    const RECT tallRight = PageEdgeBandRectIn(tallReach, kPageEdgeDirectionNext, 96);
    Check(tallRight.top == 0 && tallRight.bottom == static_cast<LONG>(clientHeight),
          L"The next-page band runs from the top of the client to the bottom.", success);

    // Bands follow the reachable client area, so a window larger than its monitor still offers a band the pointer
    // can hit. A client that fits entirely on screen is unchanged.
    const RECT wholeClient{0, 0, static_cast<LONG>(clientWidth), static_cast<LONG>(clientHeight)};
    const RECT wholeLeft = PageEdgeBandRectIn(wholeClient, kPageEdgeDirectionPrevious, 96);
    const RECT wholeRight = PageEdgeBandRectIn(wholeClient, kPageEdgeDirectionNext, 96);
    Check(EqualRect(&left, &wholeLeft) && EqualRect(&right, &wholeRight),
          L"A fully reachable client places bands exactly at the client edges.", success);

    // Half the client hangs off the right of the display.
    const RECT clipped{0, 0, 1280, static_cast<LONG>(clientHeight)};
    const RECT clippedRight = PageEdgeBandRectIn(clipped, kPageEdgeDirectionNext, 96);
    const RECT clippedLeft = PageEdgeBandRectIn(clipped, kPageEdgeDirectionPrevious, 96);
    Check(clippedRight.right == 1280 && clippedRight.left == 1280 - kPageEdgeBandWidthDips && clippedRight.top == 0 &&
              clippedRight.bottom == static_cast<LONG>(clientHeight),
          L"An off-screen client edge moves the next-page band to the reachable edge.", success);
    Check(clippedLeft.left == 0 && clippedLeft.right == kPageEdgeBandWidthDips,
          L"Clipping one side leaves the opposite band where it was.", success);

    // A reachable area offset from the client origin, as when the window hangs off the left of the display.
    const RECT offset{600, 40, 1880, 700};
    const RECT offsetLeft = PageEdgeBandRectIn(offset, kPageEdgeDirectionPrevious, 96);
    const RECT offsetRight = PageEdgeBandRectIn(offset, kPageEdgeDirectionNext, 96);
    Check(offsetLeft.left == 600 && offsetLeft.right == 600 + kPageEdgeBandWidthDips && offsetLeft.top == 40 &&
              offsetLeft.bottom == 700,
          L"A reachable area offset from the client origin places the previous-page band at its left edge.", success);
    Check(offsetRight.right == 1880 && offsetRight.left == 1880 - kPageEdgeBandWidthDips,
          L"A reachable area offset from the client origin places the next-page band at its right edge.", success);
    const RECT emptyReachable = PageEdgeBandRectIn(RECT{}, kPageEdgeDirectionNext, 96);
    Check(emptyReachable.right == emptyReachable.left, L"An empty reachable area produces no band.", success);

    // A click settles to the same offset a committed swipe in that direction reaches.
    Check(PageEdgeSettleTarget(kPageEdgeDirectionNext, 2560) == -2560 &&
              PageEdgeSettleTarget(kPageEdgeDirectionPrevious, 2560) == 2560,
          L"An edge click settles to the committed-swipe offset for its direction.", success);
    Check(PageSwipeDirection(PageEdgeSettleTarget(kPageEdgeDirectionNext, 2560)) == kPageEdgeDirectionNext &&
              PageSwipeDirection(PageEdgeSettleTarget(kPageEdgeDirectionPrevious, 2560)) == kPageEdgeDirectionPrevious,
          L"Edge settle targets agree with the swipe direction convention.", success);
    Check(PageEdgeSettleTarget(kPageEdgeDirectionNext, 0) == 0 && PageEdgeSettleTarget(0, 2560) == 0,
          L"A degenerate edge settle target is zero.", success);

    // A click starts from rest, so the settle uses the clamped upper bound of the shared duration policy.
    Check(PageSettleDurationMilliseconds(PageEdgeSettleTarget(kPageEdgeDirectionNext, 2560), 0.0f) == 280,
          L"An edge click settles with the shared ease-out at its clamped upper bound.", success);
}

void TestNonDivisibleGridEdges(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] non-divisible dashboard grid edges\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":2,"widget":{"plugin":"builtin.rotating-triangle"}},{"sizeRatio":1,"widget":{"plugin":"builtin.rotating-triangle"}}]}}]})json";

    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(settingsJson, settings);
    AttachedHostWindow window;
    if (SUCCEEDED(result))
    {
        result = window.Initialize(1001, 333);
    }
    PluginManager plugins;
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(settings);
    }
    DashboardHost dashboard;
    if (SUCCEEDED(result))
    {
        result = dashboard.Initialize(plugins, window.Get(), 1001, 333, window.Dpi(), false);
    }
    Check(SUCCEEDED(result), L"adaptive uneven-ratio dashboard initializes", success);
    if (FAILED(result))
    {
        return;
    }

    const RECT left = dashboard.PixelBoundsAt(0, 1001, 333);
    const RECT right = dashboard.PixelBoundsAt(1, 1001, 333);
    Check(left.left == 0 && left.right == right.left && right.right == 1001 && left.top == 0 && right.top == 0 &&
              left.bottom == 333 && right.bottom == 333,
          L"landscape long-side regions share one exact physical edge without gaps or overlap", success);

    const RECT portraitFirst = dashboard.PixelBoundsAt(0, 333, 1001);
    const RECT portraitSecond = dashboard.PixelBoundsAt(1, 333, 1001);
    Check(portraitFirst.left == 0 && portraitFirst.right == 333 && portraitSecond.left == 0 &&
              portraitSecond.right == 333 && portraitFirst.top == 0 && portraitFirst.bottom == portraitSecond.top &&
              portraitSecond.bottom == 1001,
          L"portrait reflows the same long-side split vertically without reparsing", success);

    Renderer renderer;
    result = renderer.Initialize(window.Get(), true, dashboard);
    if (SUCCEEDED(result))
    {
        result = renderer.Render(0.0f, 0.0f);
    }
    Check(SUCCEEDED(result) && renderer.LastFrameWidgetCount() == 2 && renderer.LastFrameSuccessfulWidgetCount() == 2,
          L"D3D viewports use the same adaptive physical edges", success);
    PluginManager adjacentPlugins;
    DashboardHost adjacentDashboard;
    if (SUCCEEDED(result))
        result = adjacentPlugins.Initialize(settings);
    if (SUCCEEDED(result))
        result = adjacentDashboard.Initialize(adjacentPlugins, window.Get(), 1001, 333, window.Dpi(), false);
    if (SUCCEEDED(result))
        result = adjacentDashboard.SetHorizontalOffset(1001 - 137);
    if (SUCCEEDED(result))
        result = renderer.SetTransitionDashboard(&adjacentDashboard);
    if (SUCCEEDED(result))
        result = dashboard.SetHorizontalOffset(-137);
    if (SUCCEEDED(result))
        result = renderer.RefreshLayout();
    if (SUCCEEDED(result))
        result = renderer.Render(0.1f, 0.0f);
    Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 4 && dashboard.HorizontalOffset() == -137,
          L"direct manipulation renders only the current and staged adjacent pages", success);
    (void)renderer.SetTransitionDashboard(nullptr);
    adjacentDashboard.Shutdown();
    (void)dashboard.SetHorizontalOffset(0);
    (void)renderer.RefreshLayout();
    renderer.Shutdown();
    dashboard.Shutdown();
}

void TestPromoteStagedDashboard(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] in-place swipe commit keeps the Direct3D device\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.rotating-triangle"}}]}},{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.rotating-triangle"}},{"sizeRatio":1,"widget":{"plugin":"builtin.rotating-triangle"}}]}}]})json";

    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(settingsJson, settings);
    AttachedHostWindow window;
    if (SUCCEEDED(result))
    {
        result = window.Initialize(kHostWidth, kHostHeight);
    }
    PluginManager currentPlugins;
    if (SUCCEEDED(result))
    {
        result = currentPlugins.Initialize(settings);
    }
    DashboardHost currentDashboard;
    if (SUCCEEDED(result))
    {
        result =
            currentDashboard.Initialize(currentPlugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    }
    Renderer renderer;
    if (SUCCEEDED(result))
    {
        result = renderer.Initialize(window.Get(), true, currentDashboard);
    }
    Check(SUCCEEDED(result) && currentPlugins.WidgetCount() == 1, L"first-page swipe host initializes one GPU widget",
          success);
    if (FAILED(result))
    {
        return;
    }

    AppSettings nextSettings = settings;
    PluginManager nextPlugins;
    DashboardHost nextDashboard;
    result = MoveDashboardPage(nextSettings, 1);
    if (SUCCEEDED(result))
    {
        result = nextPlugins.Initialize(nextSettings);
    }
    if (SUCCEEDED(result))
    {
        result = nextDashboard.Initialize(nextPlugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    }
    if (SUCCEEDED(result))
    {
        result = nextDashboard.SetHorizontalOffset(static_cast<LONG>(kHostWidth));
    }
    if (SUCCEEDED(result))
    {
        result = renderer.SetTransitionDashboard(&nextDashboard);
    }
    if (SUCCEEDED(result))
    {
        result = currentDashboard.SetHorizontalOffset(-static_cast<LONG>(kHostWidth) / 4);
    }
    if (SUCCEEDED(result))
    {
        result = renderer.RefreshLayout();
    }
    if (SUCCEEDED(result))
    {
        result = renderer.Render(0.2f, 0.0f);
    }
    Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 3,
          L"direct manipulation renders the current page and staged neighbor", success);

    result = renderer.AdoptPrimaryDashboard(nextDashboard);
    currentDashboard.Shutdown();
    if (SUCCEEDED(result))
    {
        result = nextDashboard.SetHorizontalOffset(0);
    }
    if (SUCCEEDED(result))
    {
        result = renderer.RefreshLayout();
    }
    if (SUCCEEDED(result))
    {
        result = renderer.Render(0.3f, 0.0f);
    }
    Check(SUCCEEDED(result) && renderer.LastFrameWidgetCount() == 2 && renderer.LastFrameSuccessfulWidgetCount() == 2,
          L"commit promotes the staged neighbor without recreating the device", success);

    renderer.Shutdown();
    nextDashboard.Shutdown();
}

void TestSwipeRendersPartiallyOffscreenGpuWidgets(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] swipe keeps drawing partially visible GPU widgets\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.matrix-rain"}}]}},{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.studio-clock"}},{"sizeRatio":1,"widget":{"plugin":"builtin.desk-clock"}}]}}]})json";

    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(settingsJson, settings);
    AttachedHostWindow window;
    if (SUCCEEDED(result))
    {
        result = window.Initialize(kHostWidth, kHostHeight);
    }

    AppSettings clockSettings = settings;
    if (SUCCEEDED(result))
    {
        result = MoveDashboardPage(clockSettings, 1);
    }
    PluginManager clockPlugins;
    DashboardHost clockDashboard;
    if (SUCCEEDED(result))
    {
        result = clockPlugins.Initialize(clockSettings);
    }
    if (SUCCEEDED(result))
    {
        result = clockDashboard.Initialize(clockPlugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    }
    Renderer renderer;
    if (SUCCEEDED(result))
    {
        result = renderer.Initialize(window.Get(), true, clockDashboard);
    }

    PluginManager matrixPlugins;
    DashboardHost matrixDashboard;
    if (SUCCEEDED(result))
    {
        result = matrixPlugins.Initialize(settings);
    }
    if (SUCCEEDED(result))
    {
        result = matrixDashboard.Initialize(matrixPlugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    }

    const LONG swipeRight = static_cast<LONG>(kHostWidth) / 4;
    if (SUCCEEDED(result))
    {
        result = clockDashboard.SetHorizontalOffset(swipeRight);
    }
    if (SUCCEEDED(result))
    {
        result = matrixDashboard.SetHorizontalOffset(swipeRight - static_cast<LONG>(kHostWidth));
    }
    if (SUCCEEDED(result))
    {
        result = renderer.SetTransitionDashboard(&matrixDashboard);
    }
    if (SUCCEEDED(result))
    {
        result = renderer.RefreshLayout();
    }
    if (FAILED(result))
    {
        Check(false, L"partial-visibility swipe host failed to initialize", success);
        renderer.Shutdown();
        matrixDashboard.Shutdown();
        clockDashboard.Shutdown();
        return;
    }

    const RECT incomingMatrix = matrixDashboard.PixelBoundsAt(0, kHostWidth, kHostHeight);
    const RECT leftClock = clockDashboard.PixelBoundsAt(0, kHostWidth, kHostHeight);
    const RECT rightClock = clockDashboard.PixelBoundsAt(1, kHostWidth, kHostHeight);
    Check(incomingMatrix.left < 0 && incomingMatrix.right > 0 && incomingMatrix.right < static_cast<LONG>(kHostWidth),
          L"a right-swipe places the previous Matrix page partly on-screen from the left", success);
    Check(leftClock.left > 0 && rightClock.right > static_cast<LONG>(kHostWidth),
          L"the current clocks keep their full width and slide partly past the right edge", success);

    result = renderer.Render(0.4f, 0.016f);
    Check(SUCCEEDED(result) && renderer.LastFrameWidgetCount() == 3 && renderer.LastFrameSuccessfulWidgetCount() == 3,
          L"Matrix Rain and both clocks still draw when they are not completely on-screen", success);

    (void)renderer.SetTransitionDashboard(nullptr);
    matrixDashboard.Shutdown();
    if (SUCCEEDED(result))
    {
        result = clockDashboard.SetHorizontalOffset(-swipeRight);
    }
    if (SUCCEEDED(result))
    {
        result = renderer.RefreshLayout();
    }
    const RECT slidingClock = clockDashboard.PixelBoundsAt(0, kHostWidth, kHostHeight);
    Check(slidingClock.left < 0 && slidingClock.right > 0, L"a left-swipe leaves the left clock only partly visible",
          success);
    if (SUCCEEDED(result))
    {
        result = renderer.Render(0.5f, 0.016f);
    }
    Check(SUCCEEDED(result) && renderer.LastFrameWidgetCount() == 2 && renderer.LastFrameSuccessfulWidgetCount() == 2,
          L"Studio Clock and Desk Clock still draw when their tiles are not completely on-screen", success);

    (void)clockDashboard.SetHorizontalOffset(0);
    (void)renderer.RefreshLayout();
    renderer.Shutdown();
    clockDashboard.Shutdown();
}

void TestSettingsReloadKeepsCurrentPage(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] live settings reload keeps the current page\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.rotating-triangle"}}]}},{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.gdi-orbit"}}]}}]})json";

    AppSettings current{};
    HRESULT result = ParseAppSettingsJson(settingsJson, current);
    Check(SUCCEEDED(result) && current.dashboard.activePageIndex == 0,
          L"a freshly parsed document starts on the first page", success);
    if (SUCCEEDED(result))
    {
        result = MoveDashboardPage(current, 1);
    }
    Check(SUCCEEDED(result) && current.dashboard.activePageIndex == 1, L"the runtime can leave the first page",
          success);

    AppSettings reloaded{};
    if (SUCCEEDED(result))
    {
        result = ParseAppSettingsJson(settingsJson, reloaded);
    }
    Check(SUCCEEDED(result) && reloaded.dashboard.activePageIndex == 0,
          L"a live parse still defaults to the first page", success);
    if (SUCCEEDED(result))
    {
        result = PreserveActiveDashboardPage(current, reloaded);
    }
    Check(SUCCEEDED(result) && reloaded.dashboard.activePageIndex == 1,
          L"reload keeps the second page when it still exists", success);

    PluginManager plugins;
    DashboardHost dashboard;
    AttachedHostWindow window;
    if (SUCCEEDED(result))
    {
        result = window.Initialize(kHostWidth, kHostHeight);
    }
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(reloaded);
    }
    if (SUCCEEDED(result))
    {
        result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), true);
    }
    Check(SUCCEEDED(result) && plugins.WidgetCount() == 1 && plugins.WindowWidgetAt(0) != nullptr &&
              plugins.GpuWidgetAt(0) == nullptr,
          L"a reload that keeps page two stages the native-window neighbour, not the first-page GPU widget", success);

    dashboard.Shutdown();
}

void TestAdoptPrimaryDashboardIsTransactional(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] failed adopt restores the previous dashboard\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.rotating-triangle"}}]}},{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.rotating-triangle"}}]}}]})json";

    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(settingsJson, settings);
    AttachedHostWindow window;
    if (SUCCEEDED(result))
    {
        result = window.Initialize(kHostWidth, kHostHeight);
    }
    PluginManager currentPlugins;
    DashboardHost currentDashboard;
    if (SUCCEEDED(result))
    {
        result = currentPlugins.Initialize(settings);
    }
    if (SUCCEEDED(result))
    {
        result =
            currentDashboard.Initialize(currentPlugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    }
    Renderer renderer;
    if (SUCCEEDED(result))
    {
        result = renderer.Initialize(window.Get(), true, currentDashboard);
    }
    AppSettings nextSettings = settings;
    PluginManager nextPlugins;
    DashboardHost nextDashboard;
    if (SUCCEEDED(result))
    {
        result = MoveDashboardPage(nextSettings, 1);
    }
    if (SUCCEEDED(result))
    {
        result = nextPlugins.Initialize(nextSettings);
    }
    if (SUCCEEDED(result))
    {
        result = nextDashboard.Initialize(nextPlugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    }
    if (SUCCEEDED(result))
    {
        result = renderer.SetTransitionDashboard(&nextDashboard);
    }
    Check(SUCCEEDED(result), L"transactional-adopt host stages a neighbor", success);
    if (FAILED(result))
    {
        return;
    }

    result = renderer.Resize(0, 0);
    Check(SUCCEEDED(result) && renderer.IsSuspended(), L"zero-sized resize suspends the renderer", success);
    result = renderer.AdoptPrimaryDashboard(nextDashboard);
    Check(FAILED(result), L"adopt fails while the swap chain is zero-sized", success);
    (void)renderer.SetTransitionDashboard(nullptr);
    nextDashboard.Shutdown();
    result = renderer.Resize(kHostWidth, kHostHeight);
    if (SUCCEEDED(result))
    {
        result = renderer.Render(0.4f, 1.0f / 60.0f);
    }
    Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 1,
          L"a failed adopt still draws the original page after the incoming host is destroyed", success);

    renderer.Shutdown();
    currentDashboard.Shutdown();
}

void TestNativeWindowNeighborSwipe(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] native-window neighbor swipe layout and occlusion\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":4},"pages":[{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.rotating-triangle"}}]}},{"layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.gdi-orbit"}}]}}]})json";

    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(settingsJson, settings);
    AttachedHostWindow window;
    if (SUCCEEDED(result))
    {
        result = window.Initialize(kHostWidth, kHostHeight);
    }
    PluginManager currentPlugins;
    DashboardHost currentDashboard;
    if (SUCCEEDED(result))
    {
        result = currentPlugins.Initialize(settings);
    }
    if (SUCCEEDED(result))
    {
        result = currentDashboard.Initialize(currentPlugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), true);
    }
    Renderer renderer;
    if (SUCCEEDED(result))
    {
        result = renderer.Initialize(window.Get(), true, currentDashboard);
    }
    AppSettings nextSettings = settings;
    PluginManager nextPlugins;
    DashboardHost nextDashboard;
    if (SUCCEEDED(result))
    {
        result = MoveDashboardPage(nextSettings, 1);
    }
    if (SUCCEEDED(result))
    {
        result = nextPlugins.Initialize(nextSettings);
    }
    if (SUCCEEDED(result))
    {
        result = nextDashboard.Initialize(nextPlugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), true);
    }
    Check(SUCCEEDED(result) && nextDashboard.HasWindowWidgets() && nextDashboard.WindowWidgetAt(0) != nullptr,
          L"the neighbor page hosts a native-window widget", success);
    if (FAILED(result))
    {
        return;
    }

    result = nextDashboard.SetHorizontalOffset(static_cast<LONG>(kHostWidth));
    if (SUCCEEDED(result))
    {
        result = renderer.SetTransitionDashboard(&nextDashboard);
    }
    const LONG offsets[] = {-static_cast<LONG>(kHostWidth) / 4, -static_cast<LONG>(kHostWidth) / 2,
                            -static_cast<LONG>(kHostWidth)};
    for (LONG offset : offsets)
    {
        if (FAILED(result))
        {
            break;
        }
        result = currentDashboard.SetHorizontalOffset(offset);
        if (SUCCEEDED(result))
        {
            result = nextDashboard.SetHorizontalOffset(offset + static_cast<LONG>(kHostWidth));
        }
        if (SUCCEEDED(result))
        {
            result = renderer.RefreshLayout();
        }
        if (SUCCEEDED(result))
        {
            result = renderer.Render(0.2f, 1.0f / 60.0f);
        }
    }
    Check(SUCCEEDED(result), L"RefreshLayout succeeds at every settle offset through a native neighbor", success);

    result = renderer.AdoptPrimaryDashboard(nextDashboard);
    if (SUCCEEDED(result))
    {
        currentDashboard.Shutdown();
        result = nextDashboard.SetHorizontalOffset(0);
    }
    if (SUCCEEDED(result))
    {
        result = renderer.RefreshLayout();
    }
    if (SUCCEEDED(result))
    {
        result = renderer.Render(0.3f, 1.0f / 60.0f);
    }
    Check(SUCCEEDED(result) && !renderer.IsOccluded(),
          L"a full-page native widget does not mark the swap chain occluded", success);

    renderer.Shutdown();
    nextDashboard.Shutdown();
    currentDashboard.Shutdown();
}

[[nodiscard]] bool RunStudioClockHostSoak(std::chrono::seconds duration) noexcept
{
    bool success = true;
    std::wcout << L"[ RUN      ] scheduled production-host Studio Clock soak\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":4},"pages":[{"name":"Clock","layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.studio-clock"}}]}}]})json";
    AttachedHostWindow window;
    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(settingsJson, settings);
    if (SUCCEEDED(result))
    {
        result = window.Initialize(kHostWidth, kHostHeight);
    }
    PluginManager plugins;
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(settings);
    }
    DashboardHost dashboard;
    if (SUCCEEDED(result))
    {
        result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    }
    Renderer renderer;
    if (SUCCEEDED(result))
    {
        result = renderer.Initialize(window.Get(), true, dashboard);
    }
    Check(SUCCEEDED(result) && !dashboard.RequiresContinuousFrames(), L"Studio Clock soak host initializes", success);
    if (FAILED(result))
    {
        return false;
    }

    const StudioClockSetTestTimeFn setTime =
        ResolveFunction<StudioClockSetTestTimeFn>(GetModuleHandleW(L"StudioClock.dll"), kStudioClockSetTestTimeExport);
    if (!setTime || FAILED(setTime(nullptr)))
    {
        return false;
    }
    result = renderer.Render(0.0f, 0.0f);
    StudioClockTestDiagnostics diagnosticsBefore{};
    StudioClockTestDiagnostics diagnosticsAfter{};
    ProcessMemorySnapshot memoryBefore{};
    ProcessMemorySnapshot memoryAfter{};
    if (SUCCEEDED(result))
    {
        result = ReadStudioClockDiagnostics(diagnosticsBefore);
    }
    if (SUCCEEDED(result))
    {
        result = QueryProcessMemorySnapshot(memoryBefore);
    }

    uint64_t frames = 1;
    uint64_t correctiveFrames = 0;
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + duration;
    auto previous = started;
    while (SUCCEEDED(result) && std::chrono::steady_clock::now() < deadline)
    {
        uint32_t delay = 0;
        result = dashboard.GetNextFrameDelayMilliseconds(&delay);
        if (FAILED(result) || delay == 0 || delay > 1001)
        {
            std::wcout << L"studio_clock_host_soak unexpected_delay=" << delay << L" hresult=0x" << std::hex
                       << static_cast<uint32_t>(result) << std::dec << L'\n';
            break;
        }
        if (delay == 1)
        {
            ++correctiveFrames;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
        {
            break;
        }
        const float elapsed = std::chrono::duration<float>(now - started).count();
        const float delta = std::chrono::duration<float>(now - previous).count();
        previous = now;
        result = renderer.Render(elapsed, delta);
        ++frames;
    }
    if (SUCCEEDED(result))
    {
        result = ReadStudioClockDiagnostics(diagnosticsAfter);
    }
    if (SUCCEEDED(result))
    {
        result = QueryProcessMemorySnapshot(memoryAfter);
    }
    Check(SUCCEEDED(result), L"Studio Clock scheduled soak frames render without failure", success);
    const uint64_t expectedSeconds = static_cast<uint64_t>(duration.count());
    Check(correctiveFrames <= 1 && frames >= expectedSeconds && frames <= expectedSeconds + 1 + correctiveFrames,
          L"Studio Clock soak renders no more than one scheduled frame per displayed second plus one correction",
          success);
    Check(diagnosticsAfter.liveProviderCount == diagnosticsBefore.liveProviderCount &&
              diagnosticsAfter.liveWidgetCount == diagnosticsBefore.liveWidgetCount &&
              diagnosticsAfter.liveSharedDeviceResourceSetCount == diagnosticsBefore.liveSharedDeviceResourceSetCount &&
              diagnosticsAfter.liveConstantBufferCount == diagnosticsBefore.liveConstantBufferCount,
          L"Studio Clock soak retains stable object and resource counts", success);

    const uint64_t hiddenSampleCount = diagnosticsAfter.timeSampleCount;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    result = ReadStudioClockDiagnostics(diagnosticsAfter);
    Check(SUCCEEDED(result) && diagnosticsAfter.timeSampleCount == hiddenSampleCount,
          L"a hidden Studio Clock interval performs no plugin-owned clock work", success);
    std::wcout << L"studio_clock_host_soak seconds=" << duration.count() << L" frames=" << frames
               << L" corrective_frames=" << correctiveFrames << L" private_bytes_delta="
               << (static_cast<std::int64_t>(memoryAfter.privateBytes) -
                   static_cast<std::int64_t>(memoryBefore.privateBytes))
               << L" working_set_delta="
               << (static_cast<std::int64_t>(memoryAfter.workingSetBytes) -
                   static_cast<std::int64_t>(memoryBefore.workingSetBytes))
               << L" providers=" << diagnosticsAfter.liveProviderCount << L" widgets="
               << diagnosticsAfter.liveWidgetCount << L" device_resource_sets="
               << diagnosticsAfter.liveSharedDeviceResourceSetCount << L" constant_buffers="
               << diagnosticsAfter.liveConstantBufferCount << L'\n';

    renderer.Shutdown();
    dashboard.Shutdown();
    result = ReadStudioClockDiagnostics(diagnosticsAfter);
    Check(SUCCEEDED(result) && diagnosticsAfter.liveSharedDeviceResourceSetCount == 0 &&
              diagnosticsAfter.liveConstantBufferCount == 0,
          L"Studio Clock soak shutdown releases all device resources", success);
    return success;
}

[[nodiscard]] bool RunDeskClockHostSoak(std::chrono::seconds duration) noexcept
{
    bool success = true;
    std::wcout << L"[ RUN      ] scheduled production-host Desk Clock flip soak\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":4},"pages":[{"name":"Clock","layout":{"arrangeAlong":"long-side","areas":[{"sizeRatio":1,"widget":{"plugin":"builtin.desk-clock"}}]}}]})json";
    AttachedHostWindow window;
    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(settingsJson, settings);
    if (SUCCEEDED(result))
    {
        result = window.Initialize(kHostWidth, kHostHeight);
    }
    PluginManager plugins;
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(settings);
    }
    DashboardHost dashboard;
    if (SUCCEEDED(result))
    {
        result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    }
    Renderer renderer;
    if (SUCCEEDED(result))
    {
        result = renderer.Initialize(window.Get(), true, dashboard);
    }
    Check(SUCCEEDED(result) && !dashboard.RequiresContinuousFrames(), L"Desk Clock soak host initializes", success);
    if (FAILED(result))
    {
        return false;
    }

    const DeskClockSetTestTimeFn setTime =
        ResolveFunction<DeskClockSetTestTimeFn>(GetModuleHandleW(L"DeskClock.dll"), kDeskClockSetTestTimeExport);
    if (!setTime || FAILED(setTime(nullptr)))
    {
        return false;
    }
    result = renderer.Render(0.0f, 0.0f);
    DeskClockTestDiagnostics diagnosticsBefore{};
    DeskClockTestDiagnostics diagnosticsAfter{};
    ProcessMemorySnapshot memoryBefore{};
    ProcessMemorySnapshot memoryAfter{};
    if (SUCCEEDED(result))
    {
        result = ReadDeskClockDiagnostics(diagnosticsBefore);
    }
    if (SUCCEEDED(result))
    {
        result = QueryProcessMemorySnapshot(memoryBefore);
    }

    uint64_t frames = 0;
    uint64_t activeDelayCount = 0;
    uint64_t idleDelayCount = 0;
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + duration;
    auto previous = started;
    while (SUCCEEDED(result) && std::chrono::steady_clock::now() < deadline)
    {
        uint32_t delay = 0;
        result = dashboard.GetNextFrameDelayMilliseconds(&delay);
        if (FAILED(result) || delay == 0 || delay > 1000)
        {
            std::wcout << L"desk_clock_host_soak unexpected_delay=" << delay << L" hresult=0x" << std::hex
                       << static_cast<uint32_t>(result) << std::dec << L'\n';
            break;
        }
        if (delay == 1)
        {
            ++activeDelayCount;
        }
        else
        {
            ++idleDelayCount;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
        {
            break;
        }
        const float elapsed = std::chrono::duration<float>(now - started).count();
        const float delta = std::chrono::duration<float>(now - previous).count();
        previous = now;
        result = renderer.Render(elapsed, delta);
        ++frames;
    }
    if (SUCCEEDED(result))
    {
        result = ReadDeskClockDiagnostics(diagnosticsAfter);
    }
    if (SUCCEEDED(result))
    {
        result = QueryProcessMemorySnapshot(memoryAfter);
    }
    Check(SUCCEEDED(result), L"Desk Clock scheduled soak frames render without failure", success);
    const uint64_t expectedSeconds = static_cast<uint64_t>(duration.count());
    Check(frames >= expectedSeconds && frames <= expectedSeconds * 1002 + 1,
          L"Desk Clock soak stays bounded to its second boundary and flip bursts", success);
    Check(activeDelayCount != 0 && idleDelayCount != 0,
          L"Desk Clock soak observes both active-flip and blocked-idle scheduling", success);
    Check(diagnosticsAfter.liveProviders == diagnosticsBefore.liveProviders &&
              diagnosticsAfter.liveWidgets == diagnosticsBefore.liveWidgets &&
              diagnosticsAfter.liveDeviceResourceSets == diagnosticsBefore.liveDeviceResourceSets,
          L"Desk Clock soak retains stable object and resource counts", success);
    const uint64_t soakDraws = diagnosticsAfter.drawCalls - diagnosticsBefore.drawCalls;
    Check(soakDraws >= frames * 3 && soakDraws <= frames * 4,
          L"Desk Clock soak remains within three static or four animated draws per frame", success);

    const uint64_t hiddenSampleCount = diagnosticsAfter.timeSamples;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    result = ReadDeskClockDiagnostics(diagnosticsAfter);
    Check(SUCCEEDED(result) && diagnosticsAfter.timeSamples == hiddenSampleCount,
          L"a hidden Desk Clock interval performs no plugin-owned clock work", success);
    std::wcout << L"desk_clock_host_soak seconds=" << duration.count() << L" frames=" << frames << L" active_delays="
               << activeDelayCount << L" idle_delays=" << idleDelayCount << L" private_bytes_delta="
               << (static_cast<std::int64_t>(memoryAfter.privateBytes) -
                   static_cast<std::int64_t>(memoryBefore.privateBytes))
               << L" working_set_delta="
               << (static_cast<std::int64_t>(memoryAfter.workingSetBytes) -
                   static_cast<std::int64_t>(memoryBefore.workingSetBytes))
               << L" providers=" << diagnosticsAfter.liveProviders << L" widgets=" << diagnosticsAfter.liveWidgets
               << L" device_resource_sets=" << diagnosticsAfter.liveDeviceResourceSets << L" draws=" << soakDraws
               << L'\n';

    renderer.Shutdown();
    dashboard.Shutdown();
    result = ReadDeskClockDiagnostics(diagnosticsAfter);
    Check(SUCCEEDED(result) && diagnosticsAfter.liveDeviceResourceSets == 0,
          L"Desk Clock soak shutdown releases all device resources", success);
    return success;
}

[[nodiscard]] bool RunMatrixHostSoak(std::chrono::seconds duration) noexcept
{
    bool success = true;
    std::wcout << L"[ RUN      ] hidden production-host Matrix soak\n";
    AttachedHostWindow window;
    HRESULT result = window.Initialize(kHostWidth, kHostHeight);
    PluginManager plugins;
    DashboardHost dashboard;
    Renderer renderer;
    if (SUCCEEDED(result))
    {
        AppSettings settings{};
        result = LoadDeployedSettings(kRedXeReleaseSettingsFileName, settings);
        if (SUCCEEDED(result))
        {
            result = plugins.Initialize(settings);
        }
    }
    if (SUCCEEDED(result))
    {
        result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    }
    if (SUCCEEDED(result))
    {
        result = renderer.Initialize(window.Get(), true, dashboard);
    }
    Check(SUCCEEDED(result), L"soak host initializes", success);
    if (FAILED(result))
    {
        return false;
    }

    for (uint32_t frame = 0; frame < 1024; ++frame)
    {
        result = renderer.Render(static_cast<float>(frame) / 60.0f, 1.0f / 60.0f);
        if (FAILED(result))
        {
            return false;
        }
    }

    MatrixRainTestDiagnostics diagnosticsBefore{};
    MatrixRainTestDiagnostics diagnosticsAfter{};
    ProcessMemorySnapshot memoryBefore{};
    ProcessMemorySnapshot memoryAfter{};
    result = ReadMatrixDiagnostics(diagnosticsBefore);
    if (SUCCEEDED(result))
    {
        result = QueryProcessMemorySnapshot(memoryBefore);
    }

    uint64_t frames = 0;
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + duration;
    auto previous = started;
    while (SUCCEEDED(result) && std::chrono::steady_clock::now() < deadline)
    {
        const auto now = std::chrono::steady_clock::now();
        const float elapsed = std::chrono::duration<float>(now - started).count();
        const float delta = std::chrono::duration<float>(now - previous).count();
        previous = now;
        result = renderer.Render(elapsed, delta);
        ++frames;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    if (SUCCEEDED(result))
    {
        result = ReadMatrixDiagnostics(diagnosticsAfter);
    }
    if (SUCCEEDED(result))
    {
        result = QueryProcessMemorySnapshot(memoryAfter);
    }

    Check(SUCCEEDED(result), L"soak frames render without failure", success);
    Check(diagnosticsAfter.liveProviderCount == diagnosticsBefore.liveProviderCount &&
              diagnosticsAfter.liveWidgetCount == diagnosticsBefore.liveWidgetCount &&
              diagnosticsAfter.liveDeviceResourceSetCount == diagnosticsBefore.liveDeviceResourceSetCount,
          L"soak retains a constant Matrix object and device-resource count", success);
    std::wcout << L"host_soak seconds=" << duration.count() << L" frames=" << frames << L" private_bytes_delta="
               << (static_cast<std::int64_t>(memoryAfter.privateBytes) -
                   static_cast<std::int64_t>(memoryBefore.privateBytes))
               << L" working_set_delta="
               << (static_cast<std::int64_t>(memoryAfter.workingSetBytes) -
                   static_cast<std::int64_t>(memoryBefore.workingSetBytes))
               << L" providers=" << diagnosticsAfter.liveProviderCount << L" widgets="
               << diagnosticsAfter.liveWidgetCount << L" device_resource_sets="
               << diagnosticsAfter.liveDeviceResourceSetCount << L'\n';

    renderer.Shutdown();
    dashboard.Shutdown();
    MatrixRainTestDiagnostics shutdownDiagnostics{};
    result = ReadMatrixDiagnostics(shutdownDiagnostics);
    Check(SUCCEEDED(result) && shutdownDiagnostics.liveDeviceResourceSetCount == 0,
          L"soak shutdown releases Matrix device resources", success);
    return success;
}

[[nodiscard]] bool TryParseDuration(int argumentCount, wchar_t** arguments, std::chrono::seconds& duration) noexcept
{
    duration = std::chrono::seconds(300);
    for (int index = 2; index < argumentCount; ++index)
    {
        const std::wstring_view argument = arguments[index] ? std::wstring_view(arguments[index]) : std::wstring_view{};
        constexpr std::wstring_view prefix = L"--seconds=";
        if (!argument.starts_with(prefix))
        {
            return false;
        }
        const std::wstring_view value = argument.substr(prefix.size());
        if (value.empty())
        {
            return false;
        }
        uint64_t parsed = 0;
        for (const wchar_t character : value)
        {
            if (character < L'0' || character > L'9')
            {
                return false;
            }
            parsed = parsed * 10 + static_cast<uint64_t>(character - L'0');
            if (parsed > 3600)
            {
                return false;
            }
        }
        if (parsed == 0)
        {
            return false;
        }
        duration = std::chrono::seconds(parsed);
    }
    return true;
}
} // namespace

int wmain(int argumentCount, wchar_t** arguments)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argumentCount >= 2 && arguments[1] && std::wstring_view(arguments[1]) == L"--matrix-soak")
    {
        std::chrono::seconds duration{};
        if (!TryParseDuration(argumentCount, arguments, duration))
        {
            std::wcerr << L"Usage: HostPluginTests.exe --matrix-soak [--seconds=1..3600]\n";
            return 2;
        }
        return RunMatrixHostSoak(duration) ? 0 : 1;
    }
    if (argumentCount >= 2 && arguments[1] && std::wstring_view(arguments[1]) == L"--studio-clock-soak")
    {
        std::chrono::seconds duration{};
        if (!TryParseDuration(argumentCount, arguments, duration))
        {
            std::wcerr << L"Usage: HostPluginTests.exe --studio-clock-soak [--seconds=1..3600]\n";
            return 2;
        }
        return RunStudioClockHostSoak(duration) ? 0 : 1;
    }
    if (argumentCount >= 2 && arguments[1] && std::wstring_view(arguments[1]) == L"--desk-clock-soak")
    {
        std::chrono::seconds duration{};
        if (!TryParseDuration(argumentCount, arguments, duration))
        {
            std::wcerr << L"Usage: HostPluginTests.exe --desk-clock-soak [--seconds=1..3600]\n";
            return 2;
        }
        return RunDeskClockHostSoak(duration) ? 0 : 1;
    }
    if (argumentCount != 1)
    {
        std::wcerr << L"Unknown HostPluginTests argument.\n";
        return 2;
    }

    bool success = true;
    TestFrameScheduler(success);
    TestPageSwipePolicy(success);
    TestWidgetRaisePolicy(success);
    TestWidgetRaiseHost(success);
    TestWidgetRaiseNative(success);
    TestReleaseHostIntegration(success);
    TestStudioClockScheduling(success);
    TestDeskClockScheduling(success);
    TestDebugHostComposition(success);
    TestDataProviderLookup(success);
    TestProcessViewerSubscription(success);
    TestSystemDataViewers(success);
    TestGpuTargetSizeNotification(success);
    TestSharedPluginRuntime(success);
    TestHostRequestFrameAndWidgetStatus(success);
    TestHostOwnedPlaceholderTiles(success);
    TestPageEdgeAffordancePolicy(success);
    TestPageEdgeAffordanceGeometry(success);
    TestNonDivisibleGridEdges(success);
    TestPromoteStagedDashboard(success);
    TestSwipeRendersPartiallyOffscreenGpuWidgets(success);
    TestSettingsReloadKeepsCurrentPage(success);
    TestAdoptPrimaryDashboardIsTransactional(success);
    TestNativeWindowNeighborSwipe(success);
    std::wcout << (success ? L"HostPluginTests passed.\n" : L"HostPluginTests failed.\n");
    return success ? 0 : 1;
}
