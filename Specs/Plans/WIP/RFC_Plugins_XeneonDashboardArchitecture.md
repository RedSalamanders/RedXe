# RFC: Remaining plugin-dashboard architecture decisions

Status: `DECISION` — non-normative unresolved work
Created: 2026-08-30
Last narrowed: 2026-08-31
Owner: future RedXe plugin services and dashboard configuration

## Purpose

Track only plugin-dashboard decisions that are not implemented or normative. Shipped behavior is owned by:

- `Specs/Plugins/Plugins_API.md` for factories, identifiers, widgets, GPU/window mechanisms, and plugin lifetime;
- `Specs/Core/Core_Settings.md` and `Specs/Settings.schema.json` for normalized plugin/page/instance configuration;
- `Specs/UI/UI_Dashboard.md` for pages, grid placement, and active composition;
- `Specs/Core/Core_PerformanceAndResources.md` for batching, wake-up, memory, and hot-path constraints; and
- `Specs/UI/UI_XeneonDisplayWindowing.md` for top-level and child-window behavior.

This RFC is not an alternate contract for those shipped systems.

## Settled and removed from this RFC

The current implementation already has a direct native factory, frozen generic/GPU/window widget IIDs, three bundled
DLLs, normalized version 3 settings, a 32×9 grid, multiple persisted pages, active-page-only runtime resources,
transactional reload, child-HWND hosting, Matrix Rain, explicit frame invalidation, and hidden WARP/plugin harnesses.
Changes to those behaviors start in their domain contracts, not here.

## Unresolved decisions

### 1. Data providers and broker

Before adding sensor or weather plugins, decide the immutable channel descriptor, typed sample, timestamp/quality,
worker callback, stop/drain, bounded history, and coalescing contracts. The design must specify acquisition cadence,
threading, failure isolation, secrets/network policy, and how a data update invalidates only affected static widgets.

### 2. Host-owned primitive batching

`IRedXeGpuWidget` remains the Matrix-class immediate-context mechanism. Before shipping a family of cheap gauges,
clocks, text, or graphs, measure the expected callback/map/state/draw cost and decide whether a new IID should submit
bounded host-owned primitives for one batched render pass. Do not change the existing IID or add plugin-specific
commands to `Widget.h`.

### 3. Configuration UI and schema discovery

Decide how plugins publish bounded schemas and localized labels, how RedXe renders settings pages, and how edits are
validated and saved atomically. The design must preserve the normalized plugin registry, dashboard pages, placement,
and per-instance `private` object. Secrets must never be stored inline in the ordinary JSON document.

### 4. Missing plugins and migration

Decide placeholder behavior for configured but unavailable plugins, plugin/schema version migration, and user-visible
diagnostics. Current strict behavior rejects unsupported active content and remains authoritative until replaced in
the owning specs.

### 5. Interactive native-window content

Before a bundled WebView or other networked child ships, define navigation, origin, download, permission, focus,
accessibility, process, memory, and quiescence policy. The existing `IRedXeWindowWidget` lifetime and child-container
boundaries remain unchanged.

## Decision gates

Each selected slice requires its own dated WIP implementation plan, updates to every owning normative contract, code
and automated tests, resource measurements appropriate to the slice, Debug/Release x64 validation, Release ARM64
compilation, and repository skill validation. Once no unresolved section remains, this RFC MUST move to `Done`.
