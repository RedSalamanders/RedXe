#pragma once

#include <windows.h>

// Exercises the shared native rasterizer; an optional file records all named glyphs at each supported fixture edge.
[[nodiscard]] HRESULT ValidateLauncherGlyphRasterization(const wchar_t* pixelsPath = nullptr) noexcept;
