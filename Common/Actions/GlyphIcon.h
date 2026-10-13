#pragma once

// Rasterizes one Segoe Fluent Icons glyph into a square BGRA bitmap (white ink, transparent background) so a
// binding whose `icon` is a glyph name can fill the same texture slot a shell icon would. DirectWrite is created
// per extraction batch; callers run it at extraction time, never in Render. The one-glyph wrapper keeps its
// per-call lifetime. No font resources are retained between batches.

#include <cstddef>
#include <cstdint>
#include <dwrite.h>
#include <memory>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace RedXeActions
{
// One caller/thread, one extraction batch. Factory/face creation is lazy and attempted once; coverage grows only
// to the largest clipped glyph in this batch (at most 512*512*3 bytes). All owners release at scope exit.
class FluentGlyphRasterizer final
{
  public:
    FluentGlyphRasterizer() noexcept = default;
    FluentGlyphRasterizer(const FluentGlyphRasterizer&) = delete;
    FluentGlyphRasterizer& operator=(const FluentGlyphRasterizer&) = delete;
    [[nodiscard]] HRESULT Rasterize(wchar_t glyph, uint32_t edge, uint32_t* bgra) noexcept;

  private:
    [[nodiscard]] HRESULT EnsureFace() noexcept;
    wil::com_ptr_nothrow<IDWriteFactory> _factory;
    wil::com_ptr_nothrow<IDWriteFontFace> _face;
    std::unique_ptr<uint8_t[]> _coverage;
    size_t _coverageCapacity = 0;
    HRESULT _fontResult = E_UNEXPECTED;
    bool _fontAttempted = false;
};

// Draws `glyph` centered in an edge×edge premultiplied BGRA square. Falls back from Segoe Fluent Icons to Segoe
// MDL2 Assets; ERROR_NOT_FOUND when neither family exists. bgra must hold edge*edge pixels.
[[nodiscard]] HRESULT RasterizeFluentGlyph(wchar_t glyph, uint32_t edge, uint32_t* bgra) noexcept;
} // namespace RedXeActions
