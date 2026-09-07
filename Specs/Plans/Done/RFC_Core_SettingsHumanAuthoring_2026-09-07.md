# RFC: Major 5 human-authored settings (one-way reset)

Status: COMPLETE (2026-09-07) — major 5 human authoring shipped
Created: 2026-09-07
Last reviewed: 2026-09-07

This file is historical sequencing. Current shipped behavior lives in
[`Specs/Core/Core_Settings.md`](../../Core/Core_Settings.md), [`Specs/UI/UI_Dashboard.md`](../../UI/UI_Dashboard.md),
[`Specs/Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md), and [`Specs/Settings.schema.json`](../../Settings.schema.json).
Do not edit this plan to change current requirements.

## Purpose

Humans already edit one JSON document in an external editor. Version 4 is powerful, live-reloaded, and strict, but the
authored shape is a nested adaptive tree (`layout` / `areas` / `arrangeAlong` / `sizeRatio`) plus a nested `settings`
bag and `{ "use", "override": { "settings": ... } }`.

This RFC proposes **settings major 5**: a flatter human document that the host compiles to the same typed runtime
layout tree. Version 4 user files are an **incompatible major**. There is no parse-time dual-shape and no conversion of
v4 trees. Cold load of a default v4 file uses the existing Core_Settings recovery: backup, then install the v5
template.

## Problem

The shipped v4 document is the machine layout tree plus optional `declare` names. Every nonblank page repeats
`layout` / `arrangeAlong` / `areas` / `sizeRatio`, even for a single full-bleed tile or an equal-share row. Plugin
configuration sits one level down in `settings`. Unknown members reject the candidate. Live reload is already strict
about not rewriting a valid editor save.

Pain that shows up in the shipped templates (`Settings/RedXe.settings.json`, `Settings/RedXe-debug.settings.json`):

- A one-widget page is four nested objects.
- Unequal columns with a stacked pair require a container area whose only job is `arrangeAlong: "short-side"`.
- `{ "use", "override": { "settings": { ... } } }` is correct merge syntax and still a lot of keys for one tint.
- Machine page `id` values and explicit `sizeRatio: 1` everywhere add noise without changing the layout.
- Closed plugin objects and exact types are right for safety; they make typos expensive unless diagnostics name the
  authored JSON path.

### Current JSON (one widget)

```json
{
  "name": "Matrix Focus",
  "layout": {
    "arrangeAlong": "long-side",
    "areas": [
      { "sizeRatio": 1, "widget": "Matrix" }
    ]
  }
}
```

### Intended JSON (same page)

```json
{ "name": "Matrix Focus", "widgets": ["Matrix"] }
```

### Current JSON (unequal columns, stacked pair)

```json
{
  "name": "Plugin Gallery",
  "layout": {
    "arrangeAlong": "long-side",
    "areas": [
      {
        "sizeRatio": 2,
        "arrangeAlong": "short-side",
        "areas": [
          { "sizeRatio": 1, "widget": "Triangle" },
          { "sizeRatio": 1, "widget": "Orbit" }
        ]
      },
      {
        "sizeRatio": 5,
        "widget": {
          "use": "Matrix",
          "override": {
            "settings": { "seed": 4242, "densityPercent": 55 }
          }
        }
      }
    ]
  }
}
```

### Intended JSON (same page)

```json
{
  "name": "Plugin Gallery",
  "columns": [
    { "weight": 2, "rows": ["Triangle", "Orbit"] },
    {
      "weight": 5,
      "widget": { "use": "Matrix", "seed": 4242, "densityPercent": 55 }
    }
  ]
}
```

### Current JSON (inline plugin settings)

```json
{ "plugin": "builtin.launcher", "settings": { "shortcuts": [], "iconSize": "huge" } }
```

### Intended JSON (flattened plugin object)

```json
{ "plugin": "builtin.launcher", "shortcuts": [], "iconSize": "huge" }
```

## Goals

- Make the common case (one page, a few tiles, equal share) authorable in about 5–10 lines.
- Keep one JSON document. Comments and trailing commas stay if the host still accepts them (current syntax).
- Compile to the current typed model: same split paths, ratio partition, bounds (16 pages, 32 widgets/page, 127 areas,
  8 container levels, 4096-byte settings objects, 1 MiB source). The user document has **no** `layout` / `areas` /
  `arrangeAlong` / `sizeRatio`.
- Keep live reload stable: a valid load MUST NOT write, format, or persist-merge onto the watched file. Invalid input
  keeps last-good and reports a specific JSON path plus reason (and line/column when known).
- Keep validation strict: unknown members, wrong types, unresolved names, mixed page shapes, and mixed leaf/container
  shapes still reject the complete candidate.
- Keep persist merge: `PersistWidgetSettings` still merges into the stored instance object, validates the plugin
  schema, and writes only on an explicit save path. Prefer patching flattened keys in place.
- Treat version 4 as an incompatible major with the existing cold-recovery policy. Do not convert v4 trees.

## Non-goals

- A GUI settings editor. `IRedXeSettingsEditor` does not exist and this RFC does not invent one.
- YAML, JSON5-beyond-comments-and-trailing-commas, or any second on-disk format.
- Loosening the 1 MiB cap, 4096-byte settings cap, array bounds, or closed plugin objects.
- Silently accepting unknown plugin members.
- Breaking persist merge or collect-on-exit.
- Migrating version 3. Version 3 remains a one-way invalid-default reset.
- Converting version 4 documents into version 5. Version 4 is incompatible; recovery installs the v5 template (default
  path) or runs the in-memory deployed v5 default (`--settings` portable) without rewriting the portable file.
- Dual-shape parse: a v5 file that also contains canonical v4 `layout` / `areas` / `override` / nested `settings` is
  invalid.
- Rewriting the editor file on load, including “pretty-print sugar” as a load side effect.
- Teaching short aliases such as `"Launcher"` as a plugin ID. Declare names stay user-owned; catalog IDs stay
  `builtin.*`.
- Compatibility with a v5-unaware 4.0 binary after the user has been reset to major 5. That older binary is not a
  supported reader of the new document.

## Preferred design: major 5 one-way reset

Bump the document to **major 5, minor 0** (omitted minor is 0). The parser accepts only the human page/widget syntax
below. After compile, layout math, plugin discovery, and `AppSettings` are unchanged. Typed runtime still uses the
adaptive tree internally; that tree is not a user-document shape.

This is the former Alternative A. Dual-shape v4 parse-time sugar is a rejected alternative (see below).

A new RedXe MUST treat `version.major: 4` as an **incompatible major**, not as “ignore unknown members.” Exact-version
unknown members already reject; a different major MUST follow Core_Settings cold recovery rather than attempt a
best-effort parse of leftover `layout` keys.

### Root

Unchanged members:

| Member | Required | Contract |
| --- | --- | --- |
| `$schema` | No | When present, `RedXe.settings.schema.json`. |
| `version` | Yes | `{ "major": 5 }` with optional `minor` (omitted is 0). |
| `wrapPages` | No | Omitted or false stops at page ends; true wraps. |
| `logRetentionDays` | No | Integer 1–365. Omitted is 15. |
| `declare` | No | Reusable widget definitions keyed by authored names. |
| `pages` | Yes | One through sixteen ordered pages. |

Unknown root members remain errors.

### Page shapes (exactly one)

A page object may use **one** of:

| Shape | Members | Meaning |
| --- | --- | --- |
| Blank | optional `id`, `name` only | Unchanged. `{}` stays a blank page. |
| Widget list | optional `id`, `name`, optional `along`, required `widgets` | Equal-share split. |
| Columns | optional `id`, `name`, required `columns` | Long-side split; weights optional. |
| Rows | optional `id`, `name`, required `rows` | Short-side split; weights optional. |

There is **no** canonical `layout` page shape in v5.

Mixing `layout` (or `areas` / `arrangeAlong` / `sizeRatio`) with any v5 page is an error. Mixing `columns` with `rows`
at the same page is an error. Mixing `widgets` with `columns` or `rows` is an error. `along` is valid only with
`widgets`. Unknown page members remain errors.

`along` is `long-side` or `short-side`. Omitted `along` on `widgets` is `long-side` (same default as today’s templates).

### List sugar

`widgets` is an array of widget values (below). Each item becomes a leaf with `sizeRatio: 1`. The root `arrangeAlong`
is `along` or `long-side`. Nested splits are not expressed in `widgets`; use `columns` / `rows`.

### Columns and rows

`columns` always compiles to `arrangeAlong: "long-side"`. `rows` always compiles to `arrangeAlong: "short-side"`.

Each item is either a widget value (implied `weight: 1`) or an object:

| Member | Required | Meaning |
| --- | --- | --- |
| `weight` | No | Integer 1–1000; omitted is 1. Maps to internal `sizeRatio`. |
| `widget` | One of | A widget value. Leaf. |
| `rows` | One of | Nested short-side container. Valid inside `columns` (and nested further). |
| `columns` | One of | Nested long-side container. Valid inside `rows` (and nested further). |

Exactly one of `widget`, `rows`, or `columns` in a weighted object. Nested containers exist only when the author needs
a split; equal-share stacks do not require weights.

A nested `rows` / `columns` array uses the same item grammar (widget value or weighted object).

After expansion, existing caps apply. Diagnostics SHOULD cite the authored path (`pages[1].columns[0].rows[1]`), not
only the compiled `layout.areas` path.

### Widget values

A widget value is exactly one of:

1. **Declare name**: `"Matrix"` resolves through `declare` with the current case-sensitive rules.
2. **Catalog id string**: if the string is not a declare name, and it equals a catalogued settings-visible plugin ID
   (`builtin.launcher`), it is an inline `{ "plugin": "<id>" }` with plugin defaults. Otherwise the name is unresolved
   (same class of error as today).
3. **Flattened plugin object**: `{ "plugin": "...", ...pluginKeys }` with **no** `settings` member. All other members
   become the settings object and then validate against that plugin’s closed schema.
4. **Flattened use-object**: `{ "use": "<name>", ...pluginKeys, optional "plugin" }`. There is no `override` bag.
   Extra keys besides `use` and optional `plugin` are a merge-patch onto the declaration’s settings (same merge rules
   as today’s override: absent inherit, objects merge, scalars replace, arrays replace, null removes). `plugin` on a
   use-object swaps the plugin. The result MUST contain one valid plugin and settings object.

Reserved host keys on a widget object: `plugin`, `use`. Drop `override` and nested `settings` from v5 human syntax.

`declare` values are flattened plugin objects: `{ "plugin": "...", ...pluginKeys }` — for example StudioClock
`externalDotsAlwaysOn`, Launcher `shortcuts`, AVControl `profiles`, ProcessViewer `topN`. No nested `settings` in
`declare`.

### Minimal document (target: 5–10 lines)

```json
{
  "$schema": "RedXe.settings.schema.json",
  "version": { "major": 5 },
  "pages": [
    { "name": "Main", "widgets": ["builtin.launcher", "builtin.matrix-rain"] }
  ]
}
```

Omitted `wrapPages`, `logRetentionDays`, `declare`, page `id`, `along`, and `weight` use today’s defaults. Omitted
plugin keys still merge from the plugin contract before validation.

### Compile (implementation-ready; not executed by this RFC)

Cold change-path only. Heap-backed, same 128 KiB reserved parser stack. No render-path work.

1. Parse JSON with the current comment and trailing-comma flags. Reject duplicates, oversize source, and unknown
   **root** members as today (`$schema`, `version`, `wrapPages`, `logRetentionDays`, `declare`, `pages`).
2. Require `version.major === 5`. Any other major is an incompatible-version error (including 4). Do not ignore
   unknown page members of a major-4 document in order to keep running.
3. Parse `declare` as flattened plugin objects.
4. For each page, classify the exclusive v5 shape. Reject `layout` / `areas` / `arrangeAlong` / `sizeRatio`.
5. Expand `widgets` / `columns` / `rows` into the in-memory layout tree (`arrangeAlong`, areas, `sizeRatio`, widget).
   Count expanded areas and depth against the existing caps.
6. Resolve widget values (declare, plugin ID, flattened plugin, flattened use) into the current widget definition,
   then run existing plugin-schema validation on every effective object.
7. Compile split paths exactly as `SettingsV4` does today. `DashboardHost` does not learn the human syntax.

Flattening copies plugin keys into a transient settings object; it MUST NOT allocate on the render path and MUST
honor the 4096-byte compact cap on the resulting object.

### Persist and the editor

- **Load path** never writes. Human syntax stays on disk until a user-driven save. This is already required for any
  valid live load and MUST remain true.
- **Typed model** is always the compiled layout tree. Runtime, WARP tests, and layout math never see `widgets` /
  `columns` as a dashboard primitive.
- **Widget persist** keeps today’s merge and the current two-space formatter (save-path only). It MUST NOT convert an
  entire page from `columns` / `widgets` / `rows` to `layout` merely because one instance saved.
- Persist locates the Nth widget appearance by walking human syntax (`widgets` / `columns` / `rows`) the same way it
  walks `layout` today (`PatchMutableLayout`).
- If the appearance is already a flattened object, persist patches those flattened keys **in place**.
- If the appearance is a bare string, persist MAY replace **that array item** with a flattened `{ "plugin", ...keys }`
  (or `{ "use", ...keys }` when the string was a declare name) so merged settings have an object to patch. That is a
  leaf rewrite, not a page rewrite.
- Nested `{ "plugin", "settings" }` on disk is not the preferred v5 form. Use it only if an implementation cannot
  patch flattened keys; prefer staying flattened in v5 files.
- Pretty-printing the whole document is not a load side effect. If a future save-path pretty-printer is wanted, it is
  a separate decision and still MUST NOT run on load.

Comments are already lost on today’s persist rewrite (`yyjson` mutate + format). This RFC does not try to preserve
comments across persist.

### Compatibility and recovery

Cite [`Specs/Core/Core_Settings.md`](../../Core/Core_Settings.md) **Cold load and recovery** (unchanged mechanism;
v4 becomes the incompatible-major case that already exists for a different major):

- An invalid or incompatible **default** is preserved byte-for-byte beside it, then atomically replaced with a fresh
  template. Backup name: `<stem>.invalid-YYYY-MM-DD_HH-MM-SSZ.json`. The user is told what happened and where the
  backup was written.
- A missing, unreadable, or invalid **command-line** (`--settings`) file is never modified. RedXe reports the problem
  and runs with the deployed default configuration in memory.
- If a deployed default cannot be read or validated, startup fails rather than inventing settings.

No conversion of v4 trees. Shipped v5 templates are the complete JSON in this RFC (same pages, widgets, ratios, and
plugin settings as today’s v4 templates).

| Document | New (v5-aware) RedXe | Older 4.0 RedXe |
| --- | --- | --- |
| Default user file `version.major: 4` | Incompatible major. Backup as `<stem>.invalid-YYYY-MM-DD_HH-MM-SSZ.json`, atomically install the v5 template, tell the user. No v4→v5 rewrite of the tree. | Unchanged v4 parse (user has not yet reset). |
| `--settings` portable `version.major: 4` | Report; run in-memory deployed v5 default; **do not** mutate the portable file (same as today’s invalid portable). | Unchanged v4 parse. |
| Valid v5 human document | Compile, apply in memory, do not rewrite on load. | Different major: existing error path. After the user’s default has been reset to v5, this older binary is not a relevant reader. |
| v5 file that also contains `layout` / `override` / nested `settings` | Reject the complete candidate (one-way; no dual-shape). | Not applicable. |
| Missing default | Atomic install of the v5 template (existing missing-default recovery). | Installs v4 template (current ship). |

A newer minor of major 5 remains additive the way Core_Settings describes same-major minors today. Major 4 is not a
minor of 5.

## Proposed Debug default

Proposal only. Complete intended `Settings/RedXe-debug.settings.json` for major 5. Same three pages, placements,
ratios, and plugin settings as the current Debug template. GDI Orbit stays inline `builtin.gdi-orbit` (not declared).
Comments match the current Debug template style.

```json
{
  "$schema": "RedXe.settings.schema.json",
  "version": { "major": 5, "minor": 0 },
  "wrapPages": false,
  "logRetentionDays": 15,
  // Reusable definitions create independent instances wherever they appear.
  "declare": {
    "Triangle": { "plugin": "builtin.rotating-triangle" },
    "Matrix": { "plugin": "builtin.matrix-rain" },
    "StudioClock": { "plugin": "builtin.studio-clock", "externalDotsAlwaysOn": true },
    "DeskClock": { "plugin": "builtin.desk-clock" },
    "Weather": { "plugin": "builtin.weather" },
    "Launcher": { "plugin": "builtin.launcher", "shortcuts": [] },
    "AVControl": { "plugin": "builtin.av-control", "profiles": [] },
    "Processes": { "plugin": "builtin.process-viewer", "topN": 10 },
    "Pulse": { "plugin": "builtin.system-pulse" },
    "Cpu": { "plugin": "builtin.cpu-meter" },
    "Memory": { "plugin": "builtin.memory-meter" },
    "Network": { "plugin": "builtin.network-meter", "topN": 8 },
    "Storage": { "plugin": "builtin.storage-meter" },
    "Gpu": { "plugin": "builtin.gpu-meter" },
    "GpuProcesses": { "plugin": "builtin.gpu-processes", "topN": 8 },
    "Power": { "plugin": "builtin.power-meter" },
    "Thermal": { "plugin": "builtin.thermal-meter" }
  },
  "pages": [
    {
      "id": "development",
      "name": "Development",
      "columns": [
        "Launcher",
        { "rows": ["Triangle", { "plugin": "builtin.gdi-orbit" }] },
        { "weight": 2, "widget": "Matrix" }
      ]
    },
    {
      "name": "Alternate Gallery",
      "columns": [
        { "weight": 2, "rows": ["Triangle", { "plugin": "builtin.gdi-orbit" }] },
        {
          "weight": 5,
          "widget": { "use": "Matrix", "seed": 2000, "densityPercent": 80 }
        },
        { "weight": 2, "rows": ["StudioClock", "DeskClock"] },
        { "weight": 5, "widget": "Weather" },
        { "weight": 2, "widget": "Launcher" },
        { "weight": 4, "widget": "AVControl" }
      ]
    },
    {
      "name": "System",
      "columns": [
        { "weight": 3, "rows": ["Pulse", "Cpu", "Memory"] },
        { "weight": 4, "rows": ["Processes", "GpuProcesses"] },
        { "weight": 3, "rows": ["Network", "Storage"] },
        { "weight": 3, "rows": ["Gpu", "Thermal", "Power"] }
      ]
    }
  ]
}
```

## Proposed Release default

Shipped `Settings/RedXe.settings.json`. Same three pages and placements as the v4 Release template, except the System
page last column uses Gpu/Thermal/Power row weights 2, 3, 1. Orbit is declared as `builtin.gdi-orbit`.

```json
{
  "$schema": "RedXe.settings.schema.json",
  "version": { "major": 5 },
  "wrapPages": false,
  "logRetentionDays": 15,
  "declare": {
    "Matrix": { "plugin": "builtin.matrix-rain" },
    "Triangle": { "plugin": "builtin.rotating-triangle" },
    "Orbit": { "plugin": "builtin.gdi-orbit" },
    "StudioClock": { "plugin": "builtin.studio-clock", "externalDotsAlwaysOn": true },
    "DeskClock": { "plugin": "builtin.desk-clock" },
    "Weather": { "plugin": "builtin.weather" },
    "Launcher": { "plugin": "builtin.launcher", "shortcuts": [] },
    "AVControl": { "plugin": "builtin.av-control", "profiles": [] },
    "Processes": { "plugin": "builtin.process-viewer", "topN": 10 },
    "Pulse": { "plugin": "builtin.system-pulse" },
    "Cpu": { "plugin": "builtin.cpu-meter" },
    "Memory": { "plugin": "builtin.memory-meter" },
    "Network": { "plugin": "builtin.network-meter", "topN": 8 },
    "Storage": { "plugin": "builtin.storage-meter" },
    "Gpu": { "plugin": "builtin.gpu-meter" },
    "GpuProcesses": { "plugin": "builtin.gpu-processes", "topN": 8 },
    "Power": { "plugin": "builtin.power-meter" },
    "Thermal": { "plugin": "builtin.thermal-meter" }
  },
  "pages": [
    { "name": "Matrix Focus", "widgets": ["Matrix"] },
    {
      "name": "Plugin Gallery",
      "columns": [
        { "weight": 2, "rows": ["Triangle", "Orbit"] },
        {
          "weight": 5,
          "widget": {
            "use": "Matrix",
            "seed": 4242,
            "densityPercent": 55,
            "speedPercent": 75,
            "headColor": "#B8E8FF",
            "trailColor": "#2388D1"
          }
        },
        { "weight": 2, "rows": ["StudioClock", "DeskClock"] },
        { "weight": 5, "widget": "Weather" },
        { "weight": 2, "widget": "Launcher" },
        { "weight": 4, "widget": "AVControl" }
      ]
    },
    {
      "name": "System",
      "columns": [
        { "weight": 3, "rows": ["Pulse", "Cpu", "Memory"] },
        { "weight": 4, "rows": ["Processes", "GpuProcesses"] },
        { "weight": 3, "rows": ["Network", "Storage"] },
        {
          "weight": 3,
          "rows": [
            { "weight": 2, "widget": "Gpu" },
            { "weight": 3, "widget": "Thermal" },
            { "weight": 1, "widget": "Power" }
          ]
        }
      ]
    }
  ]
}
```

## Alternatives (not preferred)

### Dual-shape v4 parse-time sugar (rejected)

Stay on major 4 and accept both `layout` and `widgets` / `columns` / `rows` in the same schema. Cost: dual-shape
validation, two persist walks, and a temptation to bump only `version.minor` (an older 4.0 binary that ignored unknown
fields could render a blank page). The user chose a one-way major-5 reset for a simpler schema. Do not keep dual-shape
as a compatibility bridge.

### Save-path sugar printer

Accept canonical files, rewrite them to `columns` / `widgets` whenever RedXe saves. Humans would see the short form
after the first persist. Cost: fights the editor (load already must not rewrite; persist would still reshape
unrelated layout), loses comments, and complicates stamp/dedup. Rejected as the default. Load never rewrites.

YAML and a GUI editor remain non-goals, not alternatives in this RFC.

## Affected specs at closeout

When this is implemented and accepted, merge durable behavior into:

- `Specs/Core/Core_Settings.md` — major 5 document, human page/widget syntax, compile-to-layout, persist-in-place
  rules, incompatible-major recovery for v4, diagnostics paths
- `Specs/UI/UI_Dashboard.md` — layout authoring: human syntax compiles to the same adaptive tree; orientation and
  ratios unchanged
- `Specs/Settings.schema.json` — v5 page `oneOf` (`widgets` / `columns` / `rows` / blank); flattened widget/`declare`
  objects; `additionalProperties: false` retained. Do not keep a v4 `layout` dual-shape.
- `RedXe/SettingsV4.cpp` (or successor) and persist walk in `RedXe/Settings.cpp`
- `Tests/SettingsTests/` — v5 parse, v4 default backup + v5 template, portable v4 non-mutation, mixing `layout`
  rejects, persist-in-place, no load rewrite
- `docs/usage.md` — settings section; plugin pages only if authored examples change
- `Settings/RedXe-debug.settings.json` and `Settings/RedXe.settings.json` — replace with the JSON in this RFC

`Specs/Plugins/Plugins_API.md` stays the ABI/persist contract (`CollectPersistentSettings`, closed objects). It needs
a sentence only if flattened authoring is described as user-document syntax rather than host compile.

## Open decisions

Removed: **v4 sugar vs v5 reset** — decided: major 5 one-way reset.

Removed: **whether shipped templates switch to the new syntax** — decided: the v5 templates **are** the JSON in this
RFC (same compositions as today).

Flattened human syntax including use-objects is **preferred and in this design** (`{ "use": "Matrix", "seed": 2000 }`,
not `override.settings`). Remaining questions were settled at implementation:

1. **Flatten vs nested `{ plugin, settings }` on persist.** Stay flattened in v5 files. Parse rejects nested
   `settings` / `override`.
2. **Whether persist may rewrite a bare string leaf** into a flattened inline object. Yes, that one item only.
3. **Equal-share default axis.** `widgets` without `along` uses `long-side`.
4. **Plugin ID strings in `widgets`.** Yes, after declare resolution. No human alias table.

## Validation checklist (future implementation)

Not executed by this RFC.

- A default file with `version.major: 4` is an incompatible major: byte-for-byte backup named
  `<stem>.invalid-YYYY-MM-DD_HH-MM-SSZ.json`, then atomic install of the v5 template shown in this RFC. No conversion
  of the v4 tree.
- A `--settings` portable v4 file is reported, left byte-for-byte unchanged, and the process runs the in-memory
  deployed v5 default.
- The proposed Debug and Release JSON in this RFC are the shipped v5 templates: same pages, widgets, ratios, and
  plugin settings as today’s templates; every catalogued settings-visible plugin is placed.
- Mixing `layout` (or `areas` / `arrangeAlong` / `sizeRatio`) into a v5 page rejects with a path on the page, not
  `path $`. `{ "use", "override": ... }` and nested `settings` in a v5 widget/`declare` object reject.
- Minimal v5 document (5–10 lines, two plugin-ID widgets) compiles to equal-share `long-side` leaves with ratio 1.
- Gallery-shaped `columns` + nested `rows` matches the typed tree of the current gallery `layout`.
- Unresolved `"Launcher"` without `declare` rejects; `"builtin.launcher"` without `declare` succeeds.
- Flattened unknown plugin member rejects; flattened valid `shortcuts` matches today’s canonical `settings.shortcuts`.
- `{ plugin, settings, shortcuts }` (mixed flatten) rejects.
- Nested human syntax exceeding 8 container levels or 127 areas rejects with the authored path.
- `TryLoadChanged` of a valid v5 file does not mutate those bytes. Invalid input does not rewrite the file or the
  last-good document.
- Widget persist on a `widgets` / `columns` page patches that appearance and leaves sibling human syntax intact; a
  bare string leaf may become a flattened inline object; the page does not become `layout`.
- Persist still rejects unknown plugin members and still merges unspecified members.
- Comments and trailing commas still parse on load. Formatter remains save-path only.
- `AppSettings` stays ≤ 64 KiB; compile stays on the change path; no render-path allocation.
- Debug and Release x64 tests, Release ARM64 compilation, and `.\validate-skills.ps1` pass after the implementation
  change (not required for this plan-only RFC).

## Implementation

Shipped 2026-09-07. `ParseAppSettingsJsonV5` in `RedXe/SettingsV4.cpp` reads major 5 only; `ParseAppSettingsJsonV4`
rejects. Templates, schema, Core_Settings, UI_Dashboard, Plugins_API, docs, settings-store skill, and SettingsTests
match the checklist above. Release System last column is Gpu/Thermal/Power weights 2/3/1. Debug keeps equal-share
System row weights. Load never writes. Persist patches flattened keys in place.
