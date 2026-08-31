# Done: Matrix Rain Direct3D plugin

Status: `COMPLETE`
Created: 2026-08-31
Completed: 2026-08-31
Owner: RedXe plugin, settings, dashboard, and Direct3D rendering

## Purpose

Define the first production-oriented bundled RedXe plugin: a low-resource Direct3D 11 Matrix-style digital-rain
animation for the 2560x720 CORSAIR XENEON EDGE display.

This document is intentionally ready for product and architecture review before implementation. Nothing in this WIP
changes current normative behavior. After approval and implementation, durable requirements MUST move into:

- `Specs/Plugins/Plugins_API.md`;
- `Specs/Core/Core_Settings.md` and `Specs/Settings.schema.json`;
- `Specs/Core/Core_PerformanceAndResources.md` only if a new cross-cutting rule is required;
- the Debug and Release templates under `Settings/`.

## Proposed outcome

- Add `Plugins/MatrixRain/MatrixRain.dll`.
- Advertise plugin ID `builtin.matrix-rain` and widget type ID `matrix-rain`.
- Implement `IRedXeWidget` and the sibling `IRedXeGpuWidget : IUnknown` interface through `QueryInterface`.
- Render a single full-dashboard Matrix Rain instance by default in Release.
- Keep the Rotating Triangle and GDI Orbit plugins as Debug fixtures rather than Release content.
- Apply Matrix Rain settings through the existing build-specific JSON store and live file watcher.
- Recreate the Matrix Rain provider and widget transactionally when its settings change; do not add a configuration
  COM interface for this first version.
- Preserve the mandatory low-CPU, low-memory, allocation-free frame-path policy.

## Inspiration and clean-room boundary

The following projects are visual and product references only:

- [DigitalChewie/ModernMatrixScreensaver](https://github.com/DigitalChewie/ModernMatrixScreensaver) demonstrates a
  Windows Direct3D 11 rain renderer with bright leaders, fading trails, glyph mutation, embedded shaders, density and
  speed controls, and optional bloom.
- [relmer/MatrixRain](https://github.com/relmer/MatrixRain) demonstrates a Win32/DirectX rain application with live
  density, speed, glow, and color controls plus explicit performance-quality choices.

RedXe MUST implement its own simulation, shaders, glyph design, assets, settings names, tests, and plugin integration.
No source, shader, image, font, or other asset is copied from either reference. The first RedXe version deliberately
does not reproduce their screensaver shell, registry settings, multi-monitor host, camera, fog, HDR, scanlines,
wireframe mode, debug overlays, or multipass Gaussian bloom.

## Product behavior

### Visual contract

The widget MUST present a dark field of vertical digital-glyph streams with all of these characteristics:

1. Streams fall from top to bottom at independently varied speeds.
2. Each stream has a bright near-white head followed by a saturated colored trail.
3. Trail opacity falls smoothly with distance from the head; disappearing tails MUST NOT end as hard rectangles.
4. Glyphs mutate at a controlled rate that is visibly slower than the display refresh rate.
5. Column start phase, speed, trail length, and glyph sequence vary deterministically from the configured seed.
6. The pattern MUST avoid synchronized restarts or an obvious short repeating cycle.
7. Scaling and clipping MUST remain correct for any host-owned widget viewport and DPI.
8. The classic default palette is a nearly black green-tinted background, green trails, and pale green-white heads.
9. Glow MUST be a bounded single-pass glyph effect, not a full-frame blur or bloom chain.

The first version uses an original RedXe digital glyph set. It MUST NOT claim to reproduce the exact film glyphs.

### Dashboard placement

- Release defaults to one Matrix Rain instance named `matrix-rain.1` filling the dashboard design canvas.
- Debug defaults keep two Rotating Triangle instances and one GDI Orbit instance alongside one Matrix Rain instance so
  all three bundled plugin examples and both rendering mechanisms remain visible during development.
- Placement remains host-owned. The Matrix Rain plugin receives only its physical viewport and DPI.
- The first version supports at most one Matrix Rain instance. Per-instance customization is deferred until the
  generalized dashboard composition model is approved.

### Animation scheduling

- The widget descriptor MUST set `RedXeWidgetFlagContinuousAnimation`.
- The plugin MUST NOT create a timer, worker thread, hidden window, or message-only window.
- Animation time comes only from `RedXeWidgetFrameContext`.
- Hidden, minimized, suspended, and occluded blocking remains host-owned and MUST continue to satisfy
  `Core_PerformanceAndResources.md`.

## Plugin and ABI contract

### Identity

| Field | Proposed value |
| --- | --- |
| DLL | `MatrixRain.dll` |
| Plugin ID | `builtin.matrix-rain` |
| Plugin display name | `Matrix Rain` |
| Widget type ID | `matrix-rain` |
| Default instance ID | `matrix-rain.1` |
| Capability | `RedXePluginCapabilityWidgetProvider` |
| Rendering interface | `IRedXeGpuWidget` |

### Interface rules

- No Matrix-specific type, glyph, color, stream, or shader declaration belongs in `Common/PlugInterfaces/`.
- `IRedXeWidget`, `IRedXeWidgetProvider`, and `IRedXeGpuWidget` remain independent COM interfaces declared with the
  `interface` keyword and derived directly from `IUnknown`.
- Matrix Rain exposes `IRedXeWidget` and `IRedXeGpuWidget` as sibling interfaces through `QueryInterface`.
- The host MUST NOT cast between sibling interface pointers.
- The plugin receives the borrowed D3D11 device at device creation and the borrowed immediate context at render time.
- The plugin never receives or queries for the HWND, swap chain, back buffer, render-target view, or presentation
  control.
- This plugin requires no new rendering IID and MUST NOT grow a published vtable.

### Configuration delivery

The proposed first version appends an optional configuration payload to the existing extensible factory options:

```cpp
struct RedXeFactoryOptions final
{
    std::uint32_t sizeBytes;
    std::uint32_t debugLevel;
    const char* configurationJsonUtf8;
    std::uint32_t configurationBytes;
};
```

The exact ABI prefix constant and padding assertions are fixed during implementation for both x64 and ARM64. The
existing eight-byte `kRedXeFactoryOptionsV1Size` remains immutable.

Rules:

- A plugin compiled against v1 ignores the larger tail.
- A host using v1 options gives Matrix Rain its compiled defaults.
- `configurationJsonUtf8` is borrowed only for the `RedXeCreate` call.
- `configurationBytes` excludes any terminator and is capped at 4096 bytes.
- A null pointer with zero bytes selects defaults; every other pointer/length mismatch returns `E_INVALIDARG`.
- The host passes only the validated `dashboard.matrixRain.settings` object, not the full RedXe settings document.
- Matrix Rain parses and copies the payload before returning from `RedXeCreate`; it retains no borrowed string.
- Invalid configuration fails provider creation without changing the active dashboard.
- The payload is cold-path work. It MUST NOT be parsed, serialized, or inspected during rendering.

This avoids a new `IRedXeConfigurable` IID for a single global configuration. If later dashboard work requires
different settings per instance without recreation, that need receives a separate WIP and a new sibling interface
derived directly from `IUnknown`.

## Settings proposal

### Schema version

The change requires settings `schemaVersion` 2 because the current version 1 contract rejects unknown members.
Version 2 also allows `dashboard.rotatingTriangleInstances` to be zero so the Release dashboard can disable the demo
plugin.

The complete proposed Release template is:

```json
{
  "$schema": "RedXe.settings.schema.json",
  "schemaVersion": 2,
  "dashboard": {
    "rotatingTriangleInstances": 0,
    "gdiOrbitEnabled": false,
    "matrixRain": {
      "enabled": true,
      "settings": {
        "seed": 1999,
        "glyphHeightDips": 18,
        "densityPercent": 70,
        "speedPercent": 100,
        "trailLengthGlyphs": 18,
        "mutationPerSecond": 8,
        "headColor": "#D8FFE5",
        "trailColor": "#00E65C",
        "backgroundColor": "#010502",
        "glowPercent": 35
      }
    }
  }
}
```

The Debug template uses the same Matrix Rain settings with these dashboard overrides:

| Setting | Debug | Release |
| --- | ---: | ---: |
| `rotatingTriangleInstances` | 2 | 0 |
| `gdiOrbitEnabled` | `true` | `false` |
| `matrixRain.enabled` | `true` | `true` |

### Matrix Rain settings

| Member | Type | Range or format | Default | Effect |
| --- | --- | --- | ---: | --- |
| `seed` | unsigned integer | 0 through 4294967295 | 1999 | Deterministic stream layout and glyph sequence |
| `glyphHeightDips` | integer | 12 through 48 | 18 | Cell height before DPI conversion |
| `densityPercent` | integer | 10 through 100 | 70 | Active column fraction and therefore GPU work |
| `speedPercent` | integer | 25 through 300 | 100 | Base fall speed |
| `trailLengthGlyphs` | integer | 6 through 48 | 18 | Mean visible trail length |
| `mutationPerSecond` | integer | 0 through 30 | 8 | Glyph-change cadence |
| `headColor` | string | `#RRGGBB` | `#D8FFE5` | Stream-head color |
| `trailColor` | string | `#RRGGBB` | `#00E65C` | Trail color |
| `backgroundColor` | string | `#RRGGBB` | `#010502` | Opaque widget background |
| `glowPercent` | integer | 0 through 100 | 35 | Single-pass SDF halo strength |

Settings are intentionally compact. Encoding choices, direction, camera, fog, HDR, scanlines, frame caps, debug
statistics, and multipass bloom are not version 1 Matrix Rain settings.

### Validation and live reload

- Members are required and unknown or duplicate members are rejected.
- Colors are exactly seven ASCII characters, start with `#`, and use case-insensitive hexadecimal digits. The plugin
  stores normalized numeric colors rather than retaining the source strings.
- The host validates the full document against the C++ parser and `Specs/Settings.schema.json` before applying it.
- On a valid change, the UI thread creates a new Matrix Rain provider/widget with the proposed factory-options tail,
  swaps the dashboard transactionally, and then releases the old widget/provider resources.
- A failed create, parse, device-resource rebuild, or dashboard swap keeps the previous settings and animation active.
- Reload does not unload or remap `MatrixRain.dll`.
- Settings edits continue to use the existing event-blocked directory watcher; Matrix Rain adds no polling.

## Direct3D rendering design

### Glyph asset

- Use an original RedXe single-channel signed-distance-field glyph atlas.
- Store the atlas as a small embedded immutable asset; no loose runtime file is required.
- Target at most 256x256 `R8_UNORM`, including all glyphs.
- Generate or author the atlas offline. Do not load DirectWrite, WIC, or a font file at runtime.
- Record the atlas-generation source and regeneration command in `Plugins/MatrixRain/README.md`.

### Shaders and geometry

- Compile Shader Model 5.0 HLSL at build time through the Visual Studio project and embed stripped bytecode.
- Do not link or load `d3dcompiler_47.dll` at runtime.
- Generate the fullscreen background triangle from `SV_VertexID`.
- Generate glyph quads from `SV_VertexID` and `SV_InstanceID`; use no vertex or index buffer.
- Derive column position, stream phase, speed variation, trail variation, and glyph index with deterministic integer
  hashes of the seed, active-column index, row, and a quantized mutation tick.
- Map active columns onto screen columns with a deterministic permutation so decreasing density decreases both visible
  columns and submitted glyph instances without a CPU-side column array.
- Use atlas distance plus configured colors to calculate crisp coverage, fading trail intensity, the bright head, and
  a bounded halo in one glyph pixel shader.

### Frame submission

Each visible frame performs:

1. one write to a fixed-size dynamic constant buffer;
2. one opaque fullscreen-background draw;
3. one instanced glyph draw with alpha blending.

Grid dimensions are derived from the cached viewport, DPI, and `glyphHeightDips`. They are recalculated only when the
viewport, DPI, or settings change. The submitted glyph count is bounded at 65,536. If an unusual viewport and minimum
glyph size would exceed the bound, the plugin reduces active columns deterministically rather than allocating or
failing the frame.

The renderer MUST explicitly bind every D3D state it depends on. Host quarantine and render-target rebinding between
widgets remain unchanged.

### Device lifecycle

- Provider-owned device resources: vertex shader, pixel shader, input-free layout state, blend/rasterizer/depth
  states, sampler, atlas texture/SRV, and one dynamic constant buffer.
- Widget-owned state: immutable validated settings and deterministic identity only.
- `OnDeviceCreated` creates the complete device-resource set transactionally.
- `OnDeviceLost` releases all device resources idempotently.
- Partial creation failure releases temporary resources and leaves the provider device-lost.
- A later `OnDeviceCreated` call rebuilds the complete set without recreating the COM objects.

## Mandatory performance and resource budget

The following budgets are part of acceptance, not optional optimization work:

| Area | Budget |
| --- | --- |
| Frame-path heap work | Zero allocations and zero frees |
| Frame-path synchronization | No mutex, event, wait, or worker handoff |
| Frame-path I/O | None |
| CPU simulation | No per-column or per-glyph update loop |
| Constant upload | At most one map/unmap and 256 bytes per instance per frame |
| Draw submissions | At most two draw calls per instance per frame |
| Glyph instances | At most 65,536 |
| Texture uploads | None after device creation |
| Runtime shader/font work | No shader compilation, DirectWrite, WIC decode, or font loading |
| Embedded glyph payload | At most 128 KiB |
| Plugin CPU state | At most 64 KiB per provider and 4 KiB per widget, excluding COM/runtime overhead |
| Threads and timers | None |

The implementation MUST measure and record CPU submission time, GPU frame time, process private bytes, working set,
and steady-state allocation counts at 2560x720 with default Release settings. Measurements are recorded in the WIP
before closeout and compared with a disabled-plugin baseline. Any missed budget blocks closeout or requires an
explicitly approved revision to this plan.

## Error behavior

- Missing or invalid embedded shader/atlas data fails provider device initialization.
- A Matrix Rain failure MUST NOT corrupt the host renderer or prevent rollback to the previous live dashboard.
- Shader or resource creation failures return the original failing `HRESULT`.
- The plugin MUST tolerate a zero-sized or suspended viewport without submitting work.
- Invalid frame-context prefixes, null borrowed pointers, invalid viewport dimensions, NaN timing, or unsupported
  device capabilities fail safely without dereferencing invalid data.
- The first implementation supports the same D3D11 feature-level floor already required by RedXe and the WARP smoke
  path.

## Required validation

### Contract and settings tests

- Factory creation succeeds with v1 options and compiled defaults.
- Factory creation succeeds with the configuration tail and returns the configured provider.
- Null, mismatched, oversized, malformed, duplicate, unknown, and out-of-range configuration values fail correctly.
- All `QueryInterface` paths return identity-consistent sibling interfaces and obey `IUnknown` reference counting.
- Settings schema version 2 accepts both shipped templates and rejects version 1/2 hybrids.
- `rotatingTriangleInstances == 0` prevents `RotatingTriangle.dll` from loading.
- `matrixRain.enabled == false` prevents `MatrixRain.dll` from loading.
- A valid settings edit changes deterministic rendered output without process restart; an invalid edit preserves the
  previous live instance.

### Rendering tests

- WARP creates all resources, renders at least two different animation times, and presents without D3D debug-layer
  errors.
- A staging readback proves the rendered viewport contains the configured background and non-background glyph pixels.
- Identical seed, settings, viewport, DPI, and time produce identical pixels within the test path.
- Different seeds or animation times produce different glyph output.
- Minimum and maximum setting values remain within the glyph-instance and resource budgets.
- Device loss followed by recreation produces a valid frame and no retained old-device objects.
- Release imports contain no runtime HLSL compiler, DirectWrite, or WIC dependency from `MatrixRain.dll`.

### Build and automated host acceptance

- Debug x64, Release x64, and Release ARM64 build cleanly with warnings as errors.
- `test.ps1` and `validate-skills.ps1` pass.
- The hidden WARP smoke test remains green with Matrix Rain enabled in the deployed template used by that build.
- `HostPluginTests` uses a hidden off-screen 2560x720 HWND and WARP to drive the production plugin manager, dashboard,
  and renderer. Release contains one full-canvas Matrix widget; Debug contains the two triangle, GDI Orbit, and Matrix
  fixtures. Suspend/restore, settings replacement, rollback, and teardown are asserted without desktop automation.
- The scheduler decision harness proves that hidden, minimized/suspended, display-off, and occluded states block on
  messages, that DXGI probing is notification-driven, and that static dashboards sleep after a clean frame.
- A five-minute Release host-harness run shows constant plugin object and D3D resource counts, records process private
  bytes and working-set deltas, and retains the allocation-free plugin frame-path result.

## Implementation sequence after approval

1. [x] Freeze the visual defaults, original glyph-set direction, settings members, and Release/Debug composition.
2. [x] Add the versioned factory-options tail and contract tests without changing existing interface IIDs.
3. [x] Add the strict settings version 2 parser, schema, deployed templates, and rollback tests.
4. [x] Add `Plugins/MatrixRain/` with its original atlas source, build-time shaders, provider, and GPU widget.
5. [x] Load Matrix Rain from `PluginManager` only when enabled and make the one-widget Release layout fill the dashboard.
6. [x] Add WARP pixel, determinism, settings-reload, device-loss, and import validation.
7. [x] Measure resource budgets, reconcile the architecture RFC, update normative specs, complete the required build
   matrix, and move this plan to `Specs/Plans/Done/`.

## Completion evidence

All validation was performed on 2026-08-31 without desktop automation.

- `./test.ps1 -Configuration Debug -Platform x64 -Rebuild`: passed the clean build, plugin contracts, settings
  contracts, production host/plugin harness, and hidden application WARP smoke test.
- `./test.ps1 -Configuration Release -Platform x64 -Rebuild`: passed the same clean validation matrix.
- `./build.ps1 -Configuration Release -Platform ARM64 -Rebuild`: compiled the host, three bundled plugins, settings
  tests, plugin contracts, and host/plugin harness. PE inspection identified the Matrix binaries as x64 and ARM64.
- `./validate-skills.ps1`: all 10 repository skills passed validation. `./format.ps1` and `git diff --check` passed.
- Release import inspection found no `d3dcompiler`, DirectWrite, or Windows Imaging Component import in
  `MatrixRain.dll`.
- Release `HostPluginTests` passed scheduler decisions; Release-only module loading; full-canvas placement; production
  WARP rendering/presentation; zero-size suspend and restore; valid reconfiguration; invalid-transaction rollback;
  mixed Debug GPU/native composition; native-widget visibility transitions; and exact teardown to zero Matrix
  providers, widgets, and device-resource sets.
- Release `PluginContractTests.exe --matrix-benchmark` at 2560x720 used the hardware D3D11 path. CPU submission was
  0.081 us/frame disabled and 0.799 us/frame enabled, a 0.718 us delta. D3D timestamp time was 0.0000 ms/frame
  disabled and 0.0091 ms/frame enabled. Private bytes were 69,144,576 disabled, 75,026,432 after enabling, and
  75,636,736 after steady measurement; working set was 36,941,824, 42,479,616, and 43,085,824 bytes respectively.
  The dedicated render allocation hook observed zero Matrix frame-path CRT allocations. Process-wide heap-walk deltas,
  which include the runtime and graphics driver, were -1 block/+65,278 bytes for the disabled measurement and
  +18 blocks/+236,451 bytes for the enabled measurement.
- Release `HostPluginTests.exe --matrix-soak` ran the production hidden host for 300 seconds and 9,331 presented
  frames. Provider/widget/device-resource counts stayed exactly 1/1/1, device-resource count returned to zero on
  shutdown, private bytes changed by -176,128, and working set changed by +626,688 bytes.

## Approved decisions

The user's implementation request on 2026-08-31 approved all three proposed decisions and the settings table:

1. **Release composition:** Matrix Rain is the only enabled Release widget and fills the dashboard.
2. **Debug composition:** Matrix Rain, two triangles, and GDI Orbit keep all rendering paths visible.
3. **Glyph direction:** an original RedXe abstract digital glyph set and embedded SDF atlas avoid font licensing,
   runtime font dependencies, and extra memory.

The user's follow-up on 2026-08-31 approved automated host/plugin validation modeled on the RedSalamander harness in
place of desktop control for this plan's closeout.
