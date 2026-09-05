# Weather layout and location corrections

Status: DONE
Owners: `Specs/Plugins/Plugins_Weather.md`, `Specs/Plugins/Plugins_API.md`,
`Specs/Core/Core_PerformanceAndResources.md`, `Specs/Core/Core_Settings.md`

Correct the duplicated today forecast, clipped glyphs and overflowing fields shown in the user's screenshot.
Use available space for upcoming hourly forecasts and clearly labelled precipitation notices. A country centroid
must never masquerade as the computer's city; use the user's stated Paris location for this execution.

- [x] Correct forecast interval parsing, local-day grouping, and precipitation notice selection.
- [x] Fit glyph ink within atlas cells and measure/truncate labels without clipping.
- [x] Implement responsive current/hourly/future-day layout and precipitation notices.
- [x] Correct automatic location behavior and configure Paris for this execution.
- [x] Add deterministic parsing, layout, glyph, and WARP rendering regressions with preview artifacts.
- [x] Run formatting, Debug/Release x64 tests, ARM64 build, and skill validation; inspect rendered previews.
- [x] Reconcile normative contracts and developer guidance; move this plan to Done after validation.

Validation on 2026-09-05:

- Debug and Release x64 `test.ps1 -Rebuild` passed, including host/plugin integration, settings, WARP and isolated
  crash harnesses. Debug `test.ps1` also passed after the final queued-settings teardown ordering correction.
- ARM64 Release build passed. All builds reported zero warnings/errors. All ten repo skills validated.
- Fourteen WARP layout cases (seven sizes, Celsius/Fahrenheit and accented long names) produced zero out-of-bounds
  quads and zero Debug steady-render allocations. The 766x622 case shows nine hourly columns and four future days;
  the 1200x800 raised case shows twelve hours and eight future days. PNG previews were inspected under `.build/`.
- Offline service fixtures exercised the real widget network path, city precedence, location reuse, queued save,
  collect fallback, helper failure/cancellation and absence of geolocation DLLs in the host test process. Discovery
  uses a real disposable helper with deterministic test output; live Windows permission/device fixes are environment
  dependent and are not a test-suite prerequisite.
- The existing user Release Weather setting was changed to `Paris, France` with an adjacent backup. No shipped
  default was changed to a machine-specific city.
