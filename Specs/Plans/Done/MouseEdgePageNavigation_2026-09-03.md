# Mouse edge page navigation

Status: `COMPLETE`
Implemented: 2026-09-03
Completed: 2026-09-04
Created: 2026-09-03
Owner: dashboard page navigation, host overlay chrome, and mouse input routing

## Goal

Give a mouse user the page navigation a touch user already has. When the pointer enters a band along the left or right
edge of the client, a host-drawn overlay appears in that band with a directional arrow. Clicking anywhere in a revealed
band animates to the adjacent page using the existing ease-out settle, exactly as a committed swipe does.

Today `UI_Dashboard.md` defines horizontal navigation only for touch and pen through `WM_POINTER*`. `Application`
handles `WM_LBUTTONUP` solely to detect the double-click that raises a widget; it never sees `WM_MOUSEMOVE`, and the
host-owned native containers subclassed in `DashboardHost.cpp` forward only `WM_POINTER*` messages to the top-level
window. A mouse user currently cannot change pages at all.

Owning contracts: [`../../UI/UI_Dashboard.md`](../../UI/UI_Dashboard.md),
[`../../UI/UI_XeneonDisplayWindowing.md`](../../UI/UI_XeneonDisplayWindowing.md), and
[`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md).

Historical context:

- [`WidgetRaiseOverlay_2026-09-02.md`](WidgetRaiseOverlay_2026-09-02.md) — the layered child-HWND GDI
  overlay pattern this feature reuses, and the source of the double-click raise gesture it must not break.

## Scope

### Included

- Two host-owned edge affordance windows, their reveal rule, their arrow chrome, and their hit behavior.
- Click-to-navigate reusing the existing staging, settle, and promote path.
- Suppression rules for raised widgets, active pans, blocked directions, and every hidden or occluded state.
- Geometry and policy as pure functions in a new header, with tests.

### Excluded

- Keyboard page navigation, scroll-wheel navigation, and page indicator dots. None are requested here.
- Any change to touch or pen navigation semantics, thresholds, axis lock, rubber-band resistance, or flick commit.
- Any change to the raised-overlay contract beyond suppressing edge affordances while a widget is raised.
- Persisting the active page. `UI_Dashboard.md` states the first page is selected on every launch; that stands.
- A reveal cross-fade. See decision D2.

## Selected product

- **Two child overlay windows.** One per edge, class `RedXe.PageEdge`, created as
  `WS_CHILD | WS_CLIPSIBLINGS` with `WS_EX_NOACTIVATE | WS_EX_NOPARENTNOTIFY | WS_EX_LAYERED`, owned by
  `Application` in `wil::unique_hwnd`, and kept above every host-owned native container in z-order. A band is
  full client height and a DPI-scaled width.
- **Created on hover, destroyed on leave.** The band window exists only while the pointer is inside its zone, the way
  the raised-overlay HWND exists only while a widget is raised. The top-level window owns hover detection and tests
  the live cursor position against each zone.

  This replaces the original design, which kept both bands alive permanently at alpha 0 and expected them to detect
  their own hover. That does not work: Windows hit-tests a layered window by its transparency, so a child at alpha 0
  is click-through and never receives a mouse message at all. It was confirmed on the running app -- the band existed
  at the right rectangle, and `WindowFromPoint` inside it returned the parent.
- **Host GDI chrome only.** A translucent wash over the band plus a chevron drawn with `CreatePen` and
  `MoveToEx`/`LineTo`, following `Application::PaintRaiseOverlay`. No Direct3D shaders and no font loading for this
  chrome, matching the constraint already placed on the raised overlay.
- **Click navigates.** `WM_LBUTTONUP` inside a revealed band stages the adjacent page and starts the existing settle
  with a commit. The left band returns to the previous page; the right band advances.
- **Blocked directions have no band.** When `wrapPages` is false and the current page is the first or last, the band
  for that direction is not shown and does not accept input. `PageSwipeBlocksDirection` in `PageNavigation.h` already
  encodes this, including wrap.
- **Touch and pen are unchanged.** Bands ignore `WM_POINTER*` and forward it to the top-level window using the same
  mechanism `DashboardHost.cpp` already uses for native containers, so a swipe that begins on a band still becomes a
  pan. Mouse messages synthesized from touch are ignored for reveal.

## Two defects found by running it

Both were found by driving the real window rather than by reading the code, and both are recorded because the shape
of the fix matters more than the fix.

1. **A layered child at alpha 0 is click-through.** The first design kept both bands alive and invisible so they could
   detect their own hover. `WindowFromPoint` inside the band returned the top-level window, so the band could never
   receive the `WM_MOUSEMOVE` that would reveal it. Hover detection moved to the top-level window, and the band is now
   created only while revealed, so it is always opaque and always hit-testable.
2. **`TrackMouseEvent(TME_LEAVE)` fires immediately when the cursor is not already inside the window.** Arming it
   right after creating the band, before Windows had hit-tested the cursor onto the new child, made the band post
   itself `WM_MOUSELEAVE` and destroy itself the instant it appeared -- once per mouse move, invisibly. Leave tracking
   is now armed from the band's own first `WM_MOUSEMOVE`, when the cursor is provably inside it, and both leave
   handlers re-test the real cursor position instead of hiding unconditionally. Cursor position against the zone is
   the single source of truth, so no handler depends on leave-event ordering.

3. **A window straddling two displays was clamped to one of them.** The reachable area used
   `MonitorFromWindow(MONITOR_DEFAULTTONEAREST)`, which picks a single monitor, so a window spanning two screens had
   its band placed at the edge of one monitor's work area -- in the middle of the window, over the other screen.
   The reachable area is now the union of the work areas of every monitor the window intersects.

A fourth issue was presentation: the chevron was drawn as a three-point `Polyline`, which looks hand-made next to the
shell. Host chrome now draws icons from `RedXe/FluentIcons.h` (Segoe Fluent Icons, then Segoe MDL2 Assets, then a
Unicode stand-in), and `AGENTS.md` records that as the rule for all future host chrome.

A further issue was product rather than mechanism: on a display smaller than the window, the client's right edge -- and
so the right band -- sat off-screen and could not be reached at all. Bands are now placed against the *reachable*
client area, the client rectangle intersected with the display work area. For a window that fits on its monitor this
is identical to the previous behaviour.

## Open defect: native-window neighbour stalls navigation — resolved 2026-09-04

Committing an edge click onto a page that contains a native-window widget used to leave the page-transition state
engaged, which suppressed both bands for the rest of the session. DXGI reported the swap chain `OCCLUDED` when the
host-owned GdiOrbit child covered it; the frame loop then waited and settle never finished. The host now ignores that
false occlusion, keeps presenting through pan and settle, treats staged-neighbor suppression separately from settle, and
restores the previous dashboard if promote fails. Host tests slide through a GdiOrbit neighbor at mid-settle and
fully off-screen offsets and prove Present does not mark the swap chain occluded.

Reproduction (historical): a two-page document, page 1 a GPU widget, page 2 `builtin.gdi-orbit`, `wrapPages` false.
Hover the right edge, click, then hover either edge — no band appeared again. GPU-only pages were unaffected.

## Interaction and animation

Navigation reuses the committed-swipe path exactly; no second animation system is introduced.

1. `StageTransitionPage(direction)` creates the adjacent page and positions it at `direction * clientWidth`.
2. `_pageVelocityPxPerSec` is set to zero.
3. `BeginPageSettle(-direction * clientWidth, /*commit=*/true)` runs the existing ease-out.
4. `TickPageSettle` interpolates through `InterpolatePageOffset` and calls `PromoteTransitionPage` on completion.

With zero velocity, `PageSettleDurationMilliseconds` clamps the speed floor to 1400 px/s and the result to the
140–280 ms range, so a click produces a 280 ms ease-out on a full-width XENEON client. `PromoteTransitionPage` swaps
the staged dashboard in place and does not recreate the Direct3D device, which is the property `UI_Dashboard.md`
already requires of a committed swipe.

Unlike a swipe, a click has no follow-finger phase, so staging is synchronous at click time rather than deferred to
the next idle turn. The deferred-staging rule in `UI_Dashboard.md` exists to protect the first follow-finger frame and
does not apply here.

## Suppression

A band is hidden and inert whenever any of the following holds. Each maps to state `Application` already tracks.

| Condition | Reason |
| --- | --- |
| `_raisedActive` | `UI_Dashboard.md` already requires that page swipe MUST NOT start or continue while a widget is raised. |
| `_pagePointerActive`, `_pagePanStarted`, or `_pageSettleActive` | A pan or settle owns the offset; a click must not race it. |
| Not `_windowVisible`, display off, renderer suspended, or occluded | No chrome and no wake-ups in a blocked state. |
| `_rendererReady` is false, or `_settings` has one page and `wrapPages` is false | Nothing to navigate to. |
| Direction blocked by `PageSwipeBlocksDirection` | That edge has no neighbor. |

Bands are re-evaluated on settings apply, page promote, `WM_SIZE`, `WM_DPICHANGED`, raise and dismiss, and every
visibility transition. `CancelPageNavigation` hides both bands.

## Accepted trade

A revealed band consumes `WM_LBUTTONUP` in its strip, so a widget under the outer band cannot be double-click-raised
in that strip. It remains raisable everywhere else in its tile. Band width is a single DPI-scaled constant in the new
header so this can be tuned from one place, and the band never feeds `Application::OnClientActivateAttempt`, so a
navigation click can never register as half of a raise gesture. `_activateTick` and `_activateWidgetIndex` reset when
navigation commits.

## Work items

| ID | Item | Pass condition |
| --- | --- | --- |
| W1 | New `RedXe/PageEdgeAffordance.h` with pure functions: DPI-scaled band width, left/right band rectangles from client size, band hit test, chevron polyline points, and a `ShouldShowEdgeAffordance` predicate over the suppression table. | Header-only, `constexpr` where possible, no Win32 state, no allocation. Matches the shape of `PageNavigation.h` and `WidgetRaise.h`. |
| W2 | Register `RedXe.PageEdge` and add `EdgeAffordanceProcedure` plus `HandleEdgeAffordanceMessage` to `Application`, following `RaiseOverlayProcedure`/`HandleRaiseOverlayMessage`, including the `WM_NCDESTROY` ownership release. | Class registered once; both bands bind their `Application` at `WM_NCCREATE`; no exception crosses the callback. |
| W3 | Create, position, show, hide, and destroy both bands from one `RefreshEdgeAffordances()` called from every re-evaluation point listed under Suppression. | Band lifetime is idempotent and leak-free. Repeated calls with unchanged state perform no window operations. |
| W4 | Reveal on `WM_MOUSEMOVE` with `TrackMouseEvent(TME_LEAVE)`; hide on `WM_MOUSELEAVE`. Ignore mouse messages synthesized from touch or pen. | Hover reveal and leave hide are reliable across band, widget, and window boundaries. Touch never reveals a band. |
| W5 | Paint the wash and chevron with GDI in `WM_PAINT`; return 1 from `WM_ERASEBKGND`. | No flicker, no Direct3D, no font or DirectWrite dependency, no GDI object leaks. |
| W6 | Navigate on `WM_LBUTTONUP` through the four-step sequence above. | A click advances or returns exactly one page with the existing ease-out and in-place promote. |
| W7 | Forward `WM_POINTERDOWN`, `WM_POINTERUPDATE`, `WM_POINTERUP`, and `WM_POINTERCAPTURECHANGED` from a band to the top-level window, reusing the `DashboardHost.cpp` forwarding approach. | A touch pan that starts inside a band behaves identically to one that starts anywhere else. |
| W8 | Keep bands above host-owned native containers; re-assert z-order after `DashboardHost::Initialize`, `ApplyRaisedNativeLayout`, and `ClearRaisedNativeLayout`. | The band reveals and accepts clicks over a `GdiOrbit` tile as it does over a GPU tile. |
| W9 | Reposition and rescale both bands on `WM_SIZE` and `WM_DPICHANGED`. | Band width tracks the destination-monitor DPI; bands stay full client height in both orientations. |

## Decisions

- **D1 — blocked-edge presentation. RESOLVED: no band at a blocked edge.** `ShouldShowEdgeAffordance` reuses
  `PageSwipeBlocksDirection`, so both input paths stop and wrap identically and there is one end-stop rule rather than
  two. Recorded in `UI_Dashboard.md`.
- **D2 — reveal cross-fade. RESOLVED: instant reveal, no fade.** A fade needs a timer, and
  `Core_PerformanceAndResources.md` prohibits wake-ups that visible content does not require. Recorded in
  `UI_Dashboard.md` and in `Core_PerformanceAndResources.md`, which now states that edge chrome owns no timer.

## Required validation

- Geometry and policy tests in `Tests/HostPluginTests/HostPluginTests.cpp`, beside the existing `PageNavigation.h` and
  `WidgetRaise.h` coverage: band rectangles at multiple DPIs and both orientations, hit test at band edges, chevron
  point derivation, and `ShouldShowEdgeAffordance` across every row of the suppression table including wrap and both
  document ends.
- Host tests: a click in each band promotes the correct neighbor in place without recreating the Direct3D device;
  a click at a blocked end does nothing; bands disappear while a widget is raised and return on dismiss; a pan that
  begins on a band still navigates as a swipe; a settle in progress rejects a band click.
- Startup, resize, DPI change, settings reload, and shutdown leave no band HWND behind and no GDI objects allocated.
- A settled multi-page dashboard requests no continuous frames and gains no timer or wake-up from this feature.
- Build Debug and Release for x64 and ARM64; run `.\test.ps1`.
- Interactive check on the XENEON: reveal, arrow direction, click animation in both directions, wrap and non-wrap
  ends, and that double-click raise still works outside the bands.

## Outcome

Implemented on 2026-09-03. `RedXe/PageEdgeAffordance.h` holds the geometry and reveal policy as pure functions;
`Application` owns the two band windows, their reveal, their chrome, and the click-to-navigate path.

One implementation note worth carrying: `RefreshPageEdgeAffordances` is called from the frame loop through
`UpdateDashboardVisibility`, so it caches the last applied `PageEdgeState`, client size, and DPI and returns without
touching a window when nothing changed. That is what makes W3's "no window operations on unchanged state" true in
practice rather than only on paper.

## Checklist

- [x] Resolve decisions D1 and D2 before implementing W4 and W5.
- [x] Implement W1–W9.
- [x] Add the geometry, policy, and host tests named above. `TestPageEdgeAffordancePolicy` and
      `TestPageEdgeAffordanceGeometry` in `Tests/HostPluginTests/HostPluginTests.cpp`, including placement against a
      reachable area that is clipped or offset from the client origin.
- [x] Verify on the running application by driving the cursor and clicking, for pages composed of GPU widgets: hover
      reveals a band, the band hit-tests as `RedXe.PageEdge`, a click advances the page, the opposite band appears on
      the last page, and a second click returns. Verified for a window wider than its display, and for a window
      straddling two displays.
- [x] Fix the native-window neighbor stall: DXGI occlusion from a covering child is ignored, pan/settle keep
      presenting, and `TestNativeWindowNeighborSwipe` slides through `builtin.gdi-orbit` at mid-settle and off-screen
      offsets.
- [x] Extend `UI_Dashboard.md`: the navigation section now covers touch and pen, a new "Mouse edge navigation"
      section owns band geometry, the reveal rule, the suppression rule, blocked-edge behavior, the accepted
      double-click trade, and the required validation.
- [x] Add `RedXe/PageEdgeAffordance.h` to the implementation anchors list in `UI_Dashboard.md`.
- [x] Run `.\build.ps1`, `.\build.ps1 -Configuration Release`, and `.\test.ps1` for both configurations.
- [x] Build ARM64 Debug. `.\build.ps1 -Platform ARM64` succeeded in this environment.
- [x] `.\validate-skills.ps1` for this closeout.
- [ ] Interactive check on the XENEON with a mouse. Reveal, click navigation, and both document ends are verified by
      driving the cursor; what still needs a human is the *look*: chevron rendering and the ease-out animation, plus
      double-click raise still working outside the bands.
- [x] Move this plan to `Specs/Plans/Done/` and remove its WIP index row. Bands are full client height; only width
      follows the reachable display edge. Widget animation keeps presenting through pan, settle, and a staged neighbor.

## Exit criteria

This plan is complete when hovering either edge of a multi-page dashboard reveals a directional arrow, a click in a
revealed band animates to that neighbor through the existing settle and in-place promote, blocked ends and every
suppressed state behave as specified, touch and pen navigation and the double-click raise are unchanged outside the
bands, a settled dashboard gains no wake-up, decisions D1 and D2 are recorded, and the durable contract lives in
`UI_Dashboard.md` rather than in this file.
