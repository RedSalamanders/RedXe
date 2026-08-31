#include "Renderer.h"

#include "DashboardHost.h"

#include <array>
#include <cmath>
#include <utility>

Renderer::~Renderer()
{
    Shutdown();
}

HRESULT Renderer::Initialize(HWND window, bool forceWarp, DashboardHost& dashboardHost) noexcept
{
    if (_window || _dashboardHost || !window || dashboardHost.WidgetCount() == 0 ||
        dashboardHost.WidgetCount() > kMaximumWidgetViewports)
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
    _forceWarp = false;
    _dpi = USER_DEFAULT_SCREEN_DPI;
    _lastFrameWidgetCount = 0;
    _lastFrameSuccessfulWidgetCount = 0;
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

HRESULT Renderer::UpdateCachedViewports() noexcept
{
    if (!_dashboardHost || _dashboardHost->WidgetCount() > _widgetViewports.size() || _width == 0 || _height == 0)
    {
        return E_UNEXPECTED;
    }

    for (std::size_t index = 0; index < _dashboardHost->WidgetCount(); ++index)
    {
        const RECT bounds = _dashboardHost->PixelBoundsAt(index, _width, _height);
        if (bounds.left < 0 || bounds.top < 0 || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        {
            return E_INVALIDARG;
        }

        D3D11_VIEWPORT& viewport = _widgetViewports[index];
        viewport.TopLeftX = static_cast<float>(bounds.left);
        viewport.TopLeftY = static_cast<float>(bounds.top);
        viewport.Width = static_cast<float>(bounds.right - bounds.left);
        viewport.Height = static_cast<float>(bounds.bottom - bounds.top);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
    }
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

    std::size_t initializedCount = 0;
    for (std::size_t index = 0; index < _dashboardHost->WidgetCount(); ++index)
    {
        IRedXeGpuWidget* widget = _dashboardHost->GpuWidgetAt(index);
        if (!widget)
        {
            continue;
        }
        const HRESULT result = widget->OnDeviceCreated(&context);
        if (FAILED(result))
        {
            for (std::size_t previous = 0; previous < initializedCount; ++previous)
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
    return S_OK;
}

void Renderer::NotifyDeviceLost() noexcept
{
    if (!_gpuWidgetsDeviceReady || !_dashboardHost)
    {
        return;
    }

    for (std::size_t index = 0; index < _dashboardHost->WidgetCount(); ++index)
    {
        IRedXeGpuWidget* widget = _dashboardHost->GpuWidgetAt(index);
        if (widget)
        {
            widget->OnDeviceLost();
        }
    }
    _gpuWidgetsDeviceReady = false;
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

    for (std::size_t index = 0; index < _dashboardHost->WidgetCount(); ++index)
    {
        IRedXeGpuWidget* widget = _dashboardHost->GpuWidgetAt(index);
        const D3D11_VIEWPORT& viewport = _widgetViewports[index];
        if (!widget || viewport.Width < 1.0f || viewport.Height < 1.0f)
        {
            continue;
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
            const HRESULT recoveryResult = RecoverDevice();
            return SUCCEEDED(recoveryResult) ? S_FALSE : recoveryResult;
        }
        if (FAILED(widgetResult))
        {
            OutputDebugStringW(L"A GPU widget failed to render; continuing with remaining widgets.\n");
            continue;
        }
        ++_lastFrameSuccessfulWidgetCount;
    }

    const HRESULT result = _swapChain->Present(1, 0);
    if (IsDeviceLost(result))
    {
        const HRESULT recoveryResult = RecoverDevice();
        return SUCCEEDED(recoveryResult) ? S_FALSE : recoveryResult;
    }
    _occluded = result == DXGI_STATUS_OCCLUDED;
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

std::size_t Renderer::LastFrameWidgetCount() const noexcept
{
    return _lastFrameWidgetCount;
}

std::size_t Renderer::LastFrameSuccessfulWidgetCount() const noexcept
{
    return _lastFrameSuccessfulWidgetCount;
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
