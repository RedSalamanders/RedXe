#pragma once

// Rasterizes one Segoe Fluent Icons glyph into a square BGRA bitmap (white ink, transparent background) so a
// binding whose `icon` is a glyph name can fill the same texture slot a shell icon would. DirectWrite is created
// per call; callers run it at extraction time, never in Render.

#include <cstdint>
#include <windows.h>

namespace RedXeActions
{
// Draws `glyph` centered in an edge×edge premultiplied BGRA square. Falls back from Segoe Fluent Icons to Segoe
// MDL2 Assets; ERROR_NOT_FOUND when neither family exists. bgra must hold edge*edge pixels.
[[nodiscard]] HRESULT RasterizeFluentGlyph(wchar_t glyph, uint32_t edge, uint32_t* bgra) noexcept;
} // namespace RedXeActions
