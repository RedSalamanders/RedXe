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
| Release without active XENEON | Show the missing-display Yes/No warning. Yes creates the standard titled fallback window; No exits successfully without creating the main window. A default settings file installed at this start carries the first-run dock ("First start without a XENEON"), so the Dock row applies instead. |
| Self-test | Skip display discovery and prompts, create the titled window hidden, validate its DPI-adjusted client dimensions, render one frame, and exit: 0 when every check passed, 6 when one failed, after naming it, with its HRESULT when there is one, on the debugger output and on stderr. A failed Debug runtime check ends the run with its report on stderr and exit code 3, and any other `abort()` with exit code 4 ([`Build_Process.md`](../Build/Build_Process.md)); neither opens a dialog. |
| Screenshot (`--screenshot <png> [--page <id>] [--widget <ordinal>] [--after <ms>]`) | Run exactly as the configuration above prescribes (same discovery, placement, services, and frame loop), jump to the named page through the host `PageGoTo` action once the renderer is live and no settle runs, wait the delay (default 3000 ms, 1–120000) with the frame loop idle-waiting as usual, capture the main window through `Common/WindowCapture.cpp` on a capture worker (Windows.Graphics.Capture of an owned, visible window; a widget ordinal crops to that tile's `PixelBoundsAt` in client space, mapped through the DWM extended frame bounds), then close. The UI thread continues handling input and timers while capture waits for its first frame. Exit 0 with the PNG written, 8 when the capture failed (no modal prompt). It MUST NOT activate, move, or resize the window, move the cursor, or send input. |
| Dock (`dock.edge` other than `none`, or `--dock <edge>[@<monitor>]`) | Debug and Release alike: create the dock window kind below on the selected monitor instead of the row that would otherwise apply, and skip the missing-display prompt. A live reload that turns the dock on or off switches the running window between this row and the one that would otherwise apply ("Switching the window kind"). `--self-test` ignores the dock. |
| Help (`--help`, `-h`, `/?`, `-?`) | Print the command-line catalog and exit 0 before any other switch is read: to the console the process was started from (a GUI process attaches to its parent's), to a redirected stdout as UTF-8, or, without either, to a message box. Every other token on the line MUST be a catalogued switch or the value of one; the first unknown token is a command-line error (exit 2, `Unknown argument "<token>". Run RedXe.exe --help for the command line.`) shown the same way, never as a message box when `--self-test` is on the line. |

The command line is declared once in `RedXe/CommandLine.h`: the catalog `--help` prints and the names `Main.cpp`
parses through, so a switch cannot exist without an entry. Adding, renaming, or removing a switch changes that
catalog, the "Command line" section of `docs/usage.md`, and the owning row of this table in the same change;
`SettingsTests` pins the catalog (unique well-formed names, every entry printed, the help aliases, the unknown-token
scanner) and `test.ps1` runs `--help` through a redirected stdout and an unknown switch under `--self-test`.

Debug and Release display discovery MUST inspect active display paths through
`QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS)`. A target friendly name containing `XENEON` or `CORSAIR`, compared
ordinally without case, qualifies. The matching source device MUST be resolved to its `HMONITOR` bounds. Discovery
failure uses normal shell placement in Debug and follows the missing-display fallback policy in Release.

## Dock window kind

The dock is RedXe as a bar along one edge of one monitor. The **effective dock** is the settings document's `dock`
object (`Specs/Core/Core_Settings.md`) with the `--dock*` command-line overrides applied, and the window kind follows
it at startup and on every live reload: an `edge` other than `none` selects the dock in both configurations, on any
monitor; the XENEON is only what the `xeneon` selector resolves to. `RedXe/DockPlacement.h` owns every pure rule
below (placement, monitor selection, MINMAXINFO, the autohide state machine and slide, the first-run monitor, edge,
and thickness) and
`RedXe/DockOptions.h` the command line and the merge; `HostPluginTests` and `SettingsTests` prove them without a
display topology.

### Command line

Every switch is a `<switch> <value>` pair; a repeated switch, a missing value, or an invalid value is a command-line
error (exit 2 with the usual message box). Each present switch replaces the same-named document member for the
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
  user's current window keeps the focus. Exit is Escape while the bar has focus (click or tap it first), `redxe.quit`,
  or `WM_CLOSE`; there is no close glyph.
- Activation is unchanged (`MA_ACTIVATE` on click or touch; hover never activates).
- The dashboard and swap chain are always sized to the **full** bar rectangle; an autohide strip is a window-size
  change only (below). `OnSize` never resizes the dashboard in dock mode; `PlaceDock` does, through the full
  rectangle, on DPI, monitor, thickness, and edge changes. A swap chain created while the window is the strip (a
  live reload that rebuilds the page of a collapsed bar, a device rebuild) is still created at the full bar, the
  size `Renderer::Resize` last received.
- `WM_GETMINMAXINFO` answers with the peek strip (autohide) or the full bar (fixed) as the minimum and the monitor as
  the maximum, because Windows applies `ptMinTrackSize` to `SetWindowPos` as well as to user tracking and the titled
  window's 480×320 minimum would refuse the strip.
- The reachable client for the edge bands is the whole client: the bar fits its monitor by construction, and a
  reserving side dock is excluded from the work area, which would otherwise leave nothing reachable.
- Z-order is topmost; a full-screen window on the dock's monitor drops the bar to `HWND_BOTTOM` and the clearing
  notification restores `HWND_TOPMOST`. `ABN_FULLSCREENAPP` names no monitor and the shell also sends it for a
  full-screen window on another display (a XENEON dashboard, a video on a second screen), so the bar steps down only
  while the shell reports one and the foreground window covers the dock's `rcMonitor` (`DockYieldsToFullscreen`).
- `--screenshot` captures the dock like any window; an autohide dock is held revealed for the whole run (a pending
  capture is a hold), so the PNG shows the bar.
- Drag-to-resize: the inner edge of a revealed bar (the side facing the desktop) is a host-owned grip band
  `kDockResizeBandDips` (6) deep (`DockResizeBandRect`); it shows the size cursor, and a mouse press there captures
  the pointer and drags the thickness (`DockThicknessFromDrag`: the distance from the outer edge in DIPs at the
  monitor DPI, keeping the offset under the pointer, clamped to 32–1080 and half the monitor). During the drag the
  window and dashboard follow the pointer live with the outer edge fixed and the shell reservation untouched; the
  release (or a lost capture) commits through `PlaceDock`, replaces a `--dock-thickness` pin for the run, and
  persists `dock.thickness` into the settings document (`SettingsStore::PersistDockThickness`, which creates the
  `dock` object and raises `version.minor` to 2 when needed; a failed write is one Warning record,
  `dock-thickness-persist-failed`). A drag is a hold for an autohide bar. Touch and pen do not resize.

### Monitor and placement

- `monitor` resolves over `EnumDisplayMonitors`: `primary` → the primary display (`MONITORINFOF_PRIMARY`);
  `secondary` → the second screen, the first display in enumeration order that is not the primary; `<n>` → the n-th
  in enumeration order (1-based); `name:<substring>` → an ordinal case-insensitive substring of the
  `QueryDisplayConfig` friendly name or of the GDI device name (`\\.\DISPLAYn`); `xeneon` → the display XENEON
  discovery found. A selector that resolves to nothing (`secondary` on a single display included) falls back to the
  primary display with one Warning log record (`dock-monitor-fallback`) and no prompt; `WM_DISPLAYCHANGE` re-runs
  discovery and the selector, so the bar returns when its monitor does.
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
  docks). Neither a reveal nor a hide recomputes the layout, calls `OnTargetSizeChanged`, or resizes the buffers.
- A reveal and a hide **slide** over `animationMilliseconds` (`Specs/Core/Core_Settings.md`; default 200, 0 keeps one
  `SetWindowPos` and one frame). Each presented frame sizes the window to the outer `v` pixels of the full bar
  (`DockHiddenRect` at the visible thickness `v`), eased out from the strip to the full bar for a reveal and eased in
  back to the strip for a hide (`DockSlideVisiblePixels`). A bottom or right bar slides as it is, because the buffer's
  top-left follows the window's moving inner edge; a top or left bar's dashboard is translated back by the part still
  hidden (`DockSlideContentOffset`, `DashboardHost::SetSlideOffset`, native containers included), so every bar leads out
  of the screen edge with its inner edge and no window ever leaves its monitor. A slide is visible motion: the host
  presents every frame until it ends (the scheduler's motion input), then blocks as before. The dashboard is visible
  from the first frame of a reveal and until the last frame of a hide, which the grip frame follows. The reveal state
  stays the authority: when it flips during a slide, a new slide starts from where the bar is and takes the share of
  `animationMilliseconds` its distance is of the whole travel (`DockSlideDurationMilliseconds`). A mouse move, touch,
  pen, or mouse-button contact on a bar sliding out counts as on the strip (dwell or immediate reveal); a contact, a
  wheel, a page change, a raise, a dock drag, a placement, and a live reload first complete a slide in progress at the
  size the reveal state asks for, so they work on settled geometry. A `--screenshot` capture waits for a slide to end.
- States and holds are the `DockPlacement.h` table: `Revealed`, `HidePending` (one-shot hide timer), `Hidden`, and
  `RevealPending` (one-shot dwell timer). A real mouse move over the strip starts the dwell (`revealDelayMilliseconds`;
  0 reveals at once); leaving during the dwell hides again; a touch, pen, or mouse-button contact on the strip and
  `redxe.dock.show` / `redxe.dock.toggle` reveal at once. A revealed bar stays while any hold is active — the pointer
  inside, the window active, a pan/settle/staged neighbour/interactive capture/raise settle, a raised widget, the
  settings-error dialog, a pending screenshot, a reveal still sliding out of the edge — and arms `hideDelayMilliseconds`
  when the last hold clears (0 hides at once); a hold returning cancels the timer. `redxe.dock.hide` and `toggle`
  collapse a revealed bar even with the pointer inside but are inert while a raise, capture, dialog, or pin holds it. A
  bar revealed by `redxe.dock.show` with nothing holding it stays until some other hold appears and clears (its own
  slide is not one). Mouse messages synthesized from touch or pen never start the dwell. At most one one-shot timer is
  armed and every state exit kills it.
- `Hidden` and `RevealPending`, once any slide has ended, are "not visible" for `UpdateDashboardVisibility` and the
  frame scheduler: widgets `SetVisible(FALSE)`, native containers hidden, keyboard focus and interactive capture
  cleared, edge bands gone, and after the single **grip frame** (dashboard clear color plus the host-chrome wash over
  the strip and a one-DIP accent line on its desktop-facing side, two quads) the host blocks on messages like a
  minimized window. The full-size swap chain is retained while hidden so a reveal presents at once, without rebuilding
  it.
- A live reload applies every `dock` member in place (thickness, edge, monitor, mode, reserve, peek, delays, slide) by
  re-placing and, when the registration row changed, re-registering; an edge change that flips orientation reflows
  the dashboard through the full rectangle. Switching between `none` and an edge switches the window kind live (next
  section) while the rest of the document applies as usual. Command-line-pinned members are re-applied after every
  merge, so `--dock none` or `--dock <edge>` keeps the kind for the run.
- During an inner-edge drag the window follows the pointer. Dashboard and swap-chain resize work is coalesced to
  at most one callback per 16 ms, with the final dimensions flushed on release before persisting thickness. The
  shell work-area reservation is committed on release.

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
- A switch logs one Info record (`window-kind-changed`). A failed step logs one Error record
  (`window-kind-switch-failed`) and closes the window like any other runtime failure (exit code 5).
- `--self-test` pins the edge to `none`, so a document `dock` never switches its hidden titled window.

### First start without a XENEON

When RedXe installs its default settings file at startup (the file is missing, or the recovery of an invalid file;
`Specs/Core/Core_Settings.md` "Cold load and recovery") and XENEON discovery succeeded without finding a display, the
installed document carries the **first-run dock**: `{ "edge": E, "monitor": M, "mode": "autohide", "thickness": T }`,
so this start and the following ones show the Dock row of the mode table instead of the Release missing-display
prompt or the Debug titled window. `MakeFirstRunDock` measures the displays once, at that install:

- `M` is the second screen when more than one display is active (`DockFirstRunMonitor`): `secondary`, which
  resolves like every selector at runtime, so the bar follows whichever display is not the primary. With one display
  it is `primary`. The primary monitor is identified by `MONITORINFOF_PRIMARY` during enumeration, even when another
  monitor contains screen coordinate `(0,0)`, and the second screen is chosen in the enumeration order the
  `secondary` selector uses.
- `E` is the horizontal edge the taskbar leaves free on that display (`DockFirstRunEdge`): `top`, unless the top is
  taken and the bottom is not, then `bottom`. An edge is taken when the display's work area is trimmed on that side
  (a taskbar that stays visible, or any reserving app bar) or when an autohide bar is registered on that edge of that
  display (`ABM_GETAUTOHIDEBAREX`: an auto-hiding taskbar). A display with neither, one without a taskbar of its own,
  follows the primary taskbar's edge (`ABM_GETTASKBARPOS`), so the bar sits opposite the taskbar the person uses; a
  side taskbar, both edges taken, or no taskbar at all (Explorer not started) gives `top`. Whenever one horizontal
  edge is free, the autohide strip therefore sits at the screen edge rather than beside a taskbar.
- `T` gives the bar the XENEON EDGE's 32:9 proportions along that display's work area, so the shipped 2560×720 pages
  keep their shape (`DockFirstRunThicknessDips`): `MulDiv(workAreaWidth, 720, 2560)` pixels, clamped to half the
  monitor like every dock, converted to DIPs at that display's effective DPI rounding down (so the runtime rescale
  stays within those pixels and does not clamp), and kept in 32–1080. On a 16:9 display that is half its height:
  720 DIPs on a 3840×2160 150 % display, 540 DIPs on a 1920×1080 100 % display. The one exception is a display under
  64 DIPs across the edge, where even the 32-DIP settings minimum is more than half of it: the runtime clamps that
  bar like any dock (one `dock-thickness-clamped` record).

The start logs one Info record (`dock-first-run`) naming the edge and the monitor selector. After a failed
discovery, or when the template cannot be patched, the plain template is installed; a `--settings` file and the
self-test never install. The file is not revisited later: a XENEON connected afterwards, a display added or removed,
or a taskbar moved changes nothing in it until the user edits `dock` (for example `edge` to `none`), which applies
live.

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
  size for the owner's DPI, reloaded on `WM_DPICHANGED`. When the shell refuses the icon (no taskbar yet, as at
  sign-in) the owner stays and adds it when `TaskbarCreated` arrives, which is also how the icon returns after Explorer
  restarts; `ChangeWindowMessageFilterEx` admits that message for an elevated run. An add refused because the taskbar
  still shows the icon becomes an update, and removal always deletes, so no icon outlives a clean exit.
- A double-click, or Enter or Space on the keyboard-focused icon (`NIN_KEYSELECT`), opens the settings file this
  process watches (the default file or the `--settings` file) with its default app, the editor associated with
  `.json`: `ShellExecuteExW` with the default verb and the shell's UI enabled, so a file type without an association
  offers the Open With picker and a missing file is reported rather than ignored. An edit within the double-click time
  of the previous one is dropped, because Enter reports `NIN_KEYSELECT` twice (`TrayIconActionFor`). Unlike the
  `redxe.settings.edit` action, this works while the settings-error dialog is open, when the file most needs editing.
- The context-menu request (right-click, Shift+F10, or the menu key: `WM_CONTEXTMENU` at the shell's anchor point)
  opens a menu with **Edit settings**, the default item drawn bold and the same as a double-click, and **Exit**, which
  closes RedXe like `WM_CLOSE`. The owner is foregrounded before the menu and posts itself `WM_NULL` after it. The menu
  runs the system's modal menu loop on the UI thread, so the dashboard presents no frame while it is open. Single
  clicks, hover, and balloon events do nothing.
- Commands reach the main window as a posted `TrayIcon::kCommandMessage`, never as a call from the owner's window
  procedure, so Exit never destroys the owner from inside its own procedure.
- `Run` shows the icon once the main window is shown; a live reload that changes `trayIcon` shows or removes it
  without rebuilding the page; `CloseMainWindow` removes it (`NIM_DELETE`) and destroys the owner first. `--self-test`
  never shows it, whatever the document says; a `--screenshot` run shows it like any interactive run. The
  fatal-process path does not call the shell, so after a crash the icon remains until the pointer passes over it.
- A failure to create the owner or to add the icon is one Warning record (`tray-icon-failed`), and a failed launch of
  the editor one Warning record (`tray-edit-settings-failed`); neither affects the dashboard.

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
coordinates), the thickness scaling and clamp, the hidden strip, the grip and its accent for every edge, the resize
band for every edge and the dragged thickness (outer-edge distance, DPI, both clamps), MINMAXINFO, monitor selection
for every selector kind with the primary fallback (`secondary` before and after an enumerated primary, and on a single
display), every autohide state-machine row including zero delays and hold
precedence, the scheduler row that a hidden dock waits after its one grip frame, a dock-kind swap chain
(`DXGI_SCALING_NONE`) presenting a tile frame and a grip frame while the client is smaller than the back buffer, the
same swap chain created at the full bar while the window is already the strip and recreated at the last resized bar,
the rebuild of one renderer and dashboard across kinds (`DXGI_SCALING_STRETCH` at the client, then
`DXGI_SCALING_NONE` at the full bar, the same widget instances presenting), the first-run thickness
(`DockFirstRunThicknessDips`: 16:9 displays at 100, 125, and 150 %, an ultrawide capped at half its height, a 5:4
display, a side bar, negative coordinates, the minimum, DPI 0, the rescale without a clamp at 175 %, and the display
under 64 DIPs across where the minimum is still clamped), the first-run monitor (`DockFirstRunMonitor`: the primary
for zero or one display, `secondary` from two), and the first-run edge (`DockFirstRunEdge`: a visible taskbar at the
bottom and at the top, an auto-hiding taskbar at either edge, a display without a taskbar following the primary
taskbar's edge and `top` when that is unknown, a side taskbar, both edges taken, negative coordinates, and the ABE
mapping back to edges), and the autohide slide (`DockSlideDurationMilliseconds`: whole, partial, reversed, zero,
and tiny travels; `DockSlideVisiblePixels`: exact ends, clamped progress, the eased halfway points of a reveal and a
hide, one-way motion within the travel; `DockSlideContentOffset` for every edge), with a dock-kind swap chain
presenting a slide frame at half the bar with every tile translated and returning in place when the slide ends;
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
2560×720 150 % XENEON with bottom taskbars.

Window-kind switches additionally require a live run with a portable `--settings` file edited while RedXe runs:
`none` → an edge and back yields the dock styles (`WS_EX_TOOLWINDOW`, topmost, no `WS_EX_APPWINDOW`) and then the
standard ones (`WS_OVERLAPPEDWINDOW`, `WS_EX_APPWINDOW`, not topmost) at the mode-table placement with its exact
client, without activation; a reserving bar's work area comes back when it switches to `none`; a minimized and a
maximized titled window become a bar and come back normal; a switched dock stays topmost while a full-screen window
is open on another display; the JSONL log holds one `device-created` and one `window-kind-changed` record per switch;
and `--screenshot` taken after a switch in either direction shows the tiles. The 2026-09-27 check recorded these on
a 3840×2160 150 % primary (NVIDIA) plus a 2560×720 100 % XENEON on the integrated GPU (AMD), with a Release RedXe
fullscreen on the XENEON.

First-run changes additionally require a live install without a XENEON: on a machine with two displays and a bottom
taskbar the installed `dock` names `top` and `secondary`, the `dock-first-run` record says so, and the bar collapses
to its strip at the top of the display that is not the primary.

Notification-area icon changes MUST keep `HostPluginTests` proving the callback table (`TrayIconActionFor`: a
double-click and `NIN_KEYSELECT` edit, `WM_CONTEXTMENU` opens the menu, single clicks, hover, and balloon events do
nothing, an edit within the double-click time of the previous one is dropped, and an earlier tick never blocks one)
and `SettingsTests` proving `trayIcon` (`Specs/Core/Core_Settings.md`). Because the shell is not automated, they
additionally require a live check: a Release run with the shipped template shows the icon with the `RedXe` tooltip
(`Shell_NotifyIconGetRect` finds it); a double-click, and Enter on the keyboard-focused icon, open the settings file
once in the default `.json` editor; a right-click and Shift+F10 show Edit settings (bold) and Exit, and a click
elsewhere closes the menu; Exit quits and removes the icon; saving `"trayIcon": false` removes the icon and `true`
brings it back without a restart; restarting Explorer brings it back; and a Debug run with the shipped template shows
none.

## Implementation and validation anchors

- Process awareness and command-line modes: `RedXe/Main.cpp`, `RedXe/app.manifest`; the switch catalog behind
  `--help`: `RedXe/CommandLine.h`
- Display discovery, window creation, and DPI transitions: `RedXe/Application.cpp`, `RedXe/Application.h`; live
  window-kind switches: `Application::SwitchWindowKind` (`RestyleWindowKind`, `RebuildPresentation`,
  `FinishWindowKindSwitch`), `PlaceStandardWindow`, and the combined page-and-kind reload in `ApplySettings`; the
  first-run dock: `MakeFirstRunDock` in `RedXe/Application.cpp`
- Dock placement, monitor selection, MINMAXINFO, the autohide state machine, the slide, and the first-run monitor,
  edge, and thickness: `RedXe/DockPlacement.h`; the `--dock*` grammar and merge: `RedXe/DockOptions.h`; dock-kind
  presentation: `Renderer::SetDockPresentation`; the slide's frames and translation: `Application::TickDockSlide`,
  `SettleDockSlide`, and `DashboardHost::SetSlideOffset`
- Notification-area icon: `RedXe/TrayIcon.h` (the owner window, the icon, the menu, and the `TrayIconActionFor`
  callback table), `RedXe/TrayIcon.cpp`; its lifetime and commands: `Application::ApplyTrayIconSettings`,
  `OnTrayCommand`, and `EditSettingsFile`
- Physical render-target resizing: `RedXe/Renderer.cpp`, `RedXe/Renderer.h`
- Automated build, scheduler, production host/plugin, and hidden WARP validation: `build.ps1`, `test.ps1`,
  `Tests/HostPluginTests/`
