# XENEON display and windowing contract

Status: current normative product contract
Last reviewed: 2026-09-28
Owner: `Application` process, display-selection, HWND, and DPI behavior

## Scope

This specification owns RedXe startup display selection, Debug and Release window modes, the screen-edge dock window
kind, the XENEON EDGE design canvas, per-monitor DPI behavior, fallback prompts, the notification-area icon, and the
hidden smoke-test window. Direct3D device and swap-chain
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
| Release without active XENEON | Show the missing-display Yes/No warning. Yes creates the standard titled fallback window; No exits successfully without creating the main window. A default settings file installed at this start because it was missing carries the first-run dock ("First start without a XENEON"), so the Dock row applies instead. |
| Self-test | Skip display discovery and prompts, create the titled window hidden, validate its DPI-adjusted client dimensions, render one frame, and exit. |
| Screenshot (`--screenshot <png> [--page <id>] [--widget <ordinal>] [--after <ms>]`) | Run exactly as the configuration above prescribes (same discovery, placement, services, and frame loop), jump to the named page through the host `PageGoTo` action once the renderer is live and no settle runs, wait the delay (default 3000 ms, 1–120000) with the frame loop idle-waiting as usual, capture the main window through `Common/WindowCapture.cpp` on a capture worker (Windows.Graphics.Capture of an owned, visible window; a widget ordinal crops to that tile's `PixelBoundsAt` in client space, mapped through the DWM extended frame bounds), then close. The UI thread continues handling input and timers while capture waits for its first frame. Exit 0 only with the PNG written; 8 when the capture failed, when its worker could not start, and when the run ended before the PNG was written (the window closed during the delay or the capture, or a startup, graphics, or rendering failure ended the run first), with one `screenshot-failed` Warning record once the log is open (a failure before the settings load reaches only the debugger output). The run is unattended (`RedXeIsUnattendedRun`, as `--self-test` is) and MUST NOT wait on a modal box: the Release missing-display prompt is not shown and the titled fallback window is created as for its Yes; the settings fallback notice is one Warning record (`settings-fallback-notice`) instead of its box; the previous-crash notice is left for the next interactive start; a command-line error goes to the console or redirected output (exit 2); and a failure exit, whatever its code, is one Error record (`failure-exit`, naming the code) and a debugger line instead of the exit-code message box (`RedXeShowsExitCodeBox`). A failed Debug runtime check ends the run with exit code 3 (`Common/FailureReports.h`). A worker still capturing when the window closes is joined and its result decides the exit code. Only this command-line mode closes after the capture; the `redxe.screenshot` action shares the pipeline and keeps RedXe running (`Plugins_Actions.md`). It MUST NOT activate, move, or resize the window, move the cursor, or send input. |
| Dock (`dock.edge` other than `none`, or `--dock <edge>[@<monitor>]`) | Debug and Release alike: create the dock window kind below on the selected monitor instead of the row that would otherwise apply, and skip the missing-display prompt. A live reload that turns the dock on or off switches the running window between this row and the one that would otherwise apply ("Switching the window kind"). `--self-test` ignores the dock. |
| Help (`--help`, `-h`, `/?`, `-?`) | Print the command-line catalog and exit 0 before any other switch is read: to the console the process was started from (a GUI process attaches to its parent's), to a redirected stdout as UTF-8, or, without either, to a message box. Every other token on the line MUST be a catalogued switch or the value of one; the first unknown token is a command-line error (exit 2, `Unknown argument "<token>". Run RedXe.exe --help for the command line.`) shown the same way, never as a message box when `--self-test` or `--screenshot` is on the line. |

The command line is declared once in `RedXe/CommandLine.h`: the catalog `--help` prints and the names `Main.cpp`
parses through, so a switch cannot exist without an entry. Adding, renaming, or removing a switch changes that
catalog, the "Command line" section of `docs/usage.md`, and the owning row of this table in the same change;
`SettingsTests` pins the catalog (unique well-formed names, every entry printed, the help aliases, the unknown-token
scanner) and the unattended-run policy `Main.cpp` applies through it (`RedXeIsUnattendedRun`: `--self-test` or
`--screenshot` on the line; `RedXeShowsExitCodeBox`: no exit-code box for an unattended run; `RedXeScreenshotExitCode`:
8 for a capture run without its PNG, whatever ended it; `RedXeExitCodeName`: the `failure-exit` record names each
code as `--help` lists it), and `test.ps1` runs `--help` through a redirected stdout, an unknown switch under
`--self-test`, and a missing switch value under `--self-test` and an invalid one under `--screenshot`, each bounded so
that a message box fails the step instead of holding it.

Debug and Release display discovery MUST inspect active display paths through
`QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS)`. A target friendly name containing `XENEON` or `CORSAIR`, compared
ordinally without case, qualifies. The matching source device MUST be resolved to its `HMONITOR` bounds. Discovery
failure uses normal shell placement in Debug and follows the missing-display fallback policy in Release.

## Dock window kind

The dock is RedXe as a bar along one edge of one monitor. The **effective dock** is the settings document's `dock`
object (`Specs/Core/Core_Settings.md`) with the `--dock*` command-line overrides applied, and the window kind follows
it at startup and on every live reload: an `edge` other than `none` selects the dock in both configurations, on any
monitor; the XENEON is only what the `xeneon` selector resolves to. `RedXe/DockPlacement.h` owns every pure rule
below (placement, what each mode reserves and the app-bar messages that reserve it, monitor selection, MINMAXINFO,
the full-screen yield, the autohide state machine and slide, the peek clamp, the dashboard canvas, input routing on a
collapsed or sliding bar, the first-run monitor, edge, and thickness) and `RedXe/DockOptions.h` the command line and
the merge; `HostPluginTests` and `SettingsTests` prove them without a display topology.

### Command line

Every switch is a `<switch> <value>` pair; a repeated switch, a missing value, or an invalid value is a command-line
error (exit 2 with the usual message box, or with the text on the console or redirected output when `--self-test` or
`--screenshot` is on the line). Each present switch replaces the same-named document member for the
process lifetime, including across live reloads; `--dock none` runs the file without its dock. A `--dock-*` switch
without an effective edge is accepted and inert. `--self-test` validates and ignores them all.

| Switch | Value | Overrides |
| --- | --- | --- |
| `--dock <edge>[@<monitor>]` | `none`, `top`, `bottom`, `left`, `right`, optionally followed by `@` and a monitor selector (`primary`, `secondary`, `xeneon`, `<n>`, `name:<substring>`; never `all`) | `dock.edge`, and `dock.monitor` when the suffix is present |
| `--dock-mode <mode>` | `fixed`, `autohide` | `dock.mode` |
| `--dock-thickness <dips>` | 32–1080 | `dock.thickness` |
| `--dock-reserve <on|off>` | `on`, `off` | `dock.reserveWorkArea` |
| `--dock-peek <pixels>` | 1–64 | `dock.peek` |

### Window

- Styles: `WS_POPUP | WS_CLIPCHILDREN`; extended `WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP`. A dock
  has no taskbar button and no Alt+Tab entry, like the taskbar itself. It is shown with `SW_SHOWNOACTIVATE`: the
  user's current window keeps the focus. The previous-crash notice (`Specs/Core/Core_CrashHandling.md`) MUST NOT be
  owned by the bar: it has no owner and is answered before the bar is shown, so the bar never stands topmost without
  a presented frame under it and closing it activates no RedXe window (an active autohide bar would stay revealed).
  Exit is Escape while the bar has focus (click or tap it first), `redxe.quit`, or `WM_CLOSE`; there is no close
  glyph.
- Activation is unchanged (`MA_ACTIVATE` on click or touch; hover never activates).
- The dashboard and swap chain are always sized to the **full** bar rectangle; an autohide strip is a window-size
  change only (below). `OnSize` never resizes the dashboard in dock mode; `PlaceDock` does, through the full
  rectangle, on DPI, monitor, thickness, and edge changes. A swap chain created while the window is the strip (a
  live reload that rebuilds the page of a collapsed bar, a device rebuild) is still created at the full bar, the
  size `Renderer::Resize` last received.
- `WM_GETMINMAXINFO` answers with the peek strip (autohide, clamped like every strip by `DockClampPeek`) or the full
  bar (fixed) as the minimum and the monitor as the maximum, because Windows applies `ptMinTrackSize` to
  `SetWindowPos` as well as to user tracking and the titled window's 480×320 minimum would refuse the strip.
- The reachable client for the edge bands is the whole client: the bar fits its monitor by construction, and a
  reserving side dock is excluded from the work area, which would otherwise leave nothing reachable.
- Z-order is topmost; a full-screen window on the dock's monitor drops the bar to `HWND_BOTTOM` and the clearing
  notification restores `HWND_TOPMOST`. `ABN_FULLSCREENAPP` names no monitor and the shell also sends it for a
  full-screen window on another display (a XENEON dashboard, a video on a second screen), so the bar steps down only
  while the shell reports one and the foreground window covers the dock's `rcMonitor` (`DockYieldsToFullscreen` over
  the pure `DockForegroundCoversMonitor`). A maximized window with a caption MUST NOT count: its `GetWindowRect`
  includes the invisible resize borders, which overhang a monitor whose work area is the whole monitor (no taskbar
  there, or an auto-hiding one) by about 8 px on every side. A captionless window covering the monitor counts,
  maximized or not, and so does one spanning several monitors.
- `--screenshot` captures the dock like any window; an autohide dock is held revealed for the whole run (a pending
  capture is a hold), so the PNG shows the bar.
- Drag-to-resize: the inner edge of a revealed bar (the side facing the desktop) is a host-owned grip band
  `kDockResizeBandDips` (6) deep (`DockResizeBandRect`); it shows the size cursor, and a mouse press there captures
  the pointer and drags the thickness (`DockThicknessFromDrag`: the distance from the outer edge in DIPs at the
  monitor DPI, keeping the offset under the pointer, clamped to 32–1080 and half the monitor). During the drag the
  window and dashboard follow the pointer live with the outer edge fixed and the shell reservation untouched; the
  release (or a lost capture) commits through `PlaceDock` (a fixed reserving bar reserves its new thickness, an
  autohide strip stays as it was), replaces a `--dock-thickness` pin for the run, and persists `dock.thickness` into
  the settings document (`SettingsStore::PersistDockThickness`, which creates the `dock` object and raises
  `version.minor` to 2 when needed, and writes nothing for a release at the thickness the document already has; a
  failed write is one Warning record, `dock-thickness-persist-failed`). A drag is a hold for an autohide bar. Touch
  and pen do not resize.

### Monitor and placement

- `monitor` resolves over `EnumDisplayMonitors`: `primary` → the primary display (`MONITORINFOF_PRIMARY`);
  `secondary` → the second screen, the first display in enumeration order that is neither the primary nor the XENEON
  discovery found, and the XENEON only when it is the only display that is not the primary
  (`RedXeActions::SecondaryMonitorRank`, the rule the actions' `@secondary` uses too), so a XENEON connected later
  never takes a `secondary` bar from the second screen; `<n>` → the n-th in enumeration order (1-based);
  `name:<substring>` → an ordinal case-insensitive substring of the `QueryDisplayConfig` friendly name or of the GDI
  device name (`\\.\DISPLAYn`); `xeneon` → the display XENEON discovery found. A selector that resolves to nothing
  (`secondary` on a single display included) falls back to the primary display with one Warning log record
  (`dock-monitor-fallback`) and no prompt; `WM_DISPLAYCHANGE` re-runs discovery and the selector, so the bar returns
  when its monitor does.
- Thickness is `MulDiv(thickness, monitorDpi, 96)` (`GetDpiForMonitor`, effective DPI) and is clamped so at least
  half of the monitor's cross dimension stays free (one Warning record, `dock-thickness-clamped`).
- What the bar reserves in the monitor's work area is its **registration row** (`DockReservationFor`): `fixed` with
  `reserveWorkArea` reserves the whole bar, `autohide` reserves its peek strip whatever `reserveWorkArea` says, and
  `fixed` without `reserveWorkArea` reserves nothing.
- A reserving bar proposes the monitor rectangle trimmed on the edge side to what it reserves
  (`DockReservationProposal`: the thickness, or the strip clamped by `DockClampPeek`) through `ABM_QUERYPOS`,
  re-trims the returned rectangle to that depth from its edge, commits it with `ABM_SETPOS`, moves the window, and
  sends `ABM_WINDOWPOSCHANGED`. The shell therefore decides the outer edge and the along-edge span (a left bar with a
  bottom taskbar ends above the taskbar; a strip on the taskbar's edge sits beside the taskbar), and maximized windows
  stop at the bar, or just inside an autohide strip, on every edge. The full bar runs the thickness inward from the
  committed rectangle's outer edge (`DockFullRectFromReserved`): a reserved bar is that rectangle, and an autohide
  bar's strip is exactly its collapsed window (`DockHiddenRect` of the full bar), so the revealed bar is a topmost
  window lying over the work area beyond the strip, never a larger reservation.
- An overlay bar (`fixed` without `reserveWorkArea`, or any bar whose `ABM_NEW` the shell refused) hugs the edge of
  the **work area**, spanning the work area along the edge, so it never covers the taskbar or another app bar.
- The dock always registers as an ordinary app bar (`ABM_NEW` with a private `WM_APP` callback) so it receives
  `ABN_POSCHANGED`, `ABN_STATECHANGE` (both re-place and reserve again), and `ABN_FULLSCREENAPP`; it sends
  `ABM_ACTIVATE` on `WM_ACTIVATE` and `ABM_WINDOWPOSCHANGED` on a `WM_WINDOWPOSCHANGED` it did not cause itself (a
  placement reports its own move, and a reveal, a hide, a slide step, and a drag preview send nothing). No dock
  registers `ABM_SETAUTOHIDEBAREX`: an autohide bar is a reserving bar of its strip, not a shell autohide bar, so
  another autohide bar on its edge (an auto-hiding taskbar) refuses it nothing. A refused `ABM_NEW` is one Warning
  record (`dock-appbar-refused`) and the bar overlays. `ABM_REMOVE` runs in `CloseMainWindow` before the HWND is
  destroyed.
- Each placement pass sends exactly the registration messages `PlanDockAppBar` lists, in order: `ABM_REMOVE` when the
  row drops a reservation (the shell keeps the last `ABM_SETPOS` rectangle until `ABM_REMOVE`), `ABM_NEW` when the bar
  is not registered, and `ABM_QUERYPOS` then `ABM_SETPOS` when the row reserves and the reservation is new or its row,
  edge, proposed rectangle (monitor, thickness, or clamped strip), or monitor DPI changed, or the shell may have moved
  it (`ABN_POSCHANGED`, `ABN_STATECHANGE`, or `WM_DISPLAYCHANGE` since the last pass; the mark is cleared before a
  pass's shell calls, so a notification heard during them marks the pass it asks for). Otherwise the committed
  rectangle is kept and nothing is sent, so the release of an autohide bar's thickness drag and a reload that leaves
  the reservation alone do not re-lay out the desktop. Autohide ↔ fixed reserving is one `ABM_QUERYPOS` /
  `ABM_SETPOS`; either to a fixed overlay is `ABM_REMOVE` then `ABM_NEW`.
- Explorer keeps app bars in its own process, so a restarted Explorer knows no registration, reserved bar or strip,
  or full-screen report of the bar. The main window of every kind hears the `TaskbarCreated` broadcast
  (`RegisterWindowMessageW`, admitted through `ChangeWindowMessageFilterEx` for an elevated run), whatever `trayIcon`
  says. On it an active dock MUST remove its registration (`ABM_REMOVE`), forget the full-screen report, place itself
  again (`ABM_NEW`, then `ABM_QUERYPOS` and `ABM_SETPOS` for a reserving bar or an autohide strip), re-apply its
  z-order, and log one Info record (`dock-appbar-renewed`). The removal comes first because a running Explorer can
  send the broadcast while it still holds the bar, and `ABM_NEW` refuses a window that is already registered; a new
  Explorer ignores the removal of a bar it does not know. A broadcast heard inside a placement's shell call is handled
  once that placement has finished (`Application::OnTaskbarCreated`).
- A placement makes cross-process shell calls and moves the window, and the UI thread dispatches sent messages while
  it waits, so a `WM_DISPLAYCHANGE`, `WM_SETTINGCHANGE`, `WM_DPICHANGED`, or app-bar notification can ask for a
  placement while one runs. That request MUST NOT be dropped and MUST NOT recurse: it is recorded, and the running
  placement makes one more pass from the topology as it is then, with the dashboard following when any recorded request
  asked for it (`BeginDockPlacement` and `NextDockPlacementPass`). At most `kDockMaximumExtraPlacementPasses` (2) extra
  passes run, so a request that every pass raises again cannot keep the UI thread placing.
- `WM_SETTINGCHANGE` with `SPI_SETWORKAREA` re-places an overlay bar; a reserving bar and an autohide strip ignore it,
  since their own reservation raises it too, and follow `ABN_POSCHANGED`. `WM_DPICHANGED` re-places from the monitor
  DPI instead of applying the suggested rectangle. Every re-placement re-checks the adapter of output exactly as a
  move does.
- The fatal-process path MUST NOT call the shell: a crash while reserving (a fixed reserving bar or an autohide strip)
  can leave the work area shrunk until Explorer next recomputes app-bar positions, which RedXe's next launch triggers.

### Autohide

- In `autohide` mode the window collapses to the outer `peek` physical pixels of the bar (`DockHiddenRect`), same
  along-edge extent, and reveals to the full rectangle. `peek` and the thickness are validated apart, so the strip is
  clamped to the bar (`DockClampPeek`: at least 1 pixel, at most the full bar); the reservation, placement, every
  reveal and hide, the grip, and `WM_GETMINMAXINFO` MUST use that one clamped value.
- The strip is reserved in the monitor's work area ("Monitor and placement"), so maximized windows stop just inside it
  on every edge instead of lying under it: a top strip leaves their caption buttons and tabs uncovered, and a bottom or
  side strip their status bars and scrollbars. A reveal lies over the work area as a topmost window at the full bar,
  over those maximized windows, exactly as an unreserved bar would; a hide returns to the strip. The reserved rectangle
  stays the strip throughout: a reveal, a hide, and every slide step move only the window and MUST NOT call the shell,
  so the work area never changes with the reveal state. The swap chain is created with `DXGI_SCALING_NONE` at
  the full bar size (`Renderer::SetDockPresentation`), so the client can shrink without `ResizeBuffers`: DWM shows the
  back buffer's top-left region, which is the bar's first rows (top and bottom docks) or columns (left and right
  docks). Neither a reveal nor a hide recomputes the layout, calls `OnTargetSizeChanged`, or resizes the buffers.
- The dashboard is laid out on the full bar, so every host-side geometry built on that layout MUST use the dashboard
  canvas (`DockDashboardCanvas`, `Application::DashboardCanvasSize`: the full bar for a placed dock, the client for
  the standard kinds) and never the client of a collapsed or sliding bar: tile bounds and hit tests, raises, page
  offsets and the staged neighbour, accessibility and text-input rectangles, and a `--screenshot` crop. A top or left
  bar's slide translation is part of the tile bounds (`DashboardHost::PixelBoundsAt`), so a point on a sliding bar
  hits the tile the frame shows there. A collapsed bar hits no tile (an OLE drop on its strip goes nowhere) and
  exposes no accessibility view; a sliding bar exposes none until it settles, as during a page settle.
- A reveal and a hide **slide** over `animationMilliseconds` (`Specs/Core/Core_Settings.md`; default 200, 0 keeps one
  `SetWindowPos` and one frame). Each presented frame sizes the window to the outer `v` pixels of the full bar
  (`DockHiddenRect` at the visible thickness `v`), eased out from the strip to the full bar for a reveal and eased in
  back to the strip for a hide (`DockSlideVisiblePixels`). A bottom or right bar slides as it is, because the buffer's
  top-left follows the window's moving inner edge; a top or left bar's dashboard is translated back by the part still
  hidden (`DockSlideContentOffset`, `DashboardHost::SetSlideOffset`, native containers included), so every bar leads out
  of the screen edge with its inner edge and no window ever leaves its monitor. Each step MUST run after the
  frame-latency wait, right before the frame that shows it (`Application::TickDockSlide`), so its `SetWindowPos` and
  the top or left translation reach the same DWM composition as that frame's `Present` rather than the one before; the
  last step ends the slide before its frame (a finished hide presents the grip frame at the strip size), and a slide
  that no frame will show (a hidden window, the display off, a suspended renderer) ends at once. A native container
  that fails to move with a step is moved again by the next offset, also when that offset is unchanged, such as the
  zero offset that ends the slide. A slide is visible motion: the host presents every frame until it ends (the
  scheduler's motion input), then blocks as before. The dashboard is visible from the first frame of a reveal and
  until the last frame of a hide, which the grip frame follows. The reveal state stays the authority: when it flips
  during a slide, a new slide starts from where the bar is and takes the share of `animationMilliseconds` its distance
  is of the whole travel (`DockSlideDurationMilliseconds`). A page change, a raise, a dock drag, a placement, and a
  live reload first complete a slide in progress at the size the reveal state asks for, so they work on settled
  geometry. A `--screenshot` capture waits for a slide to end.
- Input on the bar follows `DockRouteInput`. A mouse move over the strip or a bar sliding out starts the dwell. A
  touch, pen, or mouse-button press there reveals at once, and the rest of that contact up to its release MUST go
  nowhere: no tile was on screen under it, so no widget receives its release and no double-activate candidate is
  recorded. A wheel there MUST be dropped: it neither snaps a hiding bar shut nor scrolls or pages a widget nobody can
  see. A press on a bar sliding in first completes the slide, and its contact (the press, its moves, and its release)
  MUST be routed with `DockSettleShift`, so it reaches the tile or the inner-edge grip that was on screen under it
  rather than what the settle moves there; a wheel on a bar sliding in reaches the tile under it as the frame shows it,
  without settling (a whole page detent settles through the page change).
- States and holds are the `DockPlacement.h` table: `Revealed`, `HidePending` (one-shot hide timer), `Hidden`, and
  `RevealPending` (one-shot dwell timer). A real mouse move over the strip starts the dwell (`revealDelayMilliseconds`;
  0 reveals at once); leaving during the dwell hides again; a touch, pen, or mouse-button contact on the strip and
  `redxe.dock.show` / `redxe.dock.toggle` reveal at once. A revealed bar stays while any hold is active — the pointer
  inside, the window active, a pan/settle/staged neighbour/interactive capture/raise settle, a raised widget, the
  settings-error dialog, a pending screenshot, a reveal still sliding out of the edge — and arms `hideDelayMilliseconds`
  when the last hold clears (0 hides at once); a hold returning cancels the timer. `redxe.dock.hide` and `toggle`
  collapse a revealed bar even with the pointer inside but are inert while a raise, capture, dialog, or pin holds it. A
  bar revealed by `redxe.dock.show` with nothing holding it stays until some other hold appears and clears (its own
  slide is not one). A page or widget change on the strip or a bar sliding out (a `page.*` or `widget.*` host action
  from a Logicon key, the dialpad, or a Launcher binding, or an accessibility raise) MUST reveal the bar at once and
  settle it before it runs (`PageOrWidgetChange`, `Application::RevealDockForChange`), so the change is seen and laid
  out on the full bar. Unlike `redxe.dock.show` it pins nothing: the change's page settle or raise holds the bar, and a
  change that ends without a hold (a `page.next` on the last page) leaves it to the hide delay. Mouse messages
  synthesized from touch or pen never start the dwell. At most one one-shot timer is armed and every state exit kills
  it.
- `Hidden` and `RevealPending`, once any slide has ended, are "not visible" for `UpdateDashboardVisibility` and the
  frame scheduler: widgets `SetVisible(FALSE)`, native containers hidden, keyboard focus and interactive capture
  cleared, edge bands gone, and after the single **grip frame** (dashboard clear color plus the host-chrome wash over
  the strip and a one-DIP accent line on its desktop-facing side, two quads) the host blocks on messages like a
  minimized window. The full-size swap chain is retained while hidden so a reveal presents at once, without rebuilding
  it.
- A live reload applies every `dock` member in place (thickness, edge, monitor, mode, reserve, peek, delays, slide) by
  re-placing with the registration `PlanDockAppBar` gives it ("Monitor and placement"): `autohide` ↔ a fixed reserving
  bar moves the reservation between the strip and the whole bar, either to a fixed overlay removes the bar and registers
  it again so the work area comes back, and a member that leaves the reservation alone sends no shell call; an edge
  change that flips orientation reflows the dashboard through the full rectangle. Switching between `none` and an edge
  switches the window kind live (next section) while the rest of the document applies as usual. Command-line-pinned
  members are re-applied after every merge, so `--dock none` or `--dock <edge>` keeps the kind for the run.
- During an inner-edge drag the window follows the pointer. Dashboard and swap-chain resize work is coalesced to
  at most one callback per 16 ms, with the final dimensions flushed on release before persisting thickness. A fixed
  reserving bar's work-area reservation is committed on release; an autohide bar's strip does not depend on the
  thickness (unless the peek is clamped to a thinner bar), so its release reserves nothing new.

### Switching the window kind

A valid live reload whose effective `edge` changes between `none` and an edge switches the kind of the running
window (`Application::SwitchWindowKind`). The HWND and everything bound to it stay: widget instances, services,
native containers, the settings watcher, the drop target, and accessibility.

- Every interaction bound to the old geometry ends (raise, page navigation, wheel sequence, keyboard focus,
  interactive capture, and a dock drag, whose thickness is then not persisted; dock timers stop), the renderer shuts
  down, and a dock unregisters its app bar (`ABM_REMOVE`), so the shell returns a reserved work area before the
  standard window is placed. XENEON discovery re-runs. Shell notifications that arrive meanwhile find no dock.
- The window leaves minimized or maximized, hides (the taskbar adds or drops the button only for a window shown after
  the `WS_EX_APPWINDOW` / `WS_EX_TOOLWINDOW` change), and takes the exact styles of the target kind; topmost changes
  through `SetWindowPos`, and the `WS_DISABLED` of an open settings dialog is kept. A dock is then placed by
  `PlaceDock`, starting revealed. The standard kind is placed by the startup rows of the mode table without the
  missing-display prompt: Release fullscreen on the XENEON's `rcMonitor`; the titled window at the XENEON origin;
  without a XENEON, the titled window (in Release, the fallback) at the work-area origin of the monitor the dock was
  on. The titled window gets its exact DPI-adjusted 2560×720 canvas, re-applied at the intended origin when the move
  or the show changes its DPI (`Application::PlaceStandardWindow`).
- The dashboard is resized to the new canvas and the renderer is initialized again with the target kind's
  presentation (`DXGI_SCALING_NONE` at the full bar for a dock, `DXGI_SCALING_STRETCH` at the client otherwise) on
  the adapter that owns the new monitor. GPU widgets see one `OnDeviceLost` / `OnDeviceCreated` pair, as on an
  adapter change; no widget instance is recreated. A reload that also rebuilds the active page restyles and places
  the hidden window before the page runtime starts, so its renderer is created once, for the new kind; a failed
  apply restyles the window back before the previous page is restored.
- The window is shown with `SW_SHOWNOACTIVATE`: the window in which the file was saved keeps the focus. An autohide
  dock collapses after the hide delay as at launch.
- A switch logs one Info record (`window-kind-changed`).
- A failed step MUST NOT end the process or mark the file applied, whether the reload changes only `dock` or also
  rebuilds the active page. The window is switched back to the kind it had and placed as a switch to that kind
  places it, except that a standard window without a XENEON returns to the monitor it was on, not to the monitor of
  the failed dock. The reload is rejected like any failed apply (`Specs/Core/Core_Settings.md` "Live reload and
  diagnostics"): the previous document, page, services, and dock stay active, its stamp is not applied, and the
  settings-error dialog opens. The failure logs one Warning record (`window-kind-switch-failed`, with the failing
  `HRESULT`) and the rollback logs no `window-kind-changed`. Only a rollback that leaves no renderer is a runtime
  failure: the Error record `settings-apply-failed` and exit code 5.
- `--self-test` pins the edge to `none`, so a document `dock` never switches its hidden titled window.

### First start without a XENEON

When RedXe installs its default settings file at startup because the file is missing (`Specs/Core/Core_Settings.md`
"Cold load and recovery"), XENEON discovery succeeded without finding a display, and the process does not run in a
remote session (`SM_REMOTESESSION`, `DockFirstRunOffered`), the installed document carries the **first-run dock**:
`{ "edge": E, "monitor": M, "mode": "autohide", "thickness": T }`, so this start and the following ones show the Dock
row of the mode table instead of the Release missing-display prompt or the Debug titled window. The recovery of an
invalid default file MUST install the plain template, even without a XENEON, so a file that failed validation never
turns a XENEON or window configuration into a bar. A remote session sees only the remote client's displays, so it MUST
install the plain template too: a bar decided there would stay in the file when the user is back at the XENEON.
`MakeFirstRunDock` measures the displays once, at that install:

- `M` is the second screen when more than one display is active (`DockFirstRunMonitor`): `secondary`, which
  resolves like every selector at runtime, so the bar follows the first display that is neither the primary nor a
  XENEON ("Monitor and placement"). With one display it is `primary`. The primary monitor is identified by
  `MONITORINFOF_PRIMARY` during enumeration, even when another monitor contains screen coordinate `(0,0)`, and the
  second screen is chosen in the enumeration order the `secondary` selector uses.
- `E` is the better-ranked horizontal edge of that display (`DockFirstRunEdge`), and `bottom` when both rank the same,
  so the strip stays away from the caption buttons and tabs at the top of maximized windows wherever the bottom is
  free. Maximized windows stop beside the reserved strip on either edge ("Autohide"), so a top strip never covers
  them, but a pointer thrown against the top edge for them would land on it. From best to worst:
  1. A free screen edge.
  2. An edge beside a taskbar: the display's work area is trimmed on that side (a taskbar that stays visible, or any
     reserving app bar), or an autohide bar is registered on that edge of that display (`ABM_GETAUTOHIDEBAREX`: an
     auto-hiding taskbar). A registration counts only while its window exists (`DockAutohideBarHoldsEdge`): Explorer
     keeps reporting the autohide bar of a process that crashed or was killed until another bar registers on that
     edge, and that registration MUST NOT take the edge from the install that follows. RedXe's own bar never
     registers there, since it reserves its strip; a strip a crashed or killed RedXe left reserved trims the work area
     like any reserving bar until Explorer next recomputes app-bar positions, so an install in that gap ranks its edge
     as beside a taskbar, and the `dock-first-run` record shows the trimmed work area.
  3. An edge another display shares, whatever holds it: a display right above or below that overlaps it along the
     edge (`DockDisplayTouchesEdge`). The pointer crosses that edge between the displays, so the strip MUST NOT go
     there while the other edge is not shared.

  A display without a taskbar on either horizontal edge (a second screen without a taskbar of its own, a side
  taskbar, or Explorer not started) therefore gets `bottom`; a display whose own taskbar holds the bottom gets `top`,
  at the screen edge rather than beside the taskbar; under another display it gets `bottom`, beside its own taskbar if
  it has one; and over another display it gets `top`.
- `T` gives the bar the XENEON EDGE's 32:9 proportions along that display's work area, so the shipped 2560×720 pages
  keep their shape (`DockFirstRunThicknessDips`): `MulDiv(workAreaWidth, 720, 2560)` pixels, clamped to half the
  monitor like every dock, converted to DIPs at that display's effective DPI rounding down (so the runtime rescale
  stays within those pixels and does not clamp), and kept in 32–1080. On a 16:9 display that is half its height:
  720 DIPs on a 3840×2160 150 % display, 540 DIPs on a 1920×1080 100 % display. The one exception is a display under
  64 DIPs across the edge, where even the 32-DIP settings minimum is more than half of it: the runtime clamps that
  bar like any dock (one `dock-thickness-clamped` record).

The start logs one Info record (`dock-first-run`) naming the edge, the monitor selector, and the topology the bar was
decided from: the number of active displays and that display's rectangle, work area, and effective DPI. After a failed
discovery, or when the template cannot be patched, the plain template is installed; a `--settings` file and the
self-test never install. The file is not revisited later: a display added or removed, or a taskbar moved, changes
nothing in it until the user edits `dock` (for example `edge` to `none`), which applies live. A XENEON connected
afterwards does not take the bar either: `secondary` skips it while the second screen is there, and the bar stays until
`edge` is `none`.

## Notice windows

The settings-error dialog (`Specs/Core/Core_Settings.md` "Live reload and diagnostics") and the action-notice window
(`Specs/Plugins/Plugins_Actions.md` "Contract reading and collisions") are one window class with the text and an OK
button, created by one helper (`Application::CreateNoticeWindow`; the placement is `NoticeWindowRect` in
`RedXe/NoticeWindow.h`).

- A notice is 600×280 pixels centred on the RedXe window. A dock anchors it on the full bar, never on the peek strip
  of a collapsed autohide bar; a minimized window anchors it on its monitor's work area.
- The notice MUST lie inside the work area (`rcWork`) of the anchor's monitor: it is moved inside after centring, and
  cut to the work area only when the work area is smaller. A bar on any edge, a collapsed strip, or a window partly
  off its monitor therefore never puts the caption, the text, or the OK button off-screen.
- The settings-error dialog disables the RedXe window, focuses OK, and holds an autohide bar revealed; the action
  notice is a modeless tool window that leaves the dashboard enabled.

## Notification-area icon

`trayIcon` (`Specs/Core/Core_Settings.md`; omitted, Release shows the icon and Debug hides it) puts the product icon
in the Windows notification area for an interactive run. `RedXe/TrayIcon.*` owns the icon and its owner window;
`Application` decides when it exists and performs its commands.

- The owner is a hidden top-level `WS_POPUP` tool window of its own class (`RedXe.TrayIcon`), never shown. It is not
  the dashboard window: a notification-area menu must belong to the foreground window, and foregrounding the dashboard
  would reveal an autohide dock and move the focus to a fullscreen XENEON window. A message-only window could neither
  take the foreground nor hear the `TaskbarCreated` broadcast.
- The icon is one `Shell_NotifyIconW` entry identified by the owner and ID 1, never by a GUID (a GUID binds the icon to
  one executable path, which portable copies and the Debug and Release builds do not share), with
  `NOTIFYICON_VERSION_4`, the tooltip `RedXe` (`RedXe (Debug)` in Debug builds), and `IDI_REDXE` at the small-icon
  size for the owner's DPI, reloaded on `WM_DPICHANGED`. `NIM_SETVERSION` follows every add or update, and the icon
  counts as added only once it succeeds, so the next attempt repairs an icon left on version 0 (whose right-click opens
  nothing). An add refused because the taskbar still shows the icon becomes an update; an add the shell reports as
  timed out (last error `ERROR_TIMEOUT`, a busy Explorer) is not followed by an update, which would block the UI thread
  for a second shell timeout.
- When the shell refuses the icon and no taskbar exists (as at sign-in) the owner stays and adds it when
  `TaskbarCreated` arrives, which is also how the icon returns after Explorer restarts; `ChangeWindowMessageFilterEx`
  admits that message for an elevated run. When a running taskbar refuses the icon or times out, the owner MUST try
  again on one one-shot timer, at most four times with doubling delays from 1 s (`TrayIconAddRetryDelayMilliseconds`),
  and then arm nothing more; `TaskbarCreated` and every `Show` start the tries again, and a success ends them.
- The owner ignores `WM_CLOSE` (Alt+F4 while it is the foreground window after its menu), so only `Hide` ends it, and
  it deletes the icon (`NIM_DELETE`) on `WM_DESTROY`, while it still exists. Every way the owner ends therefore removes
  the icon, even after an add that looked refused, and no icon outlives a clean exit.
- A double-click, or Enter or Space on the keyboard-focused icon (`NIN_KEYSELECT`), opens the settings file this
  process watches (the default file or the `--settings` file) with its default app, the editor associated with
  `.json`: `ShellExecuteExW` with the default verb and the shell's UI enabled, so a file type without an association
  offers the Open With picker and a missing file is reported rather than ignored. The call runs on the host's launch
  worker (`Specs/Plugins/Plugins_Actions.md` "Launch worker"), never on the UI thread: the picker, a shell error box,
  or a `--settings` file on an unreachable share MUST NOT stop the dashboard from presenting. An edit within the
  double-click time of the previous one is dropped, because Enter reports `NIN_KEYSELECT` twice
  (`TrayIconActionFor`). Unlike the `redxe.settings.edit` action, this works while the settings-error dialog is open,
  when the file most needs editing.
- The context-menu request (right-click, Shift+F10, or the menu key: `WM_CONTEXTMENU` at the shell's anchor point)
  opens a menu with **Edit settings**, the default item drawn bold and the same as a double-click, and **Exit**, which
  closes RedXe like `WM_CLOSE`. The owner is foregrounded before the menu and posts itself `WM_NULL` after it. A menu
  closed without a choice while the owner is still the foreground window (Esc) MUST return the keyboard focus to the
  notification area (`NIM_SETFOCUS`); a click on another window keeps the foreground it moved. The menu runs the
  system's modal menu loop on the UI thread, so the dashboard presents no frame while it is open. Single clicks, hover,
  and balloon events do nothing.
- Commands reach the main window as a posted `TrayIcon::kCommandMessage`, never as a call from the owner's window
  procedure, so Exit never destroys the owner from inside its own procedure.
- `Run` shows the icon once the main window is shown, and every later settings apply with `trayIcon` on calls `Show`
  again, which costs no shell call for an icon that is there and tries again for one that is missing; a live reload
  that changes `trayIcon` shows or removes it without rebuilding the page; `CloseMainWindow` removes it (`NIM_DELETE`)
  and destroys the owner first. `--self-test` never shows it, whatever the document says; a `--screenshot` run shows
  it like any interactive run. The fatal-process path does not call the shell, so after a crash the icon remains until
  the pointer passes over it.
- A failure to create the owner or to add the icon is one Warning record (`tray-icon-failed`) when the outcome
  changes, so an apply that meets the same failure again logs nothing; a failed launch of the editor (refused by the
  launch worker, or failed in the shell there) is one Warning record (`tray-edit-settings-failed`); neither affects
  the dashboard.

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
- A live reload MUST NOT size a rebuilt page from the 0×0 client of a minimized standard window. A valid save that
  would rebuild the active page without switching the window kind waits while the window is minimized, neither
  applied nor rejected, and is read again once when a `WM_SIZE` other than `SIZE_MINIMIZED` restores it. Every other
  save applies at once: an invalid one opens the settings-error dialog, one that rebuilds no page applies in place,
  and a window-kind switch restores the window itself ("Switching the window kind").
- A live reload or `redxe.settings.reload` that arrives inside the titled window's system move/size loop (between
  `WM_ENTERSIZEMOVE` and `WM_EXITSIZEMOVE`) MUST wait and run once after `WM_EXITSIZEMOVE`: the loop applies its own
  rectangle when it ends, which would undo a window-kind switch or a dock placement made inside it. The watcher's
  notification stays unacknowledged meanwhile, so later saves coalesce into that one reload.
- A run that ends with a startup or runtime failure exit code shows one error message box after the main window is
  gone (`RedXe/Main.cpp`), and that box MUST wait for the user: the `WM_QUIT` that destroying the window posted is
  discarded first, because a modal loop that retrieves it closes the box at once. An unattended run
  (`RedXeIsUnattendedRun`) MUST NOT show it, whatever its exit code (`RedXeShowsExitCodeBox`): `--self-test` reports
  through its exit code only, and a `--screenshot` run through its exit code (8 whenever it wrote no PNG), one Error
  record (`failure-exit`, naming the code) written before the process runtime shuts down, and a debugger line.
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
- Session end: `WM_QUERYENDSESSION` MUST return `TRUE`; RedXe never vetoes or delays a sign-out, restart, or
  shutdown. Windows may end the process as soon as `WM_ENDSESSION` with `wParam` `TRUE` returns, without `WM_CLOSE`
  and without `wWinMain` returning, so that message MUST close RedXe before it returns (`Application::OnEndSession`):
  one Info record (`session-ending`, naming a sign-out, a shutdown or restart, or a close that Windows requested for
  an update, `ENDSESSION_CLOSEAPP`), then `CloseMainWindow` exactly as `WM_CLOSE` runs it (widget settings collected,
  the last release of anything `keys.down` or `mouse.down` still holds made while the log still records a refusal,
  services stopped so the Logicon lane restores its devices, the tray icon and the app bar removed, which matters to an
  Explorer that keeps running), then the launch worker stopped as runtime shutdown stops it
  (`PluginHost::StopLaunches`: queued launches dropped, one still in the shell waited for at most
  `LaunchWorker::kStopMilliseconds` (1 s), once, and logged as `launch-stop-timeout`; `Plugins_Actions.md`), then the
  queued log lines written out within `kSessionEndLogFlushMilliseconds` (0.5 s). The service stop waits at most
  `kRedXeDeviceWorkerDrainMilliseconds` (3 s) per device lane, so with the one bundled lane and a responsive shell the
  whole teardown stays inside Windows' 5 s hung-application timeout: `kSessionEndMaximumMilliseconds` (4.5 s) counts
  only the device-lane drain, the launch stop, and the log flush, not the shell calls that remove the tray icon and
  the app bar. `wParam` `FALSE` (the end was cancelled) changes nothing. The rest of the process runtime teardown,
  `RedXePluginShutdown` included, is not guaranteed at session end; when it does run, it neither waits for a stuck
  launch nor logs it again.
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
(reserving proposal and shell re-trim, overlay against a work area with a taskbar or another bar, negative coordinates),
the app-bar registration (`TestDockAppBarRegistration`: `DockReservationFor` for both modes and both `reserveWorkArea`
values; the strip proposal for every edge, at negative coordinates, with a peek clamped to a thin bar, and unchanged by
a DPI change; the full bar grown from a strip the shell moved beside a taskbar or shortened by a side taskbar, with the
collapsed window equal to the reserved strip on every edge; and the `PlanDockAppBar` sequences: a first autohide
placement is `ABM_NEW`, `ABM_QUERYPOS`, `ABM_SETPOS` and no sequence holds `ABM_SETAUTOHIDEBAREX`, a pass that changes
nothing reserved sends nothing, a stale reservation or an edge, monitor, DPI, or peek change sends `ABM_QUERYPOS` and
`ABM_SETPOS` only, autohide ↔ a fixed reserving bar re-reserves without `ABM_REMOVE`, either to an overlay is
`ABM_REMOVE` then `ABM_NEW`, a registered overlay sends nothing even after a shell change, and an unregistered bar
registers first), the thickness scaling and clamp, the hidden strip, the grip and its accent for every edge, the resize
band for every edge and the dragged thickness (outer-edge distance, DPI, both clamps), MINMAXINFO, monitor selection for
every selector kind with the primary fallback (`secondary` before and after an enumerated primary, skipping a XENEON
that enumerates before the second screen, the XENEON when it is the only display that is not the primary, on a single
display, and every `SecondaryMonitorRank`), every autohide state-machine row including zero delays and hold precedence,
the scheduler row that a hidden dock waits after its one grip frame, a dock-kind swap chain (`DXGI_SCALING_NONE`)
presenting a tile frame and a grip frame while the client is smaller than the back buffer, the same swap chain created
at the full bar while the window is already the strip and recreated at the last resized bar, the rebuild of one renderer
and dashboard across kinds (`DXGI_SCALING_STRETCH` at the client, then `DXGI_SCALING_NONE` at the full bar, the same
widget instances presenting), the first-run thickness (`DockFirstRunThicknessDips`: 16:9 displays at 100, 125, and
150 %, an ultrawide capped at half its height, a 5:4 display, a side bar, negative coordinates, the minimum, DPI 0, the
rescale without a clamp at 175 %, and the display under 64 DIPs across where the minimum is still clamped), the
first-run offer (`DockFirstRunOffered`: refused after a failed discovery, with a XENEON, for a `--settings` file, and in
a remote session), the first-run monitor (`DockFirstRunMonitor`: the primary for zero or one display, `secondary` from
two), the shared edge (`DockDisplayTouchesEdge`: a display right above and right below, a partial overlap, negative
coordinates, and none for displays side by side, at a corner, or on a side edge), and the first-run edge
(`DockFirstRunEdge`: a display without a taskbar on either horizontal edge and a side taskbar taking `bottom`, a visible
taskbar at the bottom and at the top, an auto-hiding taskbar at either edge, both edges taken, a display under another
display with and without its own taskbar, one over another display, one between two displays, negative coordinates, and
`DockAutohideBarHoldsEdge` counting a registration only while its window exists), and the autohide slide
(`DockSlideDurationMilliseconds`: whole, partial, reversed, zero, and tiny travels; `DockSlideVisiblePixels`: exact
ends, clamped progress, the eased halfway points of a reveal and a hide, one-way motion within the travel;
`DockSlideContentOffset` for every edge), with a dock-kind swap chain presenting a slide frame at half the bar with
every tile translated and returning in place when the slide ends, and the collapsed and sliding bar
(`TestDockCollapsedBarPolicy`: `DockClampPeek` up to and past the bar, with the hidden rectangle and MINMAXINFO of a
peek larger than the bar; `DockDashboardCanvas` for a collapsed, a sliding, an unplaced, and a standard window; the
`PageOrWidgetChange` reveal and the holds that follow it; `DockSettleShift` for every edge and as the inverse of a top
or left translation; every `DockRouteInput` row; `DockForegroundCoversMonitor` for a full-screen window, a maximized
captioned window's overhang, a captionless one, another monitor, a spanning window, and a partial cover), proved on a
real dashboard too (`TestDockCanvasHitTesting`: mid-slide hits on the canvas reach the tile on screen on a bottom and a
translated top bar where the client size missed, a settled press keeps that tile only with the shift, and a raise on a
collapsed bar fills the full bar; `TestDashboardSlideOffsetRetry`: a native container that failed to move is moved again
by an unchanged offset, the zero one included);
`SettingsTests` proves the `dock` member with `animationMilliseconds` (0 through 1000, default 200), its rejections,
minor 2, the `secondary` selector in the document and on `--dock`, the `--dock*` grammar with its errors, the merge
precedence, `PatchDockThickness` (replace, create with the minor bump, range, re-parse), and the first-run install
(`Specs/Core/Core_Settings.md`). Live, on the machine's topology: a reserving bar shrinks `rcWork` by exactly its
thickness while it runs and restores it on exit; an overlay bar leaves `rcWork` alone and sits against the work-area
edge; a side bar on a monitor with a bottom taskbar ends above the taskbar; an autohide bar collapses to its strip after
the hide delay, reveals after the dwell when the real cursor rests on the strip, hides after the pointer leaves, and
reveals at once on a click; each edge slides in and out over `animationMilliseconds` with its inner edge leading and 0
restores the one-step change; `--screenshot` of an autohide bar yields the full bar; a live reload re-places thickness,
mode, and edge changes; a real mouse drag on the inner edge grows the bar live, commits the reservation on release, and
writes `dock.thickness` to the file. The 2026-09-19 closeout recorded all of these on a 3840×2160 150 % primary plus a
2560×720 150 % XENEON with bottom taskbars, before an autohide bar reserved its strip.

Changes to the app-bar registration or to what an autohide bar reserves additionally require a live check on a real
desktop, a manual check for the owner: a reserving autohide bar changes the work area of the desktop in use and a
reveal needs the real cursor, so automated and agent runs MUST NOT start one. With a first-run top autohide bar (or
`--dock top@<display> --dock-mode autohide`), a window maximized on that display has its caption buttons and tabs just
below the strip, not under it, and that display's `rcWork` is trimmed by exactly the peek (4 px by default) at the top;
resting the pointer on the strip reveals the full bar over the maximized window, which keeps its size, and the bar
hides back to the strip after the pointer leaves; exiting RedXe returns the work area and the maximized window grows
back to the screen edge. A bottom or side autohide bar leaves the status bar or scrollbar of a maximized window
uncovered the same way. These are not recorded yet.

Changes to the slide step, the dashboard canvas, or input on a collapsed or sliding bar additionally require a live
check on an autohide bar, because a reveal needs the real cursor or a bound control and `--screenshot` holds the bar
revealed: frame-by-frame captures of a top bar's reveal and hide show no background band at its leading edge and
native tiles moving with the GPU tiles; a `page.goto` and a `widget.toggle` key pressed on the collapsed bar reveal it
with the page changed or the widget raised over the full bar; a click on a tile while the bar is still sliding in
reaches that tile; and a wheel over the strip changes no page. These are manual checks and are not recorded yet.

Window-kind switches additionally require a live run with a portable `--settings` file edited while RedXe runs:
`none` → an edge and back yields the dock styles (`WS_EX_TOOLWINDOW`, topmost, no `WS_EX_APPWINDOW`) and then the
standard ones (`WS_OVERLAPPEDWINDOW`, `WS_EX_APPWINDOW`, not topmost) at the mode-table placement with its exact
client, without activation; a reserving bar's work area comes back when it switches to `none`; a minimized and a
maximized titled window become a bar and come back normal; a switched dock stays topmost while a full-screen window
is open on another display; the JSONL log holds one `device-created` and one `window-kind-changed` record per switch;
and `--screenshot` taken after a switch in either direction shows the tiles. The 2026-09-27 check recorded these on
a 3840×2160 150 % primary (NVIDIA) plus a 2560×720 100 % XENEON on the integrated GPU (AMD), with a Release RedXe
fullscreen on the XENEON.

Changes to the deferred reload additionally require a live run of the same kind: a titled window minimized without
activation while a save changes `backgroundColor` stays minimized and running with no `settings-apply-failed`
record, and once restored without activation its `--screenshot` shows the saved color; a save of only `dock.edge`
made while it is minimized still turns it into a bar at once. The 2026-10-07 check recorded these on the topology
above. A failed switch step needs a Direct3D, shell, or `SetWindowPos` fault and a move/size loop needs the real
cursor, so the rollback and the move/size deferral have no automated or live check.

First-run changes additionally require a live install without a XENEON, a manual check on a machine without one: on
two displays side by side with the taskbar on the primary only, the installed `dock` names `bottom` and `secondary`,
the `dock-first-run` record says so and names two active displays with the second screen's rectangle, and the bar
collapses to its strip at the bottom of the display that is not the primary; with the taskbar on every display it
names `top`. Recovery of an invalid default file installs no `dock`, and neither does a first start in a Remote
Desktop session. These are not recorded yet.

Changes to the dock's `TaskbarCreated` handling additionally require a live run, because `HostPluginTests` does not
build `Application` and `--self-test` never runs a dock: a Debug overlay bar (`--dock bottom@primary --dock-mode fixed
--dock-reserve off`) under `--screenshot`, with the registered `TaskbarCreated` message posted twice to its own window
(the running-Explorer case), logs two `dock-appbar-renewed` records and no `dock-appbar-refused`, leaves the foreground
where it was, and exits 0. The 2026-10-07 check recorded this on the topology above. A real Explorer restart with a
reserving bar and with an autohide bar up (the bar or the strip is reserved in the work area again, a full-screen
window on the bar's monitor puts it beneath again) is a manual check.

Placement-request and notice-window changes MUST keep `HostPluginTests` proving `BeginDockPlacement` and
`NextDockPlacementPass` (a request during a pass is recorded, not run, and replayed as one more pass with the recorded
resize flags and never the first pass's; a request that every pass raises again stops after
`kDockMaximumExtraPlacementPasses`) and `NoticeWindowRect` (a window inside its work area keeps the notice centred on
it; a collapsed top or bottom strip, a top bar, and left and right bars keep it inside the work area; negative
coordinates; a work area smaller than the notice; an empty work area). A display change during a placement needs a
hot-plug or a real shell delay, so the replay itself has no live check.

Unattended-run changes MUST keep the bounded `test.ps1` command-line error step green, and additionally require a
live run: a Debug overlay bar (`--dock bottom@primary --dock-mode fixed --dock-reserve off`) under `--screenshot`
closed by a posted `WM_CLOSE` during its delay exits 8 with a `screenshot-failed` and a `failure-exit` record, shows no
message box, and leaves the foreground where it was. The 2026-10-07 check recorded this on the topology above. A
startup failure exit (1, 2, 3, 5, or 7), the settings fallback notice, a present crash marker, and the Release
missing-display path under `--screenshot` have no automated or live check.

Session-end changes MUST keep the `--self-test` step green: `WM_QUERYENDSESSION` and a cancelled `WM_ENDSESSION` sent to
its hidden window keep the window, renderer, page, and services, and `WM_ENDSESSION` with `wParam` `TRUE` returns with
the window destroyed, the page released, every service and device lane stopped, and no launch worker left, within
`kSessionEndMaximumMilliseconds`. `HostPluginTests` (`TestLaunchWorker`) proves the launch stop a session end makes: it
waits the bound once for a stuck launch and logs `launch-stop-timeout`, and the runtime shutdown after it neither waits
nor logs again. Because the self-test has no log writer, they additionally require a live run: a Debug overlay bar
(`--dock bottom@primary --dock-mode fixed --dock-reserve off`) running the Zoom service under `--screenshot`, sent both
messages with `ENDSESSION_LOGOFF` the way Windows sends them, answers `TRUE`, returns from `WM_ENDSESSION` with its
window destroyed and its JSONL log already holding `session-ending` followed by `service-stopped`, leaves the foreground
where it was, and exits by itself (8: the run ended before its capture). The 2026-10-07 check recorded this on the
topology above (`WM_ENDSESSION` returned after 26 ms, and after 29 ms once the session end also stopped the launch
worker). A real sign-out, restart, or shutdown with a Logicon keypad and dialpad bound (the keypad shows the Logi splash
on the sign-in screen, and the dialpad buttons RedXe bound work normally again) is a manual check.

Notification-area icon changes MUST keep `HostPluginTests` proving the callback table (`TrayIconActionFor`: a
double-click and `NIN_KEYSELECT` edit, `WM_CONTEXTMENU` opens the menu, single clicks, hover, and balloon events do
nothing, an edit within the double-click time of the previous one is dropped, and an earlier tick never blocks one),
the retry schedule (`TrayIconAddRetryDelayMilliseconds`), and the owner against a scripted shell that never reaches the
taskbar (`TestTrayIconOwner`: `Show` and `Hide` idempotent and the class unregistered, `WM_CLOSE` ignored, `NIM_DELETE`
on every destruction while the owner exists, `TaskbarCreated` adding again, retries only while a taskbar exists and no
timer after the last, no update after a timed-out add, and `NIM_SETVERSION` after every add or update before the icon
counts as added), and `SettingsTests` proving `trayIcon` (`Specs/Core/Core_Settings.md`). Because the shell is not
automated, they additionally require a live check: a Release run with the shipped template shows the icon with the
`RedXe` tooltip (`Shell_NotifyIconGetRect` finds it); a double-click, and Enter on the keyboard-focused icon, open the
settings file once in the default `.json` editor; a right-click and Shift+F10 show Edit settings (bold) and Exit, a
click elsewhere closes the menu, and Esc after Shift+F10 leaves the keyboard focus on the notification area; Exit
quits and removes the icon; saving `"trayIcon": false` removes the icon and `true` brings it back without a restart;
restarting Explorer brings it back; and a Debug run with the shipped template shows none.

## Implementation and validation anchors

- Process awareness and command-line modes: `RedXe/Main.cpp`, `RedXe/app.manifest`; the switch catalog behind
  `--help`: `RedXe/CommandLine.h`
- Display discovery, window creation, and DPI transitions: `RedXe/Application.cpp`, `RedXe/Application.h`; live
  window-kind switches: `Application::SwitchWindowKind` (`RestyleWindowKind`, `RebuildPresentation`,
  `FinishWindowKindSwitch`), `PlaceStandardWindow`, the dock-only reload and its rollback in `ApplyDockSettings`, and
  the combined page-and-kind reload in `ApplySettings`; the deferred reload: `OnSettingsChanged` and
  `ReplayDeferredSettingsReload`; the failure message box and the unattended runs that never show it:
  `RunApplication` in `RedXe/Main.cpp` and `Application::SetUnattended`; the first-run dock:
  `MakeFirstRunDock` in `RedXe/Application.cpp`; session end: `Application::OnEndSession`
- Dock placement, monitor selection, MINMAXINFO, the autohide state machine, the slide, and the first-run monitor,
  edge, and thickness: `RedXe/DockPlacement.h`; what each mode reserves and the registration messages of a placement
  pass: `DockReservationFor` and `PlanDockAppBar` in `RedXe/DockPlacement.h`, sent by `Application::PlaceDockPass`
  with the record in `Application::_dockAppBar`; the `--dock*` grammar and merge: `RedXe/DockOptions.h`; dock-kind
  presentation: `Renderer::SetDockPresentation`; the slide's frames and translation: `Application::TickDockSlide`,
  `SettleDockSlide`, and `DashboardHost::SetSlideOffset`; the dashboard canvas: `Application::DashboardCanvasSize`;
  the reveal before a page or widget change: `Application::RevealDockForChange`; input on a collapsed or sliding bar:
  `Application::BeginDockPress` and `DockPressPoint` in `HandleMessage` and `TryPointerClientPosition`; the app-bar
  renewal after an Explorer restart: `Application::OnTaskbarCreated`; placement requests during a placement:
  `Application::PlaceDock` over `PlaceDockPass`
- Notice windows: `Application::CreateNoticeWindow`, used by `ShowSettingsError` and `ShowActionNotices`; their
  placement: `RedXe/NoticeWindow.h`
- Notification-area icon: `RedXe/TrayIcon.h` (the owner window, the icon, the menu, the `TrayIconActionFor`
  callback table, and the `TrayIconAddRetryDelayMilliseconds` schedule), `RedXe/TrayIcon.cpp`; its lifetime and
  commands: `Application::ApplyTrayIconSettings`, `OnTrayCommand`, and `EditSettingsFile`, which hands the editor
  launch to `RedXe/LaunchWorker.*`
- Physical render-target resizing: `RedXe/Renderer.cpp`, `RedXe/Renderer.h`
- Automated build, scheduler, production host/plugin, and hidden WARP validation: `build.ps1`, `test.ps1`,
  `Tests/HostPluginTests/`
