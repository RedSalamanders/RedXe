# Launcher import and settings formatting

Status: DONE (2026-09-05)
Owners: `Specs/Core/Core_Settings.md`, `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_PerformanceAndResources.md`

- [x] Write persisted settings as readable two-space-indented JSON, with a final newline and the 1 MiB document cap.
- [x] Import taskbar pins into an empty Launcher's editable shortcuts, queue a UI-thread save immediately, retain
  collect fallback, append subsequent drops and preserve imported state after a failed drop save.
- [x] Ensure an earlier queued import cannot overwrite a later committed drop; cover formatting/round trips,
  limits/rollback, import/save/restart, unchanged visibility, missing queue and persistence failures.
- [x] Update contracts, run formatting, Debug/Release x64 tests, ARM64 build and skill validation; move to Done.

Release was initially running; an isolated Release suite passed without terminating it. Once the application closed,
the normal Release output was rebuilt and its entire suite passed too. Discovery and JSON serialization stay on cold paths with bounded storage; no workers,
timers or render-path allocation are added.

Validation: 83 C++ files formatted; 10 skills validated; Debug and Release x64 suites (including WARP, host integration,
settings rollback/watcher and crash capture) passed, with final Release ARM64 compilation. No build warnings/errors.
New tests include 4096-byte import limits using long Unicode shortcut names, saved-pin recreation without taskbar
access, queue absence/fullness, failure retention, drop rollback, queued-before-interactive ordering, readable stable
serialization for inline/named/override widgets, Unicode/escaped values and oversized-output rollback. Existing
per-user Debug/Release documents were already indented, so no direct user-file rewrite was necessary.

Logs under `.build/logs/`:
- `redxe-rebuild-20260905_113400_866-pid69200-4794070d.log` (Debug rebuild)
- `redxe-build-20260905_113716_914-pid7732-4a9360a8.log` (final Debug)
- `redxe-rebuild-20260905_113906_473-pid69880-a1df7fd4.log` (normal Release rebuild)
- `redxe-build-20260905_113924_939-pid113124-0b396393.log` (final ARM64)
