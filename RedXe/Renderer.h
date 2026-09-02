#pragma once

#include "PlugInterfaces/Widget.h"

#include <array>
#include <cstddef>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

class DashboardHost;

class Renderer final
{
  public:
    static constexpr UINT kOcclusionStatusMessage = WM_APP + 1;

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
    HRESULT Render(float elapsedSeconds, float deltaSeconds) noexcept;
    HRESULT ProbeOcclusion() noexcept;
    [[nodiscard]] bool IsSuspended() const noexcept;
    [[nodiscard]] bool IsOccluded() const noexcept;
    [[nodiscard]] size_t LastFrameWidgetCount() const noexcept;
    [[nodiscard]] size_t LastFrameSuccessfulWidgetCount() const noexcept;

  private:
    static constexpr size_t kMaximumWidgetViewports = 32;
    static constexpr DXGI_FORMAT kTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    HRESULT CreateDeviceResources() noexcept;
    HRESULT CreateDevice(bool useWarp) noexcept;
    HRESULT CreateSwapChain() noexcept;
    HRESULT CreateRenderTarget(UINT width, UINT height) noexcept;
    HRESULT UpdateCachedViewports() noexcept;
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
    D3D_FEATURE_LEVEL _featureLevel = D3D_FEATURE_LEVEL_11_0;
    size_t _lastFrameWidgetCount = 0;
    size_t _lastFrameSuccessfulWidgetCount = 0;

    std::array<D3D11_VIEWPORT, kMaximumWidgetViewports> _widgetViewports{};
    std::array<D3D11_VIEWPORT, kMaximumWidgetViewports> _transitionWidgetViewports{};

    wil::com_ptr_nothrow<ID3D11Device> _device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> _context;
    wil::com_ptr_nothrow<IDXGIFactory2> _factory;
    wil::com_ptr_nothrow<IDXGISwapChain1> _swapChain;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> _renderTarget;
    DWORD _occlusionStatusCookie = 0;
    bool _occlusionStatusRegistered = false;
};
