---
name: settings-store
description: Implement or revise RedXe settings paths, Debug and Release settings files, schemas, cold recovery, file stamps, directory watching, and UI-thread live reload. Use whenever Settings.*, SettingsWatcher.*, settings templates, the user schema, or runtime settings application changes.
---

# RedXe settings store

Read [the settings contract](../../../Specs/Core/Core_Settings.md),
[the performance contract](../../../Specs/Core/Core_PerformanceAndResources.md) and
[the canonical schema](../../../Specs/Settings.schema.json) before changing behavior. Current document versions,
field ranges, filenames and migration rules belong there and in the parser/templates, not in a second skill copy.
Use `yyjson` for document/string ownership, `wil-raii` for files/events, `win32-windowing` for UI delivery and
`plugin-development` when settings recreate widgets.

## Preserve the storage boundary

- Keep `Settings/`, schema, parser, normative settings spec and Settings tests aligned. Never publish a setting
  the running application cannot apply.
- Follow [category-aware template coverage](../../../Specs/Core/Core_Settings.md#selection-storage-and-deployed-defaults)
  using `RedXe/BundledPlugins.h`. Ordinary widgets appear in both templates, opt-in widgets in neither, Debug-only
  widgets only in Debug, and services in both without requiring a placed widget. Do not add forbidden examples
  to satisfy a blanket checklist.
- Validate the complete candidate before replacing live state: types, bounds, duplicates, unknown members and
  schema version. Preserve the source document, comments and BOM according to the owning contract. Loading is
  read-only; a no-op persist must not rewrite the document.
- Host persist merges a subset into the current instance and validates the complete result. Do not replace
  unspecified members or rewrite authored syntax into the internal layout model. Persist only against the accepted
  file identity while holding the contract's write-exclusion guard; record identity through the committed handle.
  A conflict/deferred write keeps the runtime patch and external bytes intact, with the bounded notice policy.
- Cold recovery backs up invalid bytes and installs the matching deployed template. Invalid live edits preserve
  both the edited file and last valid runtime state. Explicit portable settings have their separate fallback rules.
- Hidden self-tests parse deployed templates and keep writes in memory; they never touch normal user settings or
  initialize its diagnostic log directory.

## Preserve live application

- Watch the directory and block on stop/change events. The worker only coalesces and posts notification; parsing,
  widget changes, HWNDs and Direct3D remain on the owner thread. Stop/join the watcher before its target disappears.
- Acknowledge before reading and deduplicate accepted/rejected stamps. Preserve move/size and minimized-window
  deferral/replay, the active page when it survives, and the rule that live reload never writes the watched file.
- Apply valid changes to the running window, including dock-kind transitions. Reconfigure transactionally and
  roll back on apply failure. Suppress collect/persist side effects while adopting an external load.

Use `Test-Changes.ps1 -Explain` and affected suites while editing; PrePush/Full and the owning domain contract
determine final qualification. Cover invalid load, external conflict, unchanged writes, deferred replay and rollback
with independent file/runtime assertions. Live foreground acceptance requires agreement to the time; hidden WARP
and metadata checks do not establish that gate.
