# Mouse wheel: widget first, then dashboard page navigation

Status: `COMPLETE`
Implemented: 2026-09-18
Completed: 2026-09-18
Created: 2026-09-18
Owner: mouse input routing in `Application`, the interactive-widget pointer ABI, and the bundled paged widgets

## Goal

Make the mouse wheel useful on the dashboard on every kind of tile:

1. The top-level window receives every wheel sample (vertical `WM_MOUSEWHEEL` and horizontal `WM_MOUSEHWHEEL`) and
   offers it to the interactive widget under the pointer first, so a plugin can use it for whatever it likes — page a
   paginated control, scroll a list, adjust a control.
2. When the plugin does not use the sample, the host uses it to change the dashboard page, the way the edge bands and
   a two-finger swipe do.

Today (`271c110`) the ABI already carries `RedXePointerPhaseWheel` and `Application` forwards `WM_MOUSEWHEEL` to
the topmost `IRedXeInteractiveWidget`, but an unconsumed sample falls through to `DefWindowProc` and does nothing,
`WM_MOUSEHWHEEL` is not handled at all, Launcher declines every wheel sample although it is a paginated control, and
ProcessViewer/Weather swallow a sample at their last page (return `S_OK`) so the wheel can never hand off.

Owning contracts: [`../../UI/UI_Dashboard.md`](../../UI/UI_Dashboard.md) (host page navigation) and
[`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md) (pointer ABI and bundled widget behavior).
[`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md) applies: no timer, no
wake-up, no allocation on the wheel path.

Historical context: [`MouseEdgePageNavigation_2026-09-03.md`](MouseEdgePageNavigation_2026-09-03.md)
built the staging/settle/promote entry point (`NavigateToAdjacentPage`) and the suppression table
(`ShouldShowEdgeAffordance`) this plan reuses unchanged. It explicitly excluded wheel navigation; this plan adds it.

## Scope

### Included

- Horizontal wheel samples in the ABI (`RedXePointerPhaseHorizontalWheel`) and their forwarding.
- The host wheel fallback: unconsumed samples navigate the dashboard one page per detent, through the existing
  `NavigateToAdjacentPage` path and its suppression table.
- A wheel *sequence* policy (owner latch and detent accumulation) as a pure, testable header.
- A shared whole-detent helper for paged plugins, and the bundled paged widgets (Launcher, ProcessViewer family,
  Weather) using it and handing off at their bounds.
- Launcher paging with the wheel (its `AnimateToPage` slid the wrong way; fixed on the way).
- Specs, tests, and the user guide.

### Excluded

- Any change to touch/pen page-pan semantics, edge bands, raise, or keyboard navigation.
- Continuous (pixel) page scrolling with the wheel. A wheel navigates discretely: one detent, one page, one settle.
- A settings knob for wheel direction or sensitivity. One detent is one page everywhere; see D3.
- Widget-authored horizontal scrolling inside DxUi (`DxUi::PointerAction` has no horizontal wheel); AV Control
  returns `S_FALSE` for horizontal samples so the host takes them.

## Decisions

### D1. Widget first, host second, decided per *sequence* — not per sample

A wheel sequence is a run of samples with less than `kWheelSequenceGapMilliseconds` (500 ms) between consecutive
samples. The **first** sample of a sequence is hit-tested and forwarded to the topmost interactive widget under the
pointer (raised content while raised). Its answer decides who owns the rest of the sequence:

| First sample | Owner for the rest of the sequence |
| --- | --- |
| Widget returns `S_OK` (consumed) | That widget. Later samples go to it whether or not it consumes them; the host never navigates. |
| Widget returns `S_FALSE`, fails, or there is no interactive widget under the pointer | The host. Later samples are not offered to any widget; they accumulate toward page navigation. |

Why a latch rather than sample-by-sample chaining: nested-scroll chaining without latching means a user paging a
process list who overshoots the last page by one notch is thrown onto another dashboard page; a free-spinning wheel
makes that certain. With the latch the widget keeps the sequence until the user pauses. After a pause the next
sample is offered to the widget again, and if it is at its bound and declines, the host navigates. This is the same
model browsers use for wheel latching, and it is symmetric: a sequence the host started keeps flipping pages until
the user pauses, instead of being captured by whatever paged widget lands under the pointer on the next page.

### D2. One detent, one page, in the swipe direction

- Vertical: wheel **down** (negative `WHEEL_DELTA`) advances to the next page; wheel up returns. This matches the
  convention every scrolling surface uses (down = forward) and the bundled paged widgets step the same way.
- Horizontal: tilt **right** (positive delta) advances; tilt left returns. Content moves left, as with swipe-left.
- Samples accumulate per axis in Win32 wheel units; a whole `WHEEL_DELTA` (120) commits one navigation and clears
  the accumulator, so a precision wheel or touchpad still needs one notch's worth of travel and a classic wheel
  navigates on every notch. A direction reversal discards the remainder. Nothing is queued: a sample that arrives
  while a settle, pan, raise, or any suppressed state is active is dropped and the accumulator is cleared, so a
  fast spin cannot bank pages that play back later.
- The blocked-end and wrap rule is exactly the edge-band rule (`ShouldShowEdgeAffordance`): a wheel sample toward a
  blocked end does nothing, and `wrapPages` wraps.

### D3. No settings

Direction and sensitivity are not user settings. `Core_Settings.md` stays untouched; the schema does not change.

### D4. Bundled paged widgets consume only when they move (superseded — see the revision under Outcome)

A paged widget answers `S_OK` when the sample can move it in that direction (even a fractional sample that has not
yet completed a detent) and `S_FALSE` when it is at the bound in that direction, has a single page, or does not use
that axis. Combined with D1 this gives: page through the widget; at its end the wheel is quiet until you pause; the
next notch after the pause changes the dashboard page. Plugins step once per whole detent through the shared
`RedXeWheelDetent` helper so touchpads and precision wheels do not race through overflow pages.

- Launcher: vertical and horizontal wheel both page (down / right = next launcher page) through `AnimateToPage`.
- ProcessViewer family and Weather: vertical wheel pages the overflow; horizontal is `S_FALSE`.
- AV Control: vertical goes to DxUi as today; horizontal is `S_FALSE`.
- Logicon monitor: `S_FALSE` for both.

### D5. Pure policy in a header

`RedXe/WheelNavigation.h` holds the sequence owner, gap expiry, per-axis accumulation, and direction mapping as a
small value type with no Win32 dependency beyond `WHEEL_DELTA`, tested in `HostPluginTests` like
`PageEdgeAffordance.h`. `Application` only feeds it the message time (`GetTickCount64`), the widget result, and the
axis/delta, and calls `NavigateToAdjacentPage` when it says so. `Common/WheelDetent.h` holds the detent accumulator
shared by the host policy and the plugins.

## ABI change

`Common/PlugInterfaces/Widget.h`: add `RedXePointerPhaseHorizontalWheel = 5`. The record layout (48 bytes) does
not change; `wheelDelta` carries the horizontal delta with Win32 sign (positive = right). Every bundled
`OnPointer` is updated in the same change to answer the new phase (RedXe is pre-production; consumers rebuild
together). AV Control's `phase > RedXePointerPhaseWheel` rejection becomes `> RedXePointerPhaseHorizontalWheel`.

## Host routing (`Application`)

```text
WM_MOUSEWHEEL / WM_MOUSEHWHEEL
  drop if hidden, display off, a widget owns a captured drag, or a page pan is in progress   (unchanged)
  now = GetTickCount64(); axis, delta from the message; point = ScreenToClient(lParam)
  owner = _wheel.Begin(now)                       // expires the sequence after a 500 ms gap
  if owner == None:  forward to the widget under the pointer; _wheel.Latch(index, consumed)
  if owner == Widget: forward to that widget (if still interactive); return
  if owner == Host:  direction = _wheel.Accumulate(axis, delta)
                     if direction != 0: NavigateToAdjacentPage(direction); on any refusal Clear()
```

Resets: `CancelInteractivePointer` (hide, resize, DPI, cancel-mode, focus/capture loss), settings apply, raise and
dismiss, and page promote when a widget owns the sequence (indexes refer to the old page). A host-owned sequence
survives a promote so a spin keeps going.

## Validation

- `HostPluginTests`: `TestWheelNavigationPolicy` — sequence gap, widget latch (consumed first sample keeps later
  unconsumed samples from the host), host latch (a declined first sample keeps later samples from widgets), detent
  accumulation with fractions and reversal, both axes' direction mapping, clear on refusal, reset.
- `LauncherTests`: wheel down pages forward and the slide enters from the right (`pageSlidePx > 0`), wheel up at
  the first page returns `S_FALSE`, horizontal tilt pages, fractional samples need a whole detent, a dot tap slides
  the correct way.
- `HostPluginTests`: a single-page System Data viewer returns `S_FALSE` for both wheel phases (hand-off to the host).
- `AVControlTests`: horizontal wheel is `S_FALSE`; vertical still reaches DxUi.
- Interactive on the XENEON: wheel over a clock changes page; over Process Viewer pages its overflow, is quiet at
  the end, and changes the dashboard page after a pause; tilt wheel; blocked ends; `wrapPages`; a Logicon dialpad
  dial (`hwheel`) over the window.

## Outcome

Landed as designed. The live checks recorded in `UI_Dashboard.md` (wheel messages posted to the Debug window
under `--screenshot`, three-page Matrix / Desk Clock / Triangle scene) captured page two after one wheel-down
notch, page one after a wheel-up at the first page without `wrapPages`, page three after two tilt-right notches
700 ms apart, and page two after three notches 40 ms apart (the two during the settle were dropped, not banked).
Over an eight-page Launcher tile, one notch captured its second icon page, two notches 400 ms apart its third with
the dashboard unchanged, and seven notches plus a 900 ms pause plus one notch captured the next dashboard page.
That Launcher check caught one routing defect before closeout: the first draft latched the widget and then fell
into the widget-owned branch, delivering the first sample twice (the Launcher landed on its third page). The
Launcher dot-tap slide also had the sign of its offset reversed (the target entered from the wrong side and the
wrong page left); fixed with the wheel work and pinned by `LauncherTests`.

Same-day follow-up (page control): every widget with internal pages now presents them through one shared page
control — `Common/PageIndicator.h` carries the `DxUi::PageIndicator` metrics, layout (centred or right-aligned,
gap shrink on a narrow strip), colours, and hit test; Launcher's shader draws from that layout, the System Data
viewers reserve the strip below their fitted rows (above any footer) instead of a `+N` caption, and Weather puts
the dots right-aligned in its attribution row. `+N` remains only for items no page reaches (idle adapters,
non-graphics adapters, heat cells, hero tiles, days hidden behind a paging hour strip). Taps on dots are covered by
`HostPluginTests` (Process Viewer, `topN` 32) and `WeatherTests` (280×500) through new diagnostics fields that
report the drawn dot layout. Durable text: `Plugins_API.md`, `Plugins_Weather.md`, `Plugins_AVControl.md`.

Revision after use on the XENEON (same day): D4 is superseded. A widget with more than one page keeps every
wheel sample on its paging axis, bounds included, so the dashboard never changes page under a paging tile; the
wheel changes dashboard pages only over tiles without pages, and the edge bands cover the rest. The hand-off at a
bound had flipped the dashboard while a page control was showing, which read as a bug. Two short-tile defects went
with it: a list too short for a full row plus the strip skipped the strip and paged with no indicator (the strip is
now always reserved when six tenths of a row fit; rows shrink), and the GPU meter reserved a caption band on top of
the strip and squeezed its card (the caption now shares the strip). Live checks over a four-page Thermal tile:
wheel-up at page one and six notches past the last page leave the dashboard page alone; a notch over Matrix still
changes it.

Two things noticed and left alone: a page that places the same declared widget name more than once fails
`PluginManager::Initialize` in `Run` (exit 3) although `--self-test` accepts the same document, and `FindWindowW`
by class did not find the top-level window from another process while `Process.MainWindowHandle` did; neither is
wheel-related.

Durable behaviour lives in `Specs/UI/UI_Dashboard.md` (*Mouse wheel navigation*) and
`Specs/Plugins/Plugins_API.md` (pointer record and wheel answer contract, Launcher and System Data paging), with
`Plugins_AVControl.md`, `Plugins_Weather.md`, and the `plugin-development` skill aligned; `docs/usage.md` and the
Launcher, Process Viewer, Weather, and Logicon pages describe it for users.

## Checklist

- [x] `Widget.h`: `RedXePointerPhaseHorizontalWheel`, comments on both wheel phases and the consume contract
- [x] `Common/WheelDetent.h`
- [x] `RedXe/WheelNavigation.h` + project files
- [x] `Application`: `WM_MOUSEHWHEEL`, sequence policy, host fallback, resets
- [x] Launcher wheel paging + `AnimateToPage` direction fix
- [x] ProcessViewer family, Weather: detent stepping, `S_FALSE` at bounds and for horizontal
- [x] AV Control, Logicon monitor: horizontal phase
- [x] Tests listed above
- [x] `UI_Dashboard.md` (Mouse wheel navigation section, validation, anchors), `Plugins_API.md` (pointer record,
      wheel contract, Launcher/System Data paging, validation rows), `Plugins_AVControl.md`, `Plugins_Weather.md`,
      `plugin-development` skill
- [x] `docs/usage.md`, `docs/plugins/launcher.md`, `process-viewer.md`, `weather.md`, `logicon.md`
- [x] Build Debug, `test.ps1`, `validate-skills.ps1`; plan moved to Done
