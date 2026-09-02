# System data metrics expansion

Status: `COMPLETE` — implemented, measured, reconciled, and validated
Created: 2026-09-01
Completed: 2026-09-01
Owner: plugin services, local system acquisition, and data-driven widgets

## Goal

Expand `builtin.system-data` from the initial machine summary and process table into the common local telemetry source
for RedXe. One DLL will publish independently subscribable, bounded datasets for CPU, memory, processes, network,
storage, GPU, power, thermal, and fan information while preserving the host-owned scheduling, borrowed-snapshot, and
zero-work-when-inactive model.

The result must use only bounded local Windows interfaces, degrade honestly when hardware or providers expose no
data, and remain cheap enough for an always-on dashboard. WMI is forbidden because its library footprint, service
activation, provider discovery, startup cost, and query latency do not fit this always-on source: System Data must not
include WMI headers, link WMI libraries, create an `IWbem*` service, execute a CIM query, or retain a WMI fallback.
Still-exported Microsoft native or semi-private APIs such as `NtQuerySystemInformation`,
`NtQueryInformationProcess`, and D3DKMT are the preferred acquisition layer when their version, layout, validation,
fallback, and measured cost are explicit. Documented Win32 APIs and Microsoft SDK/WDK interfaces provide required
fallbacks and fill gaps that the native bulk surfaces do not expose.

“As much data as possible” means broad capability with explicit availability and resource limits. It does not
authorize polling every possible provider, loading vendor SDKs, using a RedXe kernel driver, issuing write/control
IOCTLs, evaluating arbitrary firmware methods, or turning unsupported sensors into invented values.

Durable behavior is owned by [`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md) and
[`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md). This plan is non-normative
until its selected behavior is reconciled into those contracts.

## Relationship to the baseline plan

[`SystemDataPlugin_2026-08-31.md`](SystemDataPlugin_2026-08-31.md) records the completed two-dataset foundation,
`PluginHost`, `ProcessViewer`, and baseline resource measurements. This expansion must preserve that accepted
baseline:

- retain the recorded baseline Release measurements when changing the acquisition ABI or source storage budget;
- retain the current `system.summary` and `process.list` IDs and existing columns so Process Viewer continues to work;
- treat every new column as append-only during the pre-production transition unless a measured prototype proves an
  in-place redesign is necessary; and
- treat the archived baseline plan as fixed historical evidence; this expansion owns every later contract and
  implementation change.

Phase 0 working artifacts of this plan, not separately indexed WIP entries:

- [`SystemDataMetricsExpansion_Phase0Evidence.md`](SystemDataMetricsExpansion_Phase0Evidence.md) — reference machines,
  measurement log, storage envelope, column freeze, and Phase 1 entry checklist.
- [`SystemDataMetricsExpansion_NativeClassMatrix.md`](SystemDataMetricsExpansion_NativeClassMatrix.md) — information-class
  dispositions and layout proof.

## Evidence from the legacy code

The reference implementation is useful as a sampling design

| Legacy area | Reusable lesson | RedXe decision |
| --- | --- | --- |
| `perfpage.cpp` | Take two cumulative samples, use the real elapsed interval, keep per-processor history, and derive global CPU and commit/memory summaries from one acquisition pass. | Preserve the delta model and one-pass fan-out. Replace fixed processor limits with processor-group-aware bounded topology. Use `NtQuerySystemInformation(SystemProcessorPerformanceInformation)` behind a guarded native adapter, with `GetSystemTimes` as the global fallback. |
| `procpage.cpp` | A single system process query can expose CPU, memory, handle, thread, and I/O counters more cheaply than repeatedly opening every process. | Use a guarded `NtQuerySystemInformation(SystemProcessInformation)` bulk backend as the preferred process path after Phase 0 validation. Retain the current Toolhelp/per-process public path as the complete fallback. |
| `netpage.cpp` | Cache stable adapter identity, sample cumulative octet counters, compute rates from deltas, and avoid collection while the page is inactive. | Use current IP Helper structures, stable interface identifiers, topology invalidation, and subscription-driven collection. |
| All pages | Fixed histories and cached display metadata reduce work, but the old implementation assumes legacy topology and internal structures. | Keep only one prior sample per entity in the provider. Time-series history and rendering remain consumer responsibilities. |

The legacy source predates modern GPU counters and does not provide a general, reliable thermal or fan interface.
Those domains therefore require independent capability probes and must be optional.

## Scope

### Included

- Multiple independently subscribable datasets from the existing `builtin.system-data` DLL.
- One host-to-source batch collection call for all datasets due from a source in the same worker pass.
- Shared raw acquisitions within CPU/memory/process, network, storage, GPU, and sensor domains.
- Documented Windows APIs, Microsoft SDK/WDK device interfaces, performance counters, and guarded Microsoft native or
  semi-private APIs whose symbols and record layouts remain valid on the supported Windows builds.
- Explicit dataset/backend availability, value quality, truncation, counter reset, and sample-cost diagnostics.
- Processor groups, CPU sets, hybrid-CPU efficiency classes, adapter/device changes, and counter-instance churn.
- Optional bounded device acquisition for ACPI thermal zones, batteries, storage temperature, and WDDM GPU sensors,
  isolated from normal system metrics when a driver call cannot satisfy the fast-lane budget.
- Deterministic fake-backend tests and real-machine smoke tests that accept legitimately unavailable optional data.

### Excluded

- Network flows, packet capture, remote addresses, SSIDs, per-process network attribution, ETW sessions, and remote
  machines.
- Process command lines, executable paths, owners, environment blocks, control operations, or elevation.
- Vendor SDKs, third-party sensor libraries, kernel drivers, firmware writes, fan control, overclocking, and power-limit
  control.
- WMI/CIM acquisition of any kind, including `IWbemLocator`, `IWbemServices`, `Win32_*`, `MSAcpi_*`, and a helper
  process or PowerShell command used to hide a WMI query.
- Arbitrary ACPI method evaluation, physical-memory or embedded-controller access, MSR/PCI configuration reads, and
  any IOCTL or D3DKMT escape that changes hardware state.
- Time-series retention, chart rendering, alerting, persistence, upload, and new settings-visible viewers.
- Treating `IDXGIAdapter3::QueryVideoMemoryInfo` as machine-wide GPU memory use; it reports the calling process's budget
  and usage, not a global total.

## Selected architecture

### Dataset catalog and compatibility

Descriptors remain immutable and module-owned. Optional datasets are always discoverable so settings and viewers do
not change shape when a driver disappears. Unsupported datasets return a valid zero-row snapshot or unavailable
values, and `source.status` explains why. Existing columns keep their IDs, types, units, and meanings.

Initial bounds and cadences are conservative. Phase 0 measurements may lower a row cap or raise an interval, but may
not remove a domain silently.

| Dataset | Maximum rows | Recommended minimum | Sensitivity | Intended contents and primary backend |
| --- | ---: | ---: | --- | --- |
| `source.status` | 32 | 5 s | None | One row per dataset: availability enum, selected backend tier/name/version, fallback-active flag, last bounded result category, last success time, collection duration, failure count, truncation count, and retry time. Static strings plus collector state. |
| `system.summary` | 1 | 1 s | None | Compatible curated row: total CPU, topology counts, process/thread/handle totals, physical/commit memory, boot time, and uptime. CPU, topology, memory, boot, and uptime come from the cheap Tier A system-information batch plus cached topology. Process, thread, and handle totals come from `SystemPerformanceInformation` / `SystemHandleCountInformation`, with Tier B `K32GetPerformanceInfo`. This 1 s dataset must not walk `SystemProcessInformation`; that bulk query runs only when `process.list` or `thread.list` is due. If cheap totals fail Phase 0, raise this interval to 2 s instead of coupling summary to the process walk. `GetTickCount64` and `GlobalMemoryStatusEx` remain validation and fallback sources. Current `SystemData.cpp` marks this dataset `LocalSensitive`; expansion publishes it as `None` because the row is machine aggregates without process names. |
| `cpu.summary` | 1 | 1 s | None | Total, user, kernel, idle, DPC, and interrupt percentages; logical/core/package/NUMA counts; current/max frequency when exposed; context-switch and interrupt rates. Primary: guarded `SystemProcessorPerformanceInformation`, `SystemPerformanceInformation`, topology APIs, and `CallNtPowerInformation(ProcessorInformation)`. Global fallback: `GetSystemTimes`; optional deep-counter fallback: PerfLib/PDH. |
| `cpu.logical` | 1,024 | 1 s | None | Stable group/index ID, core/package/NUMA mapping, CPU-set ID, efficiency class, parked/allocated flags, total/user/kernel/idle/DPC/interrupt percentages, interrupt count/rate, and current/max frequency. Primary: guarded `SystemProcessorPerformanceInformation` plus cached `GetLogicalProcessorInformationEx`, `GetSystemCpuSetInformation`, and `CallNtPowerInformation`. PerfLib/PDH is fallback only. |
| `memory.summary` | 1 | 1 s | None | Physical total/available/in-use, commit current/limit/peak, cache, paged/nonpaged pools, page faults, and page-in/page-out rates. Primary: guarded `SystemBasicInformation` plus `SystemPerformanceInformation`, with every accepted current-layout field inventoried. Tier B `K32GetPerformanceInfo` and `GlobalMemoryStatusEx` are the complete base fallback. Optional standby/modified detail uses a lazy measured counter query when no accepted native class supplies it. |
| `process.list` | 2,048 | 2 s | Local sensitive | Existing fields plus parent PID, session, user/kernel/total CPU, peak working set, private/virtual bytes, handle count, I/O byte and operation totals/rates, page faults/rate, start time, cycle count, base priority, group-aware affinity, WOW64/native architecture, subsystem, and critical-process state where query access permits. Affinity is `affinityGroup` plus `affinityMask`, or omitted until the column freeze; a single `UInt64` is never the complete affinity of a process. Primary: guarded `SystemProcessInformation` bulk query plus approved `NtQueryInformationProcess` classes for unique missing fields that still meet the 2,048-row p95 after measurement. Complete Tier B fallback: Toolhelp plus one transient `PROCESS_QUERY_LIMITED_INFORMATION` handle at a time. The PEB, path, command line, environment, debug state, and telemetry identity remain excluded. |
| `thread.list` | 8,192 | 2 s | Local sensitive | PID/create-time identity, TID, user/kernel/total CPU time and rate, priority/base priority, thread state, wait reason, context switches, and available cycle/wait counters. Materialize the validated thread records already returned inside the Tier A `SystemProcessInformation` buffer, so subscribing adds no second bulk query. A measured `SystemExtendedProcessInformation` prototype may add non-address metrics. Tier B uses a bounded Toolhelp thread snapshot and targeted thread queries only if it meets the budget. Start/stack/TEB addresses are not published. The primary host currently has ~20k threads, so this cap will set `Truncated` in normal use; do not raise it without a storage-envelope measurement. |
| `network.interface` | 256 | 1 s | Local sensitive | Stable interface ID, alias/description, type, operational/media state, MTU, receive/transmit link speed, byte/packet totals and rates, unicast/non-unicast counts, errors, discards, and utilization. Cached `MIB_IF_ROW2` identities with `GetIfEntry2`. No MAC or IP address by default. |
| `network.protocol` | 16 | 1 s | None | IPv4/IPv6, TCP, and UDP aggregate counters and rates: connections, resets/failures, segments/datagrams, retransmits, discards, and errors. IP Helper statistics APIs. |
| `storage.disk` | 128 | 1 s | Local sensitive | Stable disk number/name/type, capacity, active/idle percentage, queue depth, read/write bytes and operations per second, split count, and average read/write latency. Primary: cached read-only disk handle plus `IOCTL_DISK_PERFORMANCE`; identity/capability comes from `IOCTL_STORAGE_QUERY_PROPERTY` and size from `IOCTL_DISK_GET_LENGTH_INFO`. PerfLib/PDH is a fallback only. |
| `storage.volume` | 256 | 5 s | Local sensitive | Stable volume GUID, mount/display name, filesystem, total/free bytes, and physical disk extents. Primary: `FindFirstVolumeW`/`FindNextVolumeW`, `GetVolumeInformationByHandleW`, `GetDiskFreeSpaceExW`, and `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`. Activity is published only when an exact volume counter mapping is proven; it is never inferred by duplicating whole-disk activity. |
| `gpu.adapter` | 32 | 1 s | None | Stable adapter LUID, display name, vendor/device IDs, software/integrated/discrete flags, dedicated/shared capacity, aggregate utilization, machine-wide dedicated/shared use, engine/memory clocks, power percentage, temperature, warning/max temperature, fan RPM, and maximum fan RPM where exposed. Identity/capacity: DXGI, with optional DXCore property enrichment joined by LUID. Sensors/frequencies: `D3DKMTQueryAdapterInfo` with WDDM 2.4 performance records. Utilization/memory: guarded D3DKMT statistics or GPU PerfLib/PDH counters. |
| `gpu.engine` | 512 | 1 s | None | Adapter LUID, physical-adapter index, node ordinal, engine class, current/max frequency, voltage, and utilization. D3DKMT node metadata/performance data supplies identity and clocks; guarded node running-time deltas or GPU Engine counters supply utilization. Aggregate adapter utilization is the busiest engine, not a sum that can exceed 100%. |
| `gpu.process` | 2,048 | 2 s | Local sensitive | PID, adapter LUID, engine class, utilization, and dedicated/shared GPU memory when a batched provider exposes a trustworthy mapping. Primary prototype: one cached GPU Engine/GPU Process Memory PerfLib or PDH query. A D3DKMT process/node prototype is acceptable only when it avoids a process-by-node Cartesian query and beats the counter backend. No process name duplication. |
| `power.summary` | 1 | 5 s | None | AC/DC state, battery-present/charging flags, charge percentage, estimated remaining/full time, and battery-saver state from `GetSystemPowerStatus`; cached sleep/display capabilities from `GetPwrCapabilities`. |
| `battery.list` | 32 | 5 s | Local sensitive | Stable battery device key, state, designed/full/current capacity, charge/discharge rate, voltage, temperature, chemistry, relative-unit flag, cycle count, and estimated time. Enumerate `GUID_DEVCLASS_BATTERY` with SetupAPI, then use read-only battery IOCTLs and miniport quality sentinels; no WMI. |
| `thermal.sensor` | 128 | 10 s | Local sensitive | Provider/sensor ID, device kind, display name, current/warning/critical temperature, and quality. Sources are read-only ACPI thermal-zone IOCTL probes, storage temperature descriptors, battery miniport temperature, and WDDM GPU adapter performance data. CPU/package temperature remains unavailable when firmware exposes it only through WMI, a vendor SDK, or privileged hardware access. |
| `fan.sensor` | 128 | 10 s | Local sensitive | Provider/sensor ID, device kind, display name, current/max RPM, active state, and quality. WDDM 2.4 `D3DKMT_ADAPTER_PERFDATA`/caps can supply the main GPU fan. Generic fan enumeration may prove presence, but no RPM row is emitted without a Microsoft read contract. Motherboard/CPU fan RPM is therefore commonly unsupported; no WMI or ACPI-method fallback is permitted. |

The exact columns, units, signedness, and enum values are written into
[`SystemDataMetricsExpansion_Phase0Evidence.md`](SystemDataMetricsExpansion_Phase0Evidence.md) during Phase 0 and
copied into this catalog before Phase 1 implementation. Temperatures use degrees Celsius, byte counters use bytes,
rates use units per second, durations use milliseconds or 100 ns timestamps as named, and percentages are finite
`Float64` values in the range 0–100.

### Acquisition policy and source tiers

The default order is native-first. It is based on maximum useful coverage through bounded bulk queries, then
compatibility and measured steady-state cost. All accepted backends are shipped by Microsoft with Windows and are
called only from user mode.

| Tier | Eligible surface | Use rule |
| --- | --- | --- |
| A — Microsoft native/WDK | `NtQuerySystemInformation`, `NtQueryInformationProcess`, D3DKMT exports and SDK/WDK records, and read-only power/thermal class IOCTLs | Preferred. Use every approved read-only information class and every validated field that contributes unique data to an active dataset. Resolve optional exports at runtime from System32 modules, isolate them behind private adapters, validate every returned byte, and fall back to Tier B or per-column `Unavailable` on any symbol, version, layout, access, or semantic failure. |
| B — documented application API | Kernel32, PSAPI, IP Helper, SetupAPI, volume/storage IOCTLs, power and battery APIs, DXGI/DXCore, and other documented Win32 calls | Required compatibility fallback and gap filler. Cache topology and handles; do not rediscover or repeat data already obtained from Tier A on every sample. |
| C — Microsoft performance provider | Installed Windows counter sets consumed through a cached PerfLib V2 or PDH query | Use only when Tier A and Tier B expose no trustworthy equivalent or as their measured fallback. Open lazily for the first dependent subscription and close when the source is destroyed. |
| Forbidden | WMI/CIM, vendor DLLs, arbitrary firmware methods, raw embedded-controller/MSR/PCI access, kernel drivers, ETW sessions, and write/control APIs | Do not prototype or ship in this plan. Absence is reported through quality and `source.status`. |

Tier A is not a license to copy arbitrary internal structures or probe numeric classes blindly. Each native adapter
must record the exact information class, minimum OS/build or WDDM level, authoritative header or symbol-derived layout,
x64/ARM64 assertions, required access, returned-length rules, fields consumed, and Tier B fallback. The adapter fails
closed on an unknown symbol, size, offset, enum value, unit, or semantic invariant. Native status values are translated
to a bounded backend category; they are not formatted into per-sample strings.

“Use all flags and options” means exhaustive coverage of approved read-only information classes, not a loop from zero
to `MaxSystemInfoClass` or `MaxProcessInfoClass`. Native query functions select an information class rather than a
general flags mask. Phase 0 maintains a checked matrix of every class exposed by the selected SDK/WDK and every
additional pinned class used by the legacy Microsoft source, with one disposition: `used`, `duplicate`, `sensitive`,
`mutating`, `kernel-only`, `opaque`, `unbounded`, `unsupported`, or `unknown-layout`. Only `used` classes enter runtime
code, and only while a dependent dataset is active.

### Native information-class coverage

| Native query | Tier A classes to implement or prototype | Publication and fallback |
| --- | --- | --- |
| `NtQuerySystemInformation` base/performance | `SystemBasicInformation` (0), `SystemPerformanceInformation` (2), `SystemTimeOfDayInformation` (3), `SystemProcessorPerformanceInformation` (8), `SystemRegistryQuotaInformation` (37), and `SystemHandleCountInformation` (253) | Consume every validated field that maps to `system.summary`, `cpu.summary`, `cpu.logical`, or `memory.summary`, including page/commit/cache/pool totals, system-call/context-switch/page-I/O counters, processor times, boot/sleep timing, registry quota, and handle totals. Tier B uses system-information, PSAPI, power, and counter APIs per field. |
| `NtQuerySystemInformation` process/thread | `SystemProcessInformation` (5) is the full bulk process-and-thread sample. Inventory the legacy Microsoft `SystemExtendedProcessInformation` class but adopt it only for unique non-address fields. `SystemBasicProcessInformation` (252) is separately supported on Windows 11 build 26100.4770 and later for an identity-only request or a measured cheaper churn scan; it is not redundantly called beside the full sample without evidence. | Use every validated process and thread field that maps to `process.list` or `thread.list`. Preserve `(PID, CreateTime)` identity for the full sample and `(PID, CreateTime, TID)` for a thread; the basic class may use its sequence number. Tier B is Toolhelp plus documented per-process/thread queries. |
| `NtQuerySystemInformation` optional/security | Inventory `SystemCodeIntegrityInformation`, `SystemKernelVaShadowInformation`, `SystemSpeculationControlInformation`, and `SystemQueryPerformanceCounterInformation`; add fields only through an explicit descriptor update with documented bit meanings. | Query lazily at a slow/static cadence if Phase 1 accepts matching fields. Reserved bits are masked. No fallback is required for an optional value, but `Unavailable` and status are required. |
| `NtQuerySystemInformation` opaque classes | `SystemInterruptInformation`, `SystemExceptionInformation`, `SystemLookasideInformation`, and `SystemPolicyInformation` are recorded in the matrix but not published from currently documented opaque/reserved bytes. | They become usable only when a current Microsoft header or symbol-backed layout gives stable field meaning on every supported architecture. Merely returning `STATUS_SUCCESS` is insufficient evidence. |
| `NtQueryInformationProcess` documented classes | Use `ProcessBasicInformation` (0) for exit status, affinity, base priority, PID, and parent PID without dereferencing `PebBaseAddress`; `ProcessWow64Information` (26) for WOW64 state; `ProcessBreakOnTermination` (29) for optional critical state; and `ProcessSubsystemInformation` (75) for subsystem type. | Open at most one transient process handle at a time and issue the fixed approved class list only for fields absent from the bulk row. Tier B uses `GetExitCodeProcess`, `GetProcessAffinityMask`/group APIs, `IsWow64Process2`, `IsProcessCritical`, and `GetProcessInformation` equivalents where available. |
| `NtQueryInformationProcess` additional stable classes | Prototype the Microsoft-native classes underlying I/O counters, VM counters, times, handle count, session, cycle time, priority class/boost, page priority, and I/O priority. Adopt a class only when its current value, layout, access rights, and x64/ARM64 behavior are pinned and it adds a field or measurably improves the fallback. | Do not repeat a per-process query for data already present and validated in `SystemProcessInformation`. Access denied makes only that value unavailable. The per-sample call count and handle lifetime remain bounded and measured at 2,048 rows. |

`ProcessDebugPort`, `ProcessImageFileName`, `ProcessTelemetryIdInformation`, PEB dereferencing, parameters, command
line, environment, and debug-object/handle classes remain excluded by the existing privacy and scope rules.
Set-information operations and other control APIs that change priority, affinity, throttling, mitigation, break
behavior, memory, or device state are forbidden.

The System Data project must not include `Wbemidl.h`, link `wbemuuid.lib`, call `CoCreateInstance` for WMI, spawn
`wmic.exe`/PowerShell, or make a WinRT CIM query. Validation inspects the DLL imports and compares loaded modules
before and after activating every dataset; `wbemprox.dll`, `fastprox.dll`, and `wbemcomn.dll` must not be loaded by
System Data acquisition.

### CPU, memory, and process acquisition

One due CPU/memory/process group begins with one QPC timestamp, then performs only the raw calls needed by the
requested datasets. It finishes with a second QPC timestamp and assigns the midpoint to every snapshot materialized
from that group. `system.summary`, `cpu.summary`, `cpu.logical`, and `memory.summary` share the cheap system-information
calls. `process.list` and `thread.list` add the bulk `SystemProcessInformation` sample only when at least one of those
datasets is due. A 1 s summary subscription must not force the 2 s process walk.

| Information | Primary acquisition | Conversion and fallback |
| --- | --- | --- |
| Global and per-logical CPU time | Dynamically resolve `NtQuerySystemInformation` from the already mapped System32 `ntdll.dll`; request `SystemProcessorPerformanceInformation` into a fixed array sized for 1,024 active processors. The private record names idle, kernel, user, DPC, interrupt time, and interrupt count while matching the current SDK/native layout. | Require a returned byte count that is a whole number of records and equals the current active-processor count. Map array order to the cached group-major `(group, index)` topology only after a Phase 0 cross-check. Compute `total = kernel + user`, `busyKernel = kernel - idle`, and all percentages from unsigned deltas. If the native path is rejected, `GetSystemTimes` supplies only `cpu.summary`; `cpu.logical`, DPC, and interrupt fields use the counter fallback or become unavailable. |
| Topology | On source creation and topology invalidation, call `GetLogicalProcessorInformationEx(RelationAll)` into a bounded reusable byte buffer. Parse every variable-sized record for groups, cores, packages, dies/modules when present, caches, and NUMA affinities. Call `GetSystemCpuSetInformation` for CPU-set ID, efficiency class, parked, allocated, and realtime flags. | Validate every record `Size`, group count, mask, and buffer boundary. Identity is always `(Group, LogicalProcessorIndex)`; CPU-set ID is metadata, not row identity. Refresh when active group/count changes or a CPU-set query reports a different required size. |
| CPU frequency and limits | Call `CallNtPowerInformation(ProcessorInformation)` into a fixed `PROCESSOR_POWER_INFORMATION` array for current, maximum, and thermally limited MHz. Optionally query `KMTQAITYPE_NODEPERFDATA` only for GPU engines, never for CPUs. | Treat current MHz as a point-in-time throttle estimate, not measured effective work. If processor-to-group mapping cannot be proven on a multi-group host, publish only aggregate frequency or unavailable per-row values. A cached `Processor Information(*)` counter query may provide effective-frequency fields after measurement. |
| System totals and memory | Query the accepted `SystemBasicInformation` and `SystemPerformanceInformation` layouts once per due cheap native batch. Use checked 64-bit page-to-byte conversions for physical, commit, cache, paged/nonpaged pool, kernel, paging, and fault fields. Process and thread totals come from validated `SystemPerformanceInformation` fields; handle totals come from `SystemHandleCountInformation` when available. Do not walk `SystemProcessInformation` to fill `system.summary`. | Tier B calls `K32GetPerformanceInfo` once and uses `GlobalMemoryStatusEx` as its physical/pagefile fallback. A native/public disagreement beyond the sampling tolerance below increments a diagnostic, disables the failing native field or layout, and never averages incompatible values. |
| Deep system rates | Use the validated `SystemPerformanceInformation` fields for cumulative page faults, page reads/writes, context switches, system calls, cache activity, pool activity, and other accepted counters. Query `SystemTimeOfDayInformation` for boot/current/sleep-bias timing and `SystemRegistryQuotaInformation` for allowed/used bytes on the slower applicable cadence. | Accept only fields whose offsets, units, and monotonic or point-in-time behavior pass Windows 10/11 x64/ARM64 fixtures. Otherwise use the Tier B API or a lazy Tier C Memory/System counter query, or mark only that field unavailable; the base row still succeeds. |
| Bulk process and thread rows | Call `NtQuerySystemInformation(SystemProcessInformation)` into a reusable bounded byte arena. Start at 1 MiB, retry `STATUS_INFO_LENGTH_MISMATCH` only up to the ratified cap, and never allocate during steady state. Inventory and consume every validated non-reserved process field plus the embedded thread records that map to subscribed datasets: identities, priorities, times/cycles, state/wait reason, context switches, counts, faults, memory/pool, and I/O counters. | Walk `NextEntryOffset` with checked addition and alignment, validate the complete thread-array extent and every `UNICODE_STRING`, and special-case Idle/System names. Process identity is `(PID, CreateTime)` and thread identity is `(PID, CreateTime, TID)`, so reuse resets deltas. Materializing `thread.list` reuses the same buffer. On native rejection, use the Tier B bounded Toolhelp snapshot and one transient process or thread handle at a time for documented fallback calls. |
| Targeted process native queries | Resolve `NtQueryInformationProcess` once and, for each accessible process, issue the fixed approved information-class list only for unique fields absent from the validated bulk record. Begin with `ProcessBasicInformation`, `ProcessWow64Information`, `ProcessBreakOnTermination`, and `ProcessSubsystemInformation`; add stable private classes from the coverage matrix only after their gate passes. | Reuse the same one-at-a-time transient handle across that process's queries, request the minimum read rights, validate exact returned lengths and enums, and stop querying after process exit or invalid-handle status. Never dereference the returned PEB pointer or request excluded path, command-line, environment, telemetry, or debug data. Tier B supplies documented equivalents; per-value access denied remains local. |

Process CPU is `delta(user + kernel) / (elapsedQpc * activeLogicalProcessorCount) * 100`, after converting the QPC
interval to 100 ns. Thus one fully busy logical processor on an eight-logical-processor machine reports about 12.5%,
preserving Process Viewer semantics. Process I/O and page-fault rates use the same batch interval. A missing prior
`(PID, CreateTime)` sample is `Initializing`; a decreased counter or impossible time delta resets only that row.

Sampling tolerance for a native field versus its Tier B counterpart taken in the same QPC batch:

- Physical size, commit limit, and other slowly changing byte totals may differ by at most one native page (4 KiB on
  the primary machine, or the queried `PageSize`) or 0.1% of the larger value, whichever is greater.
- Racy object counts (process, thread, handle) may differ by at most 25% of the larger value on a quiet sample; a
  native zero against a non-zero public count, or an order-of-magnitude mismatch, fails the native field.
- Percentages computed over the same interval may differ by at most 1.0 absolute.
- Unit, signedness, or monotonicity errors fail the native field regardless of magnitude.
- On failure, increment a diagnostic, disable that native field or layout, and keep the Tier B or `Unavailable`
  value. Never average the two.

### Network and storage acquisition

| Information | Acquisition recipe | Correctness and lifetime |
| --- | --- | --- |
| Interface inventory | Call `GetIfTable2Ex(MibIfTableNormal)` only on first subscription or topology invalidation. Copy at most 256 records into fixed storage and release the OS table with `FreeMibTable`. Register one `NotifyIpInterfaceChange(AF_UNSPEC, ...)` callback while a network dataset is active. | Stable key is `InterfaceLuid`; index and alias are mutable attributes. The callback only sets an atomic dirty flag and signals the host change event. `CancelMibChangeNotify2` drains registration during teardown. |
| Interface hot sample | For each cached LUID call `GetIfEntry2` into a stack `MIB_IF_ROW2`. Read operational/media state, MTU, receive/transmit link speed, octets, packet classes, errors, and discards. | Rates use unsigned 64-bit counter deltas and QPC. Utilization is `8 * max(rxBytesPerSecond, txBytesPerSecond) / linkSpeed * 100` for full-duplex links; publish unavailable when speed/duplex semantics are unknown. An absent LUID marks topology dirty and does not abort other rows. |
| Protocol totals | Call `GetIpStatisticsEx`, `GetTcpStatisticsEx2` when available (otherwise `GetTcpStatisticsEx`), and `GetUdpStatisticsEx2` when available (otherwise `GetUdpStatisticsEx`) once for AF_INET and once for AF_INET6 as supported. | Store raw cumulative values and derive rates with width-aware wrap/reset handling. Do not call endpoint-table APIs such as `GetExtendedTcpTable` or `GetExtendedUdpTable`. |
| Physical disk inventory | Enumerate bounded `\\.\PhysicalDriveN` candidates derived from volume extents and SetupAPI disk interfaces. Open read-only/shared handles only while `storage.disk` or storage temperatures are active. Cache device number and bounded product identity from `IOCTL_STORAGE_QUERY_PROPERTY`; query length with `IOCTL_DISK_GET_LENGTH_INFO`, seek penalty/trim capability with their storage properties, and never publish the hardware serial number. | Close handles on inactivity if their measured reopen cost is acceptable, and always on topology invalidation/source destruction. Every variable descriptor offset and `Size` is bounds checked. Access denied yields an identity-only or unavailable row. |
| Physical disk activity | Issue `IOCTL_DISK_PERFORMANCE` once per cached disk. Delta `BytesRead`, `BytesWritten`, `ReadCount`, `WriteCount`, `ReadTime`, `WriteTime`, and `IdleTime`; copy instantaneous `QueueDepth` and delta `SplitCount`. | Bytes/operations per second use QPC. Average latency is the respective time delta divided by completed-operation delta. Active percentage is derived from idle-time delta and elapsed time, never by adding read and write percentages. Detect reset or device-number reuse before deriving. This query can enable system-wide disk counters: Phase 0 must measure that side effect and reject the backend if unacceptable. RedXe never issues `IOCTL_DISK_PERFORMANCE_OFF`, because it cannot prove exclusive ownership; access or policy failure selects the counter fallback. |
| Volume inventory/capacity | Enumerate volume GUID paths with `FindFirstVolumeW`/`FindNextVolumeW`; retrieve mount paths, file-system metadata, and capacity/free space with the volume/file APIs. Open a read-only volume handle only to obtain `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`. | A multi-extent or dynamic volume retains every bounded disk mapping. Whole-disk activity is not copied into a volume row. Publish volume activity only if Phase 0 proves an exact stable LogicalDisk counter instance mapping across rename, mount, and locale changes. |
| Storage temperature | On the low-rate sensor cadence, issue `IOCTL_STORAGE_QUERY_PROPERTY` with `StorageDeviceTemperatureProperty`. Parse `STORAGE_TEMPERATURE_DATA_DESCRIPTOR` and its bounded `STORAGE_TEMPERATURE_INFO` array. | `STORAGE_TEMPERATURE_VALUE_NOT_REPORTED` is unavailable. Preserve signed Celsius, sensor index, warning/critical thresholds, and source disk key. Never issue `IOCTL_STORAGE_SET_TEMPERATURE_THRESHOLD` or a protocol-specific SMART command in this plan. |

### GPU acquisition

GPU identity is LUID-first. DXGI and D3DKMT records are joined only by the same adapter LUID; display name, ordinal,
PCI ID, or enumeration order is never used as the join key.

| Information | Acquisition recipe | Correctness and fallback |
| --- | --- | --- |
| Adapter inventory/capacity | Create one DXGI factory lazily and enumerate `IDXGIAdapter1`/`IDXGIAdapter4`; cache `DXGI_ADAPTER_DESC1`/`DXGI_ADAPTER_DESC3`, LUID, flags, vendor/device IDs, and dedicated/system/shared capacities. Do not create a D3D device. Optionally resolve `DXCoreCreateAdapterFactory` from System32 `dxcore.dll`, enumerate hardware compute/graphics adapters, join `DXCoreAdapterProperty::InstanceLuid`, and read only properties whose `IsPropertySupported` and exact `GetPropertySize` checks pass: `IsHardware`, `IsIntegrated`, `IsDetachable`, KMD model, physical-adapter count, engine count, and engine names. | DXGI remains the compatibility inventory. DXCore can enrich a row or add a compute-only adapter but never creates a duplicate LUID; an unavailable property stays unavailable rather than being inferred from memory size. Refresh after device-change/removal signals or a D3DKMT removed status. `IDXGIAdapter3::QueryVideoMemoryInfo` may be exposed only as RedXe-process budget/usage and is not used for machine totals. |
| WDDM handles and capabilities | Resolve `D3DKMTEnumAdapters2`, `D3DKMTQueryAdapterInfo`, `D3DKMTQueryStatistics`, and `D3DKMTCloseAdapter` from System32 `gdi32.dll`. Enumerate into the fixed 32-adapter cap, join by LUID, retain the bounded D3DKMT handles while GPU datasets are active, and close every handle on refresh/destruction. Query and validate driver version, `KMTQAITYPE_ADAPTERTYPE`, physical-adapter count, `KMTQAITYPE_NODEMETADATA`, and performance-data capabilities once per topology generation. | Missing exports or a non-WDDM adapter disables only the D3DKMT backend. Record WDDM capability from query results rather than assuming an OS build implies driver support. Adapter classification comes from explicit DXCore/D3DKMT properties, never a dedicated-memory heuristic. |
| Clocks, power, temperature, fan | For WDDM 2.4-capable adapters, query `KMTQAITYPE_ADAPTERPERFDATA` and `KMTQAITYPE_ADAPTERPERFDATA_CAPS` once per physical adapter index; query `KMTQAITYPE_NODEPERFDATA` per bounded node for engine frequency/voltage. | Convert deci-Celsius and tenths-of-percent explicitly. A zero field is not automatically unavailable: capability, maximum, and repeated-sample checks determine whether zero is a valid off/idle value. GPU fan data describes only the driver's main adapter fan. |
| Engine utilization | Prototype guarded `D3DKMTQueryStatistics` node running-time deltas. Enumerate node count/metadata once, query each node once per sample, and divide its running-time delta by the common elapsed interval. | Some statistics records are marked reserved for system use even though the function and structures ship in the WDK; this Tier A path must pass layout and semantic fixtures. The fallback is one cached wildcard GPU Engine PerfLib/PDH query. Unexpected node/counter instances are skipped, not guessed. |
| Machine GPU memory | Prototype D3DKMT global segment statistics, distinguishing local/dedicated from non-local/shared segments using validated segment metadata. Sum resident/committed values only when scope and units are proven. | Fallback is the GPU Adapter Memory counter set. If neither path proves machine scope, capacity remains good while current machine usage is unavailable. RedXe-process DXGI budget data is never relabelled as machine use. |
| Per-process GPU | Prefer one batched GPU Engine and GPU Process Memory counter collection, parse PID/LUID/engine identifiers with a strict grammar, and join PID to the current `(PID, CreateTime)` table. | A D3DKMT process statistics prototype is admitted only with a bounded query count proportional to active returned GPU entities, not `processes × adapters × nodes`. PID-only data that cannot be protected from reuse is dropped. |

The PerfLib/PDH fallback owns one query per active domain, adds language-neutral counters with
`PdhAddEnglishCounterW` or counter-set GUID/ID through PerfLib V2, and collects all of that domain's counters in one
call. It never uses `PdhCollectQueryDataEx`, because that API owns an extra periodic thread. It pre-sizes bounded raw
and formatted arrays during warm-up, handles `PDH_MORE_DATA` only up to the domain cap, and rebuilds a query only after
instance churn or invalid-data status. Phase 0 selects direct PerfLib V2 only when it materially improves CPU,
allocation, or instance matching over cached PDH.

### Power, battery, thermal, and fan acquisition

| Information | Acquisition recipe | Availability rules |
| --- | --- | --- |
| System power | Call `GetSystemPowerStatus` for AC/DC, aggregate battery flags/percentage/time, charging, and battery saver. Cache `GetPwrCapabilities` output at source creation and after resume. | Preserve documented unknown sentinels (`255` and `0xFFFFFFFF`) as unavailable. No source-owned power notification is needed; the host's existing power/resume state can invalidate the next subscribed sample. |
| Battery inventory and static fields | Enumerate present `GUID_DEVCLASS_BATTERY`/`GUID_DEVICE_BATTERY` interfaces with SetupAPI. Open bounded read-only/shared overlapped handles, issue `IOCTL_BATTERY_QUERY_TAG`, then `IOCTL_BATTERY_QUERY_INFORMATION` for `BatteryInformation`, device name/ID, and other selected fields. | A battery tag can change; any invalid-tag result causes a bounded tag refresh. Capacity units are mWh/mW only when `BATTERY_CAPACITY_RELATIVE` is clear; relative values stay explicitly relative. Serial number is not published. |
| Battery dynamic fields | Issue `IOCTL_BATTERY_QUERY_STATUS` for capacity, voltage, signed rate, and power state. Use `IOCTL_BATTERY_QUERY_INFORMATION` with `BatteryTemperature` and `BatteryEstimatedTime`. | Convert tenths Kelvin with `C = K/10 - 273.15`; reject values below absolute zero and implausible device readings. Honor `BATTERY_UNKNOWN_*` sentinels and miniports that return zero for unsupported cycle count. |
| ACPI thermal zones | Prototype SetupAPI enumeration of `GUID_DEVICE_THERMAL_ZONE` and a read-only/shared overlapped handle. Query static trip information with `IOCTL_THERMAL_QUERY_INFORMATION`, then pass a validated `THERMAL_WAIT_READ` with `Timeout = THERMAL_WAIT_READ_TIMEOUT_IMMEDIATE` to `IOCTL_THERMAL_READ_TEMPERATURE` where user-mode access is granted. | Treat the interface as optional even when the devnode exists. Validate returned sizes, active-trip count, immediate completion/cancellation, and tenths-Kelvin conversion. If the driver rejects user-mode access, does not support cancellation, or exceeds the device-lane budget, publish no ACPI-zone rows and report unsupported/backoff; do not fall back to `MSAcpi_ThermalZoneTemperature`. |
| Other temperature rows | Re-project already collected GPU, storage, and battery readings into `thermal.sensor` with a stable source-prefixed key; do not re-query the devices. | A physical reading appears once per source sensor. `gpu.adapter`, `storage.disk`, or `battery.list` may expose the same value for domain convenience, but the batch must collect it once. |
| Fans | Publish GPU fan RPM/max RPM from the same D3DKMT adapter data. Probe `GUID_DEVICE_FAN` only to determine whether a generic fan device is present and to inform `source.status`. | The Windows thermal framework can expose fan presence/on-off behavior without a general user-mode RPM query. Presence alone does not create an RPM row. No `_FST`/`_DSM` evaluation, fan write, or WMI fallback is permitted. |

Every read-only `DeviceIoControl` that can pend uses an overlapped handle, one reusable event, a finite deadline, and
`CancelIoEx` on timeout. The backend is disabled for the source if cancellation and completion cannot be drained
within the teardown budget. Optional device telemetry is never allowed to make source destruction unbounded.

### Batch acquisition contract

Subscriptions remain per provider and dataset. The pre-production host-to-source ABI is a bounded batch request/result
(`RedXeDataCollectRequest` 24 bytes, `RedXeDataCollectResult` 32 bytes, at most 32 unique IDs, `IRedXeDataSource` IID
`C3A81F6E-2D47-4B90-A1E5-6F8C9D0B3E21`):

- `PluginHost` gathers each unique due dataset ID for one source, orders IDs deterministically, and calls the source
  once for that due-time group;
- the request contains only validated module-owned IDs and a bounded count;
- the result contains zero or one borrowed snapshot for every request, plus a common collection sequence and
  timestamp so correlated values can be compared;
- the source collects each required raw domain once, then materializes the requested snapshots from that raw sample;
- a failed optional collector does not fail unrelated results; it publishes unavailable values/status and enters
  bounded backoff; and
- host fan-out, sink borrowing, drain, callback reentry prohibitions, and identical-dataset coalescing remain intact.

Phase 1 revises the current pre-production IID and exact-size records in place, updates contract tests, and adds a
source batch limit of 32. The public subscription cap remains 32 until a fixed-storage and configured-page capacity
test proves a 128-slot cost. At most 64 unique active dataset/cadence groups may be scheduled. No `Stale` quality and
no device-I/O thread ship in Phase 1.

### Collector layout

`Plugins/SystemData` is split into private collectors with no public C++ ABI:

- `CpuCollector` owns topology, cumulative processor samples, frequency counters, and CPU rates;
- `MemoryCollector` owns physical, commit, cache, pool, and paging samples;
- `ProcessCollector` owns the bounded current/previous process and thread tables, the Tier A bulk/targeted native
  adapters, and Tier B process/thread fallback;
- `NetworkCollector` owns cached interface identity, one prior counter sample per interface, and protocol totals;
- `StorageCollector` owns disk/volume identity, counter handles, and one prior sample per entity;
- `GpuCollector` owns adapter identity, counter-instance maps, and adapter/engine/process snapshots;
- `PowerCollector` owns system power, battery handles/tags, and battery state; and
- `DeviceSensorCollector` coordinates only the read-only ACPI-zone, storage-temperature, and generic-fan capability
  probes. GPU and battery collectors remain the owners of their readings and lend them to the thermal projection.

Each collector receives an injected private platform-function table in tests. Production binds that table to Windows
APIs. Collectors use fixed-capacity arrays, reusable byte/string arenas, stable entity keys, and two-table generation
swaps where entities appear and disappear. They retain one previous sample only; no graph history belongs in the DLL.

### Fast and bounded device acquisition lanes

CPU, memory, process, network, ordinary storage, GPU, and system power use the existing fast event-blocked host lane.
Cached disk, battery, and thermal IOCTLs enter that lane only after their p95 and worst observed latency satisfy the
fast budget. Otherwise add one optional shared device-I/O lane with these constraints:

- create it lazily only when a dependent dataset first becomes active, share it across all device-lane local
  datasets/providers, and block indefinitely when none is due;
- identify device-lane datasets through a validated descriptor flag; a source cannot choose or create its own thread;
- use only overlapped read/query IOCTLs with a finite deadline, `CancelIoEx`, bounded drain, and exponential failure
  backoff from 10 seconds to 10 minutes;
- publish the last sample as `Stale` only if the Phase 1 quality-model decision adds that state; otherwise publish
  `Unavailable` and the last-success timestamp in `source.status`;
- never make battery, storage-health, thermal, or fan availability a prerequisite for source creation, host startup,
  or test success; and
- do not ship the device lane or a backend until timeout, cancellation, and teardown/drain time are bounded and
  measured. A driver surface that cannot satisfy that gate remains unsupported.

Neither lane busy-polls. Hidden, suspended, display-off, and no-subscriber states schedule no collection. Device and
interface notifications may mark cached topology dirty, but callbacks perform no sampling, allocation, host reentry,
or UI work and must drain before source destruction.

### Backend policy and decision gates

1. Attempt the applicable validated Tier A native/WDK bulk surface first. On missing symbol/class, unsupported build,
   rejected layout, access failure, invalid semantics, or bounded-buffer failure, disable that adapter and select its
   Tier B fallback without failing unrelated fields. A native class that duplicates Tier B data and is measurably
   slower may be recorded as `duplicate` rather than shipped; unique validated native fields remain eligible.
2. Use language-neutral counter lookup and enumerate instances instead of constructing localized names. Parse GPU or
   disk instance strings only through a strict tested grammar; an unknown form is unavailable, never heuristically
   joined.
3. Measure PDH and PerfLib V2 setup, warm collection, formatting, instance churn, heap, loaded-module, and
   private-memory cost. Cache queries and pre-size bounded result buffers after warm-up. Neither counter API is a
   fallback for WMI.
4. The Tier A bulk process, per-logical-CPU, system-performance, targeted process, and D3DKMT backends remain private
   adapters loaded by symbol at runtime where appropriate. Adoption requires Windows 10/11 x64 and ARM64 coverage,
   strict returned-length/record-chain validation, graceful Tier B fallback on any mismatch, and bounded measured
   cost. Every current SDK/WDK information class receives an explicit coverage-matrix disposition.
5. DXGI adapter memory queries may report the RedXe process budget/usage, but machine-wide GPU memory and utilization
   are published only from performance data whose scope is verified.
6. Thermal, battery, storage, and GPU device surfaces are capability probes, not guaranteed sensors. Empty, stale,
   constant, nonsensical, privileged, unit-ambiguous, or non-cancellable readings are rejected or unavailable.
7. No third-party or vendor sensor dependency is added in this plan. A future provider DLL may implement the same
   datasets after separate dependency, license, security, and resource review.

### Counter semantics and correctness

- All deltas use monotonic cumulative counters and the actual elapsed QPC interval. A missing baseline reports
  `Initializing`; reset, wrap, entity reuse, or non-positive elapsed time starts a new baseline.
- Global CPU and each logical CPU are 0–100% of their respective capacity. Process CPU remains 0–100% of total machine
  capacity so Process Viewer ranking remains compatible.
- CPU identity is `(processorGroup, groupRelativeIndex)`; legacy flat indices are not used on machines with more than
  64 logical processors. Topology records include CPU-set ID and efficiency class where present.
- Network/storage/GPU entity identity uses stable OS identifiers, not localized display strings or row order.
- Rates are derived after detecting counter resets. Link/disk/GPU utilization is unavailable when its denominator or
  scope is unknown rather than clamped into a plausible-looking value.
- Rows have deterministic stable-key order. A cap sets `RedXeDataSnapshotFlagTruncated`; selection is deterministic
  and `source.status` increments the truncation count.
- One batch timestamp applies to snapshots derived from the same raw acquisition. Individual collector duration is
  diagnostic only and never presented as sampling time.

### Privacy and failure isolation

All data remains local, transient, read-only, and collected with the current token. The source performs no network or
disk output and never persists snapshots. Process, thread, and GPU-process rows, adapter aliases/descriptions, volume
names, battery IDs, and sensor IDs are `LocalSensitive`. MAC addresses, IP addresses, remote endpoints, executable
paths, command lines, user names, and wireless network names are not collected.

One inaccessible process, missing counter set, native-layout rejection, device removal, driver reset, IOCTL timeout, or
malformed optional record must not fail the source or unrelated datasets. Repeated failures use bounded backoff and
appear in `source.status`; error text is a static bounded category, not formatted on every sample.

## Resource budget

These are acceptance ceilings, not allocation targets:

- fast collector writable storage: at most 16 MiB per `builtin.system-data` source on x64 and ARM64;
- optional device/sensor buffers: at most 1 MiB source-owned storage; measure SetupAPI, DXCore, PDH/PerfLib, D3DKMT,
  and opened device-handle private-byte/handle cost separately;
- borrowed batch output: fixed storage included in those limits, with no per-subscriber snapshot copies in the host;
- steady hot collections after warm-up: zero plugin heap growth, zero handle growth, no process handle retained, and no
  logging or string formatting allocation. Active device datasets may retain at most one read-only handle per bounded
  disk, volume, battery, thermal-zone, or GPU adapter entry;
- process collection: at most one transient process handle at a time, reused for the fixed Tier A targeted-query list
  and the Tier B fallback before advancing to the next process;
- complete fast batch at default cadences: p95 acquisition time below 25% of its shortest interval and RedXe average
  CPU below 0.5% of total machine capacity on the primary reference machine in
  [`SystemDataMetricsExpansion_Phase0Evidence.md`](SystemDataMetricsExpansion_Phase0Evidence.md)
  (Windows 11 Pro `10.0.26200` x64, Ryzen 9 9950X3D 32 logical processors, RTX 5080 plus AMD iGPU, no battery);
- CPU/memory-only batch: p95 below 2 ms; process batch at the 2,048-row cap: p95 below 25 ms; process plus subscribed
  `thread.list` materialization at the 8,192-thread cap: p95 below 35 ms; each network, storage, or GPU domain: p95
  below 10 ms, with failures causing backoff instead of repeated overruns;
- one fast-lane wake-up per coalesced due-time group and, only when device-lane data is active, one device-lane wake-up
  per coalesced device group; and
- inactive host: zero collections and periodic wake-ups; first subscription after inactivity reports `Initializing`
  where a prior counter baseline is no longer valid.

Phase 0 records measured distributions on that primary machine and on the additional hosts listed in the evidence file.
Any ceiling that proves unrealistic must be changed in this plan with evidence before implementation is accepted; code
must not silently relax it. The fast-lane ceiling is **16 MiB** after the envelope: compact previous samples, NT walk
cap 4 MiB, `thread.list` 8,192 truncates on the primary host. The current shipped source object remains under 1 MiB
until `thread.list` lands.

## Phase 0 working protocol

Phase 0 is the first development work. It does not implement production collectors until the evidence-file
ratification checklist is complete. Spike programs are throwaway unless a result
must stay reproducible; then they belong in a later test project, not the plugin DLL.

Execute against the artifacts above in this order:

1. Re-record the shipped Release x64 baseline with `.\.build\x64\Release\SystemDataTests.exe --benchmark` (three warmed
   runs) into the evidence file.
2. Fill the native-class matrix from SDK `10.0.26100.0` / WDK headers and the legacy code extras.
3. Spike cheap totals versus a process walk; keep 1 s summary only if the cheap path gates `keep`.
4. Spike NT CPU/process, optional `NtQueryInformationProcess` extras at 2,048 rows, PDH versus PerfLib V2, DXGI/D3DKMT,
   `IOCTL_DISK_PERFORMANCE` side effect, and (on available hardware) battery/thermal/fan cancellation. Prove no WMI
   module load.
5. Fill the storage envelope and freeze `source.status`, CPU/memory/process/thread, and batch-ABI columns in the
   evidence file, then copy them into this plan.

Phase 1 starts only when the evidence file’s ratification checklist is complete. Until then, `RedXeDataQuality` stays
`Good` / `Unavailable` / `Initializing`, and the host subscription cap stays 32. A `Stale` quality value and a 128-slot
cap remain Phase 1 decisions gated by device-lane and capacity measurements.

The primary reference machine is the Windows 11 Pro `10.0.26200` x64 desktop recorded in the evidence file (Ryzen 9
9950X3D, 32 logical processors in one group, 64 GiB-class RAM, AMD iGPU plus RTX 5080, no battery, Hyper-V present).
Battery, ACPI-zone, and fan-presence closeout still need a laptop; WARP/no-sensor closeout still needs a VM; ARM64
needs compile plus layout-size proof. This AMD dual-CCD host does not replace an Intel P/E-core efficiency-class check.

## Implementation plan

### Phase 0 — Baseline, inventory, and backend spikes

- [x] Complete the measurement items in `SystemDataPlugin_2026-08-31.md` and archive that plan when its exit criteria
      pass.
- [x] Name the primary reference machine, sampling tolerance, summary-cadence coupling, affinity rule, and working
      artifact paths in this plan plus
      [`SystemDataMetricsExpansion_Phase0Evidence.md`](SystemDataMetricsExpansion_Phase0Evidence.md) and
      [`SystemDataMetricsExpansion_NativeClassMatrix.md`](SystemDataMetricsExpansion_NativeClassMatrix.md).
- [x] Record current `system.summary` and `process.list` CPU time, wake-ups, heap deltas, private bytes, handles, and
      row-cap latency in Release x64 on the primary machine (`SystemDataTests.exe --benchmark`, three warmed runs).
- [x] Inventory every `SYSTEM_INFORMATION_CLASS` and `PROCESSINFOCLASS` in SDK `10.0.26100.0` `um\winternl.h`. WDK km
      headers were not installed; non-SDK classes remain pending in the matrix.
- [x] Inventory additional classes from the selected WDK. WDK `km` headers are not installed; legacy code extras
      remain `unknown-layout` and are not probed by guessed number.
- [x] Prototype the SDK Tier A system/process class set (`SystemHandleCountInformation`,
      `SystemProcessorPerformanceInformation`, `SystemProcessInformation`, `SystemBasicProcessInformation`,
      `SystemBasicInformation`, opaque `SystemPerformanceInformation` / `SystemTimeOfDayInformation`, and the public
      `NtQueryInformationProcess` trio). Compare with `K32GetPerformanceInfo`, `GetSystemTimes`, and Toolhelp. WDK
      private process classes are **out**.
- [x] Prove `system.summary` process/thread/handle totals from `SystemHandleCountInformation` (gate `keep`).
- [x] Prototype DXGI+optional-DXCore+D3DKMT identity and adapter performance on the primary iGPU+discrete+on-box WARP
      host. A dedicated WARP-only VM remains Phase 4 validation.
- [x] Probe battery, ACPI thermal-zone, storage-temperature, and generic-fan interfaces on the primary desktop (empty
      battery, unusable ACPI tenths-K=0, storage+GPU thermal keep). Confirm no WMI module load. Laptop remains Phase 5
      validation. Hung-IOCTL drain was not proven; no device-I/O thread in Phase 1.
- [x] Fill the storage envelope; ratify dataset columns, row caps, cadences, compact previous samples, 16 MiB fast-lane
      ceiling, 4 MiB NT buffer cap, and copy the freeze into this plan.

### Phase 1 — Contract, batching, status, and test seams

Start only after the Phase 0 ratification checklist in the evidence file is complete.

- [x] Add exact batch request/result records (`RedXeDataCollectRequest` 24, `RedXeDataCollectResult` 32), common
      sequence/timestamp semantics, the device-lane dataset flag, and keep three-valued quality in `Data.h`; revise
      the `IRedXeDataSource` IID in place.
- [x] Update `PluginHost` to group unique due datasets by source, order IDs with `source.status` last, call once,
      validate every result, and fan out only the matching snapshot.
- [x] Leave the shared device-I/O lane unimplemented; optional device fields stay unsupported or slow-cadence on the
      fast lane with status.
- [x] Add `source.status`. Collector interfaces, reusable arenas, entity-key helpers, and injected platform functions
      wait for Phase 2 domain collectors rather than unused scaffolding.
- [x] Add contract tests for malformed batches, duplicates, missing results, common timestamps, and descriptor flags.
      Subscription capacity remains 32; existing drain/reentry/quiescence tests remain in `HostPluginTests`.
- [x] Reconcile the selected ABI, lifetime, batching, availability, and lane rules into `Plugins_API.md` and the resource
      contract in the same change.

### Phase 2 — CPU, memory, and process depth

- [x] Implement processor-group-aware topology identity `(group, index)` and `cpu.summary`/`cpu.logical` from
      `SystemProcessorPerformanceInformation` with `GetSystemTimes` remaining on `system.summary`. DPC/interrupt,
      frequency, and CPU-set efficiency/parked fields stay Unavailable until those surfaces are filled. Hybrid P/E and
      >64-processor coverage remain later validation.
- [x] Implement `memory.summary` from `K32GetPerformanceInfo`, sharing physical/commit values with `system.summary`.
      Cache/page-in/out rates stay Unavailable while `SystemPerformanceInformation` is opaque.
- [x] Enrich `process.list` with append-only columns (parent, session, times, I/O, faults, create time, cycles,
      priority, single-group affinity, WOW64/architecture, critical). Subsystem stays Unavailable without WDK layout.
- [x] Implement bounded `thread.list` from the same `SystemProcessInformation` buffer (identity, priority, state/wait).
      Times/CPU/createTime/context switches stay Unavailable while SDK fields are Reserved. Cap 8,192 truncates.
- [x] Default process/CPU path is Tier A NT with Toolhelp/`GetSystemTimes`/`K32GetPerformanceInfo` fallback. Parent PID
      uses `SystemBasicProcessInformation`.
- [x] One `CollectSnapshots` batch runs the cheap NT sample and/or the process walk at most once.

### Phase 3 — Network and storage

- [x] Implement cached interface discovery, `GetIfEntry2` hot sampling, topology-dirty notification, and
      `network.interface` reset/rate/utilization logic.
- [x] Implement aggregate `network.protocol` statistics without enumerating endpoints or connections.
- [x] Implement disk/volume identity separately from counters, reject unstable instance mappings, and publish
      `storage.disk`/`storage.volume` with deterministic missing-device behavior.
- [x] Test interface renames, disconnect/reconnect, same-count replacement, counter reset, disk removal, volumes without
      mount points, cap truncation, and no-data hosts. `SystemDataTests` covers LUID identity/sort, first-sample
      Initializing rates, elapsed Good rates when counters exist, no MAC/IP columns, six protocol rows, volume GUID
      identity, and a shared-sequence batch. Live rename/removal is specified as a valid empty or `Unavailable`
      snapshot; a second catalog shape is not required. Live churn hosts remain follow-on hardware.

### Phase 4 — GPU

- [x] Enumerate adapters with stable LUIDs and implement `gpu.adapter` identity/capacity without creating a D3D device
      per sample.
- [x] Discover and cache GPU performance instances, implement `gpu.engine`, and derive busiest-engine adapter
      utilization with verified 0–100% semantics. Node utilization stays Unavailable (D3DKMT statistics reserved);
      adapter utilization is therefore also Unavailable rather than a guessed sum.
- [x] Implement `gpu.process` only where PID/adapter/engine mappings are parseable and tested; otherwise publish
      unavailable status rather than heuristics. Dedicated/shared GPU memory stays Unavailable without a proven
      machine-scope mapping. DXCore `IsIntegrated` is not shipped (GUID link / optional enrichment); `integrated` is
      Unavailable.
- [x] Test hardware GPU, multiple adapters, WARP/software-only, driver reset, disappearing instances, no counter set,
      cap truncation, and RedXe-process versus machine-wide memory scope. `SystemDataTests` covers non-empty DXGI
      adapters sorted by LUID, DXGI `software` Good 0/1 without requiring a WARP-only host, adapter utilization and
      machine-wide memory `Unavailable`, engine utilization `Unavailable`, engine/process collect success, and the
      2,048 process cap declaration. A dedicated WARP-only VM is follow-on, not a missing dataset.

### Phase 5 — Power, battery, thermal, and fan

- [x] Implement `power.summary` with documented synchronous APIs and no new periodic work.
- [x] Implement SetupAPI battery discovery, tag refresh, read-only battery query/status IOCTLs, unit/sentinel handling,
      and per-value quality for optional miniport fields.
- [x] Device-lane gate did not pass; ACPI-zone and storage-temperature IOCTLs run on the collect thread with the same
      overlapped timeout/`CancelIoEx` drain as disks. ACPI tenths-K=0 is not published.
- [x] Project already collected GPU, battery, and storage readings into `thermal.sensor`; publish `fan.sensor` only for
      D3DKMT GPU RPM (`MaxFanRpm != 0`). Generic `GUID_DEVICE_FAN` is presence-only.
- [x] No WMI fallback. Descriptors remain discoverable when a domain is empty (AC-only battery table, no ACPI rows).
- [x] Test AC-only desktop, battery laptop, unsupported VM, access denied, invalid battery tag, IOCTL timeout/cancel,
      malformed values, sensor disappearance, GPU fan absent/stopped, and generic fan presence without readable RPM.
      `SystemDataTests` covers one power row, AC-only `batteryPresent` 0 with a zero-row `battery.list`, no battery
      serial columns, thermal/fan projection when GPU reports temperature or max fan RPM, and empty battery collect
      success. Live laptop battery IOCTL is follow-on, not a missing dataset.

### Phase 6 — Integration, measurement, and closeout

- [x] Run descriptor/snapshot conformance over every dataset: exact sizes, unique IDs, types, units, row/value counts,
      quality, finite percentages, timestamps, deterministic order, and bounds. `SystemDataTests` `ValidateSnapshot`
      covers all 18 catalog datasets, unique IDs, and a prohibited-identity column scan.
- [x] Run synthetic delta tests for first sample, elapsed-time variation, reset, wrap, entity reuse, topology change,
      truncation, and failure backoff. First-sample `Initializing` and elapsed `Good` network rates are covered; GPU
      process first-sample `Initializing` remains; `thread.list` sets `Truncated` at 8,192. Live topology
      reset/wrap/entity-reuse churn is follow-on.
- [x] Measure Release x64 with each domain alone, all datasets in one batch, the row caps, hide/show, display off/on,
      and repeated create/destroy. The process row-cap Release `--benchmark` is recorded (15,920,696-byte source, zero
      heap/handle growth). `--domains` records per-dataset and full-catalog wall time and fails only on collect failure
      or a hang exceeding five seconds. Hide/show/display-off remain host Process Viewer tests. Repeated
      create/destroy is in `SystemDataTests`. A 128-subscription cap was not adopted; the host remains at 32.
- [x] Build Debug and Release x64 and Debug/Release ARM64; run `./format.ps1`, Debug and Release x64 `./test.ps1`, and
      `./validate-skills.ps1`.
- [x] Reconcile implemented dataset, backend, privacy, scheduling, quality, and resource rules for the 18 shipped
      datasets into `Plugins_API.md` and `Core_PerformanceAndResources.md`. Laptop battery, dedicated WARP-only VM,
      live adapter/disk churn, and live ARM64 runtime are explicit optional-absence follow-ons: descriptors stay valid
      with empty or `Unavailable` snapshots.
- [x] Move this plan to `Specs/Plans/Done/` and remove its WIP index row.

## Validation matrix

| Area | Required evidence |
| --- | --- |
| ABI and host | Factory/COM identity, exact-size records, batch validation, source/dataset coalescing, 128-slot bound if adopted, drain/reentry, and no subscriber copies. |
| Correctness | Fake cumulative samples with known answers, actual elapsed-time rates, first-sample quality, reset/reuse, stable ordering, truncation, and shared batch timestamp. |
| Compatibility | Existing Process Viewer receives the unchanged process fields and top-N behavior while other datasets are active. |
| Resource use | Release CPU duration/distribution, wake-ups, heap and private-byte deltas, handles, fixed storage, inactive quiescence, and optional SetupAPI/DXCore/PerfLib/PDH/D3DKMT/device-lane cost. |
| Platform | Windows 10 and 11, x64 and ARM64, single and multiple processor groups where available, hybrid CPU, physical and WARP GPU, desktop and battery systems, and an unsupported VM. |
| Failure isolation | Access denied, counter/provider missing, locale variation, device removal/reset, missing native symbol, unknown native layout, malformed native records, IOCTL timeout/cancellation, source teardown, and unaffected unrelated datasets/widgets. |
| WMI prohibition | Static imports/source scan plus runtime loaded-module delta prove that activating every dataset creates no WMI/CIM service and loads no WMI provider module. |
| Privacy | No persistence/network output and no prohibited process, endpoint, address, account, path, or wireless identity fields. |

Optional hardware absence is a passing result only when the descriptor remains valid, the snapshot/status reports the
absence truthfully, collection backs off, and unrelated metrics continue normally.

## Reference APIs

- Native CPU/process: [`NtQuerySystemInformation`](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation), [`NtQueryInformationProcess`](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryinformationprocess), [`GetLogicalProcessorInformationEx`](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlogicalprocessorinformationex), [`GetSystemCpuSetInformation`](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getsystemcpusetinformation), [processor groups](https://learn.microsoft.com/en-us/windows/win32/procthread/processor-groups), and [`PROCESSOR_POWER_INFORMATION`](https://learn.microsoft.com/en-us/windows/win32/power/processor-power-information-str).
- Memory/process fallback: [`PERFORMANCE_INFORMATION`](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-performance_information), [`GlobalMemoryStatusEx`](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-globalmemorystatusex), [`GetProcessTimes`](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes), and [`GetProcessMemoryInfo`](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo).
- Counters: [PDH consumer model](https://learn.microsoft.com/en-us/windows/win32/perfctrs/using-the-pdh-functions-to-consume-counter-data), [`PdhAddEnglishCounterW`](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhaddenglishcounterw), and [`PerfOpenQueryHandle`](https://learn.microsoft.com/en-us/windows/win32/api/perflib/nf-perflib-perfopenqueryhandle).
- Network: [`GetIfEntry2`](https://learn.microsoft.com/en-us/windows/win32/api/netioapi/nf-netioapi-getifentry2), [`MIB_IF_TABLE2`](https://learn.microsoft.com/en-us/windows/win32/api/netioapi/ns-netioapi-mib_if_table2), [`NotifyIpInterfaceChange`](https://learn.microsoft.com/en-us/windows/win32/api/netioapi/nf-netioapi-notifyipinterfacechange), and [IP Helper functions](https://learn.microsoft.com/en-us/windows/win32/iphlp/ip-helper-functions).
- Storage: [`IOCTL_DISK_PERFORMANCE`](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-ioctl_disk_performance), [`IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddvol/ni-ntddvol-ioctl_volume_get_volume_disk_extents), [`STORAGE_DEVICE_DESCRIPTOR`](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ns-winioctl-storage_device_descriptor), and [`STORAGE_TEMPERATURE_DATA_DESCRIPTOR`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddstor/ns-ntddstor-_storage_temperature_data_descriptor).
- GPU: [`DXCoreAdapterProperty`](https://learn.microsoft.com/en-us/windows/win32/api/dxcore_interface/ne-dxcore_interface-dxcoreadapterproperty), [`D3DKMTEnumAdapters2`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtenumadapters2), [`D3DKMTQueryAdapterInfo`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtqueryadapterinfo), [`D3DKMT_ADAPTER_PERFDATA`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_adapter_perfdata), [`D3DKMT_ADAPTER_PERFDATACAPS`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_adapter_perfdatacaps), [`D3DKMT_NODE_PERFDATA`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_node_perfdata), and [`IDXGIAdapter3::QueryVideoMemoryInfo`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo).
- Power/sensors: [`GetSystemPowerStatus`](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getsystempowerstatus), [enumerating battery devices](https://learn.microsoft.com/en-us/windows/win32/power/enumerating-battery-devices), [`BATTERY_INFORMATION`](https://learn.microsoft.com/en-us/windows/win32/power/battery-information-str), [`IOCTL_THERMAL_READ_TEMPERATURE`](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/ioctl-thermal-read-temperature), and [Windows thermal design guidance](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/design-guide).

## Exit criteria

This plan is complete only when the selected datasets are implemented or explicitly closed as unsupported, compatible
consumers continue to work, all active paths are subscription-driven and bounded, optional sensors cannot stall the
fast lane or shutdown, no WMI acquisition or WMI provider module is present, every accepted Tier A adapter has a
validated Tier B fallback or per-column unavailable behavior, the native information-class coverage matrix is
complete, Release measurements satisfy the ratified budgets, the full
validation matrix passes, and every durable rule is merged into the normative plugin and resource contracts.
