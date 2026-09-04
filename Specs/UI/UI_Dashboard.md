# RedXe adaptive dashboard and page-navigation contract

Status: current normative product contract
Last reviewed: 2026-09-04
Owner: `DashboardHost` layout, active-page composition, page navigation, edge-navigation chrome, host placeholder tiles, and raised overlay chrome

## Scope

This specification owns ordered pages, responsive layout compilation, runtime orientation reflow, widget geometry,
horizontal touch and pen navigation, mouse edge navigation, host-owned placeholder tiles, and the host
raised-overlay chrome. Settings syntax belongs to `Specs/Core/Core_Settings.md`; plugin identities and
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
- A page with no `layout` is blank. A page contains at most 32 widget appearances.
- Only the current page owns runtime resources, except while swiping when the adjacent transition page may also own
  resources. All other pages own no provider, widget, data subscription, child HWND, D3D resource, timer, acquisition,
  or frame work.

## Adaptive layout tree

A nonblank page root layout contains exactly `arrangeAlong` and nonempty `areas`.

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
- Resize and DPI changes recompute physical bounds without reparsing or changing the authored tree.

## Horizontal touch and pen navigation

- Navigation is horizontal in both orientations. Swipe left advances; swipe right returns.
- Omitted or false `wrapPages` stops at the first and last page. True wraps either end to the opposite end. A blocked
  end follows the pointer with rubber-band resistance and MUST NOT instantiate a neighbor or commit.
- A page follows the pointer 1:1 after a horizontal lock. The host MUST NOT capture on contact. A widget that already
  owns an interactive captured pointer sequence retains it; otherwise a horizontal pan whose absolute X delta exceeds
  both the host threshold and the absolute Y delta becomes page navigation. Vertical-dominant movement MUST NOT switch
  pages and MUST leave the contact with the widget.
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

Touch and pen navigate by panning. A mouse navigates through a host-owned affordance at each client edge, so a mouse
user is never left without page navigation. This section owns that affordance; it changes nothing about the pan
contract above.

- The host owns one full-client-height band at each **reachable** left or right edge of the client. Reachable width is
  the client rectangle intersected with the work area of every display the window touches; reachable height is always
  the full client, including over a taskbar inset or a hanging title strip, so the band runs from the top of the
  window's client to the bottom. A window straddling two monitors is reachable across both, so the reachable width
  MUST NOT be clamped to a single monitor. For a window that fits on its monitor, and therefore always for the Release
  fullscreen window, that is the whole client and the band hugs the true client edge. A titled window can be wider
  than the monitor it sits on -- the Debug XENEON canvas frequently is -- and hugging the client edge would then place
  the band beyond the side of the screen, where no pointer can reach it. Band width is DPI scaled and clamped so the
  two bands can never meet: a narrow reachable area limits each band to one third of its width.
- A band exists only while the pointer is inside its zone. It is created on entry and destroyed on exit, in the same
  way the raised-overlay window exists only while a widget is raised. It MUST NOT be kept alive invisibly: a layered
  child window at zero alpha is transparent to hit testing and can never receive the hover that would reveal it, so
  an always-present band would be permanently inert.
- Hover is owned by the top-level window, which tests the live cursor position against each zone. Host-owned native
  containers notify the top-level window when the pointer moves over them so that hover can be detected over a
  native-window widget as well as over a GPU tile.
- A committed edge-click onto a page that contains a native-window widget MUST complete settle and promote the same
  way a GPU-only page does. Host-owned native children covering the swap chain MUST NOT be treated as DXGI occlusion:
  that report parked the settle and suppressed both bands for the rest of the session on the shipped Release second
  page. After promote, both bands MUST be available again whenever the suppression table allows them.
- A band exists only when the host is composing a live multi-page dashboard and that direction has a neighbour. It
  MUST NOT exist when the renderer is not ready, the window is hidden, the display is off, the renderer is suspended
  or occluded, a widget is raised, a pointer pan is in progress, or a settle or staged transition is in progress.
- A blocked end has no band. `wrapPages` and the first/last page rule are the same ones the pan path uses, so both
  input paths stop and wrap identically.
- A band reveals instantly and paints host GDI only: a translucent wash and a chevron pointing in the travel
  direction. The chevron is a Segoe Fluent Icons glyph from `RedXe/FluentIcons.h`, not a drawn shape, and falls back
  to Segoe MDL2 Assets and then to a standard Unicode chevron. The icon font is created once per DPI and cached. It MUST NOT add a timer, a continuous frame, or any other wake-up,
  so a cross-fade is out of contract. The host MUST NOT add Direct3D shaders for this chrome.
- Reveal responds to mouse input only. Mouse messages synthesized from a touch or pen contact are ignored so the pan
  path keeps them.
- Bands sit above host-owned native containers, so they reveal and accept clicks over a native-window widget exactly
  as over a GPU widget. Pointer messages that land on a band are forwarded to the top-level window, so a pan that
  begins inside a band behaves like one that begins anywhere else.
- Clicking a revealed band stages the adjacent page, then commits through the same ease-out settle and in-place
  promote a committed pan uses. It MUST NOT recreate the Direct3D device. Unlike a pan there is no follow-finger
  phase, so staging is synchronous at click time and the settle starts from rest, which places its duration at the
  clamped upper bound of the shared settle policy.
- A navigation click MUST NOT count as part of a double-activate raise gesture. Accepted trade: a revealed band
  consumes the click in its strip, so a widget under a band cannot be double-click-raised there; it stays raisable
  everywhere else in its tile. Band width is a single DPI-scaled constant so this is tunable in one place.
- Resize, DPI change, settings reload, page promote, raise, dismiss, and every visibility transition re-evaluate both
  bands. Re-evaluation with unchanged state MUST perform no window operations.

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
with the same MDL2 and Unicode fallback as other host chrome. Host GDI paints a layered dim over everything except the
slice, a drop shadow along the inner vertical edge, and the close mark. Dim and shadow use distinct cached GDI brushes
created for the overlay lifetime, not per paint. The overlay window region punches a hole over plugin content so GPU
pixels or a native child show through at full brightness, then adds the close rectangle back so the mark stays
clickable. The host MUST NOT add Direct3D shaders for this chrome.

`SetRaised(TRUE)` runs before the overlay is shown; `SetRaised(FALSE)` runs before standard tiles return. A close
hit, Escape, resize, DPI change, settings reload, or shutdown dismisses the overlay. A tap on the dim region MUST NOT
dismiss it. Page swipe MUST NOT start or continue while a widget is raised. The overlay HWND exists only while raised.

GPU composition draws every current-page widget at its tile viewport, then draws a raised GPU widget once more at the
slice. It MUST NOT compose a transition page while raised. A raised window widget moves its host container to the
slice and MUST NOT hide sibling native containers. Dismiss restores tile bounds.

## Host-owned placeholder tiles

A tile whose widget instance failed to construct at runtime, or whose widget reported
`RedXeWidgetStatusUnavailable`, is drawn by the host rather than by the plugin. The host keeps the authored placement,
fills the tile with its placeholder wash, and leaves every sibling widget in its authored geometry, still rendering
and still scheduled. A page MUST NOT fail because one instance failed. Document-level validation failures remain
fatal and are unaffected. A widget takes its tile back as soon as it reports any status other than `Unavailable`;
`Degraded` and `Initializing` never hand the tile to the host.

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

## Required validation

- Parser/schema tests reject empty/mixed areas, malformed widgets, invalid ratios, and excessive pages, widgets,
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
- Navigation tests cover direction, axis lock, vertical rejection, rubber-band end stops, wrap, distance and flick
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
  the inner edge, overlay region holes that keep the close control, double-activate interval/slop, reverse hit-test,
  rejection of already-full tiles, a quarter System Pulse column, and a stacked clock growing into a full-height
  slice. Host tests prove the host asks each System Data viewer for its shipped extent, raises Process Viewer to a
  half-width slice while every GPU tile still draws, restores every GPU tile on dismiss, and moves a GdiOrbit
  container into overlay content then back to its tile without skipping sibling GPU draws.
- Edge-navigation geometry tests cover band rectangles at multiple DPIs, the narrow-client clamp, inclusive/exclusive
  band hit testing, chevron glyph selection across all three font tiers, chevron cell centring and containment,
  degenerate inputs, placement against a reachable area that is clipped or offset from the client origin, that a
  work-area taskbar does not shorten the band, and that an edge click's settle target and duration match a committed
  swipe in the same direction.
- Edge-navigation policy tests cover every row of the suppression rule, both non-wrapping ends, wrapping, a
  single-page document, and rejection of any direction other than previous and next.
- Host tests prove that a valid page produces no placeholder tiles, that a widget reporting unavailable hands exactly
  its own tile to the host while siblings keep drawing and the frame still composes, and that recovery restores it.
- Interactive validation on a touch-capable XENEON SHOULD verify finger tracking, settle, and both orientations before
  release. It SHOULD also verify double-tap raise, the close control, and Escape dismiss. With a mouse it SHOULD
  verify edge reveal, chevron direction, click navigation in both directions, both document ends with and without
  `wrapPages`, and that double-click raise still works outside the bands.

## Implementation anchors

- Typed pages and split paths: `RedXe/Settings.*`
- Geometry and native containers: `RedXe/DashboardHost.*`
- Pointer policy, settle, and commit math: `RedXe/PageNavigation.h`
- Mouse edge band geometry and reveal policy: `RedXe/PageEdgeAffordance.h`
- Page orchestration, double-activate, and overlay HWND: `RedXe/Application.*`
- GPU transition composition: `RedXe/Renderer.*`
- Raised overlay math: `RedXe/WidgetRaise.h`
- Tests: `Tests/SettingsTests/`, `Tests/HostPluginTests/`
