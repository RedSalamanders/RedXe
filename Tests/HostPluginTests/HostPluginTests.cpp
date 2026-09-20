#include "../../Plugins/Logicon/LogiconTestContract.h"
#include "../../Plugins/Weather/WeatherTestContract.h"
#include "DashboardHost.h"
#include "DeskClockTestContract.h"
#include "DockPlacement.h"
#include "FrameScheduler.h"
#include "HostActions.h"
#include "LauncherTestContract.h"
#include "MatrixRainTestContract.h"
#include "PageEdgeAffordance.h"
#include "PageIndicator.h"
#include "PageNavigation.h"
#include "PlugInterfaces/FactoryImpl.h"
#include "PluginHost.h"
#include "PluginManager.h"
#include "ProcessViewerTestContract.h"
#include "Renderer.h"
#include "Settings.h"
#include "StudioClockTestContract.h"
#include "WheelNavigation.h"
#include "WidgetRaise.h"
#include "WindowCapture.h"

#include <shellapi.h>
#include <tlhelp32.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <yyjson.h>

#include <ole2.h>
#include <psapi.h>
#include <wincodec.h>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
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
    state.overlayMotionActive = true;
    Check(SelectHostFrameAction(state) == HostFrameAction::Render,
          L"raise or dismiss settle keeps presenting when DXGI reports the swap chain occluded", success);
    state.rendererOccluded = false;
    Check(SelectHostFrameAction(state) == HostFrameAction::Render,
          L"an active raise settle keeps presenting so overlay motion continues", success);
    state.overlayMotionActive = false;
    Check(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage,
          L"a settled overlay does not keep a static host presenting", success);

    // A hidden autohide dock: one grip frame per invalidation, then a wait regardless of continuous widgets.
    state.dockHidden = true;
    state.continuousFramesRequired = true;
    Check(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage,
          L"a hidden dock with a clean frame waits even with continuous widgets", success);
    state.frameInvalidated = true;
    Check(SelectHostFrameAction(state) == HostFrameAction::Render, L"a hidden dock renders its one grip frame",
          success);
    state.rendererOccluded = true;
    state.occlusionStatusChanged = false;
    Check(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage,
          L"an occluded hidden dock waits like any occluded host", success);
    state.rendererOccluded = false;
    state.windowVisible = false;
    Check(SelectHostFrameAction(state) == HostFrameAction::WaitForMessage, L"a hidden dock on a hidden window waits",
          success);
}

// DockPlacement.h: placement rectangles for every edge, the shell re-trim, the peek strip, the grip, the clamp,
// MINMAXINFO, and monitor selection with its fallback, all without a display topology.
void TestDockPlacement(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] dock placement, monitor selection, and MINMAXINFO\n";
    DockEdge edge = DockEdge::None;
    Check(DockEdgeParse("top", edge) && edge == DockEdge::Top && DockEdgeParse("bottom", edge) &&
              edge == DockEdge::Bottom && DockEdgeParse("left", edge) && edge == DockEdge::Left &&
              DockEdgeParse("right", edge) && edge == DockEdge::Right && DockEdgeParse("none", edge) &&
              edge == DockEdge::None && !DockEdgeParse("middle", edge) && !DockEdgeParse("Top", edge),
          L"edge names parse exactly and nothing else does", success);
    DockMode mode = DockMode::Fixed;
    Check(DockModeParse("autohide", mode) && mode == DockMode::Autohide && DockModeParse("fixed", mode) &&
              mode == DockMode::Fixed && !DockModeParse("auto", mode),
          L"mode names parse exactly", success);
    Check(DockAppBarEdge(DockEdge::Left) == ABE_LEFT && DockAppBarEdge(DockEdge::Top) == ABE_TOP &&
              DockAppBarEdge(DockEdge::Right) == ABE_RIGHT && DockAppBarEdge(DockEdge::Bottom) == ABE_BOTTOM,
          L"edges map to the shell's ABE values", success);

    Check(DockThicknessPixels(180, 96) == 180 && DockThicknessPixels(180, 144) == 270 &&
              DockThicknessPixels(180, 192) == 360 && DockThicknessPixels(180, 0) == 180,
          L"thickness scales from DIPs by the monitor DPI", success);
    const RECT monitor{0, 0, 2560, 1440};
    const RECT work{0, 0, 2560, 1392}; // 48-pixel taskbar at the bottom
    bool clamped = false;
    Check(DockClampThickness(180, monitor, DockEdge::Bottom, clamped) == 180 && !clamped,
          L"a thickness under half the monitor is not clamped", success);
    Check(DockClampThickness(1000, monitor, DockEdge::Bottom, clamped) == 720 && clamped,
          L"a thickness over half the monitor clamps to half the cross dimension", success);
    Check(DockClampThickness(1500, monitor, DockEdge::Left, clamped) == 1280 && clamped,
          L"a side dock clamps against the monitor width", success);

    // Reserving proposal and re-trim: the shell may move the edge side; the result is always `thickness` deep.
    const RECT proposedBottom = DockTrimToThickness(monitor, DockEdge::Bottom, 180);
    Check(proposedBottom.top == 1260 && proposedBottom.bottom == 1440 && proposedBottom.left == 0 &&
              proposedBottom.right == 2560,
          L"a bottom proposal is the monitor trimmed to the thickness", success);
    const RECT shellAdjusted{0, 0, 2560, 1392};
    const RECT retrimmed = DockTrimToThickness(shellAdjusted, DockEdge::Bottom, 180);
    Check(retrimmed.top == 1212 && retrimmed.bottom == 1392, L"the re-trim keeps the thickness from the returned edge",
          success);
    Check(DockTrimToThickness(monitor, DockEdge::Top, 180).bottom == 180 &&
              DockTrimToThickness(monitor, DockEdge::Left, 240).right == 240 &&
              DockTrimToThickness(monitor, DockEdge::Right, 240).left == 2320,
          L"top, left, and right proposals trim their own side", success);

    // Overlay and autohide hug the work-area edge, so a bottom bar sits above the taskbar.
    const RECT overlayBottom = DockOverlayRect(work, DockEdge::Bottom, 180);
    Check(overlayBottom.bottom == 1392 && overlayBottom.top == 1212 && overlayBottom.right == 2560,
          L"an overlay bottom bar hugs the work-area bottom", success);
    const RECT leftWork{80, 0, 2560, 1392}; // a 80-pixel bar on the left plus the taskbar
    const RECT overlayLeft = DockOverlayRect(leftWork, DockEdge::Left, 240);
    Check(overlayLeft.left == 80 && overlayLeft.right == 320 && overlayLeft.bottom == 1392,
          L"an overlay side bar spans the work area and clears another bar", success);
    const RECT secondary{-1920, -200, 0, 880}; // off-origin secondary display at negative coordinates
    const RECT secondaryTop = DockOverlayRect(secondary, DockEdge::Top, 100);
    Check(secondaryTop.left == -1920 && secondaryTop.top == -200 && secondaryTop.bottom == -100,
          L"placement works at negative coordinates", success);

    // Hidden strip and grip for every edge.
    const RECT fullBottom{0, 1212, 2560, 1392};
    const RECT hiddenBottom = DockHiddenRect(fullBottom, DockEdge::Bottom, 4);
    Check(hiddenBottom.top == 1388 && hiddenBottom.bottom == 1392 && hiddenBottom.left == 0 &&
              hiddenBottom.right == 2560,
          L"a hidden bottom bar keeps its outer four rows", success);
    const RECT fullTop{0, 0, 2560, 180};
    Check(DockHiddenRect(fullTop, DockEdge::Top, 4).bottom == 4, L"a hidden top bar keeps its top rows", success);
    const RECT fullLeft{0, 0, 240, 1392};
    Check(DockHiddenRect(fullLeft, DockEdge::Left, 6).right == 6, L"a hidden left bar keeps its left columns", success);
    const RECT fullRight{2320, 0, 2560, 1392};
    Check(DockHiddenRect(fullRight, DockEdge::Right, 6).left == 2554, L"a hidden right bar keeps its right columns",
          success);
    const RECT gripBottom = DockGripRect(2560, 180, DockEdge::Bottom, 4);
    const RECT gripLeft = DockGripRect(240, 1392, DockEdge::Left, 6);
    Check(gripBottom.top == 0 && gripBottom.bottom == 4 && gripBottom.right == 2560 && gripLeft.left == 0 &&
              gripLeft.right == 6 && gripLeft.bottom == 1392,
          L"the grip is the back buffer's first rows or columns for every edge", success);
    const RECT emptyGrip = DockGripRect(2560, 180, DockEdge::None, 4);
    Check(emptyGrip.right == emptyGrip.left, L"no edge means no grip", success);
    const RECT accentTop = DockGripAccentRect(DockGripRect(2560, 180, DockEdge::Top, 4), DockEdge::Top, 1);
    const RECT accentBottom = DockGripAccentRect(gripBottom, DockEdge::Bottom, 1);
    const RECT accentLeft = DockGripAccentRect(gripLeft, DockEdge::Left, 2);
    const RECT accentRight = DockGripAccentRect(DockGripRect(240, 1392, DockEdge::Right, 6), DockEdge::Right, 2);
    Check(accentTop.top == 3 && accentTop.bottom == 4 && accentBottom.top == 0 && accentBottom.bottom == 1 &&
              accentLeft.left == 4 && accentLeft.right == 6 && accentRight.left == 0 && accentRight.right == 2,
          L"the accent line sits on the desktop-facing side of the strip", success);

    // Drag-to-resize: the grip band hugs the inner edge, and the dragged thickness is the distance from the outer
    // edge in DIPs, clamped to the settings range and to half the monitor.
    const RECT bandBottom = DockResizeBandRect(2560, 180, DockEdge::Bottom, 6);
    const RECT bandTop = DockResizeBandRect(2560, 180, DockEdge::Top, 6);
    const RECT bandLeft = DockResizeBandRect(240, 1392, DockEdge::Left, 6);
    const RECT bandRight = DockResizeBandRect(240, 1392, DockEdge::Right, 6);
    Check(bandBottom.top == 0 && bandBottom.bottom == 6 && bandBottom.right == 2560 && bandTop.top == 174 &&
              bandTop.bottom == 180 && bandLeft.left == 234 && bandLeft.right == 240 && bandRight.left == 0 &&
              bandRight.right == 6 && bandRight.bottom == 1392,
          L"the resize band sits on the desktop-facing edge of every dock", success);
    const RECT thinBand = DockResizeBandRect(2560, 8, DockEdge::Bottom, 6);
    const RECT noBand = DockResizeBandRect(2560, 180, DockEdge::None, 6);
    Check(thinBand.bottom == 4 && noBand.right == noBand.left, L"a thin bar halves the band and no edge means no band",
          success);
    Check(DockThicknessFromDrag(fullBottom, monitor, DockEdge::Bottom, POINT{100, 1292}, 96) == 100 &&
              DockThicknessFromDrag(fullBottom, monitor, DockEdge::Bottom, POINT{100, 1242}, 144) == 100 &&
              DockThicknessFromDrag(fullTop, monitor, DockEdge::Top, POINT{100, 250}, 96) == 250 &&
              DockThicknessFromDrag(fullLeft, monitor, DockEdge::Left, POINT{300, 10}, 96) == 300 &&
              DockThicknessFromDrag(fullRight, monitor, DockEdge::Right, POINT{2260, 10}, 96) == 300,
          L"a dragged edge measures the thickness from the outer edge at the monitor DPI", success);
    Check(DockThicknessFromDrag(fullBottom, monitor, DockEdge::Bottom, POINT{100, 1500}, 96) ==
                  kDockMinimumThicknessDips &&
              DockThicknessFromDrag(fullBottom, monitor, DockEdge::Bottom, POINT{100, -500}, 96) == 720 &&
              DockThicknessFromDrag(fullBottom, monitor, DockEdge::Bottom, POINT{100, -500}, 192) == 360,
          L"a drag past the outer edge clamps to the minimum and one into the desktop to half the monitor", success);

    // MINMAXINFO: the strip is the minimum for autohide, the full bar for fixed, the monitor the maximum.
    MINMAXINFO autohideInfo{};
    DockMinMaxInfo(monitor, DockEdge::Bottom, 4, true, fullBottom, autohideInfo);
    Check(autohideInfo.ptMinTrackSize.x == 2560 && autohideInfo.ptMinTrackSize.y == 4 &&
              autohideInfo.ptMaxTrackSize.x == 2560 && autohideInfo.ptMaxTrackSize.y == 1440,
          L"an autohide dock's minimum is its peek strip and its maximum the monitor", success);
    MINMAXINFO fixedInfo{};
    DockMinMaxInfo(monitor, DockEdge::Left, 4, false, fullLeft, fixedInfo);
    Check(fixedInfo.ptMinTrackSize.x == 240 && fixedInfo.ptMinTrackSize.y == 1392,
          L"a fixed dock's minimum is its full rectangle", success);

    // Monitor selection over candidates: primary, xeneon, index, name (friendly or device), and the fallback.
    std::array<DockMonitorCandidate, 3> candidates{};
    candidates[0].monitor = secondary;
    candidates[0].friendlyName = L"DELL U2723QE";
    candidates[0].deviceName = L"\\\\.\\DISPLAY2";
    candidates[1].monitor = monitor;
    candidates[1].primary = true;
    candidates[1].friendlyName = L"LG ULTRAGEAR";
    candidates[1].deviceName = L"\\\\.\\DISPLAY1";
    candidates[2].monitor = RECT{2560, 0, 5120, 720};
    candidates[2].xeneon = true;
    candidates[2].friendlyName = L"XENEON EDGE";
    candidates[2].deviceName = L"\\\\.\\DISPLAY3";
    bool fellBack = true;
    RedXeActions::MonitorSelector selector{};
    Check(RedXeActions::ParseMonitorSelector("primary", false, selector) &&
              SelectDockMonitor(selector, {}, candidates.data(), candidates.size(), fellBack) == 1 && !fellBack,
          L"primary selects the primary display wherever it enumerates", success);
    Check(RedXeActions::ParseMonitorSelector("xeneon", false, selector) &&
              SelectDockMonitor(selector, {}, candidates.data(), candidates.size(), fellBack) == 2 && !fellBack,
          L"xeneon selects the discovered XENEON", success);
    Check(RedXeActions::ParseMonitorSelector("1", false, selector) &&
              SelectDockMonitor(selector, {}, candidates.data(), candidates.size(), fellBack) == 0 && !fellBack &&
              RedXeActions::ParseMonitorSelector("3", false, selector) &&
              SelectDockMonitor(selector, {}, candidates.data(), candidates.size(), fellBack) == 2,
          L"a number selects by 1-based enumeration order", success);
    Check(RedXeActions::ParseMonitorSelector("name:dell", false, selector) &&
              SelectDockMonitor(selector, L"dell", candidates.data(), candidates.size(), fellBack) == 0 && !fellBack,
          L"name: matches the friendly name without case", success);
    Check(RedXeActions::ParseMonitorSelector("name:DISPLAY3", false, selector) &&
              SelectDockMonitor(selector, L"DISPLAY3", candidates.data(), candidates.size(), fellBack) == 2,
          L"name: also matches the GDI device name", success);
    Check(RedXeActions::ParseMonitorSelector("4", false, selector) &&
              SelectDockMonitor(selector, {}, candidates.data(), candidates.size(), fellBack) == 1 && fellBack,
          L"an absent display falls back to the primary and reports it", success);
    Check(RedXeActions::ParseMonitorSelector("name:ACER", false, selector) &&
              SelectDockMonitor(selector, L"ACER", candidates.data(), candidates.size(), fellBack) == 1 && fellBack,
          L"an unmatched name falls back to the primary", success);
    std::array<DockMonitorCandidate, 1> noXeneon{};
    noXeneon[0].monitor = monitor;
    noXeneon[0].primary = true;
    Check(RedXeActions::ParseMonitorSelector("xeneon", false, selector) &&
              SelectDockMonitor(selector, {}, noXeneon.data(), noXeneon.size(), fellBack) == 0 && fellBack,
          L"xeneon without a XENEON falls back to the primary", success);
    Check(SelectDockMonitor(selector, {}, nullptr, 0, fellBack) == SIZE_MAX, L"no display means no selection", success);
    Check(!RedXeActions::ParseMonitorSelector("all", false, selector), L"the dock never accepts all", success);
}

// DockPlacement.h autohide state machine: every transition of the reveal/hide table, zero delays, holds, and the
// action semantics, as a pure function of (state, event, holds).
void TestDockAutohidePolicy(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] dock autohide state machine\n";
    using S = DockRevealState;
    using E = DockRevealEvent;
    const DockHolds none{};
    DockHolds pointer{};
    pointer.pointerInside = true;
    DockHolds active{};
    active.windowActive = true;
    DockHolds raised{};
    raised.widgetRaised = true;
    DockHolds captured{};
    captured.captureActive = true;
    DockHolds dialog{};
    dialog.dialogShown = true;
    DockHolds pinned{};
    pinned.pinned = true;

    Check(NextDockRevealState(S::Hidden, E::PointerEnteredStrip, pointer, 150, 800) == S::RevealPending,
          L"a mouse move over the strip starts the dwell", success);
    Check(NextDockRevealState(S::Hidden, E::PointerEnteredStrip, pointer, 0, 800) == S::Revealed,
          L"a zero reveal delay reveals on the first mouse move", success);
    Check(NextDockRevealState(S::RevealPending, E::DwellElapsed, pointer, 150, 800) == S::Revealed,
          L"the dwell elapsing reveals", success);
    Check(NextDockRevealState(S::RevealPending, E::PointerLeft, none, 150, 800) == S::Hidden,
          L"leaving during the dwell hides again", success);
    Check(NextDockRevealState(S::Hidden, E::TouchOnStrip, none, 150, 800) == S::Revealed &&
              NextDockRevealState(S::RevealPending, E::TouchOnStrip, none, 150, 800) == S::Revealed,
          L"a touch on the strip reveals at once", success);
    Check(NextDockRevealState(S::Hidden, E::ActionShow, none, 150, 800) == S::Revealed &&
              NextDockRevealState(S::Hidden, E::ActionToggle, none, 150, 800) == S::Revealed &&
              NextDockRevealState(S::RevealPending, E::ActionToggle, none, 150, 800) == S::Revealed,
          L"show and toggle reveal a hidden or pending bar", success);
    Check(NextDockRevealState(S::Revealed, E::HoldsChanged, none, 150, 800) == S::HidePending,
          L"the last hold clearing arms the hide delay", success);
    Check(NextDockRevealState(S::Revealed, E::HoldsChanged, none, 150, 0) == S::Hidden,
          L"a zero hide delay hides at once", success);
    Check(NextDockRevealState(S::Revealed, E::HoldsChanged, pointer, 150, 800) == S::Revealed &&
              NextDockRevealState(S::Revealed, E::HoldsChanged, active, 150, 800) == S::Revealed &&
              NextDockRevealState(S::Revealed, E::HoldsChanged, raised, 150, 800) == S::Revealed &&
              NextDockRevealState(S::Revealed, E::HoldsChanged, captured, 150, 800) == S::Revealed &&
              NextDockRevealState(S::Revealed, E::HoldsChanged, dialog, 150, 800) == S::Revealed &&
              NextDockRevealState(S::Revealed, E::HoldsChanged, pinned, 150, 800) == S::Revealed,
          L"pointer, activation, raise, capture, dialog, and pin each hold the bar", success);
    Check(NextDockRevealState(S::HidePending, E::HoldsChanged, pointer, 150, 800) == S::Revealed,
          L"a hold returning during the hide delay cancels it", success);
    Check(NextDockRevealState(S::HidePending, E::HideElapsed, none, 150, 800) == S::Hidden,
          L"the hide delay elapsing hides", success);
    Check(NextDockRevealState(S::HidePending, E::ActionHide, none, 150, 800) == S::Hidden &&
              NextDockRevealState(S::Revealed, E::ActionHide, pointer, 150, 800) == S::Hidden &&
              NextDockRevealState(S::Revealed, E::ActionToggle, pointer, 150, 800) == S::Hidden,
          L"hide and toggle collapse a revealed bar even with the pointer inside", success);
    Check(NextDockRevealState(S::Revealed, E::ActionHide, raised, 150, 800) == S::Revealed &&
              NextDockRevealState(S::Revealed, E::ActionHide, captured, 150, 800) == S::Revealed &&
              NextDockRevealState(S::Revealed, E::ActionHide, dialog, 150, 800) == S::Revealed &&
              NextDockRevealState(S::Revealed, E::ActionHide, pinned, 150, 800) == S::Revealed,
          L"hide is inert while a raise, capture, dialog, or pin holds the bar", success);
    Check(NextDockRevealState(S::Hidden, E::Pin, none, 150, 800) == S::Revealed &&
              NextDockRevealState(S::HidePending, E::Pin, none, 150, 800) == S::Revealed,
          L"a pin reveals from any state", success);
    Check(NextDockRevealState(S::Hidden, E::DwellElapsed, none, 150, 800) == S::Hidden &&
              NextDockRevealState(S::Revealed, E::HideElapsed, none, 150, 800) == S::Revealed &&
              NextDockRevealState(S::Revealed, E::PointerEnteredStrip, pointer, 150, 800) == S::Revealed &&
              NextDockRevealState(S::Hidden, E::HoldsChanged, active, 150, 800) == S::Hidden,
          L"stale timer and hold events are ignored in the wrong state", success);
    DockHolds byAction{};
    byAction.pinnedByAction = true;
    Check(NextDockRevealState(S::Revealed, E::HoldsChanged, byAction, 150, 800) == S::Revealed && byAction.Any() &&
              !byAction.RefusesHide(),
          L"an action-revealed bar holds until another hold clears, and still accepts hide", success);
    Check(DockStateShowsStrip(S::Hidden) && DockStateShowsStrip(S::RevealPending) &&
              !DockStateShowsStrip(S::Revealed) && !DockStateShowsStrip(S::HidePending),
          L"the window is the strip exactly in Hidden and RevealPending", success);
}

// A dock-kind swap chain (DXGI_SCALING_NONE at the full bar size) presents both a normal frame and the grip frame
// while the window client is smaller than the back buffer, and the grip frame draws no widget.
void TestDockPresentation(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] dock swap chain and grip frame\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":5},"pages":[{"columns":[{"plugin":"builtin.matrix-rain"},{"plugin":"builtin.desk-clock"}]}]})json";
    constexpr UINT barWidth = 1280;
    constexpr UINT barHeight = 180;
    AttachedHostWindow window;
    HRESULT result = window.Initialize(barWidth, barHeight);
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
    DashboardHost dashboard;
    if (SUCCEEDED(result))
    {
        result = dashboard.Initialize(plugins, window.Get(), barWidth, barHeight, window.Dpi(), false);
    }
    Renderer renderer;
    if (SUCCEEDED(result))
    {
        renderer.SetDockPresentation(true, barWidth, barHeight);
        result = renderer.Initialize(window.Get(), true, dashboard);
    }
    Check(SUCCEEDED(result), L"a dock-kind renderer initializes on the hidden WARP host", success);
    if (FAILED(result))
    {
        dashboard.Shutdown();
        return;
    }
    Check(SUCCEEDED(dashboard.SetWidgetsVisible(true)), L"the dock test shows its page", success);
    result = renderer.Render(0.0f, 0.0f);
    Check(SUCCEEDED(result) && renderer.LastFrameWidgetCount() == 2 && renderer.LastFrameChromeQuadCount() == 0,
          L"a revealed dock presents its tiles like any other window", success);

    // Collapse the window to a 4-pixel strip without touching the swap chain, then present the grip frame.
    RECT bounds{};
    Check(GetWindowRect(window.Get(), &bounds) && SetWindowPos(window.Get(), nullptr, 0, 0, static_cast<int>(barWidth),
                                                               4, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
          L"the dock test shrinks the window to its peek strip", success);
    RECT client{};
    Check(GetClientRect(window.Get(), &client) && client.bottom - client.top < static_cast<LONG>(barHeight),
          L"the client is now smaller than the back buffer", success);
    Check(SUCCEEDED(dashboard.SetWidgetsVisible(false)), L"the hidden dock hides its widgets", success);
    HostChromeState grip{};
    grip.dockHidden = true;
    grip.dockGrip = DockGripRect(barWidth, barHeight, DockEdge::Bottom, 4);
    grip.dockGripAccent = DockGripAccentRect(grip.dockGrip, DockEdge::Bottom, 1);
    Check(renderer.SetHostChrome(grip), L"the grip state is one chrome change", success);
    Check(renderer.PrepareWidgets() == S_FALSE, L"a hidden dock prepares no widget", success);
    result = renderer.Render(0.1f, 1.0f / 60.0f);
    Check(SUCCEEDED(result) && renderer.LastFrameWidgetCount() == 0 && renderer.LastFrameChromeQuadCount() == 2 &&
              HostChromeQuadCount(grip, barWidth, barHeight, false) == 2,
          L"the grip frame draws the wash and accent and no widget, at a client smaller than the buffer", success);
    Check(SetWindowPos(window.Get(), nullptr, 0, 0, static_cast<int>(barWidth), static_cast<int>(barHeight),
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE &&
              SUCCEEDED(dashboard.SetWidgetsVisible(true)) && renderer.SetHostChrome(HostChromeState{}),
          L"the dock test reveals again", success);
    result = renderer.Render(0.2f, 1.0f / 60.0f);
    Check(SUCCEEDED(result) && renderer.LastFrameWidgetCount() == 2 && renderer.LastFrameChromeQuadCount() == 0,
          L"a reveal presents the tiles again without ResizeBuffers", success);
    renderer.Shutdown();
    dashboard.Shutdown();
}

void TestPageSwipePolicy(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] page swipe axis lock, rubber-band, commit, and settle\n";
    Check(!PageSwipeAcceptsFingerCount(0), L"no fingers do not navigate dashboard pages", success);
    Check(!PageSwipeAcceptsFingerCount(1), L"one finger does not navigate dashboard pages", success);
    Check(PageSwipeAcceptsFingerCount(2), L"two fingers may navigate dashboard pages", success);
    Check(PageSwipeAcceptsFingerCount(3), L"three fingers may navigate dashboard pages", success);
    Check(!PageSwipeAcceptsFingerCount(4), L"four fingers do not navigate dashboard pages", success);
    Check(!PageSwipeStealsWidgetGesture(false), L"an uncommitted two-finger contact does not cancel a widget gesture",
          success);
    Check(PageSwipeStealsWidgetGesture(true), L"a locked page pan cancels the widget gesture", success);

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

    // The raise dim is drawn as strips around the content, never over it, so plugin pixels stay undimmed.
    std::array<RECT, 4> strips{};
    Check(HostChromeDimStrips(2560, 720, RECT{0, 0, 1280, 720}, strips.data(), strips.size()) == 1 &&
              strips[0].left == 1280 && strips[0].right == 2560 && strips[0].top == 0 && strips[0].bottom == 720,
          L"a slice at the left edge dims one strip to its right", success);
    Check(HostChromeDimStrips(2560, 720, RECT{640, 0, 1920, 720}, strips.data(), strips.size()) == 2 &&
              strips[0].right == 640 && strips[1].left == 1920,
          L"a centred full-height slice dims one strip on each side", success);
    Check(HostChromeDimStrips(2560, 720, RECT{0, 0, 2560, 720}, strips.data(), strips.size()) == 0,
          L"a full-client slice dims nothing", success);
    Check(HostChromeDimStrips(2560, 720, RECT{100, 100, 200, 200}, strips.data(), strips.size()) == 4 &&
              strips[2].left == 100 && strips[2].right == 200 && strips[2].bottom == 100 && strips[3].top == 200,
          L"an inset rectangle dims four strips that never overlap the content", success);
    Check(HostChromeDimStrips(2560, 720, half.content, strips.data(), 2) == 0 &&
              HostChromeDimStrips(0, 720, half.content, strips.data(), strips.size()) == 0,
          L"dim strips reject a short output array and an empty client", success);
    const size_t halfStrips = HostChromeDimStrips(2560, 720, half.content, strips.data(), strips.size());
    bool stripsOutsideContent = halfStrips > 0;
    for (size_t index = 0; index < halfStrips; ++index)
    {
        stripsOutsideContent = stripsOutsideContent &&
                               (strips[index].right <= half.content.left || strips[index].left >= half.content.right);
    }
    Check(stripsOutsideContent, L"dim strips around the raised half slice stay outside plugin content", success);

    const RECT tiles[] = {{0, 0, 200, 200}, {150, 50, 400, 300}};
    Check(HitTestTopmostWidget(POINT{160, 60}, tiles, 2) == 1 && HitTestTopmostWidget(POINT{10, 10}, tiles, 2) == 0 &&
              HitTestTopmostWidget(POINT{500, 500}, tiles, 2) == SIZE_MAX,
          L"widget hit-test prefers the later overlapping tile", success);

    Check(IsDoubleActivate(1000, POINT{10, 10}, 1300, POINT{12, 11}, 500, 16) &&
              !IsDoubleActivate(1000, POINT{10, 10}, 1600, POINT{12, 11}, 500, 16) &&
              !IsDoubleActivate(1000, POINT{10, 10}, 1100, POINT{40, 10}, 500, 16) &&
              !IsDoubleActivate(1000, POINT{10, 10}, 1100, POINT{12, 11}, 0, 16),
          L"double-activate requires the system interval and slop", success);

    const RaisedLayout fromTile = RaisedLayoutFromTile(tile, 2560, 720, 96);
    Check(fromTile.content.left == tile.left && fromTile.content.top == tile.top &&
              fromTile.content.right == tile.right && fromTile.content.bottom == tile.bottom,
          L"raise settle starts from the tile rectangle", success);
    const RaisedLayout atStart = InterpolateRaisedLayout(fromTile, half, 0.0f);
    const RaisedLayout atEnd = InterpolateRaisedLayout(fromTile, half, 1.0f);
    Check(atStart.content.left == fromTile.content.left && atStart.content.bottom == fromTile.content.bottom &&
              atEnd.content.left == half.content.left && atEnd.content.right == half.content.right &&
              atEnd.content.bottom == half.content.bottom,
          L"raise interpolation preserves start and end layouts", success);
    Check(InterpolateRaisedCoordinate(0, 100, 0.5f) == 88, L"raise interpolation uses ease-out cubic", success);
    Check(InterpolateRaisedAlpha(0, 148, 0.0f) == 0 && InterpolateRaisedAlpha(0, 148, 1.0f) == 148,
          L"dim alpha interpolation preserves start and end", success);
    const UINT shortTravel = RaiseSettleDurationMilliseconds(RECT{0, 0, 10, 10}, RECT{0, 0, 12, 12});
    const UINT longTravel = RaiseSettleDurationMilliseconds(RECT{0, 0, 100, 100}, RECT{0, 0, 2000, 720});
    Check(shortTravel == 160 && longTravel == 240, L"raise settle duration stays within the 160-240 ms window",
          success);
}

void TestWidgetRaiseHost(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] raised overlay host composition\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":5},"pages":[{"name":"System","columns":[{"weight":3,"rows":[{"plugin":"builtin.system-pulse"},{"plugin":"builtin.cpu-meter"},{"plugin":"builtin.memory-meter"}]},{"weight":4,"rows":[{"plugin":"builtin.process-viewer"},{"plugin":"builtin.gpu-processes"}]},{"weight":3,"rows":[{"plugin":"builtin.network-meter"},{"plugin":"builtin.storage-meter"}]},{"weight":3,"rows":[{"plugin":"builtin.gpu-meter"},{"plugin":"builtin.thermal-meter"},{"plugin":"builtin.power-meter"}]}]}]})json";

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

// Host chrome is drawn into the swap chain by the renderer: no chrome HWND, no GDI, one coalesced frame per change.
void TestHostChromeComposition(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] host chrome composition\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":5},"pages":[{"name":"System","columns":[{"weight":3,"rows":[{"plugin":"builtin.system-pulse"},{"plugin":"builtin.cpu-meter"},{"plugin":"builtin.memory-meter"}]},{"weight":4,"rows":[{"plugin":"builtin.process-viewer"},{"plugin":"builtin.gpu-processes"}]},{"weight":3,"rows":[{"plugin":"builtin.network-meter"},{"plugin":"builtin.storage-meter"}]},{"weight":3,"rows":[{"plugin":"builtin.gpu-meter"},{"plugin":"builtin.thermal-meter"},{"plugin":"builtin.power-meter"}]}]}]})json";
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
    Check(SUCCEEDED(result), L"host chrome test initializes the hidden WARP host", success);
    if (FAILED(result))
    {
        dashboard.Shutdown();
        return;
    }
    Check(SUCCEEDED(dashboard.SetWidgetsVisible(true)), L"host chrome test shows the System page", success);
    const HostChromeResources& chrome = renderer.HostChrome();
    Check(chrome.Ready() && chrome.Dpi() == window.Dpi(),
          L"the renderer owns a host chrome pipeline rasterized for the window DPI", success);
    const bool glyphs = chrome.GlyphsAvailable();
    Check(glyphs || chrome.Font() == FluentIcons::IconFont::TextFallback,
          L"glyph rasterization succeeds or reports the text fallback", success);

    result = renderer.Render(0.0f, 0.0f);
    Check(SUCCEEDED(result) && renderer.LastFrameChromeQuadCount() == 0,
          L"a settled page with no raise and no revealed band draws no chrome quad", success);
    Check(!renderer.SetHostChrome(HostChromeState{}), L"pushing an unchanged chrome state reports no change", success);

    const RECT processTile = dashboard.PixelBoundsAt(3, kHostWidth, kHostHeight);
    const RaisedLayout layout =
        MakeRaisedLayout(kHostWidth, kHostHeight, RedXeRaisedExtentHalf, window.Dpi(), &processTile);
    IRedXeRaisedWidget* raised = dashboard.RaisedWidgetAt(3);
    Check(raised && SUCCEEDED(raised->SetRaised(TRUE)) && SUCCEEDED(renderer.SetRaisedOverlay(3, layout.content)),
          L"host chrome test raises Process Viewer to a half slice", success);
    HostChromeState raisedState{};
    raisedState.raised = true;
    raisedState.content = layout.content;
    raisedState.close = layout.close;
    raisedState.shadow = layout.shadow;
    raisedState.dimAlpha = kRaiseOverlayDimAlpha;
    raisedState.closeHovered = true;
    Check(renderer.SetHostChrome(raisedState), L"a raised chrome state is reported as a change", success);
    result = renderer.Render(0.1f, 1.0f / 60.0f);
    const size_t expectedRaised = HostChromeQuadCount(raisedState, kHostWidth, kHostHeight, glyphs);
    Check(SUCCEEDED(result) && renderer.LastFrameChromeQuadCount() == expectedRaised && expectedRaised >= 4,
          L"a raised half slice with close hover draws dim strips, shadow, hover wash, and the close glyph", success);
    Check(renderer.LastFrameWidgetCount() == 11 && renderer.LastFrameSuccessfulWidgetCount() == 11,
          L"host chrome never displaces the tile and raised widget draws", success);
    raisedState.closeHovered = false;
    Check(renderer.SetHostChrome(raisedState), L"clearing close hover is one chrome change", success);
    result = renderer.Render(0.2f, 1.0f / 60.0f);
    Check(SUCCEEDED(result) && renderer.LastFrameChromeQuadCount() + 1 == expectedRaised,
          L"an idle close control drops exactly the hover wash", success);
    raisedState.dimAlpha = 0;
    (void)renderer.SetHostChrome(raisedState);
    result = renderer.Render(0.3f, 1.0f / 60.0f);
    Check(SUCCEEDED(result) &&
              renderer.LastFrameChromeQuadCount() == HostChromeQuadCount(raisedState, kHostWidth, kHostHeight, glyphs),
          L"a raise at zero dim draws only shadow and close", success);

    Check(SUCCEEDED(raised->SetRaised(FALSE)), L"host chrome test restores Process Viewer", success);
    renderer.ClearRaisedOverlay();
    HostChromeState bandState{};
    bandState.bands[1].rect = PageEdgeBandRect(kPageEdgeDirectionNext, kHostWidth, kHostHeight, window.Dpi());
    bandState.bands[1].direction = kPageEdgeDirectionNext;
    bandState.bands[1].revealed = true;
    Check(renderer.SetHostChrome(bandState), L"revealing an edge band is one chrome change", success);
    result = renderer.Render(0.4f, 1.0f / 60.0f);
    Check(SUCCEEDED(result) && renderer.LastFrameChromeQuadCount() == (glyphs ? 2U : 1U) &&
              renderer.LastFrameWidgetCount() == 10,
          L"a revealed edge band draws its wash and chevron over every tile", success);
    Check(renderer.SetHostChrome(HostChromeState{}), L"hiding the band is one chrome change", success);
    result = renderer.Render(0.5f, 1.0f / 60.0f);
    Check(SUCCEEDED(result) && renderer.LastFrameChromeQuadCount() == 0, L"hidden chrome draws nothing", success);

    Check(renderer.SetDpi(192) == S_OK && renderer.HostChrome().Dpi() == 192 &&
              renderer.HostChrome().GlyphsAvailable() == glyphs,
          L"a DPI change re-rasterizes the chrome glyphs once", success);
    Check(renderer.SetDpi(192) == S_OK && renderer.HostChrome().Dpi() == 192,
          L"an unchanged DPI leaves the chrome atlas alone", success);
    Check(GetWindow(window.Get(), GW_CHILD) == nullptr, L"host chrome creates no child window over the swap chain",
          success);
    renderer.Shutdown();
    Check(!renderer.HostChrome().Ready(), L"shutdown releases the host chrome pipeline", success);
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

    // The native-window example is opt-in and absent from the shipped templates, so this test places it itself in the
    // former Debug Development layout: Launcher, then Triangle over GDI Orbit, then a double-width Matrix.
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":5},"pages":[{"columns":[{"plugin":"builtin.launcher","shortcuts":[]},{"rows":[{"plugin":"builtin.rotating-triangle"},{"plugin":"builtin.gdi-orbit"}]},{"weight":2,"widget":{"plugin":"builtin.matrix-rain"}}]}]})json";
    PluginManager plugins;
    AppSettings settings{};
    result = ParseAppSettingsJson(settingsJson, settings);
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
          L"GDI Orbit exposes the raised sibling", success);
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

// Adapter-of-output policy: the device must be created on the GPU whose output scans out the window's monitor. The
// policy is a pure function over flattened DXGI records, so the decision table runs without a display topology.
void TestAdapterSelectionPolicy(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] adapter-of-output selection policy\n";
    const HMONITOR xeneon = reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(0x1001));
    const HMONITOR primary = reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(0x1002));
    const HMONITOR unmapped = reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(0x1003));
    const LUID discrete{0x1AE51, 0};
    const LUID integrated{0x1C3CC, 0};
    const LUID software{0x1C38F, 0};
    const std::array<RedXeAdapterOutputRecord, 4> records{{
        {discrete, primary, false},
        {integrated, xeneon, false},
        {software, xeneon, true},
        {software, primary, true},
    }};
    Check(RedXeSelectAdapterRecordForMonitor(records, xeneon) == 1,
          L"the hardware adapter whose output owns the window's monitor is selected", success);
    Check(RedXeSelectAdapterRecordForMonitor(records, primary) == 0,
          L"a monitor on the other adapter selects that adapter", success);
    Check(RedXeSelectAdapterRecordForMonitor(records, nullptr) == SIZE_MAX,
          L"a window on no monitor keeps the default adapter", success);
    Check(RedXeSelectAdapterRecordForMonitor(records, unmapped) == SIZE_MAX,
          L"a monitor DXGI cannot map keeps the default adapter", success);
    const std::array<RedXeAdapterOutputRecord, 1> softwareOnly{{{software, xeneon, true}}};
    Check(RedXeSelectAdapterRecordForMonitor(softwareOnly, xeneon) == SIZE_MAX,
          L"a software adapter never owns a monitor", success);
    Check(RedXeSelectAdapterRecordForMonitor({}, xeneon) == SIZE_MAX, L"no outputs selects nothing", success);
    Check(RedXeSameLuid(discrete, discrete) && !RedXeSameLuid(discrete, integrated) &&
              !RedXeSameLuid(LUID{1, 2}, LUID{1, 3}),
          L"adapter identity compares both LUID halves", success);
}

// Reads one pixel of a PNG through WIC (0xAARRGGBB) so a capture can be checked without a second decoder.
[[nodiscard]] HRESULT ReadPngPixel(const wchar_t* path, UINT x, UINT y, UINT& width, UINT& height,
                                   uint32_t& pixel) noexcept
{
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    HRESULT result =
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
    wil::com_ptr_nothrow<IWICBitmapDecoder> decoder;
    if (SUCCEEDED(result))
    {
        result = factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                    decoder.put());
    }
    wil::com_ptr_nothrow<IWICBitmapFrameDecode> frame;
    if (SUCCEEDED(result))
    {
        result = decoder->GetFrame(0, frame.put());
    }
    wil::com_ptr_nothrow<IWICFormatConverter> converter;
    if (SUCCEEDED(result))
    {
        result = factory->CreateFormatConverter(converter.put());
    }
    if (SUCCEEDED(result))
    {
        result = converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                       WICBitmapPaletteTypeCustom);
    }
    if (SUCCEEDED(result))
    {
        result = converter->GetSize(&width, &height);
    }
    if (SUCCEEDED(result))
    {
        if (x >= width || y >= height)
        {
            return E_INVALIDARG;
        }
        const WICRect rect{static_cast<INT>(x), static_cast<INT>(y), 1, 1};
        result = converter->CopyPixels(&rect, 4, 4, reinterpret_cast<BYTE*>(&pixel));
    }
    return result;
}

// Windows.Graphics.Capture of a window this process owns, the path `--screenshot` and documentation captures use:
// a visible off-screen popup painted one solid color captures at its size with that color, hidden and foreign
// windows are refused, and a client-space crop yields exactly the requested rectangle.
void TestWindowCapture(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] window capture through Windows.Graphics.Capture\n";
    constexpr wchar_t kClassName[] = L"RedXe.CaptureTest";
    constexpr COLORREF kFill = RGB(0x20, 0x90, 0xE0);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = [](HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept -> LRESULT
    {
        if (message == WM_PAINT)
        {
            PAINTSTRUCT paint{};
            const HDC context = BeginPaint(window, &paint);
            const HBRUSH brush = CreateSolidBrush(kFill);
            RECT client{};
            GetClientRect(window, &client);
            FillRect(context, &client, brush);
            DeleteObject(brush);
            EndPaint(window, &paint);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    };
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kClassName;
    (void)RegisterClassExW(&windowClass);
    constexpr int kWidth = 96;
    constexpr int kHeight = 64;
    // On-screen for the few milliseconds of the capture (DWM composes nothing for an off-screen window), but never
    // activated and never on the taskbar, so no focus or desktop state changes.
    wil::unique_hwnd window{CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kClassName, L"RedXe capture test",
                                            WS_POPUP, 0, 0, kWidth, kHeight, nullptr, nullptr,
                                            GetModuleHandleW(nullptr), nullptr)};
    Check(window != nullptr, L"capture test window created", success);
    if (!window)
    {
        return;
    }
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / L"RedXeCaptureTest";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path full = directory / L"full.png";
    const std::filesystem::path cropped = directory / L"cropped.png";
    Check(RedXe::SaveWindowScreenshot(window.get(), full.c_str()) == E_INVALIDARG, L"a hidden window is refused",
          success);
    ShowWindow(window.get(), SW_SHOWNOACTIVATE);
    UpdateWindow(window.get());
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        DispatchMessageW(&message);
    }
    Check(RedXe::SaveWindowScreenshot(GetDesktopWindow(), full.c_str()) == E_INVALIDARG,
          L"a window of another process is refused", success);
    // A host that cannot capture at all (no Windows.Graphics.Capture, no hardware Direct3D 11 device, or a session
    // whose compositor exposes no surface for the window: the GPU-less CI images) is reported and skipped; the
    // argument checks above and the crop-bounds check below still run, because the product validates them first.
    SIZE surface{};
    const HRESULT support = RedXe::QueryWindowCaptureSupport(window.get(), &surface);
    if (FAILED(support) || surface.cx <= 0 || surface.cy <= 0)
    {
        std::wcout << L"[  SKIPPED ] window capture is unavailable on this host (support HRESULT 0x" << std::hex
                   << std::uppercase << static_cast<unsigned long>(support) << std::dec << std::nouppercase
                   << L", surface " << surface.cx << L'x' << surface.cy << L")\n";
    }
    else
    {
        HRESULT result = RedXe::SaveWindowScreenshot(window.get(), full.c_str());
        if (FAILED(result))
        {
            std::wcerr << L"SaveWindowScreenshot failed with HRESULT 0x" << std::hex << std::uppercase
                       << static_cast<unsigned long>(result) << std::dec << std::nouppercase << L'\n';
        }
        Check(SUCCEEDED(result), L"the visible window captures", success);
        UINT width = 0;
        UINT height = 0;
        uint32_t pixel = 0;
        if (SUCCEEDED(result))
        {
            result = ReadPngPixel(full.c_str(), kWidth / 2, kHeight / 2, width, height, pixel);
            Check(SUCCEEDED(result) && width == kWidth && height == kHeight, L"the capture has the window's size",
                  success);
            Check((pixel & 0x00FFFFFFU) == 0x002090E0U, L"the capture holds the painted color", success);
        }
        const RECT crop{8, 4, 40, 20};
        result = RedXe::SaveWindowScreenshot(window.get(), cropped.c_str(), &crop);
        Check(SUCCEEDED(result), L"a client-space crop captures", success);
        if (SUCCEEDED(result))
        {
            result = ReadPngPixel(cropped.c_str(), 0, 0, width, height, pixel);
            Check(SUCCEEDED(result) && width == 32 && height == 16 && (pixel & 0x00FFFFFFU) == 0x002090E0U,
                  L"the crop is 32x16 of the painted color", success);
        }
    }
    const RECT outside{kWidth + 10, kHeight + 10, kWidth + 20, kHeight + 20};
    Check(RedXe::SaveWindowScreenshot(window.get(), cropped.c_str(), &outside) == E_INVALIDARG,
          L"a crop outside the client area is refused", success);
    window.reset();
    std::filesystem::remove_all(directory, error);
    (void)UnregisterClassW(kClassName, GetModuleHandleW(nullptr));
}

// The production renderer on the hidden WARP host: device identity is reported, the swap chain owns a frame-latency
// waitable object (maximum latency one) that is signaled before the first frame and again after a presented frame,
// and the adapter re-check is a no-op on WARP and on a window that sits on no monitor.
void TestRendererDeviceIdentity(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] renderer device identity and frame-latency waitable object\n";
    AttachedHostWindow window;
    HRESULT result = window.Initialize(kHostWidth, kHostHeight);
    Check(SUCCEEDED(result), L"hidden host window initializes for the device identity test", success);
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
    Check(SUCCEEDED(result), L"Release composition initializes for the device identity test", success);
    if (FAILED(result))
    {
        return;
    }
    DashboardHost dashboard;
    result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    Check(SUCCEEDED(result), L"dashboard initializes for the device identity test", success);
    if (FAILED(result))
    {
        return;
    }
    Renderer renderer;
    Check(renderer.FrameLatencyWaitableObject() == nullptr, L"no waitable object exists before device creation",
          success);
    result = renderer.Initialize(window.Get(), true, dashboard);
    Check(SUCCEEDED(result), L"renderer initializes on WARP for the device identity test", success);
    if (FAILED(result))
    {
        dashboard.Shutdown();
        return;
    }
    const Renderer::DeviceIdentity identity = renderer.DeviceInfo();
    Check(identity.warp && !identity.adapterOwnsWindowMonitor,
          L"forced WARP reports a software device that owns no window monitor", success);
    Check(identity.adapterName[0] != L'\0', L"the device identity carries the adapter name", success);
    const HANDLE waitable = renderer.FrameLatencyWaitableObject();
    Check(waitable != nullptr, L"the swap chain exposes its frame-latency waitable object", success);
    Check(waitable && WaitForSingleObject(waitable, 0) == WAIT_OBJECT_0,
          L"a free back buffer is signaled before the first frame", success);
    Check(renderer.EnsureDeviceForWindowMonitor() == S_FALSE, L"the adapter re-check keeps a forced WARP device",
          success);
    result = renderer.Render(0.0f, 0.0f);
    Check(SUCCEEDED(result), L"hidden host presents one frame through the waitable swap chain", success);
    Check(waitable && WaitForSingleObject(waitable, 2000) == WAIT_OBJECT_0,
          L"the waitable object signals again once the presented frame is consumed", success);
    Check(renderer.FrameLatencyWaitableObject() == waitable, L"the waitable object is stable across frames", success);
    result = dashboard.Resize(kHostWidth / 2, kHostHeight, window.Dpi());
    if (SUCCEEDED(result))
    {
        result = renderer.Resize(kHostWidth / 2, kHostHeight);
    }
    if (SUCCEEDED(result))
    {
        result = renderer.Render(1.0f, 1.0f / 60.0f);
    }
    Check(SUCCEEDED(result) && renderer.FrameLatencyWaitableObject() != nullptr,
          L"resizing keeps the waitable swap chain flag", success);
    renderer.Shutdown();
    Check(renderer.FrameLatencyWaitableObject() == nullptr && !renderer.DeviceInfo().warp,
          L"shutdown releases the waitable object and clears the device identity", success);
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
        Check(GetModuleHandleW(L"ProcessViewer.dll") != nullptr,
              L"Release static discovery maps the process viewer DLL without creating its page", success);
        Check(GetModuleHandleW(L"StudioClock.dll") != nullptr,
              L"Release static discovery maps the Studio Clock DLL without creating its page", success);
        Check(GetModuleHandleW(L"DeskClock.dll") != nullptr,
              L"Release static discovery maps the Desk Clock DLL without creating its page", success);
        Check(GetModuleHandleW(L"Launcher.dll") != nullptr,
              L"Release static discovery maps the gallery launcher DLL without creating its page", success);
        Check(GetModuleHandleW(L"Weather.dll") != nullptr,
              L"Release static discovery maps the gallery weather DLL without creating its page", success);
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
            R"json({"seed":2000,"glyphHeightDips":18,"densityPercent":80,"speedPercent":100,"trailLengthGlyphs":18,"mutationPerSecond":8,"headColor":"#F6FFF6","trailColor":"#33FF33","glowPercent":35})json";
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
        R"json({"version":{"major":5},"pages":[{"name":"Clock","widgets":[{"plugin":"builtin.studio-clock"}]}]})json";
    constexpr std::string_view changedConfiguration =
        R"json({"showSecondProgress":true,"externalDotsAlwaysOn":true,"showSeconds":true,"secondsColor":"#00EE44","showDate":true,"dateFormat":"yyyy-mm-dd","timeColor":"#E0E0FF"})json";

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
        R"json({"version":{"major":5},"pages":[{"name":"Clock","widgets":[{"plugin":"builtin.desk-clock"}]}]})json";
    constexpr std::string_view changedConfiguration =
        R"json({"flipDurationMilliseconds":300,"cardColor":"#D02030","digitColor":"#F0F0FF","dateColor":"#C0C0D0"})json";

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
            R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.process-viewer"},{"plugin":"builtin.process-viewer"}]}]})json";
        result = ParseAppSettingsJson(processViewerComposition, settings);
    }
    Check(SUCCEEDED(result), L"Process Viewer isolated composition is parsed", success);
    if (FAILED(result))
    {
        return;
    }

    // Diagnostics survive earlier plugin-manager instances in this process.
    // Compare delivery with the pre-creation count, including on the repeated run.
    ProcessViewerTestDiagnostics beforeCreation{};
    if (GetProcessViewerDiagnosticsFunction())
    {
        result = ReadProcessViewerDiagnostics(beforeCreation);
        Check(SUCCEEDED(result), L"Process Viewer pre-creation diagnostics resolve", success);
        if (FAILED(result))
        {
            return;
        }
    }
    ProcessViewerGetTestDiagnosticsFn getDiagnostics = nullptr;
    {
        PluginManager plugins;
        result = plugins.Initialize(settings);
        Check(SUCCEEDED(result) && plugins.ProviderCount() == 1 && plugins.WidgetCount() == 2 &&
                  plugins.GpuWidgetAt(0) != nullptr && plugins.GpuWidgetAt(1) != nullptr &&
                  plugins.ScheduledWidgetAt(0) != nullptr && plugins.ScheduledWidgetAt(1) != nullptr &&
                  plugins.InteractiveWidgetAt(0) != nullptr && plugins.InteractiveWidgetAt(1) != nullptr &&
                  plugins.WindowWidgetAt(0) == nullptr && plugins.WindowWidgetAt(1) == nullptr,
              L"two Process Viewers share one widget provider as GPU scheduled interactive widgets", success);
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
                  diagnostics.sampleCount == beforeCreation.sampleCount,
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
            if (FAILED(result) ||
                (diagnostics.sampleCount > beforeCreation.sampleCount && diagnostics.lastPublishedRowCount > 0))
            {
                break;
            }
            (void)MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        Check(SUCCEEDED(result) && diagnostics.sampleCount > beforeCreation.sampleCount &&
                  diagnostics.lastPublishedRowCount > 0 &&
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
        R"json({"version":{"major":5},"declare":{"Processes":{"plugin":"builtin.process-viewer","topN":32}},"pages":[{"name":"System","columns":[{"weight":3,"rows":[{"plugin":"builtin.system-pulse"},{"plugin":"builtin.cpu-meter"},{"plugin":"builtin.memory-meter"}]},{"weight":4,"rows":["Processes",{"plugin":"builtin.gpu-processes"}]},{"weight":3,"rows":[{"plugin":"builtin.network-meter"},{"plugin":"builtin.storage-meter"}]},{"weight":3,"rows":[{"plugin":"builtin.gpu-meter"},{"plugin":"builtin.thermal-meter"},{"plugin":"builtin.power-meter"}]}]}]})json";

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
    // topN 32 makes Process Viewer overflow its half-height tile, which the page-control checks below rely on.
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

    // A horizontal wheel sample is never used by a viewer: every one declines it for host page navigation.
    bool declinedHorizontal = true;
    for (size_t index = 0; index < plugins.WidgetCount(); ++index)
    {
        IRedXeInteractiveWidget* interactive = plugins.InteractiveWidgetAt(index);
        if (!interactive)
        {
            continue;
        }
        RedXePointerEvent wheel{sizeof(RedXePointerEvent),        1,    RedXePointerKindMouse,
                                RedXePointerPhaseHorizontalWheel, 8.0f, 8.0f};
        wheel.wheelDelta = -120.0f;
        declinedHorizontal = declinedHorizontal && interactive->OnPointer(&wheel) == S_FALSE;
    }
    Check(declinedHorizontal, L"System Data viewers decline every horizontal wheel sample", success);

    // Process Viewer (slot 3, topN 32) overflows its half-height tile once its 32-row snapshot has arrived (the
    // sample counter above is process-wide, so wait for this widget's rows), then draws the shared page control;
    // tapping its second dot moves to page two and the next frame draws that page selected.
    const ULONGLONG rowsDeadline = GetTickCount64() + 8000;
    result = ReadProcessViewerDiagnostics(diagnostics);
    while (SUCCEEDED(result) && diagnostics.lastPublishedRowCount < 32 && GetTickCount64() < rowsDeadline)
    {
        window.PumpMessages();
        (void)MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        result = ReadProcessViewerDiagnostics(diagnostics);
    }
    for (uint32_t frame = 0; frame < 2 && SUCCEEDED(result); ++frame)
    {
        result = renderer.Render(0.8f + static_cast<float>(frame) / 60.0f, 1.0f / 60.0f);
    }
    if (SUCCEEDED(result))
    {
        result = ReadProcessViewerDiagnostics(diagnostics);
    }
    std::wcout << L"[       -- ] Process Viewer topN " << diagnostics.configuredTopN << L" rows "
               << diagnostics.lastPublishedRowCount << L" pages " << diagnostics.pageCount << L" dots at ("
               << diagnostics.pageDotFirstX << L", " << diagnostics.pageDotY << L") gap " << diagnostics.pageDotGap
               << L'\n';
    Check(SUCCEEDED(result) && diagnostics.lastPublishedRowCount == 32 && diagnostics.pageCount > 1 &&
              diagnostics.pageDotGap > 0.0f && diagnostics.pageIndex == 0,
          L"an overflowing Process Viewer draws page-control dots on its first page", success);
    if (IRedXeInteractiveWidget* processViewer = plugins.InteractiveWidgetAt(3);
        SUCCEEDED(result) && processViewer && diagnostics.pageCount > 1 && diagnostics.pageDotGap > 0.0f)
    {
        const RECT tile = dashboard.PixelBoundsAt(3, kHostWidth, kHostHeight);
        RedXePointerEvent tap{sizeof(RedXePointerEvent),
                              7,
                              RedXePointerKindTouch,
                              RedXePointerPhaseDown,
                              diagnostics.pageDotFirstX + diagnostics.pageDotGap,
                              diagnostics.pageDotY,
                              0,
                              static_cast<uint32_t>(tile.right - tile.left),
                              static_cast<uint32_t>(tile.bottom - tile.top),
                              window.Dpi()};
        const HRESULT down = processViewer->OnPointer(&tap);
        tap.phase = RedXePointerPhaseUp;
        const HRESULT up = processViewer->OnPointer(&tap);
        HRESULT rendered = S_OK;
        for (uint32_t frame = 0; frame < 2 && SUCCEEDED(rendered); ++frame)
        {
            rendered = renderer.Render(1.0f + static_cast<float>(frame) / 60.0f, 1.0f / 60.0f);
        }
        result = ReadProcessViewerDiagnostics(diagnostics);
        Check(down == S_FALSE && up == S_OK && SUCCEEDED(rendered) && SUCCEEDED(result) && diagnostics.pageIndex == 1,
              L"a tap on the second page dot pages the Process Viewer forward (Down stays S_FALSE for raise)", success);
        tap.phase = RedXePointerPhaseDown;
        tap.x = diagnostics.pageDotFirstX;
        (void)processViewer->OnPointer(&tap);
        tap.phase = RedXePointerPhaseUp;
        const HRESULT back = processViewer->OnPointer(&tap);
        rendered = renderer.Render(1.1f, 1.0f / 60.0f);
        result = ReadProcessViewerDiagnostics(diagnostics);
        Check(back == S_OK && SUCCEEDED(rendered) && SUCCEEDED(result) && diagnostics.pageIndex == 0,
              L"a tap on the first page dot returns to page one", success);

        // With a page control showing, the wheel stays with the viewer even at its first page: consumed, nothing
        // moves, and the dashboard never changes page under it. Wheel-down then pages it; wheel-up at page one again.
        RedXePointerEvent wheel{sizeof(RedXePointerEvent), 1,    RedXePointerKindMouse,
                                RedXePointerPhaseWheel,    8.0f, 8.0f};
        wheel.wheelDelta = 120.0f;
        const HRESULT upAtFirst = processViewer->OnPointer(&wheel);
        wheel.wheelDelta = -120.0f;
        const HRESULT wheelDown = processViewer->OnPointer(&wheel);
        rendered = renderer.Render(1.2f, 1.0f / 60.0f);
        result = ReadProcessViewerDiagnostics(diagnostics);
        Check(upAtFirst == S_OK && wheelDown == S_OK && SUCCEEDED(rendered) && SUCCEEDED(result) &&
                  diagnostics.pageIndex == 1,
              L"a paged viewer keeps wheel-up at its first page and pages forward on wheel-down", success);
        wheel.wheelDelta = 120.0f;
        (void)processViewer->OnPointer(&wheel);
        rendered = renderer.Render(1.3f, 1.0f / 60.0f);
        result = ReadProcessViewerDiagnostics(diagnostics);
        Check(SUCCEEDED(rendered) && SUCCEEDED(result) && diagnostics.pageIndex == 0,
              L"wheel-up returns the viewer to its first page", success);
    }

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
    Check(SUCCEEDED(result) && plugins.ProviderCount() == 3 && plugins.WidgetCount() == 3,
          L"Debug composition constructs launcher, triangle, and Matrix on the first page", success);
    if (FAILED(result))
    {
        return;
    }

    DashboardHost dashboard;
    result = dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false);
    Check(SUCCEEDED(result), L"Debug dashboard attaches the GPU widgets while hidden", success);
    Check(dashboard.GpuWidgetAt(0) != nullptr && dashboard.GpuWidgetAt(1) != nullptr &&
              dashboard.GpuWidgetAt(2) != nullptr && dashboard.InteractiveWidgetAt(0) != nullptr &&
              !dashboard.HasWindowWidgets(),
          L"Debug dashboard exposes launcher, triangle, and Matrix GPU mechanisms and no native container", success);
    Check(dashboard.PlacementAt(0) == WidgetPlacement{0.0f, 0.0f, 640.0f, 720.0f} &&
              dashboard.PlacementAt(1) == WidgetPlacement{640.0f, 0.0f, 640.0f, 720.0f} &&
              dashboard.PlacementAt(2) == WidgetPlacement{1280.0f, 0.0f, 1280.0f, 720.0f},
          L"Debug dashboard compiles the adaptive layout onto the design canvas", success);
    // No shipped page places a native-window widget, so nothing sits over the swap chain and every shipped page
    // can present with independent flip.
    Check(GetWindow(window.Get(), GW_CHILD) == nullptr,
          L"the shipped Debug first page creates no child HWND over the swap chain", success);
    if (FAILED(result))
    {
        return;
    }

    result = dashboard.SetWidgetsVisible(true);
    if (SUCCEEDED(result))
    {
        result = dashboard.SetWidgetsVisible(false);
    }
    Check(SUCCEEDED(result), L"production host propagates widget resume and quiesce transitions", success);

    Renderer renderer;
    result = renderer.Initialize(window.Get(), true, dashboard);
    if (SUCCEEDED(result))
    {
        result = renderer.Render(0.5f, 1.0f / 60.0f);
    }
    Check(SUCCEEDED(result) && renderer.LastFrameWidgetCount() == 3 && renderer.LastFrameSuccessfulWidgetCount() == 3,
          L"Debug WARP frame renders every GPU widget of the first page", success);

    renderer.Shutdown();
    dashboard.Shutdown();
    window.PumpMessages();
}

class PausedDataSink final : public RedXeComObject<PausedDataSink, IRedXeDataSink>
{
  public:
    wil::unique_event_nothrow entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_event_nothrow resume{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<bool> timedOut{false};

    HRESULT STDMETHODCALLTYPE OnDataSnapshot(const RedXeDataSnapshot*) noexcept override
    {
        callbacks.fetch_add(1, std::memory_order_relaxed);
        SetEvent(entered.get());
        if (WaitForSingleObject(resume.get(), 5000) != WAIT_OBJECT_0)
            timedOut.store(true, std::memory_order_relaxed);
        return S_OK;
    }
};

void TestSubscriptionDrain(bool& success)
{
    std::wcout << L"[ RUN      ] subscription callback drain and slot reuse\n";
    PluginHost host;
    wil::com_ptr_nothrow<IRedXeDataProvider> provider;
    HRESULT result = host.GetDataProvider("builtin.system-data", provider.put());
    Check(SUCCEEDED(result), L"the production data provider is available for drain tests", success);
    if (FAILED(result))
        return;
    for (bool release : {false, true, false, true})
    {
        wil::com_ptr_nothrow<PausedDataSink> sink;
        sink.attach(new PausedDataSink());
        wil::unique_event_nothrow started{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        wil::unique_event_nothrow returned{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        if (!sink->entered || !sink->resume || !started || !returned)
        {
            Check(false, L"callback barriers can be created", success);
            return;
        }
        const auto unblock = wil::scope_exit([&]() noexcept { SetEvent(sink->resume.get()); });
        wil::com_ptr_nothrow<IRedXeDataSubscription> subscription;
        const RedXeDataSubscriptionOptions options{sizeof(options), "cpu.summary", 1000};
        result = provider->Subscribe(&options, sink.get(), subscription.put());
        if (SUCCEEDED(result))
            result = subscription->SetActive(TRUE);
        const bool entered = SUCCEEDED(result) && WaitForSingleObject(sink->entered.get(), 5000) == WAIT_OBJECT_0;
        Check(entered, L"the worker enters a controlled callback", success);
        if (!entered)
            return;
        HRESULT operationResult = S_OK;
        std::jthread operation(
            [&]()
            {
                SetEvent(started.get());
                if (release)
                    subscription.reset();
                else
                    operationResult = subscription->SetActive(FALSE);
                SetEvent(returned.get());
            });
        Check(WaitForSingleObject(started.get(), 2000) == WAIT_OBJECT_0,
              L"the drain operation starts on a separate thread", success);
        Check(WaitForSingleObject(returned.get(), 150) == WAIT_TIMEOUT,
              release ? L"Release waits for the in-flight callback"
                      : L"SetActive(FALSE) waits for the in-flight callback",
              success);
        SetEvent(sink->resume.get());
        Check(WaitForSingleObject(returned.get(), 2000) == WAIT_OBJECT_0,
              L"draining completes promptly after the callback returns", success);
        operation.join();
        Check(SUCCEEDED(operationResult) && !sink->timedOut.load() && sink->callbacks.load() == 1,
              L"the callback completes normally and the drain succeeds", success);
        if (subscription)
        {
            Check(subscription->SetActive(FALSE) == S_OK, L"repeated deactivation is idempotent", success);
            ResetEvent(sink->entered.get());
            Check(subscription->SetActive(TRUE) == S_OK &&
                      WaitForSingleObject(sink->entered.get(), 2500) == WAIT_OBJECT_0,
                  L"a drained subscription can resume delivery", success);
            Check(subscription->SetActive(FALSE) == S_OK, L"reactivated delivery drains again", success);
        }
    }
}

void TestReservedSubscriptionDrain(bool& success)
{
    std::wcout << L"[ RUN      ] reserved subscription callback drain\n";
    PluginHost host;
    wil::com_ptr_nothrow<IRedXeDataProvider> provider;
    if (FAILED(host.GetDataProvider("builtin.system-data", provider.put())))
    {
        Check(false, L"the provider initializes for the reservation test", success);
        return;
    }
    std::array<wil::com_ptr_nothrow<PausedDataSink>, 3> sinks;
    for (auto& sink : sinks)
        sink.attach(new PausedDataSink());
    std::array<wil::com_ptr_nothrow<IRedXeDataSubscription>, 3> subscriptions;
    // The first gate holds one worker pass while both test subscriptions become active for the next pass.
    const auto unblock = wil::scope_exit(
        [&]() noexcept
        {
            for (auto& sink : sinks)
                SetEvent(sink->resume.get());
        });
    const RedXeDataSubscriptionOptions options{sizeof(options), "cpu.summary", 1000};
    for (size_t index = 0; index < sinks.size(); ++index)
    {
        if (!sinks[index]->entered || !sinks[index]->resume ||
            FAILED(provider->Subscribe(&options, sinks[index].get(), subscriptions[index].put())) ||
            FAILED(subscriptions[index]->SetActive(TRUE)))
        {
            Check(false, L"all reservation test subscriptions activate", success);
            return;
        }
        if (index == 0 && WaitForSingleObject(sinks[0]->entered.get(), 2500) != WAIT_OBJECT_0)
        {
            Check(false, L"the initial worker pass enters the setup gate", success);
            return;
        }
    }
    SetEvent(sinks[0]->resume.get());
    (void)subscriptions[0]->SetActive(FALSE);
    if (WaitForSingleObject(sinks[1]->entered.get(), 2500) != WAIT_OBJECT_0)
    {
        Check(false, L"the next worker pass reserves both test callbacks", success);
        return;
    }
    wil::unique_event_nothrow started{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_event_nothrow returned{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!started || !returned)
    {
        Check(false, L"the reservation operation barriers initialize", success);
        return;
    }
    HRESULT drainResult = E_PENDING;
    SetEvent(sinks[2]->resume.get());
    std::jthread operation(
        [&]()
        {
            SetEvent(started.get());
            drainResult = subscriptions[2]->SetActive(FALSE);
            SetEvent(returned.get());
        });
    Check(WaitForSingleObject(started.get(), 2000) == WAIT_OBJECT_0 &&
              WaitForSingleObject(returned.get(), 150) == WAIT_TIMEOUT &&
              WaitForSingleObject(sinks[2]->entered.get(), 0) == WAIT_TIMEOUT,
          L"deactivation waits even when the copied callback has not entered yet", success);
    SetEvent(sinks[1]->resume.get());
    operation.join();
    (void)subscriptions[1]->SetActive(FALSE);
    Check(drainResult == S_OK && sinks[2]->callbacks.load() == 1 && !sinks[1]->timedOut.load() &&
              !sinks[2]->timedOut.load(),
          L"the reserved callback completes before deactivation returns", success);
}

void TestGpuTargetSizeNotification(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] GPU widget target-size notification\n";
    // Desk Clock is the widget whose resources depend on how highTier it is drawn: its glyph atlas is rasterized at a
    // resolution tier chosen from the reported target size. That makes it the honest end-to-end probe for the
    // callback -- a no-op implementation would leave the tier stuck at 1.
    constexpr std::string_view settingsJson = R"json({"version":{"major":5},"pages":[{"widgets":[)json"
                                              R"json({"plugin":"builtin.desk-clock"}]}]})json";

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

    const UINT changedDpi = window.Dpi() == 144 ? 192 : 144;
    result = renderer.SetDpi(changedDpi);
    DeskClockTestDiagnostics afterDpi{};
    Check(SUCCEEDED(result) && read(afterDpi) && afterDpi.targetSizeChanges == beforeFrames + 1,
          L"a DPI-only change reports exactly one target notification", success);
    result = renderer.SetDpi(changedDpi);
    DeskClockTestDiagnostics repeatedDpi{};
    Check(SUCCEEDED(result) && read(repeatedDpi) && repeatedDpi.targetSizeChanges == afterDpi.targetSizeChanges,
          L"an unchanged DPI does not repeat the notification", success);

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

void TestGpuPageLifetime(bool& success)
{
    std::wcout << L"[ RUN      ] GPU page lifetime and target notification identity\n";
    constexpr std::string_view page = R"({"widgets":[)"
                                      R"({"plugin":"builtin.cpu-meter"},)"
                                      R"({"plugin":"builtin.weather"},)"
                                      R"({"plugin":"builtin.desk-clock"}]})";
    const std::string json = std::string(R"({"version":{"major":5},"pages":[)") + std::string(page) + "," +
                             std::string(page) + "," + std::string(page) + "]}";
    AppSettings settings;
    AttachedHostWindow window;
    HRESULT result = ParseAppSettingsJson(json, settings);
    if (SUCCEEDED(result))
        result = window.Initialize(kHostWidth, kHostHeight);
    std::array<PluginManager, 3> managers;
    std::array<DashboardHost, 3> dashboards;
    for (size_t index = 0; index < dashboards.size() && SUCCEEDED(result); ++index)
    {
        if (index != 0)
            result = MoveDashboardPage(settings, 1);
        if (SUCCEEDED(result))
            result = managers[index].Initialize(settings);
        if (SUCCEEDED(result))
            result = dashboards[index].Initialize(managers[index], window.Get(), kHostWidth, kHostHeight, window.Dpi(),
                                                  true);
    }
    Check(SUCCEEDED(result), L"three pages with data, network, and sized GPU widgets construct", success);
    if (FAILED(result))
        return;
    const auto viewer = ResolveFunction<ProcessViewerGetTestDiagnosticsFn>(GetModuleHandleW(L"ProcessViewer.dll"),
                                                                           kProcessViewerGetTestDiagnosticsExport);
    const auto weather = ResolveFunction<WeatherGetTestDiagnosticsFn>(GetModuleHandleW(L"Weather.dll"),
                                                                      kWeatherGetTestDiagnosticsExport);
    const auto clock = ResolveFunction<DeskClockGetTestDiagnosticsFn>(GetModuleHandleW(L"DeskClock.dll"),
                                                                      kDeskClockGetTestDiagnosticsExport);
    Check(viewer && weather && clock, L"page lifetime diagnostics are available", success);
    if (!viewer || !weather || !clock)
        return;
    ProcessViewerTestDiagnostics viewerBefore{sizeof(viewerBefore)};
    WeatherTestDiagnostics weatherBefore{sizeof(weatherBefore)};
    (void)viewer(&viewerBefore);
    (void)weather(&weatherBefore);
    Renderer renderer;
    result = renderer.Initialize(window.Get(), true, dashboards[0]);
    Check(SUCCEEDED(result) && dashboards[0].WidgetsVisible(), L"device setup restores a visible primary page",
          success);
    if (FAILED(result))
        return;
    result = renderer.SetTransitionDashboard(&dashboards[1]);
    DeskClockTestDiagnostics firstStage{sizeof(firstStage)};
    (void)clock(&firstStage);
    Check(SUCCEEDED(result) && dashboards[1].WidgetsVisible(), L"device setup restores a visible staged page", success);
    result = renderer.SetTransitionDashboard(&dashboards[2]);
    DeskClockTestDiagnostics replacement{sizeof(replacement)};
    (void)clock(&replacement);
    Check(SUCCEEDED(result) && !dashboards[1].WidgetsVisible() &&
              replacement.targetSizeChanges == firstStage.targetSizeChanges + 1,
          L"replacing a same-sized staged page hides the old page and notifies the new widget", success);
    result = renderer.AdoptPrimaryDashboard(dashboards[2]);
    DeskClockTestDiagnostics promoted{sizeof(promoted)};
    (void)clock(&promoted);
    Check(SUCCEEDED(result) && !dashboards[0].WidgetsVisible() && dashboards[2].WidgetsVisible() &&
              promoted.targetSizeChanges == replacement.targetSizeChanges,
          L"promotion retains the staged widget's size cache and hides the outgoing page", success);
    (void)dashboards[1].SetWidgetsVisible(true);
    result = renderer.AdoptPrimaryDashboard(dashboards[1]);
    Check(SUCCEEDED(result) && dashboards[1].WidgetsVisible() && !dashboards[2].WidgetsVisible(),
          L"adopting an unprepared page quiesces it for device creation and restores visibility", success);
    result = renderer.Render(0.0f, 0.0f);
    Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 3, L"the adopted page draws every widget",
          success);
    renderer.Shutdown();
    Check(!dashboards[1].WidgetsVisible(), L"renderer shutdown quiesces the page before releasing GPU resources",
          success);
    // Registering the staged page before Initialize exercises the same two-page setup ordering as device recovery:
    // NotifyDeviceCreated must prepare both pages before CreateRenderTarget has assigned physical dimensions.
    result = renderer.SetTransitionDashboard(&dashboards[0]);
    if (SUCCEEDED(result))
        result = renderer.Initialize(window.Get(), true, dashboards[1]);
    if (SUCCEEDED(result))
        result = dashboards[1].SetWidgetsVisible(true);
    if (SUCCEEDED(result))
        result = dashboards[0].SetWidgetsVisible(true);
    if (SUCCEEDED(result))
        result = renderer.Render(0.1f, 0.1f);
    Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 6,
          L"a fresh WARP device rebuilds and draws both surviving pages", success);
    renderer.Shutdown();
    Check(!dashboards[0].WidgetsVisible() && !dashboards[1].WidgetsVisible(),
          L"shutdown drains both the primary and staged pages", success);
    ProcessViewerTestDiagnostics viewerAfter{sizeof(viewerAfter)};
    WeatherTestDiagnostics weatherAfter{sizeof(weatherAfter)};
    Check(SUCCEEDED(viewer(&viewerAfter)) && SUCCEEDED(weather(&weatherAfter)) &&
              viewerAfter.deviceCallbacksWhileVisible == viewerBefore.deviceCallbacksWhileVisible &&
              weatherAfter.deviceCallbacksWhileVisible == weatherBefore.deviceCallbacksWhileVisible,
          L"all data and network widget device callbacks run while the widget is quiescent", success);
}

void TestSharedPluginRuntime(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] one process plugin runtime across current and staged pages\n";
    // Two pages that both place a System Data viewer. Before the runtime became process scoped, staging the adjacent
    // page built a second PluginHost with its own module map, its own IRedXeDataSource, and its own acquisition
    // thread beside the current one.
    constexpr std::string_view settingsJson = R"json({"version":{"major":5},"pages":[)json"
                                              R"json({"widgets":[{"plugin":"builtin.cpu-meter"}]},)json"
                                              R"json({"widgets":[{"plugin":"builtin.memory-meter"}]}]})json";

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

void TestWidgetSettingsPersist(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] widget settings persist and collect\n";
    struct Capture final
    {
        std::array<char, 64> instanceId{};
        std::array<char, 128> json{};
        std::array<char, 128> previousJson{};
        uint32_t bytes = 0;
        uint32_t calls = 0;
        DWORD threadId = 0;
    } capture{};
    const auto handler = [](void* context, const char* instanceId, const char* json, uint32_t bytes) noexcept -> HRESULT
    {
        auto* captured = static_cast<Capture*>(context);
        captured->calls += 1;
        captured->threadId = GetCurrentThreadId();
        captured->bytes = bytes;
        if (instanceId)
        {
            const size_t length = std::strlen(instanceId);
            size_t copy = captured->instanceId.size() - 1;
            if (length < copy)
            {
                copy = length;
            }
            std::memcpy(captured->instanceId.data(), instanceId, copy);
            captured->instanceId[copy] = '\0';
        }
        if (json && bytes < captured->json.size())
        {
            captured->previousJson = captured->json;
            std::memcpy(captured->json.data(), json, bytes);
            captured->json[bytes] = '\0';
        }
        return S_OK;
    };

    PluginHost host;
    Check(host.PersistWidgetSettings("widget.1", "{}", 2) == E_UNEXPECTED,
          L"persist without a host handler is rejected", success);
    host.SetSettingsPersistHandler(handler, &capture);
    Check(host.PersistWidgetSettings(nullptr, "{}", 2) == E_INVALIDARG, L"a null persist instance is rejected",
          success);
    constexpr char kPartial[] = "{\"location\":\"Paris\"}";
    Check(SUCCEEDED(host.PersistWidgetSettings("widget.1", kPartial, static_cast<uint32_t>(sizeof(kPartial) - 1))) &&
              capture.calls == 1 && std::string_view(capture.json.data()) == kPartial,
          L"persist forwards a partial settings object", success);

    wil::com_ptr_nothrow<IRedXeSettingsQueue> queue;
    Check(SUCCEEDED(host.QueryInterface(IID_PPV_ARGS(queue.put()))), L"host exposes the worker settings queue",
          success);
    if (queue)
    {
        wil::com_ptr_nothrow<IUnknown> identity;
        Check(SUCCEEDED(queue.query_to(identity.put())) && identity.get() == static_cast<IRedXeHost*>(&host),
              L"settings queue shares the host COM identity", success);
        HRESULT queued = E_FAIL;
        std::thread worker([&]() noexcept
                           { queued = queue->QueueWidgetSettings("widget.1", kPartial, sizeof(kPartial) - 1); });
        worker.join();
        Check(queued == S_OK && capture.calls == 1, L"worker settings do not persist on the worker", success);
        host.AcknowledgeUiInvalidate();
        Check(capture.calls == 2 && capture.threadId == GetCurrentThreadId() &&
                  std::string_view(capture.json.data()) == kPartial,
              L"queued settings commit on the UI thread", success);
        Check(queue->QueueWidgetSettings(nullptr, "{}", 2) == E_INVALIDARG &&
                  queue->QueueWidgetSettings("widget.1", "{}", 4097) == E_INVALIDARG,
              L"queued settings reject invalid identifiers and oversized JSON", success);
        for (uint32_t index = 0; index < 8; ++index)
        {
            char id[32]{};
            sprintf_s(id, "queued.%u", index);
            Check(queue->QueueWidgetSettings(id, "{}", 2) == S_OK, L"bounded queue accepts eight instances", success);
        }
        Check(queue->QueueWidgetSettings("overflow", "{}", 2) == HRESULT_FROM_WIN32(ERROR_BUSY),
              L"full settings queue rejects another instance", success);
        Check(queue->QueueWidgetSettings("queued.0", kPartial, sizeof(kPartial) - 1) == S_OK,
              L"a pending instance can be replaced even when full", success);
        host.ClearWidgetStatus("queued.1");
        host.AcknowledgeUiInvalidate();
        Check(capture.calls == 9, L"teardown discards the pending settings for that instance", success);
        host.AcknowledgeUiInvalidate();
        Check(capture.calls == 9, L"empty acknowledgement does not repeat settings writes", success);
        constexpr char newer[] = "{\"shortcuts\":[{\"target\":\"https://example.com/new\"}]}";
        Check(queue->QueueWidgetSettings("widget.1", kPartial, sizeof(kPartial) - 1) == S_OK &&
                  host.PersistWidgetSettings("widget.1", newer, sizeof(newer) - 1) == S_OK && capture.calls == 11 &&
                  std::string_view(capture.previousJson.data()) == kPartial &&
                  std::string_view(capture.json.data()) == newer,
              L"an older queued import is applied before a newer interactive save", success);
        host.AcknowledgeUiInvalidate();
        Check(capture.calls == 11 && std::string_view(capture.json.data()) == newer,
              L"a queued import cannot overwrite the newer interactive edit", success);
    }

    constexpr std::string_view settingsJson =
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.rotating-triangle"}]}]})json";
    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(settingsJson, settings);
    PluginManager plugins;
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(settings);
    }
    Check(SUCCEEDED(result) && plugins.WidgetCount() == 1, L"a triangle page constructs for collect", success);
    if (FAILED(result))
    {
        return;
    }
    uint32_t written = 1;
    std::array<char, 8> buffer{};
    IRedXeWidget* widget = plugins.WidgetAt(0);
    Check(widget &&
              widget->CollectPersistentSettings(buffer.data(), static_cast<uint32_t>(buffer.size()), &written) ==
                  S_FALSE &&
              written == 0,
          L"a widget with nothing to save returns S_FALSE", success);
}

void TestHostJsonlLog(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] host JSONL plugin log\n";
    PluginHost host;
    IRedXeHost* iface = host.Interface();
    Check(iface->Log(nullptr) == E_POINTER, L"a null log record is rejected", success);

    RedXeLogRecord malformed{
        sizeof(RedXeLogRecord) + 4, RedXeLogLevelInfo, nullptr, nullptr, "test-event", "message", S_OK};
    Check(iface->Log(&malformed) == E_INVALIDARG, L"a mismatched log sizeBytes is rejected", success);

    RedXeLogRecord missingEvent{sizeof(RedXeLogRecord), RedXeLogLevelInfo, nullptr, nullptr, nullptr, "message", S_OK};
    Check(iface->Log(&missingEvent) == E_INVALIDARG, L"a missing log event id is rejected", success);

    RedXeLogRecord missingMessage{sizeof(RedXeLogRecord), RedXeLogLevelInfo, nullptr, nullptr,
                                  "test-event",           nullptr,           S_OK};
    Check(iface->Log(&missingMessage) == E_INVALIDARG, L"a missing log message is rejected", success);

    RedXeLogRecord unknownLevel{sizeof(RedXeLogRecord), 99, nullptr, nullptr, "test-event", "message", S_OK};
    Check(iface->Log(&unknownLevel) == E_INVALIDARG, L"an unknown log level is rejected", success);

    const RedXeLogRecord dropped{sizeof(RedXeLogRecord),
                                 RedXeLogLevelInfo,
                                 "builtin.weather",
                                 "weather.1",
                                 "before-dir",
                                 "dropped until a log directory is set.",
                                 S_OK};
    Check(iface->Log(&dropped) == S_OK, L"log without a directory succeeds and drops the line", success);

    std::error_code error;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        (L"RedXe.LogTests." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(root, error);
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            std::error_code removeError;
            std::filesystem::remove_all(root, removeError);
        });
    if (error)
    {
        Check(false, L"a temporary log directory can be created", success);
        return;
    }

    Check(SUCCEEDED(host.SetLogDirectory(root.c_str())), L"SetLogDirectory starts the JSONL writer", success);
    Check(host.SetLogRetentionDays(0) == E_INVALIDARG, L"retention below 1 day is rejected", success);
    Check(host.SetLogRetentionDays(366) == E_INVALIDARG, L"retention above 365 days is rejected", success);
    Check(SUCCEEDED(host.SetLogRetentionDays(15)), L"a 15-day retention is accepted", success);

    const std::filesystem::path stale = root / L"RedXe-debug-2000-01-01.jsonl";
    const std::filesystem::path staleRelease = root / L"RedXe-2000-01-01.jsonl";
    const std::filesystem::path legacy = root / L"RedXe.jsonl";
    const std::filesystem::path legacyRotated = root / L"RedXe-debug.jsonl.1";
    {
        std::ofstream staleStream(stale, std::ios::binary);
        staleStream << "{\"event\":\"stale\"}\n";
        std::ofstream releaseStream(staleRelease, std::ios::binary);
        releaseStream << "{\"event\":\"stale-release\"}\n";
        std::ofstream legacyStream(legacy, std::ios::binary);
        legacyStream << "{\"event\":\"legacy\"}\n";
        std::ofstream rotatedStream(legacyRotated, std::ios::binary);
        rotatedStream << "{\"event\":\"rotated\"}\n";
    }

    const HRESULT notFound = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    const RedXeLogRecord line{sizeof(RedXeLogRecord),
                              RedXeLogLevelWarning,
                              "builtin.weather",
                              "weather.1",
                              "forecast-failed",
                              "weather fetch or parse failed.",
                              notFound};
    Check(iface->Log(&line) == S_OK, L"a valid log record is accepted", success);
    const std::array<std::string, 6> longMessages{std::string(384, '\n'),
                                                  std::string(384, '"'),
                                                  std::string(383, 'a') + "\xE2\x82\xAC",
                                                  std::string(382, 'a') + "\xF0\x9F\x8C\xA6",
                                                  std::string(100, '\x01') + std::string(280, '\\'),
                                                  std::string("invalid \xED\xA0\x80 \xFF UTF-8")};
    const std::string longIdentity(128, '\n');
    for (const std::string& message : longMessages)
    {
        const RedXeLogRecord bounded{sizeof(bounded),
                                     RedXeLogLevelError,
                                     longIdentity.c_str(),
                                     longIdentity.c_str(),
                                     "bounded-message",
                                     message.c_str(),
                                     E_FAIL};
        Check(iface->Log(&bounded) == S_OK, L"a message requiring escaping or UTF-8 truncation is accepted", success);
    }
    const RedXeLogRecord following{sizeof(following),
                                   RedXeLogLevelInfo,
                                   nullptr,
                                   nullptr,
                                   "after-bounded",
                                   "the next record remains independent",
                                   S_OK};
    Check(iface->Log(&following) == S_OK, L"a following record is accepted", success);
    Check(SUCCEEDED(host.FlushLog(2000)), L"FlushLog waits for the writer to drain", success);
    constexpr size_t flushBatches = 64;
    constexpr size_t recordsPerBatch = 4;
    const RedXeLogRecord pulse{sizeof(pulse), RedXeLogLevelInfo,   nullptr, nullptr,
                               "flush-pulse", "queued after idle", S_OK};
    bool flushesSucceeded = true;
    for (size_t batch = 0; batch < flushBatches; ++batch)
    {
        for (size_t record = 0; record < recordsPerBatch; ++record)
            flushesSucceeded = (iface->Log(&pulse) == S_OK) && flushesSucceeded;
        flushesSucceeded = (host.FlushLog(2000) == S_OK) && flushesSucceeded;
    }
    Check(flushesSucceeded, L"repeated enqueue/flush boundaries never observe a stale idle signal", success);

    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    wchar_t name[kRedXeLogFileNameCapacity]{};
    Check(RedXeFormatLogFileName(name, kRedXeLogFileNameCapacity, utc), L"today's UTC log file name can be formatted",
          success);
    const std::filesystem::path file = root / name;
    std::ifstream stream(file, std::ios::binary);
    std::string bytes;
    if (stream)
    {
        bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }
    Check(!bytes.empty(), L"the dated JSONL file can be read after flush", success);
    size_t offset = 0;
    size_t boundedCount = 0;
    size_t followingCount = 0;
    size_t flushedCount = 0;
    while (offset < bytes.size())
    {
        const size_t newline = bytes.find('\n', offset);
        if (newline == std::string::npos)
        {
            Check(false, L"every JSONL record ends in a newline", success);
            break;
        }
        using unique_json = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;
        unique_json document{yyjson_read(bytes.data() + offset, newline - offset, YYJSON_READ_NOFLAG)};
        Check(document && newline - offset < 1024, L"each bounded line is independently valid UTF-8 JSON", success);
        if (document)
        {
            yyjson_val* rootValue = yyjson_doc_get_root(document.get());
            const char* event = yyjson_get_str(yyjson_obj_get(rootValue, "event"));
            if (event && std::strcmp(event, "bounded-message") == 0)
            {
                ++boundedCount;
                const char* hr = yyjson_get_str(yyjson_obj_get(rootValue, "hr"));
                Check(hr && std::strcmp(hr, "0x80004005") == 0 && yyjson_is_str(yyjson_obj_get(rootValue, "message")),
                      L"truncation preserves the message and complete HRESULT suffix", success);
            }
            if (event && std::strcmp(event, "after-bounded") == 0)
                ++followingCount;
            if (event && std::strcmp(event, "flush-pulse") == 0)
                ++flushedCount;
        }
        offset = newline + 1;
    }
    Check(boundedCount == longMessages.size() && followingCount == 1,
          L"escaped records and the following record never merge or disappear", success);
    Check(flushedCount == flushBatches * recordsPerBatch, L"every flushed batch is present on disk", success);
    Check(bytes.find("\"event\":\"log-open\"") != std::string::npos, L"opening the log writes a log-open line",
          success);
    Check(bytes.find("\"event\":\"forecast-failed\"") != std::string::npos &&
              bytes.find("\"plugin\":\"builtin.weather\"") != std::string::npos &&
              bytes.find("\"instance\":\"weather.1\"") != std::string::npos &&
              bytes.find("\"level\":\"warning\"") != std::string::npos &&
              bytes.find("\"hr\":\"0x") != std::string::npos,
          L"a plugin log line stores event, plugin, instance, level, and HRESULT", success);
    Check(bytes.find("before-dir") == std::string::npos, L"records logged before SetLogDirectory are not written",
          success);
    std::error_code existsError;
    Check(!std::filesystem::exists(stale, existsError) && !std::filesystem::exists(staleRelease, existsError) &&
              !std::filesystem::exists(legacy, existsError) && !std::filesystem::exists(legacyRotated, existsError),
          L"dated logs older than retention and legacy undated log files are deleted", success);
}

void TestHostOwnedPlaceholderTiles(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] host-owned placeholder tiles\n";
    // Note on coverage: PluginManager's construction-failure branch is defensive. ValidateAppSettings rejects the
    // documents that would provoke a bundled plugin into refusing an instance, so no valid document can reach it;
    // it exists for runtime failures such as exhausted memory or subscription slots. What is reachable, and what is
    // covered here, is that a valid page produces no placeholders and that a constructed widget which reports itself
    // unavailable hands its tile to the host.
    constexpr std::string_view settingsJson = R"json({"version":{"major":5},"pages":[{"widgets":[)json"
                                              R"json({"plugin":"builtin.rotating-triangle"},)json"
                                              R"json({"plugin":"builtin.cpu-meter"},)json"
                                              R"json({"plugin":"builtin.memory-meter"}]}]})json";

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

void TestDashboardBackground(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] dashboard background resolution ";
    // The document color is the canvas clear and the color every provider is built with; a widget object's own
    // backgroundColor overrides it for that instance only, whatever plugin it is and however it paints.
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":5},"backgroundColor":"#102030",)json"
        R"json("declare":{"Cpu":{"plugin":"builtin.cpu-meter","backgroundColor":"#405060"}},)json"
        R"json("pages":[{"widgets":[)json"
        R"json({"plugin":"builtin.rotating-triangle"},)json"
        R"json("Cpu",)json"
        R"json({"use":"Cpu","backgroundColor":null},)json"
        R"json({"plugin":"builtin.memory-meter","backgroundColor":"#708090"}]}]})json";

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
    Check(SUCCEEDED(result) && plugins.WidgetCount() == 4, L"a page with background overrides constructs", success);
    if (FAILED(result))
    {
        return;
    }
    Check(plugins.BackgroundRgb() == 0x102030 && plugins.WidgetBackgroundRgbAt(0) == 0x102030 &&
              plugins.WidgetBackgroundRgbAt(1) == 0x405060 && plugins.WidgetBackgroundRgbAt(2) == 0x102030 &&
              plugins.WidgetBackgroundRgbAt(3) == 0x708090 && plugins.WidgetBackgroundRgbAt(4) == 0x102030,
          L"each tile resolves its own override or the document background", success);
    // Two cpu-meter instances with identical plugin settings but different backgrounds need different providers.
    Check(plugins.ProviderCount() == 4, L"the provider cache keys on the resolved background", success);

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
    Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 4 &&
              dashboard.BackgroundRgb() == 0x102030 && dashboard.WidgetBackgroundRgbAt(1) == 0x405060 &&
              dashboard.WidgetBackgroundRgbAt(2) == 0x102030,
          L"the renderer clears with the document background and fills overridden tiles", success);

    renderer.Shutdown();
    dashboard.Shutdown();
    AppSettings recolored = settings;
    recolored.backgroundRgb = 0x000000;
    Check(!ActiveDashboardRuntimeEquals(settings, recolored), L"changing the document background rebuilds the page",
          success);
    result = plugins.Reconfigure(recolored);
    Check(SUCCEEDED(result) && plugins.BackgroundRgb() == 0x000000 && plugins.WidgetBackgroundRgbAt(0) == 0x000000 &&
              plugins.WidgetBackgroundRgbAt(1) == 0x405060,
          L"reconfigure re-resolves every tile against the new document background", success);
}

void TestUnmappedCatalogModulePlaceholder(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] unmapped catalogued module placeholder\n";
    constexpr std::string_view settingsJson = R"json({"version":{"major":5},"pages":[{"widgets":[)json"
                                              R"json({"plugin":"builtin.rotating-triangle"},)json"
                                              R"json({"plugin":"builtin.launcher","shortcuts":[]}]}]})json";

    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(settingsJson, settings);
    PluginManager plugins;
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(settings);
    }
    Check(SUCCEEDED(result) && plugins.WidgetCount() == 2,
          L"a page initializes when one catalogued module cannot be mapped", success);
    if (FAILED(result))
    {
        return;
    }
    Check(!plugins.IsPlaceholderAt(0), L"the constructable sibling is not a placeholder", success);
    if (GetModuleHandleW(L"Launcher.dll") == nullptr)
    {
        Check(plugins.IsPlaceholderAt(1) && FAILED(plugins.PlaceholderFailureAt(1)),
              L"an unmapped catalogued plugin becomes a placeholder", success);
    }
    else
    {
        Check(!plugins.IsPlaceholderAt(1), L"Launcher constructs when its DLL is present", success);
    }

    AttachedHostWindow window;
    HRESULT windowResult = window.Initialize(kHostWidth, kHostHeight);
    Check(SUCCEEDED(windowResult), L"hidden host window initializes for an unmapped-module page", success);
    if (FAILED(windowResult))
    {
        return;
    }
    DashboardHost dashboard;
    Check(SUCCEEDED(dashboard.Initialize(plugins, window.Get(), kHostWidth, kHostHeight, window.Dpi(), false)),
          L"the dashboard hosts a page that includes a placeholder tile", success);
}

// A tile too short for a full row plus the page-control strip still draws the strip (rows shrink to fit above it):
// 144 px tall on a 720 px host leaves Process Viewer 66 px of content, one row less than a row plus the strip.
void TestShortTilePageControl(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] short tile keeps its page control\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":5},"declare":{"Processes":{"plugin":"builtin.process-viewer","topN":32},"Matrix":{"plugin":"builtin.matrix-rain"}},"pages":[{"rows":[{"weight":10,"widget":"Processes"},{"weight":40,"widget":"Matrix"}]}]})json";

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
    if (SUCCEEDED(result))
    {
        result = dashboard.SetWidgetsVisible(true);
    }
    Check(SUCCEEDED(result) && plugins.WidgetCount() == 2, L"short Process Viewer tile initializes on WARP", success);
    if (FAILED(result))
    {
        return;
    }
    // The row counter is process-wide (earlier tests already published 32 rows), so wait on this widget's own
    // frames: render until a frame reports its overflow pages.
    ProcessViewerTestDiagnostics diagnostics{};
    const ULONGLONG deadline = GetTickCount64() + 8000;
    uint32_t frame = 0;
    do
    {
        window.PumpMessages();
        (void)MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        result = renderer.Render(static_cast<float>(frame++) / 60.0f, 1.0f / 60.0f);
        if (SUCCEEDED(result))
        {
            result = ReadProcessViewerDiagnostics(diagnostics);
        }
    } while (SUCCEEDED(result) && diagnostics.pageCount < 2 && GetTickCount64() < deadline);
    const RECT tile = dashboard.PixelBoundsAt(0, kHostWidth, kHostHeight);
    std::wcout << L"[       -- ] short tile " << (tile.right - tile.left) << L"x" << (tile.bottom - tile.top)
               << L" render 0x" << std::hex << static_cast<unsigned long>(result) << std::dec << L" drew "
               << renderer.LastFrameSuccessfulWidgetCount() << L" rows " << diagnostics.lastPublishedRowCount
               << L" pages " << diagnostics.pageCount << L" dots at (" << diagnostics.pageDotFirstX << L", "
               << diagnostics.pageDotY << L") gap " << diagnostics.pageDotGap << L'\n';
    Check(SUCCEEDED(result) && diagnostics.pageCount > 1 && diagnostics.pageDotGap > 0.0f &&
              diagnostics.pageDotY > 0.0f && diagnostics.pageDotY < static_cast<float>(tile.bottom - tile.top),
          L"a short overflowing tile still draws its page control inside the tile", success);
    (void)dashboard.SetWidgetsVisible(false);
    renderer.Shutdown();
    dashboard.Shutdown();
}

void TestWeatherPluginConstructs(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] weather plugin DLL constructs\n";
    constexpr std::string_view settingsJson = R"json({"version":{"major":5},"pages":[{"widgets":[)json"
                                              R"json({"plugin":"builtin.weather"}]}]})json";

    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(settingsJson, settings);
    PluginManager plugins;
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(settings);
    }
    Check(SUCCEEDED(result) && plugins.WidgetCount() == 1 && !plugins.IsPlaceholderAt(0) &&
              plugins.GpuWidgetAt(0) != nullptr,
          L"Weather.dll maps with curl and zlib beside it and constructs a GPU widget", success);
    if (IRedXeInteractiveWidget* interactive = SUCCEEDED(result) ? plugins.InteractiveWidgetAt(0) : nullptr)
    {
        // Without a forecast there is one overflow page, so every wheel sample is handed back to the host.
        RedXePointerEvent wheel{sizeof(RedXePointerEvent), 1,    RedXePointerKindMouse,
                                RedXePointerPhaseWheel,    8.0f, 8.0f};
        wheel.wheelDelta = -120.0f;
        const HRESULT down = interactive->OnPointer(&wheel);
        wheel.wheelDelta = 120.0f;
        const HRESULT up = interactive->OnPointer(&wheel);
        wheel.phase = RedXePointerPhaseHorizontalWheel;
        const HRESULT tilt = interactive->OnPointer(&wheel);
        Check(down == S_FALSE && up == S_FALSE && tilt == S_FALSE,
              L"a single-page Weather tile declines wheel samples so the host can change dashboard pages", success);
    }
}

// Stub network widget that stays inside RunNetworkWork until the host signals its cancel event.
class BlockingNetworkStub final : public IRedXeNetworkWidget
{
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId == IID_IUnknown || interfaceId == __uuidof(IRedXeNetworkWidget))
        {
            *result = static_cast<IRedXeNetworkWidget*>(this);
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return 1;
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        return 1;
    }
    HRESULT STDMETHODCALLTYPE RunNetworkWork(HANDLE cancelEvent, uint32_t* nextDelayMilliseconds) noexcept override
    {
        if (!nextDelayMilliseconds)
        {
            return E_POINTER;
        }
        *nextDelayMilliseconds = 0;
        calls.fetch_add(1, std::memory_order_relaxed);
        inside.store(true, std::memory_order_release);
        SetEvent(entered.get());
        cancelled = WaitForSingleObject(cancelEvent, 5'000) == WAIT_OBJECT_0;
        inside.store(false, std::memory_order_release);
        *nextDelayMilliseconds = 1'000;
        return S_OK;
    }

    wil::unique_event_nothrow entered;
    std::atomic<uint32_t> calls{0};
    std::atomic<bool> inside{false};
    bool cancelled = false;
};

void TestNetworkLane(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] host network lane offline, drain, and join\n";
    PluginHost& host = PluginHost::Instance();
    BlockingNetworkStub stub;
    if (FAILED(stub.entered.create(wil::EventOptions::ManualReset)))
    {
        Check(false, L"network stub event creates", success);
        return;
    }
    const DWORD threadsBefore = CountProcessThreads();
    Check(!host.NetworkAccessEnabled() && !host.NetworkWorkerRunning(),
          L"automated host starts with network access disabled and no network worker", success);
    Check(SUCCEEDED(host.RegisterNetworkWidget(&stub)), L"network stub registers", success);

    // Offline: activation is a no-op. No worker thread starts and the plugin is never called.
    host.SetNetworkWidgetActive(&stub, true);
    Sleep(50);
    Check(!host.NetworkWorkerRunning() && stub.calls.load(std::memory_order_relaxed) == 0 &&
              CountProcessThreads() <= threadsBefore,
          L"offline activation never calls RunNetworkWork and starts no thread", success);

    // Enabled: the lazy serial worker starts and runs the widget once it is active.
    host.SetNetworkAccessEnabled(true);
    host.SetNetworkWidgetActive(&stub, true);
    const bool entered = WaitForSingleObject(stub.entered.get(), 5'000) == WAIT_OBJECT_0;
    Check(entered && host.NetworkWorkerRunning() && stub.calls.load(std::memory_order_relaxed) == 1,
          L"enabling network access starts one worker that calls RunNetworkWork", success);

    // Hide: deactivation cancels and drains before returning, so the call is no longer inside the plugin.
    host.SetNetworkWidgetActive(&stub, false);
    Check(entered && !stub.inside.load(std::memory_order_acquire) && stub.cancelled,
          L"deactivating a widget cancels and drains its in-flight RunNetworkWork", success);
    Sleep(50);
    Check(stub.calls.load(std::memory_order_relaxed) == 1, L"an inactive widget is not run again", success);

    // Shutdown path: disabling access cancels queued widgets, waits for idle, and joins the worker.
    host.SetNetworkAccessEnabled(false);
    Check(!host.NetworkWorkerRunning() && !host.NetworkAccessEnabled(), L"disabling network access joins the worker",
          success);
    host.UnregisterNetworkWidget(&stub);
}

struct HostActionRecord final
{
    std::vector<std::string> actions;
    std::vector<std::string> targets;
    uint32_t completed = 0;
    HRESULT result = S_OK;
};

HRESULT RecordHostAction(void* context, const char* actionUtf8, const char* targetUtf8) noexcept
{
    auto* record = static_cast<HostActionRecord*>(context);
    try
    {
        record->actions.emplace_back(actionUtf8 ? actionUtf8 : "");
        record->targets.emplace_back(targetUtf8 ? targetUtf8 : "");
    }
    catch (...)
    {
    }
    return record->result;
}

void RecordHostActionCompleted(void* context) noexcept
{
    ++static_cast<HostActionRecord*>(context)->completed;
}

void TestHostActionQueue(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] host action ring: validation, coalescing, bounds, ordered drain\n";
    PluginHost& host = PluginHost::Instance();
    HostActionRecord record;
    host.SetHostActionHandler(&RecordHostAction, &RecordHostActionCompleted, &record);
    host.DrainHostActions();

    RedXeActionRequest request{};
    Check(host.RequestAction(nullptr) == E_POINTER, L"a null request returns E_POINTER", success);
    Check(host.RequestAction(&request) == E_INVALIDARG, L"a zero sizeBytes request is rejected", success);
    request.sizeBytes = sizeof(request);
    Check(host.RequestAction(&request) == E_INVALIDARG, L"a null action name is rejected", success);
    request.actionUtf8 = "none";
    Check(host.RequestAction(&request) == E_INVALIDARG, L"a name outside the grammar is rejected", success);
    request.actionUtf8 = "nowhere.next";
    Check(host.RequestAction(&request) == E_INVALIDARG, L"an unregistered namespace is rejected", success);
    request.actionUtf8 = "logicon.keyPage.next";
    Check(host.RequestAction(&request) == S_OK, L"a registered published namespace queues", success);
    host.DrainHostActions();
    record.actions.clear();
    record.targets.clear();
    std::string overlong(kRedXeMaximumActionTargetBytes + 1, 'x');
    request.actionUtf8 = "system.launch";
    request.targetUtf8 = overlong.c_str();
    Check(host.RequestAction(&request) == E_INVALIDARG, L"a 513-byte target is rejected", success);

    request.actionUtf8 = "page.next";
    request.targetUtf8 = nullptr;
    Check(host.RequestAction(&request) == S_OK && host.PendingHostActionCount() == 1, L"a page action queues", success);
    Check(host.RequestAction(&request) == S_FALSE && host.PendingHostActionCount() == 1,
          L"an identical pending action coalesces", success);
    request.actionUtf8 = "widget.toggle";
    request.targetUtf8 = "system/2";
    Check(host.RequestAction(&request) == S_OK && host.PendingHostActionCount() == 2,
          L"a distinct action with a target queues behind it", success);
    request.targetUtf8 = "system/3";
    Check(host.RequestAction(&request) == S_OK && host.PendingHostActionCount() == 3,
          L"a different target is a distinct action", success);
    std::array<char, 16> targetText{};
    for (int32_t ordinal = 10; host.PendingHostActionCount() < 16; ++ordinal)
    {
        (void)sprintf_s(targetText.data(), targetText.size(), "%d", ordinal);
        request.targetUtf8 = targetText.data();
        if (FAILED(host.RequestAction(&request)))
        {
            break;
        }
    }
    Check(host.PendingHostActionCount() == 16, L"the ring holds sixteen distinct actions", success);
    request.targetUtf8 = "99";
    Check(host.RequestAction(&request) == HRESULT_FROM_WIN32(ERROR_BUSY) && host.PendingHostActionCount() == 16,
          L"a full ring returns ERROR_BUSY without accepting the request", success);

    host.DrainHostActions();
    Check(host.PendingHostActionCount() == 0 && record.actions.size() == 16 && record.actions[0] == "page.next" &&
              record.targets[0].empty() && record.actions[1] == "widget.toggle" && record.targets[1] == "system/2" &&
              record.targets[2] == "system/3" && record.targets[15] == "22" && record.completed == 17,
          L"drain delivers every queued action once, in submission order, with its copied target, and completes each",
          success);
    host.DrainHostActions();
    Check(record.actions.size() == 16, L"a second drain delivers nothing", success);

    // ExecuteAction runs now, on this thread, and returns the handler's result.
    record.result = HRESULT_FROM_WIN32(ERROR_BUSY);
    request.actionUtf8 = "page.first";
    request.targetUtf8 = nullptr;
    Check(host.ExecuteAction(&request) == HRESULT_FROM_WIN32(ERROR_BUSY) && record.actions.size() == 17 &&
              record.actions[16] == "page.first" && host.PendingHostActionCount() == 0,
          L"ExecuteAction is synchronous and returns the application's result", success);
    record.result = S_OK;
    host.SetHostActionHandler(nullptr, nullptr, nullptr);
}

void TestActionValidation(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] action validation: default catalog, target grammars, registry, publishers\n";
    PluginHost& host = PluginHost::Instance();
    const RedXeActionDescriptor* descriptor = nullptr;
    RedXeActionRequest request{};
    request.sizeBytes = sizeof(request);
    request.actionUtf8 = "page.goto";
    request.targetUtf8 = "system";
    Check(host.ValidateAction(&request, &descriptor) == S_OK && descriptor && descriptor->name &&
              std::strcmp(descriptor->name, "page.goto") == 0 && descriptor->targetKind == RedXeActionTargetPageRef,
          L"a default action validates with its descriptor", success);
    request.actionUtf8 = "page.nowhere";
    Check(host.ValidateAction(&request, &descriptor) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && !descriptor,
          L"an unknown verb in a default namespace is not found", success);
    request.actionUtf8 = "system.launch";
    request.targetUtf8 = "notepad.exe";
    Check(host.ValidateAction(&request, nullptr) == E_INVALIDARG, L"a relative launch target is invalid", success);
    request.targetUtf8 = "C:\\Tools\\Code.exe";
    Check(host.ValidateAction(&request, nullptr) == S_OK, L"an absolute launch target is valid", success);
    request.actionUtf8 = "system.shutdown";
    request.targetUtf8 = nullptr;
    Check(host.ValidateAction(&request, nullptr) == E_INVALIDARG, L"a destructive action needs its confirming target",
          success);
    request.targetUtf8 = "now";
    Check(host.ValidateAction(&request, nullptr) == S_OK, L"\"now\" confirms a destructive action", success);
    request.targetUtf8 = "30";
    Check(host.ValidateAction(&request, nullptr) == S_OK, L"a delay confirms a shutdown", success);
    request.actionUtf8 = "keys.press";
    request.targetUtf8 = "Ctrl+Shift+Esc,Win+D";
    Check(host.ValidateAction(&request, nullptr) == S_OK, L"a chord sequence validates", success);
    request.targetUtf8 = "Hyper+Q";
    Check(host.ValidateAction(&request, nullptr) == E_INVALIDARG, L"an unknown modifier is invalid", success);
    request.actionUtf8 = "keys.down";
    request.targetUtf8 = "Ctrl+A,Ctrl+B";
    Check(host.ValidateAction(&request, nullptr) == E_INVALIDARG, L"keys.down takes exactly one chord", success);
    request.actionUtf8 = "mouse.move";
    request.targetUtf8 = "center@xeneon";
    Check(host.ValidateAction(&request, nullptr) == S_OK, L"a monitor-relative point validates", success);
    request.targetUtf8 = "+10,20";
    Check(host.ValidateAction(&request, nullptr) == E_INVALIDARG, L"a half-relative point is invalid", success);
    request.actionUtf8 = "system.power.plan";
    request.targetUtf8 = "highPerformance";
    Check(host.ValidateAction(&request, nullptr) == S_OK, L"a named power plan validates", success);
    request.targetUtf8 = "turbo";
    Check(host.ValidateAction(&request, nullptr) == E_INVALIDARG, L"an unknown power plan is invalid", success);
    request.actionUtf8 = "nowhere.next";
    request.targetUtf8 = nullptr;
    Check(host.ValidateAction(&request, nullptr) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
          L"an unregistered namespace is not found", success);

    // Registered publishers: mapping Logicon.dll reads its contract; the zoom contract comes from zoom.action.dll.
    request.actionUtf8 = "logicon.keyPage.goto";
    request.targetUtf8 = "2";
    Check(host.ValidateAction(&request, &descriptor) == S_OK && descriptor &&
              host.ActionPublisherStateOf("logicon") == PluginHost::ActionPublisherState::Ready,
          L"a published Logicon action validates after its module maps", success);
    request.targetUtf8 = "7";
    Check(host.ValidateAction(&request, nullptr) == E_INVALIDARG, L"a published target outside its bounds is invalid",
          success);
    request.actionUtf8 = "logicon.nowhere";
    Check(host.ValidateAction(&request, nullptr) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
          L"an unknown published verb is not found", success);
    request.actionUtf8 = "zoom.mute";
    request.targetUtf8 = "toggle";
    Check(host.ValidateAction(&request, &descriptor) == S_OK && descriptor &&
              (descriptor->flags & RedXeActionFlagDeferred) != 0 &&
              host.ActionPublisherStateOf("zoom") == PluginHost::ActionPublisherState::Ready,
          L"the Zoom contract registers from zoom.action.dll", success);
    request.actionUtf8 = "zoom.share";
    request.targetUtf8 = "app@exe:Zoom.exe";
    Check(host.ValidateAction(&request, nullptr) == S_OK, L"a window suffix is accepted where the descriptor allows it",
          success);
    request.actionUtf8 = "zoom.join";
    request.targetUtf8 = "https://zoom.us/j/1234567890?pwd=abc";
    Check(host.ValidateAction(&request, nullptr) == S_OK, L"a meeting URL validates", success);
    request.targetUtf8 = "12";
    Check(host.ValidateAction(&request, nullptr) == E_INVALIDARG, L"a short meeting id is invalid", success);
    std::array<wchar_t, 2048> notices{};
    Check(host.CopyActionNotices(notices.data(), notices.size()) == 0,
          L"the bundled publishers register without a collision notice", success);

    // Default namespaces execute inside the host; with device access disabled they count and act on nothing.
    host.SetDeviceAccessEnabled(false);
    HostActions::ResetCounters();
    request.actionUtf8 = "keys.media";
    request.targetUtf8 = "volume-up";
    Check(host.ExecuteAction(&request) == S_OK, L"a keys action executes", success);
    request.actionUtf8 = "system.launch";
    request.targetUtf8 = "https://example.org";
    Check(host.ExecuteAction(&request) == S_OK, L"a launch executes", success);
    request.actionUtf8 = "mouse.scroll";
    request.targetUtf8 = "+3";
    Check(host.ExecuteAction(&request) == S_OK, L"a mouse action executes", success);
    request.actionUtf8 = "system.shutdown";
    request.targetUtf8 = "now";
    Check(host.ExecuteAction(&request) == S_OK, L"a confirmed shutdown counts without acting", success);
    request.targetUtf8 = nullptr;
    Check(host.ExecuteAction(&request) == E_INVALIDARG, L"an unconfirmed shutdown is refused before execution",
          success);
    const HostActions::Counters counters = HostActions::CopyCounters();
    Check(counters.executed == 4 && counters.injectedInputs == 3 && counters.launches == 1 &&
              counters.powerRequests == 1 && std::strcmp(counters.lastAction.data(), "system.shutdown") == 0,
          L"automated hosts count injected inputs, launches, and power requests without performing them", success);
    host.SetDeviceAccessEnabled(true);
}

void TestServiceLifetime(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] service lifetime: start, device lane, host state, settings apply, stop\n";
    PluginHost& host = PluginHost::Instance();
    host.SetDeviceAccessEnabled(false);
    Check(!host.DeviceAccessEnabled() && host.StartedServiceCount() == 0 && host.RunningDeviceWorkerCount() == 0,
          L"the automated host starts with device access disabled and no service", success);

    constexpr std::string_view withService = R"json({"version":{"major":5,"minor":1},
      "services":{"Keypad":{"plugin":"builtin.logicon","brightness":35,"keys":[{"slot":0,"action":"page.next"}]}},
      "pages":[{"widgets":[{"plugin":"builtin.rotating-triangle"}]}]})json";
    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(withService, settings);
    Check(SUCCEEDED(result) && settings.serviceCount == 1, L"a document with the Logicon service parses", success);
    if (FAILED(result))
    {
        return;
    }
    const DWORD threadsBefore = CountProcessThreads();
    result = host.StartServices(settings);
    Check(SUCCEEDED(result) && host.StartedServiceCount() == 1 && host.ServiceFor("builtin.logicon") != nullptr &&
              host.ServiceFor("builtin.launcher") == nullptr,
          L"StartServices creates and starts the configured service", success);
    Check(host.RunningDeviceWorkerCount() == 1 && CountProcessThreads() >= threadsBefore + 1,
          L"a started service with a device worker owns exactly one lane thread", success);
    Check(SUCCEEDED(host.StartServices(settings)) && host.StartedServiceCount() == 1 &&
              host.RunningDeviceWorkerCount() == 1,
          L"StartServices is idempotent", success);

    const HMODULE module = GetModuleHandleW(L"Logicon.dll");
    const auto diagnostics =
        module ? ResolveFunction<RedXeLogiconGetTestDiagnosticsFn>(module, kRedXeLogiconGetTestDiagnosticsExport)
               : nullptr;
    RedXeLogiconTestDiagnostics report{};
    report.sizeBytes = sizeof(report);
    bool laneRunning = false;
    for (int attempt = 0; attempt < 100 && diagnostics; ++attempt)
    {
        if (SUCCEEDED(diagnostics(&report)) && report.laneRunning == 1)
        {
            laneRunning = true;
            break;
        }
        Sleep(20);
    }
    Check(diagnostics != nullptr && laneRunning && report.deviceAccess == 0 && report.connected == 0 &&
              report.brightness == 35,
          L"the lane runs without device access, opens nothing, and carries the configured settings", success);

    RedXeHostState state{};
    state.sizeBytes = sizeof(state);
    state.pageIndex = 2;
    state.pageCount = 5;
    state.pageId = "system";
    state.pageName = L"System";
    state.flags = RedXeHostStateVisible;
    host.PublishHostState(state);
    bool statePublished = false;
    for (int attempt = 0; attempt < 100 && diagnostics; ++attempt)
    {
        if (SUCCEEDED(diagnostics(&report)) && report.hostPageIndex == 2 && report.hostPageCount == 5)
        {
            statePublished = true;
            break;
        }
        Sleep(20);
    }
    Check(statePublished, L"PublishHostState reaches the started service", success);

    // A reload with the same effective object re-applies nothing; a changed object re-applies.
    Check(SUCCEEDED(host.ApplyServiceSettings(settings)) && host.StartedServiceCount() == 1,
          L"ApplyServiceSettings with an unchanged object keeps the service running", success);
    constexpr std::string_view changed = R"json({"version":{"major":5,"minor":1},
      "services":{"Keypad":{"plugin":"builtin.logicon","brightness":80}},
      "pages":[{"widgets":[{"plugin":"builtin.rotating-triangle"}]}]})json";
    AppSettings changedSettings{};
    result = ParseAppSettingsJson(changed, changedSettings);
    Check(SUCCEEDED(result) && SUCCEEDED(host.ApplyServiceSettings(changedSettings)),
          L"ApplyServiceSettings with a changed object succeeds", success);
    bool reapplied = false;
    for (int attempt = 0; attempt < 100 && diagnostics; ++attempt)
    {
        if (SUCCEEDED(diagnostics(&report)) && report.brightness == 80)
        {
            reapplied = true;
            break;
        }
        Sleep(20);
    }
    Check(reapplied, L"the changed settings object reached the lane", success);

    // Removing the service from the document stops and releases it; the lane drains inside its budget.
    constexpr std::string_view without =
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.rotating-triangle"}]}]})json";
    AppSettings withoutSettings{};
    result = ParseAppSettingsJson(without, withoutSettings);
    const ULONGLONG stopStarted = GetTickCount64();
    Check(SUCCEEDED(result) && SUCCEEDED(host.ApplyServiceSettings(withoutSettings)) &&
              host.StartedServiceCount() == 0 && host.RunningDeviceWorkerCount() == 0 &&
              host.ServiceFor("builtin.logicon") == nullptr && GetTickCount64() - stopStarted < 2'000,
          L"a reload without the service stops it and joins its lane within budget", success);
    Check(diagnostics && diagnostics(&report) == HRESULT_FROM_WIN32(ERROR_NOT_READY),
          L"the stopped service object is released", success);

    // Start again and stop through StopServices, the shutdown path.
    Check(SUCCEEDED(host.StartServices(settings)) && host.StartedServiceCount() == 1, L"the service restarts", success);
    host.StopServices();
    Check(host.StartedServiceCount() == 0 && host.RunningDeviceWorkerCount() == 0,
          L"StopServices stops every service and joins every lane", success);
    host.StopServices();
    Check(host.StartedServiceCount() == 0, L"StopServices is idempotent", success);
}

void TestLauncherPluginConstructs(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] launcher plugin DLL constructs\n";
    constexpr std::string_view settingsJson = R"json({"version":{"major":5},"pages":[{"widgets":[)json"
                                              R"json({"plugin":"builtin.launcher","shortcuts":[]}]}]})json";

    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(settingsJson, settings);
    PluginManager plugins;
    if (SUCCEEDED(result))
    {
        result = plugins.Initialize(settings);
    }
    Check(SUCCEEDED(result) && plugins.WidgetCount() == 1 && !plugins.IsPlaceholderAt(0) &&
              plugins.GpuWidgetAt(0) != nullptr && plugins.InteractiveWidgetAt(0) != nullptr,
          L"Launcher.dll maps and constructs a GPU interactive widget", success);
    if (FAILED(result) || plugins.WidgetCount() == 0)
    {
        return;
    }
    uint32_t written = 1;
    std::array<char, 8> buffer{};
    IRedXeWidget* widget = plugins.WidgetAt(0);
    Check(widget &&
              widget->CollectPersistentSettings(buffer.data(), static_cast<uint32_t>(buffer.size()), &written) ==
                  S_FALSE &&
              written == 0,
          L"an empty launcher has nothing to save", success);
    const HMODULE module = GetModuleHandleW(L"Launcher.dll");
    const LauncherGetTestDiagnosticsFn getDiagnostics =
        ResolveFunction<LauncherGetTestDiagnosticsFn>(module, kLauncherGetTestDiagnosticsExport);
    Check(getDiagnostics != nullptr, L"launcher diagnostics export resolves", success);
    if (widget && getDiagnostics)
    {
        Check(SUCCEEDED(widget->SetVisible(TRUE)), L"empty launcher SetVisible succeeds", success);
        LauncherTestDiagnostics diagnostics{sizeof(LauncherTestDiagnostics)};
        Check(SUCCEEDED(getDiagnostics(&diagnostics)) && diagnostics.usingTaskbarPins == 0 &&
                  diagnostics.authoredCount == 0,
              L"automated empty launcher does not read the live taskbar", success);
    }
}

void TestPublishedArraySchema(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] plugin published array schema subset\n";
    constexpr std::string_view launcherSchema =
        R"json({"type":"object","additionalProperties":false,"properties":{"shortcuts":{"type":"array","minItems":0,"maxItems":32,"items":{"type":"object","additionalProperties":false,"properties":{"target":{"type":"string"}},"required":["target"]}}}})json";
    constexpr std::string_view launcherDefaults = R"json({"shortcuts":[]})json";
    constexpr std::string_view objectSchema =
        R"json({"type":"object","additionalProperties":false,"properties":{}})json";
    constexpr std::string_view objectDefaults = R"json({})json";
    constexpr std::string_view nestedSchema =
        R"json({"type":"object","additionalProperties":false,"properties":{"rows":{"type":"array","minItems":0,"maxItems":1,"items":{"type":"array","minItems":0,"maxItems":1,"items":{"type":"string"}}}}})json";
    Check(SUCCEEDED(PluginManager::ValidatePluginPublishedSchema(launcherSchema, launcherDefaults)),
          L"a bounded array of closed objects is accepted", success);
    Check(SUCCEEDED(PluginManager::ValidatePluginPublishedSchema(objectSchema, objectDefaults)),
          L"a Matrix-style closed object schema remains valid", success);
    Check(FAILED(PluginManager::ValidatePluginPublishedSchema(nestedSchema, R"json({"rows":[]})json")),
          L"nested arrays are rejected", success);
    constexpr std::string_view boundedText =
        R"({"type":"object","properties":{"name":{"type":"string","minLength":1,"maxLength":2}}})";
    Check(SUCCEEDED(PluginManager::ValidatePluginPublishedSchema(boundedText, R"({"name":"\u00e9\ud83d\ude00"})")),
          L"published text bounds count Unicode scalars rather than UTF-8 bytes or UTF-16 units", success);
    Check(FAILED(PluginManager::ValidatePluginPublishedSchema(boundedText, R"({"name":"abc"})")) &&
              FAILED(PluginManager::ValidatePluginPublishedSchema(boundedText, R"({"name":""})")),
          L"both published string length bounds are enforced", success);
    constexpr std::string_view machineId =
        R"({"type":"object","properties":{"id":{"type":"string","pattern":"^[A-Za-z0-9_-]+$"}}})";
    Check(SUCCEEDED(PluginManager::ValidatePluginPublishedSchema(machineId, R"({"id":"office_2-main"})")) &&
              FAILED(PluginManager::ValidatePluginPublishedSchema(machineId, R"({"id":"office.2"})")),
          L"published machine identifiers use the fixed ASCII pattern", success);
    constexpr std::string_view unsupportedEmpty =
        R"({"type":"object","properties":{"rows":{"type":"array","items":{"type":"object","properties":{"id":{"type":"string","pattern":".*"}}}}}})";
    Check(FAILED(PluginManager::ValidatePluginPublishedSchema(unsupportedEmpty, R"({"rows":[]})")),
          L"unsupported string constraints are rejected even when a defaults array is empty", success);
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

    PageEdgeState live{};
    live.rendererReady = true;
    live.windowVisible = true;
    live.displayPoweredOn = true;
    live.rendererSuspended = false;
    live.pageCount = 3;
    live.atFirstPage = false;
    live.atLastPage = false;
    Check(PageEdgeClickNavigates(live, kPageEdgeDirectionPrevious, left, POINT{0, 10}) &&
              !PageEdgeClickNavigates(live, kPageEdgeDirectionPrevious, left, POINT{left.right, 10}),
          L"A parent edge click navigates only when the zone is live and the point is inside the band.", success);
    live.widgetRaised = true;
    Check(!PageEdgeClickNavigates(live, kPageEdgeDirectionPrevious, left, POINT{0, 10}),
          L"A parent edge click does not navigate while a widget is raised.", success);

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

void TestWheelNavigationPolicy(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] mouse wheel sequence policy\n";

    // Detents: fractions sum to whole notches per axis, a reversal drops the remainder, junk completes nothing.
    RedXeWheelDetent detent{};
    Check(detent.Accumulate(120.0f) == 1 && detent.remainder == 0.0f, L"one classic notch is one detent", success);
    Check(detent.Accumulate(-300.0f) == -2 && detent.remainder == -60.0f,
          L"a fast spin reports every whole detent and keeps the fraction", success);
    Check(detent.Accumulate(40.0f) == 0 && detent.remainder == 40.0f,
          L"a direction reversal discards the opposite remainder", success);
    Check(detent.Accumulate(40.0f) == 0 && detent.Accumulate(40.0f) == 1 && detent.remainder == 0.0f,
          L"precision-wheel fractions step once per whole detent", success);
    Check(detent.Accumulate(0.0f) == 0 && detent.Accumulate(std::numeric_limits<float>::quiet_NaN()) == 0 &&
              detent.remainder == 0.0f,
          L"zero and non-finite samples complete nothing", success);

    // Direction mapping: wheel down and tilt right both advance, like swipe-left.
    Check(WheelNavigationUnits(WheelAxis::Vertical, -120.0f) > 0.0f &&
              WheelNavigationUnits(WheelAxis::Vertical, 120.0f) < 0.0f &&
              WheelNavigationUnits(WheelAxis::Horizontal, 120.0f) > 0.0f &&
              WheelNavigationUnits(WheelAxis::Horizontal, -120.0f) < 0.0f,
          L"wheel down and tilt right travel toward the next page", success);

    // A sequence whose first sample the widget consumed stays with that widget until the user pauses, even when the
    // widget later declines (a paged widget at its bound).
    WheelNavigator wheel{};
    Check(wheel.Begin(1000) == WheelOwner::None, L"the first sample of a sequence has no owner", success);
    wheel.Latch(3, true);
    Check(wheel.owner == WheelOwner::Widget && wheel.widgetIndex == 3, L"a consumed first sample latches the widget",
          success);
    Check(wheel.Begin(1200) == WheelOwner::Widget, L"a sample inside the gap stays with the latched widget", success);
    wheel.Latch(5, false);
    Check(wheel.owner == WheelOwner::Widget && wheel.widgetIndex == 3,
          L"a later declined sample does not hand a widget-owned sequence to the host", success);
    Check(wheel.Begin(1200 + kWheelSequenceGapMilliseconds) == WheelOwner::None,
          L"a pause of the gap length ends the sequence", success);

    // A declined first sample gives the sequence to the host; later samples are not offered to widgets.
    wheel.Latch(SIZE_MAX, false);
    Check(wheel.owner == WheelOwner::Host && wheel.widgetIndex == SIZE_MAX, L"a declined first sample latches the host",
          success);
    wheel.Latch(2, true);
    Check(wheel.owner == WheelOwner::Host, L"a host-owned sequence is not taken over by a widget", success);
    Check(wheel.Accumulate(WheelAxis::Vertical, -40.0f) == 0 && wheel.Accumulate(WheelAxis::Vertical, -40.0f) == 0,
          L"fractions below a detent navigate nowhere", success);
    Check(wheel.Accumulate(WheelAxis::Vertical, -40.0f) == kPageEdgeDirectionNext,
          L"a whole detent of wheel-down advances one page", success);
    Check(wheel.Accumulate(WheelAxis::Vertical, -240.0f) == kPageEdgeDirectionNext &&
              wheel.detents[0].remainder == 0.0f && wheel.detents[1].remainder == 0.0f,
          L"a fast spin navigates once per sample and banks no surplus", success);
    Check(wheel.Accumulate(WheelAxis::Vertical, 120.0f) == kPageEdgeDirectionPrevious, L"wheel-up returns one page",
          success);
    Check(wheel.Accumulate(WheelAxis::Horizontal, 120.0f) == kPageEdgeDirectionNext &&
              wheel.Accumulate(WheelAxis::Horizontal, -120.0f) == kPageEdgeDirectionPrevious,
          L"tilt right advances and tilt left returns", success);
    Check(wheel.Accumulate(WheelAxis::Horizontal, 60.0f) == 0 && wheel.Accumulate(WheelAxis::Vertical, -60.0f) == 0,
          L"axes accumulate independently", success);
    wheel.ClearAccumulation();
    Check(wheel.Accumulate(WheelAxis::Horizontal, 60.0f) == 0 && wheel.Accumulate(WheelAxis::Vertical, -60.0f) == 0,
          L"a refusal drops partial travel on both axes", success);
    Check(wheel.Begin(1700 + kWheelSequenceGapMilliseconds - 1) == WheelOwner::Host,
          L"samples inside the gap keep the host owner", success);

    // Page promote releases only a widget latch; a host-owned spin keeps flipping pages.
    wheel.ReleaseWidget();
    Check(wheel.owner == WheelOwner::Host && wheel.inSequence, L"a promote keeps a host-owned sequence", success);
    wheel.Reset();
    wheel.Latch(1, true);
    Check(wheel.owner == WheelOwner::Widget, L"reset then latch starts a widget sequence", success);
    wheel.ReleaseWidget();
    Check(wheel.owner == WheelOwner::None && !wheel.inSequence && wheel.widgetIndex == SIZE_MAX,
          L"a promote ends a widget-owned sequence", success);
}

void TestPageIndicatorGeometry(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] shared page control geometry\n";

    // Nominal metrics at 96 DPI, centred in a 400 px strip: five dots, 14 px apart, 3 / 4 px radii.
    const RedXePageIndicatorLayout centred =
        RedXePageIndicatorInStrip(0.0f, 100.0f, 400.0f, 20.0f, 96, 5, 2, RedXePageIndicatorAlign::Center);
    Check(centred.pageCount == 5 && centred.selected == 2 && centred.gap == 14.0f && centred.radius == 3.0f &&
              centred.selectedRadius == 4.0f && centred.centerY == 110.0f && centred.Width() == 64.0f &&
              std::fabs(centred.firstCenterX - (200.0f - 32.0f + 4.0f)) < 0.01f &&
              std::fabs(centred.CenterX(4) - (200.0f + 32.0f - 4.0f)) < 0.01f,
          L"five dots centre in the strip with DxUi::PageIndicator metrics", success);

    // DPI scales every metric; right alignment ends the block at the strip's right edge.
    const RedXePageIndicatorLayout right =
        RedXePageIndicatorInStrip(10.0f, 0.0f, 300.0f, 18.0f, 192, 3, 7, RedXePageIndicatorAlign::Right);
    Check(right.gap == 28.0f && right.radius == 6.0f && right.selectedRadius == 8.0f && right.selected == 2 &&
              std::fabs(right.CenterX(2) + right.selectedRadius - 310.0f) < 0.01f &&
              std::fabs(right.Left() - (310.0f - right.Width())) < 0.01f,
          L"metrics scale with DPI, a past-the-end selection clamps, and right alignment hugs the edge", success);

    // A strip too narrow for the nominal gap shrinks it so every dot stays inside, never below two selected radii.
    const RedXePageIndicatorLayout squeezed =
        RedXePageIndicatorInStrip(0.0f, 0.0f, 100.0f, 20.0f, 96, 12, 0, RedXePageIndicatorAlign::Center);
    Check(squeezed.pageCount == 12 && squeezed.gap < 14.0f && squeezed.gap >= 8.0f && squeezed.Left() >= -0.01f &&
              squeezed.CenterX(11) + squeezed.selectedRadius <= 100.01f,
          L"a narrow strip closes the gap so the block fits", success);
    const RedXePageIndicatorLayout crowded =
        RedXePageIndicatorInStrip(0.0f, 0.0f, 40.0f, 20.0f, 96, 32, 0, RedXePageIndicatorAlign::Center);
    Check(crowded.pageCount == 32 && crowded.gap == 8.0f, L"the gap never drops below two selected radii", success);

    // Nothing to draw: one page, too many pages, an empty strip.
    Check(RedXePageIndicatorInStrip(0.0f, 0.0f, 400.0f, 20.0f, 96, 1, 0, RedXePageIndicatorAlign::Center).pageCount ==
                  0 &&
              RedXePageIndicatorInStrip(0.0f, 0.0f, 400.0f, 20.0f, 96, 33, 0, RedXePageIndicatorAlign::Center)
                      .pageCount == 0 &&
              RedXePageIndicatorInStrip(0.0f, 0.0f, 0.0f, 20.0f, 96, 3, 0, RedXePageIndicatorAlign::Center).pageCount ==
                  0 &&
              RedXePageIndicatorWidthPixels(1, 96) == 0.0f && RedXePageIndicatorWidthPixels(5, 96) == 64.0f,
          L"a single page, more than 32 pages, or no strip draws no control", success);

    // Hit testing: the whole strip height is a target, the nearest dot within half a gap wins, outside misses.
    Check(RedXePageIndicatorHit(centred, centred.CenterX(3), 101.0f) == 3 &&
              RedXePageIndicatorHit(centred, centred.CenterX(3) + 6.0f, 119.0f) == 3 &&
              RedXePageIndicatorHit(centred, centred.CenterX(3) + 8.0f, 110.0f) == 4 &&
              RedXePageIndicatorHit(centred, centred.CenterX(0) - 7.0f, 110.0f) == 0 &&
              RedXePageIndicatorHit(centred, centred.CenterX(0) - 9.0f, 110.0f) == UINT32_MAX &&
              RedXePageIndicatorHit(centred, centred.CenterX(2), 99.0f) == UINT32_MAX &&
              RedXePageIndicatorHit(centred, centred.CenterX(2), 121.0f) == UINT32_MAX &&
              RedXePageIndicatorHit(RedXePageIndicatorLayout{}, 0.0f, 0.0f) == UINT32_MAX,
          L"taps anywhere in the strip land on the nearest dot within half a gap", success);
}

void TestNonDivisibleGridEdges(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] non-divisible dashboard grid edges\n";
    constexpr std::string_view settingsJson =
        R"json({"version":{"major":5},"pages":[{"columns":[{"weight":2,"widget":{"plugin":"builtin.rotating-triangle"}},{"widget":{"plugin":"builtin.rotating-triangle"}}]}]})json";

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
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.rotating-triangle"}]},{"widgets":[{"plugin":"builtin.rotating-triangle"},{"plugin":"builtin.rotating-triangle"}]}]})json";

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
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.matrix-rain"}]},{"widgets":[{"plugin":"builtin.studio-clock"},{"plugin":"builtin.desk-clock"}]}]})json";

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
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.rotating-triangle"}]},{"widgets":[{"plugin":"builtin.gdi-orbit"}]}]})json";

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
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.rotating-triangle"}]},{"widgets":[{"plugin":"builtin.rotating-triangle"}]}]})json";

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
        R"json({"version":{"major":5},"pages":[{"widgets":[{"plugin":"builtin.rotating-triangle"}]},{"widgets":[{"plugin":"builtin.gdi-orbit"}]}]})json";

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
        R"json({"version":{"major":5},"pages":[{"name":"Clock","widgets":[{"plugin":"builtin.studio-clock"}]}]})json";
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
        R"json({"version":{"major":5},"pages":[{"name":"Clock","widgets":[{"plugin":"builtin.desk-clock"}]}]})json";
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
    (void)SetEnvironmentVariableW(L"REDXE_AUTOMATED_HOST", L"1");
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
    const HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole))
    {
        std::wcerr << L"OleInitialize failed.\n";
        return 1;
    }
    PluginHost::Instance().SetNetworkAccessEnabled(false);
    PluginHost::Instance().SetControlAccessEnabled(false);
    TestFrameScheduler(success);
    TestAdapterSelectionPolicy(success);
    TestRendererDeviceIdentity(success);
    TestWindowCapture(success);
    TestPageSwipePolicy(success);
    TestWidgetRaisePolicy(success);
    TestWidgetRaiseHost(success);
    TestHostChromeComposition(success);
    TestDockPlacement(success);
    TestDockAutohidePolicy(success);
    TestDockPresentation(success);
    TestWidgetRaiseNative(success);
    TestReleaseHostIntegration(success);
    TestStudioClockScheduling(success);
    TestDeskClockScheduling(success);
    TestDebugHostComposition(success);
    TestDataProviderLookup(success);
    TestProcessViewerSubscription(success);
    TestProcessViewerSubscription(success); // Repeat with nonzero process-lifetime diagnostics.
    TestSystemDataViewers(success);
    TestSubscriptionDrain(success);
    TestReservedSubscriptionDrain(success);
    TestGpuTargetSizeNotification(success);
    TestGpuPageLifetime(success);
    TestSharedPluginRuntime(success);
    TestHostRequestFrameAndWidgetStatus(success);
    TestWidgetSettingsPersist(success);
    TestHostJsonlLog(success);
    TestHostOwnedPlaceholderTiles(success);
    TestDashboardBackground(success);
    TestUnmappedCatalogModulePlaceholder(success);
    TestShortTilePageControl(success);
    TestWeatherPluginConstructs(success);
    TestNetworkLane(success);
    TestHostActionQueue(success);
    TestActionValidation(success);
    TestServiceLifetime(success);
    TestLauncherPluginConstructs(success);
    TestPublishedArraySchema(success);
    TestPageEdgeAffordancePolicy(success);
    TestPageEdgeAffordanceGeometry(success);
    TestWheelNavigationPolicy(success);
    TestPageIndicatorGeometry(success);
    TestNonDivisibleGridEdges(success);
    TestPromoteStagedDashboard(success);
    TestSwipeRendersPartiallyOffscreenGpuWidgets(success);
    TestSettingsReloadKeepsCurrentPage(success);
    TestAdoptPrimaryDashboardIsTransactional(success);
    TestNativeWindowNeighborSwipe(success);
    std::wcout << (success ? L"HostPluginTests passed.\n" : L"HostPluginTests failed.\n");
    OleUninitialize();
    return success ? 0 : 1;
}
