---
name: settings-store
description: Implement or revise RedXe settings paths, Debug and Release settings files, schemas, cold recovery, file stamps, directory watching, and UI-thread live reload. Use whenever Settings.*, SettingsWatcher.*, settings templates, the user schema, or runtime settings application changes.
---

# RedXe settings store

Read `Specs/Core/Core_Settings.md`, `Specs/Core/Core_PerformanceAndResources.md`, and `Specs/Settings.schema.json` before
changing settings behavior. Use `yyjson` for document ownership and validation, `wil-raii` for files/events/change
notifications, `win32-windowing` for the posted UI message, and `plugin-development` when a setting recreates widgets.

## Contract

- Normal user files live under `%LocalAppData%\RedXe\Settings`. Debug selects
  `RedXe-debug.settings.json`; Release selects `RedXe-1.0.settings.json`.
- Keep `Settings/` templates, `Specs/Settings.schema.json`, the C++ parser, and the normative settings spec aligned in
  the same change. Do not publish a setting the executable cannot apply.
- The hidden self-test parses the deployed template only. It must never touch `%LocalAppData%`.
- Validate types, ranges, required members, duplicates, unknown members, schema version, and the 1 MiB limit before
  replacing typed runtime state. yyjson values and strings remain borrowed from their owning document.
- Cold invalid files are backed up and restored from the deployed template. Invalid live edits preserve both the
  edited file and the last valid runtime configuration.

## Live reload

- Watch the directory with Windows change notifications. Block indefinitely on stop/change handles; never poll.
- The worker only coalesces and posts `SettingsWatcher::kSettingsChangedMessage`. JSON parsing, widgets, HWNDs, and
  Direct3D remain on the UI thread.
- Acknowledge the coalesced message before reading so a later edit can post again. Deduplicate applied and rejected
  file stamps.
- Before replacing widget references, shut down renderer device callbacks and native child containers. Reconfigure
  widgets transactionally, rebuild the dashboard, and roll back the previous settings if apply fails.
- Stop and join the watcher before destroying its target HWND.

## Validation

Run `Tests/SettingsTests`, both x64 Debug and Release hidden WARP tests, Release ARM64 compilation, and
`.\validate-skills.ps1`. Confirm an interactive edit changes the dashboard without restart and an invalid edit leaves
the previous dashboard running without periodic CPU wake-ups.
