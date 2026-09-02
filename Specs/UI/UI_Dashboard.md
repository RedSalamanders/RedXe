# RedXe adaptive dashboard and page-navigation contract

Status: current normative product contract
Last reviewed: 2026-09-02
Owner: `DashboardHost` layout, active-page composition, and page navigation

## Scope

This specification owns ordered pages, responsive layout compilation, runtime orientation reflow, widget geometry,
and horizontal touch navigation. Settings syntax belongs to `Specs/Core/Core_Settings.md`; plugin identities and
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
- Omitted or false `wrapPages` stops at the first and last page. True wraps either end to the opposite end.
- A page follows the pointer and on release commits after the threshold or returns to the current page.
- A widget that already owns an interactive captured pointer sequence retains it; otherwise a horizontal pan crossing
  the host threshold becomes page navigation.
- The host captures the navigation pointer until commit, cancellation, or capture loss. Vertical movement alone MUST
  NOT switch pages.
- During a swipe only the current and directionally adjacent pages may be instantiated and rendered. Cancellation
  tears down the staged neighbor. Commit makes it current and tears down the prior page.
- Resize, close, reload, and capture loss cancel an active transition safely.

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
- Navigation tests cover direction, direct manipulation, cancel/commit, end stops, wrap, capture loss, vertical
  rejection, and current-plus-adjacent-only resource lifetime.
- Interactive validation on a touch-capable XENEON SHOULD verify finger tracking and both orientations before release.

## Implementation anchors

- Typed pages and split paths: `RedXe/Settings.*`
- Geometry and native containers: `RedXe/DashboardHost.*`
- Page orchestration and capture: `RedXe/Application.*`
- GPU transition composition: `RedXe/Renderer.*`
- Tests: `Tests/SettingsTests/`, `Tests/HostPluginTests/`
