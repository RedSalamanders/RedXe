# System data source, host provider, and process viewer

Status: `COMPLETE` — implemented, measured, reconciled, and validated
Created: 2026-08-31
Completed: 2026-09-01
Owner: plugin services, local system acquisition, and data-driven widgets

## Goal

Add a bundled local-system data plugin that supplies bounded snapshots of machine and process information through a
shared host-managed data provider. The first consumer is a native-window Process Viewer that displays a configurable
top-N process table without making acquisition part of a render callback. The host model must support several data
sources and several viewers without duplicating module loading or acquisition.

This plan executes the data-provider decision gate in
[`RFC_Plugins_XeneonDashboardArchitecture.md`](RFC_Plugins_XeneonDashboardArchitecture.md). Current shipped plugin
behavior remains owned by [`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md), and all acquisition and host
data-service work is subject to [`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md).

## Scope

### Included

- A plugin-side `IRedXeDataSource` with immutable dataset descriptors and synchronous borrowed snapshots.
- A host-side `IRedXeDataProvider` obtained through `IRedXeHost`, with dataset enumeration and subscriptions for one
  selected source.
- A bundled `builtin.system-data` DLL exposing `system.summary` and `process.list` datasets.
- Machine CPU, logical processor count, process/thread/handle counts, physical and committed memory, and uptime.
- Per-process PID, image name, normalized CPU use, working set, private bytes, thread count, and handle count.
- A host-owned acquisition service that schedules source calls, coalesces identical dataset acquisition, and
  synchronously fans borrowed snapshots to active subscribers on one acquisition worker.
- One bundled module catalog and loader shared by widget creation and data-source creation.
- A `builtin.process-viewer` native-window widget with `topN` settings, CPU ranking, memory, thread, and PID columns.
- Focused ABI, bounds, lifetime, sampling, and resource validation.

### Excluded

- Network, weather, hardware-vendor sensors, ETW sessions, kernel drivers, elevation, remote machines, process command
  lines, executable paths, environment blocks, and process control.
- Filtering, history charts, a settings UI, and data rendering in the provider DLL.
- Push callbacks or plugin-owned acquisition workers in the current slice.

## Selected architecture

### Pull acquisition and threading

`IRedXeDataSource::CollectSnapshot` is synchronous and non-reentrant. The host data service owns the worker and cadence;
it never calls acquisition from the UI or rendering thread. This eliminates plugin callback races and gives stop/drain
a single rule: cancel future scheduling, wait for the one serialized call to return, then release the source.

The current source has no callback and creates no plugin worker or timer. Push delivery is outside this plan and
would revise the source-coordinated pre-production interface set if selected later.

### Discovery and consumer access

`IRedXeHost::GetDataProvider` selects a provider plugin by ID and returns shared host-managed access to that one source.
The host lazily maps the source module, validates its data-source capability and descriptors, creates one
`IRedXeDataSource`, and caches the resulting runtime. A provider subscription therefore names only a dataset and
interval; the provider ID is not repeated in every subscription.

The bundled catalog has a general plugin-to-module table containing widget and data-only plugins, plus a widget-only
plugin-to-type table used by settings. One host module store resolves and validates exports for both `PluginManager`
and data-source creation. Capabilities remain authoritative in plugin metadata rather than being duplicated in the
catalog.

Several viewers using the same provider and dataset share one collection at the shortest active interval. Several
local pull providers share the same bounded event-blocked acquisition worker. Network and push-provider isolation
remain outside this plan.

### Borrowing and retention

Descriptors are module-owned and valid while the module is mapped. A snapshot and every row, value, and string it
references are provider-owned and valid until the next successful or failed `CollectSnapshot` call on that provider,
or until provider release. Consumers copy only values they retain after the call.

The host data service does not retain or duplicate source snapshots. It invokes each active sink synchronously while the
snapshot is borrowed; a consumer copies only the bounded values it retains. The Process Viewer keeps at most 32 rows
and 96 UTF-16 characters per displayed process name. History belongs to an explicitly bounded future consumer.

### Dataset model

Each dataset is a typed table with immutable columns. Values carry a quality field so inaccessible processes and the
first CPU baseline do not fail the entire snapshot.

| Dataset | Shape | Columns |
| --- | --- | --- |
| `system.summary` | Exactly one row | `totalCpuPercent`, `logicalProcessorCount`, `processCount`, `threadCount`, `handleCount`, `totalPhysicalBytes`, `availablePhysicalBytes`, `usedPhysicalBytes`, `committedBytes`, `uptimeMilliseconds` |
| `process.list` | Zero to 2,048 rows | `processId`, `imageName`, `cpuPercent`, `workingSetBytes`, `privateBytes`, `threadCount`, `handleCount` |

CPU percentages represent a share of total machine capacity in the range 0–100. The first sample reports
`Initializing`; inaccessible optional values report `Unavailable`. A snapshot sets `Truncated` rather than growing
past its row or string capacity.

### Cadence and invalidation

- Descriptors advertise a 2,000 ms recommended minimum interval for both initial datasets.
- The host provider clamps requests to the source recommendation and coalesces identical dataset subscriptions into one
  collection at the shortest active interval.
- Hidden, suspended, display-off, and no-subscriber states schedule no collection and own no periodic wake-up.
- A subscription starts inactive. `SetActive(FALSE)` drains an in-flight callback before returning. A Process Viewer
  callback copies its bounded top-N cache and posts only its own child-window invalidation.

### Privacy and failure isolation

The provider reads only local aggregate counters and the process attributes listed above with the caller's existing
token. It does not elevate, inject, terminate, suspend, open files, resolve full paths, collect command lines, access
the network, or persist data. Access denial degrades individual values to `Unavailable`.

Malformed IDs, unsupported datasets, reentrant collection, OS acquisition failure, and capacity exhaustion fail or
truncate locally. One data source failure must not stop unrelated providers, widgets, or later acquisition cycles.

## Resource budget

- `process.list` stores at most 2,048 rows and 65,536 UTF-16 image-name characters.
- Source storage is allocated once during source creation and remains bounded below 1 MiB in the initial
  implementation.
- Steady-state collection performs no plugin heap allocation. It owns one Toolhelp snapshot handle and at most one
  process query handle at a time.
- CPU history uses two fixed open-addressed tables and is replaced per sample; no map or per-process allocation is
  permitted.
- No provider timer, worker, busy loop, disk I/O, network I/O, logging, or per-sample string formatting is permitted.
- The host data service owns at most one event-blocked acquisition thread and two event handles per `PluginManager`;
  the thread is shared by all current local pull providers, and an inactive subscription blocks indefinitely and
  performs no collection.
- Process Viewer sorting uses fixed 32-row storage, painting uses one resize-owned DIB plus cached GDI objects, and no
  timer, GPU frame, per-sample heap allocation, or per-paint heap allocation is permitted.

## Implementation checklist

- [x] Add and document the current data-source, host-provider, sink, and subscription interfaces under
      `Common/PlugInterfaces/Data.h`.
- [x] Add the data-source capability bit and update the current pre-production interface IIDs/vtables in place.
- [x] Implement `Plugins/SystemData` with static descriptors and bounded pull snapshots.
- [x] Add focused factory, COM identity, descriptor, summary, process, bounds, and quality tests.
- [x] Add all four solution configurations and a build-only host reference.
- [x] Implement a host-owned data service with event-blocked scheduling, dataset acquisition deduplication, and
      stop/drain.
- [x] Define the sink/subscription interfaces in `Data.h` and prove that updates invalidate only the subscribed
      native child.
- [x] Add strict `topN` settings, deployed third-page examples, and the `builtin.process-viewer` dashboard consumer.
- [x] Replace the hardcoded broker with `IRedXeHost::GetDataProvider`, plugin-side `IRedXeDataSource`, and shared
      host-side `IRedXeDataProvider` runtimes.
- [x] Share one bundled module catalog and loader between widget providers and data sources.
- [x] Move visibility and quiescence to `IRedXeWidget::SetVisible` and apply it to every rendering mechanism.
- [x] Measure Release sampling CPU time, wake-ups, handles, private bytes, working set, and heap deltas at the row cap.
- [x] Reconcile every lasting requirement into the normative contracts and complete the full validation matrix.
- [x] After resource measurement passes, move this plan to `Done` and remove its WIP index row.

## Validation contract

1. Compile Debug and Release x64 and Release ARM64 with `/W4`, `/permissive-`, SDL checks, and warnings as errors.
2. Run the focused system-data tests through the repository test entrypoint. Verify factory output clearing,
   unsupported IID rejection, controlling-IUnknown identity, stable descriptors, unknown dataset rejection, sequence
   advance, one-row summary shape, and presence of the test process in `process.list`.
3. Verify every snapshot row and value matches the descriptor type and count, CPU is bounded to 0–100, byte counts do
   not overflow, and unavailable per-process values do not fail collection.
4. Force or simulate row and string capacity exhaustion and verify a bounded truncated snapshot with no overwrite.
5. Measure two steady-state Release collections and verify zero plugin heap-block/byte growth, no handle growth, and
   provider storage below the stated budget.
6. Verify through `HostPluginTests` that host provider lookup is shared, Process Viewer subscriptions are inactive
   while hidden, become active only when shown, publish 1–32 CPU-ranked rows, request no continuous GPU frames, drain
   on hide, and release their source, provider, widget, and subscription state on teardown.
7. Run `./format.ps1`, Debug and Release x64 `./test.ps1`, a Release ARM64 build, and `./validate-skills.ps1` before
   closeout.

## Closeout evidence

- `SystemDataTests --benchmark` uses the production per-process row-population path with 2,048 accessible synthetic
  entries. Three warmed Release x64 runs measured average wall times of 3.921, 3.823, and 3.806 ms per collection;
  the median was 3.823 ms. The corresponding 64-collection process-CPU probes measured 4.150, 3.906, and 3.662 ms per
  collection; the median was 3.906 ms.
- Each run measured two steady row-cap collections with zero busy-heap block/byte growth, zero handle growth, zero
  private-byte growth, and a 4 KiB working-set delta. Fixed source storage was 786,936 bytes, below the 1 MiB budget.
- The source diagnostic reports zero source-owned workers and timers. Release and Debug host integration left both
  Process Viewer subscriptions hidden for 2.2 seconds, longer than the 2-second dataset interval, with no additional
  delivered sample; hide and teardown drained and released both subscriptions.
- `format.ps1`, Debug and Release x64 rebuilt `test.ps1`, Release ARM64 rebuild, and `validate-skills.ps1` passed. The
  first Release rebuild encountered a transient MSVC PDB-service RPC failure; the unchanged exact retry completed
  with zero warnings and zero errors.
- Durable ABI, scheduling, privacy, resource, test-seam, measurement, and validation requirements are reconciled into
  `Plugins_API.md` and `Core_PerformanceAndResources.md`.

## Exit criteria

The plan is complete only when the host provider and consumer contract are implemented, inactive states own no collection
wake-up, measurements satisfy the resource budget, the full validation matrix passes, and durable requirements are
merged into the owning normative domain contracts.
