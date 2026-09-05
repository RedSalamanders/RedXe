# RedXe settings contract

Status: current normative product contract
Last reviewed: 2026-09-05
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

Weather treats a non-empty location as authoritative regardless of `locationMode`. An empty location allows a
disposable Windows helper; its city/country (or coordinates when reverse lookup fails) is merged into that widget's
settings for later runs. `IRedXeSettingsQueue` copies worker results and dispatches the existing transactional persist
handler on the UI thread. Plugins retain collect fallback and never write the file. JSON shape, shipped defaults and
units are unchanged; see `../Plugins/Plugins_Weather.md`.

Interactive RedXe appends host and plugin diagnostics as JSON Lines beside the settings root. When the settings
directory is named `Settings`, logs live in its parent `Logs` folder; otherwise they live in `<settingsDir>\Logs`.
Default interactive files are UTC-dated `%LocalAppData%\RedXe\Logs\RedXe-debug-YYYY-MM-DD.jsonl` (Debug) and
`%LocalAppData%\RedXe\Logs\RedXe-YYYY-MM-DD.jsonl` (Release). `--settings` follows the same sibling rule from the
portable file's directory. The host writer is event-blocked, uses a bounded 32×1024 ring, opens a new file when the
UTC day changes, and deletes dated JSONL files whose UTC date is at least `logRetentionDays` old. Omitted
`logRetentionDays` is 15; valid values are integers 1 through 365. Legacy undated `RedXe.jsonl` /
`RedXe-debug.jsonl` and `*.jsonl.1` rotate files in that folder are also deleted. `--self-test` MUST NOT open this
directory.

The build output MUST contain both templates and `RedXe.settings.schema.json`. Repository sources are `Settings/` and
`Specs/Settings.schema.json`. The hidden `--self-test` path MUST use the deployed template and MUST NOT touch or watch
the user's settings directory. It MUST NOT create or write the diagnostic `Logs` directory.

Both shipped templates MUST contain three pages: a representative low-resource startup composition, a denser gallery,
and a `System` page that places one instance of every Process Viewer family widget. Every settings-visible plugin
compiled into the product MUST have at least one effective widget
instance in each template; a declaration that is never placed does not count. The host-owned compile-time bundled
plugin catalog is the source of truth for this coverage. Automated template validation MUST iterate that catalog and
fail when either template or the canonical schema omits an entry. Adding or removing a bundled plugin therefore
requires updating the catalog, schema support, and both templates in the same change. Catalog plugin IDs and type IDs
stay unique; module file names MAY repeat so several settings-visible widgets can share `ProcessViewer.dll`.

## Version 4 document

RedXe accepts standard JSON semantics plus comments and trailing commas. It MUST reject duplicate object members and
other JSON5 extensions. The source MUST NOT exceed 1 MiB.

The root members are:

| Member | Required | Contract |
| --- | --- | --- |
| `$schema` | No | When present, `RedXe.settings.schema.json`. |
| `version` | Yes | Object with required `major` and optional `minor`; omitted minor is zero. |
| `wrapPages` | No | Omitted or false stops at page ends; true wraps. |
| `logRetentionDays` | No | Integer 1–365. Omitted is 15. How many UTC days of dated JSONL files to keep. |
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

Process Viewer settings are `{ "topN": <integer> }`. `topN` is required after default resolution, ranges from 1
through 32, and defaults to 10. Unknown members, non-integers, and values outside the range reject the complete
candidate.

Network Meter and GPU Processes settings are the same closed `{ "topN": <integer> }` object with range 1 through 16
and default 8. System Pulse, CPU Meter, Memory Meter, Storage Meter, GPU Meter, Power Meter, and Thermal Meter publish
closed empty objects `{}`. Unknown members and out-of-range `topN` values reject the complete candidate.

Studio Clock settings are the closed object `showSecondProgress`, `externalDotsAlwaysOn`, `showSeconds`,
`secondsColor`, `showDate`, `dateFormat`, `timeColor`, and `backgroundColor`. Defaults are respectively `true`, `true`,
`true`, `#FF1616`, `false`, `dd-mm-yyyy`, `#FF1616`, and `#111111`. Colors are exact `#RRGGBB`; date format is one of
`dd-mm-yyyy`, `mm-dd-yyyy`, and `yyyy-mm-dd`. The host merges omitted members from these defaults before static
validation and provider creation. Unknown members, malformed booleans/colors, or another date format reject the
complete candidate.

Desk Clock settings are the closed object `flipDurationMilliseconds`, `backgroundColor`, `cardColor`, `digitColor`,
and `dateColor`. Defaults are respectively `420`, `#000000`, `#FF3B43`, `#FFFFFF`, and `#D8D8D8`. Duration is an
integer from 250 through 800 and colors are exact `#RRGGBB` strings with case-insensitive hexadecimal digits. The host
merges omitted members from these defaults before static validation and provider creation. Unknown members,
non-integer or out-of-range duration, and malformed colors reject the complete candidate.

Launcher settings are the closed object `shortcuts`: an array of 0 through 8 closed items. Each item has required
`target` (UTF-8 string, 1 through 512 bytes) and optional `iconPng` (UTF-8 string, 0 through 260 bytes). `target` is an
absolute Win32 path or a URI with an alphabetic scheme of at least two characters followed by `:`. Defaults are
`{"shortcuts":[]}`. Unknown members, non-arrays, extra item members, empty or relative targets, schemeless host names,
overlong strings, duplicate targets, and more than eight items reject the complete candidate. An empty authored list is
valid: on first population, the widget imports up to eight taskbar pins that fit the 4096-byte settings cap, shows
them and queues them for persistence as editable shortcuts. Each placement saves its own settings/override; shared
declarations remain unchanged. Once populated, configured shortcuts are authoritative and are not reimported from
the taskbar. Empty/unavailable pin folders cause no save. Shipped defaults remain empty so first use can import.

### Plugin persist

Plugins MUST NOT write the settings file. The host owns merge, schema validation, and the optional document write.

Every widget MUST implement `IRedXeWidget::CollectPersistentSettings`. The host calls it on the UI thread after
`SetVisible(FALSE)` and before `Detach`. `S_FALSE` means nothing to save. `S_OK` supplies a complete instance settings
object or a mergeable subset. The widget MUST NOT persist from inside collect; the host writes.

At any time on the UI thread, a widget MAY call `IRedXeHost::PersistWidgetSettings` with all of its instance settings
or a part of them. There is no persist-scope enum. The host always merges supplied members into the stored instance
object, then validates the complete result (4096-byte compact cap and the plugin schema). Unknown members reject the
complete persist. A successful interactive persist MAY write the user document. `--self-test` MUST keep the merge in
memory and MUST NOT write or watch `%LocalAppData%`.

Interactive persistence MUST roll back both typed private settings and the retained source document if validation
or atomic file replacement fails. A later partial save MUST NOT resurrect a rejected change. The transaction saves
only the affected private object and source text, rather than copying the entire typed dashboard. Once replacement
commits, failure to query the file stamp MUST NOT report a failed save; clear deduplication state and allow reload.

Persisted documents MUST use a compact, readable layout with two-space indentation and a final LF newline. Keep
empty objects and arrays inline. Keep small objects inline when they fit a soft 120-byte line width; a single scalar
property stays together even when its indivisible string or path exceeds that width. Keep the root object, nonempty
`declare`, `pages`, `layout`, `areas`, and `shortcuts` sections multiline, and put each nonempty array item on its own
line. The internal plugin/factory settings representations remain compact. Reject a formatted result exceeding
1 MiB and roll back the typed and source state. Preserve semantic values, member order and compatible newer-minor
fields. Formatting changes only whitespace outside JSON tokens and runs only when saving settings.
If an interactive save arrives while an older patch for that instance is queued, apply the older patch first, then
the interactive patch. A failed older commit is logged and must not prevent a valid newer save. Neither may run
under the queue lock. A detached UI delivery batch must not drain newer worker submissions ahead of itself.

## Pages and layout

`pages` contains 1–16 entries in navigation order and at most 512 widget appearances in total. The first page is
always selected on launch. The active page is runtime state and MUST NOT be written to the document. A live reload of
a valid document MUST keep the page that was current when that page still exists: match the previous page `id` in the
new document, otherwise keep the previous index when it is still in range, otherwise select the first page. A page has
optional `id`, optional `name`, and optional `layout`. `id` is metadata and is not required for swipe or edge
navigation, but live reload uses it as the page's identity when it is present. `name` is 1–128 Unicode code points;
when omitted, UI derives `Page N` without modifying the file. `{}` is a valid blank page. Each page has at most 32
widget appearances. Adaptive layout and touch behavior are normative in `Specs/UI/UI_Dashboard.md`.

## Cold load and recovery

RedXe MUST validate settings before plugin-provider or Direct3D initialization.

- On Release, when `RedXe.settings.json` is absent and the legacy `RedXe-1.0.settings.json` exists, RedXe MUST move
  that file unchanged to the new name before validation. It MUST NOT perform a schema conversion. A present new-name
  file always wins and leaves the legacy path untouched.
- A missing default file causes atomic installation and loading of the selected v4 template. Startup MUST continue
  after that install. A catalogued plugin whose DLL cannot be mapped becomes a placeholder tile; it MUST NOT abort
  launch or roll back the installed file.
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
- A valid candidate is applied transactionally after the host reselects the page that was current, when that page
  still exists in the candidate. Failure preserves or restores the previous settings and dashboard.
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
  merge results, plugin settings failures, Process Viewer `topN` values outside 1 through 32, Network Meter and GPU
  Processes `topN` values outside 1 through 16, and malformed Studio
  Clock booleans including `externalDotsAlwaysOn`, colors, date formats, and unknown members. They also reject Desk
  Clock duration and color failures and verify its complete merged defaults and valid partial overrides. They also
  reject Launcher shortcut failures (schemeless host names, unknown members, duplicate targets) and verify empty-list
  defaults plus a valid persist merge of a `shortcuts` array.
- Tests cover merge rules, plugin replacement, minor compatibility, and compatible unknown-field preservation.
- Tests prove a partial widget persist merge keeps unspecified members and rejects unknown plugin members.
- Tests prove compact/idempotent formatting, inline small objects and long single-path records, multiline sections
  and arrays, fewer lines than fully expanded output, escaped/Unicode paths, named/inline/override widget round
  trips, compatible unknown-field retention, and transactional rejection of oversized formatted output.
- An isolated file test MUST block replacement with an open handle, verify exact typed/source/disk rollback, then
  release the handle and verify a different partial save commits without including the rejected patch.
- Plugin contract tests prove `CollectPersistentSettings` returns `S_FALSE` when nothing to save and `E_POINTER` for a
  null `writtenBytes`. Host tests prove persist without a handler is `E_UNEXPECTED` and that a handler receives a
  partial object.
- Tests verify exact invalid-default backup bytes/name, fresh installation, external fallback without mutation,
  one-time legacy Release filename migration, stamps, watching, last-valid preservation, modal refresh/close
  behavior, and rejected-stamp deduplication.
- Hidden host tests prove first-page startup, blank pages, inactive-page resource absence, transactional apply, WARP
  rendering, current-plus-adjacent-only swipe staging, that a live reload of an unchanged page list keeps the page
  that was current, and that a catalogued plugin whose DLL cannot be mapped becomes a placeholder without aborting
  startup. Host tests prove JSONL `Log` reject/write/flush behavior, UTC-dated file names, and retention deletion of
  expired and legacy log files. Settings tests prove the default logs path is the
  `Logs` sibling of `Settings`, omitted `logRetentionDays` is 15, and values outside 1–365 are rejected.
- Parser tests prove that `PreserveActiveDashboardPage` follows an authored page id across a reorder, keeps a
  generated `page.N` index when that page remains, and falls back to the first page when the current page is gone.
- Debug and Release x64 tests, Release ARM64 compilation, formatting, skill validation, and `git diff --check` pass.

## Implementation anchors

- Parsing, paths, recovery, diagnostics, stamps, and persist merge: `RedXe/Settings.*`
- Event-blocked watching: `RedXe/SettingsWatcher.*`
- UI-thread apply, persist thunk, and error-dialog state: `RedXe/Application.*`
- Collect-on-exit: `RedXe/DashboardHost.cpp`
- Contracts and runtime creation: `RedXe/PluginManager.*`, `Common/PlugInterfaces/Factory.*`
- Host persist thunk and JSONL log writer: `RedXe/PluginHost.*`
- Schema/templates: `Specs/Settings.schema.json`, `Settings/`
- Tests: `Tests/SettingsTests/`, `Tests/HostPluginTests/`, `Tests/PluginContractTests/`
