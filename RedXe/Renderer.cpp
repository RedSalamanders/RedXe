#include "Renderer.h"

#include "DashboardHost.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

Renderer::~Renderer()
{
    Shutdown();
}

HRESULT Renderer::Initialize(HWND window, bool forceWarp, DashboardHost& dashboardHost) noexcept
{
    if (_window || _dashboardHost || !window || dashboardHost.WidgetCount() > kMaximumWidgetViewports)
    {
        return E_INVALIDARG;
    }

    const UINT dpi = GetDpiForWindow(window);
    if (dpi == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    _window = window;
    _forceWarp = forceWarp;
    _dashboardHost = &dashboardHost;
    _dpi = dpi;
    return CreateDeviceResources();
}

void Renderer::Shutdown() noexcept
{
    ReleaseDeviceResources();
    _window = nullptr;
    _dashboardHost = nullptr;
    _transitionDashboardHost = nullptr;
    _forceWarp = false;
    _dpi = USER_DEFAULT_SCREEN_DPI;
    _lastFrameWidgetCount = 0;
    _lastFrameSuccessfulWidgetCount = 0;
    _raisedOverlayActive = false;
    _raisedOverlayIndex = SIZE_MAX;
    _raisedContent = {};
    _raisedViewport = {};
}

HRESULT Renderer::SetTransitionDashboard(DashboardHost* dashboardHost) noexcept
{
    if (dashboardHost && dashboardHost->WidgetCount() > _transitionWidgetViewports.size())
    {
        return E_INVALIDARG;
    }
    if (_transitionWidgetsDeviceReady && _transitionDashboardHost)
    {
        for (size_t index = 0; index < _transitionDashboardHost->WidgetCount(); ++index)
        {
            if (IRedXeGpuWidget* widget = _transitionDashboardHost->GpuWidgetAt(index))
                widget->OnDeviceLost();
        }
    }
    _transitionWidgetsDeviceReady = false;
    _transitionDashboardHost = dashboardHost;
    if (!dashboardHost)
    {
        return S_OK;
    }
    if (!_device || !_gpuWidgetsDeviceReady)
    {
        return S_OK;
    }
    const RedXeGpuDeviceContext context{sizeof(RedXeGpuDeviceContext), _device.get(), kTargetFormat, _featureLevel};
    for (size_t index = 0; index < dashboardHost->WidgetCount(); ++index)
    {
        if (IRedXeGpuWidget* widget = dashboardHost->GpuWidgetAt(index))
        {
            const HRESULT result = widget->OnDeviceCreated(&context);
            if (FAILED(result))
            {
                for (size_t previous = 0; previous < index; ++previous)
                {
                    if (IRedXeGpuWidget* initialized = dashboardHost->GpuWidgetAt(previous))
                        initialized->OnDeviceLost();
                }
                _transitionDashboardHost = nullptr;
                return result;
            }
        }
    }
    _transitionWidgetsDeviceReady = true;
    return UpdateCachedViewports();
}

HRESULT Renderer::AdoptPrimaryDashboard(DashboardHost& dashboardHost) noexcept
{
    if (!_window || !_device || dashboardHost.WidgetCount() > kMaximumWidgetViewports)
    {
        return E_INVALIDARG;
    }
    if (_dashboardHost == &dashboardHost)
    {
        _transitionDashboardHost = nullptr;
        _transitionWidgetsDeviceReady = false;
        return (_width == 0 || _height == 0) ? S_OK : UpdateCachedViewports();
    }

    const bool keepDevice = _transitionDashboardHost == &dashboardHost && _transitionWidgetsDeviceReady;
    DashboardHost* const previousPrimary = _dashboardHost;
    DashboardHost* const previousTransition = _transitionDashboardHost;
    const bool previousTransitionReady = _transitionWidgetsDeviceReady;
    const bool previousGpuReady = _gpuWidgetsDeviceReady;

    _dashboardHost = &dashboardHost;
    _transitionDashboardHost = nullptr;
    _transitionWidgetsDeviceReady = false;
    if (keepDevice)
    {
        _gpuWidgetsDeviceReady = true;
    }

    HRESULT result = S_OK;
    if (keepDevice)
    {
        result = UpdateCachedViewports();
    }
    else
    {
        _gpuWidgetsDeviceReady = false;
        result = NotifyDeviceCreated();
        if (SUCCEEDED(result))
        {
            result = UpdateCachedViewports();
        }
    }
    if (FAILED(result))
    {
        if (!keepDevice)
        {
            for (size_t index = 0; index < dashboardHost.WidgetCount(); ++index)
            {
                if (IRedXeGpuWidget* widget = dashboardHost.GpuWidgetAt(index))
                {
                    widget->OnDeviceLost();
                }
            }
        }
        _dashboardHost = previousPrimary;
        _transitionDashboardHost = previousTransition;
        _transitionWidgetsDeviceReady = previousTransitionReady;
        _gpuWidgetsDeviceReady = previousGpuReady;
        return result;
    }

    if (previousGpuReady && previousPrimary && previousPrimary != &dashboardHost)
    {
        for (size_t index = 0; index < previousPrimary->WidgetCount(); ++index)
        {
            if (IRedXeGpuWidget* widget = previousPrimary->GpuWidgetAt(index))
            {
                widget->OnDeviceLost();
            }
        }
    }
    if (previousTransitionReady && previousTransition && previousTransition != &dashboardHost)
    {
        for (size_t index = 0; index < previousTransition->WidgetCount(); ++index)
        {
            if (IRedXeGpuWidget* widget = previousTransition->GpuWidgetAt(index))
            {
                widget->OnDeviceLost();
            }
        }
    }
    return S_OK;
}

HRESULT Renderer::SetRaisedOverlay(size_t widgetIndex, const RECT& content) noexcept
{
    if (!_dashboardHost || widgetIndex >= _dashboardHost->WidgetCount() || content.right <= content.left ||
        content.bottom <= content.top)
    {
        return E_INVALIDARG;
    }
    _raisedOverlayActive = true;
    _raisedOverlayIndex = widgetIndex;
    _raisedContent = content;
    _raisedViewport = {};
    return (!_suspended && _width != 0 && _height != 0) ? UpdateCachedViewports() : S_OK;
}

void Renderer::ClearRaisedOverlay() noexcept
{
    _raisedOverlayActive = false;
    _raisedOverlayIndex = SIZE_MAX;
    _raisedContent = {};
    _raisedViewport = {};
    if (!_suspended && _dashboardHost && _width != 0 && _height != 0)
    {
        (void)UpdateCachedViewports();
    }
}

HRESULT Renderer::SetDpi(UINT dpi) noexcept
{
    if (dpi == 0)
    {
        return E_INVALIDARG;
    }
    _dpi = dpi;
    return S_OK;
}

HRESULT Renderer::CreateDeviceResources() noexcept
{
    ReleaseDeviceResources();

    HRESULT result = CreateDevice(_forceWarp);
    if (FAILED(result) && !_forceWarp)
    {
        OutputDebugStringW(L"Hardware Direct3D initialization failed; trying WARP.\n");
        result = CreateDevice(true);
    }
    if (FAILED(result))
    {
        return result;
    }

    result = CreateSwapChain();
    if (FAILED(result))
    {
        return result;
    }

    result = NotifyDeviceCreated();
    if (FAILED(result))
    {
        return result;
    }

    RECT client{};
    if (!GetClientRect(_window, &client))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const UINT width = static_cast<UINT>(client.right - client.left);
    const UINT height = static_cast<UINT>(client.bottom - client.top);
    if (width == 0 || height == 0)
    {
        _suspended = true;
        return S_OK;
    }

    return CreateRenderTarget(width, height);
}

HRESULT Renderer::CreateDevice(bool useWarp) noexcept
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    constexpr std::array featureLevels{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    auto create = [&](UINT requestedFlags) noexcept
    {
        _device.reset();
        _context.reset();
        _context1.reset();
        return D3D11CreateDevice(nullptr, useWarp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 requestedFlags, featureLevels.data(), static_cast<UINT>(featureLevels.size()),
                                 D3D11_SDK_VERSION, _device.put(), &_featureLevel, _context.put());
    };

    HRESULT result = create(flags);
#if defined(_DEBUG)
    if (result == DXGI_ERROR_SDK_COMPONENT_MISSING)
    {
        OutputDebugStringW(L"Direct3D debug layer is unavailable; continuing without it.\n");
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        result = create(flags);
    }
#endif
    return result;
}

HRESULT Renderer::CreateSwapChain() noexcept
{
    wil::com_ptr_nothrow<IDXGIDevice1> dxgiDevice;
    HRESULT result = _device.query_to(dxgiDevice.put());
    if (FAILED(result))
    {
        return result;
    }

    result = dxgiDevice->SetMaximumFrameLatency(1);
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<IDXGIAdapter> adapter;
    result = dxgiDevice->GetAdapter(adapter.put());
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<IDXGIFactory2> factory;
    result = adapter->GetParent(IID_PPV_ARGS(factory.put()));
    if (FAILED(result))
    {
        return result;
    }

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Format = kTargetFormat;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    result = factory->CreateSwapChainForHwnd(_device.get(), _window, &description, nullptr, nullptr, _swapChain.put());
    if (FAILED(result))
    {
        return result;
    }

    result = factory->MakeWindowAssociation(_window, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(result))
    {
        return result;
    }

    DWORD occlusionStatusCookie = 0;
    result = factory->RegisterOcclusionStatusWindow(_window, kOcclusionStatusMessage, &occlusionStatusCookie);
    if (FAILED(result))
    {
        return result;
    }

    _factory = std::move(factory);
    _occlusionStatusCookie = occlusionStatusCookie;
    _occlusionStatusRegistered = true;
    return S_OK;
}

HRESULT Renderer::CreateRenderTarget(UINT width, UINT height) noexcept
{
    wil::com_ptr_nothrow<ID3D11Texture2D> backBuffer;
    HRESULT result = _swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.put()));
    if (FAILED(result))
    {
        return result;
    }

    result = _device->CreateRenderTargetView(backBuffer.get(), nullptr, _renderTarget.put());
    if (FAILED(result))
    {
        return result;
    }

    _width = width;
    _height = height;
    _suspended = false;
    _occluded = false;
    return UpdateCachedViewports();
}

#if defined(_DEBUG)
void Renderer::ValidateWidgetPipelineState(size_t index) noexcept
{
    if (!_context)
    {
        return;
    }
    wil::com_ptr_nothrow<ID3D11RasterizerState> rasterizer;
    _context->RSGetState(rasterizer.put());
    if (!rasterizer)
    {
        return;
    }
    D3D11_RASTERIZER_DESC description{};
    rasterizer->GetDesc(&description);
    if (description.ScissorEnable)
    {
        wchar_t message[160]{};
        (void)swprintf_s(message, L"Widget %zu left ScissorEnable set; the next widget may render clipped.\n", index);
        OutputDebugStringW(message);
    }
}
#endif

void Renderer::DrawPlaceholder(const D3D11_VIEWPORT& viewport) noexcept
{
    if (!_context1 || !_renderTarget || viewport.Width < 1.0f || viewport.Height < 1.0f)
    {
        return;
    }
    constexpr std::array placeholderColor{0.180f, 0.055f, 0.075f, 1.0f};
    const D3D11_RECT rect{static_cast<LONG>(viewport.TopLeftX), static_cast<LONG>(viewport.TopLeftY),
                          static_cast<LONG>(viewport.TopLeftX + viewport.Width),
                          static_cast<LONG>(viewport.TopLeftY + viewport.Height)};
    _context1->ClearView(_renderTarget.get(), placeholderColor.data(), &rect, 1);
}

void Renderer::ResetTargetSizes() noexcept
{
    _widgetTargetSizes = {};
    _transitionTargetSizes = {};
}

void Renderer::NotifyTargetSizes() noexcept
{
    const auto notify = [this](DashboardHost* dashboard,
                               const std::array<D3D11_VIEWPORT, kMaximumWidgetViewports>& viewports,
                               std::array<SIZE, kMaximumWidgetViewports>& reported, bool includeRaised) noexcept
    {
        if (!dashboard)
        {
            return;
        }
        const size_t count = std::min(dashboard->WidgetCount(), viewports.size());
        for (size_t index = 0; index < count; ++index)
        {
            const D3D11_VIEWPORT& viewport = viewports[index];
            SIZE size{static_cast<LONG>(viewport.Width + 0.5f), static_cast<LONG>(viewport.Height + 0.5f)};
            // A raised widget is drawn at its tile and again at the overlay slice in the same frame, so report the
            // larger of the two: the smaller draw is a minification the sampler already handles well.
            if (includeRaised && _raisedOverlayActive && index == _raisedOverlayIndex)
            {
                size.cx = std::max(size.cx, static_cast<LONG>(_raisedViewport.Width + 0.5f));
                size.cy = std::max(size.cy, static_cast<LONG>(_raisedViewport.Height + 0.5f));
            }
            if (size.cx <= 0 || size.cy <= 0 || (size.cx == reported[index].cx && size.cy == reported[index].cy))
            {
                continue;
            }
            IRedXeGpuWidget* widget = dashboard->GpuWidgetAt(index);
            if (!widget)
            {
                reported[index] = size;
                continue;
            }
            const RedXeGpuTargetSizeContext context{
                sizeof(RedXeGpuTargetSizeContext),
                static_cast<uint32_t>(size.cx),
                static_cast<uint32_t>(size.cy),
                _dpi,
            };
            // A failed rebuild is isolated: the widget keeps whatever resources it already had and still renders.
            if (FAILED(widget->OnTargetSizeChanged(&context)))
            {
                OutputDebugStringW(L"A GPU widget failed to resize its resources; keeping the previous ones.\n");
            }
            reported[index] = size;
        }
    };

    notify(_dashboardHost, _widgetViewports, _widgetTargetSizes, true);
    notify(_transitionDashboardHost, _transitionWidgetViewports, _transitionTargetSizes, false);
}

HRESULT Renderer::UpdateCachedViewports() noexcept
{
    // Render indexes the cached viewport arrays with a widget count bounded by PluginManager, so the two limits must
    // not drift apart. Raising kMaximumWidgetsPerPage alone fails here instead of overrunning a frame.
    static_assert(PluginManager::kMaximumWidgetInstances <= kMaximumWidgetViewports);

    if (!_dashboardHost || _dashboardHost->WidgetCount() > _widgetViewports.size() || _width == 0 || _height == 0)
    {
        return E_UNEXPECTED;
    }

    for (size_t index = 0; index < _dashboardHost->WidgetCount(); ++index)
    {
        const RECT bounds = _dashboardHost->PixelBoundsAt(index, _width, _height);
        D3D11_VIEWPORT& viewport = _widgetViewports[index];
        if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        {
            viewport = {};
            continue;
        }

        viewport.TopLeftX = static_cast<float>(bounds.left);
        viewport.TopLeftY = static_cast<float>(bounds.top);
        viewport.Width = static_cast<float>(bounds.right - bounds.left);
        viewport.Height = static_cast<float>(bounds.bottom - bounds.top);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
    }
    _raisedViewport = {};
    if (_raisedOverlayActive && _raisedOverlayIndex < _dashboardHost->WidgetCount() &&
        _raisedContent.right > _raisedContent.left && _raisedContent.bottom > _raisedContent.top)
    {
        _raisedViewport.TopLeftX = static_cast<float>(_raisedContent.left);
        _raisedViewport.TopLeftY = static_cast<float>(_raisedContent.top);
        _raisedViewport.Width = static_cast<float>(_raisedContent.right - _raisedContent.left);
        _raisedViewport.Height = static_cast<float>(_raisedContent.bottom - _raisedContent.top);
        _raisedViewport.MinDepth = 0.0f;
        _raisedViewport.MaxDepth = 1.0f;
    }
    if (_transitionDashboardHost)
    {
        for (size_t index = 0; index < _transitionDashboardHost->WidgetCount(); ++index)
        {
            const RECT bounds = _transitionDashboardHost->PixelBoundsAt(index, _width, _height);
            D3D11_VIEWPORT& viewport = _transitionWidgetViewports[index];
            if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
            {
                viewport = {};
                continue;
            }
            viewport.TopLeftX = static_cast<float>(bounds.left);
            viewport.TopLeftY = static_cast<float>(bounds.top);
            viewport.Width = static_cast<float>(bounds.right - bounds.left);
            viewport.Height = static_cast<float>(bounds.bottom - bounds.top);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
        }
    }
    NotifyTargetSizes();
    return S_OK;
}

HRESULT Renderer::NotifyDeviceCreated() noexcept
{
    if (!_dashboardHost || !_device || _gpuWidgetsDeviceReady)
    {
        return E_UNEXPECTED;
    }

    const RedXeGpuDeviceContext context{
        sizeof(RedXeGpuDeviceContext),
        _device.get(),
        kTargetFormat,
        _featureLevel,
    };

    size_t initializedCount = 0;
    for (size_t index = 0; index < _dashboardHost->WidgetCount(); ++index)
    {
        IRedXeGpuWidget* widget = _dashboardHost->GpuWidgetAt(index);
        if (!widget)
        {
            continue;
        }
        const HRESULT result = widget->OnDeviceCreated(&context);
        if (FAILED(result))
        {
            for (size_t previous = 0; previous < initializedCount; ++previous)
            {
                IRedXeGpuWidget* initializedWidget = _dashboardHost->GpuWidgetAt(previous);
                if (initializedWidget)
                {
                    initializedWidget->OnDeviceLost();
                }
            }
            return result;
        }
        initializedCount = index + 1;
    }

    _gpuWidgetsDeviceReady = true;
    ResetTargetSizes();
    if (_transitionDashboardHost)
    {
        _transitionWidgetsDeviceReady = false;
        return SetTransitionDashboard(_transitionDashboardHost);
    }
    return S_OK;
}

void Renderer::NotifyDeviceLost() noexcept
{
    if (!_gpuWidgetsDeviceReady || !_dashboardHost)
    {
        return;
    }

    for (size_t index = 0; index < _dashboardHost->WidgetCount(); ++index)
    {
        IRedXeGpuWidget* widget = _dashboardHost->GpuWidgetAt(index);
        if (widget)
        {
            widget->OnDeviceLost();
        }
    }
    if (_transitionWidgetsDeviceReady && _transitionDashboardHost)
    {
        for (size_t index = 0; index < _transitionDashboardHost->WidgetCount(); ++index)
        {
            if (IRedXeGpuWidget* widget = _transitionDashboardHost->GpuWidgetAt(index))
                widget->OnDeviceLost();
        }
        _transitionWidgetsDeviceReady = false;
    }
    _gpuWidgetsDeviceReady = false;
    ResetTargetSizes();
}

HRESULT Renderer::Resize(UINT width, UINT height) noexcept
{
    if (!_swapChain)
    {
        return E_UNEXPECTED;
    }

    if (width == 0 || height == 0)
    {
        _width = 0;
        _height = 0;
        _suspended = true;
        _occluded = false;
        return S_OK;
    }

    if (width == _width && height == _height && _renderTarget)
    {
        _suspended = false;
        _occluded = false;
        return S_OK;
    }

    _context->OMSetRenderTargets(0, nullptr, nullptr);
    _renderTarget.reset();
    const HRESULT result = _swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (IsDeviceLost(result))
    {
        return RecoverDevice();
    }
    if (FAILED(result))
    {
        return result;
    }

    return CreateRenderTarget(width, height);
}

HRESULT Renderer::RefreshLayout() noexcept
{
    return !_suspended && _renderTarget ? UpdateCachedViewports() : S_OK;
}

HRESULT Renderer::Render(float elapsedSeconds, float deltaSeconds) noexcept
{
    _lastFrameWidgetCount = 0;
    _lastFrameSuccessfulWidgetCount = 0;
    if (_suspended)
    {
        return S_OK;
    }
    if (!_context || !_swapChain || !_renderTarget || !_dashboardHost || !_gpuWidgetsDeviceReady ||
        !std::isfinite(elapsedSeconds) || !std::isfinite(deltaSeconds) || elapsedSeconds < 0.0f || deltaSeconds < 0.0f)
    {
        return E_UNEXPECTED;
    }

    constexpr std::array clearColor{0.025f, 0.035f, 0.075f, 1.0f};
    ID3D11RenderTargetView* renderTargets[] = {_renderTarget.get()};
    _context->OMSetRenderTargets(1, renderTargets, nullptr);
    _context->ClearRenderTargetView(_renderTarget.get(), clearColor.data());

    const size_t widgetCount = _dashboardHost->WidgetCount();
    const auto drawWidget = [&](size_t index, const D3D11_VIEWPORT& viewport) noexcept -> HRESULT
    {
        if (viewport.Width < 1.0f || viewport.Height < 1.0f)
        {
            return S_FALSE;
        }
        if (_dashboardHost->RequiresPlaceholderAt(index))
        {
            DrawPlaceholder(viewport);
            return S_FALSE;
        }
        IRedXeGpuWidget* widget = _dashboardHost->GpuWidgetAt(index);
        if (!widget)
        {
            return S_FALSE;
        }

        const RedXeWidgetFrameContext widgetFrame{
            sizeof(RedXeWidgetFrameContext),
            static_cast<UINT>(viewport.Width + 0.5f),
            static_cast<UINT>(viewport.Height + 0.5f),
            _dpi,
            elapsedSeconds,
            deltaSeconds,
        };
        const RedXeGpuFrameContext gpuFrame{
            sizeof(RedXeGpuFrameContext),
            &widgetFrame,
            _context.get(),
            viewport,
        };

        ++_lastFrameWidgetCount;
        _context->OMSetRenderTargets(1, renderTargets, nullptr);
        _context->RSSetViewports(1, &viewport);
        const HRESULT widgetResult = widget->Render(&gpuFrame);
        if (IsDeviceLost(widgetResult))
        {
            return widgetResult;
        }
        if (FAILED(widgetResult))
        {
            OutputDebugStringW(L"A GPU widget failed to render; continuing with remaining widgets.\n");
            return S_FALSE;
        }
        ++_lastFrameSuccessfulWidgetCount;
#if defined(_DEBUG)
        ValidateWidgetPipelineState(index);
#endif
        return S_OK;
    };

    for (size_t index = 0; index < widgetCount; ++index)
    {
        const HRESULT widgetResult = drawWidget(index, _widgetViewports[index]);
        if (IsDeviceLost(widgetResult))
        {
            const HRESULT recoveryResult = RecoverDevice();
            return SUCCEEDED(recoveryResult) ? S_FALSE : recoveryResult;
        }
    }
    if (_raisedOverlayActive && _raisedOverlayIndex < widgetCount && _raisedViewport.Width >= 1.0f &&
        _raisedViewport.Height >= 1.0f)
    {
        const HRESULT raisedResult = drawWidget(_raisedOverlayIndex, _raisedViewport);
        if (IsDeviceLost(raisedResult))
        {
            const HRESULT recoveryResult = RecoverDevice();
            return SUCCEEDED(recoveryResult) ? S_FALSE : recoveryResult;
        }
    }

    if (!_raisedOverlayActive && _transitionDashboardHost && _transitionWidgetsDeviceReady)
    {
        for (size_t index = 0; index < _transitionDashboardHost->WidgetCount(); ++index)
        {
            const D3D11_VIEWPORT& viewport = _transitionWidgetViewports[index];
            if (viewport.Width < 1.0f || viewport.Height < 1.0f)
                continue;
            if (_transitionDashboardHost->RequiresPlaceholderAt(index))
            {
                DrawPlaceholder(viewport);
                continue;
            }
            IRedXeGpuWidget* widget = _transitionDashboardHost->GpuWidgetAt(index);
            if (!widget)
                continue;
            const RedXeWidgetFrameContext widgetFrame{sizeof(RedXeWidgetFrameContext),
                                                      static_cast<UINT>(viewport.Width + 0.5f),
                                                      static_cast<UINT>(viewport.Height + 0.5f),
                                                      _dpi,
                                                      elapsedSeconds,
                                                      deltaSeconds};
            const RedXeGpuFrameContext gpuFrame{sizeof(RedXeGpuFrameContext), &widgetFrame, _context.get(), viewport};
            ++_lastFrameWidgetCount;
            _context->OMSetRenderTargets(1, renderTargets, nullptr);
            _context->RSSetViewports(1, &viewport);
            const HRESULT widgetResult = widget->Render(&gpuFrame);
            if (IsDeviceLost(widgetResult))
            {
                const HRESULT recoveryResult = RecoverDevice();
                return SUCCEEDED(recoveryResult) ? S_FALSE : recoveryResult;
            }
            if (SUCCEEDED(widgetResult))
                ++_lastFrameSuccessfulWidgetCount;
        }
    }

    const HRESULT result = _swapChain->Present(1, 0);
    if (IsDeviceLost(result))
    {
        const HRESULT recoveryResult = RecoverDevice();
        return SUCCEEDED(recoveryResult) ? S_FALSE : recoveryResult;
    }
    // A host-owned native child that covers the swap chain makes DXGI report OCCLUDED even though the window is
    // still the foreground dashboard. Treating that as real occlusion parked page-settle and hid the edge bands for
    // the rest of the session on any page that includes a window widget.
    const bool nativeCover = (_dashboardHost && _dashboardHost->HasWindowWidgets()) ||
                             (_transitionDashboardHost && _transitionDashboardHost->HasWindowWidgets());
    _occluded = result == DXGI_STATUS_OCCLUDED && !nativeCover;
    return _occluded ? S_OK : result;
}

HRESULT Renderer::ProbeOcclusion() noexcept
{
    if (!_swapChain || !_occluded)
    {
        return E_UNEXPECTED;
    }

    const HRESULT result = _swapChain->Present(0, DXGI_PRESENT_TEST);
    if (IsDeviceLost(result))
    {
        const HRESULT recoveryResult = RecoverDevice();
        return SUCCEEDED(recoveryResult) ? S_FALSE : recoveryResult;
    }
    if (result == DXGI_STATUS_OCCLUDED)
    {
        return S_FALSE;
    }
    if (FAILED(result))
    {
        return result;
    }

    _occluded = false;
    return S_OK;
}

bool Renderer::IsSuspended() const noexcept
{
    return _suspended;
}

bool Renderer::IsOccluded() const noexcept
{
    return _occluded;
}

size_t Renderer::LastFrameWidgetCount() const noexcept
{
    return _lastFrameWidgetCount;
}

size_t Renderer::LastFrameSuccessfulWidgetCount() const noexcept
{
    return _lastFrameSuccessfulWidgetCount;
}

bool Renderer::HasRaisedOverlay() const noexcept
{
    return _raisedOverlayActive;
}

size_t Renderer::RaisedOverlayIndex() const noexcept
{
    return _raisedOverlayIndex;
}

RECT Renderer::RaisedContentRect() const noexcept
{
    return _raisedContent;
}

HRESULT Renderer::RecoverDevice() noexcept
{
    OutputDebugStringW(L"Direct3D device was lost; rebuilding graphics resources.\n");
    return CreateDeviceResources();
}

void Renderer::ReleaseDeviceResources() noexcept
{
    if (_factory && _occlusionStatusRegistered)
    {
        _factory->UnregisterOcclusionStatus(_occlusionStatusCookie);
        _occlusionStatusCookie = 0;
        _occlusionStatusRegistered = false;
    }
    if (_context)
    {
        _context->ClearState();
        _context->Flush();
    }
    NotifyDeviceLost();

    _renderTarget.reset();
    _swapChain.reset();
    _factory.reset();
    _context1.reset();
    _context.reset();
    _device.reset();
    _width = 0;
    _height = 0;
    _suspended = true;
    _occluded = false;
}

bool Renderer::IsDeviceLost(HRESULT result) noexcept
{
    return result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ||
           result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}
