# Matrix Rain bundled plugin

Matrix Rain uses an original RedXe abstract digital glyph set. It does not contain font data or assets from the
Matrix films or the clean-room inspiration projects cited by the product specification.

`MatrixRainGlyphAtlas.h` is a generated 192×384 single-channel signed-distance-field atlas containing 64 abstract
glyphs: each is two to four vector strokes (round-capped segments) chosen deterministically from a 16-stroke palette
in a 1×2 design cell and rasterized as an exact distance field into a 24×48 texel cell, so diagonals stay straight
at any glyph size. The pixel shader thresholds the field over about one screen pixel (`fwidth`), which keeps the
edges smooth from a 12-DIP tile glyph to a raised 48-DIP one. The uncompressed embedded payload is 72 KiB.
Regenerate it from the repository root with:

```powershell
.\Plugins\MatrixRain\GenerateGlyphAtlas.ps1
```

The four Shader Model 5.0 HLSL entry points are compiled and embedded by MSBuild. The DLL performs no runtime shader
compilation, font loading, DirectWrite initialization, or WIC decoding.
