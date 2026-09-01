# Done: Studio Clock external dots and date spacing

Status: `COMPLETE`
Created: 2026-09-01
Completed: 2026-09-01
Owner: Studio Clock settings, procedural ring state, and date geometry

## Purpose

Add a default-enabled option that keeps all 12 outward five-second dots fully lit, while retaining progress-linked
active/dim behavior when disabled. Rebalance each date separator so its clear space to the following numeric group is
no longer visibly compressed.

## Frozen behavior

- Add closed boolean Studio Clock setting `externalDotsAlwaysOn`, default `true`.
- The setting affects only the 12 outward five-second companions. When true, all 12 use full `secondsColor` alpha;
  when false, companions at or before the displayed second are full and future companions remain at 18 percent alpha.
- The ordinary 60-dot progress circle always retains its existing active/dim second progression.
- `showSecondProgress:false` submits no ordinary or outward ring dots regardless of `externalDotsAlwaysOn`.
- Reuse `secondsAndFlags.w` in the existing 160-byte constant buffer for the setting; instance, draw, upload,
  allocation, resource, and scheduling bounds do not change.
- Move both three-dot date separator centers left from 0.394/0.529 to 0.38125/0.51625 of the square. This balances
  separator spacing without moving date digit groups, changing date formats, or widening the 10:9 dated composition.

## Settings alignment

- Update the plugin schema/default object, strict direct and normalized parsing, canonical host schema, both shipped
  settings templates' effective default behavior, Settings tests, focused plugin tests, and normative settings/plugin
  contracts together.
- Missing authored settings continue to receive `externalDotsAlwaysOn:true` through host default merging. Direct
  effective factory objects remain complete and reject a missing, duplicate, unknown, or non-boolean member.

## Required validation

- WARP tests prove all 12 outward dots are fully lit by default at second 00, while disabling the option restores
  active companion counts of 1/1/7/12 at seconds 00/01/30/59.
- Pixel probes prove the ordinary future ring remains dim when outward dots are forced on and that both relocated date
  separators render with a clear background interval before the following numeric group.
- Regenerate and inspect the 720×800 dated capture.
- Run formatting, Debug and Release x64 rebuild/tests, Release ARM64 rebuild, Release benchmark, skill validation, and
  `git diff --check`; reconcile durable behavior into the owning specs before moving this plan to Done.

## Implementation sequence

1. [x] Freeze setting semantics, shader-flag reuse, and balanced date separator coordinates.
2. [x] Implement the plugin schema/parser/default and shader behavior.
3. [x] Align canonical schema, templates/effective defaults, and settings/plugin tests.
4. [x] Add WARP state and date-spacing probes and inspect the dated capture.
5. [x] Run performance and the full validation matrix.
6. [x] Reconcile normative contracts, record evidence, move this plan to Done, and remove its WIP row.

## Completion evidence

- The plugin, host settings parser, canonical schema, Debug/Release templates, strict factory parser, and focused tests
  all publish or consume `externalDotsAlwaysOn:true` as the merged default. Non-boolean, missing direct-effective,
  duplicate, and unknown members remain rejected.
- WARP readback proves 12 fully lit outward dots by default at seconds 00/01/30/59 while the ordinary ring retains
  counts 1/2/31/60. With the option disabled, outward active counts return to 1/1/7/12. All 16 combinations of the
  four booleans are accepted, and hiding progress still removes all 72 ring instances.
- The inspected captures `.build/StudioClock-external-dots.bmp` and `.build/StudioClock-date-spacing.bmp` show the
  default bright outward markers and the wider right-side gaps. Pixel probes cover both relocated separator centers
  and clear background samples between each separator and the following numeric group.
- The implementation reuses `secondsAndFlags.w`; the constant buffer remains 160 bytes, the default/maximum instance
  counts remain 228/402, unchanged frames map zero times, and frames remain bounded at two draws.
- Six 2560×720 Release WARP runs produced CPU submission deltas of 298.868, 275.366, 280.600, 275.916, 341.148, and
  281.050 microseconds/frame and GPU timestamp values of 0.0908, 0.0918, 0.1191, 0.1079, 0.0974, and 0.1013 ms/frame.
  Representative medians are 280.825 microseconds and 0.0993 ms; the bounded dynamic-setting cost is recorded in the
  normative resource contract without any resource, allocation, draw, upload, or wake-frequency regression.
- `format.ps1`, Debug and Release x64 `test.ps1 -Rebuild`, Release ARM64 `build.ps1 -Rebuild`,
  `validate-skills.ps1`, and `git diff --check` passed. Durable behavior is reconciled into
  `Specs/Core/Core_Settings.md`, `Specs/Plugins/Plugins_API.md`, and `Specs/Core/Core_PerformanceAndResources.md`.

## Closeout condition

The change is complete because the default outward dots remain fully lit without changing ordinary progress, the
disabled option restores linked progression, date separator spacing is visibly balanced, all settings surfaces agree,
resource budgets remain unchanged, required validation passes, and durable behavior is normative outside this plan.
