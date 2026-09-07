# Host chrome without GDI: Direct3D edge bands and raise overlay, no chrome HWNDs, no redirection surface

Status: `ACTIVE`
Created: 2026-09-07
Owner: `Application` chrome and input routing, `Renderer` host chrome pipeline, `DashboardHost` native containers,
shipped settings templates

## Goal

Remove GDI and child HWNDs from the shipped presentation path so every shipped page can present with independent
flip and DWM allocates no redirection surface for the main window:

1. The mouse edge bands and the raise overlay (dim, shadow, close control) are drawn by the renderer into the swap
   chain as a handful of quads with one small glyph atlas. The two layered chrome HWNDs, their window classes, GDI
   brushes, fonts, regions, and paint procedures go away.
2. GdiOrbit leaves the shipped templates. It stays bundled, settings-visible, documented, and covered by
   `HostPluginTests` as the native-window example, because `IRedXeWindowWidget` remains the opt-in path for future
   WebView, media, and native-control widgets.
3. Native-widget containers become `WS_EX_LAYERED` children, so each native widget carries its own DWM surface and
   can be dimmed with the same alpha as the Direct3D dim while another widget is raised.
4. The top-level window is created with `WS_EX_NOREDIRECTIONBITMAP`. Nothing paints the main window with GDI any
   more, so the 2560×720×4 DWM redirection surface and its per-frame copy in composed mode are gone.

This plan changes normative text that today forbids Direct3D chrome ("The host MUST NOT add Direct3D shaders for this
chrome") and mandates GDI chrome and chrome HWNDs. The justification is measured: a plain child HWND over the swap
chain forces composed presentation and a DWM copy per frame, the raise overlay adds a client-sized layered surface,
and the raise animation creates two to three GDI regions per frame (`RuntimeMemoryAndLatency_2026-09-07.md`).

Owning contracts changed at closeout: `Specs/UI/UI_Dashboard.md`, `Specs/Core/Core_PerformanceAndResources.md`,
`Specs/UI/UI_XeneonDisplayWindowing.md`, `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_Settings.md` (template
coverage exception), `AGENTS.md` (chrome iconography, template rule), `docs/plugins/gdi-orbit.md`.

## Design

### Host chrome pipeline (`RedXe/HostChrome.h`, `RedXe/HostChrome.cpp`, `RedXe/HostChrome*.hlsl`)

- `HostChromeState` is plain data owned by `Application` and pushed to `Renderer::SetHostChrome` whenever raise,
  dim, close hover, or band reveal changes; every change invalidates exactly one coalesced frame. No timer, no
  continuous frame, no HWND.
- `HostChromeResources` owns one vertex shader, one pixel shader, one 64-byte dynamic constant buffer, one blend
  state, one rasterizer state, one sampler, and one `R8_UNORM` glyph atlas (three glyphs: chevron left, chevron
  right, close) rasterized with DirectWrite at device creation and again only when the DPI changes. Fonts resolve
  through `FluentIcons::ResolveIconFamily` (Segoe Fluent Icons, then Segoe MDL2 Assets, then the Unicode fallback in
  Segoe UI); a missing DirectWrite face degrades to wash-only chrome and logs once.
- Each quad is one `Map(WRITE_DISCARD)` of the constant buffer and one `Draw(4, 0)`. A raised frame draws at most
  four dim strips around the content, one shadow, one close wash (hover), one close glyph; an edge reveal draws one
  wash and one chevron. Dim strips and shadow draw before the raised widget so plugin pixels stay undimmed; close
  and bands draw after it.
- Geometry stays pure and testable: `RaisedDimStrips` in `WidgetRaise.h` replaces `CreateRaisedOverlayRegion`;
  band and close rectangles are unchanged.

### Input

The top-level window already owns band hover and the double-activate paths. It additionally owns close hover
(`WM_MOUSEMOVE`, `WM_MOUSELEAVE`, hand cursor in `WM_SETCURSOR`), close click (`WM_LBUTTONUP`), and close tap
(`WM_POINTERUP`). Native containers keep posting `kPageEdgeHoverMessage`; the band navigate message is no longer needed
because the top-level window is the only click target.

### Native containers

Containers are created `WS_EX_LAYERED` with alpha 255 and receive `255 − dim` while another widget is raised, so a
native widget dims with the Direct3D dim and returns to full alpha on dismiss. A raised native widget covers the
close control; Escape and double-activate dismiss it, and the contract says so.

### Templates

`builtin.gdi-orbit` is removed from every shipped page and declaration. The Debug template keeps a commented example.
`SettingsTests` template coverage exempts plugins the catalog marks as opt-in native examples.

## Items

| ID | Item | Pass condition |
| --- | --- | --- |
| C1 | Host chrome pipeline and glyph atlas; `Renderer::SetHostChrome`, `LastFrameChromeQuadCount`, chrome drawn below and above the raised widget. | `HostPluginTests` `TestHostChromeComposition`: no chrome → 0 quads; raised half slice with close hover → dim strips + shadow + close wash + glyph; one revealed band → wash + chevron; atlas font kind reported; device loss and DPI change rebuild. |
| C2 | `Application` pushes chrome state; band HWNDs, overlay HWND, their classes, brushes, fonts, regions, paint procedures, and `kPageEdgeNavigateMessage` removed; close hover/click/tap and band click owned by the top-level window. | Self-test: no window of the old chrome classes can exist (classes gone); `Application.cpp` has no `BeginPaint`, `CreateSolidBrush`, `SetWindowRgn`, or `SetLayeredWindowAttributes` call. |
| C3 | `RaisedDimStrips` replaces `CreateRaisedOverlayRegion`; geometry tests updated. | `TestWidgetRaisePolicy`: half slice at the left edge → one strip, centred slice → two strips, full slice → none. |
| C4 | Native containers `WS_EX_LAYERED`, `DashboardHost::SetNativeDimAlpha`; raised native widget keeps HWND_TOP. Found while landing: Windows rejects a layered child (error 87) in a process without a Windows 8+ `supportedOS` manifest, so `HostPluginTests` now embeds `HostPluginTests.manifest`. | `TestWidgetRaiseNative` still passes; container ex-style includes `WS_EX_LAYERED`; the test binary embeds a compatibility manifest. |
| C5 | Top-level `WS_EX_NOREDIRECTIONBITMAP`; self-test verifies it and proves one layered child can be created (manifest guard). | Release live launch on the XENEON shows the dashboard (screen capture of the XENEON region is not black); `--self-test --warp` passes in Debug and Release. |
| C6 | GdiOrbit off the shipped templates; catalog exemption for opt-in native examples; docs and specs updated. | `SettingsTests` and `HostPluginTests` (`TestDebugHostComposition`, `TestReleaseHostIntegration`) green; both templates parse; Debug first page is Launcher, Triangle, Matrix. |
| C7 | Specs, skills, AGENTS, docs rewritten to the Direct3D chrome contract. | `validate-skills.ps1` passes; no normative sentence still requires GDI chrome or a chrome HWND. |
| C8 | Receipts: Release launch private bytes/threads unchanged or lower; PresentMon PresentMode on Matrix Focus and Gallery when capture works. | Recorded in `.build/receipts/2026-09-07-host-chrome.md` and this plan. |

## Validation

```powershell
.\build.ps1 -Configuration Debug
.\build.ps1 -Configuration Release
.\test.ps1 -Configuration Debug
.\test.ps1 -Configuration Release
.\build.ps1 -Configuration Release -Platform ARM64
.\validate-skills.ps1
```

Live: launch Release on the XENEON, capture the XENEON screen region, confirm non-black content and the
`device-created` record; hover an edge (chevron appears in the swap chain), double-click a tile (dim, shadow, close).

## Status (2026-09-07, end of round)

Receipts: `.build/receipts/2026-09-07-host-chrome.md`.

| ID | State | Receipt |
| --- | --- | --- |
| C1 | Landed | `TestHostChromeComposition` passes in Debug and Release; live hover shows the chevron and wash in the swap chain, the raise shows dim strips, shadow and close glyph. |
| C2 | Landed | Main window has zero child windows on Matrix Focus; band and overlay classes, brushes, fonts and `PaintRaiseOverlay` are gone from `Application.cpp`. |
| C3 | Landed | `HostChromeDimStrips` checks in `TestWidgetRaisePolicy`. |
| C4 | Landed | Live Gallery container is `Static ex=0x80004` holding `RedXe.Plugin.GdiOrbit`; it dims through window alpha while the Triangle is raised. `HostPluginTests.manifest` added for the layered-child rule. |
| C5 | Landed | Live extended style `0x240000`; self-test passes with the layered-child probe; panel capture is not black. |
| C6 | Landed | Both templates parse with seven Gallery widgets; Debug first page is Launcher, Triangle, Matrix; `SettingsTests` and `HostPluginTests` green. |
| C7 | Landed | `validate-skills.ps1` valid; specs, AGENTS, docs and the rendering skill describe the Direct3D chrome and the manifest rule. |
| C8 | Partly | Launch on Matrix Focus: 35.4 MiB private, 42 threads, GDI 13 / USER 25 (previous round 40.1 MiB). PresentMon capture still blocked by stale elevated ETW sessions. |

Open: PresentMon `PresentMode` on Matrix Focus and Gallery. Everything else in this plan is done; close it once that
capture exists or the owner decides the receipt is not required.

## Excluded

- Porting GdiOrbit to a GPU widget (it stays the native-window example).
- A bundled WebView or media widget; the layered container is the prerequisite, not the feature.
- Removing `IRedXeWindowWidget` from the ABI.
