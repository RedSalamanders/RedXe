# RFC: Screen-edge dock — a fixed or auto-hiding RedXe bar on any monitor

Status: DECISION — proposed, nothing implemented; the current window modes in
[`UI_XeneonDisplayWindowing.md`](../../UI/UI_XeneonDisplayWindowing.md) remain the contract until this RFC is decided
Date: 2026-09-18
Requested deliverables: settings and command-line switches that place RedXe as a bar on one edge of a chosen monitor
in a **fixed** mode, with the choice of shrinking the maximize (work) area or not, and an **autohide** mode that leaves
a few pixels visible and brings the bar back when the mouse reaches them
Proposed identity: a third window kind, **dock**, owned by `Application`; `RedXe/DockPlacement.h` (pure geometry,
monitor resolution, and the autohide state machine); `RedXe/DockOptions.h` (command-line parse and settings merge);
the `dock` root settings member (major 5, minor 2); the `--dock*` switch family; `redxe.dock.*` actions

## Purpose and authority

RedXe today owns one monitor completely (Release: a borderless popup filling the XENEON) or lives in an ordinary
titled window (Debug and the Release fallback). Neither shape lets the dashboard sit beside the user's other windows
the way the Windows taskbar does: a strip along one screen edge that maximized windows respect, or that steps out of
the way and returns when the pointer touches the edge.

This RFC proposes a **dock** window kind:

1. **Fixed dock.** A borderless, topmost bar spanning one edge (`top`, `bottom`, `left`, `right`) of one monitor
   selected by the existing monitor-selector grammar. With `reserveWorkArea` on, RedXe registers as a shell app bar
   (`SHAppBarMessage`) so the monitor's work area shrinks and maximized windows stop at the bar. With it off, the bar
   overlays the work area and maximized windows pass beneath it.
2. **Autohide dock.** The same bar, but it collapses to a `peek`-pixel strip at the screen edge and reveals when the
   mouse dwells on that strip (or a finger taps it, or an action asks for it), then hides again once the pointer has
   left and the bar is no longer active. Nothing polls and nothing hooks: the strip is the RedXe window itself, so the
   reveal is an ordinary `WM_MOUSEMOVE`.
3. **Settings first, command line for the run.** The durable configuration is one closed `dock` object in the settings
   document. `--dock*` switches override individual members for one process, exactly as `--settings` selects a file
   for one process.

Owning contracts this RFC expects to change at implementation:

- [XENEON display and windowing](../../UI/UI_XeneonDisplayWindowing.md): the dock window kind, its styles, placement,
  DPI and monitor-change behavior, app-bar registration, and the `--dock*` command-line modes.
- [Dashboard](../../UI/UI_Dashboard.md): the reachable client rectangle in dock mode, autohide holds while a widget is
  raised or a pan/settle/capture is active, and the autohidden scheduling state.
- [Settings](../../Core/Core_Settings.md) and [`Settings.schema.json`](../../Settings.schema.json): the `dock`
  member, minor 2, and the live-reload rule for it.
- [Performance and resources](../../Core/Core_PerformanceAndResources.md): the dock budget below.
- [Actions](../../Plugins/Plugins_Actions.md): `redxe.dock.show`, `redxe.dock.hide`, `redxe.dock.toggle`.
- `docs/usage.md` (new "Dock" section) and `docs/actions.md`.

## Required outcomes

| ID | Requirement | Observable result |
| --- | --- | --- |
| DK-01 | Any edge of any monitor. | `"dock": { "edge": "bottom", "monitor": "primary" }` or `--dock bottom@primary` starts RedXe as a bar along the bottom of the primary monitor; `left@2`, `top@name:DELL`, and `right@xeneon` place it on that monitor's edge. |
| DK-02 | Reserve or overlay. | With `reserveWorkArea` on, a maximized Notepad on that monitor ends at the bar; with it off, the maximized window extends beneath the bar and the bar stays on top. |
| DK-03 | Autohide with a peek strip. | In `autohide` mode only `peek` physical pixels of the bar remain visible at the screen edge; the pointer dwelling on them (default 150 ms), a touch on them, or `redxe.dock.show` reveals the whole bar; after the pointer leaves and the bar is inactive (default 800 ms) it collapses again. |
| DK-04 | The dashboard is unchanged inside the bar. | Pages, the adaptive layout tree, orientation (a side dock is portrait), two/three-finger swipe, edge bands, wheel navigation, raise, settings live reload, and services work exactly as in the other window kinds. |
| DK-05 | No polling, no hooks. | Every dock state blocks on messages. Reveal and hide use at most one armed one-shot timer; no `WH_MOUSE_LL`, no cursor polling, no periodic wake-up, no `ResizeBuffers` on reveal or hide. |
| DK-06 | Command line wins for one run. | Each `--dock*` switch overrides the same-named document member for the process lifetime, including across live reloads; `--dock none` runs the file without its dock. |
| DK-07 | Shell citizenship. | Reservation is a real app bar (`ABM_NEW`/`ABM_SETPOS`), released on exit; the bar never covers the taskbar or another app bar; a full-screen application on the dock's monitor pushes the bar beneath it (`ABN_FULLSCREENAPP`); the dock has no taskbar button. |
| DK-08 | Robust to monitor changes. | A missing selected monitor falls back to the primary with one Warning log record and no prompt; `WM_DISPLAYCHANGE`, `WM_DPICHANGED`, `SPI_SETWORKAREA`, and `ABN_POSCHANGED` re-place the bar without recreating the window, device, or dashboard. |
| DK-09 | Automated hosts unaffected. | `--self-test` ignores the dock and keeps its hidden titled window; `HostPluginTests` proves placement, monitor resolution, MINMAXINFO, and the autohide state machine as pure tables without a display. |

## Terms

| Term | Meaning |
| --- | --- |
| Edge | The monitor side the bar is attached to: `top`, `bottom`, `left`, or `right`. `none` means no dock (today's behavior). |
| Cross axis | The axis perpendicular to the edge. A top/bottom bar's cross axis is vertical; a left/right bar's is horizontal. |
| Thickness | The bar's cross-axis extent in DIPs, converted with `MulDiv(thickness, monitorDpi, 96)`. The along-edge extent is always the full edge (see placement). |
| Peek | In autohide, the number of **physical pixels** of the bar that stay on screen while hidden. |
| Reserve | Registering the bar's rectangle with the shell so `rcWork` excludes it (maximized windows and the desktop icon area shrink). |
| Reveal / hide | Autohide transitions between the full rectangle and the peek strip. |
| Hold | A condition that keeps a revealed bar from hiding: pointer inside, window active, pointer pan/settle/interactive capture, raised widget, or the modal settings-error dialog. |

## Settings — the `dock` root member

`dock` is an optional closed object on the document root (additive; the document minor becomes 2, minor 0 and 1
documents still load with `dock` omitted). Omitted `dock`, or `"edge": "none"`, is today's behavior exactly.

| Member | Type and range | Default | Contract |
| --- | --- | --- | --- |
| `edge` | `none`, `top`, `bottom`, `left`, `right` | `none` | Edge of the selected monitor. `none` disables the dock and makes every other member inert (still validated). |
| `monitor` | Monitor selector: `primary`, `xeneon`, `<n>` (1-based `EnumDisplayMonitors` order), `name:<substring>` | `primary` | `all` is rejected. `xeneon` is the display found by the existing XENEON discovery (`FindXeneonDisplay`), not "the monitor hosting the window" as in action targets, because the window does not exist yet. A selector that resolves to nothing falls back to the primary monitor with one Warning log record. |
| `thickness` | Integer DIPs, 32–1080 | `180` | Cross-axis size. At runtime it is clamped so at least half of the monitor's cross dimension stays free (one Warning record when clamped). |
| `mode` | `fixed`, `autohide` | `fixed` | `fixed` keeps the full bar on screen. `autohide` collapses it to the peek strip. |
| `reserveWorkArea` | Boolean | `true` | `fixed` only: register the bar with the shell so the work area shrinks. Ignored in `autohide` (an autohide bar never reserves; see below). |
| `peek` | Integer physical pixels, 1–64 | `4` | `autohide` only: pixels that remain visible while hidden. |
| `revealDelayMilliseconds` | Integer, 0–2000 | `150` | `autohide` only: pointer dwell on the peek strip before the bar reveals. 0 reveals on the first mouse move. Touch and actions never wait. |
| `hideDelayMilliseconds` | Integer, 0–10000 | `800` | `autohide` only: delay after the last hold clears before the bar collapses. |

Unknown members, wrong types, out-of-range values, `all`, and an unknown edge or mode reject the complete candidate
with a diagnostic on `$.dock.<member>`, like every other host member. The object has no plugin keys and no `use`.

```jsonc
{
  "$schema": "RedXe.settings.schema.json",
  "version": { "major": 5, "minor": 2 },
  "backgroundColor": "#101418",
  "dock": {
    "edge": "bottom",
    "monitor": "primary",
    "thickness": 180,
    "mode": "autohide",
    "peek": 4,
    "revealDelayMilliseconds": 150,
    "hideDelayMilliseconds": 800
  },
  "declare": {
    "Launcher": { "plugin": "builtin.launcher", "shortcuts": [], "iconSize": "medium" },
    "Clock": { "plugin": "builtin.desk-clock" }
  },
  "pages": [
    { "name": "Bar", "columns": [ { "weight": 5, "widget": "Launcher" }, { "weight": 2, "widget": "Clock" } ] }
  ]
}
```

A fixed bar that reserves the work area is the two-member form:

```jsonc
"dock": { "edge": "left", "monitor": "2", "thickness": 240, "reserveWorkArea": true }
```

Pages are authored for the bar's shape by the user; the shipped templates keep their XENEON pages and stay at
`"edge": "none"` (they carry a commented-out `dock` example so the file documents itself). RedXe does not reflow a
2560×720 page into a 180-DIP strip any differently than it reflows any other resize: the adaptive tree partitions the
client, a side dock is portrait, and small tiles show what fits (the meters already report `+N` for what does not).

## Command line

Every switch is a `<switch> <value>` pair parsed by the existing `GetValueArgument` rule: a repeated switch or a
missing value is a command-line error (exit 2 with the usual message box). Values are validated with the same ranges
as the settings members; an invalid value is the same error.

| Switch | Value | Overrides |
| --- | --- | --- |
| `--dock <edge>[@<monitor>]` | `none`, `top`, `bottom`, `left`, `right`, optionally followed by `@` and a monitor selector (`primary`, `xeneon`, `<n>`, `name:<substring>`) | `dock.edge`, and `dock.monitor` when the suffix is present |
| `--dock-mode <mode>` | `fixed`, `autohide` | `dock.mode` |
| `--dock-thickness <dips>` | 32–1080 | `dock.thickness` |
| `--dock-reserve <on|off>` | `on`, `off` | `dock.reserveWorkArea` |
| `--dock-peek <pixels>` | 1–64 | `dock.peek` |

The delays stay settings-only; the command line covers what a person needs to try a bar without editing the file.

```powershell
RedXe.exe --dock bottom@primary
RedXe.exe --dock left@2 --dock-thickness 240 --dock-reserve off
RedXe.exe --dock top@name:DELL --dock-mode autohide --dock-peek 6
RedXe.exe --settings C:\Dash\bar.settings.json --dock bottom
RedXe.exe --settings C:\Dash\bar.settings.json --dock none
RedXe.exe --dock right@xeneon --screenshot C:\Dash\bar.png --after 4000
```

Merge rule (pure, in `RedXe/DockOptions.h`, shared with tests): effective dock = document `dock` with defaults
merged, then each present switch replaces its member. The merge re-runs on every live reload, so the file may change
members the command line did not name while the named ones stay pinned. A `--dock-*` switch without an effective
edge (`none` from either source) is accepted and inert. `--self-test` ignores every `--dock*` switch after validating
it.

## Window kind: dock

`Application` gains a third window kind beside titled and fullscreen. Selection happens once at startup from the
effective dock: an edge other than `none` means dock mode in both Debug and Release, on any monitor, and replaces the
mode table row that would otherwise apply.

| Aspect | Contract |
| --- | --- |
| Discovery and prompts | XENEON discovery still runs (it resolves the `xeneon` selector). The Release missing-display prompt is **not** shown in dock mode; a dock does not need the XENEON. |
| Styles | `WS_POPUP \| WS_CLIPCHILDREN`; extended `WS_EX_TOOLWINDOW \| WS_EX_TOPMOST \| WS_EX_NOREDIRECTIONBITMAP`. No `WS_EX_APPWINDOW`: a bar has no taskbar button and no Alt+Tab entry, like the taskbar itself. |
| Exit | Escape while the bar has focus (click or tap it first), `redxe.quit`, or `WM_CLOSE` from a tool. There is no close glyph and no tray icon (non-goal). |
| Activation | Unchanged: `WM_MOUSEACTIVATE` / `WM_POINTERACTIVATE` return `MA_ACTIVATE` so a click reaches the widget and keyboard widgets (AV Control text) keep working. Hover never activates. |
| Placement | See "Placement". The dashboard and swap chain are always sized to the **full** dock rectangle; the peek strip is a window-size change only. |
| `WM_GETMINMAXINFO` | The dock answers with minimum = the peek strip size and maximum = the monitor size. Windows applies `ptMinTrackSize` to `SetWindowPos` as well as to user tracking, so the titled window's 480×320 minimum would otherwise refuse the 4-pixel strip. |
| DPI | The bar's thickness is recomputed from the monitor DPI on `WM_DPICHANGED` (the popup rule "use the suggested monitor rectangle" does not apply; the dock re-places itself from `DockPlacement.h`), then the renderer receives the physical client through `WM_SIZE` as today. |
| Monitor changes | `WM_DISPLAYCHANGE` re-resolves the selector. If the selected monitor is gone the bar moves to the primary (one Warning record); when it returns, the bar moves back. `WM_SETTINGCHANGE` with `SPI_SETWORKAREA` re-places an overlay or autohide bar; `ABN_POSCHANGED` re-queries a reserving bar. Adapter-of-output re-check runs after every re-placement exactly as after a move. |
| Reachable client | In dock mode the reachable client for the edge bands is the whole client. The current intersection with `rcWork` would be empty for a reserving side dock (its own rectangle is excluded from the work area) and would suppress both bands. The bar fits its monitor by construction, so the bands hug the true client edges. |
| Z-order | Topmost. On `ABN_FULLSCREENAPP` with a full-screen window on the dock's monitor the bar drops to `HWND_BOTTOM` and returns to `HWND_TOPMOST` when the notification clears, matching the taskbar. |
| Self-test and screenshot | `--self-test` keeps its hidden titled window. `--screenshot` captures the dock like any other window; an autohide dock is **held revealed** for the whole capture run (the hide timer never arms), so the PNG shows the bar. |

### Placement (`RedXe/DockPlacement.h`, pure)

Inputs: monitor rectangle, work-area rectangle, edge, thickness in pixels, reserve flag, peek in pixels.

- **Reserving (fixed + `reserveWorkArea`)**: the proposal to the shell is the monitor rectangle trimmed to
  `thickness` on the edge side (bottom: `top = bottom − thickness`). `ABM_QUERYPOS` lets the shell move it clear of
  the taskbar and other bars; RedXe re-trims to `thickness` from the returned edge and commits with `ABM_SETPOS`; the
  window is moved to the committed rectangle and `ABM_WINDOWPOSCHANGED` is sent. The shell therefore decides the
  along-edge span (a left bar with a bottom taskbar ends above the taskbar).
- **Overlay (fixed without reserve) and autohide**: the bar hugs the edge of the **work area**, spanning the work
  area along the edge, so it never covers the taskbar or another app bar. Consequence, documented for users: with the
  taskbar on the same edge, the peek strip sits just above the taskbar and is not an "infinite" edge target; choose
  another edge or set the taskbar to autohide.
- **Hidden rectangle**: the outer `peek` pixels of the full rectangle (bottom bar: the strip whose `top` is
  `full.bottom − peek`). Along-edge extent is unchanged.
- **Thickness clamp**: `min(thicknessPx, crossDimension / 2)`.
- **Monitor resolution**: `primary` → `MONITORINFOF_PRIMARY`; `<n>` → n-th in enumeration order; `name:` →
  ordinal case-insensitive substring of the `QueryDisplayConfig` friendly name; `xeneon` → the discovery result; none
  found → primary.

Everything is a function of rectangles and integers so `HostPluginTests` proves the table for every edge, both
taskbar sides, an off-origin secondary monitor, negative coordinates, and 96/144/192 DPI without a display topology.

## Work-area reservation and the shell

The dock always registers as an app bar in dock mode (`ABM_NEW` with a private `WM_APP` callback) so it receives
`ABN_POSCHANGED`, `ABN_FULLSCREENAPP`, and `ABN_STATECHANGE`; whether it **reserves** depends on the mode:

| Mode | Registration | Work area |
| --- | --- | --- |
| `fixed`, `reserveWorkArea: true` | `ABM_NEW`, `ABM_QUERYPOS` → `ABM_SETPOS` on every placement | Shrinks by the bar. Maximized windows stop at the bar. |
| `fixed`, `reserveWorkArea: false` | `ABM_NEW` only (no `ABM_SETPOS`) | Unchanged. The topmost bar overlays maximized windows. |
| `autohide` | `ABM_NEW` plus `ABM_SETAUTOHIDEBAREX` for the edge and monitor | Unchanged. If the shell refuses (that edge already has an autohide bar, typically an autohide taskbar), one Warning record and the bar continues as a plain overlay strip. |

`ABM_ACTIVATE` is sent on `WM_ACTIVATE` and `ABM_WINDOWPOSCHANGED` on `WM_WINDOWPOSCHANGED`, as the app-bar contract
asks. `ABM_REMOVE` (and `ABM_SETAUTOHIDEBAREX` with `FALSE`) run in `CloseMainWindow` before the HWND is destroyed
and are idempotent. Switching between the three rows at live reload removes and re-registers.

The fatal-process path MUST NOT call the shell: `SHAppBarMessage` is IPC to the tray window and has no place in an
exception filter (`Core_CrashHandling.md`). A crash while reserving can therefore leave the work area shrunk until
Explorer next recomputes app-bar positions; RedXe's next launch (its own `ABM_SETPOS`) is expected to be such a
recompute. The live validation below verifies this and, if Explorer does not recover, the user page documents the
manual fix (sign out, or toggle the taskbar's "automatically hide" setting).

## Autohide

### Why the strip is the window

The reveal trigger must not cost anything while nothing happens. A low-level mouse hook wakes the process on every
pointer move on every monitor; a cursor poll is a periodic wake-up; a separate transparent sentinel window would need
the main window hidden and its own z-order care. Keeping the peek strip **as the RedXe window itself** means the
reveal is an ordinary `WM_MOUSEMOVE` the window already handles, the hide is the `WM_MOUSELEAVE` the edge bands already
track, and while hidden the process blocks in `WaitUntilMessage` exactly like a minimized window.

### State machine (`RedXe/DockPlacement.h`, pure)

| State | Window | Dashboard | Timer |
| --- | --- | --- | --- |
| `Revealed` | Full rectangle | Visible, normal scheduling | none |
| `HidePending` | Full rectangle | Visible | one-shot `hideDelayMilliseconds` |
| `Hidden` | Peek strip | **Not visible**: widgets `SetVisible(FALSE)`, native containers hidden, no frames, blocks on messages (the minimized policy) | none |
| `RevealPending` | Peek strip | Not visible | one-shot `revealDelayMilliseconds` |

Transitions:

| From | Event | To |
| --- | --- | --- |
| `Hidden` | mouse move inside the strip (not synthesized from touch) | `RevealPending` (or `Revealed` when the delay is 0) |
| `RevealPending` | dwell elapsed | `Revealed` |
| `RevealPending` | `WM_MOUSELEAVE` | `Hidden` (timer killed) |
| `Hidden`, `RevealPending` | `WM_POINTERDOWN` (touch or pen) on the strip, `redxe.dock.show`, `redxe.dock.toggle` | `Revealed` immediately |
| `Revealed` | last hold clears (pointer left **and** window inactive **and** no pan/settle/capture **and** no raised widget **and** no settings dialog) | `HidePending` (or `Hidden` when the delay is 0) |
| `HidePending` | any hold returns | `Revealed` (timer killed) |
| `HidePending` | delay elapsed, `redxe.dock.hide` | `Hidden` |
| `Revealed` | `redxe.dock.hide`, `redxe.dock.toggle` | `Hidden` even with the pointer inside; the next mouse move over the strip starts a new dwell |
| any | live reload to `fixed`, or a `--screenshot` run | `Revealed`, pinned |

Holds are read from state `Application` already tracks (`_pagePointerActive`, `_pagePanStarted`, `_pageSettleActive`,
`_interactiveOwnsPointer`, `_raisedActive`, `_settingsErrorDialog`) plus a new `_windowActive` from `WM_ACTIVATE`.
The decision is one pure function of (state, event, holds) so every row above is a `HostPluginTests` case.

### Reveal and hide cost

- Reveal: one `SetWindowPos` to the full rectangle (`SWP_NOACTIVATE | SWP_NOZORDER`), `SetWidgetsVisible(TRUE)`,
  one invalidated frame. Hide: one `SetWindowPos` to the strip, one **grip frame** (below), `SetWidgetsVisible(FALSE)`,
  then block. Neither path calls `ResizeBuffers`, recomputes layout, or fires `OnTargetSizeChanged`.
- To make that true, the dock window's swap chain is created with `DXGI_SCALING_NONE` (flip model, Windows 8+):
  the back buffer keeps the full dock size and DWM shows its top-left region clipped to the smaller client. For
  every edge the visible strip is therefore back-buffer rows `0..peek` (top/bottom docks) or columns `0..peek`
  (left/right docks). The titled and fullscreen kinds keep `DXGI_SCALING_STRETCH`; with equal buffer and client sizes
  the two modes are indistinguishable, so this is a dock-only creation flag, not a renderer redesign.
- `OnSize` treats a size change caused by a dock reveal or hide as **not** a dashboard resize (`_dockResizing`
  flag around the `SetWindowPos`): `DashboardHost::Resize` and `Renderer::Resize` are skipped. Every other `WM_SIZE`
  (DPI, monitor, thickness, edge change) carries the full rectangle and follows the normal path.
- The **grip frame** is the single frame presented on hide: the dashboard clear color plus one host-chrome quad in
  the strip region — a translucent wash with a one-DIP accent line on the outer edge (`RedXe/HostChrome.*`, no new
  glyph). It tells the user where the bar is without the dashboard's arbitrary edge pixels showing through. While
  hidden nothing is presented; DWM keeps the last frame.
- Memory while hidden is the full-size swap chain (2560×180 BGRA ×2 ≈ 3.7 MiB for the default bar on a 2560-wide
  monitor). Releasing it would add a reallocation and a black flash on every reveal; keeping it is the justified
  steady-state cost.

### Input details

- Mouse messages synthesized from touch or pen are ignored for the dwell, exactly as the edge bands ignore them; a
  real touch-down on the strip reveals at once.
- The mouse wheel over the strip does nothing while hidden (the strip is host chrome, and hidden pages own no
  widgets); over a revealed bar the wheel navigates as usual.
- `WM_MOUSELEAVE` is already tracked through `TrackMouseEvent`; entering a native container counts as a leave for
  the parent, so the hide decision re-tests the live cursor against the full rectangle before treating it as "left",
  the same way `UpdatePageEdgeHover` does.
- Keyboard: Escape in a revealed autohide bar still closes RedXe (unchanged contract); `redxe.dock.hide` is the
  action to collapse it.

### Actions (`redxe` namespace)

| Action | Target | Effect |
| --- | --- | --- |
| `redxe.dock.show` | none | Reveal (autohide) and restart the hold evaluation. Counted and inert in `fixed` mode and without a dock. |
| `redxe.dock.hide` | none | Collapse now. Inert while a hold other than "pointer inside" is active (a raised widget, a capture, the dialog). |
| `redxe.dock.toggle` | none | `show` when hidden or pending, otherwise `hide`. |

They follow the default-namespace rules in `Plugins_Actions.md` (UI thread, no waiting, counted under automated
hosts) and let a Logicon key or a Launcher tile bring the bar back on a keypad without touching the edge.

### Phase 3: slide animation (deferred)

Phase 1 and 2 reveal and hide in one frame each, the same choice the edge bands made ("each change is exactly one
frame"). A taskbar-style slide is possible later without new mechanisms: presentation-paced frames for
`slideDurationMilliseconds` (the settle policy in `PageNavigation.h`), the window's cross-axis size interpolated by
`EaseOutCubic`, and — for top and left docks only — a cross-axis translation of every tile so the bar's outer edge
leads the motion (bottom and right docks are already top-left anchored and slide correctly with no translation).
This needs `DXGI_SCALING_NONE` (already required above) and a cross-axis offset in `DashboardHost` composed with the
existing pan offset; it adds no timer and no wake-up outside the slide.

## Live reload

- Changing any `dock` member other than `edge` between `none` and an edge applies in place: `thickness`, `edge`,
  `monitor`, `mode`, `reserveWorkArea`, `peek`, and the delays re-place the window (remove and re-register the app bar
  when the registration row changes; `ABM_SETPOS` when only the rectangle changes). An edge change that flips
  orientation reflows the dashboard through the ordinary resize path. The window, device, plugins, and services are
  not recreated.
- Switching between `none` and an edge changes the window kind (styles, app-bar registration, MINMAXINFO, swap-chain
  scaling) and takes effect at the next launch; the reload logs one Warning record naming that. The rest of the
  document still applies live.
- Command-line-pinned members are re-applied after every merge (DK-06).

## Interaction with existing contracts

| Contract | In dock mode |
| --- | --- |
| Debug titled window at the XENEON origin, Release fullscreen popup, missing-display prompt | Replaced by the dock for the process; the prompt is skipped. The XENEON is just one possible monitor. |
| 2560×720 design canvas | Not used; the dock's client is `edgeLength × thicknessPx`. The adaptive tree owns the layout, as it does for any titled-window resize. |
| Orientation | Top/bottom docks are landscape, left/right docks are portrait (`UI_Dashboard.md` "Dynamic orientation"). |
| Two/three-finger swipe, edge bands, wheel | Unchanged. Bands use the whole client as reachable (above). |
| Raise overlay | Unchanged inside the bar; a raised widget holds an autohide bar revealed. |
| `GUID_SESSION_DISPLAY_STATUS`, DXGI occlusion, minimized/hidden scheduling | Unchanged; `Hidden` is one more "not visible" input to `UpdateDashboardVisibility` and to the scheduler decision table. |
| Adapter of output | Unchanged; the dock's monitor selects the adapter, and every re-placement re-checks it. |
| `--settings`, live reload, persist | Unchanged; `dock` is a host member and is never persisted by a widget. |
| Services (Logicon, Zoom) | Unchanged. A dock document may configure them like any other. |
| `--screenshot` | Works; autohide is held revealed for the run. |
| A second dashboard window (XENEON fullscreen **and** a dock) | Out of scope. The supported shape is a second process with its own `--settings` file in its own directory (so it gets its own `Logs` sibling); services must be configured in only one of the two documents because a HID device or a Zoom session cannot be claimed twice. |

## Performance and resource budget

- Zero periodic wake-ups in every dock state. At most one one-shot `SetTimer` is armed at a time (dwell or hide) and
  it is killed on every state exit; a zero delay arms nothing.
- No hook, no cursor polling, no `GetCursorPos` outside the message handlers that already call it.
- Reveal and hide: one `SetWindowPos`, one frame, no `ResizeBuffers`, no layout recompute, no `OnTargetSizeChanged`,
  no allocation.
- Shell traffic only on placement, activation, `WM_WINDOWPOSCHANGED`, and shell notifications; never per frame.
- Hidden autohide dock: the same steady state as a minimized window (blocks in `WaitUntilMessage`, widgets not
  visible, native containers hidden) plus the retained full-size swap chain justified above.
- Fixed dock: identical to the fullscreen kind at a smaller client.
- Every `dock` value is validated on the cold change path (parse) and the effective placement is cached; the render
  path reads integers.

## Settings, schema, templates, and validation surfaces

| Surface | Change |
| --- | --- |
| `Specs/Settings.schema.json` | `$defs.dock` (closed object, the eight members with ranges and defaults) and the root `dock` property; `version.minor` default stays 0, templates author 2. |
| `RedXe/Settings.h` / `Settings.cpp` | `DockSettings` on `AppSettings` (edge, monitor selector text, thickness, mode, reserve, peek, delays), parsed and range-checked with the other host members; unknown members reject on `$.dock.<member>`. |
| `Settings/RedXe.settings.json`, `Settings/RedXe-debug.settings.json` | Minor 2 and a commented-out `dock` example; no placed change. |
| `RedXe/Main.cpp` + `RedXe/DockOptions.h` | Parse `--dock`, `--dock-mode`, `--dock-thickness`, `--dock-reserve`, `--dock-peek`; the pure merge. |
| `RedXe/DockPlacement.h` | Edge, placement rectangles, hidden rectangle, clamp, monitor resolution, MINMAXINFO, state machine. |
| `RedXe/Application.*` | Dock window kind, app-bar registration and callback, `WM_ACTIVATE`, `WM_SETTINGCHANGE`, `WM_DISPLAYCHANGE` re-placement, reveal/hide, grip frame request, `_dockResizing`. |
| `RedXe/Renderer.*` | `DXGI_SCALING_NONE` for the dock kind; `Resize` untouched. |
| `RedXe/HostChrome.*` | The grip quad. |
| `RedXe/HostActionCatalog.cpp`, `HostActions.*` | `redxe.dock.show/hide/toggle`. |
| `Tests/SettingsTests` | `dock` member acceptance, every rejection, minor 2 read/write, minor 1 without `dock`, command-line merge precedence and every switch error. |
| `Tests/HostPluginTests` | Placement table, monitor resolution and fallback, MINMAXINFO, state-machine table, scheduler table with `Hidden`, `--self-test` unaffected. |
| `docs/usage.md`, `docs/actions.md` | "Dock" section (settings, switches, examples, taskbar-on-same-edge note, exit, crash note) and the three actions. |

## Required validation

Automated (every phase):

```powershell
.\test.ps1 -Configuration Debug -Platform x64 -Rebuild
.\test.ps1 -Configuration Release -Platform x64 -Rebuild
.\build.ps1 -Configuration Release -Platform ARM64 -Rebuild
```

- `SettingsTests`: defaults, ranges, `all` rejected, unknown member, wrong type, `edge: none` with other members,
  minor 2 written, minor 1 read; `--dock` grammar (`bottom`, `bottom@primary`, `left@2`, `top@name:DELL`, `none`),
  errors (repeat, missing value, `@all`, `@0`, unknown edge, `--dock-reserve maybe`, `--dock-peek 0`); merge
  precedence and inert switches.
- `HostPluginTests`: the placement table for the four edges × reserve/overlay × taskbar top/bottom/left/right × a
  secondary monitor at negative coordinates × 96/144/192 DPI; the hidden rectangle for each edge; the thickness clamp;
  monitor resolution for every selector kind and the fallback; MINMAXINFO; every state-machine row including zero
  delays and hold precedence; the scheduler decision table proving `Hidden` waits for a message and builds no frame;
  `--self-test` still creates the hidden titled window with a dock in the deployed template.
- The hidden WARP smoke test additionally creates a dock-kind swap chain (`DXGI_SCALING_NONE`) and presents one
  frame at a client smaller than the buffer.

Live (before closeout), recorded in the plan with the machine's topology:

1. `--dock bottom@primary` fixed + reserve: maximize Notepad → its bottom edge is above the bar; `rcWork` shrank by
   `thicknessPx`; exit RedXe → `rcWork` restored.
2. `--dock-reserve off`: the maximized window passes beneath the bar; the bar stays on top; taskbar untouched.
3. `--dock left@2` on a secondary monitor with a bottom taskbar: the bar ends above the taskbar; the edge bands are
   at the true client edges; swipe and wheel change pages; a side dock is portrait.
4. Autohide: mouse dwell reveals after ~150 ms, passing through the edge quickly does not; touch reveals at once;
   the bar hides ~800 ms after the pointer leaves and it lost activation; it does not hide while a widget is raised,
   during a swipe, or while the settings-error dialog is up; `redxe.dock.show/hide/toggle` from a Logicon key.
5. `ABN_FULLSCREENAPP`: a full-screen video on the dock's monitor covers the bar; leaving full screen brings it back.
6. DPI: change the dock monitor from 100% to 150% → the bar's thickness re-scales, one `WM_DPICHANGED`, no device
   recreation unless the adapter changed.
7. Monitor removal: unplug the dock monitor → the bar moves to the primary with one Warning record; replug → it
   returns.
8. Crash while reserving: end the process from Task Manager; confirm whether the next launch restores the work area;
   document the result on the user page.
9. `--screenshot` of an autohide dock yields the full bar. Every dock picture in `docs/screenshots/` comes from it.
10. Two processes: the XENEON fullscreen dashboard plus a primary-monitor dock from a second settings file in its own
    directory, services in one document only, both logging to their own `Logs` folders.

Under a Per-Monitor-V2 probe, record the final window rectangle for 1, 3, and 6 and compare them with the
`DockPlacement.h` table output for the same inputs.

## Documentation

`docs/usage.md` gains a "Dock" section after "Window": what a dock is, the `dock` members with defaults and ranges,
the five switches with the examples above, the taskbar-on-the-same-edge note, how to exit (Escape after a click,
`redxe.quit`), the minor-2 note, the "authored for the bar" settings example, and the crash note from validation 8.
`docs/actions.md` lists the three `redxe.dock.*` actions. Screenshots of a dock come from `--screenshot`.

## Implementation phases

| Phase | Scope | Exit criteria |
| --- | --- | --- |
| 1 — Fixed dock | `dock` member, schema, templates minor 2, `--dock*` switches and merge, `DockPlacement.h` placement and monitor resolution, the dock window kind, app-bar registration with reserve on/off, `ABN_FULLSCREENAPP`, MINMAXINFO, reachable client, DPI and monitor re-placement, live reload of members, tests, `docs/usage.md`. | DK-01, DK-02, DK-04, DK-06 through DK-09; live validations 1–3, 5–8, 10. |
| 2 — Autohide | `mode: autohide`, `peek`, delays, `ABM_SETAUTOHIDEBAREX`, the state machine, `DXGI_SCALING_NONE` for the dock kind, `_dockResizing`, the grip frame, `WM_ACTIVATE`, `redxe.dock.*`, screenshot hold, scheduler table row, tests, `docs/actions.md`. | DK-03, DK-05; live validations 4 and 9. |
| 3 — Slide (optional) | `slideDurationMilliseconds`, presentation-paced reveal/hide, cross-axis translation for top/left docks. | Same budget: no timer, no wake-up outside the slide. |

Phase 1 alone is a complete, shippable feature. Phase 2 depends on phase 1 only.

## Decisions

Each item states the proposal; taking it as proposed needs no further text.

| ID | Decision | Proposal | Alternative considered |
| --- | --- | --- | --- |
| D1 | Settings shape | One closed root object `dock`; document minor 2. | Flat root members (`dockEdge`, …): more root noise, no grouping for the schema. |
| D2 | Command-line shape | `--dock <edge>[@<monitor>]` plus `--dock-mode`, `--dock-thickness`, `--dock-reserve`, `--dock-peek`, all `<switch> <value>` pairs; delays settings-only. | One packed `--dock bottom@primary,autohide,180,peek=4` string: harder to read and to validate. |
| D3 | Units | `thickness` in DIPs (scales with the monitor like every host size); `peek` in physical pixels (the user asked for pixels, and the strip is a screen-edge target whose size only affects visibility). | Both in DIPs: a 4-DIP strip is 8 px at 200%, more than asked. |
| D4 | Taskbar presence | Tool window: no taskbar button, no Alt+Tab entry; exit by Escape or `redxe.quit`. | `WS_EX_APPWINDOW`: a discoverable "Close window" but a bar that lists itself among the windows it serves. |
| D5 | Z-order | Always topmost; lowered on `ABN_FULLSCREENAPP`. | A per-document `alwaysOnTop`: an autohide strip that can be covered can never be reached, so the member would be fixed-only and rarely wanted. |
| D6 | Overlay placement | Hug the work-area edge (never cover the taskbar); the shell decides for a reserving bar. | Hug the monitor edge over the taskbar: unreliable z-order against the taskbar, and it hides the Start button. |
| D7 | Autohide and reservation | An autohide bar never reserves; it registers `ABM_SETAUTOHIDEBAREX`; refusal is a Warning, not an error. | Reserve the peek strip: a 4-px dead zone in every maximized window for no benefit. |
| D8 | Hidden scheduling | `Hidden` is "not visible": widgets hidden, no frames, block on messages; one grip frame on hide. | Keep rendering into the strip: continuous cost for 4 visible pixels. |
| D9 | Reveal and hide triggers | Mouse dwell (default 150 ms) or touch-down or action to reveal; hide 800 ms after every hold clears; holds are pointer-inside, active, pan/settle/capture, raise, dialog. | Reveal on first mouse move (delay 0): flashes when the pointer only passes the edge. Users can still set 0. |
| D10 | Swap-chain scaling | `DXGI_SCALING_NONE` for the dock kind so the client can shrink without `ResizeBuffers`. | `ResizeBuffers` on each transition: two buffer reallocations per reveal cycle and a per-frame reallocation if a slide is ever added. |
| D11 | Animation | One frame per reveal or hide in phases 1–2; the slide is phase 3 with the translation design above. | Slide in phase 2: more surface to prove before the feature is usable. |
| D12 | Live switch of the window kind | `none` ↔ edge takes effect at the next launch (one Warning record); every other member is live. | Recreate the HWND live: the renderer, containers, accessibility, drag-drop, and services all bind to the HWND; not worth the risk for a mode switch. |
| D13 | Missing monitor | Fall back to the primary with one Warning record; no prompt; return when the monitor reappears; `xeneon` means the discovered XENEON. | Prompt like the Release fallback: a bar is explicit configuration, not the XENEON default, so the prompt would only get in the way of a scripted launch. |
| D14 | Defaults | `monitor: primary`, `thickness: 180`, `mode: fixed`, `reserveWorkArea: true`, `peek: 4`, `revealDelayMilliseconds: 150`, `hideDelayMilliseconds: 800`. | `monitor: xeneon`: the dock exists precisely for the *other* monitors; `xeneon` stays one selector away. |

Open for the requester: (a) is 180 DIPs the right default thickness for the shipped widgets, or should the default be
smaller (a taskbar is 48) and the example document carry 180; (b) whether the phase-3 slide is wanted at all; (c)
whether a tray icon (exit, show/hide, open settings) should be planned as a follow-up now that the bar has no taskbar
button.

## Non-goals

- A bar that spans only part of an edge, is centered, or is offset along the edge: the span is always the full
  edge (shell-adjusted for a reserving bar, work-area for the others).
- Two dashboard windows in one process (XENEON fullscreen plus a dock). See the two-process shape above.
- Per-dock pages or a separate page list: the document's pages are the bar's pages. Use `--settings` for a
  bar-specific document.
- A tray icon or any other shell UI (recorded as an open follow-up).
- Docking to the edge of the *window* the user is working in, or following the foreground window.
- Reading or changing the taskbar's own settings.
- Persisting runtime dock state (revealed/hidden, the fallback monitor): like the active page, it is never written.
