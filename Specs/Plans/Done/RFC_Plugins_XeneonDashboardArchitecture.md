# RFC: Remaining plugin-dashboard architecture decisions

Status: `DECISION` — non-normative unresolved work
Created: 2026-08-30
Last narrowed: 2026-09-01
Owner: future RedXe plugin services and interactive content

## Purpose

Track only plugin-dashboard decisions that are not implemented or normative. Shipped behavior is owned by:

- `Specs/Plugins/Plugins_API.md` for factories, identifiers, widgets, GPU/window mechanisms, and plugin lifetime;
- `Specs/Core/Core_Settings.md` and `Specs/Settings.schema.json` for versioned plugin settings and page configuration;
- `Specs/UI/UI_Dashboard.md` for pages, adaptive layout, orientation reflow, and swipe composition;
- `Specs/Core/Core_PerformanceAndResources.md` for batching, wake-up, memory, and hot-path constraints; and
- `Specs/UI/UI_XeneonDisplayWindowing.md` for top-level and child-window behavior.

This RFC is not an alternate contract for those shipped systems.

## Settled and removed from this RFC

The current implementation already has a direct native factory, one pre-production generic/GPU/window/scheduled
widget interface set,
bundled DLLs, human-first version 4 settings, adaptive ratio layouts, ordered swipeable pages, current-page-only runtime
resources outside transitions, transactional reload, child-HWND hosting, Matrix Rain, explicit frame invalidation,
and hidden WARP/plugin harnesses.
Changes to those behaviors start in their domain contracts, not here.

## Unresolved decisions

### 1. Data services beyond local pull sources

The baseline local system-data slice is complete in
[`SystemDataPlugin_2026-08-31.md`](../Done/SystemDataPlugin_2026-08-31.md). Plugin-side pull sources, host provider
lookup, the shared widget/data module loader, one event-blocked local acquisition worker, borrowed sink/subscription
delivery, multiple-viewer support, row-cap resource measurements, and closeout validation are implemented. Broader
local metrics, host batching, provider isolation, and future push/network-provider policy remain unresolved.

### 2. Host-owned primitive batching

`IRedXeGpuWidget` remains the immediate-context mechanism. The bounded Studio Clock and Desk Clock measurements
accepted one two-draw dot clock and one three/four-draw split-flap clock without a host-owned primitive IID; that
decision is normative in the plugin and resource contracts. Before shipping a materially larger family of cheap
gauges, clocks, text, or graphs, measure aggregate callback/map/state/draw cost and decide whether the current
pre-production interface set should add a bounded host-owned primitive mechanism. Do not add plugin-specific commands
to the generic widget root.

### 3. Missing plugins and migration beyond version 4 reset

Decide placeholder behavior for configured but unavailable plugins, plugin/schema version migration, and user-visible
diagnostics beyond the approved preserve-and-reset behavior. Settings syntax, versioned plugin schemas, validation,
and the future settings-UI data model are owned by `Specs/Core/Core_Settings.md`.

### 4. Interactive native-window content

Before a bundled WebView or other networked child ships, define navigation, origin, download, permission, focus,
accessibility, process, memory, and quiescence policy. The existing `IRedXeWindowWidget` lifetime and child-container
boundaries remain unchanged.

## Decision gates

Each selected slice requires its own dated WIP implementation plan, updates to every owning normative contract, code
and automated tests, resource measurements appropriate to the slice, Debug/Release x64 validation, Release ARM64
compilation, and repository skill validation. Once no unresolved section remains, this RFC MUST move to `Done`.
