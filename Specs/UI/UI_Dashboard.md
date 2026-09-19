# RedXe adaptive dashboard and page-navigation contract

Status: current normative product contract
Last reviewed: 2026-09-19
Owner: `DashboardHost` layout, active-page composition, page navigation, edge-navigation chrome, host placeholder tiles, and raised overlay chrome

## Scope

This specification owns ordered pages, responsive layout compilation, runtime orientation reflow, widget geometry,
horizontal two- or three-finger touch navigation, mouse edge navigation, mouse wheel navigation, host-owned
placeholder tiles, and the host raised-overlay chrome. Settings syntax belongs to `Specs/Core/Core_Settings.md`; plugin identities and
rendering mechanisms belong to `Specs/Plugins/Plugins_API.md`; display/DPI policy belongs to
`Specs/UI/UI_XeneonDisplayWindowing.md`.

The terms **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative.

## Pages

- A document contains 1–16 ordered pages. Order is navigation order and the first page is selected on every launch.
  RedXe does not persist the last active page to disk. A live settings reload MUST keep the page that was current when
  that page still exists: the host selects the page whose `id` matches the page that was showing, or the same index if
  that id is gone but the index is still in range, otherwise the first page. An in-progress swipe is cancelled without
  a settle, as with any other reload.
- Page `id` and `name` are optional metadata and do not control swipe or edge navigation. A missing name is displayed
  as `Page N` without modifying the document. Live reload uses `id` only to recognize the page that was current.
- A page with no `widgets`, `columns`, or `rows` is blank. A page contains at most 32 widget appearances.
  Authored JSON uses those human shapes; `Specs/Core/Core_Settings.md` owns the syntax. The host compiles them to the
  adaptive tree below before `DashboardHost` evaluates geometry.
- Only the current page owns runtime resources, except while swiping when the adjacent transition page may also own
  resources. All other pages own no provider, widget, data subscription, child HWND, D3D resource, timer, acquisition,
  or frame work.

## Adaptive layout tree

The compiled in-memory tree for a nonblank page root contains exactly `arrangeAlong` and nonempty `areas`. User
documents MUST NOT author `layout` / `areas` / `arrangeAlong` / `sizeRatio`; those keys are compile output only.

- `arrangeAlong` is `long-side` or `short-side`.
- Every child area has integer `sizeRatio` from 1 through 1000.
- A leaf contains `sizeRatio` and `widget`.
- A container contains `sizeRatio`, `arrangeAlong`, and nonempty `areas` directly.
- Leaf and container members MUST NOT be mixed in one area.
- One page has at most 127 areas, eight container levels, and 32 leaves.
- Area array order is visual order and widget composition order.

Sibling ratios partition their parent proportionally. At every level RedXe calculates shared integer edges from
cumulative ratios; adjacent regions have no gap or overlap and the last sibling reaches the exact parent edge. The
tree therefore partitions the client rectangle without authored coordinates, out-of-bounds rectangles, or overlap.

The parser compiles each leaf into one bounded split path. `DashboardHost` caches those paths and evaluates physical
bounds only on initialization, resize, orientation change, or transition offset change. Rendering MUST NOT reparse
settings or allocate layout storage.

## Dynamic orientation

Orientation is runtime state and MUST NOT appear in settings.

- Client width greater than or equal to height is landscape; otherwise it is portrait.
- In landscape, `long-side` is horizontal and `short-side` is vertical.
- In portrait, `long-side` is vertical and `short-side` is horizontal.
- Horizontal order is left-to-right and vertical order is top-to-bottom.
- GPU viewports and native child containers use identical cached geometry and shared edges.
- Resize and DPI changes recompute physical bounds without reparsing or changing the compiled tree.

## Horizontal two- and three-finger touch navigation

- Navigation is horizontal in both orientations. Swipe left advances; swipe right returns.
- Dashboard page pan requires **two or three** simultaneous touch contacts. One finger, a pen, and four or more
  fingers MUST NOT start or continue host page navigation. One-finger and pen contacts are forwarded to the topmost
  `IRedXeInteractiveWidget` so a plugin can consume them (AV Control sliders, Launcher internal pages, and similar).
- A second or third finger may begin tracking a page gesture. It MUST NOT cancel an in-progress one-finger widget
  gesture, including `RedXePointerCapture`, until that pan locks horizontally. Overlapping double-taps that never
  lock MUST complete the original contact (slider commit or double-activate raise) instead of swallowing it.
  After a page pan locks, the host sends `Cancel` to the widget that held the first contact and does not treat that
  contact as a launch. One finger alone MUST NOT steal a captured slider or other widget gesture.
- Omitted or false `wrapPages` stops at the first and last page. True wraps either end to the opposite end. A blocked
  end follows the pointer with rubber-band resistance and MUST NOT instantiate a neighbor or commit.
- A page follows the two- or three-finger centroid 1:1 after a horizontal lock. The host MUST NOT capture on contact.
  Vertical-dominant two- or three-finger movement MUST NOT switch pages.
- Mouse edge-band clicks never reach the widget. One-finger touch over an edge-band zone is forwarded to the widget;
  only mouse hover/click owns the band.
- Host-owned native containers forward uncaptured pointer messages to the top-level window so a pan can start over a
  window widget. Taking over a pan sends `WM_CANCELMODE` to the original target and captures the pointer until commit,
  cancellation, or capture loss.
- Neighbor staging MUST NOT block the first follow-finger frame. The current page may slide immediately; the
  directionally adjacent page is created on the following idle turn. Reversing through zero tears down the prior
  neighbor before staging the opposite page.
- On release the host settles with presentation-paced ease-out frames. It commits when travel reaches one quarter of
  the client width or a same-direction flick exceeds the DPI-scaled speed threshold; otherwise it returns to the
  current page. Commit promotes the already-staged neighbor in place and MUST NOT recreate the Direct3D device.
- While a pan, settle, or staged neighbor is in progress the host MUST keep presenting frames. Widget animation on
  both participating pages (continuous GPU widgets, sample-driven eases, and native-window timers) MUST continue
  through the slide. The page-scroll ease itself is those presentation-paced frames; the host MUST NOT freeze the
  dashboard on the last pointer-move invalidation and wait for the next input.
- During a swipe the host keeps each widget's full design-canvas size and only translates it. GPU viewports and native
  containers MAY have a negative origin or extend past the client; the host MUST NOT shrink a tile to the visible
  intersection (that would reflow clocks and rain and fire `OnTargetSizeChanged`) and MUST NOT hide a tile merely
  because it is not completely inside the client. Direct3D and the parent HWND clip the visible portion. The host MUST
  still draw every GPU widget on the current and staged pages whose viewport has positive size, including the incoming
  page sliding in from the left.
- During a swipe only the current and directionally adjacent pages may be instantiated and rendered. Cancellation
  tears down the staged neighbor. Commit makes it current and tears down the prior page.
- Resize, close, reload, and capture loss cancel an active transition immediately, without a settle animation.

## Mouse edge navigation

Touch uses two or three fingers to pan between dashboard pages. Pen and one-finger contacts stay with the widget. A
mouse navigates through a host-owned affordance at each client edge, so a mouse user is never left without page
navigation. This section owns that affordance; it changes nothing about the pan contract above.

- The host owns one full-client-height band at each **reachable** left or right edge of the client. Reachable width is
  the client rectangle intersected with the work area of every display the window touches; reachable height is always
  the full client, including over a taskbar inset or a hanging title strip, so the band runs from the top of the
  window's client to the bottom. A window straddling two monitors is reachable across both, so the reachable width
  MUST NOT be clamped to a single monitor. For a window that fits on its monitor, and therefore always for the Release
  fullscreen window, that is the whole client and the band hugs the true client edge. A titled window can be wider
  than the monitor it sits on -- the Debug XENEON canvas frequently is -- and hugging the client edge would then place
  the band beyond the side of the screen, where no pointer can reach it. Band width is DPI scaled and clamped so the
  two bands can never meet: a narrow reachable area limits each band to one third of its width.
- A band is host chrome drawn by the renderer into the swap chain: it exists as two quads (wash and chevron) only
  while the pointer is inside its zone and is otherwise nothing at all. No band HWND exists, so there is no layered
  child to keep alive, hit-test, or stack.
- Hover is owned by the top-level window, which tests the live cursor position against each zone. Host-owned native
  containers notify the top-level window when the pointer moves over them so that hover can be detected over a
  native-window widget as well as over a GPU tile. The top-level window MUST apply the hand cursor and MUST commit
  navigation when the live cursor is inside a navigable zone on `WM_LBUTTONUP`; it is the only click target.
- A committed edge-click onto a page that contains a native-window widget MUST complete settle and promote the same
  way a GPU-only page does. Host-owned native children covering the swap chain MUST NOT be treated as DXGI occlusion:
  that report parked the settle and suppressed both bands for the rest of the session on the shipped Release second
  page. After promote, both bands MUST be available again whenever the suppression table allows them.
- A band exists only when the host is composing a live multi-page dashboard and that direction has a neighbour. It
  MUST NOT exist when the renderer is not ready, the window is hidden, the display is off, the renderer is suspended
  or occluded, a widget is raised, a pointer pan is in progress, or a settle or staged transition is in progress.
- In the dock window kind (`UI_XeneonDisplayWindowing.md`) the reachable client is the whole client, and a collapsed
  autohide dock (its peek strip) counts as a hidden window: no band exists until the bar reveals.
- A blocked end has no band. `wrapPages` and the first/last page rule are the same ones the pan path uses, so both
  input paths stop and wrap identically.
- A band reveals within one coalesced frame as a translucent wash and a chevron pointing in the travel direction,
  both drawn by the renderer's host chrome pipeline (`RedXe/HostChrome.*`). The chevron is a Segoe Fluent Icons glyph
  from `RedXe/FluentIcons.h`, not a drawn shape, rasterized into the host glyph atlas with DirectWrite once per DPI,
  falling back to Segoe MDL2 Assets and then to a standard Unicode chevron. Reveal and hide MUST NOT add a timer, a
  continuous frame, or any other wake-up, so a cross-fade is out of contract; each change is exactly one frame.
- Reveal responds to mouse input only. Mouse messages synthesized from a touch or pen contact are ignored so the pan
  path keeps them.
- Bands draw over every GPU tile. A native-window container is a layered child above the swap chain, so a band is
  drawn beneath it there; hover and click still work over the container because the container forwards pointer
  movement and releases to the top-level window. No shipped page places a native-window widget.
- Clicking a revealed band stages the adjacent page, then commits through the same ease-out settle and in-place
  promote a committed pan uses. It MUST NOT recreate the Direct3D device. Unlike a pan there is no follow-finger
  phase, so staging is synchronous at click time and the settle starts from rest, which places its duration at the
  clamped upper bound of the shared settle policy.
- A navigation click MUST NOT count as part of a double-activate raise gesture. Accepted trade: a revealed band
  consumes the click in its strip, so a widget under a band cannot be double-click-raised there; it stays raisable
  everywhere else in its tile. Band width is a single DPI-scaled constant so this is tunable in one place.
- Resize, DPI change, settings reload, page promote, raise, dismiss, and every visibility transition re-evaluate both
  bands. Re-evaluation with unchanged state MUST perform no work.

## Mouse wheel navigation

The top-level window receives every mouse wheel sample: vertical `WM_MOUSEWHEEL` and horizontal `WM_MOUSEHWHEEL`,
whichever window had focus, because Windows delivers wheel input to the window under the pointer and a host-owned
native container passes it up through `DefWindowProc`. A sample is offered to the widget under the pointer first;
a sample the widget does not use changes the dashboard page. This section owns that routing; the plugin-side
contract (`RedXePointerPhaseWheel`, `RedXePointerPhaseHorizontalWheel`, `S_OK` versus `S_FALSE`) is owned by
`Specs/Plugins/Plugins_API.md`.

- A **wheel sequence** is a run of samples with less than 500 ms (`kWheelSequenceGapMilliseconds`) between
  consecutive samples on either axis. The **first** sample of a sequence is hit-tested like a click, converted to
  widget-local pixels, and forwarded to the topmost `IRedXeInteractiveWidget` under the pointer (the raised widget's
  overlay content while raised). Its answer latches the owner of the whole sequence: `S_OK` gives every later sample
  of the sequence to that widget, whether or not it consumes them, and the host MUST NOT navigate; `S_FALSE`, a
  failure, or no interactive widget under the pointer gives the sequence to the host, which MUST NOT offer later
  samples to any widget. A pause of the gap length ends the sequence and the next sample is offered to the widget
  again. A widget that shows a page control keeps every sample on the axis it pages, including at its first and
  last page (`Specs/Plugins/Plugins_API.md`), so the dashboard never changes page under a paging tile; the wheel
  changes dashboard pages over tiles that do not page (clocks, Matrix Rain, a single-page list) and the edge bands
  remain for the rest. A host-owned sequence keeps navigating after a promote; a widget-owned sequence ends on
  promote because its slot belongs to the old page.
- A host-owned sample accumulates per axis in Win32 wheel units toward the next page: wheel **down** (negative
  `WHEEL_DELTA`) and tilt **right** (positive) advance, wheel up and tilt left return, matching swipe-left. One whole
  `WHEEL_DELTA` (120) commits exactly one adjacent-page navigation and clears both axes, so a classic wheel navigates
  on every notch and a precision wheel or touchpad needs one notch's worth of travel. A direction reversal discards
  the remainder. Nothing is queued: a sample that arrives during a settle, a pan, a staged transition, or while a
  widget is raised is dropped and clears the accumulator, so a fast spin cannot bank pages that play back later.
- Navigation goes through the same staging, ease-out settle, and in-place promote as an edge-band click and uses the
  same suppression table: it MUST NOT navigate in any state where `ShouldShowEdgeAffordance` refuses that direction
  (renderer not ready, hidden, display off, suspended, occluded, raised, pan, settle, staged neighbour, single page,
  or a blocked end without `wrapPages`). A wheel sample is never a double-activate candidate.
- Samples are dropped, without accumulation, while the window is hidden, the display is off, a widget owns a
  captured drag (`RedXePointerCapture`), or a two- or three-finger page pan is in progress.
- Modifier keys are forwarded to the widget in `modifiers` and do not change host routing.
- Hide, resize, DPI change, settings apply, raise, dismiss, and capture or focus loss end the sequence outright.
- There is no wheel setting: direction and sensitivity are fixed and `Core_Settings.md` is unaffected.
- The wheel path adds no timer, wake-up, or allocation; the sequence gap is measured between the tick counts at
  which consecutive samples are handled.

## Widget raise overlay

A widget that does not already fill the client MAY raise into a host-owned overlay after a mouse double-click or a
touch/pen double-tap on its tile. The host MUST query `IRedXeRaisedWidget::GetRaisedExtent` and MUST NOT invent a
size. Invalid, missing, or full-client tiles stay in standard layout.

The overlay is a full-height slice whose width is 1/4, 1/3, 1/2, or 1/1 of the client, as returned by
`GetRaisedExtent`. The host MUST NOT invent that fraction. The slice stays on-screen and keeps covering the original
tile's column: it grows from the tile's left edge, then shifts left only as needed to remain inside the client. It MUST
NOT shrink either axis below the tile. Other tiles remain in their standard positions, keep rendering and scheduled
updates, and appear dimmed. A small DPI-scaled close control sits in the top-right of the slice; the plugin occupies
the full slice, including under that control. The close mark is a Segoe Fluent Icons glyph from `RedXe/FluentIcons.h`,
with the same MDL2 and Unicode fallback as other host chrome. The renderer's host chrome pipeline draws the dim as
strips around the slice (never over it), a drop shadow along the inner vertical edge, and the close mark, all as quads
in the swap chain: dim and shadow before the raised widget's draw, the close control after it. GPU pixels therefore
show through at full brightness with no window region, and there is no overlay HWND. A native-window container is a
layered child that receives the same dim as window alpha while another widget is raised. The close control MUST show
a hover wash and a brighter glyph, plus the hand cursor, while the pointer is inside its rectangle; that hover change
invalidates exactly one coalesced frame and MUST NOT add a timer. Leaving the close rectangle restores the idle chrome
with the next frame.

`SetRaised(TRUE)` runs before the overlay is shown; `SetRaised(FALSE)` runs after a dismiss settle completes, or
immediately when dismiss cannot animate. Raise and restore interpolate the overlay content rectangle from the tile to
the full-height slice (and back) with the same presentation-paced ease-out cubic as a page settle, duration clamped to
160–240 ms from travel. Dim alpha eases from 0 to 148 and back. The host reports the **final** overlay size to
`OnTargetSizeChanged` when raise starts and MUST NOT call it on each interpolated frame. `Render` draws at the live
viewport; native containers follow the interpolated HWND. Close, Escape, and a double-activate on the raised content
animate the restore. Resize, DPI change, settings reload, and shutdown snap dismiss with no animation. A tap on the
dim region MUST NOT dismiss it. A mouse double-click or touch/pen double-tap on the raised plugin content MUST restore
it, using the same interval and slop as raise. Two- or three-finger page swipe MUST NOT start or continue while a widget is raised. The
top-level window owns the close control's hover, click, and touch/pen tap.

GPU composition draws every current-page widget at its tile viewport, then draws a raised GPU widget once more at the
slice. GPU widget geometry MUST follow each actual draw size; the largest-target notification does not replace the
tile's layout. Hit testing uses the last drawn geometry, so raised input matches the final overlay draw.
It MUST NOT compose a transition page while raised. A raised window widget moves its host container to the
slice at full alpha and MUST NOT hide sibling native containers; because that container is a child HWND above the
swap chain it covers the host-drawn close control, so a raised native widget is dismissed by Escape or a
double-activate on its content.
Host-owned native containers forward mouse `WM_LBUTTONUP` to the top-level window (screen-converted, ignoring
pointer-synthesized mouse) so a GDI widget can raise and restore through the same double-activate path as a GPU tile.
Dismiss restores tile bounds.

GPU widgets that expose `IRedXeInteractiveWidget` receive host-forwarded pointer and drop events in widget-local
pixels. Hit-testing uses the same topmost tile bounds as raise. The first mouse or touch contact that activates the
top-level window is still forwarded: `Application` returns `MA_ACTIVATE` for `WM_MOUSEACTIVATE` and
`WM_POINTERACTIVATE`. `OnPointer` returning `S_OK` on Down/Up consumes the
contact for that widget and MUST NOT count toward double-activate raise. `S_FALSE` (padding, empty cell, or empty
tile) leaves raise and edge-click navigation unchanged. The host still forwards later Move/Up of that one-finger
contact to the same widget so it can start an internal pan after a padding Down. A two- or three-finger page pan that
locks sends `Cancel` and does not treat the contact as a launch. Mouse edge-band clicks never reach the widget. While a widget is raised, pointer
coordinates use the overlay content rectangle as the local origin.

A widget may return `RedXePointerCapture` for a hit-tested Down that requires an uninterrupted one-finger gesture. The host
then acquires mouse/touch/pen capture, routes matching Move/Up outside the original control, and suppresses one-finger
page pan, edge navigation and double-activate raise. A second or third concurrent touch may begin tracking a page
gesture; it cancels that capture only when the pan locks horizontally. Other one-finger contacts cannot replace the captured pointer. Consumed touch Up clears a pending double-activate candidate, matching mouse Up. `WM_POINTERDOWN` already
implicitly captures that contact to the window; explicit `SetPointerCapture` is best-effort. Capture API failure MUST
NOT Cancel the widget Down. Hide, resize, DPI change, a canceled/out-of-contact pointer, and cancellation still
deliver Cancel. Hit-testing uses the digitizer's raw contact point, not the predicted sample. The pointer record includes the actual viewport
dimensions, DPI and tile/raised view identity so a prepared final layout can map an animated viewport correctly.

The top-level HWND is an OLE drop target after `OleInitialize`. `DragOver` hit-tests a GPU interactive widget and
calls `OnDragOver`. `Drop` copies `CF_HDROP` filesystem paths and Unicode text that is a full URL into a bounded
`RedXeDropEvent` (1 through 8 items) and calls `OnDrop`. A native-window child that is not itself a drop target
(GdiOrbit) MUST NOT steal drops destined for a GPU tile. `Application` revokes the drop target before destroying the
HWND and calls `OleUninitialize` after dashboard teardown. Plugins MUST NOT initialize or uninitialize COM/OLE and
MUST NOT call `RegisterDragDrop`.

## Dashboard background

The canvas behind every page is the document `backgroundColor` (`Specs/Core/Core_Settings.md`; omitted is
`#000000`). The renderer clears the swap chain with it every frame, every bundled widget paints it wherever it paints
an opaque background, and the AV Control theme window surface takes it outside high contrast, so tiles on one page
share one background by default. A widget object's own `backgroundColor` overrides it for that instance only: the
host fills that tile with the override before the widget draws and builds the widget's provider with the same color,
so the override is uniform whether or not the plugin paints anything behind its content. A change to the document
color rebuilds the active page like any other runtime settings change.

## Host-owned placeholder tiles

A tile whose widget instance failed to construct at runtime, whose catalogued module could not be mapped, or whose
widget reported `RedXeWidgetStatusUnavailable`, is drawn by the host rather than by the plugin. The host keeps the
authored placement, fills the tile with its placeholder wash via `ID3D11DeviceContext1::ClearView`, and leaves every
sibling widget in its authored
geometry, still rendering and still scheduled. A page MUST NOT fail because one instance failed. Document-level
validation failures remain fatal and are unaffected. An unknown plugin ID is still a document failure; a known
catalogued DLL that `LoadLibraryExW` cannot map is not. A widget takes its tile back as soon as it reports any status
other than `Unavailable`; `Degraded` and `Initializing` never hand the tile to the host.

## Low-cadence scheduled frames

- A widget whose content changes at a moment it cannot predict calls `IRedXeHost::RequestFrame`, which coalesces one
  frame. It never forces one: every blocked, hidden, suspended, display-off, and occluded state still blocks.
  `RedXeWidgetFlagContinuousAnimation` describes a widget *type* and cannot express an instance whose motion starts
  and stops, so intermittent motion uses `RequestFrame` rather than declaring continuous animation or returning a
  short scheduled delay purely to be polled.
- An active widget may expose `IRedXeScheduledWidget` to request a relative deadline without making its page
  continuous. `DashboardHost` queries only widgets instantiated for the current page and, during a swipe, the one
  staged adjacent page; it caches no deadline for any other page.
- After a successful frame, the application selects the earliest valid delay from the current and staged dashboards,
  converts it immediately to a monotonic deadline, and blocks in one message-aware wait. Deadline expiry coalesces one
  ordinary frame invalidation.
- A continuous widget on either participating dashboard supersedes scheduled waits. An active pan, settle, or staged
  neighbor also supersedes them so the scroll ease and in-widget animation keep presenting. Unrelated messages do not
  render a clean static dashboard.
- Hidden, minimized, suspended, display-off, occluded, inactive-page, transition-cancel, and shutdown paths discard
  scheduled deadlines. Recovery invalidates once and establishes a fresh deadline after the recovery frame; missed
  deadlines are never replayed.
- A collapsed autohide dock is one more hidden state: after its single grip frame the host discards scheduled
  deadlines and blocks on messages regardless of continuous widgets; a raised widget, an active pan, settle, staged
  neighbour, or interactive capture holds a revealed autohide dock open, so none of that motion is ever cut by a
  hide (`UI_XeneonDisplayWindowing.md` "Autohide").

## Required validation

- Parser/schema tests reject mixed page shapes, `layout` keys, empty/mixed compiled areas, malformed widgets, invalid
  ratios, and excessive pages, widgets,
  areas, or depth.
- Geometry tests prove exact partitioning in landscape and portrait, including non-divisible dimensions and identical
  native/GPU edges.
- Host tests prove first-page startup, blank pages, dynamic reflow, active-page-only creation, transactional switching,
  and WARP rendering. Host tests prove that a live-reload reconfigure of an unchanged two-page document keeps the
  second page's widgets. Host tests prove that `Renderer::AdoptPrimaryDashboard` is transactional: a failed adopt leaves
  the renderer pointing at the previous dashboard after the incoming host is destroyed.
- Host tests prove that sliding through a native-window neighbor (GdiOrbit) succeeds at mid-settle and fully off-screen
  offsets, that promote does not mark the swap chain occluded, and that a full-page native child does not freeze
  subsequent frames.
- Navigation tests cover two- and three-finger eligibility, one-finger exclusion, that an uncommitted second contact
  does not steal a widget gesture, direction, axis lock, vertical rejection, rubber-band end stops, wrap, distance and flick
  commit, settle interpolation, capture loss, deferred adjacent staging, in-place commit without device recreation,
  current-plus-adjacent-only resource lifetime, and that page navigation requires presentation-paced frames even when
  DXGI reports the swap chain occluded.
- Host tests prove that a right-swipe onto a previous Matrix page still draws that Matrix while its viewport origin is
  negative, that Studio Clock and Desk Clock keep drawing when only part of the tile is on-screen, and that
  `LastFrameSuccessfulWidgetCount` includes those widgets rather than dropping a tile that is not completely visible.
- Host tests prove that Process Viewer and the System Data GPU family activate data collection only while visible,
  expose GPU and scheduled interfaces rather than native-window widgets, request no continuous frames when settled, and
  drain subscriptions when the page is no longer active. The shipped `System` page places one instance of each family
  widget.
- Host tests prove that Studio Clock pages remain non-continuous, aggregate guarded next-boundary delays, coalesce a
  wall-clock correction, add no resources or deadline while inactive, and stop scheduled work in every blocked state.
- Host tests prove that Desk Clock pages remain non-continuous, request the next-second boundary while static, request
  smooth presentation-paced frames only during a split-flap burst, return to a blocked wait at completion, add no
  resources or deadline while inactive, and stop scheduled work in every blocked state.
- Geometry tests prove raised overlay width fractions, full-height slices, close hit-testing, shadow placement along
  the inner edge, dim strips that surround but never overlap the content, double-activate interval/slop, reverse
  hit-test, rejection of already-full tiles, a quarter System Pulse column, and a stacked clock growing into a
  full-height slice. Host tests prove the host asks each System Data viewer for its shipped extent, raises Process
  Viewer to a half-width slice while every GPU tile still draws, restores every GPU tile on dismiss, and moves a
  GdiOrbit container into overlay content then back to its tile without skipping sibling GPU draws. Host tests also
  prove the host chrome pipeline: zero chrome quads on a settled page, the raised dim/shadow/close quads with and
  without hover, a revealed band's wash and chevron over every tile, one re-rasterization per DPI change, and no child
  HWND over the swap chain. Geometry tests also prove tile-to-slice interpolation endpoints, ease-out cubic mixing,
  raise-settle duration clamps, and that the parent edge-click predicate is
  `ShouldShowEdgeAffordance && PageEdgeBandContains`.
- LauncherTests prove drop of a file and of an `https://` URL reach `OnDrop`, that `iconSize` `huge` paginates eight
  shortcuts on a large tile while `small`, `medium`, `large`, and `automatic` change the per-page count, that a tall
  256×720 tile spreads at most two columns of `small` icons through the height with even gutters of at least 8 DIP
  between icon edges (and 8 DIP edge inset) instead of clustering, that 32 shortcuts are the closed cap, that a
  one-finger horizontal swipe follows the finger
  then settles to the next page without launching, that a page-dot tap animates rather than teleporting, and that a
  bottom page-dot strip is hittable when more than one launcher page exists. Consumed-click versus raise and two-finger swipe-over-tile host pan are
  Application pointer routing. HostPluginTests do not compile `Application.cpp` and cannot
  fully prove that path; interactive checks cover click-to-launch, raise on padding, one-finger launcher paging, and
  two- or three-finger host pan.
- Edge-navigation geometry tests cover band rectangles at multiple DPIs, the narrow-client clamp, inclusive/exclusive
  band hit testing, chevron glyph selection across all three font tiers, chevron cell centring and containment,
  degenerate inputs, placement against a reachable area that is clipped or offset from the client origin, that a
  work-area taskbar does not shorten the band, and that an edge click's settle target and duration match a committed
  swipe in the same direction.
- Edge-navigation policy tests cover every row of the suppression rule, both non-wrapping ends, wrapping, a
  single-page document, and rejection of any direction other than previous and next.
- Wheel policy tests (`WheelNavigation.h`, `WheelDetent.h`) cover whole-detent stepping from fractions, a
  reversal discarding the remainder, zero and non-finite samples, the direction mapping of both axes, a consumed
  first sample latching the widget so a later declined sample does not reach the host, a declined first sample
  latching the host so a later consumed answer does not steal the sequence, the sequence gap, one navigation per
  notch with no banked surplus, per-axis accumulation, clearing on refusal, and a promote releasing only a widget
  latch. Host tests prove that a single-page Weather tile and every System Data viewer at its first page decline
  wheel-up and every horizontal sample. `LauncherTests` prove wheel-up at the first launcher page is declined, a
  fractional sample is accepted without paging, a whole detent pages forward with the slide entering from the right,
  tilt-left pages back with the slide entering from the left and is declined at the first page, and a dot tap slides
  the same way. `AVControlTests` prove a horizontal sample is declined and a phase past it is rejected. The
  end-to-end routing lives in `Application.cpp`, which `HostPluginTests` does not compile; it is validated on the
  live window by posting `WM_MOUSEWHEEL`/`WM_MOUSEHWHEEL` to the Debug build under `--screenshot`: one wheel-down
  notch over a non-interactive page captures the next page, wheel-up at the first page without `wrapPages` captures
  the same page, two tilt-right notches 700 ms apart capture the third page, and three notches 40 ms apart capture
  only the second. Over an eight-page Launcher tile (`--widget 0` crop) one notch captures its second icon page
  and not its third (the first sample is delivered once), two notches 400 ms apart capture its third page with the
  dashboard page unchanged (the sequence stays with the widget). Over a four-page Thermal Meter tile with a second
  dashboard page available, one wheel-up notch at its first page and six notches 700 ms apart past its last page
  both capture the same dashboard page (the tile keeps the wheel and sits on its last page), while one notch over
  the Matrix tile captures the next dashboard page.
- Host tests prove that a valid page produces no placeholder tiles, that a widget reporting unavailable hands exactly
  its own tile to the host while siblings keep drawing and the frame still composes, that recovery restores it, and
  that a catalogued plugin whose module cannot be mapped becomes a placeholder without aborting the page. The renderer
  obtains `ID3D11DeviceContext1` at device creation so the placeholder wash is actually filled.
- Interactive validation on a touch-capable XENEON SHOULD verify two- and three-finger tracking, settle, one-finger
  delivery to interactive widgets, and both orientations before
  release. It SHOULD also verify double-tap raise, the close control, and Escape dismiss. With a mouse it SHOULD
  verify edge reveal, chevron direction, click navigation in both directions, both document ends with and without
  `wrapPages`, that a revealed band shows the hand cursor and a click changes page even if the mouse has not moved
  since the band appeared, that double-click raise still works outside the bands, that double-click or double-tap on
  raised content restores the tile, that the close control hover-washes, and that raise and restore ease rather than
  jump. With a wheel it SHOULD verify that a notch over a clock changes page, that over a Process Viewer with page
  dots the wheel pages its overflow and never changes the dashboard page, not even past either end, that a tilt
  wheel and a precision touchpad navigate, and that a Logicon dialpad dial (`hwheel`) over the window changes page.

## Implementation anchors

The application lazily creates its UI Automation root on WM_GETOBJECT for UiaRootObjectId. Participating prepared
GPU widgets appear as virtual fragment children in dashboard slot order. Only the raised widget is exposed during
a settled raised view. Hidden, suspended, occluded, display-off, moving-page/overlay and settings-error-modal states
remove these children immediately. Reappearing views get fresh attachment identities. This integration covers
opted-in widget content; it does not assert that unrelated host chrome or every plugin is accessible.

Accessibility focus and committed raise/dismiss are processed through one generation-tagged posted message.
The queue is bounded to 32 widget slots, coalesces repeated requests and keeps only the latest requested focus.
The application updates geometry after successful preparation or placement/focus changes; clean frames must not
call into a widget or retry unavailable providers. The COM contract lives in Plugins_API.md. Hidden-window component
tests exercise the production root and AV DLL; real assistive-technology validation remains a separate gate.

- Typed pages and split paths: `RedXe/Settings.*`
- Geometry and native containers: `RedXe/DashboardHost.*`
- Pointer policy, settle, and commit math: `RedXe/PageNavigation.h`
- Mouse edge band geometry and reveal policy: `RedXe/PageEdgeAffordance.h`
- Mouse wheel sequence owner, gap, and detent accumulation: `RedXe/WheelNavigation.h` over `Common/WheelDetent.h`
  (the detent helper is shared with the bundled paged widgets)
- Page orchestration, double-activate, wheel routing (`OnMouseWheel`), and overlay HWND: `RedXe/Application.*`
- GPU transition composition: `RedXe/Renderer.*`
- Raised overlay math: `RedXe/WidgetRaise.h`
- Tests: `Tests/SettingsTests/`, `Tests/HostPluginTests/`
