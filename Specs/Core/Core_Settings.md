# RedXe settings contract

Status: current normative product contract
Last reviewed: 2026-08-31
Owner: `SettingsStore`, `SettingsWatcher`, and UI-thread application orchestration

## Scope

This specification owns settings selection, the version 4 host document, plugin-settings validation, cold recovery,
diagnostics, and live reload for one physical XENEON display. Dashboard layout and page navigation belong to
`Specs/UI/UI_Dashboard.md`; plugin ABI belongs to `Specs/Plugins/Plugins_API.md`; all resource rules in
`Specs/Core/Core_PerformanceAndResources.md` remain mandatory.

The terms **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative.

## Selection, storage, and deployed defaults

Without a command-line override, RedXe selects one independent editable file under
`%LocalAppData%\RedXe\Settings\`:

| Build | File |
| --- | --- |
| Debug | `RedXe-debug.settings.json` |
| Release | `RedXe.settings.json` |

The filename MUST NOT encode the settings schema version; compatibility is determined exclusively by the document's
`version` member. Debug and Release MUST NOT seed from or read one another. `--settings <path>` selects one dedicated portable document
instead. RedXe MUST monitor the selected external file at its own location. Duplicate `--settings` arguments and a
missing path are command-line errors.

The build output MUST contain both templates and `RedXe.settings.schema.json`. Repository sources are `Settings/` and
`Specs/Settings.schema.json`. The hidden `--self-test` path MUST use the deployed template and MUST NOT touch or watch
the user's settings directory.

Both shipped templates MUST contain one or two pages and demonstrate every settings-visible plugin compiled into the
product. Their first page SHOULD remain a representative low-resource startup composition; another page MAY provide
a denser gallery and alternate settings. Adding or removing a bundled plugin requires updating both templates and the
template validation test in the same change.

## Version 4 document

RedXe accepts standard JSON semantics plus comments and trailing commas. It MUST reject duplicate object members and
other JSON5 extensions. The source MUST NOT exceed 1 MiB.

The root members are:

| Member | Required | Contract |
| --- | --- | --- |
| `$schema` | No | When present, `RedXe.settings.schema.json`. |
| `version` | Yes | Object with required `major` and optional `minor`; omitted minor is zero. |
| `wrapPages` | No | Omitted or false stops at page ends; true wraps. |
| `declare` | No | Reusable widget definitions keyed by authored names. |
| `pages` | Yes | One through sixteen ordered pages. |

Version 4 begins at major 4, minor 0. Missing, malformed, or different-major versions are errors. A newer RedXe accepts
older minors and supplies documented defaults. An older RedXe accepts a newer minor of the same major, validates the
shape it understands, and silently ignores additive unknown host fields. Exact-version unknown host fields are
errors. A future editor saving a compatible newer-minor file MUST preserve unknown fields. Version 3 is not migrated.

### Declarations and widgets

`declare` is optional and has at most 128 members. A declaration name is 1–128 Unicode code points, may contain spaces,
and matches references by the exact case-sensitive sequence without normalization. Every declaration is a widget
definition containing required `plugin` and optional object `settings` members. There is no user-visible plugin
registry, enable flag, type ID, plugin-private object, or separate instance-private object.

A layout leaf's `widget` is an inline definition, a declaration-name string, or
`{ "use": <name>, "override": <merge-patch> }`. Each appearance creates an independent runtime widget instance. An
override applies to the complete declaration: absent fields inherit, object members merge recursively, scalars
replace, arrays replace in full, and null removes an inherited member. The result MUST contain one valid plugin and
settings object. An override MAY replace the plugin.

Every authored settings object, override, effective settings object, plugin schema, and plugin defaults object MUST
have a compact representation no larger than 4096 bytes.

### Plugin settings contracts

Every settings-visible plugin MUST export its immutable static settings contract before provider or widget creation.
The contract contains the stable plugin ID, an independently versioned schema contract, a bounded Draft 2020-12 JSON
Schema for its settings object, and bounded defaults. Defaults MUST validate against the schema. Discovery MUST create
no provider, widget, HWND, device resource, timer, or worker.

RedXe validates host structure, resolves declarations and overrides, discovers every unique referenced plugin at most
once, and validates every effective widget on every page. Inactive pages create no runtime widget resources. Contract
or settings failure rejects the complete candidate. ABI details are normative in `Specs/Plugins/Plugins_API.md`.

## Pages and layout

`pages` contains 1–16 entries in navigation order and at most 512 widget appearances in total. The first page is
always selected on launch. A page has optional `id`, optional `name`, and optional `layout`. `id` is metadata and is
not required for navigation. `name` is 1–128 Unicode code points; when omitted, UI derives `Page N` without modifying
the file. `{}` is a valid blank page. Each page has at most 32 widget appearances. Adaptive layout and touch behavior
are normative in `Specs/UI/UI_Dashboard.md`.

## Cold load and recovery

RedXe MUST validate settings before plugin-provider or Direct3D initialization.

- On Release, when `RedXe.settings.json` is absent and the legacy `RedXe-1.0.settings.json` exists, RedXe MUST move
  that file unchanged to the new name before validation. It MUST NOT perform a schema conversion. A present new-name
  file always wins and leaves the legacy path untouched.
- A missing default file causes atomic installation and loading of the selected v4 template.
- An invalid or incompatible default is preserved byte-for-byte beside it, then atomically replaced with a fresh
  template. Its backup name is `<stem>.invalid-YYYY-MM-DD_HH-MM-SSZ.json`. The user is told what happened and where
  the backup was written.
- A missing, unreadable, or invalid command-line file is never modified. RedXe reports the problem and runs with the
  deployed default configuration in memory.
- If a deployed default cannot be read or validated, startup fails rather than inventing settings.
- Template/schema installation and recovery use same-directory temporary files and write-through atomic rename.

## Live reload and diagnostics

- Normal execution watches the selected default or external file using event-blocked directory notification. Polling
  and periodic reload timers are forbidden.
- The watcher worker only coalesces and posts `SettingsWatcher::kSettingsChangedMessage`; parsing and runtime mutation
  remain on the UI thread.
- The UI thread compares volume, file identity, last-write time, and size before parsing. Applied and rejected stamps
  are deduplicated; every distinct later change is reconsidered.
- A valid candidate is applied transactionally. Failure preserves or restores the previous settings and dashboard.
- Invalid, unreadable, or missing live input remains untouched and leaves the exact last-valid in-memory state active.
- Diagnostics SHOULD include every reliable line, column, and JSON path in clear user language.
- At most one modal settings-error dialog may exist. Monitoring continues while visible. A later invalid save refreshes
  it with the newest bounded error list; dialogs never stack. If all errors do not fit, an ellipsis states that more
  were omitted. A valid save applies and closes the dialog automatically.
- Dismissal suppresses only that rejected stamp. A distinct later invalid save may display again.
- Shutdown signals and joins the watcher before destroying its target window.

## Resource requirements and bounds

- Pages: 16; widgets: 32 per page and 512 per document; declarations: 128; unique referenced plugins: 64.
- Layout: 127 areas per page and 8 container levels; declaration/page names: 128 Unicode code points.
- Live reload holds at most the authoritative document and one candidate. Full documents and schemas use bounded heap
  storage, never large UI-thread stack values.
- Parsing, discovery, merge, validation, and layout compilation are cold change-path work. The render path performs no
  settings I/O, parsing, schema work, or layout allocation.
- The watcher adds one sleeping thread and notification handle and causes zero periodic wake-ups while unchanged.
- `AppSettings` MUST remain at most 64 KiB. The parser MUST run on a 128 KiB reserved stack with heap-backed input and
  output.

## Required validation

- Templates and canonical schema agree with v4 and all syntax, count, size, and depth limits.
- Tests reject malformed syntax/version, duplicate and exact-version unknown members, unresolved references, invalid
  merge results, and plugin settings failures.
- Tests cover merge rules, plugin replacement, minor compatibility, and compatible unknown-field preservation.
- Tests verify exact invalid-default backup bytes/name, fresh installation, external fallback without mutation,
  one-time legacy Release filename migration, stamps, watching, last-valid preservation, modal refresh/close
  behavior, and rejected-stamp deduplication.
- Hidden host tests prove first-page startup, blank pages, inactive-page resource absence, transactional apply, WARP
  rendering, and current-plus-adjacent-only swipe staging.
- Debug and Release x64 tests, Release ARM64 compilation, formatting, skill validation, and `git diff --check` pass.

## Implementation anchors

- Parsing, paths, recovery, diagnostics, and stamps: `RedXe/Settings.*`
- Event-blocked watching: `RedXe/SettingsWatcher.*`
- UI-thread apply and error-dialog state: `RedXe/Application.*`
- Contracts and runtime creation: `RedXe/PluginManager.*`, `Common/PlugInterfaces/Factory.*`
- Schema/templates: `Specs/Settings.schema.json`, `Settings/`
- Tests: `Tests/SettingsTests/`, `Tests/HostPluginTests/`, `Tests/PluginContractTests/`
