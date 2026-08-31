# RFC: Remaining plugin-dashboard architecture decisions

Status: `DECISION` — non-normative unresolved work
Created: 2026-08-30
Last narrowed: 2026-08-31
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

The current implementation already has a direct native factory, frozen generic/GPU/window widget IIDs, three bundled
DLLs, human-first version 4 settings, adaptive ratio layouts, ordered swipeable pages, current-page-only runtime
resources outside transitions, transactional reload, child-HWND hosting, Matrix Rain, explicit frame invalidation,
and hidden WARP/plugin harnesses.
Changes to those behaviors start in their domain contracts, not here.

## Unresolved decisions

### 1. Data providers and broker

The local system-data slice is now executing in
[`SystemDataPlugin_2026-08-31.md`](SystemDataPlugin_2026-08-31.md). That plan selects a host-worker pull model,
immutable typed table descriptors, borrowed bounded snapshots, timestamps and per-value quality, stop/drain,
coalescing, cadence, privacy, and failure isolation. Keep this RFC section open until the broker and widget-consumer
contract are implemented and the plan closes; network and push-provider policy remain future decisions.

### 2. Host-owned primitive batching

`IRedXeGpuWidget` remains the Matrix-class immediate-context mechanism. Before shipping a family of cheap gauges,
clocks, text, or graphs, measure the expected callback/map/state/draw cost and decide whether a new IID should submit
bounded host-owned primitives for one batched render pass. Do not change the existing IID or add plugin-specific
commands to `Widget.h`.

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
