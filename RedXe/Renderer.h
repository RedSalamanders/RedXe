#pragma once

#include "AdapterSelection.h"
#include "HostChrome.h"
#include "PlugInterfaces/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

class DashboardHost;

class Renderer final
{
  public:
    static constexpr UINT kOcclusionStatusMessage = WM_APP + 1;

    // Identity of the live Direct3D device, logged as the `device-created` JSONL record and probed by tests.
    struct DeviceIdentity final
    {
        LUID adapterLuid{};
        bool warp = false;
        // True when the adapter was chosen because one of its outputs scans out the window's monitor. False on the
        // default-adapter fallback (window off every monitor, hidden test window) and on WARP.
        bool adapterOwnsWindowMonitor = false;
        std::array<wchar_t, 128> adapterName{};
    };

    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    HRESULT Initialize(HWND window, bool forceWarp, DashboardHost& dashboardHost) noexcept;
    void Shutdown() noexcept;
    HRESULT SetDpi(UINT dpi) noexcept;
    HRESULT Resize(UINT width, UINT height) noexcept;
    HRESULT RefreshLayout() noexcept;
    HRESULT SetTransitionDashboard(DashboardHost* dashboardHost) noexcept;
    HRESULT AdoptPrimaryDashboard(DashboardHost& dashboardHost) noexcept;
    HRESULT SetRaisedOverlay(size_t widgetIndex, const RECT& content, SIZE targetPixels = {}) noexcept;
    void ClearRaisedOverlay() noexcept;
    // Host chrome (edge bands, raise dim/shadow/close) drawn into the swap chain. Returns true when the state
    // changed, which is the caller's cue to invalidate exactly one frame.
    bool SetHostChrome(const HostChromeState& state) noexcept;
    [[nodiscard]] const HostChromeState& HostChromeStateView() const noexcept;
    [[nodiscard]] const HostChromeResources& HostChrome() const noexcept;
    [[nodiscard]] size_t LastFrameChromeQuadCount() const noexcept;
    HRESULT Render(float elapsedSeconds, float deltaSeconds) noexcept;
    // Separate from Render: dirty retained controls may prepare their bounded resources here.
    HRESULT PrepareWidgets(size_t observedWidget = SIZE_MAX, bool* observedChanged = nullptr,
                           uint64_t* changedWidgets = nullptr) noexcept;
    // UI-thread cached value supplied by Application; no registry/system-color queries in Prepare/Render.
    void SetAppearance(const RedXeAppearance& appearance) noexcept
    {
        _appearance = appearance;
    }
    HRESULT ProbeOcclusion() noexcept;
    // Rebuilds the device on the adapter that now owns the window's monitor. S_OK after a rebuild, S_FALSE when the
    // device already sits on that adapter (or the window is off every monitor, or WARP is forced), failure otherwise.
    HRESULT EnsureDeviceForWindowMonitor() noexcept;
    [[nodiscard]] DeviceIdentity DeviceInfo() const noexcept;
    // Frame-latency waitable object of the swap chain (maximum latency one). The UI thread waits on it before
    // building a frame so Present never blocks; null before device creation.
    [[nodiscard]] HANDLE FrameLatencyWaitableObject() const noexcept;
    [[nodiscard]] bool IsSuspended() const noexcept;
    [[nodiscard]] bool IsOccluded() const noexcept;
    [[nodiscard]] size_t LastFrameWidgetCount() const noexcept;
    [[nodiscard]] size_t LastFrameSuccessfulWidgetCount() const noexcept;
    [[nodiscard]] bool HasRaisedOverlay() const noexcept;
    [[nodiscard]] size_t RaisedOverlayIndex() const noexcept;
    [[nodiscard]] RECT RaisedContentRect() const noexcept;

  private:
    static constexpr size_t kMaximumWidgetViewports = 32;
    static constexpr DXGI_FORMAT kTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    struct ReportedTarget final
    {
        LONG cx = 0;
        LONG cy = 0;
        UINT dpi = 0;
    };

    HRESULT CreateDeviceResources() noexcept;
    HRESULT CreateDevice(bool useWarp) noexcept;
    // Adapter-of-output lookup for `monitor`: the hardware adapter with an output on that monitor, or ERROR_NOT_FOUND.
    static HRESULT FindAdapterForMonitor(HMONITOR monitor, wil::com_ptr_nothrow<IDXGIAdapter1>& adapter,
                                         LUID& adapterLuid) noexcept;
    void RecordDeviceIdentity(bool warp, bool adapterOwnsWindowMonitor) noexcept;
    HRESULT CreateSwapChain() noexcept;
    HRESULT CreateRenderTarget(UINT width, UINT height) noexcept;
    HRESULT UpdateCachedViewports() noexcept;
    // Notifies widgets whose largest drawn viewport changed. Called from UpdateCachedViewports, which already runs
    // exactly on the events that can change a viewport, so this never fires per frame or for a position-only change.
    void NotifyTargetSizes() noexcept;
    void ResetTargetSizes() noexcept;
    // Fills one tile with the host placeholder wash. Used when a widget instance failed to construct or reported
    // itself unavailable, so a failed tile reads as failed instead of stale or blank.
    void DrawPlaceholder(const D3D11_VIEWPORT& viewport) noexcept;
#if defined(_DEBUG)
    // Bounded check of the pipeline-state contract Widget.h states: the host binds only render target and viewport,
    // so a widget that leaves scissor clipping enabled silently breaks whichever sibling draws next.
    void ValidateWidgetPipelineState(size_t index) noexcept;
#endif
    HRESULT NotifyDeviceCreated() noexcept;
    void NotifyDeviceLost() noexcept;
    HRESULT RecoverDevice() noexcept;
    void ReleaseDeviceResources() noexcept;

    static bool IsDeviceLost(HRESULT result) noexcept;

    HWND _window = nullptr;
    DashboardHost* _dashboardHost = nullptr;
    DashboardHost* _transitionDashboardHost = nullptr;
    bool _forceWarp = false;
    bool _suspended = true;
    bool _occluded = false;
    bool _gpuWidgetsDeviceReady = false;
    bool _transitionWidgetsDeviceReady = false;
    UINT _width = 0;
    UINT _height = 0;
    UINT _dpi = USER_DEFAULT_SCREEN_DPI;
    RedXeAppearance _appearance;
    D3D_FEATURE_LEVEL _featureLevel = D3D_FEATURE_LEVEL_11_0;
    size_t _lastFrameWidgetCount = 0;
    size_t _lastFrameSuccessfulWidgetCount = 0;
    size_t _lastFrameChromeQuadCount = 0;
    HostChromeResources _hostChrome;
    HostChromeState _hostChromeState{};
    bool _raisedOverlayActive = false;
    size_t _raisedOverlayIndex = SIZE_MAX;
    RECT _raisedContent{};
    SIZE _raisedTargetSize{};
    D3D11_VIEWPORT _raisedViewport{};

    std::array<D3D11_VIEWPORT, kMaximumWidgetViewports> _widgetViewports{};
    std::array<D3D11_VIEWPORT, kMaximumWidgetViewports> _transitionWidgetViewports{};
    // Last size reported to each widget. A zeroed entry means "not yet reported", so the first notification after
    // device creation always fires.
    std::array<ReportedTarget, kMaximumWidgetViewports> _widgetTargetSizes{};
    std::array<ReportedTarget, kMaximumWidgetViewports> _transitionTargetSizes{};
    std::array<HRESULT, kMaximumWidgetViewports> _lastLoggedRenderFailure{};

    wil::com_ptr_nothrow<ID3D11Device> _device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> _context;
    wil::com_ptr_nothrow<ID3D11DeviceContext1> _context1;
    wil::com_ptr_nothrow<IDXGIFactory2> _factory;
    wil::com_ptr_nothrow<IDXGISwapChain1> _swapChain;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> _renderTarget;
    wil::unique_handle _frameLatencyWaitable;
    DeviceIdentity _deviceInfo{};
    // Monitor the device was matched against; null on the default-adapter fallback.
    HMONITOR _deviceMonitor = nullptr;
    DWORD _occlusionStatusCookie = 0;
    bool _occlusionStatusRegistered = false;
};
