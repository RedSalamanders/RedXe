#include "DashboardHost.h"
#include "FrameScheduler.h"
#include "MatrixRainTestContract.h"
#include "PluginManager.h"
#include "Renderer.h"
#include "Settings.h"

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
        std::uint32_t processed = 0;
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
    std::uint64_t privateBytes = 0;
    std::uint64_t workingSetBytes = 0;
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
        const WidgetGridPlacement releaseGridPlacement = plugins.WidgetGridPlacementAt(0);
        Check(plugins.GridColumns() == 32 && plugins.GridRows() == 9 &&
                  std::string_view(plugins.WidgetInstanceIdAt(0)) == "matrix-rain.1" &&
                  releaseGridPlacement.column == 0 && releaseGridPlacement.row == 0 &&
                  releaseGridPlacement.columnSpan == 32 && releaseGridPlacement.rowSpan == 9,
              L"Release manager stages only the active page and preserves its full-grid instance", success);
        Check(GetModuleHandleW(L"RotatingTriangle.dll") == nullptr, L"Release composition does not load triangle DLL",
              success);
        Check(GetModuleHandleW(L"GdiOrbit.dll") == nullptr, L"Release composition does not load GDI DLL", success);
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
        MatrixRainTestDiagnostics shortDiagnostics{sizeof(std::uint32_t), 0, 0, 0};
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
        changed.dashboard.activePageId = changed.dashboard.pages[1].id;
        result = plugins.Reconfigure(changed);
        Check(SUCCEEDED(result) && std::string_view(plugins.WidgetInstanceIdAt(0)) == "matrix-rain.alt",
              L"host transactionally switches to the configured alternate page", success);
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
        Check(FAILED(invalidResult) && plugins.WidgetCount() == 1 &&
                  std::string_view(plugins.WidgetInstanceIdAt(0)) == "matrix-rain.alt",
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
              dashboard.PlacementAt(1) == WidgetPlacement{640.0f, 0.0f, 640.0f, 720.0f} &&
              dashboard.PlacementAt(2) == WidgetPlacement{1280.0f, 0.0f, 640.0f, 720.0f} &&
              dashboard.PlacementAt(3) == WidgetPlacement{1920.0f, 0.0f, 640.0f, 720.0f},
          L"Debug dashboard maps four adjacent 8x9 grid regions onto the design canvas", success);
    HWND gdiContainer = GetWindow(window.Get(), GW_CHILD);
    RECT gdiBounds{};
    if (gdiContainer && GetWindowRect(gdiContainer, &gdiBounds))
    {
        MapWindowPoints(HWND_DESKTOP, window.Get(), reinterpret_cast<POINT*>(&gdiBounds), 2);
    }
    Check(gdiContainer && gdiBounds.left == 1280 && gdiBounds.top == 0 && gdiBounds.right == 1920 &&
              gdiBounds.bottom == 720,
          L"native GDI container uses the same configured grid bounds", success);
    if (FAILED(result))
    {
        return;
    }

    result = dashboard.SetWindowWidgetsVisible(true);
    if (SUCCEEDED(result))
    {
        result = dashboard.SetWindowWidgetsVisible(false);
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
        R"json({"$schema":"RedXe.settings.schema.json","schemaVersion":3,"plugins":[{"id":"builtin.rotating-triangle","enabled":true,"private":{}}],"dashboard":{"grid":{"columns":7,"rows":3},"activePageId":"page.uneven","pages":[{"id":"page.uneven","name":"Uneven","widgets":[{"id":"triangle.left","pluginId":"builtin.rotating-triangle","typeId":"rotating-triangle","placement":{"column":0,"row":0,"columnSpan":3,"rowSpan":3},"private":{}},{"id":"triangle.right","pluginId":"builtin.rotating-triangle","typeId":"rotating-triangle","placement":{"column":3,"row":0,"columnSpan":4,"rowSpan":3},"private":{}}]}]}})json";

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
    Check(SUCCEEDED(result), L"uneven seven-column dashboard initializes", success);
    if (FAILED(result))
    {
        return;
    }

    const RECT left = dashboard.PixelBoundsAt(0, 1001, 333);
    const RECT right = dashboard.PixelBoundsAt(1, 1001, 333);
    Check(left.left == 0 && left.right == right.left && right.right == 1001 && left.top == 0 && right.top == 0 &&
              left.bottom == 333 && right.bottom == 333,
          L"adjacent grid regions share one rounded physical edge without gaps or overlap", success);

    Renderer renderer;
    result = renderer.Initialize(window.Get(), true, dashboard);
    if (SUCCEEDED(result))
    {
        result = renderer.Render(0.0f, 0.0f);
    }
    Check(SUCCEEDED(result) && renderer.LastFrameWidgetCount() == 2 && renderer.LastFrameSuccessfulWidgetCount() == 2,
          L"D3D viewports use the same uneven physical grid edges", success);
    renderer.Shutdown();
    dashboard.Shutdown();
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

    for (std::uint32_t frame = 0; frame < 1024; ++frame)
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

    std::uint64_t frames = 0;
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
        std::uint64_t parsed = 0;
        for (const wchar_t character : value)
        {
            if (character < L'0' || character > L'9')
            {
                return false;
            }
            parsed = parsed * 10 + static_cast<std::uint64_t>(character - L'0');
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
    if (argumentCount != 1)
    {
        std::wcerr << L"Unknown HostPluginTests argument.\n";
        return 2;
    }

    bool success = true;
    TestFrameScheduler(success);
    TestReleaseHostIntegration(success);
    TestDebugHostComposition(success);
    TestNonDivisibleGridEdges(success);
    std::wcout << (success ? L"HostPluginTests passed.\n" : L"HostPluginTests failed.\n");
    return success ? 0 : 1;
}
