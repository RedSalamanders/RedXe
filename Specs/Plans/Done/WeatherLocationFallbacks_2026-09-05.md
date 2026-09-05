# Weather location fallbacks

Status: DONE (2026-09-05)
Owner: `Specs/Plugins/Plugins_Weather.md`, `Specs/Core/Core_PerformanceAndResources.md`

Add independent, bounded Windows location fallbacks inside the disposable helper. Configured cities remain
authoritative; discovery results keep the existing UI-thread persistence and per-widget reuse.

- [x] Try current Windows location, coarse Windows location, a recent legacy location report, then Windows' saved
  default position. Validate every result; isolate failures so each remaining source can run.
- [x] Cover fallback ordering, unavailable APIs, denial, invalid/old/inaccurate reports, persistence, cancellation,
  timeout and process isolation with deterministic offline tests.
- [x] Reconcile normative contracts and old active-plan exclusions; format, run Debug/Release x64 tests and ARM64
  compilation, validate skills, then move this plan to Done.

Resource budget: at most eight seconds for the primary acquisition and four for coarse acquisition; a twenty-second
parent deadline bounds initialization and synchronous fallback APIs too. No retries, subscriptions, new host threads,
resident location libraries, external IP-location service, or locale/country-centroid guesses.

Validation: 83 C++ files formatted; all 10 skills validated; Debug and Release x64 rebuild/test suites passed, including
WARP and crash tests. Final Debug incremental suite passed after the explicit async deadline was added. ARM64 Release
compilation passed. Builds reported zero warnings/errors. Offline tests exercise all three fallback successes through
real helper processes and UI-thread persistence, then recreate widgets from the saved city with discovery unavailable.
Cancellation completed within three seconds; a deliberately stalled child returned timeout within the asserted
19–25-second window. No live discovery was invoked against the configured Paris widget.

Build logs under `.build/logs/`:
- `redxe-rebuild-20260905_110339_281-pid95632-57a88e07.log` (Debug)
- `redxe-rebuild-20260905_110458_248-pid65104-3c895b8b.log` (Release)
- `redxe-build-20260905_110540_092-pid90916-7395a788.log` (ARM64)
- `redxe-build-20260905_110626_135-pid74440-c6ac8d66.log` (final Debug)
