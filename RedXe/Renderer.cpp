#include "Renderer.h"

#include "DashboardHost.h"
#include "PluginHost.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>
#include <utility> // namespace

namespace
{
constexpr UINT kSwapChainFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
constexpr size_t kMaximumEnumeratedAdapters = 8;
constexpr size_t kMaximumEnumeratedOutputs = 32;
} // namespace

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
    _raisedTargetSize = {};
    _raisedViewport = {};
    _lastLoggedRenderFailure = {};
}

HRESULT Renderer::SetTransitionDashboard(DashboardHost* dashboardHost) noexcept
{
    if (dashboardHost && dashboardHost->WidgetCount() > _transitionWidgetViewports.size())
    {
        return E_INVALIDARG;
    }
    const bool incomingVisible = dashboardHost && dashboardHost->WidgetsVisible();
    if (dashboardHost)
    {
        const HRESULT result = dashboardHost->SetWidgetsVisible(false);
        if (FAILED(result))
            return result;
    }
    if (_transitionWidgetsDeviceReady && _transitionDashboardHost)
    {
        const HRESULT result = _transitionDashboardHost->SetWidgetsVisible(false);
        if (FAILED(result))
            return result;
        for (size_t index = 0; index < _transitionDashboardHost->WidgetCount(); ++index)
        {
            if (IRedXeGpuWidget* widget = _transitionDashboardHost->GpuWidgetAt(index))
                widget->OnDeviceLost();
        }
    }
    _transitionWidgetsDeviceReady = false;
    _transitionDashboardHost = dashboardHost;
    _transitionTargetSizes = {};
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
    // During device recovery both pages are initialized before the new render target exists. Its creation will
    // calculate viewports and send the initial size notifications for both pages together.
    if (_width == 0 || _height == 0)
    {
        return S_OK;
    }
    const HRESULT result = UpdateCachedViewports();
    return SUCCEEDED(result) ? dashboardHost->SetWidgetsVisible(incomingVisible) : result;
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
    const bool incomingVisible = dashboardHost.WidgetsVisible();
    if (!keepDevice)
    {
        const HRESULT hidden = dashboardHost.SetWidgetsVisible(false);
        if (FAILED(hidden))
            return hidden;
    }
    DashboardHost* const previousPrimary = _dashboardHost;
    DashboardHost* const previousTransition = _transitionDashboardHost;
    const bool previousTransitionReady = _transitionWidgetsDeviceReady;
    const bool previousGpuReady = _gpuWidgetsDeviceReady;
    const auto previousTargets = _widgetTargetSizes;
    const auto incomingTargets = _transitionTargetSizes;

    _dashboardHost = &dashboardHost;
    _transitionDashboardHost = nullptr;
    _transitionWidgetsDeviceReady = false;
    if (keepDevice)
    {
        _gpuWidgetsDeviceReady = true;
        _widgetTargetSizes = incomingTargets;
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
        if (SUCCEEDED(result))
        {
            result = dashboardHost.SetWidgetsVisible(incomingVisible);
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
        _widgetTargetSizes = previousTargets;
        _transitionTargetSizes = incomingTargets;
        return result;
    }

    if (previousGpuReady && previousPrimary && previousPrimary != &dashboardHost)
    {
        (void)previousPrimary->SetWidgetsVisible(false);
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
        (void)previousTransition->SetWidgetsVisible(false);
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

HRESULT Renderer::SetRaisedOverlay(size_t widgetIndex, const RECT& content, SIZE targetPixels) noexcept
{
    if (!_dashboardHost || widgetIndex >= _dashboardHost->WidgetCount() || content.right <= content.left ||
        content.bottom <= content.top)
    {
        return E_INVALIDARG;
    }
    _raisedOverlayActive = true;
    _raisedOverlayIndex = widgetIndex;
    _raisedContent = content;
    if (targetPixels.cx > 0 && targetPixels.cy > 0)
    {
        _raisedTargetSize = targetPixels;
    }
    else
    {
        _raisedTargetSize = SIZE{content.right - content.left, content.bottom - content.top};
    }
    _raisedViewport = {};
    return (!_suspended && _width != 0 && _height != 0) ? UpdateCachedViewports() : S_OK;
}

void Renderer::ClearRaisedOverlay() noexcept
{
    _raisedOverlayActive = false;
    _raisedOverlayIndex = SIZE_MAX;
    _raisedContent = {};
    _raisedTargetSize = {};
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
    if (_dpi == dpi)
    {
        return S_OK;
    }
    _dpi = dpi;
    if (_gpuWidgetsDeviceReady && !_suspended)
    {
        NotifyTargetSizes();
    }
    return S_OK;
}

HRESULT Renderer::CreateDeviceResources() noexcept
{
    const bool primaryVisible = _dashboardHost && _dashboardHost->WidgetsVisible();
    const bool transitionVisible = _transitionDashboardHost && _transitionDashboardHost->WidgetsVisible();
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

    result = CreateRenderTarget(width, height);
    if (SUCCEEDED(result) && _dashboardHost)
    {
        result = _dashboardHost->SetWidgetsVisible(primaryVisible);
    }
    if (SUCCEEDED(result) && _transitionDashboardHost)
    {
        result = _transitionDashboardHost->SetWidgetsVisible(transitionVisible);
    }
    return result;
}

HRESULT Renderer::FindAdapterForMonitor(HMONITOR monitor, wil::com_ptr_nothrow<IDXGIAdapter1>& adapter,
                                        LUID& adapterLuid) noexcept
{
    adapter.reset();
    adapterLuid = {};
    if (!monitor)
    {
        return E_INVALIDARG;
    }
    wil::com_ptr_nothrow<IDXGIFactory1> factory;
    const HRESULT factoryResult = CreateDXGIFactory1(IID_PPV_ARGS(factory.put()));
    if (FAILED(factoryResult))
    {
        return factoryResult;
    }

    // Bounded enumeration; the policy itself is the pure function in AdapterSelection.h, tested without DXGI.
    std::array<wil::com_ptr_nothrow<IDXGIAdapter1>, kMaximumEnumeratedAdapters> adapters;
    std::array<RedXeAdapterOutputRecord, kMaximumEnumeratedOutputs> records{};
    std::array<size_t, kMaximumEnumeratedOutputs> recordAdapters{};
    size_t recordCount = 0;
    for (UINT adapterIndex = 0; adapterIndex < adapters.size(); ++adapterIndex)
    {
        wil::com_ptr_nothrow<IDXGIAdapter1> candidate;
        if (factory->EnumAdapters1(adapterIndex, candidate.put()) != S_OK)
        {
            break;
        }
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(candidate->GetDesc1(&description)))
        {
            continue;
        }
        const bool software = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        for (UINT outputIndex = 0; recordCount < records.size(); ++outputIndex)
        {
            wil::com_ptr_nothrow<IDXGIOutput> output;
            if (candidate->EnumOutputs(outputIndex, output.put()) != S_OK)
            {
                break;
            }
            DXGI_OUTPUT_DESC outputDescription{};
            if (FAILED(output->GetDesc(&outputDescription)))
            {
                continue;
            }
            records[recordCount] =
                RedXeAdapterOutputRecord{description.AdapterLuid, outputDescription.Monitor, software};
            recordAdapters[recordCount] = adapterIndex;
            ++recordCount;
        }
        adapters[adapterIndex] = std::move(candidate);
    }

    const size_t selected = RedXeSelectAdapterRecordForMonitor(
        std::span<const RedXeAdapterOutputRecord>(records.data(), recordCount), monitor);
    if (selected == SIZE_MAX)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    adapter = adapters[recordAdapters[selected]];
    adapterLuid = records[selected].adapterLuid;
    return S_OK;
}

void Renderer::RecordDeviceIdentity(bool warp, bool adapterOwnsWindowMonitor) noexcept
{
    _deviceInfo = {};
    _deviceInfo.warp = warp;
    _deviceInfo.adapterOwnsWindowMonitor = adapterOwnsWindowMonitor;
    wil::com_ptr_nothrow<IDXGIDevice> dxgiDevice;
    wil::com_ptr_nothrow<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC description{};
    if (_device && SUCCEEDED(_device.query_to(dxgiDevice.put())) && SUCCEEDED(dxgiDevice->GetAdapter(adapter.put())) &&
        SUCCEEDED(adapter->GetDesc(&description)))
    {
        _deviceInfo.adapterLuid = description.AdapterLuid;
        static_assert(sizeof(_deviceInfo.adapterName) == sizeof(description.Description));
        std::memcpy(_deviceInfo.adapterName.data(), description.Description, sizeof(description.Description));
        _deviceInfo.adapterName.back() = L'\0';
    }

    // One record per device creation, never per frame: the receipt Core_PerformanceAndResources.md asks for.
    std::array<char, 128> name{};
    for (size_t index = 0; index + 1 < name.size() && _deviceInfo.adapterName[index] != L'\0'; ++index)
    {
        const wchar_t character = _deviceInfo.adapterName[index];
        name[index] = character < 0x80 ? static_cast<char>(character) : '?';
    }
    std::array<char, 320> message{};
    (void)std::snprintf(message.data(), message.size(),
                        "Direct3D device created on %s (LUID %08lX-%08lX, %s, adapter owns window monitor: %s).",
                        name.data(), static_cast<unsigned long>(_deviceInfo.adapterLuid.HighPart),
                        static_cast<unsigned long>(_deviceInfo.adapterLuid.LowPart), warp ? "WARP" : "hardware",
                        adapterOwnsWindowMonitor ? "yes" : "no");
    (void)RedXeHostLog(PluginHost::Instance().Interface(), RedXeLogLevelInfo, nullptr, nullptr, "device-created",
                       message.data());
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

    // Adapter of output: render on the GPU that scans out the window's monitor so presentation never crosses
    // adapters. A window on no monitor (hidden test host) keeps the default-adapter path.
    _deviceMonitor = nullptr;
    wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
    if (!useWarp)
    {
        const HMONITOR monitor = MonitorFromWindow(_window, MONITOR_DEFAULTTONULL);
        LUID adapterLuid{};
        if (monitor && SUCCEEDED(FindAdapterForMonitor(monitor, adapter, adapterLuid)))
        {
            _deviceMonitor = monitor;
        }
        else
        {
            adapter.reset();
        }
    }
    const D3D_DRIVER_TYPE driverType =
        useWarp ? D3D_DRIVER_TYPE_WARP : (adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE);

    auto create = [&](UINT requestedFlags) noexcept
    {
        _device.reset();
        _context.reset();
        _context1.reset();
        return D3D11CreateDevice(adapter.get(), driverType, nullptr, requestedFlags, featureLevels.data(),
                                 static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION, _device.put(),
                                 &_featureLevel, _context.put());
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
    if (FAILED(result))
    {
        return result;
    }
    result = _context.query_to(_context1.put());
    if (FAILED(result))
    {
        _context1.reset();
        _context.reset();
        _device.reset();
        return result;
    }
    RecordDeviceIdentity(useWarp, adapter != nullptr);
    return result;
}

HRESULT Renderer::EnsureDeviceForWindowMonitor() noexcept
{
    if (!_window || !_device || _forceWarp || _deviceInfo.warp)
    {
        return S_FALSE;
    }
    const HMONITOR monitor = MonitorFromWindow(_window, MONITOR_DEFAULTTONULL);
    if (!monitor || monitor == _deviceMonitor)
    {
        return S_FALSE;
    }
    wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
    LUID adapterLuid{};
    if (FAILED(FindAdapterForMonitor(monitor, adapter, adapterLuid)))
    {
        // DXGI cannot map this monitor to an output (for example a virtual display); keep the current device.
        return S_FALSE;
    }
    if (RedXeSameLuid(adapterLuid, _deviceInfo.adapterLuid))
    {
        _deviceMonitor = monitor;
        return S_FALSE;
    }
    // The window moved to a monitor scanned out by another GPU: rebuild on that adapter through the same path device
    // loss uses, so widgets see OnDeviceLost/OnDeviceCreated exactly once.
    const HRESULT result = CreateDeviceResources();
    return FAILED(result) ? result : S_OK;
}

Renderer::DeviceIdentity Renderer::DeviceInfo() const noexcept
{
    return _deviceInfo;
}

HANDLE Renderer::FrameLatencyWaitableObject() const noexcept
{
    return _frameLatencyWaitable.get();
}

HRESULT Renderer::CreateSwapChain() noexcept
{
    wil::com_ptr_nothrow<IDXGIDevice1> dxgiDevice;
    HRESULT result = _device.query_to(dxgiDevice.put());
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
    description.Flags = kSwapChainFlags;

    result = factory->CreateSwapChainForHwnd(_device.get(), _window, &description, nullptr, nullptr, _swapChain.put());
    if (FAILED(result))
    {
        return result;
    }

    // Maximum frame latency one, enforced by the swap chain's waitable object rather than a blocking Present: the UI
    // thread waits on this handle before building a frame, so input queued meanwhile is dispatched first.
    wil::com_ptr_nothrow<IDXGISwapChain2> swapChain2;
    result = _swapChain.query_to(swapChain2.put());
    if (FAILED(result))
    {
        return result;
    }
    result = swapChain2->SetMaximumFrameLatency(1);
    if (FAILED(result))
    {
        return result;
    }
    _frameLatencyWaitable.reset(swapChain2->GetFrameLatencyWaitableObject());
    if (!_frameLatencyWaitable)
    {
        return E_UNEXPECTED;
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
    const auto notify =
        [this](DashboardHost* dashboard, const std::array<D3D11_VIEWPORT, kMaximumWidgetViewports>& viewports,
               std::array<ReportedTarget, kMaximumWidgetViewports>& reported, bool includeRaised) noexcept
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
                const LONG raisedWidth =
                    _raisedTargetSize.cx > 0 ? _raisedTargetSize.cx : static_cast<LONG>(_raisedViewport.Width + 0.5f);
                const LONG raisedHeight =
                    _raisedTargetSize.cy > 0 ? _raisedTargetSize.cy : static_cast<LONG>(_raisedViewport.Height + 0.5f);
                size.cx = std::max(size.cx, raisedWidth);
                size.cy = std::max(size.cy, raisedHeight);
            }
            if (size.cx <= 0 || size.cy <= 0 ||
                (size.cx == reported[index].cx && size.cy == reported[index].cy && _dpi == reported[index].dpi))
            {
                continue;
            }
            IRedXeGpuWidget* widget = dashboard->GpuWidgetAt(index);
            if (!widget)
            {
                reported[index] = {size.cx, size.cy, _dpi};
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
            reported[index] = {size.cx, size.cy, _dpi};
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
            (void)RedXeHostLog(PluginHost::Instance().Interface(), RedXeLogLevelError, nullptr,
                               _dashboardHost->WidgetInstanceIdAt(index), "gpu-device-create-failed",
                               "IRedXeGpuWidget::OnDeviceCreated failed.", result);
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
    const HRESULT result = _swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, kSwapChainFlags);
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

HRESULT Renderer::PrepareWidgets(size_t observedWidget, bool* observedChanged, uint64_t* changedWidgets) noexcept
{
    if (observedChanged)
        *observedChanged = false;
    if (changedWidgets) *changedWidgets = 0;
    if (_suspended || _occluded)
        return S_FALSE;
    if (!_context || !_dashboardHost || !_gpuWidgetsDeviceReady)
        return E_UNEXPECTED;
    bool unbound = false;
    const auto prepare = [&](DashboardHost* dashboard, const auto& viewports, bool includeRaised) noexcept -> HRESULT
    {
        if (!dashboard)
            return S_OK;
        for (size_t index = 0; index < dashboard->WidgetCount(); ++index)
        {
            IRedXePreparedGpuWidget* widget = dashboard->PreparedGpuWidgetAt(index);
            if (!widget || dashboard->RequiresPlaceholderAt(index))
                continue;
            if (!unbound)
            {
                _context->OMSetRenderTargets(0, nullptr, nullptr);
                unbound = true;
            }
            const bool raised = includeRaised && _raisedOverlayActive && index == _raisedOverlayIndex;
            const D3D11_VIEWPORT& tile = viewports[index];
            const RedXeGpuPreparationContext context{
                sizeof(RedXeGpuPreparationContext),
                _dpi,
                static_cast<uint32_t>(std::max(0.0f, tile.Width) + 0.5f),
                static_cast<uint32_t>(std::max(0.0f, tile.Height) + 0.5f),
                raised ? static_cast<uint32_t>(_raisedTargetSize.cx > 0 ? _raisedTargetSize.cx
                                                                        : _raisedViewport.Width + 0.5f)
                       : 0U,
                raised ? static_cast<uint32_t>(_raisedTargetSize.cy > 0 ? _raisedTargetSize.cy
                                                                        : _raisedViewport.Height + 0.5f)
                       : 0U,
                _appearance};
            const HRESULT result = widget->Prepare(&context);
            if (observedChanged && dashboard == _dashboardHost && index == observedWidget && result == S_OK)
                *observedChanged = true;
            if (changedWidgets && dashboard == _dashboardHost && index < 64 && result == S_OK)
                *changedWidgets |= uint64_t{1} << index;
            if (IsDeviceLost(result))
                return result;
            // Other failures are local to the prepared widget, which suppresses stale input/composition.
        }
        return S_OK;
    };
    HRESULT result = prepare(_dashboardHost, _widgetViewports, true);
    if (SUCCEEDED(result) && !_raisedOverlayActive && _transitionWidgetsDeviceReady)
        result = prepare(_transitionDashboardHost, _transitionWidgetViewports, false);
    if (IsDeviceLost(result))
    {
        const HRESULT recovery = RecoverDevice();
        return SUCCEEDED(recovery) ? S_FALSE : recovery;
    }
    return result;
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
    const auto drawWidget = [&](size_t index, const D3D11_VIEWPORT& viewport, uint32_t viewId) noexcept -> HRESULT
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
            sizeof(RedXeGpuFrameContext), &widgetFrame, _context.get(), viewport, viewId,
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
            if (index < _lastLoggedRenderFailure.size() && _lastLoggedRenderFailure[index] != widgetResult)
            {
                _lastLoggedRenderFailure[index] = widgetResult;
                (void)RedXeHostLog(PluginHost::Instance().Interface(), RedXeLogLevelError, nullptr,
                                   _dashboardHost->WidgetInstanceIdAt(index), "gpu-render-failed",
                                   "IRedXeGpuWidget::Render failed.", widgetResult);
            }
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
        const HRESULT widgetResult = drawWidget(index, _widgetViewports[index], 0);
        if (IsDeviceLost(widgetResult))
        {
            const HRESULT recoveryResult = RecoverDevice();
            return SUCCEEDED(recoveryResult) ? S_FALSE : recoveryResult;
        }
    }
    if (_raisedOverlayActive && _raisedOverlayIndex < widgetCount && _raisedViewport.Width >= 1.0f &&
        _raisedViewport.Height >= 1.0f)
    {
        const HRESULT raisedResult = drawWidget(_raisedOverlayIndex, _raisedViewport, 1);
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
    // Visibility deactivation drains data and network callbacks before any plugin device state is released.
    if (_dashboardHost)
    {
        (void)_dashboardHost->SetWidgetsVisible(false);
    }
    if (_transitionDashboardHost)
    {
        (void)_transitionDashboardHost->SetWidgetsVisible(false);
    }
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
    _frameLatencyWaitable.reset();
    _swapChain.reset();
    _factory.reset();
    _context1.reset();
    _context.reset();
    _device.reset();
    _deviceInfo = {};
    _deviceMonitor = nullptr;
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
