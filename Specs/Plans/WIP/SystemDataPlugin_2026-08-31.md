# System data provider and broker

Status: `ACTIVE` — provider foundation implemented; host broker and dashboard consumption pending
Created: 2026-08-31
Owner: plugin services, local system acquisition, and data-driven widgets

## Goal

Add a bundled local-system data plugin that supplies bounded snapshots of machine and process information for future
dashboard widgets. The first slice freezes the pull-provider ABI and implements the provider DLL. Later slices add the
host broker, subscriptions, invalidation, settings, and user-facing widgets without making acquisition part of a
render callback.

This plan executes the data-provider decision gate in
[`RFC_Plugins_XeneonDashboardArchitecture.md`](RFC_Plugins_XeneonDashboardArchitecture.md). Current shipped plugin
behavior remains owned by [`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md), and all acquisition and
broker work is subject to [`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md).

## Scope

### Included

- A new sibling COM interface, `IRedXeDataProvider`, with immutable dataset descriptors and synchronous borrowed
  snapshots.
- A bundled `builtin.system-data` DLL exposing `system.summary` and `process.list` datasets.
- Machine CPU, logical processor count, process/thread/handle counts, physical and committed memory, and uptime.
- Per-process PID, image name, normalized CPU use, working set, private bytes, thread count, and handle count.
- A future host-owned acquisition broker that schedules provider calls, copies retained data, coalesces updates, and
  invalidates only consumers whose subscribed generation changed.
- Focused ABI, bounds, lifetime, sampling, and resource validation.

### Excluded

- Network, weather, hardware-vendor sensors, ETW sessions, kernel drivers, elevation, remote machines, process command
  lines, executable paths, environment blocks, and process control.
- Rendering, sorting, filtering, history charts, and a settings UI in the provider DLL.
- Push callbacks or plugin-owned acquisition workers in version 1.

## Selected architecture

### Pull acquisition and threading

`IRedXeDataProvider::CollectSnapshot` is synchronous and non-reentrant. The future broker owns the worker and cadence;
it never calls acquisition from the UI or rendering thread. This eliminates plugin callback races and gives stop/drain
a single rule: cancel future scheduling, wait for the one serialized call to return, then release the provider.

Version 1 has no provider callback and creates no plugin worker or timer. A source that fundamentally requires push
delivery will use a new IID rather than changing this vtable.

### Borrowing and retention

Descriptors are module-owned and valid while the module is mapped. A snapshot and every row, value, and string it
references are provider-owned and valid until the next successful or failed `CollectSnapshot` call on that provider,
or until provider release. Consumers copy only values they retain after the call.

The broker will keep one latest bounded snapshot per active dataset, not an unbounded event stream. History belongs to
an explicitly bounded consumer or future broker policy.

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
- The future broker will clamp requests to the provider recommendation and coalesce identical dataset subscriptions.
- Hidden, suspended, display-off, and no-subscriber states schedule no collection and own no periodic wake-up.
- A changed generation posts one coalesced UI invalidation for affected static widgets. Unchanged content does not
  invalidate or present a frame.

### Privacy and failure isolation

The provider reads only local aggregate counters and the process attributes listed above with the caller's existing
token. It does not elevate, inject, terminate, suspend, open files, resolve full paths, collect command lines, access
the network, or persist data. Access denial degrades individual values to `Unavailable`.

Malformed IDs, unsupported datasets, reentrant collection, OS acquisition failure, and capacity exhaustion fail or
truncate locally. One data source failure must not stop unrelated providers, widgets, or later acquisition cycles.

## Resource budget

- `process.list` stores at most 2,048 rows and 65,536 UTF-16 image-name characters.
- Provider storage is allocated once during provider creation and remains bounded below 1 MiB in the initial
  implementation.
- Steady-state collection performs no plugin heap allocation. It owns one Toolhelp snapshot handle and at most one
  process query handle at a time.
- CPU history uses two fixed open-addressed tables and is replaced per sample; no map or per-process allocation is
  permitted.
- No provider timer, worker, busy loop, disk I/O, network I/O, logging, or per-sample string formatting is permitted.

## Implementation checklist

- [x] Add and document the version 1 data-provider IID and records under `Common/PlugInterfaces/`.
- [x] Add the data-provider capability bit without changing existing interface vtables or IIDs.
- [x] Implement `Plugins/SystemData` with static descriptors and bounded pull snapshots.
- [x] Add focused factory, COM identity, descriptor, summary, process, bounds, and quality tests.
- [x] Add all four solution configurations and a build-only host reference.
- [ ] Implement a host-owned broker with event/timer-blocked scheduling, subscriber deduplication, stop/drain, and
      bounded latest-snapshot storage.
- [ ] Define the widget-side read/subscription interface under a new IID and prove that updates invalidate only
      affected static widgets.
- [ ] Add settings declarations and dashboard widgets only after the broker contract is validated.
- [ ] Measure Release sampling CPU time, wake-ups, handles, private bytes, working set, and heap deltas at the row cap.
- [ ] Reconcile every lasting requirement into the normative contracts, complete the full validation matrix, move
      this plan to `Done`, and remove its WIP index row.

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
6. Run `./format.ps1`, Debug and Release x64 `./test.ps1`, a Release ARM64 build, and `./validate-skills.ps1` before
   closeout.

## Exit criteria

The plan is complete only when the broker and consumer contract are implemented, inactive states own no collection
wake-up, measurements satisfy the resource budget, the full validation matrix passes, and durable requirements are
merged into the owning normative domain contracts.
