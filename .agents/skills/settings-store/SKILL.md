---
name: settings-store
description: Implement or revise RedXe settings paths, Debug and Release settings files, schemas, cold recovery, file stamps, directory watching, and UI-thread live reload. Use whenever Settings.*, SettingsWatcher.*, settings templates, the user schema, or runtime settings application changes.
---

# RedXe settings store

Read `Specs/Core/Core_Settings.md`, `Specs/Core/Core_PerformanceAndResources.md`, and `Specs/Settings.schema.json` before
changing settings behavior. Bounded array plugin schemas and widget settings persist
(`IRedXeHost::PersistWidgetSettings` and `IRedXeWidget::CollectPersistentSettings`) are owned by
`Specs/Core/Core_Settings.md` and `Specs/Plugins/Plugins_API.md`. Persist merge, schema rejection, collect-on-exit, and the in-memory
`--self-test` write policy are owned by `Specs/Core/Core_Settings.md`. Use `yyjson` for document ownership and
validation, `wil-raii` for files/events/change notifications, `win32-windowing` for the posted UI message, and
`plugin-development` when a setting recreates widgets.

## Contract

- Normal user files live under `%LocalAppData%\RedXe\Settings`. Debug selects
  `RedXe-debug.settings.json`; Release selects `RedXe.settings.json`. Schema compatibility comes from the document,
  never the filename. Interactive diagnostics live under the sibling `%LocalAppData%\RedXe\Logs` folder as UTC-dated
  `RedXe-debug-YYYY-MM-DD.jsonl` / `RedXe-YYYY-MM-DD.jsonl`. `logRetentionDays` (omitted default 15, range 1–365)
  deletes older dated files and leftover undated `*.jsonl` / `*.jsonl.1` names. When the settings directory is not named
  `Settings`, logs are `<settingsDir>\Logs`. `--self-test` must never write that folder.
- Release performs a one-time unchanged rename from legacy `RedXe-1.0.settings.json` only when the new filename is
  absent; a present new-name file always wins.
- Keep `Settings/` templates, `Specs/Settings.schema.json`, the C++ parser, and the normative settings spec aligned in
  the same change. Do not publish a setting the executable cannot apply.
- Both shipped templates must contain a real placed example of every settings-visible entry in
  `RedXe/BundledPlugins.h`. Template validation must iterate that catalog rather than maintain a second plugin list.
- The hidden self-test parses the deployed template only. It must never touch `%LocalAppData%` and must never call
  `PluginHost::SetLogDirectory`.
- Cold start with no user file installs the template and continues. An unmapped catalogued plugin DLL is a
  placeholder, not a settings or startup failure.
- Validate types, ranges, required members, duplicates, unknown members, schema version, and the 1 MiB limit before
  replacing typed runtime state. yyjson values and strings remain borrowed from their owning document.
- `PatchWidgetInstanceSettings` merges supplied members into the stored instance object, validates the complete
  result, and patches `sourceDocument`. It MUST NOT replace unspecified members. Interactive persist MAY write the
  user file; `--self-test` keeps the merge in memory.
- Cold invalid files are backed up and restored from the deployed template. Invalid live edits preserve both the
  edited file and the last valid runtime configuration.

## Live reload

- Watch the directory with Windows change notifications. Block indefinitely on stop/change handles; never poll.
- The worker only coalesces and posts `SettingsWatcher::kSettingsChangedMessage`. JSON parsing, widgets, HWNDs, and
  Direct3D remain on the UI thread.
- Acknowledge the coalesced message before reading so a later edit can post again. Deduplicate applied and rejected
  file stamps.
- After a valid parse, reselect the page that was current when that page still exists (`PreserveActiveDashboardPage`)
  before applying. Launch still starts on the first page; the active page is not written to the document.
- Before replacing widget references, shut down renderer device callbacks and native child containers. Reconfigure
  widgets transactionally, rebuild the dashboard, and roll back the previous settings if apply fails.
- Stop and join the watcher before destroying its target HWND.

## Validation

Run `Tests/SettingsTests`, both x64 Debug and Release hidden WARP tests, Release ARM64 compilation, and
`.\validate-skills.ps1`. Confirm an interactive edit changes the dashboard without restart and an invalid edit leaves
the previous dashboard running without periodic CPU wake-ups.
