#include "WeatherGpu.h"

#include "WeatherIcons.h"
#include "WeatherPixelShader.h"
#include "WeatherVertexShader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cwchar>
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
constexpr wchar_t kStaticExtraGlyphs[] = {0x00B0};
constexpr uint32_t kStaticExtraCount =
    static_cast<uint32_t>(sizeof(kStaticExtraGlyphs) / sizeof(kStaticExtraGlyphs[0]));
constexpr uint32_t kStaticReservedCount = kStaticGlyphCount + kStaticExtraCount;
constexpr uint32_t kHeroGlyphCount = static_cast<uint32_t>(sizeof(kWeatherHeroGlyphs) / sizeof(kWeatherHeroGlyphs[0]));
constexpr uint32_t kIconGlyphCount = static_cast<uint32_t>(sizeof(kWeatherIconGlyphs) / sizeof(kWeatherIconGlyphs[0]));
// The bottom rows of the atlas hold every 96 px cell: condition icons first, then hero glyphs.
constexpr uint32_t kLargeCellColumns = kWeatherAtlasSize / kWeatherIconCell;
constexpr uint32_t kLargeCellRows = 3;
constexpr uint32_t kLargeCellCapacity = kLargeCellColumns * kLargeCellRows;
constexpr uint32_t kLargeCellReserveY = kWeatherAtlasSize - kWeatherIconCell * kLargeCellRows;
constexpr uint32_t kMissingGlyph = 0xFFFFFFFFu;
// Every large twin consumes a slot index, and slot indices map onto 48 px cell positions above the reserve.
static_assert(kStaticReservedCount + 2 * kIconGlyphCount + kHeroGlyphCount <
              (kLargeCellReserveY / kWeatherGlyphCell) * kWeatherGlyphColumns);
static_assert(kIconGlyphCount + kHeroGlyphCount <= kLargeCellCapacity);
static_assert(kLargeCellCapacity <= kWeatherGlyphCapacity);

SRWLOCK g_gpuLock = SRWLOCK_INIT;
WeatherGpuResources g_resources;
uint32_t g_users = 0;
std::atomic<uint32_t> g_liveResourceSets{0};
std::atomic<uint32_t> g_mapCount{0};
std::atomic<uint32_t> g_drawCount{0};
std::atomic<uint32_t> g_typographyCount{0};
std::atomic<uint32_t> g_heroTwinCount{0};
std::atomic<uint32_t> g_iconCellCount{0};

[[nodiscard]] HRESULT CreateWeatherFontFace(IDWriteFactory& factory,
                                            wil::com_ptr_nothrow<IDWriteFontFace>& face) noexcept
{
    face.reset();
    wil::com_ptr_nothrow<IDWriteFontCollection> collection;
    HRESULT result = factory.GetSystemFontCollection(collection.put(), FALSE);
    if (FAILED(result))
    {
        return result;
    }
    constexpr const wchar_t* families[] = {L"Segoe UI", L"Bahnschrift", L"Arial"};
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
        result = family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_LIGHT, DWRITE_FONT_STRETCH_NORMAL,
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

[[nodiscard]] HRESULT CreateWeatherIconFontFace(IDWriteFactory& factory,
                                                wil::com_ptr_nothrow<IDWriteFontFace>& face) noexcept
{
    face.reset();
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(static_cast<const void*>(&CreateWeatherIconFontFace)),
                           &module) == FALSE)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return HRESULT_FROM_WIN32(length == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER);
    }
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash)
    {
        return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    }
    slash[1] = L'\0';
    if (wcscat_s(path, L"weathericons-regular-webfont.ttf") != 0)
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
    {
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    wil::com_ptr_nothrow<IDWriteFontFile> file;
    HRESULT result = factory.CreateFontFileReference(path, nullptr, file.put());
    if (FAILED(result))
    {
        return result;
    }
    BOOL supported = FALSE;
    DWRITE_FONT_FILE_TYPE fileType{};
    DWRITE_FONT_FACE_TYPE faceType{};
    UINT32 faceCount = 0;
    result = file->Analyze(&supported, &fileType, &faceType, &faceCount);
    if (FAILED(result) || supported == FALSE || faceCount == 0)
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
    IDWriteFontFile* files[] = {file.get()};
    return factory.CreateFontFace(faceType, 1, files, 0, DWRITE_FONT_SIMULATIONS_NONE, face.put());
}

struct DirectWriteSession final
{
    wil::unique_hmodule module;
    wil::com_ptr_nothrow<IDWriteFactory> factory;
    wil::com_ptr_nothrow<IDWriteFontFace> face;
    wil::com_ptr_nothrow<IDWriteFontFace> iconFace;
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
    result = CreateWeatherFontFace(*session.factory, session.face);
    if (FAILED(result))
    {
        return result;
    }
    (void)CreateWeatherIconFontFace(*session.factory, session.iconFace);
    return S_OK;
}

[[nodiscard]] HRESULT BlitGlyphAnalysis(IDWriteGlyphRunAnalysis& analysis, uint32_t atlasX, uint32_t atlasY,
                                        uint32_t cell, uint8_t* atlasPixels) noexcept
{
    RECT bounds{};
    HRESULT result = analysis.GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);
    if (FAILED(result))
    {
        return result;
    }
    // Reject a clipped glyph rather than silently baking missing strokes into the atlas.
    if (bounds.left < 0 || bounds.top < 0 || bounds.right > static_cast<LONG>(cell) ||
        bounds.bottom > static_cast<LONG>(cell))
        return E_BOUNDS;
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
    {
        return S_OK;
    }
    const uint32_t width = static_cast<uint32_t>(bounds.right - bounds.left);
    const uint32_t height = static_cast<uint32_t>(bounds.bottom - bounds.top);
    std::array<uint8_t, kWeatherIconCell * kWeatherIconCell * 3> scratch{};
    const uint32_t required = width * height * 3U;
    result = analysis.CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, scratch.data(), required);
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
                static_cast<size_t>(atlasY + static_cast<uint32_t>(bounds.top) + y) * kWeatherAtlasSize + atlasX +
                static_cast<uint32_t>(bounds.left) + x;
            atlasPixels[destination] = static_cast<uint8_t>((coverage + 1U) / 3U);
        }
    }
    return S_OK;
}

[[nodiscard]] HRESULT RasterizeIntoAtlas(IDWriteFactory& factory, IDWriteFontFace& face, wchar_t character,
                                         uint32_t atlasX, uint32_t atlasY, uint32_t cell, uint8_t* atlasPixels,
                                         float& advance, bool centerInCell, WeatherGlyphInk& ink) noexcept
{
    ink = {};
    const float cellF = static_cast<float>(cell);
    const float maxInk = cellF - 4.0f;
    const float fitInk = cellF - 6.0f;
    const LONG cellEdge = static_cast<LONG>(cell) - 1;
    for (uint32_t row = 0; row < cell; ++row)
        std::memset(atlasPixels + static_cast<size_t>(atlasY + row) * kWeatherAtlasSize + atlasX, 0, cell);
    const UINT32 codePoint = static_cast<UINT32>(character);
    UINT16 glyphIndex = 0;
    HRESULT result = face.GetGlyphIndices(&codePoint, 1, &glyphIndex);
    if (FAILED(result))
    {
        return result;
    }
    if (glyphIndex == 0)
    {
        advance = centerInCell ? 1.0f : 0.35f;
        return centerInCell ? S_FALSE : S_OK;
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
    // Text cells scale from the 48 px base so a 96 px twin is an exact 2x raster with the same normalized metrics.
    const float textScale = cellF / static_cast<float>(kWeatherGlyphCell);
    float emSize = centerInCell ? cellF * 0.78f
                                : textScale * std::min(kGlyphEmSize, (static_cast<float>(kWeatherGlyphCell) - 4.0f) *
                                                                         fontMetrics.designUnitsPerEm /
                                                                         (fontMetrics.ascent + fontMetrics.descent));
    const float designScale = emSize / static_cast<float>(fontMetrics.designUnitsPerEm);
    float pixelAdvance = static_cast<float>(glyphMetrics.advanceWidth) * designScale;
    advance = centerInCell ? 1.0f : pixelAdvance / cellF;
    float baselineX = 3.0f * textScale;
    const float ascent = static_cast<float>(fontMetrics.ascent) * designScale;
    const float descent = static_cast<float>(fontMetrics.descent) * designScale;
    float baselineY = (cellF - ascent - descent) * 0.5f + ascent;
    if (centerInCell)
    {
        baselineX = 0.0f;
        baselineY = emSize;
    }
    DWRITE_GLYPH_RUN run{&face, emSize, 1, &glyphIndex, &pixelAdvance, nullptr, FALSE, 0};
    wil::com_ptr_nothrow<IDWriteGlyphRunAnalysis> analysis;
    result = factory.CreateGlyphRunAnalysis(&run, 1.0f, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                            DWRITE_MEASURING_MODE_NATURAL, baselineX, baselineY, analysis.put());
    if (FAILED(result))
    {
        return result;
    }
    for (uint32_t attempt = 0; attempt < 4; ++attempt)
    {
        RECT bounds{};
        result = analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);
        if (FAILED(result))
        {
            return result;
        }
        const float inkWidth = static_cast<float>(bounds.right - bounds.left);
        const float inkHeight = static_cast<float>(bounds.bottom - bounds.top);
        if (inkWidth <= 0.0f || inkHeight <= 0.0f)
            return S_OK;
        if (inkWidth <= maxInk && inkHeight <= maxInk && bounds.left >= 1 && bounds.top >= 1 &&
            bounds.right <= cellEdge && bounds.bottom <= cellEdge && (!centerInCell || attempt != 0))
        {
            // Include one transparent texel to preserve antialiasing when filtering the cropped quad.
            ink = {(bounds.left - 1) / cellF, (bounds.top - 1) / cellF, (inkWidth + 2) / cellF,
                   (inkHeight + 2) / cellF};
            advance = centerInCell ? 1.0f : pixelAdvance / cellF;
            return BlitGlyphAnalysis(*analysis, atlasX, atlasY, cell, atlasPixels);
        }
        if (inkWidth > maxInk || inkHeight > maxInk)
        {
            const float scale = fitInk / std::max(inkWidth, inkHeight);
            emSize *= scale;
            pixelAdvance *= scale;
            run.fontEmSize = emSize;
            baselineX *= scale;
            baselineY *= scale;
        }
        else
        {
            const float shiftX =
                (centerInCell ? (cellF - inkWidth) * 0.5f
                              : std::clamp(static_cast<float>(bounds.left), 2.0f, cellF - 2.0f - inkWidth)) -
                bounds.left;
            const float shiftY =
                (centerInCell ? (cellF - inkHeight) * 0.5f
                              : std::clamp(static_cast<float>(bounds.top), 2.0f, cellF - 2.0f - inkHeight)) -
                bounds.top;
            baselineX += shiftX;
            baselineY += shiftY;
        }
        analysis.reset();
        result = factory.CreateGlyphRunAnalysis(&run, 1.0f, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                                DWRITE_MEASURING_MODE_NATURAL, baselineX, baselineY, analysis.put());
        if (FAILED(result))
        {
            return result;
        }
    }
    return E_BOUNDS;
}
} // namespace

WeatherGpuResources::~WeatherGpuResources()
{
    Reset();
}

HRESULT WeatherGpuResources::Initialize(ID3D11Device* device) noexcept
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
        device->CreateVertexShader(g_WeatherVertexShader, sizeof(g_WeatherVertexShader), nullptr, vertexShader.put());
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<ID3D11PixelShader> pixelShader;
    result = device->CreatePixelShader(g_WeatherPixelShader, sizeof(g_WeatherPixelShader), nullptr, pixelShader.put());
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
        device->CreateInputLayout(layout, 4, g_WeatherVertexShader, sizeof(g_WeatherVertexShader), inputLayout.put());
    if (FAILED(result))
    {
        return result;
    }

    D3D11_BUFFER_DESC constantDescription{};
    constantDescription.ByteWidth = sizeof(WeatherConstants);
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
    instanceDescription.ByteWidth = sizeof(WeatherQuadInstance) * kWeatherMaximumQuads;
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
    atlasDescription.Width = kWeatherAtlasSize;
    atlasDescription.Height = kWeatherAtlasSize;
    atlasDescription.MipLevels = 1;
    atlasDescription.ArraySize = 1;
    atlasDescription.Format = DXGI_FORMAT_R8_UNORM;
    atlasDescription.SampleDesc.Count = 1;
    atlasDescription.Usage = D3D11_USAGE_DEFAULT;
    atlasDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA atlasData{};
    atlasData.pSysMem = _atlasPixels.data();
    atlasData.SysMemPitch = kWeatherAtlasSize;
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

void WeatherGpuResources::Reset() noexcept
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

HRESULT WeatherGpuResources::BuildStaticAtlas() noexcept
{
    _atlasPixels.fill(0);
    _glyphCharacters.fill(0);
    _glyphAdvances.fill(0.0f);
    _glyphInk.fill({});
    _glyphAtlasX.fill(0);
    _glyphAtlasY.fill(0);
    _glyphCell.fill(0);
    _glyphLarge.fill(0);
    _glyphCount = 0;
    _dynamicCursor = kStaticReservedCount;
    DirectWriteSession session;
    HRESULT result = OpenDirectWrite(session);
    if (FAILED(result))
    {
        return result;
    }
    const auto place = [this](uint32_t slot, uint32_t atlasX, uint32_t atlasY, uint32_t cell) noexcept
    {
        _glyphAtlasX[slot] = static_cast<uint16_t>(atlasX);
        _glyphAtlasY[slot] = static_cast<uint16_t>(atlasY);
        _glyphCell[slot] = static_cast<uint16_t>(cell);
    };
    for (uint32_t character = kStaticGlyphFirst; character <= kStaticGlyphLast; ++character)
    {
        const uint32_t slot = _glyphCount;
        const uint32_t atlasX = (slot % kWeatherGlyphColumns) * kWeatherGlyphCell;
        const uint32_t atlasY = (slot / kWeatherGlyphColumns) * kWeatherGlyphCell;
        if (atlasY + kWeatherGlyphCell > kLargeCellReserveY)
        {
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
        float advance = 0.5f;
        result = RasterizeIntoAtlas(*session.factory, *session.face, static_cast<wchar_t>(character), atlasX, atlasY,
                                    kWeatherGlyphCell, _atlasPixels.data(), advance, false, _glyphInk[slot]);
        if (FAILED(result))
        {
            return result;
        }
        _glyphCharacters[slot] = static_cast<wchar_t>(character);
        _glyphAdvances[slot] = advance;
        place(slot, atlasX, atlasY, kWeatherGlyphCell);
        ++_glyphCount;
    }
    for (uint32_t extra = 0; extra < kStaticExtraCount; ++extra)
    {
        const uint32_t slot = _glyphCount;
        const uint32_t atlasX = (slot % kWeatherGlyphColumns) * kWeatherGlyphCell;
        const uint32_t atlasY = (slot / kWeatherGlyphColumns) * kWeatherGlyphCell;
        if (atlasY + kWeatherGlyphCell > kLargeCellReserveY)
        {
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
        float advance = 0.5f;
        result = RasterizeIntoAtlas(*session.factory, *session.face, kStaticExtraGlyphs[extra], atlasX, atlasY,
                                    kWeatherGlyphCell, _atlasPixels.data(), advance, false, _glyphInk[slot]);
        if (FAILED(result))
        {
            return result;
        }
        _glyphCharacters[slot] = kStaticExtraGlyphs[extra];
        _glyphAdvances[slot] = advance;
        place(slot, atlasX, atlasY, kWeatherGlyphCell);
        ++_glyphCount;
    }
    uint32_t iconCells = 0;
    uint32_t largeIndex = 0;
    const auto largeCellOrigin = [](uint32_t index, uint32_t& x, uint32_t& y) noexcept
    {
        x = (index % kLargeCellColumns) * kWeatherIconCell;
        y = kLargeCellReserveY + (index / kLargeCellColumns) * kWeatherIconCell;
    };
    if (session.iconFace)
    {
        for (wchar_t glyph : kWeatherIconGlyphs)
        {
            const uint32_t slot = _glyphCount;
            const uint32_t atlasX = (slot % kWeatherGlyphColumns) * kWeatherGlyphCell;
            const uint32_t atlasY = (slot / kWeatherGlyphColumns) * kWeatherGlyphCell;
            if (atlasY + kWeatherGlyphCell > kLargeCellReserveY)
            {
                return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            }
            float advance = 1.0f;
            result = RasterizeIntoAtlas(*session.factory, *session.iconFace, glyph, atlasX, atlasY, kWeatherGlyphCell,
                                        _atlasPixels.data(), advance, true, _glyphInk[slot]);
            if (result == S_FALSE)
            {
                continue;
            }
            if (FAILED(result))
            {
                return result;
            }
            _glyphCharacters[slot] = glyph;
            _glyphAdvances[slot] = advance;
            place(slot, atlasX, atlasY, kWeatherGlyphCell);
            ++_glyphCount;
            if (largeIndex < kLargeCellCapacity)
            {
                const uint32_t largeSlot = _glyphCount;
                uint32_t largeX = 0;
                uint32_t largeY = 0;
                largeCellOrigin(largeIndex++, largeX, largeY);
                float largeAdvance = 1.0f;
                result =
                    RasterizeIntoAtlas(*session.factory, *session.iconFace, glyph, largeX, largeY, kWeatherIconCell,
                                       _atlasPixels.data(), largeAdvance, true, _glyphInk[largeSlot]);
                if (FAILED(result))
                {
                    return result;
                }
                if (result == S_OK)
                {
                    _glyphCharacters[largeSlot] = 0;
                    _glyphAdvances[largeSlot] = largeAdvance;
                    place(largeSlot, largeX, largeY, kWeatherIconCell);
                    _glyphLarge[slot] = static_cast<uint16_t>(largeSlot);
                    ++_glyphCount;
                    ++iconCells;
                }
            }
        }
    }
    // Hero glyphs already own a static 48 px cell; add the 96 px twin that AppendText picks above ~1.1x that cell.
    uint32_t heroLinked = 0;
    for (wchar_t glyph : kWeatherHeroGlyphs)
    {
        const uint32_t slot = FindGlyph(glyph);
        if (slot == kMissingGlyph || _glyphInk[slot].width <= 0.0f || largeIndex >= kLargeCellCapacity)
        {
            continue;
        }
        const uint32_t largeSlot = _glyphCount;
        uint32_t largeX = 0;
        uint32_t largeY = 0;
        largeCellOrigin(largeIndex++, largeX, largeY);
        float largeAdvance = 0.5f;
        WeatherGlyphInk largeInk{};
        result = RasterizeIntoAtlas(*session.factory, *session.face, glyph, largeX, largeY, kWeatherIconCell,
                                    _atlasPixels.data(), largeAdvance, false, largeInk);
        if (FAILED(result))
        {
            return result;
        }
        // The twin reuses the small cell's normalized advance and ink so MeasureText is slot-independent. Its own ink
        // (an exact 2x raster rounded outward, inside the small cell's doubled padding) must sit within that
        // rectangle; otherwise keep drawing this glyph from the small cell.
        const WeatherGlyphInk& ink = _glyphInk[slot];
        if (largeInk.width <= 0.0f || largeInk.left < ink.left || largeInk.top < ink.top ||
            largeInk.left + largeInk.width > ink.left + ink.width ||
            largeInk.top + largeInk.height > ink.top + ink.height)
        {
            continue;
        }
        _glyphCharacters[largeSlot] = 0;
        _glyphAdvances[largeSlot] = _glyphAdvances[slot];
        _glyphInk[largeSlot] = ink;
        place(largeSlot, largeX, largeY, kWeatherIconCell);
        _glyphLarge[slot] = static_cast<uint16_t>(largeSlot);
        ++_glyphCount;
        ++heroLinked;
    }
    g_heroTwinCount.store(heroLinked, std::memory_order_relaxed);
    g_iconCellCount.store(iconCells, std::memory_order_relaxed);
    _staticGlyphCount = _glyphCount;
    _dynamicCursor = _glyphCount;
    _atlasDirty = true;
    g_typographyCount.fetch_add(1, std::memory_order_relaxed);
    return S_OK;
}

uint32_t WeatherGpuResources::ResolveLargeSlot(uint32_t slot, float extent) const noexcept
{
    const uint16_t large = _glyphLarge[slot];
    if (large != 0 && extent > static_cast<float>(_glyphCell[slot]) * 1.1f && large < _glyphCount &&
        _glyphCell[large] != 0)
    {
        return large;
    }
    return slot;
}

uint32_t WeatherGpuResources::FindGlyph(wchar_t character) const noexcept
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

HRESULT WeatherGpuResources::EnsureGlyphs(const wchar_t* text, uint32_t characters) noexcept
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
        if (_dynamicCursor >= kWeatherGlyphCapacity)
        {
            _dynamicCursor = _staticGlyphCount == 0 ? kStaticReservedCount : _staticGlyphCount;
        }
        const uint32_t slot = _dynamicCursor++;
        const uint32_t atlasX = (slot % kWeatherGlyphColumns) * kWeatherGlyphCell;
        const uint32_t atlasY = (slot / kWeatherGlyphColumns) * kWeatherGlyphCell;
        if (atlasY + kWeatherGlyphCell > kLargeCellReserveY)
        {
            --_dynamicCursor;
            break;
        }
        float advance = 0.5f;
        result = RasterizeIntoAtlas(*session.factory, *session.face, character, atlasX, atlasY, kWeatherGlyphCell,
                                    _atlasPixels.data(), advance, false, _glyphInk[slot]);
        if (FAILED(result))
        {
            return result;
        }
        _glyphCharacters[slot] = character;
        _glyphAdvances[slot] = advance;
        _glyphAtlasX[slot] = static_cast<uint16_t>(atlasX);
        _glyphAtlasY[slot] = static_cast<uint16_t>(atlasY);
        _glyphCell[slot] = static_cast<uint16_t>(kWeatherGlyphCell);
        if (slot >= _glyphCount)
        {
            _glyphCount = slot + 1;
        }
        _atlasDirty = true;
    }
    g_typographyCount.fetch_add(1, std::memory_order_relaxed);
    return S_OK;
}

float WeatherGpuResources::MeasureText(const wchar_t* text, uint32_t characters, float height) const noexcept
{
    if (!text || height <= 0.0f)
    {
        return 0.0f;
    }
    float width = 0.0f;
    float pen = 0.0f;
    for (uint32_t index = 0; index < characters; ++index)
    {
        const wchar_t character = text[index];
        if (character == L'\0')
        {
            break;
        }
        const uint32_t slot = FindGlyph(character);
        const float advance = slot == kMissingGlyph ? 0.45f : _glyphAdvances[slot];
        if (slot != kMissingGlyph)
            width = std::max(width, pen + (_glyphInk[slot].left + _glyphInk[slot].width) * height);
        pen += advance * height;
    }
    return std::max(width, pen);
}

HRESULT WeatherGpuResources::AppendText(WeatherDrawList& list, float x, float y, float height, float red, float green,
                                        float blue, float alpha, const wchar_t* text, uint32_t characters) noexcept
{
    if (!text || height <= 0.0f)
    {
        return S_OK;
    }
    float pen = x;
    const float inverseAtlas = 1.0f / static_cast<float>(kWeatherAtlasSize);
    for (uint32_t index = 0; index < characters; ++index)
    {
        const wchar_t character = text[index];
        if (character == L'\0')
        {
            break;
        }
        // A large twin shares the small cell's normalized metrics, so this pick never changes measured layout.
        const uint32_t slot = FindGlyph(character);
        const float advance = (slot == kMissingGlyph ? 0.45f : _glyphAdvances[slot]) * height;
        if (slot != kMissingGlyph)
        {
            const uint32_t drawSlot = ResolveLargeSlot(slot, height);
            const auto& ink = _glyphInk[drawSlot];
            const float cellPx =
                static_cast<float>(_glyphCell[drawSlot] != 0 ? _glyphCell[drawSlot] : kWeatherGlyphCell);
            const float u0 = (static_cast<float>(_glyphAtlasX[drawSlot]) + ink.left * cellPx) * inverseAtlas;
            const float v0 = (static_cast<float>(_glyphAtlasY[drawSlot]) + ink.top * cellPx) * inverseAtlas;
            if (!list.Add(WeatherQuadKindGlyph, pen + ink.left * height, y + ink.top * height, ink.width * height,
                          ink.height * height, red, green, blue, alpha, 0.0f, 0.0f, u0, v0,
                          u0 + ink.width * cellPx * inverseAtlas, v0 + ink.height * cellPx * inverseAtlas))
            {
                return S_OK;
            }
        }
        pen += advance;
    }
    return S_OK;
}

HRESULT WeatherGpuResources::AppendIcon(WeatherDrawList& list, float x, float y, float size, wchar_t glyph, float red,
                                        float green, float blue, float alpha) noexcept
{
    return AppendIconFit(list, x, y, size, size, glyph, red, green, blue, alpha);
}

HRESULT WeatherGpuResources::AppendIconFit(WeatherDrawList& list, float x, float y, float width, float height,
                                           wchar_t glyph, float red, float green, float blue, float alpha) noexcept
{
    if (width <= 0.0f || height <= 0.0f || alpha <= 0.0f)
    {
        return S_OK;
    }
    uint32_t slot = FindGlyph(glyph);
    if (slot == kMissingGlyph)
    {
        return S_FALSE;
    }
    slot = ResolveLargeSlot(slot, std::max(width, height));
    const auto& ink = _glyphInk[slot];
    if (ink.width <= 0.0f || ink.height <= 0.0f)
    {
        return S_FALSE;
    }
    const float inverseAtlas = 1.0f / static_cast<float>(kWeatherAtlasSize);
    const float cellPx = static_cast<float>(_glyphCell[slot] != 0 ? _glyphCell[slot] : kWeatherGlyphCell);
    const float u0 = (static_cast<float>(_glyphAtlasX[slot]) + ink.left * cellPx) * inverseAtlas;
    const float v0 = (static_cast<float>(_glyphAtlasY[slot]) + ink.top * cellPx) * inverseAtlas;
    const float scale = std::min(width / ink.width, height / ink.height);
    const float drawnW = ink.width * scale;
    const float drawnH = ink.height * scale;
    (void)list.Add(WeatherQuadKindGlyph, x + (width - drawnW) * 0.5f, y + (height - drawnH) * 0.5f, drawnW, drawnH, red,
                   green, blue, alpha, 0.0f, 0.0f, u0, v0, u0 + ink.width * cellPx * inverseAtlas,
                   v0 + ink.height * cellPx * inverseAtlas);
    return S_OK;
}

void WeatherGpuResources::UploadAtlas() noexcept
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
    context->UpdateSubresource(_atlas.get(), 0, nullptr, _atlasPixels.data(), kWeatherAtlasSize, 0);
    _atlasDirty = false;
}

HRESULT WeatherGpuResources::Render(ID3D11DeviceContext* context, float width, float height,
                                    const WeatherDrawList& list) noexcept
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
    auto* constants = static_cast<WeatherConstants*>(mapped.pData);
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
    std::memcpy(mapped.pData, list.Data(), sizeof(WeatherQuadInstance) * list.Count());
    context->Unmap(_instanceBuffer.get(), 0);
    g_mapCount.fetch_add(1, std::memory_order_relaxed);

    const UINT stride = sizeof(WeatherQuadInstance);
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

HRESULT WeatherGpuAcquire(ID3D11Device* device) noexcept
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

void WeatherGpuRelease() noexcept
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

uint32_t WeatherGpuHeroTwinCount() noexcept
{
    return g_heroTwinCount.load(std::memory_order_relaxed);
}

uint32_t WeatherGpuIconCellCount() noexcept
{
    return g_iconCellCount.load(std::memory_order_relaxed);
}

WeatherGpuResources* WeatherGpuGet() noexcept
{
    return g_users > 0 ? &g_resources : nullptr;
}

void WeatherGpuLock() noexcept
{
    AcquireSRWLockExclusive(&g_gpuLock);
}

void WeatherGpuUnlock() noexcept
{
    ReleaseSRWLockExclusive(&g_gpuLock);
}
