#include "LogiconMonitor.h"

#include "../ProcessViewer/ViewerGpu.h"
#include "LogiconMonitorPixelShader.h"
#include "LogiconMonitorVertexShader.h"
#include "LogiconService.h"
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Widget.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace Logicon
{
namespace
{
constexpr float kPadding = 12.0f;
constexpr float kPageButtonRowHeight = 38.0f;
constexpr uint32_t kTraceLines = 8;
constexpr uint32_t kMaximumTextCharacters = 96;
// The tile's type descriptor asks for 720×420; text scales up with taller tiles and comes down only a little when
// the status column is narrower than its reference width, so every line still fits.
constexpr float kReferenceHeight = 420.0f;
constexpr float kReferenceColumnWidth = 400.0f;
constexpr float kBaseTextHeight = 18.0f;
constexpr float kBaseLineHeight = 22.0f;
constexpr float kBaseChipHeight = 30.0f;
constexpr float kBaseKeyLabelHeight = 14.0f;
constexpr float kMinimumTextScale = 0.9f;
constexpr float kMaximumTextScale = 1.6f;

// Text and control sizes for one tile.
struct Metrics final
{
    float text = kBaseTextHeight;
    float line = kBaseLineHeight;
    float chip = kBaseChipHeight;
    float keyLabel = kBaseKeyLabelHeight;
};

[[nodiscard]] Metrics ScaledMetrics(float scale) noexcept
{
    return Metrics{kBaseTextHeight * scale, kBaseLineHeight * scale, kBaseChipHeight * scale,
                   kBaseKeyLabelHeight * scale};
}

[[nodiscard]] float HeightScale(float height) noexcept
{
    return std::clamp(height / kReferenceHeight, 1.0f, kMaximumTextScale);
}

[[nodiscard]] Metrics ComputeMetrics(float height, float columnWidth) noexcept
{
    const float widthScale = std::clamp(columnWidth / kReferenceColumnWidth, kMinimumTextScale, kMaximumTextScale);
    return ScaledMetrics(std::min(HeightScale(height), widthScale));
}

constexpr std::array kWidgetTypes{
    RedXeWidgetTypeDescriptor{
        sizeof(RedXeWidgetTypeDescriptor),
        kMonitorWidgetTypeId,
        L"Logicon Monitor",
        L"Live view of the MX Creative Console keypad and dialpad driven by the Logicon service.",
        720.0f,
        420.0f,
        360.0f,
        240.0f,
        RedXeWidgetFlagNone,
    },
};

// Colors cycled by the Color tap mode.
constexpr std::array<uint32_t, 8> kPalette{0xE53935, 0xFB8C00, 0xFDD835, 0x43A047,
                                           0x1E88E5, 0x8E24AA, 0x00ACC1, 0xF5F5F5};

enum class TapMode : uint8_t
{
    Press = 0,
    Color,
    Picture,
    Clear,
};

struct Rect final
{
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;

    [[nodiscard]] bool Contains(float px, float py) const noexcept
    {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};

struct Layout final
{
    Metrics metrics{};
    Rect panel{};
    Rect grid{};
    std::array<Rect, kKeyCount> keys{};
    std::array<Rect, 2> pageButtons{};
    std::array<Rect, 4> modeChips{};
    Rect syntheticChip{};
    Rect brightnessDown{};
    Rect brightnessUp{};
    std::array<Rect, kDialpadButtonCount> dialButtons{};
    float textX = 0;
    float textY = 0;
    float textWidth = 0;
    float dialpadY = 0;
    float traceY = 0;
    float bottom = 0;
};

// Lays out `count` equal chips across the text column at y.
void PlaceChipRow(const Layout& layout, float y, Rect* chips, uint32_t count) noexcept
{
    constexpr float gap = 6.0f;
    const float chipWidth = std::max((layout.textWidth - (count - 1) * gap) / count, 40.0f);
    for (uint32_t index = 0; index < count; ++index)
    {
        chips[index] = Rect{layout.textX + index * (chipWidth + gap), y, chipWidth, layout.metrics.chip};
    }
}

[[nodiscard]] Layout ComputeLayout(float width, float height) noexcept
{
    Layout layout{};
    // The panel side follows the height-only scale so the page-button row below it always fits; the final metrics
    // may only be smaller (a narrow column), never larger.
    const float pageRow = std::max(kPageButtonRowHeight, ScaledMetrics(HeightScale(height)).chip + 8.0f);
    const float available = std::max(height - 2.0f * kPadding - pageRow, 60.0f);
    const float side = std::max(std::min(available, width * 0.5f), 60.0f);
    layout.metrics = ComputeMetrics(height, std::max(width - side - 3.0f * kPadding, 80.0f));
    const Metrics& metrics = layout.metrics;
    layout.panel = Rect{kPadding, kPadding, side, side};
    const float scale = side / static_cast<float>(kPanelSize);
    layout.grid = Rect{layout.panel.x + kGridOriginX * scale, layout.panel.y + kGridOriginY * scale, kGridSize * scale,
                       kGridSize * scale};
    for (uint32_t slot = 0; slot < kKeyCount; ++slot)
    {
        const ImageRegion region = KeyRegion(slot);
        layout.keys[slot] = Rect{layout.panel.x + region.x * scale, layout.panel.y + region.y * scale,
                                 region.width * scale, region.height * scale};
    }
    const float buttonY = layout.panel.y + side + 6.0f;
    const float buttonWidth = (side - 8.0f) * 0.5f;
    layout.pageButtons[0] = Rect{layout.panel.x, buttonY, buttonWidth, metrics.chip};
    layout.pageButtons[1] = Rect{layout.panel.x + buttonWidth + 8.0f, buttonY, buttonWidth, metrics.chip};

    layout.textX = layout.panel.x + side + kPadding;
    layout.textY = kPadding;
    layout.textWidth = std::max(width - layout.textX - kPadding, 80.0f);
    // Six status lines, then the tap-mode chips, then the toggles.
    const float chipY = layout.textY + 6.0f * metrics.line + 4.0f;
    PlaceChipRow(layout, chipY, layout.modeChips.data(), static_cast<uint32_t>(layout.modeChips.size()));
    const float toggleY = chipY + metrics.chip + 6.0f;
    std::array<Rect, 3> toggles{};
    PlaceChipRow(layout, toggleY, toggles.data(), static_cast<uint32_t>(toggles.size()));
    layout.syntheticChip = toggles[0];
    layout.brightnessDown = toggles[1];
    layout.brightnessUp = toggles[2];
    // Dialpad: a status line, one line per wheel, then its four buttons as chips.
    layout.dialpadY = toggleY + metrics.chip + 10.0f;
    PlaceChipRow(layout, layout.dialpadY + 3.0f * metrics.line + 2.0f, layout.dialButtons.data(),
                 static_cast<uint32_t>(layout.dialButtons.size()));
    layout.traceY = layout.dialButtons[0].y + metrics.chip + 8.0f;
    layout.bottom = height - kPadding;
    return layout;
}

class MonitorGpu final
{
  public:
    ~MonitorGpu()
    {
        Reset();
    }

    [[nodiscard]] HRESULT Initialize(ID3D11Device* device) noexcept
    {
        if (!device)
        {
            return E_POINTER;
        }
        if (_deviceIdentity == device && _texture)
        {
            return S_OK;
        }
        Reset();
        HRESULT result = ViewerGpuAcquire(device);
        if (FAILED(result))
        {
            return result;
        }
        _viewerAcquired = true;
        result = device->CreateVertexShader(g_LogiconMonitorVertexShader, sizeof(g_LogiconMonitorVertexShader), nullptr,
                                            _vertexShader.put());
        if (SUCCEEDED(result))
        {
            result = device->CreatePixelShader(g_LogiconMonitorPixelShader, sizeof(g_LogiconMonitorPixelShader),
                                               nullptr, _pixelShader.put());
        }
        if (SUCCEEDED(result))
        {
            D3D11_BUFFER_DESC constants{};
            constants.ByteWidth = sizeof(Constants);
            constants.Usage = D3D11_USAGE_DYNAMIC;
            constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            result = device->CreateBuffer(&constants, nullptr, _constants.put());
        }
        if (SUCCEEDED(result))
        {
            D3D11_TEXTURE2D_DESC texture{};
            texture.Width = kGridSize;
            texture.Height = kGridSize;
            texture.MipLevels = 1;
            texture.ArraySize = 1;
            texture.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            texture.SampleDesc.Count = 1;
            texture.Usage = D3D11_USAGE_DEFAULT;
            texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            result = device->CreateTexture2D(&texture, nullptr, _texture.put());
        }
        if (SUCCEEDED(result))
        {
            result = device->CreateShaderResourceView(_texture.get(), nullptr, _textureView.put());
        }
        if (SUCCEEDED(result))
        {
            D3D11_SAMPLER_DESC sampler{};
            sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.MaxLOD = D3D11_FLOAT32_MAX;
            result = device->CreateSamplerState(&sampler, _sampler.put());
        }
        if (SUCCEEDED(result))
        {
            D3D11_RASTERIZER_DESC rasterizer{};
            rasterizer.FillMode = D3D11_FILL_SOLID;
            rasterizer.CullMode = D3D11_CULL_NONE;
            rasterizer.DepthClipEnable = TRUE;
            result = device->CreateRasterizerState(&rasterizer, _rasterizer.put());
        }
        if (SUCCEEDED(result))
        {
            D3D11_BLEND_DESC blend{};
            blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            result = device->CreateBlendState(&blend, _blend.put());
        }
        if (SUCCEEDED(result))
        {
            D3D11_DEPTH_STENCIL_DESC depth{};
            depth.DepthEnable = FALSE;
            depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
            result = device->CreateDepthStencilState(&depth, _depth.put());
        }
        if (FAILED(result))
        {
            Reset();
            return result;
        }
        _deviceIdentity = device;
        return S_OK;
    }

    void Reset() noexcept
    {
        _depth.reset();
        _blend.reset();
        _rasterizer.reset();
        _sampler.reset();
        _textureView.reset();
        _texture.reset();
        _constants.reset();
        _pixelShader.reset();
        _vertexShader.reset();
        if (_viewerAcquired)
        {
            ViewerGpuRelease();
            _viewerAcquired = false;
        }
        _deviceIdentity = nullptr;
    }

    [[nodiscard]] bool Ready() const noexcept
    {
        return _texture != nullptr;
    }

    void UploadFaces(ID3D11DeviceContext* context, const uint32_t* bgra) noexcept
    {
        if (context && _texture && bgra)
        {
            context->UpdateSubresource(_texture.get(), 0, nullptr, bgra, kGridSize * sizeof(uint32_t), 0);
        }
    }

    [[nodiscard]] HRESULT DrawFaces(ID3D11DeviceContext* context, float width, float height, const Rect& rect) noexcept
    {
        if (!context || !_texture)
        {
            return E_UNEXPECTED;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT result = context->Map(_constants.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(result))
        {
            return result;
        }
        auto* constants = static_cast<Constants*>(mapped.pData);
        constants->rect[0] = rect.x;
        constants->rect[1] = rect.y;
        constants->rect[2] = rect.width;
        constants->rect[3] = rect.height;
        constants->targetSize[0] = width;
        constants->targetSize[1] = height;
        constants->targetSize[2] = 0.0f;
        constants->targetSize[3] = 0.0f;
        context->Unmap(_constants.get(), 0);
        ID3D11Buffer* buffer = _constants.get();
        ID3D11ShaderResourceView* view = _textureView.get();
        ID3D11SamplerState* sampler = _sampler.get();
        const float blendFactor[4] = {};
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(_vertexShader.get(), nullptr, 0);
        context->PSSetShader(_pixelShader.get(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &buffer);
        context->PSSetShaderResources(0, 1, &view);
        context->PSSetSamplers(0, 1, &sampler);
        context->RSSetState(_rasterizer.get());
        context->OMSetDepthStencilState(_depth.get(), 0);
        context->OMSetBlendState(_blend.get(), blendFactor, 0xffffffff);
        context->Draw(6, 0);
        ID3D11ShaderResourceView* nullView = nullptr;
        context->PSSetShaderResources(0, 1, &nullView);
        return S_OK;
    }

  private:
    struct Constants final
    {
        float rect[4];
        float targetSize[4];
    };

    ID3D11Device* _deviceIdentity = nullptr;
    bool _viewerAcquired = false;
    wil::com_ptr_nothrow<ID3D11VertexShader> _vertexShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _pixelShader;
    wil::com_ptr_nothrow<ID3D11Buffer> _constants;
    wil::com_ptr_nothrow<ID3D11Texture2D> _texture;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> _textureView;
    wil::com_ptr_nothrow<ID3D11SamplerState> _sampler;
    wil::com_ptr_nothrow<ID3D11RasterizerState> _rasterizer;
    wil::com_ptr_nothrow<ID3D11BlendState> _blend;
    wil::com_ptr_nothrow<ID3D11DepthStencilState> _depth;
};

// Near-white body text, a dimmer grey for secondary lines: the column sits on the dashboard background.
constexpr float kTextPrimary = 0.96f;
constexpr float kTextSecondary = 0.82f;

void AppendLine(ViewerGpuResources& gpu, ViewerDrawList& list, const Metrics& metrics, float x, float& y,
                float maxWidth, const wchar_t* text, float red, float green, float blue) noexcept
{
    uint32_t characters = static_cast<uint32_t>(wcsnlen_s(text, kMaximumTextCharacters));
    while (characters > 0 && gpu.MeasureText(text, characters, metrics.text) > maxWidth)
    {
        --characters;
    }
    if (characters != 0)
    {
        (void)gpu.EnsureGlyphs(text, characters);
        (void)gpu.AppendText(list, x, y, metrics.text, red, green, blue, 1.0f, text, characters);
    }
    y += metrics.line;
}

void AppendChip(ViewerGpuResources& gpu, ViewerDrawList& list, const Metrics& metrics, const Rect& rect,
                const wchar_t* text, bool active) noexcept
{
    (void)list.AddFill(rect.x, rect.y, rect.width, rect.height, active ? 0.20f : 0.16f, active ? 0.50f : 0.18f,
                       active ? 0.90f : 0.22f, 1.0f, 6.0f);
    (void)list.AddStroke(rect.x, rect.y, rect.width, rect.height, 0.70f, 0.75f, 0.85f, active ? 1.0f : 0.7f, 6.0f,
                         1.5f);
    uint32_t characters = static_cast<uint32_t>(wcsnlen_s(text, 32));
    (void)gpu.EnsureGlyphs(text, characters);
    float textWidth = gpu.MeasureText(text, characters, metrics.text);
    while (characters > 1 && textWidth > rect.width - 6.0f)
    {
        --characters;
        textWidth = gpu.MeasureText(text, characters, metrics.text);
    }
    (void)gpu.AppendText(list, rect.x + std::max((rect.width - textWidth) * 0.5f, 3.0f),
                         rect.y + (rect.height - metrics.text) * 0.5f, metrics.text, 1.0f, 1.0f, 1.0f, 1.0f, text,
                         characters);
}

// The verb part of an action name (after the namespace), or an empty label for an unbound slot. Names are ASCII.
[[nodiscard]] const char* ActionLabel(const ActionName& action) noexcept
{
    const char* name = action.data();
    const char* dot = std::strchr(name, '.');
    return dot ? dot + 1 : name;
}

class MonitorWidget final : public RedXeComObject<MonitorWidget, IRedXeWidget, IRedXeGpuWidget, IRedXePreparedGpuWidget,
                                                  IRedXeInteractiveWidget>
{
  public:
    MonitorWidget(wil::com_ptr_nothrow<IRedXeWidgetProvider>&& providerOwner, IRedXeHost* host,
                  const char* instanceId) noexcept
        : _providerOwner(std::move(providerOwner)), _host(host)
    {
        strncpy_s(_instanceId.data(), _instanceId.size(), instanceId, _TRUNCATE);
        if (LogiconService* service = LogiconService::Current())
        {
            service->AttachMonitor(true);
            _attached = true;
        }
    }

    ~MonitorWidget()
    {
        if (_attached)
        {
            if (LogiconService* service = LogiconService::Current())
            {
                service->AttachMonitor(false);
            }
        }
    }

    HRESULT STDMETHODCALLTYPE SetVisible(BOOL visible) noexcept override
    {
        _visible = visible != FALSE;
        if (_visible && _host)
        {
            (void)_host->RequestFrame();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CollectPersistentSettings(char* jsonUtf8, uint32_t capacityBytes,
                                                        uint32_t* writtenBytes) noexcept override
    {
        return RedXeCollectNoPersistentSettings(jsonUtf8, capacityBytes, writtenBytes);
    }

    HRESULT STDMETHODCALLTYPE OnDeviceCreated(const RedXeGpuDeviceContext* context) noexcept override
    {
        if (!context)
        {
            return E_POINTER;
        }
        if (context->sizeBytes != sizeof(RedXeGpuDeviceContext) || !context->device)
        {
            return E_INVALIDARG;
        }
        _faceGeneration = 0;
        return _gpu.Initialize(context->device);
    }

    void STDMETHODCALLTYPE OnDeviceLost() noexcept override
    {
        _gpu.Reset();
        _faceGeneration = 0;
    }

    HRESULT STDMETHODCALLTYPE OnTargetSizeChanged(const RedXeGpuTargetSizeContext* context) noexcept override
    {
        if (!context || context->sizeBytes != sizeof(RedXeGpuTargetSizeContext))
        {
            return E_INVALIDARG;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Prepare(const RedXeGpuPreparationContext* context) noexcept override
    {
        if (!context || context->sizeBytes != sizeof(RedXeGpuPreparationContext))
        {
            return E_INVALIDARG;
        }
        LogiconService* service = LogiconService::Current();
        if (!service)
        {
            _snapshotValid = false;
            return S_FALSE;
        }
        if (!_faces)
        {
            _faces.reset(new (std::nothrow) uint32_t[kGridPixels]);
            if (!_faces)
            {
                return E_OUTOFMEMORY;
            }
            std::fill_n(_faces.get(), kGridPixels, 0xFF000000U);
        }
        service->CopyMonitorSnapshot(_snapshot);
        _snapshotValid = true;
        if (service->CopyFaceSurface(_faces.get(), kGridPixels, _faceGeneration))
        {
            _facesDirty = true;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Render(const RedXeGpuFrameContext* context) noexcept override
    {
        if (!context || !context->widget)
        {
            return E_POINTER;
        }
        const RedXeWidgetFrameContext& widget = *context->widget;
        if (context->sizeBytes != sizeof(RedXeGpuFrameContext) || widget.sizeBytes != sizeof(RedXeWidgetFrameContext) ||
            !context->deviceContext || widget.widthPixels == 0 || widget.heightPixels == 0)
        {
            return E_INVALIDARG;
        }
        if (!_gpu.Ready())
        {
            return E_UNEXPECTED;
        }
        const float width = static_cast<float>(widget.widthPixels);
        const float height = static_cast<float>(widget.heightPixels);
        const Layout layout = ComputeLayout(width, height);
        if (_facesDirty && _faces)
        {
            _gpu.UploadFaces(context->deviceContext, _faces.get());
            _facesDirty = false;
        }

        ViewerGpuLock();
        ViewerGpuResources* gpu = ViewerGpuGet();
        if (!gpu)
        {
            ViewerGpuUnlock();
            return E_UNEXPECTED;
        }
        _list.Reset();
        (void)_list.AddFill(layout.panel.x, layout.panel.y, layout.panel.width, layout.panel.height, 0.07f, 0.07f,
                            0.08f, 1.0f, 8.0f);
        HRESULT result = gpu->Render(context->deviceContext, width, height, _list);
        if (SUCCEEDED(result))
        {
            result = _gpu.DrawFaces(context->deviceContext, width, height, layout.grid);
        }
        _list.Reset();
        DrawOverlay(*gpu, layout);
        if (SUCCEEDED(result))
        {
            result = gpu->Render(context->deviceContext, width, height, _list);
        }
        ViewerGpuUnlock();
        return result;
    }

    HRESULT STDMETHODCALLTYPE OnPointer(const RedXePointerEvent* event) noexcept override
    {
        if (!event || event->sizeBytes != sizeof(RedXePointerEvent))
        {
            return E_INVALIDARG;
        }
        if (event->phase != RedXePointerPhaseDown && event->phase != RedXePointerPhaseUp &&
            event->phase != RedXePointerPhaseCancel)
        {
            return S_FALSE;
        }
        LogiconService* service = LogiconService::Current();
        const Layout layout =
            ComputeLayout(static_cast<float>(event->widthPixels), static_cast<float>(event->heightPixels));
        if (event->phase == RedXePointerPhaseUp || event->phase == RedXePointerPhaseCancel)
        {
            if (_heldKind != 0xFF && service)
            {
                (void)service->InjectControl(_heldKind, _heldIndex, false);
            }
            const bool consumed = _heldKind != 0xFF;
            _heldKind = 0xFF;
            return consumed ? S_OK : S_FALSE;
        }
        for (uint32_t slot = 0; slot < kKeyCount; ++slot)
        {
            if (layout.keys[slot].Contains(event->x, event->y))
            {
                return TapKey(service, slot);
            }
        }
        for (uint32_t button = 0; button < 2; ++button)
        {
            if (layout.pageButtons[button].Contains(event->x, event->y))
            {
                if (service && SUCCEEDED(service->InjectControl(kInjectKindPageButton, button, true)))
                {
                    _heldKind = kInjectKindPageButton;
                    _heldIndex = static_cast<uint8_t>(button);
                }
                return S_OK;
            }
        }
        for (uint32_t button = 0; button < kDialpadButtonCount; ++button)
        {
            if (layout.dialButtons[button].Contains(event->x, event->y))
            {
                if (service && SUCCEEDED(service->InjectControl(kInjectKindDialButton, button, true)))
                {
                    _heldKind = kInjectKindDialButton;
                    _heldIndex = static_cast<uint8_t>(button);
                }
                return S_OK;
            }
        }
        for (uint32_t index = 0; index < layout.modeChips.size(); ++index)
        {
            if (layout.modeChips[index].Contains(event->x, event->y))
            {
                _mode = static_cast<TapMode>(index);
                RequestFrame();
                return S_OK;
            }
        }
        if (layout.syntheticChip.Contains(event->x, event->y))
        {
            if (service)
            {
                (void)service->SetSynthetic(!_snapshot.synthetic);
            }
            return S_OK;
        }
        if (layout.brightnessDown.Contains(event->x, event->y) || layout.brightnessUp.Contains(event->x, event->y))
        {
            if (service)
            {
                const int delta = layout.brightnessUp.Contains(event->x, event->y) ? 10 : -10;
                const int next = std::clamp(static_cast<int>(_snapshot.brightness) + delta,
                                            static_cast<int>(kMinimumBrightness), static_cast<int>(kMaximumBrightness));
                (void)service->SetBrightness(static_cast<uint32_t>(next));
            }
            return S_OK;
        }
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE OnDragOver(float, float) noexcept override
    {
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE OnDragLeave() noexcept override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDrop(const RedXeDropEvent*) noexcept override
    {
        return S_FALSE;
    }

  private:
    void RequestFrame() noexcept
    {
        if (_host)
        {
            (void)_host->RequestFrame();
        }
    }

    [[nodiscard]] HRESULT TapKey(LogiconService* service, uint32_t slot) noexcept
    {
        if (!service)
        {
            return S_OK;
        }
        switch (_mode)
        {
        case TapMode::Press:
            if (SUCCEEDED(service->InjectControl(kInjectKindKey, slot, true)))
            {
                _heldKind = kInjectKindKey;
                _heldIndex = static_cast<uint8_t>(slot);
            }
            break;
        case TapMode::Color:
            (void)service->SetFaceOverride(slot, OverrideKind::Color, kPalette[_paletteIndex]);
            _paletteIndex = (_paletteIndex + 1) % static_cast<uint32_t>(kPalette.size());
            break;
        case TapMode::Picture:
            (void)service->SetFaceOverride(slot, OverrideKind::Picture, 0);
            break;
        case TapMode::Clear:
            (void)service->SetFaceOverride(slot, OverrideKind::None, 0);
            break;
        }
        return S_OK;
    }

    // Appends one HID++ frame as hex. prefix names the device and direction.
    void AppendTraceLine(ViewerGpuResources& gpu, const Layout& layout, float& y, const wchar_t* prefix,
                         const TraceEntry& entry) noexcept
    {
        std::array<wchar_t, kMaximumTextCharacters> text{};
        int written = swprintf_s(text.data(), text.size(), L"%s", prefix);
        const uint32_t shown = std::min<uint32_t>(entry.length, 12);
        for (uint32_t byteIndex = 0; byteIndex < shown && written > 0 && static_cast<size_t>(written) + 3 < text.size();
             ++byteIndex)
        {
            const int appended = swprintf_s(text.data() + written, text.size() - static_cast<size_t>(written), L"%02X ",
                                            entry.bytes[byteIndex]);
            if (appended <= 0)
            {
                break;
            }
            written += appended;
        }
        AppendLine(gpu, _list, layout.metrics, layout.textX, y, layout.textWidth, text.data(), 0.72f, 0.92f, 0.72f);
    }

    void DrawOverlay(ViewerGpuResources& gpu, const Layout& layout) noexcept
    {
        const MonitorSnapshot& snapshot = _snapshot;
        const Metrics& metrics = layout.metrics;
        std::array<wchar_t, kMaximumTextCharacters> text{};
        // Key overlays: slot number, action, pressed and override state, on a dark pill so faces never hide them.
        for (uint32_t slot = 0; slot < kKeyCount; ++slot)
        {
            const Rect& key = layout.keys[slot];
            const bool pressed = (snapshot.keys & (1U << slot)) != 0;
            if (pressed)
            {
                (void)_list.AddStroke(key.x - 2.0f, key.y - 2.0f, key.width + 4.0f, key.height + 4.0f, 1.0f, 0.85f,
                                      0.2f, 1.0f, 6.0f, 3.0f);
            }
            else if (snapshot.invalid[slot])
            {
                (void)_list.AddStroke(key.x, key.y, key.width, key.height, 0.9f, 0.2f, 0.2f, 0.9f, 4.0f, 2.0f);
            }
            else if (snapshot.overrides[slot].kind != OverrideKind::None)
            {
                (void)_list.AddStroke(key.x, key.y, key.width, key.height, 0.3f, 0.7f, 1.0f, 0.9f, 4.0f, 2.0f);
            }
            (void)swprintf_s(text.data(), text.size(), L"%u %S", slot + 1U, ActionLabel(snapshot.actions[slot]));
            const uint32_t characters = static_cast<uint32_t>(wcsnlen_s(text.data(), text.size()));
            (void)gpu.EnsureGlyphs(text.data(), characters);
            const float labelWidth = gpu.MeasureText(text.data(), characters, metrics.keyLabel);
            (void)_list.AddFill(key.x + 2.0f, key.y + 2.0f, labelWidth + 8.0f, metrics.keyLabel + 4.0f, 0.0f, 0.0f,
                                0.0f, 0.55f, 4.0f);
            (void)gpu.AppendText(_list, key.x + 6.0f, key.y + 4.0f, metrics.keyLabel, 1.0f, 1.0f, 1.0f, 1.0f,
                                 text.data(), characters);
        }
        // Page buttons.
        for (uint32_t button = 0; button < 2; ++button)
        {
            const Rect& rect = layout.pageButtons[button];
            const bool pressed = (snapshot.pageButtons & (1U << button)) != 0;
            AppendChip(gpu, _list, metrics, rect, button == 0 ? L"< page" : L"page >", pressed);
        }

        // Status column.
        float y = layout.textY;
        const float x = layout.textX;
        const float maxWidth = layout.textWidth;
        const wchar_t* link = snapshot.connected ? (snapshot.synthetic ? L"synthetic" : L"usb") : L"disconnected";
        (void)swprintf_s(text.data(), text.size(), L"Keypad %s · lane %s · access %s", link,
                         snapshot.laneRunning ? L"on" : L"off", snapshot.deviceAccess ? L"yes" : L"no");
        AppendLine(gpu, _list, metrics, x, y, maxWidth, text.data(), snapshot.connected ? 0.65f : 1.0f,
                   snapshot.connected ? 1.0f : 0.55f, 0.65f);
        (void)swprintf_s(text.data(), text.size(), L"19A1@%02X 1B04@%02X 8040@%02X · ports %u · page %u/%u",
                         snapshot.features.display, snapshot.features.reprogControls, snapshot.features.brightness,
                         snapshot.portCount, snapshot.keyPage + 1U, snapshot.keyPageCount);
        AppendLine(gpu, _list, metrics, x, y, maxWidth, text.data(), kTextPrimary, kTextPrimary, kTextPrimary);
        (void)swprintf_s(text.data(), text.size(), L"bright %u · faces %u (gen %u) · img %u", snapshot.brightness,
                         snapshot.facesWritten, snapshot.faceGeneration, snapshot.counters.imagesWritten);
        AppendLine(gpu, _list, metrics, x, y, maxWidth, text.data(), kTextPrimary, kTextPrimary, kTextPrimary);
        (void)swprintf_s(text.data(), text.size(), L"in %u · out %u · errors %u · hid++ 0x%02X",
                         snapshot.counters.reportsIn, snapshot.counters.reportsOut, snapshot.counters.commandErrors,
                         snapshot.counters.lastHidppError);
        AppendLine(gpu, _list, metrics, x, y, maxWidth, text.data(), kTextPrimary, kTextPrimary, kTextPrimary);
        (void)swprintf_s(text.data(), text.size(), L"host %u/%u %s%s · actions %u (last %S)",
                         snapshot.host.pageIndex + 1U, snapshot.host.pageCount, snapshot.host.pageName.data(),
                         (snapshot.host.flags & RedXeHostStateRaised) != 0 ? L" [raised]" : L"",
                         snapshot.actionsRequested, snapshot.lastAction.data());
        AppendLine(gpu, _list, metrics, x, y, maxWidth, text.data(), kTextPrimary, kTextPrimary, kTextPrimary);
        if (snapshot.competingWriter)
        {
            AppendLine(gpu, _list, metrics, x, y, maxWidth, L"Logi Options+ is running: faces may be repainted", 1.0f,
                       0.78f, 0.35f);
        }
        else if (FAILED(snapshot.lastFailure))
        {
            (void)swprintf_s(text.data(), text.size(), L"last failure 0x%08X after %u attempt(s)",
                             static_cast<unsigned>(snapshot.lastFailure), snapshot.connectAttempts);
            AppendLine(gpu, _list, metrics, x, y, maxWidth, text.data(), 1.0f, 0.6f, 0.6f);
        }
        else if (snapshot.systemFeed)
        {
            (void)swprintf_s(text.data(), text.size(), L"faces: %s · system cpu %d%% mem %d%% gpu %d%%",
                             snapshot.rendererReady ? L"ok" : L"unavailable", snapshot.system.cpuPercent,
                             snapshot.system.memoryPercent, snapshot.system.gpuPercent);
            AppendLine(gpu, _list, metrics, x, y, maxWidth, text.data(), kTextSecondary, kTextSecondary,
                       kTextSecondary);
        }
        else
        {
            AppendLine(gpu, _list, metrics, x, y, maxWidth,
                       snapshot.rendererReady ? (snapshot.iconFont ? L"faces: DirectWrite + Fluent icons"
                                                                   : L"faces: DirectWrite (no icon font)")
                                              : L"faces: unavailable",
                       kTextSecondary, kTextSecondary, kTextSecondary);
        }

        // Tap mode chips and toggles.
        const wchar_t* modes[] = {L"Press", L"Color", L"Picture", L"Clear"};
        for (uint32_t index = 0; index < layout.modeChips.size(); ++index)
        {
            AppendChip(gpu, _list, metrics, layout.modeChips[index], modes[index],
                       static_cast<uint32_t>(_mode) == index);
        }
        AppendChip(gpu, _list, metrics, layout.syntheticChip, L"Synthetic", snapshot.synthetic);
        AppendChip(gpu, _list, metrics, layout.brightnessDown, L"Bright -", false);
        AppendChip(gpu, _list, metrics, layout.brightnessUp, L"Bright +", false);

        // Dialpad: the HID++ vendor collection carries the four buttons; the dial and roller are the wheels of its
        // mouse collection, read through Raw Input.
        const DialpadSnapshot& dialpad = snapshot.dialpad;
        y = layout.dialpadY;
        if (dialpad.connected)
        {
            (void)swprintf_s(text.data(), text.size(), L"Dialpad bluetooth · 1B04@%02X · in %u · presses %u",
                             dialpad.features.reprogControls, dialpad.counters.reportsIn, dialpad.buttonPresses);
            AppendLine(gpu, _list, metrics, x, y, maxWidth, text.data(), 0.65f, 1.0f, 0.65f);
        }
        else if (FAILED(dialpad.lastFailure) && dialpad.lastFailure != HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED))
        {
            (void)swprintf_s(text.data(), text.size(), L"Dialpad failed 0x%08X after %u attempt(s)",
                             static_cast<unsigned>(dialpad.lastFailure), dialpad.connectAttempts);
            AppendLine(gpu, _list, metrics, x, y, maxWidth, text.data(), 1.0f, 0.6f, 0.6f);
        }
        else
        {
            AppendLine(gpu, _list, metrics, x, y, maxWidth, L"Dialpad not connected (Bluetooth, PID BC00)", 1.0f, 0.55f,
                       0.65f);
        }
        const WheelState& wheels = dialpad.wheels;
        if (dialpad.wheelsListening)
        {
            // Detents = raw / kWheelDetentUnits; the raw sum and the action bound to each wheel stay visible.
            (void)swprintf_s(text.data(), text.size(), L"dial %+d (raw %+d, %u ev) -> %S / %S · steps %u",
                             wheels.dialRaw / static_cast<int32_t>(kWheelDetentUnits), wheels.dialRaw,
                             wheels.dialEvents, dialpad.turns[0].data(), dialpad.turns[1].data(), dialpad.wheelSteps);
            AppendLine(gpu, _list, metrics, x, y, maxWidth, text.data(), kTextPrimary, kTextPrimary, kTextPrimary);
            (void)swprintf_s(text.data(), text.size(), L"roller %+d (raw %+d, %u ev) -> %S / %S",
                             wheels.rollerRaw / static_cast<int32_t>(kWheelDetentUnits), wheels.rollerRaw,
                             wheels.rollerEvents, dialpad.turns[2].data(), dialpad.turns[3].data());
            AppendLine(gpu, _list, metrics, x, y, maxWidth, text.data(), kTextPrimary, kTextPrimary, kTextPrimary);
        }
        else
        {
            AppendLine(gpu, _list, metrics, x, y, maxWidth, L"wheels: raw input unavailable", 1.0f, 0.78f, 0.35f);
            (void)swprintf_s(text.data(), text.size(), L"other mice %u", wheels.otherReports);
            AppendLine(gpu, _list, metrics, x, y, maxWidth, text.data(), kTextSecondary, kTextSecondary,
                       kTextSecondary);
        }
        // Unbound buttons are not diverted (they keep working as Back/Forward and their keyboard usages) and are
        // shown in brackets; a tap still injects the press so a binding can be exercised without hardware.
        const wchar_t* dialNames[kDialpadButtonCount] = {L"Back", L"Forward", L"Btn 6", L"Btn 7"};
        const wchar_t* nativeNames[kDialpadButtonCount] = {L"[Back]", L"[Forward]", L"[Btn 6]", L"[Btn 7]"};
        for (uint32_t button = 0; button < kDialpadButtonCount; ++button)
        {
            const bool diverted = (dialpad.divertedButtons & (1U << button)) != 0;
            AppendChip(gpu, _list, metrics, layout.dialButtons[button],
                       diverted ? dialNames[button] : nativeNames[button], (dialpad.buttons & (1U << button)) != 0);
        }

        // Trace: the newest frames of both devices, merged by tick, as many as fit.
        y = layout.traceY;
        (void)swprintf_s(text.data(), text.size(), L"HID++ trace · mouse 0x%02X motion %u other %u", wheels.buttonMask,
                         wheels.motionEvents, wheels.otherReports);
        AppendLine(gpu, _list, metrics, x, y, maxWidth, text.data(), kTextSecondary, kTextSecondary, kTextSecondary);
        const uint32_t room =
            y + metrics.line <= layout.bottom ? static_cast<uint32_t>((layout.bottom - y) / metrics.line) : 0U;
        const uint32_t lines = std::min(room, kTraceLines);
        // Pick the newest `lines` entries from the two oldest-first arrays, then print them oldest first.
        struct Pick final
        {
            const TraceEntry* entry = nullptr;
            bool dialpad = false;
        };
        std::array<Pick, kTraceLines> picks{};
        uint32_t picked = 0;
        uint32_t keypadIndex = snapshot.traceCount;
        uint32_t dialpadIndex = dialpad.traceCount;
        while (picked < lines && (keypadIndex > 0 || dialpadIndex > 0))
        {
            const bool takeDialpad = keypadIndex == 0 || (dialpadIndex > 0 && dialpad.trace[dialpadIndex - 1].tick >=
                                                                                  snapshot.trace[keypadIndex - 1].tick);
            if (takeDialpad)
            {
                picks[picked++] = Pick{&dialpad.trace[--dialpadIndex], true};
            }
            else
            {
                picks[picked++] = Pick{&snapshot.trace[--keypadIndex], false};
            }
        }
        for (uint32_t index = picked; index > 0; --index)
        {
            const Pick& pick = picks[index - 1];
            AppendTraceLine(gpu, layout, y,
                            pick.dialpad ? (pick.entry->outbound ? L"d> " : L"d< ")
                                         : (pick.entry->outbound ? L"k> " : L"k< "),
                            *pick.entry);
        }
    }

    wil::com_ptr_nothrow<IRedXeWidgetProvider> _providerOwner;
    IRedXeHost* _host;
    std::array<char, 128> _instanceId{};
    MonitorGpu _gpu;
    ViewerDrawList _list;
    MonitorSnapshot _snapshot{};
    bool _snapshotValid = false;
    std::unique_ptr<uint32_t[]> _faces;
    uint32_t _faceGeneration = 0;
    bool _facesDirty = false;
    bool _visible = false;
    bool _attached = false;
    TapMode _mode = TapMode::Press;
    uint32_t _paletteIndex = 0;
    uint8_t _heldKind = 0xFF;
    uint8_t _heldIndex = 0;
};

class MonitorProvider final : public RedXeComObject<MonitorProvider, IRedXeWidgetProvider>
{
  public:
    explicit MonitorProvider(IRedXeHost* host) noexcept : _host(host) {}

    HRESULT STDMETHODCALLTYPE GetWidgetTypes(const RedXeWidgetTypeDescriptor** descriptors,
                                             uint32_t* count) noexcept override
    {
        if (descriptors)
        {
            *descriptors = nullptr;
        }
        if (count)
        {
            *count = 0;
        }
        if (!descriptors || !count)
        {
            return E_POINTER;
        }
        *descriptors = kWidgetTypes.data();
        *count = static_cast<uint32_t>(kWidgetTypes.size());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateWidget(const char* typeId, const char* instanceId,
                                           IRedXeWidget** widget) noexcept override
    {
        if (!widget)
        {
            return E_POINTER;
        }
        *widget = nullptr;
        if (!typeId || !instanceId || instanceId[0] == '\0')
        {
            return E_INVALIDARG;
        }
        if (!RedXeAsciiEqualsIgnoreCase(typeId, kMonitorWidgetTypeId))
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
        wil::com_ptr_nothrow<IRedXeWidgetProvider> providerOwner;
        const HRESULT result =
            QueryInterface(__uuidof(IRedXeWidgetProvider), reinterpret_cast<void**>(providerOwner.put()));
        if (FAILED(result))
        {
            return result;
        }
        auto* created = new (std::nothrow) MonitorWidget(std::move(providerOwner), _host, instanceId);
        if (!created)
        {
            return E_OUTOFMEMORY;
        }
        *widget = static_cast<IRedXeWidget*>(created);
        return S_OK;
    }

  private:
    IRedXeHost* _host;
};
} // namespace

HRESULT CreateMonitorProvider(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                              void** result) noexcept
{
    if (interfaceId != __uuidof(IRedXeWidgetProvider))
    {
        return E_NOINTERFACE;
    }
    const HRESULT configurationResult = RedXeValidateEmptyNormalizedConfiguration(options);
    if (FAILED(configurationResult))
    {
        return configurationResult;
    }
    auto* provider = new (std::nothrow) MonitorProvider(host);
    if (!provider)
    {
        return E_OUTOFMEMORY;
    }
    *result = provider;
    return S_OK;
}
} // namespace Logicon
