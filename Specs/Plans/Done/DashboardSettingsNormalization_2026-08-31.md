# Done: Normalize plugin, page, and grid dashboard settings

Status: `COMPLETE`
Created: 2026-08-31
Completed: 2026-08-31
Owner: RedXe settings, plugin loading, dashboard layout, and validation

## Purpose

Replace the plugin-specific schema version 2 dashboard flags with one normalized settings model that supports a
bounded plugin registry, multiple pages, grid-based widget placement, and private configuration for every plugin and
widget instance.

Durable behavior belongs in `Specs/Core/Core_Settings.md`, `Specs/UI/UI_Dashboard.md`,
`Specs/Plugins/Plugins_API.md`, and `Specs/Settings.schema.json`. This plan is only the implementation record.

## Frozen schema version 3 shape

- `plugins` is an array of bounded records with the exact host-owned fields `id`, `enabled`, and `private`.
- Plugin IDs are valid machine IDs and unique case-insensitively across the document.
- `dashboard.grid` defines 1–64 columns and rows; shipped XENEON templates use 32 columns by 9 rows.
- `dashboard.activePageId` resolves exactly one entry in `dashboard.pages`.
- Each page has a unique ID, display name, and one or more widget instances.
- Each widget has a document-unique instance ID, plugin ID, type ID, grid placement, and private JSON object.
- Placements use zero-based `column` and `row` plus positive `columnSpan` and `rowSpan`, remain inside the grid, and
  do not overlap within a page.
- Every widget references an enabled registered plugin. The current executable accepts the three bundled plugin/type
  pairs and fails closed on unsupported active content.
- The host creates only the active page. A live `activePageId` change recreates it transactionally.
- Factory configuration is normalized as `{ "plugin": <plugin-private>, "instance": <instance-private> }`.
  Providers with identical plugin/instance payloads are shared; distinct private payloads receive distinct providers.
- Matrix Rain accepts the normalized envelope while retaining its previously shipped direct settings object for
  factory compatibility.

## Resource bounds

- At most 16 plugin records, 8 pages, and 16 widgets per page.
- Machine IDs and page names are at most 128 UTF-8 bytes.
- Each plugin or instance private object serializes to at most 1024 bytes; the normalized factory envelope remains
  under the existing 4096-byte ABI cap.
- The complete typed settings value remains bounded to 256 KiB; production full-document scratch values are
  heap-backed so live reload cannot exhaust the UI stack.
- Parsing, validation, page switching, and provider recreation are cold paths. Cached active-page placements keep the
  frame and resize paths allocation-free.

## Implementation and validation checklist

1. [x] Add the normative dashboard settings and grid/page contracts plus the mandatory WIP closeout rule.
2. [x] Replace the templates and canonical JSON Schema with strict schema version 3 documents.
3. [x] Implement bounded typed parsing, unique/cross-reference checks, grid bounds/overlap checks, and private-object
   canonicalization.
4. [x] Drive `PluginManager` from the active page and normalized factory envelopes while retaining conditional module
   loading and transactional reconfiguration.
5. [x] Make `DashboardHost` derive cached design-canvas placements from configured grid cells.
6. [x] Expand settings and hidden host/plugin tests for duplicates, page selection, private configuration, invalid
   references, grid bounds/overlap, module loading, page switching, and placement scaling.
7. [x] Reconcile the broader architecture RFC, run formatting, Debug/Release x64 tests, Release ARM64 compilation,
   skill validation, and diff checks.
8. [x] Record evidence, mark every item complete, move this plan to `Specs/Plans/Done/`, and remove its WIP index row.

## RedSalamander reference review

- `Z:/src/RedSalamander/Common/PlugInterfaces/Factory.h` confirms direct factory selection by stable plugin ID,
  bounded borrowed metadata enumeration, normal COM ownership, and optional static configuration-schema discovery.
- `Z:/src/RedSalamander/docs/SettingsFile.md` confirms exact schema-version loading, cold preservation of invalid bytes
  before restoring defaults, comments/trailing-comma editing, and retention of the last valid runtime state after an
  invalid live edit.
- RedXe adopts those proven lifetime and recovery patterns without importing RedSalamander's unrelated file-manager
  settings shape. The dashboard-specific v3 registry, page/grid records, and normalized factory envelope remain owned
  by RedXe's normative contracts.

## Implementation outcome

- `AppSettings` uses bounded fixed-capacity plugin, page, widget, ID, and compact-private storage. The application is
  the sole complete typed-settings owner; `PluginManager` retains only narrow active grid state and runtime objects.
- All bundled providers receive a normalized `{ "plugin": ..., "instance": ... }` V2 factory envelope. Static
  triangle/GDI providers validate empty private objects; Matrix Rain validates and copies its instance-private object
  while retaining direct-object factory compatibility.
- Only the selected page is instantiated. Equal compact configurations share providers, page changes stage before
  commit, and rejected configurations preserve the prior live page.
- Grid cell edges are rounded once from integer cell coordinates for both child HWND bounds and GPU viewports. The
  hidden host harness covers a 7-column grid at a 1001-pixel width to prove adjacent regions have no seam or overlap.

## Validation evidence

- `./format.ps1`: passed; 29 C++ files formatted.
- `./test.ps1 -Configuration Debug -Platform x64`: passed settings/schema/watcher, the 128 KiB-stack parser regression,
  plugin ABI/rendering, hidden host/plugin, hidden WARP, and application self-test coverage.
- `./test.ps1 -Configuration Release -Platform x64`: passed the same automated suite using Release defaults.
- `./build.ps1 -Configuration Release -Platform ARM64`: passed all application, plugin, and test targets.
- `./validate-skills.ps1`: passed all 10 repository skills.
- `git diff --check`: passed.
- No desktop automation or interactive computer control was used; the production host and plugin stack was exercised
  through hidden HWND and WARP harnesses.

## Closeout condition

This plan is complete only when implementation, tests, required validation, and all normative contracts agree. At
that point repository policy requires it to move to `Specs/Plans/Done/`; a completed plan must not remain under WIP.
