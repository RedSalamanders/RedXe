#include "GlyphIcon.h"

#include <algorithm>
#include <dwrite.h>
#include <memory>
#include <new>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace RedXeActions
{
namespace
{
[[nodiscard]] HRESULT CreateFace(IDWriteFactory* factory, const wchar_t* family, IDWriteFontFace** face) noexcept
{
    *face = nullptr;
    wil::com_ptr_nothrow<IDWriteFontCollection> collection;
    HRESULT result = factory->GetSystemFontCollection(collection.put(), FALSE);
    if (FAILED(result))
    {
        return result;
    }
    UINT32 index = 0;
    BOOL exists = FALSE;
    result = collection->FindFamilyName(family, &index, &exists);
    if (FAILED(result) || !exists)
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    wil::com_ptr_nothrow<IDWriteFontFamily> fontFamily;
    result = collection->GetFontFamily(index, fontFamily.put());
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IDWriteFont> font;
    result = fontFamily->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                              DWRITE_FONT_STYLE_NORMAL, font.put());
    if (FAILED(result))
    {
        return result;
    }
    return font->CreateFontFace(face);
}
} // namespace

HRESULT RasterizeFluentGlyph(wchar_t glyph, uint32_t edge, uint32_t* bgra) noexcept
{
    if (glyph == 0 || edge == 0 || edge > 512 || !bgra)
    {
        return E_INVALIDARG;
    }
    wil::com_ptr_nothrow<IDWriteFactory> factory;
    HRESULT result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory),
                                         reinterpret_cast<IUnknown**>(factory.put()));
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IDWriteFontFace> face;
    result = CreateFace(factory.get(), L"Segoe Fluent Icons", face.put());
    if (FAILED(result))
    {
        result = CreateFace(factory.get(), L"Segoe MDL2 Assets", face.put());
    }
    if (FAILED(result))
    {
        return result;
    }
    const UINT32 codePoint = glyph;
    UINT16 glyphIndex = 0;
    result = face->GetGlyphIndicesW(&codePoint, 1, &glyphIndex);
    if (FAILED(result))
    {
        return result;
    }
    if (glyphIndex == 0)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    // The glyph fills 70% of the edge; Fluent glyphs are square in their em box, so centering on the em is centering
    // on the ink.
    const float emSize = static_cast<float>(edge) * 0.7f;
    DWRITE_FONT_METRICS metrics{};
    face->GetMetrics(&metrics);
    const float scale = emSize / static_cast<float>(metrics.designUnitsPerEm);
    DWRITE_GLYPH_METRICS glyphMetrics{};
    result = face->GetDesignGlyphMetrics(&glyphIndex, 1, &glyphMetrics, FALSE);
    if (FAILED(result))
    {
        return result;
    }
    const float advance = static_cast<float>(glyphMetrics.advanceWidth) * scale;
    const float inkTop = static_cast<float>(glyphMetrics.verticalOriginY - glyphMetrics.topSideBearing) * scale;
    const float inkHeight = static_cast<float>(static_cast<int>(glyphMetrics.advanceHeight) -
                                               glyphMetrics.topSideBearing - glyphMetrics.bottomSideBearing) *
                            scale;
    const float originX = (static_cast<float>(edge) - advance) * 0.5f;
    const float baselineY = (static_cast<float>(edge) - inkHeight) * 0.5f + inkTop;
    DWRITE_GLYPH_RUN run{};
    run.fontFace = face.get();
    run.fontEmSize = emSize;
    run.glyphCount = 1;
    run.glyphIndices = &glyphIndex;
    run.glyphAdvances = &advance;
    wil::com_ptr_nothrow<IDWriteGlyphRunAnalysis> analysis;
    result = factory->CreateGlyphRunAnalysis(&run, 1.0f, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                             DWRITE_MEASURING_MODE_NATURAL, originX, baselineY, analysis.put());
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
    std::fill_n(bgra, static_cast<size_t>(edge) * edge, 0U);
    bounds.left = std::max(bounds.left, 0L);
    bounds.top = std::max(bounds.top, 0L);
    bounds.right = std::min(bounds.right, static_cast<LONG>(edge));
    bounds.bottom = std::min(bounds.bottom, static_cast<LONG>(edge));
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
    {
        return S_OK;
    }
    const uint32_t width = static_cast<uint32_t>(bounds.right - bounds.left);
    const uint32_t height = static_cast<uint32_t>(bounds.bottom - bounds.top);
    const size_t required = static_cast<size_t>(width) * height * 3U;
    std::unique_ptr<uint8_t[]> coverage(new (std::nothrow) uint8_t[required]);
    if (!coverage)
    {
        return E_OUTOFMEMORY;
    }
    result = analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, coverage.get(),
                                          static_cast<UINT32>(required));
    if (FAILED(result))
    {
        return result;
    }
    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            const size_t source = (static_cast<size_t>(y) * width + x) * 3U;
            const uint32_t alpha =
                (static_cast<uint32_t>(coverage[source]) + coverage[source + 1U] + coverage[source + 2U] + 1U) / 3U;
            const size_t destination =
                (static_cast<size_t>(bounds.top) + y) * edge + static_cast<size_t>(bounds.left) + x;
            // Premultiplied white ink.
            bgra[destination] = (alpha << 24U) | (alpha << 16U) | (alpha << 8U) | alpha;
        }
    }
    return S_OK;
}
} // namespace RedXeActions
