# RedXe settings contract

Status: current normative product contract
Last reviewed: 2026-09-28
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

The document is UTF-8. Every read of the user document (load, live reload, and the source the widget persist and the
dock patches start from) MUST accept a leading UTF-8 byte order mark (BOM, `EF BB BF`), which Windows PowerShell 5.1
`Set-Content -Encoding UTF8` and editors saving "UTF-8 with signature" write, and MUST NOT treat it as an error or a
reason for cold recovery. The BOM counts toward the 1 MiB limit and toward the byte offsets and first-line columns of
diagnostics. Every host write of a document that starts with a BOM (a widget persist, a `dock.thickness` drag) MUST
keep it, once, at the start; RedXe never adds a BOM to a document that has none. A file holding only a BOM, blanks, or
comments is reported as empty. `v1.0.102` and earlier builds reject a BOM-prefixed document.

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
| `trayIcon` | No | Boolean (minor 3): the notification-area icon of an interactive run. Omitted is `true` in Release and `false` in Debug. |
| `pages` | Yes | One through sixteen ordered pages. |

Version 5 begins at major 5, minor 0. Minor 1 adds the optional additive `services` member, minor 2 the optional
additive `dock` member, and minor 3 the optional additive `trayIcon` member, the `secondary` monitor selector, and
`dock.animationMilliseconds`; this build reads and writes minor 3 and accepts minor 0 through 2 documents unchanged. A
new selector value is not an additive member: a build older than minor 3 rejects a document whose `dock.monitor` is
`secondary`. This build reads major 5 only. Missing, malformed, or different-major versions (including 4) are errors
(`ERROR_INVALID_DATA`) with a diagnostic on `$.version.major`. RedXe MUST NOT convert a version 4 layout tree. A newer
RedXe accepts older minors of major 5 and supplies documented defaults. An older RedXe accepts a newer minor of the same
major, validates the shape it understands, and silently ignores additive unknown host fields. Exact-version unknown host
fields are errors. A future editor saving a compatible newer-minor file MUST preserve unknown fields. Version 3 is not
migrated. Version 4 is an incompatible major: cold recovery of a default file backs up the bytes and installs the
version 5 template; a `--settings` portable version 4 file is left unchanged and the process runs the in-memory deployed
version 5 default.

A settings file written by an earlier release, back to the public `v1.0.102`, MUST load unchanged after an upgrade.
Plugin members are not versioned by `minor`, so a plugin settings model that retires a member MUST keep accepting it
with any value and ignore it, whatever the document's minor. RedXe MUST NOT migrate or rewrite the file for a retired
member. Likewise a `services` entry naming a plugin that is no longer a service (today `builtin.zoom`, with or without
the seven retired Zoom SDK members, `Specs/Plugins/Plugins_Zoom.md`) loads and is ignored, and the host logs one
Warning record `service-retired-settings-ignored` for it per load or live apply ("Services"). A binding to a verb a
published namespace no longer publishes (the removed `zoom.*` controls) stays in the document as an invalid binding
(`Specs/Plugins/Plugins_Actions.md`), not a document error, and so does a Logicon key, dialpad button, or turn bound
to `keys.down` or `mouse.down` (`Specs/Plugins/Plugins_Logicon.md`). Rolling back to an older build is not supported:
an older build validates plugin members as a closed set, so it may reject a file that uses a newer member (for example
Studio Clock `glowPercent`, which `v1.0.102` rejects). Its cold recovery then keeps the user's file as the
`.invalid-<timestamp>.json` backup described in "Cold load and recovery".

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
| `monitor` | `primary`, `secondary` (the first display in `EnumDisplayMonitors` order that is neither the primary nor the XENEON, and the XENEON only when it is the only display that is not the primary; minor 3), `xeneon`, `<n>` (1-based `EnumDisplayMonitors` order), `name:<substring>` | `primary` | Validated with the shared monitor-selector grammar (`Common/Actions/ActionTargets.h`, `all` rejected); resolved at window creation, where an absent display falls back to the primary. |
| `thickness` | Integer DIPs, 32–1080 | `180` | Cross-axis size, scaled by the monitor DPI and clamped to half of the monitor at runtime. |
| `mode` | `fixed`, `autohide` | `fixed` | `fixed` keeps the whole bar on screen; `autohide` collapses it to the peek strip, which it reserves in the monitor's work area so maximized windows stop just inside it, and reveals the full bar over the work area. |
| `reserveWorkArea` | Boolean | `true` | `fixed` only: reserve the whole bar in the monitor's work area so maximized windows stop at it; `false` reserves nothing and the bar overlays the work area. Ignored in `autohide`, which always reserves its peek strip. |
| `peek` | Integer physical pixels, 1–64 | `4` | `autohide` only: pixels that stay visible while hidden, clamped to the bar's thickness; the bar reserves exactly that strip in the work area. |
| `revealDelayMilliseconds` | Integer, 0–2000 | `150` | `autohide` only: pointer dwell on the strip before the bar reveals; 0 reveals on the first mouse move. |
| `hideDelayMilliseconds` | Integer, 0–10000 | `800` | `autohide` only: delay after the last hold clears before the bar collapses. |
| `animationMilliseconds` | Integer, 0–1000 | `200` | `autohide` only (minor 3): how long the bar slides out of its edge when it reveals and back when it hides; 0 reveals and hides in one step. |

The `--dock <edge>[@<monitor>]`, `--dock-mode`, `--dock-thickness`, `--dock-reserve`, and `--dock-peek` switches
override the same-named members for one process (`RedXe/DockOptions.h`), including across live reloads; the delays and
`animationMilliseconds` are settings-only. A valid live reload applies every changed member to the running window,
including `edge` between `none` and an edge, which switches the window kind without a restart
(`Specs/UI/UI_XeneonDisplayWindowing.md` "Switching the window kind"). `dock` is a host member: it never enters a plugin
contract, a factory envelope, or a widget persist. The one host-driven write to an existing document is `dock.thickness`
after the bar's inner edge is dragged (`PatchDockThickness`): it replaces or adds that member, creates the `dock` object
when absent, raises `version.minor` to 2 when lower, and uses the same stamp check and atomic replacement as a widget
persist ("Plugin persist"). A release at the thickness the document already has (a click on the edge) MUST NOT patch
or write the document and returns `S_FALSE`, unless an earlier deferred write is still held in memory, which it then
writes as an unchanged widget persist does. The source edit MUST preserve comments, spacing, and every unrelated
member, and MUST follow the document's layout: a new `dock` is `"dock": { "thickness": N }` on its own line right after
`version`, at that member's indentation and in the file's own line breaks (CRLF, LF, or CR in a file that uses only
CR), where the first-run dock goes; a comment that ends `version`'s line stays on that line. A missing `thickness`,
or a missing `version.minor`, follows the object's last member, on a new line at its indentation when that member
starts its own line and otherwise on the same line; in an empty object whose closing brace starts its own line, it
gets its own line one level deeper than the brace. A line comment ends at CR or LF, as the parser ends it. The
patched text MUST parse back to the running `dock` with the new thickness, and to the document it was made from with
only `dock.thickness` and a raised minor changed, before it is kept or written; otherwise the drag changes nothing in
the document. A raised source minor is also the typed minor, so a later comment-only reload still matches the running
settings. Both shipped templates author the current minor and stay at `edge: none`, carrying a commented-out `dock`
example; a missing default file installed on a machine without a XENEON adds the first-run dock and drops that
example ("Cold load and recovery").

### Notification-area icon

`trayIcon` is an optional boolean (minor 3). `true` shows the product icon in the Windows notification area while
RedXe runs interactively; `false` shows none. Omitted, it follows the build: `true` in Release, `false` in Debug
(`kRedXeDefaultTrayIcon`), including in documents of an older minor, so an existing Release file gains the icon.
Any other type rejects the complete candidate with a diagnostic on `$.trayIcon`. Both shipped templates author it
explicitly: `true` in the Release template, `false` in the Debug template. It is a host member that never enters a
plugin contract, and a change is a runtime change applied live without rebuilding the page. `--self-test` never
shows the icon. `Specs/UI/UI_XeneonDisplayWindowing.md` "Notification-area icon" owns the icon, its double-click (the
settings file in its default editor), and its menu.

### Services

`services` is optional and has at most 8 members. A member name follows the declaration-name rules. Every value is a
flattened plugin object naming a catalogued **service** plugin (`RedXe/BundledPlugins.h` `kRedXeBundledServices`,
today `builtin.logicon`) plus that plugin's settings keys on the same object; `use`, nested `settings`, and the
reserved widget keys are rejected. A widget plugin ID, an unknown plugin ID, or the same service plugin configured
twice rejects the complete candidate. The host merges the plugin's published defaults under the authored keys, then
validates the effective object with the plugin's shared model (`Plugins/Logicon/LogiconSettings.cpp`, compiled into
the host); a model rejection is a document error on the authored path. Every `action` member the
model accepts (Logicon keys, dialpad buttons and turns) is additionally checked against the action-name grammar, the
default and registered namespaces, and the default verbs
(`RedXe/HostActionCatalog.cpp` `IsKnownActionName`); an unsatisfied target is not a document error
(`Specs/Plugins/Plugins_Actions.md`). The effective compact object is
stored per service and reaches the plugin as the `instance` member of the ordinary factory envelope.

A member naming a **retired** service plugin (`kRedXeRetiredServices`, today `builtin.zoom`, which became a dedicated
action DLL that needs no entry) MUST still load so an older file keeps working. Its authored keys are validated as
written, never merged with defaults first (a merge drops `null` members), with the retired plugin's legacy model
(`Plugins/Actions/Zoom/ZoomSettings.cpp`, compiled into the host: only the seven retired Zoom SDK members, each with
any value, `null` included); any other key, whatever its value, or the same retired plugin configured twice, rejects
the complete candidate. A valid entry is recorded in the typed `AppSettings::retiredServices` and otherwise ignored:
it is never created or started, stores no configuration, and MUST NOT cause the file to be rewritten. Each load or
live apply logs one Warning record `service-retired-settings-ignored` from that plugin per such entry. Retired entries
count toward the 8 members.

Services are not widgets: they are never placed, never counted as referenced widget plugins, own no instance ID, and
persist nothing. A service settings change on a live reload re-applies only that service; adding or removing a member
starts or stops it without rebuilding the dashboard. `Specs/Plugins/Plugins_API.md` owns the service lifetime, `Specs/Plugins/Plugins_Logicon.md` the Logicon members,
and `Specs/Plugins/Plugins_Zoom.md` the retired Zoom entry.

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

Process Viewer settings are `{ "topN": <integer>, "hideIdle": <boolean> }`. `topN` is required after default
resolution, ranges from 1 through 32, and defaults to 10. `hideIdle` defaults to `true` and leaves PID 0 (the System
Idle Process) out of the ranking. Unknown members, non-integers, non-booleans, and values outside the range reject the
complete candidate.

Network Meter and GPU Processes settings are the same closed `{ "topN": <integer> }` object with range 1 through 16
and default 8. System Pulse, CPU Meter, Memory Meter, Storage Meter, GPU Meter, Power Meter, and Thermal Meter publish
closed empty objects `{}`. Unknown members and out-of-range `topN` values reject the complete candidate.

Studio Clock settings are the closed object `showSecondProgress`, `externalDotsAlwaysOn`, `showSeconds`,
`secondsColor`, `showDate`, `dateFormat`, `timeColor`, and `glowPercent`. Defaults are respectively `true`, `true`,
`true`, `#FF1616`, `false`, `dd-mm-yyyy`, `#FF1616`, and `35`. Colors are exact `#RRGGBB`; date format is one of
`dd-mm-yyyy`, `mm-dd-yyyy`, and `yyyy-mm-dd`; `glowPercent` is an integer from 0 through 100. The host merges omitted
members from these defaults before static validation and provider creation. Unknown members (including
`backgroundColor`, which is host-owned), malformed booleans/colors, another date format, or a non-integer or
out-of-range `glowPercent` reject the complete candidate. The member list, date formats, `glowPercent` range, and
defaults exist once in `Plugins/StudioClock/StudioClockSettings.h`, compiled into both the host parser and the DLL.

Desk Clock settings are the closed object `flipDurationMilliseconds`, `cardColor`, `digitColor`, and `dateColor`.
Defaults are respectively `420`, `#FF3B43`, `#FFFFFF`, and `#D8D8D8`. Duration is an integer from 250 through 800 and
colors are exact `#RRGGBB` strings with case-insensitive hexadecimal digits. The host merges omitted members from
these defaults before static validation and provider creation. Unknown members (including `backgroundColor`),
non-integer or out-of-range duration, and malformed colors reject the complete candidate.

Matrix Rain settings are the closed object `seed`, `glyphHeightDips`, `densityPercent`, `speedPercent`,
`trailLengthGlyphs`, `mutationPerSecond`, `headColor`, `trailColor`, and `glowPercent`; the rain falls over the
host-resolved dashboard background. `backgroundColor` is not a Matrix Rain member.

5H4D3R5 settings are the closed object `mode`, `shader`, `intervalSeconds`, `shuffle`, and `renderScalePercent`.
`mode` is `single`, `random`, or `slideshow` (default `slideshow`); `shader` is one of the fourteen exact lowercase
names of the bundled catalog in `Plugins/5H4D3R5/ShadersSettings.h` (default `seascape`);
`intervalSeconds` is an integer from 10 through 3600 (default 120); `shuffle` is a boolean (default `false`);
`renderScalePercent` is an integer from 25 through 100 (default 50). The host merges omitted members from these
defaults before static validation and provider creation. Unknown members (including `backgroundColor`, which is
host-owned), another mode, a name outside the catalog, non-integer or out-of-range numbers, and a non-boolean
`shuffle` reject the complete candidate. The catalog, mode names, ranges, and defaults exist once in that header,
compiled into both the host parser and the DLL.

Launcher settings are the closed object `shortcuts` plus optional `iconSize`. `shortcuts` is an array of 0 through 32
closed binding items (`Specs/Plugins/Plugins_Actions.md`): optional `action` (an action name; omitted means
`system.launch`), `target` (UTF-8 string, at most 512 bytes; required and non-empty for `system.launch`), and
optional `icon` (a Segoe Fluent Icons glyph name or `png:<absolute path>`, at most 260 bytes; required for any other
action). `iconSize` is `"small"`, `"medium"`, `"large"`, `"huge"`, or `"automatic"`; omitted values merge to
`"automatic"`. Defaults are `{"shortcuts":[],"iconSize":"automatic"}`. Unknown members, non-arrays, extra item members (including the
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
commits, failure to read the renamed file's stamp MUST NOT report a failed save; clear deduplication state and allow
reload.

A document write (a widget persist, a `dock.thickness` drag, or a template install or recovery, with or without the
first-run dock) writes a same-directory temporary file, MUST flush it to disk (`FlushFileBuffers`) before the
rename, and MUST treat a short write (`WriteFile` reporting fewer bytes and no error) as the failure
`ERROR_WRITE_FAULT`, so a power loss right after a save cannot leave the settings name on unwritten or truncated
bytes. A failed write MUST NOT leave its temporary file behind. The temporary's handle is write-through, shares
nothing, and renames the file through itself, so no other program can open the temporary before it is renamed. A
rename through a handle is not documented as durable when it returns, so the write MUST flush the renamed file again
after the rename, which writes the file system's log through the new name (NTFS and ReFS journal the rename); the
rename has committed by then, so a failed second flush MUST NOT report a failed save. The flushes run synchronously on
the UI thread, before and after the rename of each actual document write: the release of a dock drag that changed the
thickness, a widget persist or queued import that changed the document, collect-on-exit only when the collect changed
something (including when a page swipe commits, inside that frame's tick), and an install at startup. They never run
per frame, while idle, or on a live reload. Measured 2026-10-07 for the data flush of a 24 KB document on an NVMe
system SSD: median about 2 ms and p95 under 2.5 ms, but the worst of 3,300 flushes took
about 235 ms while parallel builds ran on the machine, which a swipe commit shows as a visible hitch. The schema copy,
refreshed from the deployed file on every start, is not flushed: a schema lost to a power loss is copied again at the
next start.

A persist whose merge leaves the stored instance object unchanged (an unchanged collect, a repeated import) MUST NOT
validate, re-serialize, or write the document: typed settings and the retained source, comments included, stay byte
for byte and the host returns `S_FALSE`. The one exception is an earlier deferred write still held in memory (below),
which such a persist writes once the file allows it.

The host MUST write the user document, for a widget persist, collect-on-exit, a queued import, or a `dock.thickness`
drag, only while the file on disk is the document last applied: its current stamp MUST equal the stamp recorded when
that document was applied or written by the host. With no applied stamp, a different stamp, or a stamp that cannot be
read, the host MUST NOT write. This covers a rejected save, a save the watcher has not processed yet, a deleted or
unreadable file, a file another program holds open with write access or without `FILE_SHARE_DELETE`, and a
`--settings` file that fell back to the deployed default. The patched typed settings and source stay in memory, the
host returns `S_FALSE` so the widget keeps its state, and one Warning `settings-persist-deferred` is logged per
distinct on-disk state (a missing and an unreadable file count as one state). A later persist, even one that changes
nothing more, writes the held change once the file is free and still the document last applied; the next applied load
replaces that in-memory document instead. An applied load and a write each settle a deferral whose notice was not
taken yet, so no later persist reports it.

The stamp check and the replacement form one guarded step, so that a save another program makes meanwhile is not
overwritten. The host opens the target with `DELETE` access sharing only read and delete, which keeps every other writer
out and fails (a deferral) while another program holds the file as above; for the few milliseconds the guard is open, a
reader that does not share delete (most do not) is refused too and can read again. It checks the stamp through that
guard, writes and flushes the temporary, checks that the path still names the guarded file, and renames the temporary
over it with POSIX semantics (`FileRenameInfoEx`) while the guard stays open. An editor's in-place save or classic
rename during that time is refused; an atomic replacement, rename, or deletion of the name, which the guard's delete
sharing lets through, makes the persist defer instead of undoing it. Only the instant between that last check and the
rename stays open. The stamp then recorded is the renamed file's own, read through the temporary's handle before any
other program can open the file, never a later path query that an editor's save could answer. Where the file system or
Windows build has no POSIX rename (FAT, exFAT, some network shares), the guard is released right before a classic
replacement, which fails like any refused replacement while another program holds the file open, and the stamp is read
by path once the temporary's handle has closed, since such a file system may set the last write time only then. The
guard opens without data access: measured 2026-10-07 on the same SSD with a probe of both write sequences, 300
interleaved writes of a 24 KB document each, the guarded replacement took a median 3.5 ms (p95 6.7 ms) against 3.3 ms
(p95 6.3 ms) for the previous unguarded write, while a guard opened for reading data took a median 4.3 ms to open the
just-written file on that machine.

A widget persist that changes the document (collect-on-exit and a queued import included) writes the whole document
again from its parsed form, which keeps no comments, and that document MUST use a compact, readable layout with
two-space indentation and a final LF newline. Keep
empty objects and arrays inline. Keep small objects inline when they fit a soft 120-byte line width; a single scalar
property stays together even when its indivisible string or path exceeds that width. Keep the root object, nonempty
`declare`, `pages`, `widgets`, `columns`, `rows`, and `shortcuts` sections multiline, and put each nonempty array item on its own
line. The internal plugin/factory settings representations remain compact. Reject a formatted result exceeding
1 MiB and roll back the typed and source state. Preserve semantic values, member order and compatible newer-minor
fields. Formatting changes only whitespace outside JSON tokens and runs only when saving settings. The other writes
keep the document's own text: the `dock.thickness` drag and the first-run dock are source edits that leave every byte
outside their patch as it was, comments included ("Dock", "Cold load and recovery"), and an install writes the
template's bytes unchanged.
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
- When XENEON discovery succeeded without finding a display outside a remote session
  (`Specs/UI/UI_XeneonDisplayWindowing.md` "First start without a XENEON"), a default file installed because it was
  missing is the template plus the first-run dock (`PatchFirstRunDock`): one `dock` member on its own line after
  `version`, at that member's indentation and in the file's own line breaks, preceded by a two-line comment naming why
  it was added, that `"edge": "none"` restores the standard window, and where the other members are described. The
  template's commented-out `dock` example (a root `//` line whose text starts with `"dock":`) MUST be removed together
  with the comment lines directly above it that introduce it and tell the reader to uncomment it, each whole line with
  its line break, so the installed file defines the dock once and following its comments cannot add a duplicate
  `dock` member. Every other byte of the template is unchanged; an existing `dock` member would have its value
  replaced instead, and `version.minor` rises to 2 when lower, or to 3 when the dock names the `secondary` monitor or
  sets a non-default `animationMilliseconds` (both added by minor 3).
  The display spec owns the dock's edge, monitor, and thickness (the free bottom edge first, on the second screen when
  there is more than one display). The store MUST ask for that dock (`FirstRunDockProvider`) only once it has found
  the default file missing, at most once per start, so a start that finds the file, a recovery, a `--settings` file,
  and the self-test measure no display for it. The patched document is validated before the same atomic
  same-directory write, and an existing file is never patched. The recovery of an invalid or incompatible default MUST
  install the plain template, even without a XENEON, so a file that failed validation never turns the user's XENEON or
  window configuration into a bar. After a failed discovery, in a remote session, when no dock is made, or when the
  patch cannot be applied, the plain template is installed; the first-run dock never fails startup.
- A missing, unreadable, or invalid command-line file is never modified. RedXe reports the problem, runs with the
  deployed default configuration in memory, and writes nothing to that path until a later save of it loads ("Plugin
  persist").
- The recovery notice and the command-line file report are a message box before the window is created, except in an
  unattended `--screenshot` run, which writes the same text as one Warning record (`settings-fallback-notice`) instead
  (`Specs/UI/UI_XeneonDisplayWindowing.md` "Configuration behavior").
- If a deployed default cannot be read or validated, startup fails rather than inventing settings.
- Template/schema installation and recovery use same-directory temporary files and write-through atomic rename; a
  settings template is written and flushed like any document write ("Plugin persist"), the schema copy is not.

## Live reload and diagnostics

- Normal execution watches the selected default or external file using event-blocked directory notification. Polling
  and periodic reload timers are forbidden.
- The watcher worker only coalesces and posts `SettingsWatcher::kSettingsChangedMessage`; parsing and runtime mutation
  remain on the UI thread.
- The UI thread compares volume, file identity, last-write time, and size before parsing. Applied and rejected stamps
  are deduplicated; every distinct later change is reconsidered.
- A valid candidate is applied transactionally after the host reselects the page that was current, when that page
  still exists in the candidate. Failure preserves or restores the previous settings and dashboard and never marks the
  file applied. That includes a live window-kind switch that fails: the window returns to its previous kind
  (`Specs/UI/UI_XeneonDisplayWindowing.md` "Switching the window kind").
- A reload that arrives inside the titled window's move/size loop, and a valid candidate that would rebuild the active
  page of a minimized standard window, MUST wait: neither applied nor rejected, with no stamp recorded, until the loop
  ends or the window is restored, when the file is read once more (`Specs/UI/UI_XeneonDisplayWindowing.md` "Window
  and rendering lifecycle"). `redxe.settings.reload` waits the same way.
- `redxe.settings.reload` forgets the stamps and posts the watcher's message; the reload MUST run from the message
  loop, never inside the widget input callback that requested it, because applying it can release that widget
  (`Specs/Plugins/Plugins_Actions.md`).
- A candidate whose typed settings equal the running ones in every member except the retained source and the retired
  `services` entries, which nothing runs (`RuntimeSettingsEqual`; for example a comment or spacing edit), only becomes
  the retained source document: it MUST NOT cancel a page swipe, a raise, or a wheel sequence. A swipe that commits
  after it MUST change only the active page, so that source stays the document a later persist or dock drag writes.
- A successful live load MUST apply the candidate in memory only. It MUST NOT write the watched file, format it,
  persist a widget merge, or collect-on-exit onto that path. The editor's bytes stay until an explicit widget persist
  or other user-driven save. `--self-test` MUST NOT write `%LocalAppData%` and MUST NOT write the deployed template.
- Invalid, unreadable, or missing live input remains untouched and leaves the exact last-valid in-memory state active.
  An invalid live load MUST NOT rewrite the invalid file and MUST NOT write the last-good document over it, and neither
  may a later persist or dock drag ("Plugin persist"). Monitoring continues until a later distinct save can be loaded.
- Diagnostics MUST name the JSON path and the specific problem in clear user language. When the byte location is
  reliable (JSON syntax errors, or a path the locator can resolve), they MUST also include line and column. The
  locator MUST read comments where the parser does, as the dock source patches do: a line comment ends at CR or LF.
  The dialog MUST NOT report a generic version-5 schema failure at `path $` when a more specific member is known.
- At most one modal settings-error dialog may exist. Monitoring continues while visible. A later invalid save refreshes
  it with the newest bounded error list; dialogs never stack. If all errors do not fit, an ellipsis states that more
  were omitted. A valid save applies and closes the dialog automatically. The dialog opens inside the work area of the
  RedXe window's monitor, over the full bar of a dock even while an autohide bar is collapsed
  (`Specs/UI/UI_XeneonDisplayWindowing.md` "Notice windows").
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
  template System last column compiles Gpu/Thermal/Power short-side ratios 2, 3, and 1. Every schema `$defs` entry is
  referenced.
- Tests reject version 4 documents, `layout` / `areas` / `arrangeAlong` / `sizeRatio`, nested `settings`, and
  `override`. They reject malformed syntax/version, duplicate and exact-version unknown members, unresolved
  references, invalid merge results, plugin settings failures, Process Viewer `topN` values outside 1 through 32,
  Network Meter and GPU Processes `topN` values outside 1 through 16, and malformed Studio
  Clock booleans including `externalDotsAlwaysOn`, colors, date formats, `glowPercent` values that are not integers
  from 0 through 100, and unknown members, and verify its complete merged defaults and a valid `glowPercent`
  override. They also reject Desk Clock duration and color failures and verify its complete merged defaults and valid
  partial overrides. They also
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
  non-object `services`, a string member, and `use`. Both shipped templates MUST carry the current minor (3),
  configure every catalogued service, and carry no retired service entry.
- Tests parse the exact `v1.0.102` Release and Debug templates (`Settings.Tests.ReleasedTemplates.h`) and prove they
  load and validate, with the Debug template's removed `zoom.*` bindings kept and the Zoom entry recorded as retired
  rather than configured, and that the default store keeps such a file byte for byte without a fallback or
  `.invalid-` backup. They prove the retired Zoom entry loads with any value of the seven retired members in a minor 3
  document, with a sole retired member set to `null`, and without them, is recorded in `retiredServices` and not in
  `services`, that any other Zoom member (including a retired name in another case, and one set to `null`, alone or
  beside a `null` retired member) and a second Zoom entry are rejected, and that the schema's services Zoom variant is
  deprecated and lists the members as deprecated. A minor 2 document whose Logicon key, dialpad button, and turn bind
  `keys.down` or `mouse.down` loads, validates, and keeps those bindings for the service.
- Host tests load an empty services Zoom entry, one carrying retired members, and one whose only retired member is
  `null` through the settings store, each at startup and on a live reload, and prove the store records the entry in
  `retiredServices` at each load and loads nothing for an unchanged notification; that the report both of
  `Application`'s load paths make (`PluginHost::LogRetiredServiceSettings`, after the startup load and after each
  applied live reload) logs exactly one `service-retired-settings-ignored` Warning from `builtin.zoom`, without an
  `HRESULT`, per entry of the document it is given and none for a document without one; and that starting and
  re-applying the services log none. `test.ps1`'s end-to-end `--screenshot` run starts `RedXe.exe` with a portable file
  carrying the `v1.0.102` entry, so `Application`'s startup order runs for real: the file loads as written and its log
  holds that one Warning.
- Tests prove `trayIcon`: omitted it is `true` in Release and `false` in Debug, also in a minor 2 document; an authored
  `true` or `false` wins; a string, a number, `null`, an object, and a duplicate member are rejected, with the
  diagnostic on `$.trayIcon`; the member changes no other typed setting, and a toggle is a runtime change
  (`RuntimeSettingsEqual`) that keeps the active page (`ActiveDashboardRuntimeEquals`); both templates author it
  (`false` in Debug, `true` in Release); and the schema declares it a boolean without a default.
- Tests prove `RuntimeSettingsEqual` member by member: a candidate that differs from the running settings in any one
  root member alone is a runtime change, and one that differs only in the retained source or the retired `services`
  entries is not.
- Tests accept a complete `dock` object, prove member-by-member default merging (an omitted object, `{}`, and a minor 1
  document equal `edge: none`), keep `edge: none` validating the other members, accept the `secondary` selector in the
  document and on `--dock` and an `animationMilliseconds` of 0, reject every malformed member (unknown edge or mode,
  `all`, `0`, `name:`, `Secondary`, and `second` selectors, thickness 31 and 1081, peek 0 and 65, delays past their
  maximum, an `animationMilliseconds` of 1001, -1, a string, or a fraction, a non-boolean reserve, an unknown member, a
  non-object `dock`) with the diagnostic on `$.dock.<member>`, and prove the `--dock*` grammar, its errors, and the
  merge precedence over the document.
- Tests prove `PatchFirstRunDock` on both shipped templates with a `top` bar on `secondary` (one contiguous commented
  insertion right after `version`, the template's commented `dock` example and its introduction removed so the result
  holds one `"dock"` and no instruction to uncomment another, the template's CRLF line breaks kept, the typed document
  otherwise unchanged), on a document whose root example follows a separate comment and a blank line (only the
  example and its introduction go; a nested `// "dock":` comment and one after a member stay), on a
  minor 1 document (raised to 3 for `secondary` or a non-default `animationMilliseconds`, which is then written, and to
  2 for `primary`), with `version` as the last member, and over an existing `dock` value (a `name:` selector leaves
  minor 2), and that an invalid dock (the `none` edge, an edge or mode outside the enumerations, the `all` selector, a
  number out of range, an `animationMilliseconds` past 1000) or a malformed document, including a scalar `version` or
  incompatible major version, is refused without changing the source. They prove that the store installs the first-run
  dock for a missing default file, keeps an existing file byte for byte, recovers an invalid default file with the
  plain template byte for byte and a notice that names no bar although a dock is offered, installs the plain template
  byte for byte when no dock is offered, none is made, or the offered dock is refused by the patch, and never writes a
  missing `--settings` file; and that it asks its provider for the dock exactly once for a missing default file and
  never for an existing file, a recovery, a `--settings` file, or the self-test. A dock whose every member leaves its
  default is written, parsed back, and installed exactly as it was made.
- Tests prove the BOM rule: both shipped templates with a BOM load to the same typed settings with the BOM kept in
  the source; `PatchDockThickness` and `PatchFirstRunDock` produce the BOM followed by the bytes they produce without
  it; a widget persist rewrites the document with exactly one leading BOM; an error after a BOM is reported on its own
  line and not as a BOM problem, and a BOM alone or with only comments is an empty file; and the store loads a
  BOM-prefixed default file at startup unchanged and without a backup, then writes a dock drag to it with the BOM
  kept.
- Tests prove `PatchDockThickness` adds a new `dock` to both shipped templates as one line right after `version` in the
  template's line breaks, and below a comment that ends `version`'s line, with or without a comma after `version`;
  appends a missing `thickness` after the last dock member on its line, before a trailing comma and comment, and on a
  new line at its indentation; fills an empty `dock`, on a line of its own one level deeper when the closing brace
  starts its own line, and appends a missing `version.minor`; keeps CR line breaks in a CR-only document; patches a
  `dock` that follows a line comment ended by a lone CR in place; and raises the typed minor with the source minor. A
  release at the current thickness returns `S_FALSE` and leaves the source and typed minor unchanged, and typed
  settings whose dock differs from the document's are refused with `ERROR_INVALID_DATA` and leave the source,
  thickness, and minor unchanged. A dock drag whose write fails restores the typed minor with the thickness and
  source, and a committed drag leaves the typed minor equal to the file's. The diagnostic locator, which shares the
  patch's scanner, locates a member after a line comment ended by a lone CR where the parser reads it.
- Tests prove a partial widget persist merge keeps unspecified members and rejects unknown plugin members.
- Tests prove the persist write gate: a widget persist or dock drag over a rejected save, a loaded but not yet applied
  save, a deleted file, and a `--settings` file that fell back to the default returns `S_FALSE`, leaves the file bytes
  (or its absence) unchanged, keeps the merge in memory, and raises one deferral notice per on-disk state; a persist
  after the reload is applied writes again, and a repeated persist writes the changes an earlier deferral held once the
  same file is back. An unchanged widget persist and a dock drag released at the current thickness return `S_FALSE`
  and leave typed settings, source, and file bytes, comments included, unchanged; such a drag still writes a change an
  earlier deferral held once the same file is back, and then nothing. A deferral whose notice was not taken is not
  reported after an applied load replaced its change (the next unchanged persist reports nothing) or after a write
  carried it.
- Through the SettingsTests write seam (`SetSettingsWriteSeamForTesting`, compiled only with `REDXE_SETTINGS_TESTS`),
  tests prove the guarded replacement: a file another program holds for writing (also while sharing everything, as VS
  Code does), without sharing, or without `FILE_SHARE_DELETE` defers each persist with one notice for the one on-disk
  state, keeps the change and the bytes, and the next persist writes the change once the file is free; an in-place save
  attempted while the temporary is flushed fails with `ERROR_SHARING_VIOLATION` and the persist commits; an in-place
  save and a POSIX replacement attempted right after the rename both fail with `ERROR_SHARING_VIOLATION`, and the stamp
  recorded is the one the next `TryLoadChanged` sees (`Unchanged`); a POSIX replacement or a deletion while the
  temporary is flushed goes through and the persist defers without overwriting or recreating the file, and an applied
  load of the replacing document is then written by the next persist; and with the POSIX rename refused the classic
  replacement commits. No temporary file stays behind in any case.
- Tests prove compact/idempotent formatting, inline small objects and long single-path records, multiline sections
  and arrays, fewer lines than fully expanded output, escaped/Unicode paths, named/inline/use-object widget round
  trips, compatible unknown-field retention, and transactional rejection of oversized formatted output.
- An isolated file test MUST fail the replacement with a short write through the write seam (`ERROR_WRITE_FAULT`),
  verify exact typed/source/disk rollback for a widget persist and a dock drag with no temporary file left beside the
  settings file, then verify a different partial save commits without including the rejected patch.
- Plugin contract tests prove `CollectPersistentSettings` returns `S_FALSE` when nothing to save and `E_POINTER` for a
  null `writtenBytes`. Host tests prove persist without a handler is `E_UNEXPECTED` and that a handler receives a
  partial object.
- Tests verify exact invalid-default backup bytes/name, fresh installation, external fallback without mutation,
  one-time legacy Release filename migration, stamps, watching, last-valid preservation, modal refresh/close
  behavior, and rejected-stamp deduplication.
- Hidden host tests prove first-page startup, blank pages, inactive-page resource absence, transactional apply, WARP
  rendering, current-plus-adjacent-only swipe staging, that a live reload of an unchanged page list keeps the page
  that was current, and that a catalogued plugin whose DLL cannot be mapped becomes a placeholder without aborting
  startup. The `--self-test` run proves that a source-only reload keeps a staged swipe and is still the document after
  the swipe commits to its page. Host tests prove JSONL `Log` reject/write/flush behavior, UTC-dated file names, and
  retention deletion of expired and legacy log files. Settings tests prove the default logs path is the
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

- Parsing, paths, recovery (including the first-run dock install), diagnostics, stamps, and persist merge:
  `RedXe/Settings.*`
- Event-blocked watching: `RedXe/SettingsWatcher.*`
- UI-thread apply, persist thunk, and error-dialog state: `RedXe/Application.*`
- Collect-on-exit: `RedXe/DashboardHost.cpp`
- Contracts and runtime creation: `RedXe/PluginManager.*`, `Common/PlugInterfaces/Factory.*`
- Host persist thunk and JSONL log writer: `RedXe/PluginHost.*`
- Schema/templates: `Specs/Settings.schema.json`, `Settings/`
- Tests: `Tests/SettingsTests/`, `Tests/HostPluginTests/`, `Tests/PluginContractTests/`
