# 5H4D3R5 bundled plugin

`5H4D3R5.dll` draws one of the bundled full-screen shaders and demos, or a fading slideshow of them. The catalog is
thirteen entries today: twelve ports of Shadertoy works and RedXe's own Cosmic Orb. The catalog, the settings model,
the ranges, and the defaults live once in `ShadersSettings.h`, which the host parser (`RedXe/Settings.cpp`,
`RedXe/SettingsV4.cpp`) and the DLL both compile. `LICENSES.md` carries the attribution and license of every entry;
`docs/plugins/5h4d3r5.md` is the user page.

## Shaders

Every shader under `Shaders/` is a Shader Model 5.0 pixel shader with a Shadertoy-style `mainImage(out vec4, in
vec2 fragCoord)` entry, so a Shadertoy work ports with its author's header and its code as close to the GLSL as
HLSL allows, and an original written in HLSL (`CosmicOrb.hlsl`) only needs the `mainImage` adapter that feeds it
`fragCoord / iResolution.xy` and `iTime`. To add one: drop the `.hlsl` under `Shaders/` with the two includes, add
its `FxCompile` item to `5H4D3R5.vcxproj`, its `ShaderInfo` row to `kShaders` and its name to the schema enum in
`ShadersSettings.h`, its blob to `kPrograms` in `Shaders.cpp`, its row to `LICENSES.md`, the same name to
`Specs/Settings.schema.json`, and its thumbnail row to the user page.

- `ShadersCommon.hlsli` supplies the Shadertoy uniform block (`iResolution`, `iTime`, `iTimeDelta`, `iFrame`,
  `iMouse`, `iDate`), `iChannel0` with its sampler, the GLSL names HLSL lacks (`vec2`/`vec3`/`vec4`, `fract`, `mix`,
  `mod` with GLSL sign semantics, `inversesqrt`), and `texture()`/`textureLod()` that flip V so channels keep
  Shadertoy's bottom-left origin.
- `ShadersEntry.hlsli` is the pixel entry point: it turns `SV_Position` into a bottom-left `fragCoord` of the pass,
  calls the port's `mainImage`, and (image passes) clamps and fades the result toward the dashboard background. A
  feedback-buffer pass defines `SHADERS_BUFFER_PASS` and stores the raw value.
- GLSL row-vector products became `mul(matrix, vector)` with the same coefficient list, `atan(y, x)` became
  `atan2`, scalar-broadcast constructors were spelled out, and long marches carry `[loop]`. Two ports (Fluid solver,
  Flammes 3) have a "Buffer A" feedback pass; Heartfelt samples a photograph on Shadertoy, replaced here by a
  procedural night-street texture the plugin builds once per device.
- Two ports carry a marked RedXe adjustment: Seascape clamps its final `pow()` base (a trough can push a channel
  below zero, and the resulting NaN showed as pink on hardware), and Protean clouds anchors its mouse-free camera
  framing to Shadertoy's 16:9 canvas (on a 32:9 tile the source's canvas-relative offsets left the clouds). The
  entry point also maps any NaN to black, since some GPUs store a NaN as 1.0, which is why the shaders compile with
  `/Gis`.

To re-check a port against fxc without a full build:

```powershell
& "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe" /T ps_5_0 /E PixelMain /Ges /Gis /O3 /WX Plugins\5H4D3R5\Shaders\Seascape.hlsl
```

## Rendering

The widget is a continuous-animation GPU widget. Per frame it draws at most three full-screen triangles: the
feedback pass (only for the two multi-pass ports, once per presented frame at the buffer's own size), the image
pass, and, when `renderScalePercent` is below 100, a blit that stretches the offscreen image into the tile. Shader
objects, samplers, pipeline states, and the Heartfelt background are provider-owned and shared by every widget of
that provider; the constant buffer, the offscreen texture, and the two `R32G32B32A32_FLOAT` feedback buffers are
per widget and sized in `OnTargetSizeChanged`. `Render` allocates nothing and performs one to three constant-buffer
uploads. The DLL performs no runtime shader compilation and loads no font, WIC, or DirectWrite component.

A slideshow advances every `intervalSeconds` and fades through the dashboard background for 0.75 s on either side
of the change; `iTime` and `iFrame` restart with each slide. A single or random shader restarts the same way once
an hour so the 32-bit `iTime` keeps sub-frame resolution. A click or tap on a slideshow tile skips ahead: the widget
keeps a clock offset (double) that the tap pushes to 0.75 s before the next cycle boundary, so the ordinary
fade-out/fade-in plays and every derived value (cycle, entry, `iTime`, `iFrame`) stays a pure function of that
clock. The Up is consumed, which by the host contract removes double-click raise from a slideshow tile; other modes
leave clicks to the host.
