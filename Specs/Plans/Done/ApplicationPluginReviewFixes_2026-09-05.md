# Application and plugin review fixes

Status: COMPLETE

Owning contracts: `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_PerformanceAndResources.md`,
`Specs/Core/Core_Settings.md`, `Specs/UI/UI_Dashboard.md`, `Specs/UI/UI_XeneonDisplayWindowing.md`.

## Scope and checklist

- [x] Drain reserved/in-flight data deliveries before deactivation or subscription release returns; test with barriers.
- [x] Synchronize plugin GPU lifetime checks and quiesce widgets before device teardown; test host recovery and page lifetime.
- [x] Make widget persistence transactional across failed file writes; test rollback and later successful persistence.
- [x] Keep Launcher tile and overlay layouts correct for each actual draw; test WARP pixels, hit testing, and allocations.
- [x] Encode manual Weather locations as URL query values; test spaces, delimiters, Unicode, and bounds offline.
- [x] Keep bounded diagnostic records valid UTF-8 JSONL after escaping/truncation; test long messages and following records.
- [x] Notify GPU widgets on DPI-only changes and preserve notification identity across page transitions; test unchanged frames.
- [x] Update normative contracts and run formatting, Debug/Release x64 tests, Release ARM64 build, and skill validation.
- [x] Record results, move this plan to Done, and remove its active index entry.

## Validation policy

Tests use production code with isolated files, hidden WARP windows, and offline plugin fixtures. Callback barriers
exist only in tests. Production keeps bounded storage, event-blocked waits, and allocation-free render callbacks.
Generated logs belong under `.build/`.

## Validation results

- Clean Debug x64 and Release x64 `test.ps1 -Rebuild`: passed with zero build warnings/errors. Includes plugin ABI,
  system data, clocks, Launcher, Weather, settings, production host, hidden WARP smoke, and isolated crash capture.
- After the two-page recovery and log-flush follow-ups, both complete x64 suites passed again on the final source:
  `.build/fix-debug-final-tests.log` and `.build/fix-release-final-tests.log`. Per-assertion host logs are retained at
  `.build/x64/Debug/HostPluginTests.log` and `.build/x64/Release/HostPluginTests.log`.
- Release ARM64 host, plugin, and test builds: passed with zero warnings/errors (`.build/fix-arm64-final.log`).
  ARM64 execution was not attempted on this x64 host.
- Formatting: passed for all 76 C++ files. Skill validation: all 10 repository skills passed.
- Five-minute Release WARP stability runs passed for Matrix (9,500 frames), Studio Clock (301 frames, no corrective
  frames), and Desk Clock (8,212 frames). Each retained one provider/widget/device-resource set and released all GPU
  resources on teardown; both clocks performed no plugin-owned work during their hidden intervals. These isolated
  processes ran concurrently for resource/lifetime checks, not comparative timing. Observed private-byte deltas were
  1,032,192 / 3,178,496 / 9,658,368 respectively; full observations are in `.build/fix-*-soak.log`.
- Release Matrix benchmark at 2560x720: hardware CPU baseline/enabled 0.115/0.785 microseconds per frame; GPU
  baseline/enabled 0.0000/0.0092 milliseconds. Full private/working-set/heap measurements are in
  `.build/fix-matrix-benchmark.log`; these include driver allocation and are not a claim of zero process-wide growth.
- DPI notification tests use hidden WARP windows and simulated DPI values; no physical cross-monitor move was needed
  for this renderer notification fix. Live Weather requests are not used by regression tests.

The device-lifetime regression also exposed premature viewport calculation when a staged page exists during device
recreation. The host now defers both pages' size calculation until the recreated target exists. The final regression
rebuilds two surviving pages, verifies six successful widget draws, and verifies that shutdown hides both pages.

One Release host run returned failure without child output; its direct rerun and eight further runs passed. A
subsequent code check found an independent log idle-signal race: enqueue could reset the event between the writer's
empty check and idle signal. Both transitions now hold the queue lock. Tests exercise 64 four-record enqueue/flush
batches and verify all records on disk. `test.ps1` now retains host stdout/stderr and prints them on failure. The
original intermittent assertion could not be identified from the old runner's output and is not attributed to this
race without evidence.
