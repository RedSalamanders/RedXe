#include "DashboardHost.h"
#include "PlugInterfaces/FactoryImpl.h"
#include "PluginManager.h"
#include "Renderer.h"
#include "Settings.h"

#include <atomic>
#include <iostream>
#include <new>

struct RendererResourceTestAccess final
{
    static ID3D11Device* Device(const Renderer& renderer) noexcept
    {
        return renderer._device.get();
    }
    static ID3D11DeviceContext* Context(const Renderer& renderer) noexcept
    {
        return renderer._context.get();
    }
    static ID3D11RenderTargetView* Target(const Renderer& renderer) noexcept
    {
        return renderer._renderTarget.get();
    }
    static bool HasTransition(const Renderer& renderer) noexcept
    {
        return renderer._transitionDashboardHost != nullptr;
    }
    static HRESULT Recover(Renderer& renderer) noexcept
    {
        return renderer.RecoverDevice();
    }
};

HRESULT RecoverPageProbeTestDevice(Renderer& renderer) noexcept
{
    return RendererResourceTestAccess::Recover(renderer);
}

struct PluginManagerGpuResourceTestAccess final
{
    static void Replace(PluginManager& plugins, IRedXeGpuWidget* replacement) noexcept
    {
        plugins._widgets[0].gpuWidget = replacement;
    }
    static void ReplaceVisibility(PluginManager& plugins, IRedXeWidget* replacement) noexcept
    {
        plugins._widgets[0].widget = replacement;
    }
};

namespace
{
class VisibilityFaultWidget final : public RedXeComObject<VisibilityFaultWidget, IRedXeWidget>
{
  public:
    explicit VisibilityFaultWidget(IRedXeWidget* inner) noexcept : _inner(inner) {}
    HRESULT STDMETHODCALLTYPE SetVisible(BOOL visible) noexcept override
    {
        const HRESULT failure = visible ? showFailure : hideFailure;
        return FAILED(failure) ? failure : _inner->SetVisible(visible);
    }
    HRESULT STDMETHODCALLTYPE CollectPersistentSettings(char* json, uint32_t capacity,
                                                        uint32_t* bytes) noexcept override
    {
        return _inner->CollectPersistentSettings(json, capacity, bytes);
    }
    HRESULT showFailure = S_OK;
    HRESULT hideFailure = S_OK;

  private:
    wil::com_ptr_nothrow<IRedXeWidget> _inner;
};
// Both wrappers use one staging texture and copy the completed frame before Present, while a flip-discard back
// buffer's contents are still defined. The copy changes no pipeline state.
class CaptureGpuWidget final : public RedXeComObject<CaptureGpuWidget, IRedXeGpuWidget>
{
  public:
    CaptureGpuWidget(IRedXeGpuWidget* inner, ID3D11Texture2D* staging) noexcept : _inner(inner), _staging(staging) {}
    HRESULT STDMETHODCALLTYPE OnDeviceCreated(const RedXeGpuDeviceContext* context) noexcept override
    {
        return _inner->OnDeviceCreated(context);
    }
    void STDMETHODCALLTYPE OnDeviceLost() noexcept override
    {
        _inner->OnDeviceLost();
    }
    HRESULT STDMETHODCALLTYPE OnTargetSizeChanged(const RedXeGpuTargetSizeContext* context) noexcept override
    {
        return _inner->OnTargetSizeChanged(context);
    }
    HRESULT STDMETHODCALLTYPE Render(const RedXeGpuFrameContext* context) noexcept override
    {
        RETURN_IF_FAILED(_inner->Render(context));
        wil::com_ptr_nothrow<ID3D11RenderTargetView> target;
        context->deviceContext->OMGetRenderTargets(1, target.put(), nullptr);
        if (!target)
            return E_UNEXPECTED;
        wil::com_ptr_nothrow<ID3D11Resource> resource;
        target->GetResource(resource.put());
        context->deviceContext->CopyResource(_staging.get(), resource.get());
        return S_OK;
    }

  private:
    wil::com_ptr_nothrow<IRedXeGpuWidget> _inner;
    wil::com_ptr_nothrow<ID3D11Texture2D> _staging;
};

constexpr UINT kWidth = 640;
constexpr UINT kHeight = 360;
constexpr GUID kResourceLifetime = {0x3b778e03, 0xe7e6, 0x4315, {0xa9, 0xe4, 0x35, 0xeb, 0x83, 0x47, 0x92, 0xa1}};

class ResourceLifetime final : public RedXeComObject<ResourceLifetime, IUnknown>
{
  public:
    explicit ResourceLifetime(std::atomic<UINT>& destructions) noexcept : _destructions(destructions) {}
    ~ResourceLifetime()
    {
        ++_destructions;
    }

  private:
    std::atomic<UINT>& _destructions;
};

// Bind resources in slots the shipped triangles never use. Once the local owners go away, only the production
// immediate context retains them. A single private-data identity dies only after every resource is released.
HRESULT BindRetiringResources(Renderer& renderer, std::atomic<UINT>& destructions) noexcept
{
    ID3D11Device* const device = RendererResourceTestAccess::Device(renderer);
    ID3D11DeviceContext* const context = RendererResourceTestAccess::Context(renderer);
    wil::com_ptr_nothrow<ResourceLifetime> lifetime;
    lifetime.attach(new (std::nothrow) ResourceLifetime(destructions));
    if (!lifetime)
        return E_OUTOFMEMORY;
    const D3D11_TEXTURE2D_DESC textureDescription{
        32, 32, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    RETURN_IF_FAILED(device->CreateTexture2D(&textureDescription, nullptr, texture.put()));
    RETURN_IF_FAILED(texture->SetPrivateDataInterface(kResourceLifetime, lifetime.get()));
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> view;
    RETURN_IF_FAILED(device->CreateShaderResourceView(texture.get(), nullptr, view.put()));
    ID3D11ShaderResourceView* views[] = {view.get()};
    context->VSSetShaderResources(7, 1, views);
    context->GSSetShaderResources(4, 1, views);
    context->PSSetShaderResources(127, 1, views);
    context->CSSetShaderResources(127, 1, views);
    D3D11_BUFFER_DESC bufferDescription{64, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
    wil::com_ptr_nothrow<ID3D11Buffer> vertex;
    RETURN_IF_FAILED(device->CreateBuffer(&bufferDescription, nullptr, vertex.put()));
    RETURN_IF_FAILED(vertex->SetPrivateDataInterface(kResourceLifetime, lifetime.get()));
    ID3D11Buffer* buffers[] = {vertex.get()};
    const UINT stride = 16;
    const UINT offset = 0;
    context->IASetVertexBuffers(31, 1, buffers, &stride, &offset);
    bufferDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
    wil::com_ptr_nothrow<ID3D11Buffer> index;
    RETURN_IF_FAILED(device->CreateBuffer(&bufferDescription, nullptr, index.put()));
    RETURN_IF_FAILED(index->SetPrivateDataInterface(kResourceLifetime, lifetime.get()));
    context->IASetIndexBuffer(index.get(), DXGI_FORMAT_R16_UINT, 0);
    bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    wil::com_ptr_nothrow<ID3D11Buffer> constants;
    RETURN_IF_FAILED(device->CreateBuffer(&bufferDescription, nullptr, constants.put()));
    RETURN_IF_FAILED(constants->SetPrivateDataInterface(kResourceLifetime, lifetime.get()));
    buffers[0] = constants.get();
    context->VSSetConstantBuffers(13, 1, buffers);
    context->PSSetConstantBuffers(13, 1, buffers);
    context->CSSetConstantBuffers(13, 1, buffers);
    return S_OK;
}

bool RetiringBindingsAreClear(const Renderer& renderer) noexcept
{
    ID3D11DeviceContext* const context = RendererResourceTestAccess::Context(renderer);
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> vs, gs, ps, cs;
    wil::com_ptr_nothrow<ID3D11Buffer> vertex, index, vsConstants, psConstants, csConstants;
    context->VSGetShaderResources(7, 1, vs.put());
    context->GSGetShaderResources(4, 1, gs.put());
    context->PSGetShaderResources(127, 1, ps.put());
    context->CSGetShaderResources(127, 1, cs.put());
    UINT stride = 0;
    UINT offset = 0;
    DXGI_FORMAT format{};
    context->IAGetVertexBuffers(31, 1, vertex.put(), &stride, &offset);
    context->IAGetIndexBuffer(index.put(), &format, &offset);
    context->VSGetConstantBuffers(13, 1, vsConstants.put());
    context->PSGetConstantBuffers(13, 1, psConstants.put());
    context->CSGetConstantBuffers(13, 1, csConstants.put());
    return !vs && !gs && !ps && !cs && !vertex && !index && !vsConstants && !psConstants && !csConstants;
}

HRESULT CreateFrameCapture(const Renderer& renderer, wil::com_ptr_nothrow<ID3D11Texture2D>& staging) noexcept
{
    wil::com_ptr_nothrow<ID3D11Resource> resource;
    RendererResourceTestAccess::Target(renderer)->GetResource(resource.put());
    wil::com_ptr_nothrow<ID3D11Texture2D> target;
    RETURN_IF_FAILED(resource.query_to(target.put()));
    D3D11_TEXTURE2D_DESC description{};
    target->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    return RendererResourceTestAccess::Device(renderer)->CreateTexture2D(&description, nullptr, staging.put());
}

HRESULT FrameFingerprint(const Renderer& renderer, ID3D11Texture2D* staging, uint64_t& fingerprint,
                         UINT& changedPixels) noexcept
{
    D3D11_TEXTURE2D_DESC description{};
    staging->GetDesc(&description);
    ID3D11DeviceContext* const context = RendererResourceTestAccess::Context(renderer);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    RETURN_IF_FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped));
    const auto unmap = wil::scope_exit([&] { context->Unmap(staging, 0); });
    fingerprint = 14695981039346656037ULL;
    changedPixels = 0;
    const auto* const background = static_cast<const uint8_t*>(mapped.pData);
    for (UINT y = 0; y < description.Height; ++y)
    {
        const auto* const row = static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch;
        for (UINT x = 0; x < description.Width; ++x)
        {
            const uint8_t* const pixel = row + x * 4;
            changedPixels += pixel[0] != background[0] || pixel[1] != background[1] || pixel[2] != background[2];
            for (UINT channel = 0; channel < 4; ++channel)
                fingerprint = (fingerprint ^ pixel[channel]) * 1099511628211ULL;
        }
    }
    return S_OK;
}
} // namespace

void TestGpuResourceRetirement(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] production context releases retired page bindings without a device rebuild\n";
    const auto check = [&](bool condition, const wchar_t* name)
    {
        if (!condition)
        {
            success = false;
            std::wcerr << L"FAIL GPU resource retirement: " << name << L'\n';
        }
    };
    const HRESULT com = OleInitialize(nullptr);
    check(SUCCEEDED(com), L"COM initialization");
    if (FAILED(com))
        return;
    const auto uninitialize = wil::scope_exit([] { OleUninitialize(); });
    std::atomic<UINT> destructions = 0;
    wil::unique_hwnd window(CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"STATIC", L"RedXe GPU retirement test",
                                            WS_POPUP, -32000, -32000, kWidth, kHeight, nullptr, nullptr,
                                            GetModuleHandleW(nullptr), nullptr));
    check(!!window, L"hidden owned window");
    if (!window)
        return;
    AppSettings settings{};
    HRESULT result = ParseAppSettingsJson(
        R"({"version":{"major":5},"pages":[{"columns":[{"widget":{"plugin":"builtin.rotating-triangle"}}]},{"columns":[{"widget":{"plugin":"builtin.rotating-triangle"}}]}]})",
        settings);
    PluginManager primaryPlugins;
    PluginManager stagedPlugins;
    DashboardHost primary;
    DashboardHost staged;
    Renderer renderer;
    const auto cleanup = wil::scope_exit(
        [&]
        {
            renderer.Shutdown();
            staged.Shutdown();
            primary.Shutdown();
        });
    if (SUCCEEDED(result))
        result = primaryPlugins.Initialize(settings);
    if (SUCCEEDED(result))
        result = primary.Initialize(primaryPlugins, window.get(), kWidth, kHeight, USER_DEFAULT_SCREEN_DPI, false);
    if (SUCCEEDED(result))
        result = renderer.Initialize(window.get(), true, primary);
    if (SUCCEEDED(result))
        result = MoveDashboardPage(settings, 1);
    if (SUCCEEDED(result))
        result = stagedPlugins.Initialize(settings);
    if (SUCCEEDED(result))
        result = staged.Initialize(stagedPlugins, window.get(), kWidth, kHeight, USER_DEFAULT_SCREEN_DPI, false);
    if (SUCCEEDED(result))
        result = staged.SetHorizontalOffset(kWidth);
    wil::com_ptr_nothrow<ID3D11Texture2D> capture;
    if (SUCCEEDED(result))
        result = CreateFrameCapture(renderer, capture);
    wil::com_ptr_nothrow<CaptureGpuWidget> primaryCapture;
    wil::com_ptr_nothrow<CaptureGpuWidget> stagedCapture;
    if (SUCCEEDED(result))
    {
        primaryCapture.attach(new (std::nothrow) CaptureGpuWidget(primaryPlugins.GpuWidgetAt(0), capture.get()));
        stagedCapture.attach(new (std::nothrow) CaptureGpuWidget(stagedPlugins.GpuWidgetAt(0), capture.get()));
        if (!primaryCapture || !stagedCapture)
            result = E_OUTOFMEMORY;
        else
        {
            PluginManagerGpuResourceTestAccess::Replace(primaryPlugins, primaryCapture.get());
            PluginManagerGpuResourceTestAccess::Replace(stagedPlugins, stagedCapture.get());
        }
    }
    if (SUCCEEDED(result))
        result = renderer.SetTransitionDashboard(&staged);
    check(SUCCEEDED(result), L"production primary and staged triangles initialize");
    if (FAILED(result))
        return;
    ID3D11DeviceContext* const originalContext = RendererResourceTestAccess::Context(renderer);
    const uint64_t originalGeneration = renderer.QueryProbeGraphics().generation;
    uint64_t originalPixels = 0;
    uint64_t incomingPixels = 0;
    uint64_t currentPixels = 0;
    UINT changedPixels = 0;
    result = renderer.Render(0.1f, 0.0f);
    if (SUCCEEDED(result))
        result = FrameFingerprint(renderer, capture.get(), originalPixels, changedPixels);
    check(SUCCEEDED(result) && changedPixels > 1000, L"control frame contains actual triangle pixels");
    if (FAILED(result))
        return;

    result = BindRetiringResources(renderer, destructions);
    check(SUCCEEDED(result) && destructions == 0 && !RetiringBindingsAreClear(renderer),
          L"unused bindings retain resources after their plugin owners release");
    result = renderer.SetTransitionDashboard(nullptr);
    // The driver's command buffer may defer final destruction. Drain it only in the test; production uses the next
    // ordinary page frame rather than adding a retirement flush.
    RendererResourceTestAccess::Context(renderer)->Flush();
    check(SUCCEEDED(result) && RetiringBindingsAreClear(renderer) && destructions == 1,
          L"canceling a staged page clears every bound resource and releases its last owner");
    result = renderer.Render(0.1f, 0.0f);
    if (SUCCEEDED(result))
        result = FrameFingerprint(renderer, capture.get(), currentPixels, changedPixels);
    check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 1 && currentPixels == originalPixels,
          L"primary triangle pixels are identical after cancellation without setup");

    result = renderer.SetTransitionDashboard(&staged);
    if (SUCCEEDED(result))
        result = BindRetiringResources(renderer, destructions);
    if (SUCCEEDED(result))
        result = renderer.SetTransitionDashboard(&staged);
    RendererResourceTestAccess::Context(renderer)->Flush();
    check(SUCCEEDED(result) && RetiringBindingsAreClear(renderer) && destructions == 2,
          L"replacing a staged page releases the previous staged GPU bindings");
    result = primary.SetHorizontalOffset(-static_cast<LONG>(kWidth));
    if (SUCCEEDED(result))
        result = staged.SetHorizontalOffset(0);
    if (SUCCEEDED(result))
        result = renderer.RefreshLayout();
    if (SUCCEEDED(result))
        result = renderer.Render(0.1f, 0.0f);
    if (SUCCEEDED(result))
        result = FrameFingerprint(renderer, capture.get(), incomingPixels, changedPixels);
    check(SUCCEEDED(result) && changedPixels > 1000, L"incoming control frame contains actual triangle pixels");
    if (SUCCEEDED(result))
        result = BindRetiringResources(renderer, destructions);
    if (SUCCEEDED(result))
        result = renderer.AdoptPrimaryDashboard(staged);
    RendererResourceTestAccess::Context(renderer)->Flush();
    check(SUCCEEDED(result) && RetiringBindingsAreClear(renderer) && destructions == 3 &&
              RendererResourceTestAccess::Context(renderer) == originalContext &&
              renderer.QueryProbeGraphics().generation == originalGeneration,
          L"promotion clears bindings and retains the device, swap chain and incoming resources");
    result = renderer.Render(0.1f, 0.0f);
    if (SUCCEEDED(result))
        result = FrameFingerprint(renderer, capture.get(), currentPixels, changedPixels);
    check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 1 && currentPixels == incomingPixels,
          L"incoming triangle pixels are identical after promotion without device setup");

    result = renderer.SetTransitionDashboard(&primary);
    if (SUCCEEDED(result))
        result = renderer.Resize(0, 0);
    if (SUCCEEDED(result))
        result = BindRetiringResources(renderer, destructions);
    check(SUCCEEDED(result), L"suspended transactional failure fixture initializes");
    if (FAILED(result))
        return;
    result = renderer.AdoptPrimaryDashboard(primary);
    check(FAILED(result) && destructions == 3 && !RetiringBindingsAreClear(renderer),
          L"failed promotion retains the existing pages and does not retire their bindings");
    result = renderer.SetTransitionDashboard(nullptr);
    if (SUCCEEDED(result))
        result = renderer.Resize(kWidth, kHeight);
    if (SUCCEEDED(result))
        result = renderer.Render(0.1f, 0.0f);
    if (SUCCEEDED(result))
        result = FrameFingerprint(renderer, capture.get(), currentPixels, changedPixels);
    check(SUCCEEDED(result) && destructions == 4 && renderer.LastFrameSuccessfulWidgetCount() == 1 &&
              currentPixels == incomingPixels,
          L"failed-promotion recovery cancels the neighbor and preserves primary pixels");

    wil::com_ptr_nothrow<VisibilityFaultWidget> visibility;
    visibility.attach(new (std::nothrow) VisibilityFaultWidget(primaryPlugins.WidgetAt(0)));
    check(bool(visibility), L"the visibility fault wrapper is available");
    if (!visibility)
        return;
    PluginManagerGpuResourceTestAccess::ReplaceVisibility(primaryPlugins, visibility.get());
    result = primary.SetWidgetsVisible(true);
    visibility->showFailure = E_ACCESSDENIED;
    if (SUCCEEDED(result))
        result = renderer.SetTransitionDashboard(&primary);
    check(result == E_ACCESSDENIED && !RendererResourceTestAccess::HasTransition(renderer),
          L"failed staged visibility drops the renderer's borrowed page before its owner can destroy it");
    visibility->showFailure = S_OK;
    (void)renderer.SetTransitionDashboard(nullptr);
    result = primary.SetWidgetsVisible(true);
    if (SUCCEEDED(result))
        result = renderer.SetTransitionDashboard(&primary);
    if (SUCCEEDED(result))
        result = BindRetiringResources(renderer, destructions);
    visibility->hideFailure = E_ABORT;
    if (SUCCEEDED(result))
        result = renderer.SetTransitionDashboard(nullptr);
    RendererResourceTestAccess::Context(renderer)->Flush();
    check(result == E_ABORT && !RendererResourceTestAccess::HasTransition(renderer) &&
              RetiringBindingsAreClear(renderer) && destructions == 5,
          L"failed staged hide reports its error but still detaches and releases retiring bindings");
    visibility->hideFailure = S_OK;
    (void)renderer.SetTransitionDashboard(nullptr);
    result = renderer.Render(0.1f, 0.0f);
    if (SUCCEEDED(result))
        result = FrameFingerprint(renderer, capture.get(), currentPixels, changedPixels);
    check(SUCCEEDED(result) && renderer.LastFrameSuccessfulWidgetCount() == 1 && currentPixels == incomingPixels,
          L"staged visibility failures preserve primary pixels and presentation resources");
}
