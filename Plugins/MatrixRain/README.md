# Matrix Rain bundled plugin

Matrix Rain uses an original RedXe abstract digital glyph set. It does not contain font data or assets from the
Matrix films or the clean-room inspiration projects cited by the product specification.

`MatrixRainGlyphAtlas.h` is a generated 128×128 single-channel signed-distance-field atlas containing 64 abstract
glyphs. Its uncompressed embedded payload is 16 KiB. Regenerate it from the repository root with:

```powershell
.\Plugins\MatrixRain\GenerateGlyphAtlas.ps1
```

The four Shader Model 5.0 HLSL entry points are compiled and embedded by MSBuild. The DLL performs no runtime shader
compilation, font loading, DirectWrite initialization, or WIC decoding.
