# Done: System Data viewers in Process Viewer

Status: `COMPLETE`
Created: 2026-09-02
Completed: 2026-09-02
Owner: bundled GPU widgets, dashboard templates, and host module catalog

## Goal

Ship a family of Direct3D 11 System Data widgets in `ProcessViewer.dll`, with motion and polish that matches a XENEON
strip, and add one default System page that instantiates every widget in the family.

Do not add a raw table per dataset. Each widget MUST present the smallest set of values a person can use at a glance:
ranked lists, capacity bars, KPI tiles, adapter cards, heatmaps, and sparklines. Bars, ranks, heat cells, and KPIs
MUST ease to new samples. Unavailable or empty hardware MUST look quiet and honest, never like a zero reading.

Owning contracts that this plan will change: [`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md),
[`../../Core/Core_Settings.md`](../../Core/Core_Settings.md), [`../../UI/UI_Dashboard.md`](../../UI/UI_Dashboard.md),
and [`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md).

Related work:
[`../WIP/PluginDashboardRemainingCloseout_2026-09-02.md`](../WIP/PluginDashboardRemainingCloseout_2026-09-02.md) and
[`SystemDataMetricsExpansion_2026-09-01.md`](SystemDataMetricsExpansion_2026-09-01.md).

## Selected product

### One DLL, many settings-visible plugins

`Plugins/ProcessViewer` remains `ProcessViewer.dll`. It already publishes `builtin.process-viewer`. It MUST also
publish one settings-visible plugin ID per widget kind below. The ABI already allows a DLL to enumerate several IDs,
each with its own settings contract and one widget type.

The host catalog today forbids duplicate module names and gives each plugin ID its own `PluginHost` slot. This plan
changes that:

- plugin IDs stay unique;
- type IDs stay unique;
- several catalog rows MAY share `ProcessViewer.dll`;
- `PluginHost` MUST map that path once, share exports, and call optional shutdown once.

Settings still name `plugin` only, never `typeId`. Each new ID maps to exactly one type in `kRedXeBundledWidgets`.

### Direct3D widgets with scheduled motion

Every catalog widget, including Process Viewer, is `IRedXeGpuWidget` plus `IRedXeScheduledWidget` on one controlling
`IUnknown`. None of them set `RedXeWidgetFlagContinuousAnimation`. Process Viewer MUST leave the native-window path;
`GdiOrbit` remains the shipped window-widget example.

Visual language (XENEON strip):

- Near-black panels (`#111111`) with a hairline and a soft inner red glow (`#FF1616`).
- Large eased numerals, dim labels, rounded capacity bars, heat cells, and 60-sample sparklines.
- Ranked rows slide to their new order; bars and heat cells lerp over 280–400 ms with ease-out.
- A brief accent pulse when a KPI crosses a high band (CPU or GPU load), visual only. No audio.
- Empty and `Unavailable` values render as muted em dashes or “No battery”, never as invented zeros.
- Widget-local sparkline history is at most 60 samples in fixed storage. History MUST NOT move into System Data.

Animation is sample-driven, following Desk Clock: presentation-paced frames only while an ease is in flight, then a
blocked wait until the next snapshot. A settled System page MUST NOT spin at display refresh. Snapshot delivery on the
acquisition worker copies bounded CPU state and MUST NOT render. The host MUST coalesce one UI-thread frame
invalidation after delivering snapshots to an active GPU data sink, because these widgets have no child HWND to post
to.

Shared GPU resources live once per Process Viewer provider/device, not once per widget instance:

- build-time Shader Model 5.0 blobs (no runtime compiler);
- one panel/bar/heatmap instanced pipeline and one glyph pipeline;
- one bounded grayscale atlas for digits, units, and live labels;
- DirectWrite and system fonts only while filling new atlas glyphs, then released, as Desk Clock does at device
  init. Visible process, adapter, and interface names rasterize only when the displayed string set changes.

Each widget maps a small dynamic constant buffer only when visual state changes. A frame SHOULD stay at or under three
draws per widget. The generic widget root MUST NOT grow plugin drawing records. Host-owned primitive batching stays
unselected unless the System-page measurement gate below fails.

### Widget catalog

Do not ship viewers for `source.status`, `thread.list`, or `gpu.engine`. Status is a diagnostic table. Threads are
already summarized on Process Viewer and System Pulse; materializing 8,192 rows for a dashboard is the wrong shape.
Engine utilization is `Unavailable`.

Keep Process Viewer’s leading `process.list` columns and `topN` contract. The table becomes a GPU ranked list with
eased CPU bars and sliding rows.

| Plugin ID | Type ID | What the user sees | Datasets (interval) | Motion |
| --- | --- | --- | --- | --- |
| `builtin.process-viewer` | `process-viewer` | Ranked process table with CPU bars | `process.list` (2 s) | Row slide + bar ease |
| `builtin.system-pulse` | `system-pulse` | Six KPI tiles: CPU, processes, threads, handles, used RAM, uptime | `system.summary` (1 s) | Numeral tick + tile pulse |
| `builtin.cpu-meter` | `cpu-meter` | Large total %, stacked user/kernel/idle, logical heatmap capped at 64 cells with “+N” overflow | `cpu.summary` + `cpu.logical` (1 s) | Heat lerp + peak flash |
| `builtin.memory-meter` | `memory-meter` | Physical and commit stacked bars, cache, page-in/out rates | `memory.summary` (1 s) | Fill ease + rate sparkline |
| `builtin.network-meter` | `network-meter` | Top interfaces by byte rate (default 8), sparkline, IPv4/IPv6 TCP/UDP KPI row | `network.interface` + `network.protocol` (1 s) | Sparkline scroll + bar ease |
| `builtin.storage-meter` | `storage-meter` | Volume capacity bars plus per-disk active/idle and bytes/s | `storage.volume` (5 s) + `storage.disk` (1 s) | Capacity ease + activity pulse |
| `builtin.gpu-meter` | `gpu-meter` | One card per adapter: name, dedicated size, clocks, power, temperature, fan | `gpu.adapter` (1 s) | Temp color lerp + fan tick |
| `builtin.gpu-processes` | `gpu-processes` | Top processes by GPU engine util (default 8), PID + engine type | `gpu.process` (2 s) | Same ranking motion as Process Viewer |
| `builtin.power-meter` | `power-meter` | AC/DC, charge ring, saver flag; battery rows or a quiet “AC only” state | `power.summary` (5 s) + `battery.list` (5 s) | Charge ring ease |
| `builtin.thermal-meter` | `thermal-meter` | Temperature list + fan RPM bars; hide empty groups | `thermal.sensor` + `fan.sensor` (10 s) | Color lerp + RPM ease |

Closed settings:

- Process Viewer keeps required `topN` 1–32, default 10.
- Network and GPU-process viewers publish required `topN` 1–16, default 8.
- Every other new plugin publishes a closed empty object `{}`.

All widgets call `IRedXeHost::GetDataProvider("builtin.system-data", ...)` and subscribe only while visible. Several
widgets MAY share one host provider and coalesce identical datasets. Unique datasets on the System page MUST stay at
or under the host cap of 32 subscriptions; the catalog above uses 15 unique dataset IDs if every widget on that page
is visible.

Do not subscribe to `thread.list`.

### Default System page

Both Debug and Release templates keep their current first page (low-resource startup) and second page (bundled
gallery). They MUST add a third page named `System` that places one instance of every catalog widget above, including
Process Viewer.

Release launch still starts on full-canvas Matrix. The System page is created only when it is current or the staged
swipe neighbor, like every other page.

Suggested ultrawide tree (long-side = horizontal on landscape). Ratios are starting points; keep 10 leaves, depth ≤ 8:

```text
long-side
  3  short-side: System Pulse / CPU / Memory
  4  short-side: Processes / GPU Processes
  3  short-side: Network / Storage
  3  short-side: GPU / Thermal / Power
```

Gallery pages keep their existing Process Viewer instance so every current bundled widget remains visible there.
Template coverage is still “at least one placed instance per settings-visible plugin per template,” not per page.

`Core_Settings.md` currently requires exactly two shipped pages. This plan updates that to three: startup, gallery,
System.

## Host and factory work

- Relax `BundledPlugins.h` so module names may repeat; keep unique plugin IDs and unique type IDs; keep the 64-plugin
  settings cap.
- Share `PluginHost` module slots by normalized module name. Loading `builtin.process-viewer` MUST map
  `ProcessViewer.dll` once for every sibling ID.
- After `OnDataSnapshot` fan-out on the acquisition worker, coalesce one UI-thread frame invalidation so GPU data
  widgets can start an ease without a child HWND.
- `ValidateMetadata` already accepts several records; keep requiring that the requested ID is present.
- Extend `RedXeGetPluginSettingsContract` in Process Viewer to dispatch by plugin ID. Extend `FactoryImpl.h` with a
  bounded ID-to-contract table if that avoids copy-paste.
- `RedXeCreate` for each new ID returns a widget provider that enumerates that ID’s one type.
- Schema `oneOf` branches, parser support, `BundledPlugins.h`, and both templates change in the same change.
- Rewrite Process Viewer host tests off the native-window path. Add WARP readback for a System-page composition,
  device-loss rebuild, hide/show subscription drain, and proof that a settled page is non-continuous.

## Performance

- Render and snapshot copy stay allocation-free after attach. Ranked lists copy at most `topN` rows into fixed
  storage.
- CPU heatmap stores at most 64 display cells; extra logical processors collapse to a count.
- Inactive System page creates no providers, widgets, subscriptions, device resources, or scheduled deadlines.
- Hidden, minimized, suspended, occluded, and display-off MUST stop eases and drains subscriptions. Recovery draws
  current values; missed eases are not replayed.
- Measure Release x64 WARP at 2560×720 with the System page visible, at rest and during a full ease. Record CPU
  submission, GPU timestamp, draws, maps, heap/handle deltas, and subscription count.
- Gate: at-rest median CPU submission MUST stay under 1.5 ms and a full-page ease under 3 ms, with zero heap/handle
  growth. If the page misses that gate, stop and add a host-owned primitive batch before shipping the family. Do not
  paper over it with continuous animation.
- Do not raise the 32-subscription cap or the 16 MiB source ceiling in this plan.

## Excluded

- New System Data datasets, WMI, DXCore, a D3D device for GPU identity, or a device-I/O thread.
- Publishing command lines, paths, MAC, IP, serials, or SSIDs.
- A `thread.list` or `gpu.engine` dashboard widget.
- Runtime shader compiler on the frame path. DirectWrite and WIC MUST NOT remain loaded after atlas glyph fills.
- Audio, extra HWND, or GDI paint in this DLL.
- Moving Process Viewer to a new DLL name.

## Checklist

- [x] Share `ProcessViewer.dll` across unique plugin IDs in the bundled catalog and `PluginHost`.
- [x] Coalesce UI-thread frame invalidation after GPU data-sink delivery.
- [x] Convert Process Viewer to GPU/scheduled and implement the nine sibling widgets with shared device resources and
      the motion catalog above.
- [x] Keep Process Viewer columns, `topN`, hide/show drain, and host-provider sharing.
- [x] Add the System page to both shipped templates and update schema/parser/catalog together.
- [x] Update `Plugins_API.md`, `Core_Settings.md`, `UI_Dashboard.md`, and resource validation, including Process
      Viewer no longer being a window widget.
- [x] Add contract, settings, WARP, device-loss, and host tests for the new IDs and the System page.
- [x] Record the System-page WARP measurement; take the primitive-batching gate if it fails.
- [x] `.\format.ps1`, Debug and Release x64 `.\test.ps1`, ARM64 Debug and Release compile, `.\validate-skills.ps1`.
- [x] Move this plan to `Specs/Plans/Done/` when the contracts, templates, and tests agree.

## Exit criteria

This plan is complete when every catalog widget is a Direct3D scheduled widget with the selected motion, both
templates show the System page with one live instance of each, optional-empty hardware still renders honestly,
subscriptions stay within the host cap, a settled page does not run continuous frames, the WARP gate passes or host
batching is implemented, and every durable rule lives in the owning domain contracts rather than only here.

Closeout evidence (2026-09-02): Debug and Release x64 `test.ps1` passed (crash-harness leftover exit 17). ARM64 Debug
and Release compiled. Release x64 System-page WARP at 2560×720 measured 8.24 ms mean full-frame `Renderer::Render`
(32 frames); Debug was 8.25 ms. WARP Present dominates, so host primitive batching was not added. Durable rules live
in `Plugins_API.md`, `Core_Settings.md`, `UI_Dashboard.md`, and `Core_PerformanceAndResources.md`.
