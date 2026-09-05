# Plugin-dashboard remaining closeout

Status: `ACTIVE` — remaining host validation and unresolved architecture gates after the local metrics catalog
Created: 2026-09-02
Owner: plugin services, local system acquisition, settings migration, and interactive window content

## Goal

Finish the leftover closeout that was not required to ship the 18-dataset local catalog or to retire the architecture
RFC. Current product behavior stays in the owning domain contracts. This plan does not reopen shipped dataset IDs,
column freezes, WMI prohibition, host batch collect, or the 32-subscription cap.

Owning contracts: [`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md),
[`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md),
[`../../Core/Core_Settings.md`](../../Core/Core_Settings.md), and
[`../../UI/UI_XeneonDisplayWindowing.md`](../../UI/UI_XeneonDisplayWindowing.md).

Historical context:

- [`../Done/RFC_Plugins_XeneonDashboardArchitecture.md`](../Done/RFC_Plugins_XeneonDashboardArchitecture.md) — settled
  factory, widget, settings, dashboard, and local-pull decisions.
- [`../Done/SystemDataPlugin_2026-08-31.md`](../Done/SystemDataPlugin_2026-08-31.md) — first two datasets, host
  provider, Process Viewer.
- [`../Done/SystemDataMetricsExpansion_2026-09-01.md`](../Done/SystemDataMetricsExpansion_2026-09-01.md) — 18-dataset
  catalog and host batching.

## Scope

### Included

- Live System Data validation on hosts the primary desktop cannot represent.
- Product decisions that still need an explicit later implementation plan: push/network providers, host primitive
  batching, missing-plugin migration, and interactive WebView policy.

### Excluded

- Changing the shipped 18-dataset catalog, privacy rules, or native-first acquisition policy.
- Adopting a 128-subscription cap (host remains 32).
- Linking DXCore, creating a D3D device for GPU identity, a device-I/O thread, or any WMI/CIM path.
- Reopening Matrix Rain, Studio Clock, Desk Clock, or version-4 settings as active work.
- System Data dashboard widgets. Those shipped in
  [`../Done/SystemDataViewers_2026-09-02.md`](../Done/SystemDataViewers_2026-09-02.md): Direct3D scheduled widgets with
  sample-driven motion. Host primitive batching was not opened after the System-page WARP measurement.

## Remaining System Data hosts

Optional hardware absence remains a passing result on the primary AC-only desktop. These items close only when the
named host is exercised or the item is explicitly dropped.

| Item | Host | Pass condition |
| --- | --- | --- |
| Live battery IOCTL | Laptop with a present `GUID_DEVCLASS_BATTERY` device | `battery.list` rows use overlapped read-only queries; serial numbers stay unpublished; `power.summary` `batteryPresent` is 1 |
| Dedicated software GPU | VM or session with WARP/software GPU only | `gpu.adapter` `software` is Good 0/1; no D3D device; empty or `Unavailable` sensors stay honest |
| Live NIC/disk churn | Physical adapter rename, disconnect/reconnect, disk removal, volumes without mount points | Missing devices produce empty or `Unavailable` snapshots without a second catalog shape; unrelated datasets continue |
| Live ARM64 runtime | ARM64 Windows | `SystemDataTests` and Phase 0 layout asserts run on ARM64, not compile-only |
| Intel hybrid mapping | Optional P/E-core host | Efficiency-class columns match the OS CPU-set mapping |
| Live topology deltas | Any host that can reset or wrap counters | Network/storage rates recover after reset/reuse; `thread.list` truncation remains `Truncated` at 8,192 |

Do not raise row caps, cadences, or source storage from these hosts without a new measurement against the 16 MiB
fast-lane ceiling.

## Remaining architecture gates

Each gate needs its own later dated implementation plan before code lands. This file only records that the decision
is still open.

### 1. Data services beyond local pull

Decide provider isolation and push/network-provider policy. Local `IRedXeDataSource` pull, host `CollectSnapshots`
batching, and `builtin.system-data` are already shipped.

Outbound HTTP for bundled widgets is no longer decided from this file.
[`WeatherPlugin_2026-09-04.md`](WeatherPlugin_2026-09-04.md) owns the host network lane (schedule, cancel, shutdown),
plugin-owned curl in `Weather.dll`, and the weather widget. Push providers and a network `IRedXeDataSource` remain
open here.

### 2. Host-owned primitive batching

Keep `IRedXeGpuWidget` as the immediate-context mechanism. Studio Clock, Desk Clock, and the shipped Process Viewer
System Data family do not justify a host primitive IID. Release x64 WARP of the ten-widget System page at 2560×720
measured 8.24 ms mean full-frame `Renderer::Render`; WARP Present dominates, so primitive batching was not opened.
Do not add plugin-specific commands to the generic widget root.

### 3. Missing plugins and migration beyond version 4 reset

Decide placeholder behavior for configured but unavailable plugins, plugin/schema version migration, and user-visible
diagnostics beyond the approved preserve-and-reset behavior. Settings syntax and versioned plugin schemas stay owned
by `Core_Settings.md`.

### 4. Interactive native-window content

Before a bundled WebView or other networked child ships, define navigation, origin, download, permission, focus,
accessibility, process, memory, and quiescence policy. `IRedXeWindowWidget` lifetime and child-container boundaries
stay unchanged.

## Checklist

- [ ] Exercise the System Data host table or explicitly drop any row that will not be pursued.
- [ ] Keep optional-absence behavior on the primary desktop aligned with `Plugins_API.md`.
- [ ] Open a dated implementation plan for any architecture gate that is selected; do not implement it from this file
      alone.
- [ ] Reconcile lasting decisions into the owning domain contracts.
- [ ] Run the validation required by those contracts, plus `.\validate-skills.ps1`.
- [ ] Move this plan to `Specs/Plans/Done/` and remove its WIP index row.

## Exit criteria

This plan is complete when every System Data host row is validated or explicitly dropped, every architecture gate is
either implemented through its own dated plan or still recorded as a later decision in a replacement WIP document, and
no remaining requirement exists only here.
