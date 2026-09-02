# RedXe adaptive dashboard and page-navigation contract

Status: current normative product contract
Last reviewed: 2026-09-02
Owner: `DashboardHost` layout, active-page composition, page navigation, and raised overlay chrome

## Scope

This specification owns ordered pages, responsive layout compilation, runtime orientation reflow, widget geometry,
horizontal touch navigation, and the host raised-overlay chrome. Settings syntax belongs to `Specs/Core/Core_Settings.md`; plugin identities and
rendering mechanisms belong to `Specs/Plugins/Plugins_API.md`; display/DPI policy belongs to
`Specs/UI/UI_XeneonDisplayWindowing.md`.

The terms **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative.

## Pages

- A document contains 1–16 ordered pages. Order is navigation order and the first page is selected on every launch.
  RedXe does not persist the last active page.
- Page `id` and `name` are optional metadata and do not control navigation. A missing name is displayed as `Page N`
  without modifying the document.
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

## Horizontal touch navigation

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
- During a swipe only the current and directionally adjacent pages may be instantiated and rendered. Cancellation
  tears down the staged neighbor. Commit makes it current and tears down the prior page.
- Resize, close, reload, and capture loss cancel an active transition immediately, without a settle animation.

## Widget raise overlay

A widget that does not already fill the client MAY raise into a host-owned overlay after a mouse double-click or a
touch/pen double-tap on its tile. The host MUST query `IRedXeRaisedWidget::GetRaisedExtent` and MUST NOT invent a
size. Invalid, missing, or full-client tiles stay in standard layout.

The overlay is a full-height slice whose width is 1/4, 1/3, 1/2, or 1/1 of the client, as returned by
`GetRaisedExtent`. The host MUST NOT invent that fraction. The slice stays on-screen and keeps covering the original
tile's column: it grows from the tile's left edge, then shifts left only as needed to remain inside the client. It MUST
NOT shrink either axis below the tile. Other tiles remain in their standard positions, keep rendering and scheduled
updates, and appear dimmed. A small DPI-scaled close control sits in the top-right of the slice; the plugin occupies
the full slice, including under that control. Host GDI paints a layered dim over everything except the slice, a drop
shadow along the inner vertical edge, and the close mark. The overlay window region punches a hole over plugin content
so GPU pixels or a native child show through at full brightness, then adds the close rectangle back so the mark stays
clickable. The host MUST NOT add Direct3D shaders for this chrome.

`SetRaised(TRUE)` runs before the overlay is shown; `SetRaised(FALSE)` runs before standard tiles return. A close
hit, Escape, resize, DPI change, settings reload, or shutdown dismisses the overlay. A tap on the dim region MUST NOT
dismiss it. Page swipe MUST NOT start or continue while a widget is raised. The overlay HWND exists only while raised.

GPU composition draws every current-page widget at its tile viewport, then draws a raised GPU widget once more at the
slice. It MUST NOT compose a transition page while raised. A raised window widget moves its host container to the
slice and MUST NOT hide sibling native containers. Dismiss restores tile bounds.

## Low-cadence scheduled frames

- An active widget may expose `IRedXeScheduledWidget` to request a relative deadline without making its page
  continuous. `DashboardHost` queries only widgets instantiated for the current page and, during a swipe, the one
  staged adjacent page; it caches no deadline for any other page.
- After a successful frame, the application selects the earliest valid delay from the current and staged dashboards,
  converts it immediately to a monotonic deadline, and blocks in one message-aware wait. Deadline expiry coalesces one
  ordinary frame invalidation.
- A continuous widget on either participating dashboard supersedes scheduled waits. Unrelated messages do not render
  a clean static dashboard.
- Hidden, minimized, suspended, display-off, occluded, inactive-page, transition-cancel, and shutdown paths discard
  scheduled deadlines. Recovery invalidates once and establishes a fresh deadline after the recovery frame; missed
  deadlines are never replayed.

## Required validation

- Parser/schema tests reject empty/mixed areas, malformed widgets, invalid ratios, and excessive pages, widgets,
  areas, or depth.
- Geometry tests prove exact partitioning in landscape and portrait, including non-divisible dimensions and identical
  native/GPU edges.
- Host tests prove first-page startup, blank pages, dynamic reflow, active-page-only creation, transactional switching,
  and WARP rendering.
- Host tests prove that Process Viewer and the System Data GPU family activate data collection only while visible,
  expose GPU and scheduled interfaces rather than native-window widgets, request no continuous frames when settled, and
  drain subscriptions when the page is no longer active. The shipped `System` page places one instance of each family
  widget.
- Host tests prove that Studio Clock pages remain non-continuous, aggregate guarded next-boundary delays, coalesce a
  wall-clock correction, add no resources or deadline while inactive, and stop scheduled work in every blocked state.
- Host tests prove that Desk Clock pages remain non-continuous, request the next-second boundary while static, request
  smooth presentation-paced frames only during a split-flap burst, return to a blocked wait at completion, add no
  resources or deadline while inactive, and stop scheduled work in every blocked state.
- Navigation tests cover direction, axis lock, vertical rejection, rubber-band end stops, wrap, distance and flick
  commit, settle interpolation, capture loss, deferred adjacent staging, in-place commit without device recreation,
  and current-plus-adjacent-only resource lifetime.
- Geometry tests prove raised overlay width fractions, full-height slices, close hit-testing, shadow placement along
  the inner edge, overlay region holes that keep the close control, double-activate interval/slop, reverse hit-test,
  rejection of already-full tiles, a quarter System Pulse column, and a stacked clock growing into a full-height
  slice. Host tests prove the host asks each System Data viewer for its shipped extent, raises Process Viewer to a
  half-width slice while every GPU tile still draws, restores every GPU tile on dismiss, and moves a GdiOrbit
  container into overlay content then back to its tile without skipping sibling GPU draws.
- Interactive validation on a touch-capable XENEON SHOULD verify finger tracking, settle, and both orientations before
  release. It SHOULD also verify double-tap raise, the close control, and Escape dismiss.

## Implementation anchors

- Typed pages and split paths: `RedXe/Settings.*`
- Geometry and native containers: `RedXe/DashboardHost.*`
- Pointer policy, settle, and commit math: `RedXe/PageNavigation.h`
- Page orchestration, double-activate, and overlay HWND: `RedXe/Application.*`
- GPU transition composition: `RedXe/Renderer.*`
- Raised overlay math: `RedXe/WidgetRaise.h`
- Tests: `Tests/SettingsTests/`, `Tests/HostPluginTests/`
