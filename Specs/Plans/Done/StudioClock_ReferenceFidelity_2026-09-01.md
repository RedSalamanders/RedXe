# Done: Studio Clock reference fidelity

Status: `COMPLETE`
Created: 2026-09-01
Completed: 2026-09-01
Owner: Studio Clock procedural Direct3D rendering and visual validation

## Purpose

Refine the original Studio Clock rendering after direct comparison with the user-provided reference. The first pass
captured the content hierarchy but used glyph dots that were too large and sparse, an upright block font, overly tall
time digits, loose secondary-row spacing, and a ring without the doubled five-second markers visible in the reference.

The reference remains visual inspiration only. RedXe will preserve original procedural geometry and will not copy the
product enclosure, logo, ruler, photograph, or branding.

## Frozen refinement contract

- Keep the existing plugin ID, widget type, settings schema, factory ABI, scheduled-widget ABI, time/date semantics,
  colors, device lifecycle, and two-draw pipeline unchanged.
- Replace the primary 5-by-7 cell grid with an original 5-by-11 dotted seven-segment vocabulary. Horizontal strokes
  contain five dots; vertical strokes use four intermediate dots per half. All rows receive a small forward x offset
  that decreases from top to bottom, producing the reference's slight oblique stance without a font dependency.
- Use the same denser vocabulary at reduced scale for numeric seconds and the optional date. Keep `HH:MM` dominant,
  place seconds beneath it with clear separation, and place the date below the seconds without colliding with the
  progress ring.
- Reduce dot radii and vertical pitch so glyph strokes read as fine LED dots rather than widely spaced beads. Tighten
  digit gaps around the colon and balance the four primary digits symmetrically around the composition center.
- Render 60 outer ring positions plus one inner emphasis dot at every multiple of five seconds, including zero. Each
  emphasis dot shares the active/dim state and `secondsColor` of its corresponding outer position.
- Keep the ring centered at twelve o'clock and clockwise. At second `s`, outer positions zero through `s` are active;
  inner emphasis positions whose represented second is not greater than `s` are active.
- Center the complete composition in the largest square fitting the viewport. Portrait, landscape, minimum-size, and
  DPI behavior remain unchanged.

## Resource budget

- One fullscreen background draw and one instanced dot draw.
- At most one unchanged 160-byte constant-buffer map when visual state changes; unchanged frames map zero times.
- Bounded instance counts: 222 primary time cells/separators, 110 seconds cells, 446 date cells/separators, and 72
  ring cells, for a maximum of 850 submitted instances.
- No texture, font, DirectWrite, WIC, runtime shader compiler, allocation, worker, timer, HWND, I/O, or synchronization
  is added to the render path.
- Release measurement must compare the refined result with the recorded first-pass CPU/GPU values. Any material
  regression must be recorded in the normative resource contract before closeout.

## Required validation

- WARP readback must prove outer active counts at seconds 00, 01, 30, and 59 and inner active emphasis counts of 1,
  1, 7, and 12 respectively.
- Pixel probes must cover the recalibrated primary time, seconds, outer ring, inner five-second marker, background,
  and configured colors.
- Focused tests must preserve all visibility combinations, date orders, non-square/minimum viewports, DPI cache,
  allocation, upload/draw, device-loss, resource-sharing, scheduling, and teardown coverage.
- Generate and visually inspect a 720-by-720 WARP capture beside the supplied reference. Review dot size/density,
  oblique stance, digit proportions, colon spacing, secondary-row spacing, doubled five-second markers, balance, and
  absence of copied branding.
- Run formatting, Debug and Release x64 rebuild/tests, Release ARM64 rebuild, Release benchmark, skill validation, and
  `git diff --check` before closeout.

## Implementation sequence

1. [x] Compare the reference and first-pass capture and freeze measurable geometry and marker behavior.
2. [x] Implement the 5-by-11 oblique segment vocabulary, calibrated layout, smaller dots, and doubled ring markers.
3. [x] Update deterministic WARP probes, ring-marker counts, instance budgets, and all existing focused/host tests.
4. [x] Generate and inspect the revised reference capture; iterate if proportions still diverge materially.
5. [x] Measure Release performance and run the required repository validation matrix.
6. [x] Reconcile `Plugins_API.md` and `Core_PerformanceAndResources.md`, record evidence, move this plan to Done, and
   remove its active index row.

## Completion evidence

- The final 720×720 WARP capture at `.build/StudioClock-reference-fidelity.bmp` was inspected beside the supplied
  reference. It preserves the original unbranded procedural composition while matching the denser 5×11 dot rhythm,
  forward oblique stance, compact main/seconds spacing, fine dot scale, and paired five-second ring markers.
- `StudioClockTests` proves outer active counts of 1/2/31/60 and companion counts of 1/1/7/12 at seconds
  00/01/30/59. It also passes configured-color probes for the main time, seconds, outer progress, inner companion,
  and background; every visibility/date toggle; square-fit and minimum canvases; DPI caching; device recreation;
  shared resources; zero steady render allocations; and bounded 404-default/850-maximum instance counts.
- Three 2560×720 Release WARP measurements produced CPU submission deltas of 287.055, 265.150, and 289.499
  microseconds/frame and GPU timestamp values of 0.0789, 0.0902, and 0.0842 ms/frame. The representative medians are
  287.055 microseconds and 0.0842 ms. Against the first-pass 244.160 microseconds and 0.0858 ms, the denser geometry
  accepts a bounded CPU increase for 48.5 percent more default dot instances while GPU time, two draws, one conditional
  upload, allocation behavior, scheduling, and resource ownership remain within contract.
- Repository formatting completed. `test.ps1 -Configuration Debug -Platform x64 -Rebuild` and
  `test.ps1 -Configuration Release -Platform x64 -Rebuild` passed every suite; `build.ps1 -Configuration Release
  -Platform ARM64 -Rebuild` passed; `validate-skills.ps1` validated all ten repository skills; and `git diff --check`
  passed.
- Durable geometry, marker-state, instance-budget, measurement, and validation requirements are reconciled into
  `Specs/Plugins/Plugins_API.md` and `Specs/Core/Core_PerformanceAndResources.md`.

## Closeout condition

The refinement is complete only when the procedural result visibly reflects the reference's fine dense dotted
typography, slight oblique stance, calibrated spacing, and doubled five-second markers; automated budgets and the full
build matrix pass; and all durable behavior is normative outside this plan.
