# RedXe dashboard pages and grid contract

Status: current normative product contract
Last reviewed: 2026-08-31
Owner: `DashboardHost` placement, active-page composition, and widget container layout

## Scope

This specification owns dashboard pages, the design grid, widget instance placement, active-page selection, and the
conversion from configured grid cells to GPU viewports and native child-window bounds. Settings syntax and recovery
belong to `Specs/Core/Core_Settings.md`; plugin identities and rendering mechanisms belong to
`Specs/Plugins/Plugins_API.md`; top-level display and DPI policy belong to `Specs/UI/UI_XeneonDisplayWindowing.md`.

The terms **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative.

## Design canvas and grid

- The dashboard design canvas is 2560×720 logical units.
- Settings MUST declare one dashboard grid with 1–64 columns and 1–64 rows. The shipped XENEON templates MUST use a
  32-column by 9-row grid, producing 80×80 logical cells.
- Widget placement uses zero-based `column` and `row` plus positive `columnSpan` and `rowSpan` values.
- Every placement MUST remain completely inside the declared grid. Placements MUST NOT overlap within one page.
- A page MAY leave cells unused. The host MUST NOT invent margins, gaps, or automatic placement that is absent from
  the settings document.
- Widget array order is stable composition order. The current non-overlap rule avoids ambiguous interleaving between
  the GPU surface and native-window containers.

`DashboardHost` MUST cache each active widget's design-canvas rectangle during initialization. Given grid dimensions
`C×R` and placement `(x, y, w, h)`, the logical rectangle is:

```text
left   = 2560 * x       / C
top    =  720 * y       / R
right  = 2560 * (x + w) / C
bottom =  720 * (y + h) / R
```

Physical bounds MUST be derived from the cached logical edges and current client dimensions so adjacent widgets share
the same rounded edge and a full-grid widget covers the entire client area.

## Pages

- Settings MUST contain between 1 and 8 ordered pages. Page IDs are valid machine IDs and unique
  case-insensitively; page names are non-empty UTF-8 text of at most 128 bytes.
- `activePageId` MUST resolve exactly one page.
- Every page MUST contain between 1 and 16 widget instances. Widget instance IDs are valid machine IDs and unique
  case-insensitively across the complete document.
- The runtime MUST instantiate only the active page. Inactive pages remain validated typed settings and consume no
  plugin providers, widgets, child HWNDs, D3D resources, timers, or frame work.
- Changing `activePageId` through live settings reload MUST tear down the prior dashboard in the normal renderer-first
  order, stage the selected page, and publish it transactionally. Failure restores the previous page.
- An edit confined to inactive-page content or labels MUST update typed settings without rebuilding the unchanged
  active page.
- Page selection does not load a disabled plugin and does not hot-unload an already mapped plugin module.

## Widget instances

Each widget record MUST contain exactly `id`, `pluginId`, `typeId`, `placement`, and `private`.

- `pluginId` MUST resolve a unique enabled plugin record.
- `typeId` identifies a widget type within that plugin. The current executable supports the three bundled
  plugin/type pairs; unsupported active content fails before replacing a live dashboard.
- `private` is a JSON object owned by that widget instance. The host validates its bounded JSON representation but
  does not add plugin-specific fields to the host-owned placement record.
- GPU viewports and native-window child containers MUST use the same cached placement transform.
- Resize and DPI changes recompute physical bounds only; they MUST NOT reparse settings or allocate frame-path layout
  storage.

## Required validation

- Parser and schema tests MUST reject missing or duplicate page fields, duplicate page or instance IDs, invalid
  active-page references, empty or oversized page lists, invalid grid dimensions, zero spans, out-of-grid placements,
  and overlap within a page.
- Shipped Debug settings MUST place two triangles, GDI Orbit, and Matrix Rain in four adjacent 8×9 grid regions on
  its active 32×9 page. Shipped Release settings MUST place one Matrix Rain instance across 32×9 cells.
- `HostPluginTests` MUST verify configured logical placements, active-page-only instantiation, page switching through
  reconfiguration, native-container layout, and WARP rendering after a page change. A non-divisible client/grid width
  case MUST prove that adjacent native and GPU regions use one shared rounded physical edge without a gap or overlap.
- Debug and Release x64 tests plus Release ARM64 compilation MUST pass.

## Implementation anchors

- Typed pages, grid, placement, and private settings: `RedXe/Settings.*`
- Active-page plugin and widget creation: `RedXe/PluginManager.*`
- Cached logical and physical layout: `RedXe/DashboardHost.*`
- Parser/schema validation: `Tests/SettingsTests/`
- Production host validation: `Tests/HostPluginTests/`
