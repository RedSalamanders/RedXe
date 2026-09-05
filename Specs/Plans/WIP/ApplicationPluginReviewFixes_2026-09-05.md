# Application and plugin review fixes

Status: ACTIVE

Owning contracts: `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_PerformanceAndResources.md`,
`Specs/Core/Core_Settings.md`, `Specs/UI/UI_Dashboard.md`, `Specs/UI/UI_XeneonDisplayWindowing.md`.

## Scope and checklist

- [ ] Drain reserved/in-flight data deliveries before deactivation or subscription release returns; test with barriers.
- [ ] Synchronize plugin GPU lifetime checks and quiesce widgets before device teardown; test host recovery and page lifetime.
- [ ] Make widget persistence transactional across failed file writes; test rollback and later successful persistence.
- [ ] Keep Launcher tile and overlay layouts correct for each actual draw; test WARP pixels, hit testing, and allocations.
- [ ] Encode manual Weather locations as URL query values; test spaces, delimiters, Unicode, and bounds offline.
- [ ] Keep bounded diagnostic records valid UTF-8 JSONL after escaping/truncation; test long messages and following records.
- [ ] Notify GPU widgets on DPI-only changes and preserve notification identity across page transitions; test unchanged frames.
- [ ] Update normative contracts and run formatting, Debug/Release x64 tests, Release ARM64 build, and skill validation.
- [ ] Record results, move this plan to Done, and remove its active index entry.

## Validation policy

Tests use production code with isolated files, hidden WARP windows, and offline plugin fixtures. Callback barriers
exist only in tests. Production keeps bounded storage, event-blocked waits, and allocation-free render callbacks.
Generated logs belong under `.build/`.
