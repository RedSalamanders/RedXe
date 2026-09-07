#include "HostChrome.h"

#include "HostChromePixelShader.h"
#include "HostChromeVertexShader.h"
#include "PageEdgeAffordance.h"
#include "PluginHost.h"
#include "WidgetRaise.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <dwrite.h>

namespace
{
// Colors match the former GDI chrome: RGB(8,10,16) dim, black shadow, RGB(52,62,84) close hover wash, RGB(10,14,26)
// band wash at kPageEdgeRevealedAlpha, RGB(228,236,248) chevron, RGB(214,220,230) / white close glyph.
constexpr float kDimRgb[3] = {8.0f / 255.0f, 10.0f / 255.0f, 16.0f / 255.0f};
constexpr float kShadowAlpha = 0.42f;
constexpr float kCloseHoverColor[4] = {52.0f / 255.0f, 62.0f / 255.0f, 84.0f / 255.0f, 1.0f};
constexpr float kCloseGlyphColor[4] = {214.0f / 255.0f, 220.0f / 255.0f, 230.0f / 255.0f, 1.0f};
constexpr float kCloseGlyphHoverColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
constexpr float kBandWashColor[4] = {10.0f / 255.0f, 14.0f / 255.0f, 26.0f / 255.0f,
                                     static_cast<float>(kPageEdgeRevealedAlpha) / 255.0f};
constexpr float kChevronColor[4] = {228.0f / 255.0f, 236.0f / 255.0f, 248.0f / 255.0f, 1.0f};
constexpr float kGlyphEmDips = 22.0f;

[[nodiscard]] wchar_t GlyphCharacter(HostChromeGlyph glyph, FluentIcons::IconFont font) noexcept
{
    switch (glyph)
    {
    case HostChromeGlyph::ChevronLeft:
        return PageEdgeChevronGlyph(kPageEdgeDirectionPrevious, font);
    case HostChromeGlyph::ChevronRight:
        return PageEdgeChevronGlyph(kPageEdgeDirectionNext, font);
    case HostChromeGlyph::Close:
        return FluentIcons::SelectGlyph(font, FluentIcons::kClear, FluentIcons::kFallbackClear);
    default:
        return L'\0';
    }
}

[[nodiscard]] RECT GlyphRectCentred(const RECT& cell, const RECT& ink, UINT cellPixels) noexcept
{
    // Place the glyph's ink box centred in the target cell at its native pixel size (already rasterized for the DPI).
    const LONG inkWidth = ink.right - ink.left;
    const LONG inkHeight = ink.bottom - ink.top;
    const LONG centreX = (cell.left + cell.right) / 2;
    const LONG centreY = (cell.top + cell.bottom) / 2;
    if (inkWidth <= 0 || inkHeight <= 0 || inkWidth > static_cast<LONG>(cellPixels) ||
        inkHeight > static_cast<LONG>(cellPixels))
    {
        return RECT{};
    }
    return RECT{centreX - inkWidth / 2, centreY - inkHeight / 2, centreX - inkWidth / 2 + inkWidth,
                centreY - inkHeight / 2 + inkHeight};
}
} // namespace

HostChromeResources::~HostChromeResources()
{
    Release();
}

HRESULT HostChromeResources::Initialize(ID3D11Device* device, UINT dpi) noexcept
{
    Release();
    if (!device)
    {
        return E_POINTER;
    }
    HRESULT result = device->CreateVertexShader(g_HostChromeVertexShader, sizeof(g_HostChromeVertexShader), nullptr,
                                                _vertexShader.put());
    if (FAILED(result))
    {
        return result;
    }
    result = device->CreatePixelShader(g_HostChromePixelShader, sizeof(g_HostChromePixelShader), nullptr,
                                       _pixelShader.put());
    if (FAILED(result))
    {
        return result;
    }
    D3D11_BUFFER_DESC constants{};
    constants.ByteWidth = sizeof(Constants);
    constants.Usage = D3D11_USAGE_DYNAMIC;
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    result = device->CreateBuffer(&constants, nullptr, _constants.put());
    if (FAILED(result))
    {
        return result;
    }
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    result = device->CreateBlendState(&blend, _blend.put());
    if (FAILED(result))
    {
        return result;
    }
    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    result = device->CreateRasterizerState(&rasterizer, _rasterizer.put());
    if (FAILED(result))
    {
        return result;
    }
    D3D11_DEPTH_STENCIL_DESC depth{};
    result = device->CreateDepthStencilState(&depth, _depth.put());
    if (FAILED(result))
    {
        return result;
    }
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    result = device->CreateSamplerState(&sampler, _sampler.put());
    if (FAILED(result))
    {
        return result;
    }
    _device = device;
    // Glyph rasterization is best effort: wash-only chrome is still correct chrome.
    (void)BuildAtlas(dpi);
    return S_OK;
}

void HostChromeResources::Release() noexcept
{
    _atlasView.reset();
    _sampler.reset();
    _depth.reset();
    _rasterizer.reset();
    _blend.reset();
    _constants.reset();
    _pixelShader.reset();
    _vertexShader.reset();
    _device.reset();
    _glyphInk = {};
    _font = FluentIcons::IconFont::TextFallback;
    _dpi = 0;
    _glyphsAvailable = false;
}

HRESULT HostChromeResources::SetDpi(UINT dpi) noexcept
{
    if (!_device)
    {
        return E_UNEXPECTED;
    }
    const UINT effective = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
    if (effective == _dpi)
    {
        return S_FALSE;
    }
    return BuildAtlas(effective);
}

bool HostChromeResources::Ready() const noexcept
{
    return _device && _vertexShader && _pixelShader && _constants;
}

bool HostChromeResources::GlyphsAvailable() const noexcept
{
    return _glyphsAvailable;
}

FluentIcons::IconFont HostChromeResources::Font() const noexcept
{
    return _font;
}

UINT HostChromeResources::Dpi() const noexcept
{
    return _dpi;
}

HRESULT HostChromeResources::BuildAtlas(UINT dpi) noexcept
{
    _atlasView.reset();
    _glyphInk = {};
    _glyphsAvailable = false;
    _dpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
    if (!_device)
    {
        return E_UNEXPECTED;
    }

    // One shared DirectWrite factory for the rasterization only; nothing survives past this function except the
    // texture. The system font collection is the same one DxUi already loaded in this process.
    wil::com_ptr_nothrow<IDWriteFactory> factory;
    HRESULT result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                         reinterpret_cast<IUnknown**>(factory.put()));
    wil::com_ptr_nothrow<IDWriteFontCollection> collection;
    if (SUCCEEDED(result))
    {
        result = factory->GetSystemFontCollection(collection.put(), FALSE);
    }
    wil::com_ptr_nothrow<IDWriteFontFace> face;
    if (SUCCEEDED(result))
    {
        const wchar_t* family = FluentIcons::ResolveIconFamily(collection.get(), _font);
        UINT32 familyIndex = 0;
        BOOL exists = FALSE;
        result = collection->FindFamilyName(family, &familyIndex, &exists);
        if (SUCCEEDED(result) && !exists)
        {
            result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
        wil::com_ptr_nothrow<IDWriteFontFamily> fontFamily;
        if (SUCCEEDED(result))
        {
            result = collection->GetFontFamily(familyIndex, fontFamily.put());
        }
        wil::com_ptr_nothrow<IDWriteFont> font;
        if (SUCCEEDED(result))
        {
            result = fontFamily->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                      DWRITE_FONT_STYLE_NORMAL, font.put());
        }
        if (SUCCEEDED(result))
        {
            result = font->CreateFontFace(face.put());
        }
    }
    if (FAILED(result))
    {
        if (!_atlasFailureLogged)
        {
            _atlasFailureLogged = true;
            (void)RedXeHostLog(PluginHost::Instance().Interface(), RedXeLogLevelWarning, nullptr, nullptr,
                               "host-chrome-glyphs-unavailable",
                               "DirectWrite icon rasterization failed; host chrome draws washes only.", result);
        }
        return result;
    }

    // Atlas: kGlyphCount cells of kCellPixels, one row. Rasterize each glyph at the DPI-scaled em size, centred in
    // its cell, and remember its ink box for exact placement.
    constexpr uint32_t atlasWidth = kCellPixels * kGlyphCount;
    constexpr uint32_t atlasHeight = kCellPixels;
    std::array<uint8_t, atlasWidth * atlasHeight> pixels{};
    DWRITE_FONT_METRICS fontMetrics{};
    face->GetMetrics(&fontMetrics);
    if (fontMetrics.designUnitsPerEm == 0)
    {
        return E_UNEXPECTED;
    }
    const float emPixels = kGlyphEmDips * static_cast<float>(_dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    const float designScale = emPixels / static_cast<float>(fontMetrics.designUnitsPerEm);
    bool anyGlyph = false;
    for (uint32_t slot = 0; slot < kGlyphCount; ++slot)
    {
        const wchar_t character = GlyphCharacter(static_cast<HostChromeGlyph>(slot), _font);
        const UINT32 codePoint = static_cast<UINT32>(character);
        UINT16 glyphIndex = 0;
        if (character == L'\0' || FAILED(face->GetGlyphIndices(&codePoint, 1, &glyphIndex)) || glyphIndex == 0)
        {
            continue;
        }
        DWRITE_GLYPH_METRICS glyphMetrics{};
        if (FAILED(face->GetDesignGlyphMetrics(&glyphIndex, 1, &glyphMetrics, FALSE)))
        {
            continue;
        }
        // Baseline placed so the glyph's design box is centred in the cell.
        const float advance = static_cast<float>(glyphMetrics.advanceWidth) * designScale;
        const float ascent = static_cast<float>(fontMetrics.ascent) * designScale;
        const float descent = static_cast<float>(fontMetrics.descent) * designScale;
        const float cell = static_cast<float>(kCellPixels);
        const float baselineX = static_cast<float>(slot) * cell + (cell - advance) * 0.5f;
        const float baselineY = (cell - ascent - descent) * 0.5f + ascent;
        const DWRITE_GLYPH_RUN run{face.get(), emPixels, 1, &glyphIndex, &advance, nullptr, FALSE, 0};
        wil::com_ptr_nothrow<IDWriteGlyphRunAnalysis> analysis;
        if (FAILED(factory->CreateGlyphRunAnalysis(&run, 1.0f, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                                   DWRITE_MEASURING_MODE_NATURAL, baselineX, baselineY,
                                                   analysis.put())))
        {
            continue;
        }
        RECT bounds{};
        if (FAILED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds)))
        {
            continue;
        }
        const LONG cellLeft = static_cast<LONG>(slot * kCellPixels);
        bounds.left = std::max(bounds.left, cellLeft);
        bounds.top = std::max(bounds.top, 0L);
        bounds.right = std::min(bounds.right, cellLeft + static_cast<LONG>(kCellPixels));
        bounds.bottom = std::min(bounds.bottom, static_cast<LONG>(kCellPixels));
        if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        {
            continue;
        }
        const uint32_t width = static_cast<uint32_t>(bounds.right - bounds.left);
        const uint32_t height = static_cast<uint32_t>(bounds.bottom - bounds.top);
        std::array<uint8_t, kCellPixels * kCellPixels * 3> scratch{};
        if (FAILED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, scratch.data(),
                                                width * height * 3U)))
        {
            continue;
        }
        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                const size_t source = (static_cast<size_t>(y) * width + x) * 3U;
                const uint32_t coverage =
                    static_cast<uint32_t>(scratch[source]) + scratch[source + 1U] + scratch[source + 2U];
                const size_t destination = static_cast<size_t>(bounds.top + static_cast<LONG>(y)) * atlasWidth +
                                           static_cast<size_t>(bounds.left + static_cast<LONG>(x));
                pixels[destination] = static_cast<uint8_t>((coverage + 1U) / 3U);
            }
        }
        // Ink box relative to the cell origin.
        _glyphInk[slot] = RECT{bounds.left - cellLeft, bounds.top, bounds.right - cellLeft, bounds.bottom};
        anyGlyph = true;
    }
    if (!anyGlyph)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    D3D11_TEXTURE2D_DESC description{};
    description.Width = atlasWidth;
    description.Height = atlasHeight;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA initial{pixels.data(), atlasWidth, 0};
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    result = _device->CreateTexture2D(&description, &initial, texture.put());
    if (FAILED(result))
    {
        return result;
    }
    result = _device->CreateShaderResourceView(texture.get(), nullptr, _atlasView.put());
    if (FAILED(result))
    {
        return result;
    }
    _glyphsAvailable = true;
    return S_OK;
}

HRESULT HostChromeResources::DrawQuad(ID3D11DeviceContext* context, const RECT& rect, const float (&color)[4],
                                      UINT viewportWidth, UINT viewportHeight, const HostChromeGlyph* glyph,
                                      size_t& quadsDrawn) noexcept
{
    if (rect.right <= rect.left || rect.bottom <= rect.top)
    {
        return S_FALSE;
    }
    Constants constants{};
    constants.rect[0] = static_cast<float>(rect.left);
    constants.rect[1] = static_cast<float>(rect.top);
    constants.rect[2] = static_cast<float>(rect.right);
    constants.rect[3] = static_cast<float>(rect.bottom);
    std::memcpy(constants.color, color, sizeof(constants.color));
    constants.viewportGlyph[0] = static_cast<float>(viewportWidth);
    constants.viewportGlyph[1] = static_cast<float>(viewportHeight);
    if (glyph)
    {
        const uint32_t slot = static_cast<uint32_t>(*glyph);
        const RECT& ink = _glyphInk[slot];
        constexpr float atlasWidth = static_cast<float>(kCellPixels * kGlyphCount);
        constexpr float atlasHeight = static_cast<float>(kCellPixels);
        constants.uv[0] = (static_cast<float>(slot * kCellPixels) + static_cast<float>(ink.left)) / atlasWidth;
        constants.uv[1] = static_cast<float>(ink.top) / atlasHeight;
        constants.uv[2] = (static_cast<float>(slot * kCellPixels) + static_cast<float>(ink.right)) / atlasWidth;
        constants.uv[3] = static_cast<float>(ink.bottom) / atlasHeight;
        constants.viewportGlyph[2] = 1.0f;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT result = context->Map(_constants.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(result))
    {
        return result;
    }
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    context->Unmap(_constants.get(), 0);
    context->Draw(4, 0);
    ++quadsDrawn;
    return S_OK;
}

HRESULT HostChromeResources::Draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, UINT viewportWidth,
                                  UINT viewportHeight, const HostChromeState& state, HostChromePhase phase,
                                  size_t& quadsDrawn) noexcept
{
    if (!context || !target)
    {
        return E_POINTER;
    }
    if (!Ready() || viewportWidth == 0 || viewportHeight == 0)
    {
        return S_FALSE;
    }
    if (HostChromeQuadCount(state, viewportWidth, viewportHeight, _glyphsAvailable) == 0)
    {
        return S_FALSE;
    }

    // Bind the complete pipeline: the widget that drew before this left arbitrary state behind.
    ID3D11RenderTargetView* targets[] = {target};
    context->OMSetRenderTargets(1, targets, nullptr);
    const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight),
                                  0.0f, 1.0f};
    context->RSSetViewports(1, &viewport);
    context->RSSetState(_rasterizer.get());
    context->OMSetBlendState(_blend.get(), nullptr, 0xffffffff);
    context->OMSetDepthStencilState(_depth.get(), 0);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->VSSetShader(_vertexShader.get(), nullptr, 0);
    context->PSSetShader(_pixelShader.get(), nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* constants[] = {_constants.get()};
    context->VSSetConstantBuffers(0, 1, constants);
    context->PSSetConstantBuffers(0, 1, constants);
    ID3D11ShaderResourceView* atlas[] = {_atlasView.get()};
    context->PSSetShaderResources(0, 1, atlas);
    ID3D11SamplerState* samplers[] = {_sampler.get()};
    context->PSSetSamplers(0, 1, samplers);
    context->SetPredication(nullptr, FALSE);

    HRESULT result = S_OK;
    if (phase == HostChromePhase::BelowRaised)
    {
        if (state.raised && state.dimAlpha != 0)
        {
            const float dim[4] = {kDimRgb[0], kDimRgb[1], kDimRgb[2], static_cast<float>(state.dimAlpha) / 255.0f};
            std::array<RECT, 4> strips{};
            const size_t count =
                HostChromeDimStrips(viewportWidth, viewportHeight, state.content, strips.data(), strips.size());
            for (size_t index = 0; index < count && SUCCEEDED(result); ++index)
            {
                result = DrawQuad(context, strips[index], dim, viewportWidth, viewportHeight, nullptr, quadsDrawn);
            }
        }
        if (SUCCEEDED(result) && state.raised)
        {
            const float shadow[4] = {0.0f, 0.0f, 0.0f, kShadowAlpha * static_cast<float>(state.dimAlpha) / 255.0f};
            result = DrawQuad(context, state.shadow, shadow, viewportWidth, viewportHeight, nullptr, quadsDrawn);
        }
    }
    else
    {
        if (state.raised && state.close.right > state.close.left && state.close.bottom > state.close.top)
        {
            if (state.closeHovered)
            {
                result = DrawQuad(context, state.close, kCloseHoverColor, viewportWidth, viewportHeight, nullptr,
                                  quadsDrawn);
            }
            if (SUCCEEDED(result) && _glyphsAvailable)
            {
                const HostChromeGlyph glyph = HostChromeGlyph::Close;
                const RECT glyphRect =
                    GlyphRectCentred(state.close, _glyphInk[static_cast<uint32_t>(glyph)], kCellPixels);
                result = DrawQuad(context, glyphRect, state.closeHovered ? kCloseGlyphHoverColor : kCloseGlyphColor,
                                  viewportWidth, viewportHeight, &glyph, quadsDrawn);
            }
        }
        for (const HostChromeEdgeBand& band : state.bands)
        {
            if (FAILED(result) || !band.revealed)
            {
                continue;
            }
            result = DrawQuad(context, band.rect, kBandWashColor, viewportWidth, viewportHeight, nullptr, quadsDrawn);
            if (SUCCEEDED(result) && _glyphsAvailable)
            {
                const HostChromeGlyph glyph = band.direction == kPageEdgeDirectionPrevious
                                                  ? HostChromeGlyph::ChevronLeft
                                                  : HostChromeGlyph::ChevronRight;
                const RECT cell = PageEdgeChevronCell(band.rect, _dpi);
                const RECT glyphRect = GlyphRectCentred(cell, _glyphInk[static_cast<uint32_t>(glyph)], kCellPixels);
                result = DrawQuad(context, glyphRect, kChevronColor, viewportWidth, viewportHeight, &glyph, quadsDrawn);
            }
        }
    }

    ID3D11ShaderResourceView* unbound[] = {nullptr};
    context->PSSetShaderResources(0, 1, unbound);
    return FAILED(result) ? result : S_OK;
}
