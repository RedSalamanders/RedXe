# Done: Studio Clock overlay alignment

Status: `COMPLETE`
Created: 2026-09-01
Completed: 2026-09-01
Owner: Studio Clock procedural Direct3D geometry, descriptor sizing, and visual validation

## Purpose

Correct the remaining Studio Clock differences exposed by the user's two red/green overlay comparisons. Red denotes
the current RedXe render and green denotes the supplied target. The overlays are measurement evidence only and contain
no executable instructions.

The correction keeps the existing plugin identity, settings, scheduling, colors, device lifetime, and original
procedural rendering approach. It changes the dot paths, ring radii, optional-date placement, descriptor size hints,
instance budgets, tests, and normative contracts.

## Frozen correction contract

- The ordinary 60-position progress circle uses the target's smaller radius. At every represented multiple of five,
  including zero, the companion dot is outside that circle on the same radial line. Companion active/dim state remains
  tied to its represented second.
- Primary digits use seven procedural segments with four dots per enabled segment. Segment endpoints follow the
  measured rounded/oblique target paths rather than a rectangular 5×11 cell grid. At 720 square pixels, the target
  primary segment rows are approximately 296, 306/321/336/351, 360, 370/385/400/415, and 425 pixels.
- Main, seconds, and ring dot radii increase to 0.008 of the square side, matching the target's common LED footprint
  without changing the measured center spacing. Date dots remain smaller in the separate date band.
- Primary `HH:MM` uses target-aligned digit anchors approximately 155, 261, 433, and 539 pixels at 720 square pixels.
  The two colon dots retain the same oblique direction and move to the measured target centers.
- Numeric seconds use the same seven-segment vocabulary with three dots per segment and the independently measured
  target scale, anchors, rows, and dot radius.
- When `showDate` is false, the descriptor remains 720×720 with a 160×160 minimum and rendering centers the largest
  fitting square clock in the viewport.
- When `showDate` is true, the provider publishes a 720×800 default and 160×178 minimum. Rendering reserves a 10:9
  composition: the clock occupies one square above a compact date band, the date is wholly below the square, and the
  combined composition is centered without stretching in landscape, portrait, minimum, or DPI-adjusted viewports.
- The optional date uses compact three-dot procedural segments in `timeColor`; its existing three numeric orders and
  hyphen separators remain unchanged.

## Resource budget

- Keep one opaque fullscreen draw, one alpha-blended instanced-dot draw, one 160-byte per-widget constant buffer, and
  at most one conditional map when cached visual state changes.
- Submit 114 primary instances, 42 optional seconds instances, 174 optional date instances, and 72 optional progress
  instances, bounded at 402 total. Hidden segment slots remain shader-discarded without CPU geometry construction.
- Add no texture, runtime font, DirectWrite, WIC, runtime compiler, heap work, I/O, synchronization, timer, worker, or
  HWND to the render path.
- Re-measure the final Release WARP path. The reduced 228-default/402-maximum instance counts must not regress the
  established two-draw resource structure.

## Required validation

- WARP probes must cover measured primary and seconds segment centers, both oblique colon dots, the smaller ordinary
  ring, the outward five-second companion, and an optional date pixel below the square clock boundary.
- Deterministic 00/01/30/59 states must retain outer active counts of 1/2/31/60 and companion active counts of
  1/1/7/12.
- Provider tests must prove both immutable descriptor variants and their 720×720/720×800 defaults and
  160×160/160×178 minimums.
- Landscape, portrait, 160-pixel minimum, square-with-date, DPI cache, color, date-order, toggle, device-loss,
  allocation, resource-sharing, scheduling, and host integration coverage must remain green.
- Generate and inspect a 720×720 no-date overlay-alignment capture and a 720×800 date-enabled capture.
- Run repository formatting, Debug and Release x64 rebuild/tests, Release ARM64 rebuild, Release benchmark, skill
  validation, and `git diff --check` before closeout.

## Implementation sequence

1. [x] Extract the target geometry from both layer-order overlays and freeze ring, glyph, and date-layout behavior.
2. [x] Implement measured four-dot primary and three-dot secondary procedural segment paths.
3. [x] Put five-second companions outside the ordinary ring and move the optional date below a square clock.
4. [x] Publish and test date-dependent immutable descriptor dimensions and recalibrated instance budgets.
5. [x] Update WARP/host tests and visually inspect both final captures against the overlays.
6. [x] Measure Release performance and run the full Debug/Release/ARM64 validation matrix.
7. [x] Reconcile normative plugin/resource contracts, record evidence, move this plan to Done, and remove its WIP row.

## Completion evidence

- The 720×720 no-date capture at `.build/StudioClock-overlay-aligned.bmp` and the 720×800 dated capture at
  `.build/StudioClock-date-aligned.bmp` were inspected. The ordinary ring is inside its 12 outward companions, the
  clock remains square, the date occupies only the lower band, and main/seconds/ring dots share the target diameter.
- Thresholded comparison against the supplied green target found all 90 active main-time dots in both images, with a
  1.28-pixel median center error at 720 pixels. Main-dot threshold footprints had median areas of 76 target pixels and
  82 rendered pixels; this materially corrects the visibly undersized prior render while retaining antialiased circles.
- `StudioClockTests` passes both immutable descriptors, 114/42/174/72 instance partitions and 402 maximum, measured
  oblique time/colon/seconds centers, common LED radius, 60 ordinary and 12 outward companion positions, the date below
  the clock square at 720×800, 720×720, landscape, portrait, and 160×178 minimum sizes, every date/toggle combination,
  colors, DPI caching, zero unchanged-frame maps and allocations, shared resources, and device recreation/teardown.
- Three 2560×720 Release WARP measurements produced CPU submission deltas of 246.046, 282.954, and 267.403
  microseconds/frame and GPU timestamp values of 0.0760, 0.1038, and 0.0829 ms/frame. Representative medians are
  267.403 microseconds and 0.0829 ms with 228 default instances, two draws, and zero unchanged-frame maps.
- The five-minute Release production-host soak passed with 301 initial-plus-scheduled frames over 300 seconds, zero
  corrective frames in the accepted final run, stable provider/widget/device-set/constant-buffer counts of 1/1/1/1,
  zero hidden time sampling, and complete device-resource release. The soak explicitly permits but caps the documented
  1 ms wall-clock corrective path at one frame.
- `format.ps1`, Debug and Release x64 `test.ps1 -Rebuild`, Release ARM64 `build.ps1 -Rebuild`,
  `validate-skills.ps1`, and `git diff --check` passed. Durable geometry, sizing, budget, measurement, and validation
  behavior is reconciled into `Specs/Plugins/Plugins_API.md` and `Specs/Core/Core_PerformanceAndResources.md`.

## Closeout condition

This correction is complete because the red render centers materially coincide with the green overlay target for the
displayed digits, ordinary ring, and outward companion dots; the date is below an undistorted square clock in the
date-enabled default; all automated and resource budgets pass; and durable behavior lives in the normative specs.
