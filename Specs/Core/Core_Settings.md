# RedXe settings contract

Status: current normative product contract
Last reviewed: 2026-09-19
Owner: `SettingsStore`, `SettingsWatcher`, and UI-thread application orchestration

## Scope

This specification owns settings selection, the version 5 host document, plugin-settings validation, cold recovery,
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
instance in each template; a declaration that is never placed does not count. The one exception is the catalog's
opt-in list (`kRedXeOptInBundledWidgetIds`, today only the GdiOrbit native-window example): those plugins MUST stay
schema-accepted and documented but MUST NOT be declared or placed by either template, because a native child HWND
forces composed presentation for the whole window. The host-owned compile-time bundled
plugin catalog is the source of truth for this coverage. Automated template validation MUST iterate that catalog and
fail when either template or the canonical schema omits an entry, or when a template places an opt-in plugin. Adding or removing a bundled plugin therefore
requires updating the catalog, schema support, and both templates in the same change. Catalog plugin IDs and type IDs
stay unique; module file names MAY repeat so several settings-visible widgets can share `ProcessViewer.dll`.

## Version 5 document

RedXe accepts standard JSON semantics plus comments and trailing commas. It MUST reject duplicate object members and
other JSON5 extensions. The source MUST NOT exceed 1 MiB.

The root members are:

| Member | Required | Contract |
| --- | --- | --- |
| `$schema` | No | When present, `RedXe.settings.schema.json`. |
| `version` | Yes | Object with required `major` and optional `minor`; omitted minor is zero. |
| `wrapPages` | No | Omitted or false stops at page ends; true wraps. |
| `logRetentionDays` | No | Integer 1–365. Omitted is 15. How many UTC days of dated JSONL files to keep. |
| `backgroundColor` | No | Exact `#RRGGBB`. Omitted is `#000000`. The dashboard background: the host canvas clear color and the background every widget paints unless its widget object overrides it (see below). |
| `declare` | No | Reusable widget definitions keyed by authored names. |
| `services` | No | Headless service plugins the host starts at launch, keyed by authored names (minor 1). |
| `dock` | No | Screen-edge dock: RedXe as a bar on one edge of one monitor (minor 2). Omitted or `edge: none` keeps the standard window. |
| `pages` | Yes | One through sixteen ordered pages. |

Version 5 begins at major 5, minor 0. Minor 1 adds the optional additive `services` member and minor 2 the optional
additive `dock` member; this build reads and writes minor 2 and accepts minor 0 and 1 documents unchanged. This
build reads major 5 only. Missing, malformed, or different-major versions (including 4) are errors
(`ERROR_INVALID_DATA`) with a diagnostic on `$.version.major`. RedXe MUST NOT convert a version 4 layout tree. A newer
RedXe accepts older minors of major 5 and supplies documented defaults. An older RedXe accepts a newer minor of the
same major, validates the shape it understands, and silently ignores additive unknown host fields. Exact-version
unknown host fields are errors. A future editor saving a compatible newer-minor file MUST preserve unknown fields.
Version 3 is not migrated. Version 4 is an incompatible major: cold recovery of a default file backs up the bytes and
installs the version 5 template; a `--settings` portable version 4 file is left unchanged and the process runs the
in-memory deployed version 5 default.

User documents MUST NOT contain `layout`, `areas`, `arrangeAlong`, `sizeRatio`, nested `settings`, or `override`.
Those members reject the complete candidate with a diagnostic on the authored JSON path.

### Declarations and widgets

`declare` is optional and has at most 128 members. A declaration name is 1–128 Unicode code points, may contain spaces,
and matches references by the exact case-sensitive sequence without normalization. Every declaration is a flattened
plugin object: required `plugin` plus that plugin's settings keys on the same object. Nested `settings` is not
accepted. There is no user-visible plugin registry, enable flag, type ID, plugin-private object, or separate
instance-private object.

A widget value is exactly one of:

1. A declaration-name string resolved through `declare`.
2. A catalogued settings-visible plugin ID string (`builtin.launcher`) when that string is not a declare name. Human
   aliases such as `"Launcher"` without a matching `declare` entry are unresolved errors.
3. A flattened plugin object `{ "plugin": "...", ...pluginKeys }` with no `settings` member. Other members become the
   settings object and then validate against that plugin's closed schema.
4. A flattened use-object `{ "use": "<name>", ...pluginKeys, optional "plugin" }`. Extra keys besides `use` and
   optional `plugin` merge onto the declaration (absent inherit, objects merge, scalars replace, arrays replace, null
   removes). `plugin` on a use-object swaps the plugin. The result MUST contain one valid plugin and settings object.

Reserved host keys on a widget object are `plugin`, `use`, and `backgroundColor`. `weight`, `widget`, `rows`,
`columns`, and `along` are reserved on split items as specified below. Flattened keys become the settings object,
then existing plugin validators run (including launcher `iconSize`). After parse, typed `privateConfiguration`
remains the compact plugin object (for example launcher `{shortcuts, iconSize}`) even when the file stored flattened
keys.

`backgroundColor` on a widget object (a declare entry, a flattened plugin object, or a use-object) is accepted for
every catalogued plugin. It is an exact `#RRGGBB` string; any other value rejects the complete candidate. It
participates in declare/use merging like any flattened key (a use-object may replace it or remove it with `null`),
and is then lifted onto the typed instance before plugin defaults merge and plugin validators run. It MUST NOT enter
`privateConfiguration`, a plugin settings contract, or a factory configuration object: the host resolves each
instance's effective background (its own override, else the document `backgroundColor`) and supplies it to the
plugin through `RedXeFactoryOptions::backgroundColor`. Widget persist (`ApplyFlattenedSettings`) MUST preserve the
widget object's `backgroundColor` beside `plugin` / `use`, and a plugin persist that names `backgroundColor` is an
unknown member and MUST be rejected. A change to the document `backgroundColor` is a runtime change that rebuilds
the active page. Both shipped templates MUST author the document `backgroundColor` explicitly.

Each appearance creates an independent runtime widget instance.

### Dock

`dock` is an optional closed object (minor 2) that places RedXe as a bar on one edge of one monitor instead of the
standard XENEON window; `Specs/UI/UI_XeneonDisplayWindowing.md` "Dock window kind" owns the window, placement, and
autohide behavior. Every member is optional and the parser merges the defaults below, so an omitted object, `{}`, and
`{ "edge": "none" }` are the same typed value. Unknown members, wrong types, out-of-range values, `all`, and an
unknown edge or mode reject the complete candidate with a diagnostic on `$.dock.<member>`.

| Member | Type and range | Default | Contract |
| --- | --- | --- | --- |
| `edge` | `none`, `top`, `bottom`, `left`, `right` | `none` | Edge of the selected monitor. `none` disables the dock and leaves every other member validated and inert. |
| `monitor` | `primary`, `xeneon`, `<n>` (1-based `EnumDisplayMonitors` order), `name:<substring>` | `primary` | Validated with the shared monitor-selector grammar (`Common/Actions/ActionTargets.h`, `all` rejected); resolved at window creation, where an absent display falls back to the primary. |
| `thickness` | Integer DIPs, 32–1080 | `180` | Cross-axis size, scaled by the monitor DPI and clamped to half of the monitor at runtime. |
| `mode` | `fixed`, `autohide` | `fixed` | `fixed` keeps the whole bar on screen; `autohide` collapses it to the peek strip. |
| `reserveWorkArea` | Boolean | `true` | `fixed` only: register the bar with the shell so maximized windows stop at it. Ignored in `autohide`. |
| `peek` | Integer physical pixels, 1–64 | `4` | `autohide` only: pixels that stay visible while hidden. |
| `revealDelayMilliseconds` | Integer, 0–2000 | `150` | `autohide` only: pointer dwell on the strip before the bar reveals; 0 reveals on the first mouse move. |
| `hideDelayMilliseconds` | Integer, 0–10000 | `800` | `autohide` only: delay after the last hold clears before the bar collapses. |

The `--dock <edge>[@<monitor>]`, `--dock-mode`, `--dock-thickness`, `--dock-reserve`, and `--dock-peek` switches
override the same-named members for one process (`RedXe/DockOptions.h`), including across live reloads; the
delays are settings-only. A live reload applies changed members in place, except that switching `edge` between
`none` and an edge takes effect at the next launch (one Warning log record). `dock` is a host member: it never
enters a plugin contract, a factory envelope, or a widget persist. The one host-driven write is `dock.thickness`
after the bar's inner edge is dragged (`PatchDockThickness`): it replaces or adds that member, creates the `dock`
object when absent, raises `version.minor` to 2 when lower, and goes through the same formatting and atomic
replacement as a widget persist; it MUST NOT touch any other member. Both shipped templates author minor 2 and stay at
`edge: none`, carrying a commented-out `dock` example.

### Services

`services` is optional and has at most 8 members. A member name follows the declaration-name rules. Every value is a
flattened plugin object naming a catalogued **service** plugin (`RedXe/BundledPlugins.h` `kRedXeBundledServices`,
today `builtin.logicon` and `builtin.zoom`) plus that plugin's settings keys on the same object; `use`, nested `settings`, and the
reserved widget keys are rejected. A widget plugin ID, an unknown plugin ID, or the same service plugin configured
twice rejects the complete candidate. The host merges the plugin's published defaults under the authored keys, then
validates the effective object with the plugin's shared model (`Plugins/Logicon/LogiconSettings.cpp` and
`Plugins/Actions/Zoom/ZoomSettings.cpp`, compiled into the host); a model rejection is a document error on the
authored path. Every `action` member the model accepts (Logicon keys, dialpad buttons and turns) is additionally
checked against the action-name grammar, the default and registered namespaces, and the default verbs
(`RedXe/HostActionCatalog.cpp` `IsKnownActionName`); an unsatisfied target is not a document error
(`Specs/Plugins/Plugins_Actions.md`). The effective compact object is
stored per service and reaches the plugin as the `instance` member of the ordinary factory envelope.

Services are not widgets: they are never placed, never counted as referenced widget plugins, own no instance ID, and
persist nothing. A service settings change on a live reload re-applies only that service; adding or removing a member
starts or stops it without rebuilding the dashboard. `Specs/Plugins/Plugins_API.md` owns the service lifetime, `Specs/Plugins/Plugins_Logicon.md` the Logicon members,
and `Specs/Plugins/Plugins_Zoom.md` the Zoom members.

Every authored settings object, effective settings object, plugin schema, and plugin defaults object MUST have a
compact representation no larger than 4096 bytes.

### Plugin settings contracts

Every settings-visible plugin MUST export its immutable static settings contract before provider or widget creation.
The contract contains the stable plugin ID, an independently versioned schema contract, a bounded Draft 2020-12 JSON
Schema for its settings object, and bounded defaults. Defaults MUST validate against the schema. Discovery MUST create
no provider, widget, HWND, device resource, timer, or worker.

RedXe validates host structure, resolves declarations and flattened use-objects, discovers every unique referenced plugin at most
once, and validates every effective widget on every page. Inactive pages create no runtime widget resources. Contract
or settings failure rejects the complete candidate. ABI details are normative in `Specs/Plugins/Plugins_API.md`.

Process Viewer settings are `{ "topN": <integer> }`. `topN` is required after default resolution, ranges from 1
through 32, and defaults to 10. Unknown members, non-integers, and values outside the range reject the complete
candidate.

Network Meter and GPU Processes settings are the same closed `{ "topN": <integer> }` object with range 1 through 16
and default 8. System Pulse, CPU Meter, Memory Meter, Storage Meter, GPU Meter, Power Meter, and Thermal Meter publish
closed empty objects `{}`. Unknown members and out-of-range `topN` values reject the complete candidate.

Studio Clock settings are the closed object `showSecondProgress`, `externalDotsAlwaysOn`, `showSeconds`,
`secondsColor`, `showDate`, `dateFormat`, and `timeColor`. Defaults are respectively `true`, `true`, `true`,
`#FF1616`, `false`, `dd-mm-yyyy`, and `#FF1616`. Colors are exact `#RRGGBB`; date format is one of `dd-mm-yyyy`,
`mm-dd-yyyy`, and `yyyy-mm-dd`. The host merges omitted members from these defaults before static validation and
provider creation. Unknown members (including `backgroundColor`, which is host-owned), malformed booleans/colors, or
another date format reject the complete candidate.

Desk Clock settings are the closed object `flipDurationMilliseconds`, `cardColor`, `digitColor`, and `dateColor`.
Defaults are respectively `420`, `#FF3B43`, `#FFFFFF`, and `#D8D8D8`. Duration is an integer from 250 through 800 and
colors are exact `#RRGGBB` strings with case-insensitive hexadecimal digits. The host merges omitted members from
these defaults before static validation and provider creation. Unknown members (including `backgroundColor`),
non-integer or out-of-range duration, and malformed colors reject the complete candidate.

Matrix Rain settings are the closed object `seed`, `glyphHeightDips`, `densityPercent`, `speedPercent`,
`trailLengthGlyphs`, `mutationPerSecond`, `headColor`, `trailColor`, and `glowPercent`; the rain falls over the
host-resolved dashboard background. `backgroundColor` is not a Matrix Rain member.

Launcher settings are the closed object `shortcuts` plus optional `iconSize`. `shortcuts` is an array of 0 through 32
closed binding items (`Specs/Plugins/Plugins_Actions.md`): optional `action` (an action name; omitted means
`system.launch`), `target` (UTF-8 string, at most 512 bytes; required and non-empty for `system.launch`), and
optional `icon` (a Segoe Fluent Icons glyph name or `png:<absolute path>`, at most 260 bytes; required for any other
action). `iconSize` is `"small"`, `"medium"`, `"large"`, `"huge"`, or `"automatic"`; omitted values merge to `"huge"`.
Defaults are `{"shortcuts":[],"iconSize":"huge"}`. Unknown members, non-arrays, extra item members (including the
former `iconPng`), an unknown `action`, an empty launch target, a non-launch item without `icon`, overlong strings,
duplicate shortcuts, unknown `iconSize` values, and more than 32 items reject the complete candidate. A launch target
that is not an absolute Win32 path or a URI is accepted; the widget draws it as a warning tile. An empty authored list is
valid: on first population, the widget imports up to 32 taskbar pins that fit the 4096-byte settings cap, shows
them and queues them for persistence as editable shortcuts. Each placement saves its own flattened keys; shared
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
complete persist. A successful interactive persist MAY write the user document. Persist MUST patch flattened keys in place on the
authored `widgets` / `columns` / `rows` sugar. It MUST NOT rewrite a page into `layout`. A bare string leaf MAY become
an inline `{ "use", ...keys }` object when the string was a declare name, or `{ "plugin", ...keys }` otherwise, for
that instance only. `--self-test` MUST keep the merge in
memory and MUST NOT write or watch `%LocalAppData%`.

Interactive persistence MUST roll back both typed private settings and the retained source document if validation
or atomic file replacement fails. A later partial save MUST NOT resurrect a rejected change. The transaction saves
only the affected private object and source text, rather than copying the entire typed dashboard. Once replacement
commits, failure to query the file stamp MUST NOT report a failed save; clear deduplication state and allow reload.

Persisted documents MUST use a compact, readable layout with two-space indentation and a final LF newline. Keep
empty objects and arrays inline. Keep small objects inline when they fit a soft 120-byte line width; a single scalar
property stays together even when its indivisible string or path exceeds that width. Keep the root object, nonempty
`declare`, `pages`, `widgets`, `columns`, `rows`, and `shortcuts` sections multiline, and put each nonempty array item on its own
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
optional `id`, optional `name`, and exactly one of: no placement members (blank), `widgets`, `columns`, or `rows`.
Mixing those placement members, or mixing them with `along` except `along` on `widgets`, rejects the page. `id` is
metadata and is not required for swipe or edge navigation, but live reload uses it as the page's identity when it is
present. `name` is 1–128 Unicode code points; when omitted, UI derives `Page N` without modifying the file. `{}` is a
valid blank page. Each page has at most 32 widget appearances.

The parser compiles human syntax into the in-memory adaptive tree owned by `Specs/UI/UI_Dashboard.md`. `weight`
(omitted 1, range 1–1000) becomes `sizeRatio`. `columns` compiles to a long-side split. `rows` compiles to a
short-side split. `widgets` is an equal-share list; omitted `along` is `long-side`. Nested `rows` inside `columns`
(and the reverse) compile to nested containers. After that compile, existing area, depth, and widget caps apply.
`DashboardHost` never sees `widgets` / `columns` / `rows` as a dashboard primitive. Adaptive layout and touch
behavior are normative in `Specs/UI/UI_Dashboard.md`.

## Cold load and recovery

RedXe MUST validate settings before plugin-provider or Direct3D initialization.

- On Release, when `RedXe.settings.json` is absent and the legacy `RedXe-1.0.settings.json` exists, RedXe MUST move
  that file unchanged to the new name before validation. It MUST NOT perform a schema conversion. A present new-name
  file always wins and leaves the legacy path untouched.
- A missing default file causes atomic installation and loading of the selected version 5 template. Startup MUST continue
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
- A successful live load MUST apply the candidate in memory only. It MUST NOT write the watched file, format it,
  persist a widget merge, or collect-on-exit onto that path. The editor's bytes stay until an explicit widget persist
  or other user-driven save. `--self-test` MUST NOT write `%LocalAppData%` and MUST NOT write the deployed template.
- Invalid, unreadable, or missing live input remains untouched and leaves the exact last-valid in-memory state active.
  An invalid live load MUST NOT rewrite the invalid file and MUST NOT write the last-good document over it. Monitoring
  continues until a later distinct save can be loaded.
- Diagnostics MUST name the JSON path and the specific problem in clear user language. When the byte location is
  reliable (JSON syntax errors, or a path the locator can resolve), they MUST also include line and column. The dialog
  MUST NOT report a generic version-5 schema failure at `path $` when a more specific member is known.
- At most one modal settings-error dialog may exist. Monitoring continues while visible. A later invalid save refreshes
  it with the newest bounded error list; dialogs never stack. If all errors do not fit, an ellipsis states that more
  were omitted. A valid save applies and closes the dialog automatically.
- Dismissal suppresses only that rejected stamp. A distinct later invalid save may display again.
- Shutdown signals and joins the watcher before destroying its target window.

## Resource requirements and bounds

- Pages: 16; widgets: 32 per page and 512 per document; declarations: 128; unique referenced plugins: 64;
  services: 8.
- Layout: 127 areas per page and 8 container levels; declaration/page names: 128 Unicode code points.
- Live reload holds at most the authoritative document and one candidate. Full documents and schemas use bounded heap
  storage, never large UI-thread stack values.
- Parsing, discovery, merge, validation, and layout compilation are cold change-path work. The render path performs no
  settings I/O, parsing, schema work, or layout allocation.
- The watcher adds one sleeping thread and notification handle and causes zero periodic wake-ups while unchanged.
- `AppSettings` MUST remain at most 64 KiB. The parser MUST run on a 128 KiB reserved stack with heap-backed input and
  output.

## Required validation

- Templates and canonical schema agree with version 5 and all syntax, count, size, and depth limits. The Release
  template System last column compiles Gpu/Thermal/Power short-side ratios 2, 3, and 1.
- Tests reject version 4 documents, `layout` / `areas` / `arrangeAlong` / `sizeRatio`, nested `settings`, and
  `override`. They reject malformed syntax/version, duplicate and exact-version unknown members, unresolved
  references, invalid merge results, plugin settings failures, Process Viewer `topN` values outside 1 through 32,
  Network Meter and GPU Processes `topN` values outside 1 through 16, and malformed Studio
  Clock booleans including `externalDotsAlwaysOn`, colors, date formats, and unknown members. They also reject Desk
  Clock duration and color failures and verify its complete merged defaults and valid partial overrides. They also
  reject Launcher shortcut failures (unknown members, `iconPng`, an unknown `action`, a non-launch item without
  `icon`, duplicate shortcuts, unknown `iconSize`), accept a schemeless launch target, and verify empty-list
  defaults plus a valid persist merge of a `shortcuts` array. Settings tests prove a live
  `TryLoadChanged` of a valid or invalid file does not mutate those bytes, and that persist writes remain opt-in.
  Persist of an instance on a `widgets` / `columns` / `rows` page MUST leave sibling human syntax intact and MUST NOT
  rewrite the page to `layout`.
- Tests cover merge rules, plugin replacement, minor compatibility, and compatible unknown-field preservation.
- Tests accept a `services` member with defaults merged and the plugin model applied, prove it adds no referenced
  widget plugin, and reject a widget plugin as a service, an unknown service plugin, a duplicated service plugin,
  plugin-model failures (`slot` 9, `brightness` 0, unknown members), an unknown or malformed `action` name, a
  non-object `services`, a string member, and `use`. Both shipped templates MUST carry minor 2 and configure every
  catalogued service.
- Tests accept a complete `dock` object, prove member-by-member default merging (an omitted object, `{}`, and a minor
  1 document equal `edge: none`), keep `edge: none` validating the other members, reject every malformed member
  (unknown edge or mode, `all`, `0` and `name:` selectors, thickness 31 and 1081, peek 0 and 65, delays past their
  maximum, a non-boolean reserve, an unknown member, a non-object `dock`) with the diagnostic on `$.dock.<member>`,
  and prove the `--dock*` grammar, its errors, and the merge precedence over the document.
- Tests prove a partial widget persist merge keeps unspecified members and rejects unknown plugin members.
- Tests prove compact/idempotent formatting, inline small objects and long single-path records, multiline sections
  and arrays, fewer lines than fully expanded output, escaped/Unicode paths, named/inline/use-object widget round
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
- Settings tests prove the document `backgroundColor` default and rejection of malformed values at the root and on
  widget objects, that a widget `backgroundColor` lifts onto the typed instance for closed-empty and settings-bearing
  plugins alike without entering `privateConfiguration`, that declare/use merging and `null` removal apply to it,
  that it survives a plugin persist and is rejected as a plugin persist member, that both templates author it, that
  the schema accepts it on every widget object variant and on no plugin settings definition, and that a document
  color change is a runtime change. Host tests prove `PluginManager` resolves each tile's color, keys the per-pass
  provider cache on it, and that the renderer clears with the document color and fills overridden tiles.
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
