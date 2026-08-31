# RedXe settings contract

Status: current normative product contract
Last reviewed: 2026-08-31
Owner: `SettingsStore`, `SettingsWatcher`, and UI-thread application orchestration

## Scope

This specification owns the RedXe user-settings location, build-specific file selection, schema, cold-start recovery,
and live reload. Plugin ABI and rendering behavior remain in `Specs/Plugins/Plugins_API.md`; CPU, memory, allocation,
and wake-up requirements remain mandatory through `Specs/Core/Core_PerformanceAndResources.md`.

The terms **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative.

## Storage and deployed templates

RedXe MUST store editable user settings under:

```text
%LocalAppData%\RedXe\Settings\
```

The selected file is build-specific:

| Build | User settings file |
| --- | --- |
| Debug | `RedXe-debug.settings.json` |
| Release | `RedXe-1.0.settings.json` |

The build output MUST contain `Settings\RedXe-debug.settings.json`, `Settings\RedXe-1.0.settings.json`, and
`Settings\RedXe.settings.schema.json`. The repository source of truth for the two templates is `Settings/`; the
canonical user schema is `Specs/Settings.schema.json`.

On first normal startup RedXe MUST create the settings directory, install the selected template without replacing an
existing settings file, and install the schema beside it. When the Debug file is absent and the Release file exists,
Debug MUST seed its independent file from the Release file; subsequent edits remain independent. Release never reads
the Debug file.

The hidden `--self-test` path MUST parse the deployed template selected for its build and MUST NOT create, read, write,
or watch the user's `%LocalAppData%` settings.

## Version 3 normalized document

The canonical machine contract is `Specs/Settings.schema.json`. A version 3 document contains exactly `$schema`,
`schemaVersion`, `plugins`, and `dashboard` at its root.

- `$schema` MUST equal `RedXe.settings.schema.json` and `schemaVersion` MUST equal `3`.
- Version 1, version 2, mixed-version, missing, duplicate, and unknown host-owned members MUST be rejected. Cold-start
  recovery preserves an old document as an invalid backup before installing the selected version 3 template; RedXe
  does not silently reinterpret a previous shape.
- JSON comments and trailing commas MAY be accepted for hand-edited user files.
- The complete settings file MUST NOT exceed 1 MiB.

### Normalized plugin registry

`plugins` MUST contain between 1 and 16 records. Every record contains exactly:

| Member | Contract |
| --- | --- |
| `id` | Stable 1–128-byte machine ID, unique case-insensitively in the document |
| `enabled` | Boolean module-eligibility state |
| `private` | Plugin-owned JSON object whose compact representation is at most 1024 bytes |

The shipped templates MUST list `builtin.rotating-triangle`, `builtin.gdi-orbit`, and `builtin.matrix-rain` exactly
once. Their plugin-private objects are empty in version 3. Disabled bundled plugins MUST NOT be loaded before first
use. A module mapped earlier remains mapped under the plugin no-unload policy, but disabled plugins own no provider,
widget, child-window, or device resources.

Plugin ID uniqueness is a semantic rule enforced by the C++ parser because JSON Schema `uniqueItems` cannot express
case-insensitive uniqueness by one member.

### Dashboard, pages, grid, and instances

`dashboard` contains exactly `grid`, `activePageId`, and `pages`. Page and placement behavior is normative in
`Specs/UI/UI_Dashboard.md`.

- The grid has 1–64 columns and rows; shipped templates use 32×9.
- There are 1–8 pages and 1–16 widgets per page.
- Every widget contains exactly `id`, `pluginId`, `typeId`, `placement`, and `private`.
- Widget IDs are unique case-insensitively across the document. Plugin references resolve enabled registry entries.
- A placement is a non-overlapping in-grid rectangle expressed by `column`, `row`, `columnSpan`, and `rowSpan`.
- Each `private` instance object is compacted and copied into bounded typed settings storage. yyjson values and source
  strings are never retained.
- The runtime instantiates only `activePageId`; inactive pages remain validated configuration without runtime
  resources.

### Bundled widget private settings

Rotating Triangle and GDI Orbit currently require an empty instance-private object. Matrix Rain requires exactly:

| Member | Type | Accepted value |
| --- | --- | --- |
| `seed` | unsigned integer | 0–4294967295 |
| `glyphHeightDips` | integer | 12–48 |
| `densityPercent` | integer | 10–100 |
| `speedPercent` | integer | 25–300 |
| `trailLengthGlyphs` | integer | 6–48 |
| `mutationPerSecond` | integer | 0–30 |
| `headColor` | string | exactly `#RRGGBB`, case-insensitive hexadecimal |
| `trailColor` | string | exactly `#RRGGBB`, case-insensitive hexadecimal |
| `backgroundColor` | string | exactly `#RRGGBB`, case-insensitive hexadecimal |
| `glowPercent` | integer | 0–100 |

The Debug active page MUST contain two Rotating Triangle instances, one GDI Orbit instance, and one Matrix Rain
instance. The Release active page MUST contain only one Matrix Rain instance spanning the complete 32×9 grid.

### Normalized factory configuration

For each active widget group, the host serializes one bounded factory object:

```json
{
  "plugin": {},
  "instance": {}
}
```

`plugin` is the referenced registry record's private object and `instance` is the widget record's private object.
The host passes this complete envelope through the factory V2 configuration tail and retains neither source pointer in
the plugin. Providers with byte-identical compact plugin and instance objects MUST be shared; different
instance-private objects MUST not accidentally share configurable provider state. Bundled plugins with no private
fields validate the normalized empty-object envelope; Matrix Rain validates and copies its normalized instance object.

## Cold-start validation and recovery

RedXe MUST validate the selected user file before plugin or Direct3D initialization. If the file is syntactically or
semantically invalid, RedXe MUST preserve the exact invalid bytes beside it using a UTC-stamped `.bad.*` suffix,
atomically restore the build's deployed template, and validate the restored file before continuing. I/O, permission,
or deployment failures MUST fail startup rather than overwrite data whose validity was not established.

Template and schema replacement MUST use a same-directory temporary file followed by a write-through rename so an
interrupted copy cannot expose a partial document.

## Live reload

- Normal interactive execution MUST watch the settings directory with `FindFirstChangeNotificationW` using file-name,
  last-write, and size notifications. It MUST block on a stop event and the directory notification; polling and
  periodic reload timers are prohibited.
- The watcher worker MUST only coalesce and post `SettingsWatcher::kSettingsChangedMessage`. It MUST NOT parse JSON,
  mutate settings, create widgets, touch HWND children, or call Direct3D.
- The UI-thread handler MUST compare a file stamp made from volume, file identity, last-write time, and size before
  parsing. Already-applied and already-rejected stamps MUST be ignored.
- A valid changed document whose active runtime projection changes MUST recreate the configured widget instances and
  dashboard live without reloading plugin DLLs or restarting the process. The active runtime projection consists of
  the grid dimensions, ordered active-page widget records, and plugin records referenced by those widgets. The
  renderer MUST release device callbacks before widget references are replaced, then recreate device resources for
  the new dashboard.
- A valid edit that changes only inactive pages, page labels, or plugin records unused by the active page MUST replace
  the authoritative typed document and advance its applied stamp without tearing down or recreating the active
  dashboard.
- Plugin and instance private objects MUST be serialized into the normalized bounded factory envelope only during
  provider creation. A private setting or active-page change MUST stage the complete selected page before committing
  the dashboard; a plugin MUST retain no pointer into host settings storage.
- An invalid live edit or a temporarily missing file MUST leave the last valid runtime settings active. Live reload
  MUST NOT replace or back up the user's invalid edit. A rejected stamp MUST be diagnosed once and reconsidered only
  after the file changes again.
- If applying a valid changed document fails, RedXe MUST restore the previous widget configuration and runtime. It
  MUST mark the failed stamp rejected and keep the window running when rollback succeeds.
- The directory watcher MUST start only after the previous-crash prompt has completed. Its initial catch-up
  notification MUST reconcile edits made before watcher startup; settings reload MUST NOT execute from that modal
  prompt's nested message loop.
- Shutdown MUST signal and join the watcher before destroying the target HWND.

## Performance and resource requirements

The watcher adds one sleeping thread and one change-notification handle only during normal interactive execution. It
MUST produce zero periodic wake-ups while the settings directory is unchanged. Notifications MUST be coalesced, and
the 100 ms edit debounce MUST wait on the stop event rather than sleep uninterruptibly. Parsing and widget recreation
are cold change-path work; no settings operation may allocate or perform I/O on the frame path.

`AppSettings` MUST remain no larger than 256 KiB. Every full-document scratch value used for parsing, live reload,
self-test, or transactional application MUST use bounded heap storage rather than automatic UI-thread storage. Live
reload MUST compare the file stamp before allocating one candidate, must not allocate a second full-document parsing
scratch value, must release rejected candidates promptly, and must roll back against the unchanged active member
settings without making another full-document copy. The authoritative bounded document retains validated inactive
pages so page selection and transactional rollback do not require another file read. After a successful apply,
`Application` adopts the candidate allocation as the new authoritative document instead of copying its full fixed
storage.

Disabling a plugin registry record before its DLL is loaded MUST avoid loading that DLL. A DLL already mapped during
the v1 session remains mapped under the plugin no-unload policy, but its providers, widget instances, native resources,
and device resources MUST be released when it has no active-page widgets.

## Required validation

- Both deployed templates parse to schema version 3 and match their normalized plugin registry, 32×9 grid, pages,
  active composition, instance-private objects, and Matrix defaults.
- Invalid JSON, previous-version hybrids, empty registries/pages, missing or duplicate fields, unknown host fields,
  wrong types, duplicate plugin/page/instance IDs, invalid active-page or plugin references, disabled references,
  unsupported bundled plugin/type pairs, oversized private objects, invalid grids, zero/out-of-bounds/overlapping
  placements, malformed colors, and every out-of-range Matrix value are rejected.
- The canonical schema itself parses and expresses the normalized records, required private objects, page/grid bounds,
  placement fields, color format, and Matrix numeric bounds. C++ tests cover semantic uniqueness, cross-reference,
  overlap, and dynamic grid-bound rules that JSON Schema cannot express.
- File stamps change after an atomic replacement.
- A directory edit wakes the watcher and posts one coalesced UI message; stopping the watcher terminates promptly.
- The production parser succeeds on a thread with a 128 KiB stack reservation when its input and output settings
  storage are heap-backed.
- Debug and Release x64 hidden WARP tests parse their deployed templates without touching user settings.
- The hidden self-test MUST switch to another valid page/private Matrix configuration, render the recreated widget,
  and prove that an invalid candidate does not mutate the active typed settings. Debug additionally verifies that a
  disabled Matrix module is not loaded; Release verifies the same rule for disabled triangle and GDI modules.
- The hidden production host/plugin harness MUST prove active-page-only creation, configured grid placement, a valid
  page/private reconfiguration with bounded provider lifetime, and rollback that preserves the previous live page.
- Settings tests MUST prove that inactive-only edits compare equal under the active runtime projection while an active
  widget placement or referenced plugin change compares unequal.
- Release ARM64 compiles.

## Implementation anchors

- Typed parsing, locations, recovery, and stamp dedupe: `RedXe/Settings.*`
- Event-blocked directory notification: `RedXe/SettingsWatcher.*`
- UI-thread application and rollback: `RedXe/Application.*`
- Widget instance recreation: `RedXe/PluginManager.*`
- Canonical schema and shipped defaults: `Specs/Settings.schema.json`, `Settings/`
- Automated parser, schema, stamp, and watcher checks: `Tests/SettingsTests/`
- Automated transactional host/plugin checks: `Tests/HostPluginTests/`
