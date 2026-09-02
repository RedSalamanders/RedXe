#include "DashboardHost.h"
#include "DeskClockTestContract.h"
#include "FrameScheduler.h"
#include "MatrixRainTestContract.h"
#include "PluginHost.h"
#include "PluginManager.h"
#include "ProcessViewerTestContract.h"
#include "Renderer.h"
#include "Settings.h"
#include "StudioClockTestContract.h"

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
    Check(SUCCEEDED(result) && descriptors && descriptorCount == 18, L"host data provider exposes its source datasets",
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
    Check(dashboard.GetNextFrameDelayMilliseconds(&delay) == S_OK && delay >= 1,
          L"settled System page publishes a scheduled delay", success);

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
    Check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 4,
          L"direct manipulation renders only the current and staged adjacent pages", success);
    (void)renderer.SetTransitionDashboard(nullptr);
    adjacentDashboard.Shutdown();
    (void)dashboard.SetHorizontalOffset(0);
    (void)renderer.RefreshLayout();
    renderer.Shutdown();
    dashboard.Shutdown();
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
    TestReleaseHostIntegration(success);
    TestStudioClockScheduling(success);
    TestDeskClockScheduling(success);
    TestDebugHostComposition(success);
    TestDataProviderLookup(success);
    TestProcessViewerSubscription(success);
    TestSystemDataViewers(success);
    TestNonDivisibleGridEdges(success);
    std::wcout << (success ? L"HostPluginTests passed.\n" : L"HostPluginTests failed.\n");
    return success ? 0 : 1;
}
