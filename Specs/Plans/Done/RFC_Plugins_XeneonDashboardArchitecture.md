# RFC: Remaining plugin-dashboard architecture decisions

Status: `DONE` — settled architecture merged; remaining closeout transferred
Created: 2026-08-30
Completed: 2026-09-02
Last narrowed: 2026-09-02
Owner: future RedXe plugin services and interactive content

## Purpose

This RFC was the non-normative tracker for plugin-dashboard decisions that were not yet implemented. Shipped behavior
is owned by:

- `Specs/Plugins/Plugins_API.md` for factories, identifiers, widgets, GPU/window mechanisms, data sources, and plugin
  lifetime;
- `Specs/Core/Core_Settings.md` and `Specs/Settings.schema.json` for versioned plugin settings and page configuration;
- `Specs/UI/UI_Dashboard.md` for pages, adaptive layout, orientation reflow, and swipe composition;
- `Specs/Core/Core_PerformanceAndResources.md` for batching, wake-up, memory, and hot-path constraints; and
- `Specs/UI/UI_XeneonDisplayWindowing.md` for top-level and child-window behavior.

This file is historical. It is not an alternate contract for those shipped systems.

Remaining closeout lives in
[`../WIP/PluginDashboardRemainingCloseout_2026-09-02.md`](../WIP/PluginDashboardRemainingCloseout_2026-09-02.md).

## Settled by this RFC

The current implementation already has a direct native factory, one pre-production generic/GPU/window/scheduled
widget interface set, bundled DLLs, human-first version 4 settings, adaptive ratio layouts, ordered swipeable pages,
current-page-only runtime resources outside transitions, transactional reload, child-HWND hosting, Matrix Rain,
Studio Clock, Desk Clock, explicit frame invalidation, and hidden WARP/plugin harnesses.

Local pull data services are complete:

- [`SystemDataPlugin_2026-08-31.md`](SystemDataPlugin_2026-08-31.md) — plugin-side pull sources, host provider lookup,
  shared widget/data module loader, one event-blocked local acquisition worker, borrowed sink/subscription delivery,
  Process Viewer, and baseline measurements.
- [`SystemDataMetricsExpansion_2026-09-01.md`](SystemDataMetricsExpansion_2026-09-01.md) — 18 independently
  subscribable local datasets and host `CollectSnapshots` batching.

`IRedXeGpuWidget` remains the immediate-context mechanism. Bounded Studio Clock and Desk Clock measurements accepted
one two-draw dot clock and one three/four-draw split-flap clock without a host-owned primitive IID; that decision is
normative in the plugin and resource contracts.

Changes to those behaviors start in their domain contracts, not here.

## Remaining work transferred

The following gates were still open when this RFC closed. They are recorded in the remaining-closeout plan rather
than kept as an active RFC:

1. Provider isolation and future push/network-provider policy.
2. Host-owned primitive batching, only after measuring a materially larger cheap-widget family.
3. Missing-plugin placeholders, schema migration beyond version 4 reset, and user-visible diagnostics.
4. Interactive native-window / WebView navigation, origin, download, permission, focus, accessibility, process,
   memory, and quiescence policy.
5. System Data follow-on hosts: live laptop battery IOCTL, dedicated WARP-only VM, live NIC/disk churn, live ARM64
   runtime, and optional hybrid-CPU mapping.

Each selected slice still requires its own dated WIP implementation plan, updates to every owning normative contract,
code and automated tests, resource measurements appropriate to the slice, Debug/Release x64 validation, Release ARM64
compilation, and repository skill validation.
