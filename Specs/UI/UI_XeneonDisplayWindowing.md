# XENEON display and windowing contract

Status: current normative product contract
Last reviewed: 2026-08-31
Owner: `Application` process, display-selection, HWND, and DPI behavior

## Scope

This specification owns RedXe startup display selection, Debug and Release window modes, the XENEON EDGE design
canvas, per-monitor DPI behavior, fallback prompts, and the hidden smoke-test window. Direct3D device and swap-chain
ownership remains in `Renderer`; see the `direct3d11-rendering` skill for that boundary.

Dashboard pages, adaptive placement, runtime orientation reflow, and horizontal touch navigation are owned by
`Specs/UI/UI_Dashboard.md`.

The product target is the CORSAIR XENEON EDGE in its native landscape mode: **2560×720**, **32:9**. The hardware basis
is the [CORSAIR XENEON EDGE product specification](https://www.corsair.com/newsroom/press-release/corsair-launches-the-xeneon-edge-14-5%E2%80%B3-lcd-touchscreen-a-dazzling-and-expansive-display-customized-by-you).

The terms **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative.

## Process DPI contract

- The application MUST run as Per-Monitor-V2 DPI aware. `app.manifest` is the persistent declaration and process
  startup MUST request `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` before creating an HWND.
- The default titled-window client canvas is 2560×720 at `USER_DEFAULT_SCREEN_DPI` (96 DPI). These values are logical
  design dimensions, not unscaled physical pixels on every monitor.
- The physical client dimensions MUST be calculated with `MulDiv(logicalSize, monitorDpi, 96)`.
- Initial non-client dimensions MUST be produced with `AdjustWindowRectExForDpi` for the selected style and DPI.
- After HWND creation, RedXe MUST use `GetDpiForWindow` and correct the initial size before showing the window. This
  prevents a shell-selected monitor whose DPI differs from the system DPI from establishing the wrong canvas.
- A diagnostic process that measures RedXe MUST itself use a Per-Monitor-V2 thread context; otherwise Windows may
  virtualize the returned rectangle and produce misleading dimensions.

## DPI transitions

- The top-level window MUST handle `WM_DPICHANGED` using the new DPI carried in `wParam`.
- A titled window MUST use the suggested destination position and recalculate its outer dimensions for an exact
  2560×720 logical client canvas at the new DPI. This avoids cumulative non-client rounding drift.
- The current fixed-canvas foundation resets a titled window to the default logical canvas during a DPI transition.
- A borderless fullscreen popup MUST use the suggested monitor rectangle rather than applying titled-window sizing.
- The renderer MUST continue receiving the resulting physical client dimensions through `WM_SIZE`.
- Moving a default titled window from a 150% (144 DPI) monitor to a 100% (96 DPI) XENEON MUST result in a measured
  2560×720 physical client area on the XENEON.

## Configuration behavior

| Mode | Required behavior |
| --- | --- |
| Debug with active XENEON | Create a visible `WS_OVERLAPPEDWINDOW` with the RedXe title bar and DPI-adjusted 2560×720 logical client canvas. Place its outer top-left corner at the detected XENEON `rcMonitor` origin; do not force fullscreen. |
| Debug without active XENEON | Create the same titled window using normal shell-selected placement. Do not prompt and do not force fullscreen. |
| Release with active XENEON | Create a `WS_POPUP` borderless window using the detected XENEON monitor's exact `rcMonitor` bounds. |
| Release without active XENEON | Show the missing-display Yes/No warning. Yes creates the standard titled fallback window; No exits successfully without creating the main window. |
| Self-test | Skip display discovery and prompts, create the titled window hidden, validate its DPI-adjusted client dimensions, render one frame, and exit. |

Debug and Release display discovery MUST inspect active display paths through
`QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS)`. A target friendly name containing `XENEON` or `CORSAIR`, compared
ordinally without case, qualifies. The matching source device MUST be resolved to its `HMONITOR` bounds. Discovery
failure uses normal shell placement in Debug and follows the missing-display fallback policy in Release.

## Windows shell identity

`RedXe.exe` MUST embed the product icon as its conventional primary icon group and use that same resource for the
large and small window-class icons. It MUST embed version information identifying `RedXe.exe`, product `RedXe`, and
file description `RedXe XENEON dashboard`. This gives code and shell surfaces a stable executable identity in
addition to the HWND icon. The hidden self-test MUST extract both large and small icons from its own executable, and
the repository test entrypoint MUST validate the version fields without desktop automation.

## Window and rendering lifecycle

- `Application` MUST own the top-level HWND with `wil::unique_hwnd` and route messages through the instance bound at
  `WM_NCCREATE`.
- Debug and fallback windows MUST retain standard resize, minimize, maximize, move, and title-bar behavior.
- `WM_SIZE` with a zero client dimension is suspension, not failure.
- `WM_PAINT` validates the update region; continuous rendering remains on the idle side of the message loop.
- The window class MUST NOT request `CS_HREDRAW` or `CS_VREDRAW`; resize rendering is driven by `WM_SIZE` and the
  renderer rather than redundant full-client paint invalidation.
- The top-level titled and fullscreen window styles MUST include `WS_CLIPCHILDREN`. Direct3D presentation MUST be
  clipped out of host-owned native-widget child rectangles so GDI, native-control, media, and WebView content remains
  visible above the swap-chain surface.
- `Application` MUST subscribe to `GUID_SESSION_DISPLAY_STATUS`. While the session display is powered off, hidden, or
  minimized, it MUST drain pending messages and then block without rendering or presenting. Display-on, show, and
  restore messages resume normal scheduling.
- Host-owned native-widget child containers MUST use the same physical design-canvas transform as GPU viewports.
  Resize and `WM_DPICHANGED` MUST reposition them and report the destination DPI. Hidden, minimized, display-off, and
  DXGI-occluded states MUST hide and quiesce them before the host blocks.
- `Renderer` MUST register `Application` for DXGI factory occlusion-status window messages. After presentation reports
  full occlusion, RedXe MUST block until that notification and then issue `DXGI_PRESENT_TEST` without building a
  frame. It resumes rendering only after that test succeeds.
- Escape and `WM_CLOSE` close the application through the HWND owner.
- Fullscreen selection and DPI policy belong to `Application`; swap-chain sizing and presentation belong to
  `Renderer`.

## Validation contract

Changes to display selection, window styles, initial sizing, DPI handling, resize routing, manifest awareness, or
startup policy MUST run:

```powershell
.\test.ps1 -Configuration Debug -Platform x64 -Rebuild
.\test.ps1 -Configuration Release -Platform x64 -Rebuild
.\build.ps1 -Configuration Release -Platform ARM64 -Rebuild
```

Interactive DPI changes additionally require a live move test between monitors with different scale factors. Under a
Per-Monitor-V2 probe, record the starting and destination DPI plus the final physical client rectangle. A 144→96 move
onto a native landscape XENEON is green only when the final client rectangle is exactly 2560×720.

Debug placement changes additionally require a live launch with an active XENEON. The initial outer window origin
MUST equal the detected XENEON `rcMonitor` origin, and `MonitorFromWindow` MUST resolve the window to that monitor.

The Release missing-display prompt MUST be checked manually when no matching display is active. The hidden self-test
MUST remain noninteractive in both configurations and MUST verify the top-level `WS_CLIPCHILDREN` style.

Native-window composition changes additionally require a live launch with a native widget enabled. The child content
MUST remain visible and animated while the surrounding Direct3D widgets continue presenting.

Scheduling-policy changes MUST extend `HostPluginTests` and its pure scheduler decision table. The automated cases
must prove that hidden, minimized/suspended, session-display-off, and fully occluded states wait for a message, that
occlusion probes occur only after a DXGI status notification, and that static content sleeps after its clean frame.
Changes to the operating-system notification registration or message wiring outside that decision seam additionally
require a live inactive/resume check.

## Implementation and validation anchors

- Process awareness and command-line modes: `RedXe/Main.cpp`, `RedXe/app.manifest`
- Display discovery, window creation, and DPI transitions: `RedXe/Application.cpp`, `RedXe/Application.h`
- Physical render-target resizing: `RedXe/Renderer.cpp`, `RedXe/Renderer.h`
- Automated build, scheduler, production host/plugin, and hidden WARP validation: `build.ps1`, `test.ps1`,
  `Tests/HostPluginTests/`
