# Third-party notices for the 5H4D3R5 plugin

Twelve of the shaders under `Shaders/` are HLSL ports of works published on [Shadertoy](https://www.shadertoy.com).
RedXe adapted them to Direct3D 11 (GLSL to HLSL syntax, the RedXe uniform block, the Shadertoy coordinate
convention, and a fade toward the dashboard background); the pictures and the algorithms are the authors'. Each
port keeps its author's original header and license text verbatim at the top of the file. The thirteenth,
`CosmicOrb.hlsl` (Psychedelic Cosmic Orb), is RedXe's own demo shader, written in HLSL for this plugin; it is not a
Shadertoy work and is under the repository's terms like the rest of the plugin.

Shadertoy's [terms](https://www.shadertoy.com/terms) state that a shader whose author placed no license of their
own on it is protected by Shadertoy's default license, the
[Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License](https://creativecommons.org/licenses/by-nc-sa/3.0/)
(CC BY-NC-SA 3.0). Four of the twelve authors state that same license in their headers; the other eight state none,
so the default applies. **Every Shadertoy port in this catalog is therefore CC BY-NC-SA 3.0**: attribution is required,
commercial use is not permitted, and adaptations (including these ports) must be shared under the same license.
This affects only the twelve ports; the plugin's own C++, `CosmicOrb.hlsl`, and the framework HLSL
(`ShadersCommon.hlsli`, `ShadersEntry.hlsli`, `ShadersVertex.hlsl`, `ShadersBlitPixel.hlsl`) are RedXe's.

| Port | Title | Author | Source | License |
| --- | --- | --- | --- | --- |
| `FluidSolverImage.hlsl`, `FluidSolverBufferA.hlsl` | Fluid solver | David A Roberts (davidar), 2017 | https://www.shadertoy.com/view/XlsBDf | CC BY-NC-SA 3.0 (Shadertoy default; no license stated). Simplex noise credited by the author to https://www.shadertoy.com/view/XsX3zB |
| `CineShaderLava.hlsl` | CineShader Lava | Edan Kwan (edankwan), 2019 | https://www.shadertoy.com/view/3sySRK | CC BY-NC-SA 3.0 (Shadertoy default; no license stated) |
| `SynthwaveSunset.hlsl` | another synthwave sunset thing | stduhpf, 2020 | https://www.shadertoy.com/view/tsScRK | CC BY-NC-SA 3.0 (Shadertoy default; no license stated) |
| `Seascape.hlsl` | Seascape | Alexander Alekseev aka TDM, 2014 | https://www.shadertoy.com/view/Ms2SD1 | CC BY-NC-SA 3.0, stated in the header (contact tdmaav@gmail.com) |
| `BaseWarpFbm.hlsl` | Base warp fBM | trinketMage, 2019 | https://www.shadertoy.com/view/tdG3Rd | CC BY-NC-SA 3.0 (Shadertoy default; no license stated). Warp technique from Inigo Quilez's article; `transform_rose` colormap from colormap-shaders |
| `FractalPyramid.hlsl` | fractal pyramid | bradjamesgrant, 2020 | https://www.shadertoy.com/view/tsXBzS | CC BY-NC-SA 3.0 (Shadertoy default; no license stated) |
| `Octagrams.hlsl` | Octagrams | whisky_shusuky, 2020 | https://www.shadertoy.com/view/tlVGDt | CC BY-NC-SA 3.0 (Shadertoy default; no license stated) |
| `Heartfelt.hlsl` | Heartfelt | Martijn Steinrucken aka BigWings, 2017 | https://www.shadertoy.com/view/ltffzl | CC BY-NC-SA 3.0, stated in the header. The Shadertoy stock photograph on iChannel0 is not redistributed; RedXe generates its own night-street background |
| `ProteanClouds.hlsl` | Protean clouds | nimitz (@stormoid), 2019 | https://www.shadertoy.com/view/3l23Rh | CC BY-NC-SA 3.0, stated in the header ("Contact the author for other licensing options") |
| `TheDriveHome.hlsl` | The Drive Home | Martijn Steinrucken aka BigWings, 2017 | https://www.shadertoy.com/view/MdfBRX | CC BY-NC-SA 3.0, stated in the header |
| `FlammesVortexImage.hlsl`, `FlammesVortexBufferA.hlsl`, `FlammesVortexCommon.hlsli` | Flammes 3 - Vortex | athibaul, 2020 | https://www.shadertoy.com/view/WsccDH | CC BY-NC-SA 3.0 (Shadertoy default; no license stated). Hash functions credited by the author to Dave Hoskins, https://www.shadertoy.com/view/4djSRW |
| `NeonPulseFractal.hlsl` | Neon Pulse Fractal | bogdoslav, 2026 | https://www.shadertoy.com/view/7csXD4 | CC BY-NC-SA 3.0 (Shadertoy default; no license stated) |
| `CosmicOrb.hlsl` | Psychedelic Cosmic Orb | RedXe, 2026 | this repository | RedXe repository terms (not a Shadertoy work) |

Sources were retrieved from shadertoy.com on 2026-09-19. Music and sound channels of the originals are not
reproduced; they do not affect the pictures.
