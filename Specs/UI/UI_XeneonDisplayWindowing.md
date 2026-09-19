# XENEON display and windowing contract

Status: current normative product contract
Last reviewed: 2026-09-19
Owner: `Application` process, display-selection, HWND, and DPI behavior

## Scope

This specification owns RedXe startup display selection, Debug and Release window modes, the screen-edge dock window
kind, the XENEON EDGE design canvas, per-monitor DPI behavior, fallback prompts, and the hidden smoke-test window. Direct3D device and swap-chain
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
- The renderer MUST report a changed DPI to GPU widgets through `OnTargetSizeChanged` even when their physical
  viewport dimensions stay unchanged. Repeating the same dimensions and DPI MUST NOT notify again.
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
| Screenshot (`--screenshot <png> [--page <id>] [--widget <ordinal>] [--after <ms>]`) | Run exactly as the configuration above prescribes (same discovery, placement, services, and frame loop), jump to the named page through the host `PageGoTo` action once the renderer is live and no settle runs, wait the delay (default 3000 ms, 1–120000) with the frame loop idle-waiting as usual, capture the main window through `Common/WindowCapture.cpp` (Windows.Graphics.Capture of an owned, visible window; a widget ordinal crops to that tile's `PixelBoundsAt` in client space, mapped through the DWM extended frame bounds), then close. Exit 0 with the PNG written, 8 when the capture failed (no modal prompt). It MUST NOT activate, move, or resize the window, move the cursor, or send input. |
| Dock (`dock.edge` other than `none`, or `--dock <edge>[@<monitor>]`) | Debug and Release alike: create the dock window kind below on the selected monitor instead of the row that would otherwise apply, and skip the missing-display prompt. `--self-test` ignores the dock. |

Debug and Release display discovery MUST inspect active display paths through
`QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS)`. A target friendly name containing `XENEON` or `CORSAIR`, compared
ordinally without case, qualifies. The matching source device MUST be resolved to its `HMONITOR` bounds. Discovery
failure uses normal shell placement in Debug and follows the missing-display fallback policy in Release.

## Dock window kind

The dock is RedXe as a bar along one edge of one monitor. The **effective dock** is the settings document's `dock`
object (`Specs/Core/Core_Settings.md`) with the `--dock*` command-line overrides applied, and the window kind is
decided once at startup from it: an `edge` other than `none` selects the dock in both configurations, on any
monitor; the XENEON is only what the `xeneon` selector resolves to. `RedXe/DockPlacement.h` owns every pure rule
below (placement, monitor selection, MINMAXINFO, the autohide state machine) and `RedXe/DockOptions.h` the command
line and the merge; `HostPluginTests` and `SettingsTests` prove them without a display topology.

### Command line

Every switch is a `<switch> <value>` pair; a repeated switch, a missing value, or an invalid value is a command-line
error (exit 2 with the usual message box). Each present switch replaces the same-named document member for the
process lifetime, including across live reloads; `--dock none` runs the file without its dock. A `--dock-*` switch
without an effective edge is accepted and inert. `--self-test` validates and ignores them all.

| Switch | Value | Overrides |
| --- | --- | --- |
| `--dock <edge>[@<monitor>]` | `none`, `top`, `bottom`, `left`, `right`, optionally followed by `@` and a monitor selector (`primary`, `xeneon`, `<n>`, `name:<substring>`; never `all`) | `dock.edge`, and `dock.monitor` when the suffix is present |
| `--dock-mode <mode>` | `fixed`, `autohide` | `dock.mode` |
| `--dock-thickness <dips>` | 32–1080 | `dock.thickness` |
| `--dock-reserve <on|off>` | `on`, `off` | `dock.reserveWorkArea` |
| `--dock-peek <pixels>` | 1–64 | `dock.peek` |

### Window

- Styles: `WS_POPUP | WS_CLIPCHILDREN`; extended `WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP`. A dock
  has no taskbar button and no Alt+Tab entry, like the taskbar itself. It is shown with `SW_SHOWNOACTIVATE`: the
  user's current window keeps the focus. Exit is Escape while the bar has focus (click or tap it first), `redxe.quit`,
  or `WM_CLOSE`; there is no close glyph.
- Activation is unchanged (`MA_ACTIVATE` on click or touch; hover never activates).
- The dashboard and swap chain are always sized to the **full** bar rectangle; an autohide strip is a window-size
  change only (below). `OnSize` never resizes the dashboard in dock mode; `PlaceDock` does, through the full
  rectangle, on DPI, monitor, thickness, and edge changes.
- `WM_GETMINMAXINFO` answers with the peek strip (autohide) or the full bar (fixed) as the minimum and the monitor as
  the maximum, because Windows applies `ptMinTrackSize` to `SetWindowPos` as well as to user tracking and the titled
  window's 480×320 minimum would refuse the strip.
- The reachable client for the edge bands is the whole client: the bar fits its monitor by construction, and a
  reserving side dock is excluded from the work area, which would otherwise leave nothing reachable.
- Z-order is topmost; `ABN_FULLSCREENAPP` with a full-screen window on the dock's monitor drops the bar to
  `HWND_BOTTOM` and the clearing notification restores `HWND_TOPMOST`.
- `--screenshot` captures the dock like any window; an autohide dock is held revealed for the whole run (a pending
  capture is a hold), so the PNG shows the bar.

### Monitor and placement

- `monitor` resolves over `EnumDisplayMonitors`: `primary` → the primary display; `<n>` → the n-th in enumeration
  order (1-based); `name:<substring>` → an ordinal case-insensitive substring of the `QueryDisplayConfig` friendly
  name or of the GDI device name (`\\.\DISPLAYn`); `xeneon` → the display XENEON discovery found. A selector that
  resolves to nothing falls back to the primary display with one Warning log record (`dock-monitor-fallback`) and
  no prompt; `WM_DISPLAYCHANGE` re-runs discovery and the selector, so the bar returns when its monitor does.
- Thickness is `MulDiv(thickness, monitorDpi, 96)` (`GetDpiForMonitor`, effective DPI) and is clamped so at least
  half of the monitor's cross dimension stays free (one Warning record, `dock-thickness-clamped`).
- A reserving bar (`fixed` with `reserveWorkArea`) proposes the monitor rectangle trimmed to the thickness on the
  edge side through `ABM_QUERYPOS`, re-trims the returned rectangle to the thickness from its edge, commits it with
  `ABM_SETPOS`, moves the window there, and sends `ABM_WINDOWPOSCHANGED`. The shell therefore decides the along-edge
  span (a left bar with a bottom taskbar ends above the taskbar) and maximized windows stop at the bar.
- An overlay bar (`fixed` without `reserveWorkArea`) and an autohide bar hug the edge of the **work area**, spanning
  the work area along the edge, so they never cover the taskbar or another app bar. With the taskbar on the same
  edge the peek strip therefore sits just above the taskbar rather than at the screen edge.
- The dock always registers as an app bar (`ABM_NEW` with a private `WM_APP` callback) so it receives
  `ABN_POSCHANGED`, `ABN_STATECHANGE` (both re-place), and `ABN_FULLSCREENAPP`; it sends `ABM_ACTIVATE` on
  `WM_ACTIVATE` and `ABM_WINDOWPOSCHANGED` on `WM_WINDOWPOSCHANGED`. An autohide bar additionally registers
  `ABM_SETAUTOHIDEBAREX` for its edge and monitor; a refusal (another autohide bar owns that edge) is one Warning
  record (`dock-autohide-refused`) and the bar continues as a plain strip. `ABM_REMOVE` runs in `CloseMainWindow`
  before the HWND is destroyed. Changing the registration row (reserve ↔ overlay ↔ autohide, or the autohide edge or
  monitor) removes and re-registers.
- `WM_SETTINGCHANGE` with `SPI_SETWORKAREA` re-places an overlay or autohide bar; a reserving bar ignores it and
  follows `ABN_POSCHANGED`. `WM_DPICHANGED` re-places from the monitor DPI instead of applying the suggested
  rectangle. Every re-placement re-checks the adapter of output exactly as a move does.
- The fatal-process path MUST NOT call the shell: a crash while reserving can leave the work area shrunk until
  Explorer next recomputes app-bar positions, which RedXe's next launch triggers.

### Autohide

- In `autohide` mode the window collapses to the outer `peek` physical pixels of the bar (`DockHiddenRect`), same
  along-edge extent, and reveals to the full rectangle. The swap chain is created with `DXGI_SCALING_NONE` at the
  full bar size (`Renderer::SetDockPresentation`), so the client can shrink without `ResizeBuffers`: DWM shows the
  back buffer's top-left region, which is the bar's first rows (top and bottom docks) or columns (left and right
  docks). Reveal and hide are one `SetWindowPos` and one frame each; no layout recompute and no
  `OnTargetSizeChanged` happen.
- States and holds are the `DockPlacement.h` table: `Revealed`, `HidePending` (one-shot hide timer), `Hidden`, and
  `RevealPending` (one-shot dwell timer). A real mouse move over the strip starts the dwell (`revealDelayMilliseconds`;
  0 reveals at once); leaving during the dwell hides again; a touch, pen, or mouse-button contact on the strip and
  `redxe.dock.show` / `redxe.dock.toggle` reveal at once. A revealed bar stays while any hold is active — the pointer
  inside, the window active, a pan/settle/staged neighbour/interactive capture/raise settle, a raised widget, the
  settings-error dialog, a pending screenshot — and arms `hideDelayMilliseconds` when the last hold clears (0 hides
  at once); a hold returning cancels the timer. `redxe.dock.hide` and `toggle` collapse a revealed bar even with the
  pointer inside but are inert while a raise, capture, dialog, or pin holds it. A bar revealed by `redxe.dock.show`
  with nothing holding it stays until some other hold appears and clears. Mouse messages synthesized from touch or
  pen never start the dwell. At most one one-shot timer is armed and every state exit kills it.
- `Hidden` and `RevealPending` are "not visible" for `UpdateDashboardVisibility` and the frame scheduler: widgets
  `SetVisible(FALSE)`, native containers hidden, keyboard focus and interactive capture cleared, edge bands gone, and
  after the single **grip frame** (dashboard clear color plus the host-chrome wash over the strip and a one-DIP accent
  line on its desktop-facing side, two quads) the host blocks on messages like a minimized window. The full-size swap
  chain is retained while hidden so a reveal shows the last frame at once.
- A live reload applies every `dock` member in place (thickness, edge, monitor, mode, reserve, peek, delays) by
  re-placing and, when the registration row changed, re-registering; an edge change that flips orientation reflows
  the dashboard through the full rectangle. Switching between `none` and an edge changes the window kind and takes
  effect at the next launch: one Warning record (`dock-restart-required`) and the rest of the document still
  applies. Command-line-pinned members are re-applied after every merge.

## Windows shell identity

`RedXe.exe` MUST embed the product icon as its conventional primary icon group and use that same resource for the
large and small window-class icons. It MUST embed version information identifying `RedXe.exe`, product `RedXe`, and
file description `RedXe XENEON dashboard`. This gives code and shell surfaces a stable executable identity in
addition to the HWND icon. The hidden self-test MUST extract both large and small icons from its own executable, and
the repository test entrypoint MUST validate the version fields without desktop automation.

## Window and rendering lifecycle

- `Application` MUST own the top-level HWND with `wil::unique_hwnd` and route messages through the instance bound at
  `WM_NCCREATE`.
- `WM_MOUSEACTIVATE` and `WM_POINTERACTIVATE` MUST return `MA_ACTIVATE` so the activating mouse or touch contact is
  delivered to the dashboard. The first tap on an inactive window MUST NOT be eaten.
- Debug and fallback windows MUST retain standard resize, minimize, maximize, move, and title-bar behavior.
- `WM_SIZE` with a zero client dimension is suspension, not failure.
- `WM_PAINT` validates the update region; continuous rendering remains on the idle side of the message loop.
- The window class MUST NOT request `CS_HREDRAW` or `CS_VREDRAW`; resize rendering is driven by `WM_SIZE` and the
  renderer rather than redundant full-client paint invalidation.
- The top-level titled and fullscreen window styles MUST include `WS_CLIPCHILDREN` and the extended style MUST
  include `WS_EX_NOREDIRECTIONBITMAP`: the main window never paints with GDI (its chrome is drawn into the swap
  chain), so DWM keeps no client-sized redirection surface for it. Host-owned native-widget containers are
  `WS_EX_LAYERED` children with their own DWM surface, so GDI, native-control, media, and WebView content remains
  visible above the swap chain and can be dimmed with window alpha while another widget is raised. Direct3D
  presentation MUST still be clipped out of those container rectangles. Windows honors `WS_EX_LAYERED` on a child
  only for a process whose manifest declares a Windows 8 or later `supportedOS`, so `RedXe/app.manifest` declares
  Windows 10, every test binary that creates native containers embeds an equivalent compatibility manifest
  (`Tests/HostPluginTests/HostPluginTests.manifest`), and the self-test creates one layered child and fails when the
  system rejects it.
- `Application` MUST subscribe to `GUID_SESSION_DISPLAY_STATUS`. While the session display is powered off, hidden, or
  minimized, it MUST drain pending messages and then block without rendering or presenting. Display-on, show, and
  restore messages resume normal scheduling.
- Host-owned native-widget child containers MUST use the same physical design-canvas transform as GPU viewports.
  Resize and `WM_DPICHANGED` MUST reposition them and report the destination DPI. Hidden, minimized, display-off, and
  DXGI-occluded states MUST hide and quiesce them before the host blocks.
- `Renderer` MUST register `Application` for DXGI factory occlusion-status window messages. After presentation reports
  full occlusion, RedXe MUST block until that notification and then issue `DXGI_PRESENT_TEST` without building a
  frame. It resumes rendering only after that test succeeds.
- Adapter of output: `Renderer` MUST create the Direct3D device on the hardware adapter whose DXGI output scans out
  the monitor returned by `MonitorFromWindow` for the top-level window, so a XENEON attached to an integrated GPU is
  driven by that GPU and never through a cross-adapter DWM copy. `Application` MUST re-check that adapter on
  `WM_MOVE`, `WM_EXITSIZEMOVE`, `WM_DISPLAYCHANGE`, and after `WM_DPICHANGED`; when the window's monitor is now scanned
  out by another adapter, `Renderer` rebuilds the device on it through the device-loss path. A window on no monitor
  (the hidden self-test and test hosts) uses the default adapter; forced WARP never re-checks. Each device creation
  logs one `device-created` record. The policy and its budgets are owned by
  `Specs/Core/Core_PerformanceAndResources.md`.
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

Adapter-selection changes additionally require a live launch on a machine whose XENEON is attached to a different
GPU than the primary display: the `device-created` log record MUST name the adapter that owns the XENEON with
`adapter owns window monitor: yes`, the process MUST appear on exactly one adapter LUID in the `GPU Engine` and
`GPU Process Memory` counters, and dragging the Debug titled window onto a monitor of the other GPU and back MUST
produce two further `device-created` records with different LUIDs and no visible failure. `HostPluginTests` proves
the pure decision table and the hidden WARP identity without a display topology.

The Release missing-display prompt MUST be checked manually when no matching display is active. The hidden self-test
MUST remain noninteractive in both configurations and MUST verify the top-level `WS_CLIPCHILDREN` style and the
`WS_EX_NOREDIRECTIONBITMAP` extended style.

`HostPluginTests` MUST prove the capture helper without a dashboard: a solid-color popup this process owns, shown
without activation for the capture, yields a PNG of its size and color; a client-space crop yields exactly that
rectangle; a hidden window, a window of another process, and a crop outside the client area are refused. Every
picture under `docs/screenshots/` MUST come from `--screenshot` (a live Debug build, one tile through `--widget`),
never from a desktop screenshot tool.

Native-window composition changes additionally require a live launch with a native widget enabled. The child content
MUST remain visible and animated while the surrounding Direct3D widgets continue presenting.

Scheduling-policy changes MUST extend `HostPluginTests` and its pure scheduler decision table. The automated cases
must prove that hidden, minimized/suspended, session-display-off, and fully occluded states wait for a message, that
occlusion probes occur only after a DXGI status notification, and that static content sleeps after its clean frame.
Changes to the operating-system notification registration or message wiring outside that decision seam additionally
require a live inactive/resume check.

Dock changes MUST keep the pure tables green: `HostPluginTests` proves the placement rectangles for every edge
(reserving proposal and shell re-trim, overlay against a work area with a taskbar or another bar, negative
coordinates), the thickness scaling and clamp, the hidden strip, the grip and its accent for every edge, MINMAXINFO,
monitor selection for every selector kind with the primary fallback, every autohide state-machine row including zero
delays and hold precedence, the scheduler row that a hidden dock waits after its one grip frame, and a dock-kind
swap chain (`DXGI_SCALING_NONE`) presenting a tile frame and a grip frame while the client is smaller than the back
buffer; `SettingsTests` proves the `dock` member, its rejections, minor 2, the `--dock*` grammar with its errors, and
the merge precedence. Live, on the machine's topology: a reserving bar shrinks `rcWork` by exactly its thickness
while it runs and restores it on exit; an overlay bar leaves `rcWork` alone and sits against the work-area edge; a
side bar on a monitor with a bottom taskbar ends above the taskbar; an autohide bar collapses to its strip after the
hide delay, reveals after the dwell when the real cursor rests on the strip, hides after the pointer leaves, and
reveals at once on a click; `--screenshot` of an autohide bar yields the full bar; a live reload re-places thickness,
mode, and edge changes and logs `dock-restart-required` for `none`. The 2026-09-19 closeout recorded all of these on
a 3840×2160 150 % primary plus a 2560×720 150 % XENEON with bottom taskbars.

## Implementation and validation anchors

- Process awareness and command-line modes: `RedXe/Main.cpp`, `RedXe/app.manifest`
- Display discovery, window creation, and DPI transitions: `RedXe/Application.cpp`, `RedXe/Application.h`
- Dock placement, monitor selection, MINMAXINFO, and the autohide state machine: `RedXe/DockPlacement.h`; the
  `--dock*` grammar and merge: `RedXe/DockOptions.h`; dock-kind presentation: `Renderer::SetDockPresentation`
- Physical render-target resizing: `RedXe/Renderer.cpp`, `RedXe/Renderer.h`
- Automated build, scheduler, production host/plugin, and hidden WARP validation: `build.ps1`, `test.ps1`,
  `Tests/HostPluginTests/`
