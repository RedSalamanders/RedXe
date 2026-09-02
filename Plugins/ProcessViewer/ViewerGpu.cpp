#include "ViewerGpu.h"

#include "ViewerPixelShader.h"
#include "ViewerVertexShader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <dwrite.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr float kGlyphEmSize = 40.0f;
constexpr uint32_t kStaticGlyphFirst = 32;
constexpr uint32_t kStaticGlyphLast = 126;
constexpr uint32_t kStaticGlyphCount = kStaticGlyphLast - kStaticGlyphFirst + 1;
constexpr uint32_t kMissingGlyph = 0xFFFFFFFFu;

SRWLOCK g_gpuLock = SRWLOCK_INIT;
ViewerGpuResources g_resources;
uint32_t g_users = 0;
std::atomic<uint32_t> g_liveResourceSets{0};
std::atomic<uint32_t> g_mapCount{0};
std::atomic<uint32_t> g_drawCount{0};
std::atomic<uint32_t> g_typographyCount{0};

[[nodiscard]] HRESULT CreateViewerFontFace(IDWriteFactory& factory,
                                           wil::com_ptr_nothrow<IDWriteFontFace>& face) noexcept
{
    face.reset();
    wil::com_ptr_nothrow<IDWriteFontCollection> collection;
    HRESULT result = factory.GetSystemFontCollection(collection.put(), FALSE);
    if (FAILED(result))
    {
        return result;
    }
    constexpr const wchar_t* families[] = {L"Bahnschrift", L"Segoe UI", L"Arial"};
    for (const wchar_t* familyName : families)
    {
        UINT32 familyIndex = 0;
        BOOL exists = FALSE;
        result = collection->FindFamilyName(familyName, &familyIndex, &exists);
        if (FAILED(result) || !exists)
        {
            continue;
        }
        wil::com_ptr_nothrow<IDWriteFontFamily> family;
        result = collection->GetFontFamily(familyIndex, family.put());
        if (FAILED(result))
        {
            return result;
        }
        wil::com_ptr_nothrow<IDWriteFont> font;
        result = family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STRETCH_CONDENSED,
                                              DWRITE_FONT_STYLE_NORMAL, font.put());
        if (FAILED(result))
        {
            continue;
        }
        result = font->CreateFontFace(face.put());
        if (SUCCEEDED(result))
        {
            return S_OK;
        }
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

struct DirectWriteSession final
{
    wil::unique_hmodule module;
    wil::com_ptr_nothrow<IDWriteFactory> factory;
    wil::com_ptr_nothrow<IDWriteFontFace> face;
};

[[nodiscard]] HRESULT OpenDirectWrite(DirectWriteSession& session) noexcept
{
    session.module.reset(LoadLibraryExW(L"dwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
    if (!session.module)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    using DWriteCreateFactoryFn = HRESULT(WINAPI*)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
    const auto createFactory =
        reinterpret_cast<DWriteCreateFactoryFn>(GetProcAddress(session.module.get(), "DWriteCreateFactory"));
    if (!createFactory)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    HRESULT result = createFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(session.factory.put()));
    if (FAILED(result))
    {
        return result;
    }
    return CreateViewerFontFace(*session.factory, session.face);
}

[[nodiscard]] HRESULT RasterizeIntoAtlas(IDWriteFactory& factory, IDWriteFontFace& face, wchar_t character,
                                         uint32_t atlasX, uint32_t atlasY, uint8_t* atlasPixels,
                                         float& advance) noexcept
{
    const UINT32 codePoint = static_cast<UINT32>(character);
    UINT16 glyphIndex = 0;
    HRESULT result = face.GetGlyphIndices(&codePoint, 1, &glyphIndex);
    if (FAILED(result))
    {
        return result;
    }
    if (glyphIndex == 0)
    {
        advance = 0.35f;
        return S_OK;
    }
    DWRITE_GLYPH_METRICS glyphMetrics{};
    result = face.GetDesignGlyphMetrics(&glyphIndex, 1, &glyphMetrics, FALSE);
    if (FAILED(result))
    {
        return result;
    }
    DWRITE_FONT_METRICS fontMetrics{};
    face.GetMetrics(&fontMetrics);
    if (fontMetrics.designUnitsPerEm == 0)
    {
        return E_UNEXPECTED;
    }
    const float designScale = kGlyphEmSize / static_cast<float>(fontMetrics.designUnitsPerEm);
    const float pixelAdvance = static_cast<float>(glyphMetrics.advanceWidth) * designScale;
    advance = pixelAdvance / static_cast<float>(kViewerGlyphCell);
    const float baselineX = 4.0f - static_cast<float>(glyphMetrics.leftSideBearing) * designScale;
    const float capHeight = static_cast<float>(fontMetrics.capHeight) * designScale;
    const float descent = static_cast<float>(fontMetrics.descent) * designScale;
    const float baselineY = (static_cast<float>(kViewerGlyphCell) - capHeight - descent) * 0.5f + capHeight;
    const DWRITE_GLYPH_RUN run{&face, kGlyphEmSize, 1, &glyphIndex, &pixelAdvance, nullptr, FALSE, 0};
    wil::com_ptr_nothrow<IDWriteGlyphRunAnalysis> analysis;
    result = factory.CreateGlyphRunAnalysis(&run, 1.0f, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                            DWRITE_MEASURING_MODE_NATURAL, baselineX, baselineY, analysis.put());
    if (FAILED(result))
    {
        return result;
    }
    RECT bounds{};
    result = analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);
    if (FAILED(result))
    {
        return result;
    }
    bounds.left = std::max(bounds.left, 0L);
    bounds.top = std::max(bounds.top, 0L);
    bounds.right = std::min(bounds.right, static_cast<LONG>(kViewerGlyphCell));
    bounds.bottom = std::min(bounds.bottom, static_cast<LONG>(kViewerGlyphCell));
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
    {
        return S_OK;
    }
    const uint32_t width = static_cast<uint32_t>(bounds.right - bounds.left);
    const uint32_t height = static_cast<uint32_t>(bounds.bottom - bounds.top);
    std::array<uint8_t, kViewerGlyphCell * kViewerGlyphCell * 3> scratch{};
    const uint32_t required = width * height * 3U;
    result = analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, scratch.data(), required);
    if (FAILED(result))
    {
        return result;
    }
    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            const size_t source = (static_cast<size_t>(y) * width + x) * 3U;
            const uint32_t coverage =
                static_cast<uint32_t>(scratch[source]) + scratch[source + 1U] + scratch[source + 2U];
            const size_t destination =
                static_cast<size_t>(atlasY + static_cast<uint32_t>(bounds.top) + y) * kViewerAtlasSize + atlasX +
                static_cast<uint32_t>(bounds.left) + x;
            atlasPixels[destination] = static_cast<uint8_t>((coverage + 1U) / 3U);
        }
    }
    return S_OK;
}
} // namespace

ViewerGpuResources::~ViewerGpuResources()
{
    Reset();
}

HRESULT ViewerGpuResources::Initialize(ID3D11Device* device) noexcept
{
    if (!device)
    {
        return E_POINTER;
    }
    if (_deviceIdentity == device && _instanceBuffer)
    {
        return S_OK;
    }

    HRESULT result = BuildStaticAtlas();
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<ID3D11VertexShader> vertexShader;
    result =
        device->CreateVertexShader(g_ViewerVertexShader, sizeof(g_ViewerVertexShader), nullptr, vertexShader.put());
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<ID3D11PixelShader> pixelShader;
    result = device->CreatePixelShader(g_ViewerPixelShader, sizeof(g_ViewerPixelShader), nullptr, pixelShader.put());
    if (FAILED(result))
    {
        return result;
    }

    constexpr D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"RECT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    wil::com_ptr_nothrow<ID3D11InputLayout> inputLayout;
    result =
        device->CreateInputLayout(layout, 4, g_ViewerVertexShader, sizeof(g_ViewerVertexShader), inputLayout.put());
    if (FAILED(result))
    {
        return result;
    }

    D3D11_BUFFER_DESC constantDescription{};
    constantDescription.ByteWidth = sizeof(ViewerConstants);
    constantDescription.Usage = D3D11_USAGE_DYNAMIC;
    constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    wil::com_ptr_nothrow<ID3D11Buffer> constantBuffer;
    result = device->CreateBuffer(&constantDescription, nullptr, constantBuffer.put());
    if (FAILED(result))
    {
        return result;
    }

    D3D11_BUFFER_DESC instanceDescription{};
    instanceDescription.ByteWidth = sizeof(ViewerQuadInstance) * kViewerMaximumQuads;
    instanceDescription.Usage = D3D11_USAGE_DYNAMIC;
    instanceDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    instanceDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    wil::com_ptr_nothrow<ID3D11Buffer> instanceBuffer;
    result = device->CreateBuffer(&instanceDescription, nullptr, instanceBuffer.put());
    if (FAILED(result))
    {
        return result;
    }

    D3D11_TEXTURE2D_DESC atlasDescription{};
    atlasDescription.Width = kViewerAtlasSize;
    atlasDescription.Height = kViewerAtlasSize;
    atlasDescription.MipLevels = 1;
    atlasDescription.ArraySize = 1;
    atlasDescription.Format = DXGI_FORMAT_R8_UNORM;
    atlasDescription.SampleDesc.Count = 1;
    atlasDescription.Usage = D3D11_USAGE_DEFAULT;
    atlasDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA atlasData{};
    atlasData.pSysMem = _atlasPixels.data();
    atlasData.SysMemPitch = kViewerAtlasSize;
    wil::com_ptr_nothrow<ID3D11Texture2D> atlas;
    result = device->CreateTexture2D(&atlasDescription, &atlasData, atlas.put());
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> atlasView;
    result = device->CreateShaderResourceView(atlas.get(), nullptr, atlasView.put());
    if (FAILED(result))
    {
        return result;
    }

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    wil::com_ptr_nothrow<ID3D11SamplerState> sampler;
    result = device->CreateSamplerState(&samplerDescription, sampler.put());
    if (FAILED(result))
    {
        return result;
    }

    D3D11_RASTERIZER_DESC rasterizerDescription{};
    rasterizerDescription.FillMode = D3D11_FILL_SOLID;
    rasterizerDescription.CullMode = D3D11_CULL_NONE;
    rasterizerDescription.DepthClipEnable = TRUE;
    wil::com_ptr_nothrow<ID3D11RasterizerState> rasterizer;
    result = device->CreateRasterizerState(&rasterizerDescription, rasterizer.put());
    if (FAILED(result))
    {
        return result;
    }

    D3D11_DEPTH_STENCIL_DESC depthDescription{};
    depthDescription.DepthEnable = FALSE;
    depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depthDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;
    wil::com_ptr_nothrow<ID3D11DepthStencilState> depthState;
    result = device->CreateDepthStencilState(&depthDescription, depthState.put());
    if (FAILED(result))
    {
        return result;
    }

    D3D11_BLEND_DESC blendDescription{};
    D3D11_RENDER_TARGET_BLEND_DESC& target = blendDescription.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D11_BLEND_ONE;
    target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    wil::com_ptr_nothrow<ID3D11BlendState> blendState;
    result = device->CreateBlendState(&blendDescription, blendState.put());
    if (FAILED(result))
    {
        return result;
    }

    Reset();
    _deviceIdentity = device;
    _vertexShader = std::move(vertexShader);
    _pixelShader = std::move(pixelShader);
    _inputLayout = std::move(inputLayout);
    _constantBuffer = std::move(constantBuffer);
    _instanceBuffer = std::move(instanceBuffer);
    _atlas = std::move(atlas);
    _atlasView = std::move(atlasView);
    _sampler = std::move(sampler);
    _rasterizer = std::move(rasterizer);
    _depthState = std::move(depthState);
    _blendState = std::move(blendState);
    _atlasDirty = false;
    g_liveResourceSets.fetch_add(1, std::memory_order_relaxed);
    return S_OK;
}

void ViewerGpuResources::Reset() noexcept
{
    const bool hadResources = _instanceBuffer != nullptr;
    _vertexShader.reset();
    _pixelShader.reset();
    _inputLayout.reset();
    _constantBuffer.reset();
    _instanceBuffer.reset();
    _atlas.reset();
    _atlasView.reset();
    _sampler.reset();
    _rasterizer.reset();
    _depthState.reset();
    _blendState.reset();
    _deviceIdentity = nullptr;
    if (hadResources)
    {
        g_liveResourceSets.fetch_sub(1, std::memory_order_relaxed);
    }
}

HRESULT ViewerGpuResources::BuildStaticAtlas() noexcept
{
    _atlasPixels.fill(0);
    _glyphCharacters.fill(0);
    _glyphAdvances.fill(0.0f);
    _glyphCount = 0;
    _dynamicCursor = kStaticGlyphCount;
    DirectWriteSession session;
    HRESULT result = OpenDirectWrite(session);
    if (FAILED(result))
    {
        return result;
    }
    for (uint32_t character = kStaticGlyphFirst; character <= kStaticGlyphLast; ++character)
    {
        const uint32_t slot = _glyphCount;
        const uint32_t atlasX = (slot % kViewerGlyphColumns) * kViewerGlyphCell;
        const uint32_t atlasY = (slot / kViewerGlyphColumns) * kViewerGlyphCell;
        float advance = 0.5f;
        result = RasterizeIntoAtlas(*session.factory, *session.face, static_cast<wchar_t>(character), atlasX, atlasY,
                                    _atlasPixels.data(), advance);
        if (FAILED(result))
        {
            return result;
        }
        _glyphCharacters[slot] = static_cast<wchar_t>(character);
        _glyphAdvances[slot] = advance;
        ++_glyphCount;
    }
    _atlasDirty = true;
    g_typographyCount.fetch_add(1, std::memory_order_relaxed);
    return S_OK;
}

uint32_t ViewerGpuResources::FindGlyph(wchar_t character) const noexcept
{
    if (character >= static_cast<wchar_t>(kStaticGlyphFirst) && character <= static_cast<wchar_t>(kStaticGlyphLast))
    {
        return static_cast<uint32_t>(character) - kStaticGlyphFirst;
    }
    for (uint32_t index = kStaticGlyphCount; index < _glyphCount; ++index)
    {
        if (_glyphCharacters[index] == character)
        {
            return index;
        }
    }
    return kMissingGlyph;
}

HRESULT ViewerGpuResources::EnsureGlyphs(const wchar_t* text, uint32_t characters) noexcept
{
    if (!text && characters != 0)
    {
        return E_POINTER;
    }
    bool missing = false;
    for (uint32_t index = 0; index < characters; ++index)
    {
        const wchar_t character = text[index];
        if (character == L'\0')
        {
            break;
        }
        if (character == L'\n' || character == L'\r')
        {
            continue;
        }
        if (FindGlyph(character) == kMissingGlyph)
        {
            missing = true;
            break;
        }
    }
    if (!missing)
    {
        return S_OK;
    }

    DirectWriteSession session;
    HRESULT result = OpenDirectWrite(session);
    if (FAILED(result))
    {
        return result;
    }
    for (uint32_t index = 0; index < characters; ++index)
    {
        const wchar_t character = text[index];
        if (character == L'\0')
        {
            break;
        }
        if (character == L'\n' || character == L'\r' || FindGlyph(character) != kMissingGlyph)
        {
            continue;
        }
        if (_dynamicCursor >= kViewerGlyphCapacity)
        {
            _dynamicCursor = kStaticGlyphCount;
        }
        const uint32_t slot = _dynamicCursor++;
        const uint32_t atlasX = (slot % kViewerGlyphColumns) * kViewerGlyphCell;
        const uint32_t atlasY = (slot / kViewerGlyphColumns) * kViewerGlyphCell;
        float advance = 0.5f;
        result = RasterizeIntoAtlas(*session.factory, *session.face, character, atlasX, atlasY, _atlasPixels.data(),
                                    advance);
        if (FAILED(result))
        {
            return result;
        }
        _glyphCharacters[slot] = character;
        _glyphAdvances[slot] = advance;
        if (slot >= _glyphCount)
        {
            _glyphCount = slot + 1;
        }
        _atlasDirty = true;
    }
    g_typographyCount.fetch_add(1, std::memory_order_relaxed);
    return S_OK;
}

float ViewerGpuResources::MeasureText(const wchar_t* text, uint32_t characters, float height) const noexcept
{
    if (!text || height <= 0.0f)
    {
        return 0.0f;
    }
    float width = 0.0f;
    for (uint32_t index = 0; index < characters; ++index)
    {
        const wchar_t character = text[index];
        if (character == L'\0')
        {
            break;
        }
        const uint32_t slot = FindGlyph(character);
        const float advance = slot == kMissingGlyph ? 0.45f : _glyphAdvances[slot];
        width += advance * height;
    }
    return width;
}

HRESULT ViewerGpuResources::AppendText(ViewerDrawList& list, float x, float y, float height, float red, float green,
                                       float blue, float alpha, const wchar_t* text, uint32_t characters) noexcept
{
    if (!text || height <= 0.0f)
    {
        return S_OK;
    }
    float pen = x;
    const float inverseAtlas = 1.0f / static_cast<float>(kViewerAtlasSize);
    const float cell = static_cast<float>(kViewerGlyphCell) * inverseAtlas;
    for (uint32_t index = 0; index < characters; ++index)
    {
        const wchar_t character = text[index];
        if (character == L'\0')
        {
            break;
        }
        const uint32_t slot = FindGlyph(character);
        const float advance = (slot == kMissingGlyph ? 0.45f : _glyphAdvances[slot]) * height;
        if (slot != kMissingGlyph)
        {
            const float u0 = static_cast<float>(slot % kViewerGlyphColumns) * cell;
            const float v0 = static_cast<float>(slot / kViewerGlyphColumns) * cell;
            if (!list.Add(ViewerQuadKindGlyph, pen, y, height, height, red, green, blue, alpha, 0.0f, 0.0f, u0, v0,
                          u0 + cell, v0 + cell))
            {
                return S_OK;
            }
        }
        pen += advance;
    }
    return S_OK;
}

void ViewerGpuResources::UploadAtlas() noexcept
{
    if (!_atlasDirty || !_atlas || !_deviceIdentity)
    {
        return;
    }
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    _deviceIdentity->GetImmediateContext(context.put());
    if (!context)
    {
        return;
    }
    context->UpdateSubresource(_atlas.get(), 0, nullptr, _atlasPixels.data(), kViewerAtlasSize, 0);
    _atlasDirty = false;
}

HRESULT ViewerGpuResources::Render(ID3D11DeviceContext* context, float width, float height,
                                   const ViewerDrawList& list) noexcept
{
    if (!context)
    {
        return E_POINTER;
    }
    if (!_instanceBuffer || width <= 0.0f || height <= 0.0f || list.Count() == 0)
    {
        return S_OK;
    }
    UploadAtlas();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT result = context->Map(_constantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(result))
    {
        return result;
    }
    auto* constants = static_cast<ViewerConstants*>(mapped.pData);
    constants->targetSize[0] = width;
    constants->targetSize[1] = height;
    constants->targetSize[2] = 0.0f;
    constants->targetSize[3] = 0.0f;
    context->Unmap(_constantBuffer.get(), 0);

    result = context->Map(_instanceBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(result))
    {
        return result;
    }
    std::memcpy(mapped.pData, list.Data(), sizeof(ViewerQuadInstance) * list.Count());
    context->Unmap(_instanceBuffer.get(), 0);
    g_mapCount.fetch_add(1, std::memory_order_relaxed);

    const UINT stride = sizeof(ViewerQuadInstance);
    const UINT offset = 0;
    ID3D11Buffer* instanceBuffer = _instanceBuffer.get();
    ID3D11Buffer* constantBuffer = _constantBuffer.get();
    ID3D11ShaderResourceView* atlasView = _atlasView.get();
    ID3D11SamplerState* sampler = _sampler.get();
    const float blendFactor[4] = {};
    context->IASetInputLayout(_inputLayout.get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetVertexBuffers(0, 1, &instanceBuffer, &stride, &offset);
    context->VSSetShader(_vertexShader.get(), nullptr, 0);
    context->PSSetShader(_pixelShader.get(), nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &constantBuffer);
    context->PSSetShaderResources(0, 1, &atlasView);
    context->PSSetSamplers(0, 1, &sampler);
    context->RSSetState(_rasterizer.get());
    context->OMSetDepthStencilState(_depthState.get(), 0);
    context->OMSetBlendState(_blendState.get(), blendFactor, 0xffffffff);
    context->DrawInstanced(6, list.Count(), 0, 0);
    g_drawCount.fetch_add(1, std::memory_order_relaxed);
    return S_OK;
}

HRESULT ViewerGpuAcquire(ID3D11Device* device) noexcept
{
    AcquireSRWLockExclusive(&g_gpuLock);
    const HRESULT result = g_resources.Initialize(device);
    if (SUCCEEDED(result))
    {
        ++g_users;
    }
    ReleaseSRWLockExclusive(&g_gpuLock);
    return result;
}

void ViewerGpuRelease() noexcept
{
    AcquireSRWLockExclusive(&g_gpuLock);
    if (g_users > 0)
    {
        --g_users;
        if (g_users == 0)
        {
            g_resources.Reset();
        }
    }
    ReleaseSRWLockExclusive(&g_gpuLock);
}

ViewerGpuResources* ViewerGpuGet() noexcept
{
    return g_users > 0 ? &g_resources : nullptr;
}
