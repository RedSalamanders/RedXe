# RFC: Human-first RedXe settings schema version 4

Status: `DONE` — implemented, validated, and merged into normative contracts
Created: 2026-08-31
Last decided: 2026-08-31
Owner: RedXe settings, dashboard composition, plugin configuration, and future settings UI

## Purpose and authority

Define the approved version 4 settings model for one physical XENEON display. Direct JSON editing is the primary
authoring experience; a future RedXe settings UI MUST represent and round-trip the same model.

This RFC is the completed design and implementation record. Current product authority is merged into
`Specs/Core/Core_Settings.md`, `Specs/UI/UI_Dashboard.md`, `Specs/Plugins/Plugins_API.md`,
`Specs/UI/UI_XeneonDisplayWindowing.md`, and `Specs/Settings.schema.json`.

## Product model

- One portable document represents one physical XENEON display.
- One RedXe process targets one display. Multi-display orchestration is outside this schema.
- With no settings argument, Debug and Release use their existing independent files under
  `%LocalAppData%\RedXe\Settings`: `RedXe-debug.settings.json` and `RedXe.settings.json`.
- `--settings <path>` selects a dedicated portable document. RedXe monitors the selected file even outside the normal
  Settings directory.
- The document contains ordered, horizontally swipeable pages. Every launch starts on the first page.
- Orientation is detected runtime state, never a settings field. Layout reflows dynamically between landscape and
  portrait.
- The supplied iCUE composer screenshots are behavioral references only. RedXe does not copy iCUE's private format.

## Complete representative document

```jsonc
{
  "$schema": "RedXe.settings.schema.json",
  "version": {
    "major": 4,
    "minor": 0
  },
  "wrapPages": false,

  // Optional reusable widget definitions.
  "declare": {
    "Main clock": {
      "plugin": "builtin.clock",
      "settings": {
        "format": "24-hour",
        "showSeconds": true,
        "style": {
          "textColor": "#FFFFFF",
          "backgroundColor": "#000000"
        }
      }
    },
    "System weather": {
      "plugin": "builtin.weather",
      "settings": {
        "location": "Paris"
      }
    }
  },

  "pages": [
    {
      "id": "main",
      "name": "Main",
      "layout": {
        "arrangeAlong": "long-side",
        "areas": [
          {
            "sizeRatio": 2,
            "arrangeAlong": "short-side",
            "areas": [
              {
                "sizeRatio": 3,
                "widget": "Main clock"
              },
              {
                "sizeRatio": 2,
                "widget": {
                  "use": "Main clock",
                  "override": {
                    "settings": {
                      "showSeconds": false,
                      "style": {
                        "textColor": "#FF4040"
                      }
                    }
                  }
                }
              }
            ]
          },
          {
            "sizeRatio": 1,
            "widget": {
              "plugin": "builtin.matrix-rain"
            }
          },
          {
            "sizeRatio": 2,
            "widget": "System weather"
          }
        ]
      }
    },

    // An empty object is a valid blank page.
    {}
  ]
}
```

The example IDs illustrate the shape. Shipped files MUST use IDs actually published by installed plugins.

## Root and syntax contract

| Member | Required | Contract |
| --- | --- | --- |
| `$schema` | No | Identifies the installed editor schema. Omission remains valid. |
| `version` | Yes | Object with required `major` and optional `minor`; omitted `minor` means `0`. |
| `wrapPages` | No | Omitted or `false` stops at both ends; `true` wraps continuously. |
| `declare` | No | Reusable widget definitions keyed by user-authored reference names. |
| `pages` | Yes | Ordered array containing 1–16 pages. The first page is the startup page. |

Duplicate member names in any object are errors. Comments and trailing commas are accepted. Other JSON5-only syntax,
including single-quoted strings, unquoted keys, non-finite numbers, extended numbers, and extended escapes, is invalid.

### Host schema compatibility

- Version 4 starts at `{ "major": 4, "minor": 0 }`.
- Missing, malformed, or unsupported versions are errors; RedXe MUST NOT guess a shape.
- A different or newer major is incompatible and rejected.
- A newer RedXe accepts older minors in the same major and supplies documented defaults for fields added later.
- An older RedXe accepts a newer minor in the same major, validates the shape it understands, and silently ignores
  additive fields it does not understand.
- For the exact supported version, unknown host-owned members are errors so typing mistakes are diagnosed.
- A future settings UI editing a newer compatible minor MUST preserve unknown fields when saving.
- Version 3 is not migrated automatically. Cold load follows invalid-default preservation and reset.

## Declarations and widget instances

### Declarations

- `declare` is optional and contains only reusable widget definitions.
- Each object key is the reference name; a repeated `id` member is not used.
- Names contain 1–128 Unicode characters and MAY contain spaces.
- Matching uses the exact case-sensitive Unicode sequence. RedXe performs no normalization.
- A document contains at most 128 declarations.

### Widget definition

| Member | Required | Contract |
| --- | --- | --- |
| `plugin` | Yes | Stable plugin ID selecting exactly one settings-visible widget kind. |
| `settings` | No | Plugin-owned object. Omission selects plugin defaults. |

One settings-visible plugin ID selects one widget kind. A DLL MAY package multiple kinds, but each kind publishes a
different plugin ID. The user document has no separate plugin registry, `enabled` flag, or type ID. Referencing a
plugin is sufficient to use it.

The schema does not expose separate plugin-private and instance-private objects. Each widget keeps its plugin ID and
base settings together. Provider sharing is a host optimization and MUST NOT change the user model.

### Widget forms

A leaf area's `widget` is exactly one of:

1. Inline definition:

   ```json
   { "plugin": "builtin.matrix-rain", "settings": {} }
   ```

2. Declared reference without override:

   ```json
   "Main clock"
   ```

3. Declared reference with override:

   ```json
   {
     "use": "Main clock",
     "override": {
       "settings": { "showSeconds": false }
     }
   }
   ```

Every appearance creates an independent runtime instance. References reuse authored configuration only; they do not
share mutable state, providers, HWNDs, device resources, timers, or runtime ownership.

### Override merge patch

An override applies to the complete declaration before validation:

- absent members preserve inherited members;
- new members are added and existing scalars are replaced;
- objects merge recursively;
- arrays replace inherited arrays in full; and
- `null` removes an inherited member.

The result MUST contain one valid `plugin` and settings valid under that plugin's schema. An override MAY replace the
plugin; the result is then validated only against the replacement plugin.

## Pages and responsive layout

### Pages

- `pages` contains 1–16 ordered entries.
- Page order controls startup and swipe navigation.
- `id` is an optional machine ID. RedXe does not generate or persist one when omitted.
- `name` is optional Unicode text of 1–128 characters. When omitted, UI derives `Page N` from array position without
  modifying the file.
- `layout` is optional. A page without it is blank, so `{}` is valid.
- A page contains at most 32 widget appearances, counting every inline widget and reference independently.

### Layout tree

A nonblank page root `layout` contains `arrangeAlong` and `areas`.

- `arrangeAlong` is `long-side` or `short-side`.
- `areas` contains child areas in visual order.
- Every child area has integer `sizeRatio` from 1 through 1000.
- A leaf area has `sizeRatio` and `widget`.
- A container area has `sizeRatio`, `arrangeAlong`, and `areas` directly; there is no extra `layout` wrapper.
- A page has at most 127 areas and 8 container levels.
- Empty `areas`, mixed leaf/container content, and unresolved references are invalid.

Sibling ratios divide their parent's extent proportionally. Ratios `2`, `1`, `2` receive 40%, 20%, and 40%. RedXe
uses shared integer edges so adjacent regions have no gaps or overlaps and the last sibling reaches the exact boundary.

### Orientation

- Orientation MUST NOT appear in settings.
- The long side is horizontal when client width is greater than or equal to height and vertical otherwise.
- `long-side` arranges left-to-right in landscape and top-to-bottom in portrait.
- `short-side` arranges top-to-bottom in landscape and left-to-right in portrait.
- Resize and orientation change recompute and cache the active layout without reparsing settings.
- The tree always partitions the client rectangle; overlap and out-of-bounds placement are impossible by construction.

## Touch navigation

- Horizontal swipe is used in landscape and portrait.
- Swipe left advances; swipe right returns.
- `wrapPages` controls end behavior.
- Pages follow the finger and settle to the adjacent page or return on release.
- The host owns page navigation. A widget that captured an interactive pointer sequence retains it; otherwise a
  horizontal pan crossing the host threshold becomes page navigation.
- Only the current and adjacent transition pages may own staged runtime instances during a swipe. After settling, the
  inactive page is torn down. Other inactive pages own no providers, widgets, HWNDs, D3D resources, timers, or work.

## Versioned plugin settings contracts

Every settings-visible plugin MUST publish one static contract before provider or widget creation:

- stable plugin ID selecting one widget kind;
- independent schema version with required major and optional minor, where omitted minor means zero;
- bounded JSON Schema Draft 2020-12 for its `settings` object; and
- bounded default settings object.

The schema and defaults are borrowed immutable module data valid while the DLL is mapped. The contract is available
through static discovery without creating a provider or widget, following RedSalamander's proven
`RedSalamanderGetConfigurationSchema` pattern.

Plugin compatibility follows the host major/minor model. RedXe rejects malformed contracts, invalid defaults,
duplicate IDs, unsupported majors, and effective settings that fail the selected plugin schema. Defaults MUST validate
against the exact published schema.

RedXe validates the host structure first, resolves every declaration/reference/override, loads each unique referenced
module at most once for static discovery, and validates every effective widget on every page. Inactive pages create no
providers, widget instances, HWNDs, D3D resources, timers, or frame work. Mapped modules remain mapped under v1 policy.

Schemas MAY contain bounded `x-ui-*` annotations for localized labels, descriptions, grouping, ordering, and editor
hints. UI annotations do not alter runtime validation. Secrets MUST NOT be stored in widget settings or defaults.

The installed `RedXe.settings.schema.json` describes the host and bundled plugin settings. A future UI also consumes
live contracts for installed third-party plugins and preserves compatible unknown-minor fields.

## Loading, recovery, and live reload

### Selection and cold recovery

- No settings argument selects the build-specific default file.
- `--settings <path>` selects a dedicated file.
- A missing, unreadable, or invalid command-line file is never modified. RedXe shows a clear error and runs with the
  default configuration.
- A missing default file causes RedXe to install and load a fresh version 4 default.
- An invalid or incompatible default is preserved exactly, then a fresh editable default is installed and loaded.
- The backup keeps the base name, adds `.invalid-`, and appends UTC `YYYY-MM-DD_HH-MM-SSZ` before `.json`, for example
  `RedXe-1.0.settings.invalid-2026-08-31_14-30-12Z.json`.
- RedXe explains recovery and the backup location. Copies and replacements remain atomic and write-through.

### Live reload

- RedXe event-monitors the selected default or command-line file without polling.
- A valid change applies transactionally. Every later actual change is reconsidered.
- Invalid, unreadable, or missing live input leaves the file untouched and preserves the exact last-valid settings and
  dashboard.
- Diagnostics include every reliable location, such as line, column, and JSON path, in clear user language.
- Errors appear in at most one modal dialog.
- While visible, monitoring continues. A new invalid save refreshes or replaces the one modal with the newest bounded
  diagnostic list without stacking dialogs.
- If the file becomes valid while the modal is visible, RedXe applies it and closes the modal automatically.
- The modal shows as many errors as fit legibly. An ellipsis states when more errors were omitted.
- After dismissal, the same rejected version is not reported again. A later distinct invalid change may show a modal.

## Resource bounds

- Source file: at most 1 MiB.
- Pages: 1–16; widget appearances: at most 32 per page and 512 per document.
- Declarations: at most 128; unique referenced plugin IDs: at most 64.
- Layout: at most 127 areas per page and 8 container levels.
- Declaration/page names: at most 128 Unicode characters.
- Each compact authored settings object, override, effective settings object, plugin schema, and defaults object: at
  most 4096 bytes.
- Live reload holds at most the authoritative document and one candidate. Full documents and schemas use bounded heap
  storage, never large UI-thread stack values.
- Parsing, schema discovery, merge resolution, validation, and layout compilation are cold load/change work.
- Active and swipe-neighbor geometry is cached; steady-state rendering remains allocation-free.
- Watchers block on events and generate zero periodic wake-ups while unchanged.
- Any capacity, discovery, validation, merge, or layout failure rejects the complete candidate without partial publish.

## Shipped defaults

- Both templates use version 4.0 and include `$schema`.
- Debug demonstrates declarations, inline widgets, references, overrides, nested adaptive areas, multiple pages, and
  `wrapPages: false` with bundled plugins.
- Release has one page with one full-display Matrix Rain widget and omits optional fields without meaning.
- Both work in landscape and portrait without orientation fields.

## Required validation

### Document and recovery

- Parse both templates with comments/trailing commas and reject other JSON5 forms.
- Reject missing/malformed versions, unsupported majors, duplicate members, exact-version unknown members, wrong
  types, excessive lengths/counts/depths/sizes, and oversized files.
- Prove older-minor defaults, newer-minor unknown acceptance, newer-major rejection, and UI preservation fixtures.
- Validate missing/invalid default recovery, exact backup bytes/name, fresh-default installation, and external-file
  fallback without mutation.

### Declarations and plugins

- Reject empty/duplicate names, unresolved references, malformed forms, and excessive declarations.
- Cover scalar replacement, recursive object merge, array replacement, `null` removal, plugin replacement, and
  independent instances.
- Validate every plugin contract, version, schema, defaults, unique ID, size bound, and every effective widget.
- Prove one DLL may expose multiple settings-visible plugin IDs and static discovery creates no runtime resources.

### Layout, touch, and runtime

- Reject invalid ratios, empty/mixed areas, excessive depth/areas/widgets, and malformed pages.
- Prove exact no-gap/no-overlap partitioning in both orientations, including non-divisible dimensions.
- Prove cached dynamic reflow and no frame-path allocation.
- Cover swipe direction, direct manipulation, cancel/commit, end stops, wrap, pointer capture, and two-page lifetime.
- Prove inactive pages own no runtime widget resources.

### Reload, resources, and platforms

- Prove default/external monitoring, invalid-state preservation, correction, one-modal behavior, newest diagnostics,
  automatic close on valid input, and rejected-stamp deduplication.
- Keep idle wake-ups at zero and steady-state rendering allocation-free.
- Run Debug and Release x64 tests, hidden WARP host/plugin tests, Release ARM64 compilation, interactive orientation and
  touch checks, `./format.ps1`, `./validate-skills.ps1`, and `git diff --check`.

## Implementation plan

1. [x] Add a versioned static plugin-settings contract without changing frozen COM vtables or IIDs in place.
2. [x] Update bundled plugins to publish one settings-visible ID, schema, and defaults.
3. [x] Replace the canonical host schema and templates with version 4.
4. [x] Implement bounded parsing, major/minor compatibility, declarations, merge patches, plugin validation, and diagnostic
   locations.
5. [x] Replace fixed grid rectangles with compiled adaptive layout trees and cached orientation reflow.
6. [x] Add horizontal touch navigation, adjacent-page staging, and pointer-capture arbitration.
7. [x] Add `--settings`, external monitoring, recovery, and the live modal state machine.
8. [x] Reconcile every normative spec, developer document, test, and hidden harness.
9. [x] Run Debug and Release x64 validation, Release ARM64 compilation, formatting, skill validation, and diff checks.
10. [x] Move this RFC to Done after code, tests, templates, schema, and normative contracts agree.

## Reference basis

- `Z:/src/RedSalamander/docs/SettingsFile.md` verifies cold preservation/default restoration and last-valid live
  behavior.
- RedSalamander's static configuration-schema export verifies schema discovery without a live plugin instance.
- RedXe retains its own XENEON dashboard, ABI, resource, and orientation requirements.
