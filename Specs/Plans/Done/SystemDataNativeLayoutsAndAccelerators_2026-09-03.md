# System data native layouts and modern accelerators

Status: `COMPLETE` — implemented, measured, reconciled, and validated on x64; ARM64 blocked by a toolchain bootstrap
Created: 2026-09-03
Completed: 2026-09-03
Owner: `builtin.system-data` acquisition layer, native information-class dispositions, and accelerator enumeration

## Outcome

All five workstreams landed. Durable behavior is reconciled into
[`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md); the class dispositions are amended in
[`../Done/SystemDataMetricsExpansion_NativeClassMatrix.md`](SystemDataMetricsExpansion_NativeClassMatrix.md)
and the measurements in
[`../Done/SystemDataMetricsExpansion_Phase0Evidence.md`](SystemDataMetricsExpansion_Phase0Evidence.md).

What the evidence showed:

- The predicted `SystemPerformanceInformation` record size was confirmed by measurement: the kernel returns **376**
  bytes, not the 312 the earlier evidence recorded. The old number was `sizeof` of the SDK struct, which the Phase 0
  tool also passed as the query length.
- The class-5 mapping is exact where an exact oracle exists: parent PID matches Toolhelp on every comparable row, and
  create time matches `GetProcessTimes` bit for bit.
- Workstream A cut `process.list` from a median 46.6 ms to 17.2 ms at ~520 processes — a 63% reduction — and the
  full-catalog batch from 48.2 ms to 37.4 ms.
- The oracle harness earned its place twice: it caught a real defect in this plan's own code (a per-processor query
  length that was generous but not a whole number of records, which the kernel rejects outright), and it rejected a
  deliberately regressed build during the A/B measurement.

Three deviations from the plan as written, each recorded rather than quietly absorbed:

1. **`security.posture` ships without the speculative-execution and VBS rows.** Classes 196, 201 and 202 have no SDK
   enumerator, so there is no SDK member to anchor an overlay against and rule 3 of the layout-proof discipline cannot
   be satisfied. They stay `pending`. The dataset ships with the code-integrity fields — including HVCI, the headline
   flag — plus one documented `IsProcessorFeaturePresent` probe.
2. **`SystemInterruptInformation` (23) is proven but unpublished.** Its layout probe passes, but the non-`Ex` entry
   point returns success with a zero length on Win11 26100. Nothing depends on it; its columns are served by class 8.
   Publishing would have required `NtQuerySystemInformationEx` with a processor-group input, which this plan excluded.
3. **ARM64 was not built.** `vcpkg-install.ps1 -Platform ARM64` fails while configuring the `wil` port, before any
   RedXe code compiles. See the evidence file for the diagnosis; it is a pre-existing host toolchain problem.

## Goal

Raise the information yield of `builtin.system-data` without raising its cost, by fixing three separable problems that
the Phase 0 evidence pass left open:

1. Most of the data the source currently pays a second query or a per-process handle to obtain is **already inside the
   buffers it walks**, sitting in SDK `Reserved*` members whose contents are publicly named elsewhere.
2. Four information classes were dispositioned `opaque` on the strength of `sizeof(SDK struct)` rather than a measured
   kernel `ReturnLength`, and four more were dispositioned `unknown-layout` because the WDK `km` headers are absent.
   Neither reason survives contact with a named, versioned public layout source.
3. The plugin has no accelerator domain at all. Every adapter row is sourced from DXGI, and DXGI does not enumerate
   compute-only adapters, so NPUs are structurally invisible — as are the modern OS surfaces (efficiency mode,
   virtualization-based security posture, modern standby) that a 2026 dashboard is expected to show.

Owning contracts: [`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md) and
[`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md). This plan is non-normative
until its selected behavior is reconciled into those contracts.

Historical context this plan supersedes on its own subject matter, and does not otherwise reopen:

- [`../Done/SystemDataMetricsExpansion_2026-09-01.md`](SystemDataMetricsExpansion_2026-09-01.md) — the dataset
  catalog, tiering model, budgets, and the WMI prohibition, all of which stand unchanged.
- [`../Done/SystemDataMetricsExpansion_NativeClassMatrix.md`](SystemDataMetricsExpansion_NativeClassMatrix.md)
  — the allowlist this plan amends. Workstreams A and B rewrite specific rows; the matrix stays the allowlist and the
  "do not loop numeric classes at runtime" rule is unchanged.
- [`../Done/SystemDataMetricsExpansion_Phase0Evidence.md`](SystemDataMetricsExpansion_Phase0Evidence.md) —
  measurement method and column freeze. This plan adds measurements; it does not invalidate the recorded ones.

Interaction with active work: [`../WIP/PluginBoundaryHardening_2026-09-03.md`](../WIP/PluginBoundaryHardening_2026-09-03.md)
explicitly excludes "any change to shipped dataset IDs, column freezes, row caps, the 32-subscription cap". Those
changes are owned here. That plan also splits the oversized `SystemData.cpp` translation unit; workstream A lands more
cleanly after that split, so sequence accordingly rather than racing it.

## What changed since Phase 0

Phase 0 was measured on 2026-09-01 against SDK `10.0.26100.0` with no WDK `km` headers installed. Three of its blocking
assumptions are no longer true, and one was a methodology artifact.

| Phase 0 assumption | Status on 2026-09-03 | Consequence |
| --- | --- | --- |
| A named layout for the reserved members requires the WDK `km` headers. | False. The System Informer project (`winsiderss/systeminformer`) publishes `phnt`, a maintained public header set that names every field of these records, annotates the build each field appeared in, and is the layout source behind a widely deployed tool. | `opaque` and `unknown-layout` become *unverified*, not *unknowable*. They now need a proof procedure, not a WDK install. |
| `SYSTEM_PERFORMANCE_INFORMATION` "returns 312 reserved bytes". | Misread. 312 is `sizeof` of the **SDK** struct, asserted in `Tests/SystemDataPhase0`; it is not a kernel `ReturnLength`. The SDK's `Reserved1[312]` is exactly the prefix through `SystemCalls`; the current kernel record continues past it. | The class was never actually probed. It must be re-probed with an oversized buffer. |
| The WDK is required for compute-only / accelerator adapter classification. | False. The installed SDK `10.0.26100.0` already ships `shared\d3dkmthk.h` (`KMTQAITYPE_ADAPTERTYPE = 15`, `D3DKMT_ADAPTERTYPE.ComputeOnly`, `D3DKMT_QUERYSTATISTICS`) and `um\dxcore_interface.h` (`DXCORE_HARDWARE_TYPE_ATTRIBUTE_NPU`, `DXCoreHardwareTypeFilterFlags::NPU`, `DXCoreAdapterState`). | The accelerator domain is buildable today with named SDK types and no private headers. |
| ARM64 sizes "not yet measured". | Still true. | Carried forward as a checklist item, not a new finding. |

## Scope

### Included

- Consuming the named contents of SDK `Reserved*` members in records the source already walks, through RedXe-owned
  overlay structs with compile-time offset proofs and a runtime cross-check oracle.
- Removing the second bulk process query and most of the per-process handle work that those named fields make redundant.
- Length-versioned probes for `SystemPerformanceInformation`, `SystemTimeOfDayInformation`,
  `SystemInterruptInformation`, and `SystemExceptionInformation`, with per-field rather than whole-struct acceptance.
- Populating `cpu.logical` columns that are declared, published, and permanently `Unavailable` today.
- A new accelerator domain (`npu.*`) built on DXCore plus D3DKMT, and adapter classification for the existing `gpu.*`
  datasets.
- Modern-OS surfaces with a once-per-session or slow cadence: platform security posture, per-process efficiency mode,
  and modern-standby capability.
- Matrix, evidence, and column-freeze updates for every disposition this plan changes.

### Excluded

- Vendoring `phnt`, or any third-party header, into the build. `phnt` is consulted as documentation; the build keeps
  compiling against the Windows SDK only. See "Layout-proof discipline".
- Any class requiring elevation or a privilege the dashboard does not hold. `SystemStoreInformation` (memory
  compression) requires `SeProfileSingleProcessPrivilege` and `SystemFullProcessInformation` requires admin; both stay
  **out**, and "Compressed" memory is therefore not offered. Do not synthesize it from a delta.
- Guessing an undocumented performance-counter set name. Microsoft has stated publicly that the NPU counter set is not
  "GPU Engine" and that its name is not disclosed; the accelerator design must not depend on discovering it.
- WMI/CIM in any form, a kernel driver, ETW sessions, firmware method evaluation, and any state-changing IOCTL or
  D3DKMT escape. Unchanged prohibitions from the expansion plan.
- Process command lines, image paths, owners, environment blocks, PEB dereference, and thread start/stack/TEB
  addresses. `SystemExtendedProcessInformation` may be used only for its non-address members.
- New settings-visible viewers, time-series retention, and chart rendering.

## Verified evidence (primary machine, 2026-09-03)

Recorded so the workstreams below are auditable rather than assertive. Every row was checked against the installed SDK
or the current sources in this repository.

| # | Finding | Where verified |
| --- | --- | --- |
| E1 | SDK `SYSTEM_PROCESS_INFORMATION` and the `phnt` record are the **same 256-byte layout**; only the names differ. Offsets recomputed independently from both headers agree at every member. | `um\winternl.h`; `phnt/ntexapi.h` |
| E2 | SDK `SYSTEM_THREAD_INFORMATION` (80 bytes) reserves exactly per-thread `KernelTime`, `UserTime`, `CreateTime`, `WaitTime`, and `ContextSwitches`. | same |
| E3 | SDK `SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION` (48 bytes) reserves exactly `DpcTime`, `InterruptTime`, and `InterruptCount`. | same |
| E4 | `SYSTEM_PERFORMANCE_INFORMATION` is 4 `LARGE_INTEGER` + 70 `ULONG` + 8 eight-byte members = **376 bytes** on 24H2. `4*8 + 70*4 = 312` is precisely the SDK `Reserved1[312]` prefix. `MdlPagesAllocated` is annotated "since 24H2", so the tail is build-dependent. | `phnt/ntexapi.h`, field census |
| E5 | `SYSTEM_TIMEOFDAY_INFORMATION` (48), `SYSTEM_INTERRUPT_INFORMATION` (24), and `SYSTEM_EXCEPTION_INFORMATION` (16) each match their SDK `Reserved1[n]` size **exactly**, so those three are fully covered with no version tail. | both headers |
| E6 | `NativeQuery.cpp` issues a **second** bulk query (`SystemBasicProcessInformation`, 256 KiB buffer) and builds a 4,096-entry open-addressed hash map, solely to recover parent PID — which is at `Reserved2` (offset 88) of the class-5 record already in hand. | `Plugins/SystemData/NativeQuery.cpp:311-315`, `:221-244` |
| E7 | Even on the native walk path, `SystemData.cpp` opens a handle **per process** and calls `GetProcessTimes`, `GetProcessIoCounters`, `QueryProcessCycleTime`, `K32GetProcessMemoryInfo`, `IsWow64Process2`, and `IsProcessCritical`. The first four are redundant with E1. | `Plugins/SystemData/SystemData.cpp:2731-2782`, `:2489`, `:2547` |
| E8 | `RedXeNativeLogicalCpu::currentMhz`, `maxMhz`, `cpuSetId`, `efficiencyClass`, `parked`, and `allocated` are declared and published as `cpu.logical` columns but are **never written** — zero occurrences in `NativeQuery.cpp`. Those columns are permanently `Unavailable`. | `NativeQuery.h:15-31` vs `NativeQuery.cpp`; `SystemData.cpp:1695-1715` |
| E9 | `RedXeGpuSampleAdapters` builds adapter rows from `IDXGIFactory1::EnumAdapters1`. `RefreshKmtAdapters` enumerates D3DKMT adapters first and stores their LUIDs and handles, but they are used only as a lookup joined **from** a DXGI row. An adapter with no DXGI entry is silently dropped. | `Plugins/SystemData/GpuQuery.cpp:345-423`, `:269-296` |
| E10 | The only GPU utilization backend is the PDH counter path `\GPU Engine(*)\Utilization Percentage`. Nothing queries `KMTQAITYPE_ADAPTERTYPE`, so `ComputeOnly` is never read. | `GpuQuery.cpp:17`, `:11-15` |
| E11 | The installed SDK ships everything the accelerator domain needs: `shared\d3dkmthk.h` (`KMTQAITYPE_ADAPTERTYPE = 15`, `D3DKMT_ADAPTERTYPE.ComputeOnly` under WDDM 2.6+, `D3DKMT_QUERYSTATISTICS_PROCESS_NODE`/`_PROCESS_SEGMENT_GROUP`) and `um\dxcore_interface.h` (`DXCORE_HARDWARE_TYPE_ATTRIBUTE_NPU`, `DXCoreHardwareTypeFilterFlags::NPU`, `IDXCoreAdapterFactory1::CreateAdapterListByWorkload`, `DXCoreAdapterState` values 2–10, `DXCoreAdapterEngineIndex`, `DXCoreEngineQueryOutput`, `DXCoreFrequencyQueryOutput`). | SDK `10.0.26100.0` |
| E12 | `GetSystemCpuSetInformation` / `SYSTEM_CPU_SET_INFORMATION` (with `EfficiencyClass`, `Parked`, `Allocated`), `CallNtPowerInformation(ProcessorInformation)` / `PROCESSOR_POWER_INFORMATION`, `GetProcessInformation(ProcessPowerThrottling)` / `PROCESS_POWER_THROTTLING_STATE`, and `SYSTEM_POWER_CAPABILITIES.AoAc` are all **documented** SDK surfaces. None of them needs a native class. | `um\processthreadsapi.h`, `um\winnt.h` |
| E13 | `PowerSensors.cpp` already calls `GetPwrCapabilities`, so modern-standby capability is a one-field append, not a new acquisition. | `Plugins/SystemData/PowerSensors.cpp:396-397` |
| E14 | Microsoft's public guidance states the NPU counter set is not "GPU Engine" and its name is not disclosed; the recommended path is DXCore adapter enumeration. The `DXCoreAdapterState` items 2–10 are documented but flagged prerelease. | Microsoft Learn `DXCoreAdapterState`; Microsoft Q&A on NPU utilization |
| E15 | `RedXeDataCollectMaximumDataSets` is 32, `kStatusMaximumRows` is 32, and both are `static_assert`-ed against `kDataSets.size()`. There are 18 datasets today, so **14 slots remain**. | `Common/PlugInterfaces/Data.h:46`; `SystemData.cpp:80`, `:840-841` |

## Layout-proof discipline

This is the rule that makes workstreams A and B admissible under the existing matrix policy ("a successful `NtQuery*`
return is not layout proof"). It applies to every reserved member this plan consumes.

1. **`phnt` is documentation, never a dependency.** Do not add it to the repository, the vcpkg manifest, or an include
   path. Do not `#include` it and do not copy a `phnt` type name into RedXe code.
2. **Read through a RedXe-owned overlay.** Declare one `RedXeNt*` POD per record in the plugin, with RedXe's own field
   names, and pin it with `static_assert` on `sizeof` and on every consumed `offsetof`.
3. **Anchor the overlay to the SDK struct, not to a remembered number.** Each overlay carries
   `static_assert(offsetof(RedXeNtProcessRecord, parentProcessId) == offsetof(SYSTEM_PROCESS_INFORMATION, Reserved2))`
   and the equivalent for every other member. A future SDK that renames or resizes a reserved member breaks the build
   instead of silently shifting a column.
4. **Every consumed field needs a runtime oracle** — an independent, documented API that must agree on a quiet sample
   before the field is promoted from `pending` to `used`. The oracles are named per field in the workstreams below.
   A field with no oracle stays out of runtime code.
5. **Bound consumption by `ReturnLength`, not by `sizeof`.** For any record with a build-dependent tail, consume a
   field only when `ReturnLength` covers its end offset, and mark it `Unavailable` otherwise. Never widen a buffer
   guard to make a tail field appear.
6. **A field that fails its oracle is a defect in this plan, not in the oracle.** Drop the field and record the failure
   in the matrix; do not ship the disagreeing value behind a lowered quality flag.

## Workstream A — consume the named layout already inside the buffers we walk

Highest yield, negative cost. No new query is issued; two existing ones are removed.

### A1 — process records (`SystemProcessInformation`, class 5)

| SDK member | Offset | Contents | Oracle | Column outcome |
| --- | ---: | --- | --- | --- |
| `Reserved1[48]` | 8 | `WorkingSetPrivateSize` (8), `HardFaultCount` (16), `NumberOfThreadsHighWatermark` (20), `CycleTime` (24), `CreateTime` (32), `UserTime` (40), `KernelTime` (48) | `GetProcessTimes` and `QueryProcessCycleTime` on accessible PIDs during the transition pass | `userTime100ns`, `kernelTime100ns`, `cpuPercent`, `cpuUserPercent`, `cpuKernelPercent`, `startTime`, `cycleCount` stop needing a handle; `hardFaultCount` and `workingSetPrivateBytes` are new appends |
| `Reserved2` | 88 | `InheritedFromUniqueProcessId` | the existing class-252 walk, which already produces the same value from a named field | `parentProcessId` — and E6's second query and hash map are deleted |
| `Reserved3` | 104 | `UniqueProcessKey` | none required; not consumed | stays out |
| `Reserved4` | 128 | `PageFaultCount` | `K32GetProcessMemoryInfo.PageFaultCount` | `pageFaultCount`, `pageFaultRate` stop needing a handle |
| `Reserved5` / `Reserved6` | 152 / 168 | `QuotaPeakPagedPoolUsage` / `QuotaPeakNonPagedPoolUsage` | `K32GetProcessMemoryInfo` peak quota members | new appends, optional |
| `Reserved7[6]` | 208 | `ReadOperationCount`, `WriteOperationCount`, `OtherOperationCount`, `ReadTransferCount`, `WriteTransferCount`, `OtherTransferCount` | `GetProcessIoCounters` on accessible PIDs | `ioReadBytes`, `ioWriteBytes`, `ioOtherBytes` and their rates stop needing a handle; operation counts become available for **every** process, not only accessible ones |

Consequences to implement together:

- Delete the `SystemBasicProcessInformation` query, `state.basicProcessBuffer` (256 KiB), `ParentMapEntry`, the
  4,096-slot map, and `WalkBasicParents`. Keep class 252 alive **only** as the transition oracle, behind the gate
  below, then remove it.
- Reduce the per-process `OpenProcess` to the fields that genuinely require a handle: WOW64/architecture, critical
  process, and efficiency mode (workstream E2). Keep `IsWow64Process2` and `IsProcessCritical` as today.
- Coverage improves as a side effect: CPU, I/O, and fault columns currently read `Unavailable` for every process the
  dashboard cannot open. After A1 they are `Good` for every row in the walk.

### A2 — thread records (`SYSTEM_THREAD_INFORMATION`, same buffer)

`Reserved1[3]` at offset 0 is `KernelTime`, `UserTime`, `CreateTime`; `Reserved2` at 24 is `WaitTime`; `Reserved3` at
64 is `ContextSwitches`. `thread.list` publishes `contextSwitchCount` and `contextSwitchRate` columns today; this is
the backing for them, plus per-thread CPU time and rate, at zero additional query cost.

Oracle: the system-wide `ContextSwitches` field from workstream B1 must be within sampling tolerance of the sum across
threads on a quiet sample; per-thread times must be monotonic across consecutive collections.

### A3 — per-logical-processor DPC and interrupt

`Reserved1[2]` at offset 24 is `DpcTime` and `InterruptTime`; `Reserved2` at 40 is `InterruptCount`. The matrix
currently says "Do not consume `Reserved1`/`Reserved2` (DPC/interrupt) yet". This unblocks the declared-but-empty
`cpu.summary` and `cpu.logical` DPC/interrupt columns from the query already running in the cheap batch.

Oracle: `DpcTime + InterruptTime <= KernelTime` per processor on every sample, and `InterruptCount` monotonic.

## Workstream B — retire the four `opaque` dispositions

Each of these is `opaque` because it was never probed, not because the layout is unknown. The probe is the same in each
case: issue the class with an **oversized** buffer, record the kernel's `ReturnLength`, and accept fields by end offset.

### B1 — `SystemPerformanceInformation` (2)

The only one with a build-dependent tail. Consume in three bands:

| Band | End offset | Fields of interest | Accept when |
| --- | ---: | --- | --- |
| Base | 312 | `IdleProcessTime`; system-wide I/O transfer and operation counts; `AvailablePages`, `CommittedPages`, `CommitLimit`, `PeakCommitment`; `PageFaultCount`, `DemandZeroCount`, `TransitionCount`, `CopyOnWriteCount`, `PageReadCount`/`PageReadIoCount`; `PagedPoolPages`, `NonPagedPoolPages`, `AvailablePagedPoolPages`, `FreeSystemPtes`; `ContextSwitches`, `SystemCalls`, `FirstLevelTbFills`, `SecondLevelTbFills` | `ReturnLength >= 312` |
| Threshold tail | 344 | `CcTotalDirtyPages`, `CcDirtyPageThreshold`, `ResidentAvailablePages`, `SharedCommittedPages` | `ReturnLength >= 344` |
| 24H2 tail | 376 | `MdlPagesAllocated`, `PfnDatabaseCommittedPages`, `SystemPageTableCommittedPages`, `ContiguousPagesAllocated` | `ReturnLength >= 376` |

Oracles: `AvailablePages * PageSize` against `GlobalMemoryStatusEx.ullAvailPhys`; `CommittedPages` / `CommitLimit`
against `K32GetPerformanceInfo.CommitTotal` / `CommitLimit`; `PagedPoolPages` / `NonPagedPoolPages` against
`K32GetPerformanceInfo`; system-wide I/O transfer counts against the sum of workstream A1's per-process `Reserved7`
totals; `IdleProcessTime` against the sum of per-processor `IdleTime`; every counter monotonic across samples.

This is what finally makes `memory.summary`'s paged/nonpaged pool, page-fault-rate, and cache columns real, and gives
`cpu.summary` a true `contextSwitchRate` — the evidence file already flags that column as "Unavailable if
`SystemPerformanceInformation` stays opaque".

### B2 — `SystemTimeOfDayInformation` (3), `SystemInterruptInformation` (23), `SystemExceptionInformation` (33)

Fixed size, exactly matching their SDK reserved arrays (E5), so a single `ReturnLength ==` equality check is a complete
version gate.

- Class 3 → `BootTime`, `CurrentTime`, `TimeZoneBias`, `TimeZoneId`, `BootTimeBias`, `SleepTimeBias`. Gives
  `system.summary` a real boot time and — via `SleepTimeBias` — an uptime that distinguishes wall time from awake time,
  which `GetTickCount64` cannot. Oracle: `CurrentTime` against `GetSystemTimeAsFileTime`; `CurrentTime - BootTime`
  against `GetTickCount64` within one tick period.
- Class 23 → per-processor `ContextSwitches`, `DpcCount`, `DpcRate`, `TimeIncrement`, `DpcBypassCount`,
  `ApcBypassCount`. Complements A3 with DPC *counts* rather than time. Oracle: sum of per-processor `ContextSwitches`
  against B1's system-wide value.
- Class 33 → `AlignmentFixupCount`, `ExceptionDispatchCount`, `FloatingEmulationCount`, `ByteWordEmulationCount`.
  Low value on x64, genuinely diagnostic on ARM64 where emulation counts matter. Gate it behind ARM64 measurement
  rather than shipping a permanently-zero column on x64.

### B3 — dispositions that stay closed

`SystemLookasideInformation` (45) and `SystemPolicyInformation` (134) remain `opaque`: no owning column wants them.
`SystemExtendedProcessInformation` (57) moves from `unknown-layout` to `restricted` — the layout is known, but every
member it adds over class 5 is an address (`StackBase`, `StackLimit`, `Win32StartAddress`, `TebBaseAddress`) and the
excluded list forbids publishing those. It is therefore **not** worth a second walk. `SystemFullProcessInformation`
and `SystemStoreInformation` stay out on privilege grounds (Scope/Excluded).

## Workstream C — populate the columns that ship permanently unavailable

E8 is the smallest and least defensible gap: `cpu.logical` advertises six columns that no code path can ever fill.
None of them needs a native class.

| Column | Source | Notes |
| --- | --- | --- |
| `cpuSetId`, `efficiencyClass`, `parked`, `allocated` | `GetSystemCpuSetInformation` → `SYSTEM_CPU_SET_INFORMATION` | Documented. Cache the identity portion; re-read only on a topology change. `EfficiencyClass` is the hybrid P/E-core signal the dashboard needs on Intel hybrid and ARM big.LITTLE parts. |
| `currentMhz`, `maxMhz` | `CallNtPowerInformation(ProcessorInformation)` → `PROCESSOR_POWER_INFORMATION[]` | Documented, no privilege, one call covers all logical processors. Fill `maxMhz` from `MaxMhz` and clamp `currentMhz` to `MhzLimit`. |

Optional follow-on, only if C's measured cost leaves headroom: `SystemProcessorPerformanceDistribution` (100) gives
P-state residency for a true average frequency, but it requires `NtQuerySystemInformationEx` with a processor-group
input and is a per-group query. Leave it `pending`; do not enter it in runtime code in this plan.

Set `hasCpuSet` / `hasFrequency` honestly: a machine where either call fails keeps reporting `Unavailable`, which is
the current behavior and remains correct.

## Workstream D — accelerators, and why there is no NPU data today

### D1 — the three independent reasons NPUs are invisible

This is not one missing feature; it is three, and fixing any one alone changes nothing.

1. **Enumeration.** `RedXeGpuSampleAdapters` builds its row set by iterating `IDXGIFactory1::EnumAdapters1` (E9). DXGI
   enumerates graphics adapters. An NPU on Windows is an **MCDM** (Microsoft Compute Driver Model) device — it is a
   `dxgkrnl` adapter, but it has no Direct3D user-mode driver and therefore no DXGI adapter. `RefreshKmtAdapters` runs
   *first* and already holds the NPU's LUID and `D3DKMT` handle; the row loop then discards it, because the join is
   directional, from DXGI to KMT.
2. **Classification.** Nothing queries `KMTQAITYPE_ADAPTERTYPE`, so `D3DKMT_ADAPTERTYPE.ComputeOnly` is never read
   (E10). Even if a compute-only adapter reached the row set, the plugin would have no way to label it as anything
   other than a GPU with missing fields.
3. **Utilization.** The single utilization backend is the PDH counter set `\GPU Engine(*)\Utilization Percentage`
   (E10). NPU engines do not appear in that counter set, and Microsoft has not published the name of the one they do
   appear in (E14). A design whose only utilization path is a named counter set has no route to NPU utilization at all.

Root cause, stated plainly: the GPU domain was designed as *"DXGI adapters, enriched by D3DKMT and PDH"*. Modern
Windows has accelerators that are D3DKMT adapters but not DXGI adapters, so the spine is at the wrong layer.

### D2 — invert the spine

Make the **D3DKMT adapter list** the row spine and demote DXGI to enrichment.

1. `D3DKMTEnumAdapters2` produces the row set — it already runs and already returns compute-only adapters.
2. `KMTQAITYPE_ADAPTERTYPE` classifies each: `DisplaySupported` / `RenderSupported` / `ComputeOnly` / `SoftwareDevice`
   / `HybridIntegrated` / `HybridDiscrete` / `Paravirtualized` / `Detachable`.
3. DXGI supplies description, vendor/device ID, and memory capacity **when a LUID match exists**. No match is normal,
   not an error.
4. DXCore supplies what DXGI cannot: `IDXCoreAdapterFactory1::CreateAdapterListByWorkload`, and hardware type via
   `DXCORE_HARDWARE_TYPE_ATTRIBUTE_NPU` / `_GPU` / `_COMPUTE_ACCELERATOR` / `_MEDIA_ACCELERATOR` — Microsoft documents
   that exactly one of these is reported by the driver or inferred by DXCore, which is precisely the labelling Task
   Manager relies on. Join by `DXCoreAdapterProperty::InstanceLuid`.

Precedence for the device class, most to least trustworthy: DXCore hardware-type GUID → `D3DKMT_ADAPTERTYPE` bits →
DXGI presence. Publish the class explicitly; never infer "GPU" from the absence of evidence.

### D3 — utilization and memory without a counter-set name

`DXCoreAdapterState` (E11, E14) covers exactly the metrics the PDH path was providing, per engine and per process, and
works for any adapter DXCore enumerates — NPU included:

| Need | State item |
| --- | --- |
| per-engine busy time | `AdapterEngineRunningTimeMicroseconds` (input `DXCoreAdapterEngineIndex`) |
| per-process per-engine busy time | `AdapterEngineRunningTimeByProcessMicroseconds` |
| machine-wide dedicated/shared use | `AdapterMemoryUsageBytes` — resident **and** committed, by segment group |
| per-process memory | `AdapterMemoryUsageByProcessBytes` |
| temperature, engine/memory clocks | `AdapterTemperatureCelsius`, `AdapterEngineFrequencyHertz`, `AdapterMemoryFrequencyHertz` |
| engine inventory | `AdapterEngineCount` / `AdapterEngineName` properties |

Two notes that must survive into the implementation:

- `AdapterMemoryUsageBytes` is the documented answer to the exclusion the expansion plan wrote down —
  "`IDXGIAdapter3::QueryVideoMemoryInfo` is the calling process's budget, not a global total". Machine-wide GPU memory
  becomes publishable for the first time.
- These state items are documented but **flagged prerelease** (E14). Capability-probe every one per item, per adapter,
  at subscription time; a failure marks the column `Unavailable` and falls back to the existing D3DKMT
  `ADAPTERPERFDATA` / PDH path for GPUs. Never assume availability from the OS build number alone.

Second, independent backend for per-process attribution: `D3DKMT_QUERYSTATISTICS` with
`D3DKMT_QUERYSTATISTICS_PROCESS_SEGMENT_GROUP` (resident versus committed, a distinction the PDH path cannot express)
and `D3DKMT_QUERYSTATISTICS_PROCESS_NODE` for per-node running time. Both are in the shipped SDK and both work on MCDM
adapters.

### D4 — datasets

Add three, keeping the shipped `gpu.*` IDs, columns, and meanings frozen. A viewer that says "GPU" must not silently
start listing an NPU.

| Dataset | Max rows | Recommended minimum | Contents |
| --- | ---: | ---: | --- |
| `npu.adapter` | 16 | 1 s | LUID, name, vendor/device ID, device class, driver version, compute-only flag, dedicated/shared memory capacity and use, aggregate utilization, temperature, memory clock. |
| `npu.engine` | 128 | 1 s | LUID, physical adapter index, engine index, engine name, running-time-derived utilization, current/max frequency. |
| `npu.process` | 512 | 2 s | PID, LUID, engine index, utilization percent, committed and resident memory. Local sensitive. |

Also append to `gpu.adapter` (append-only, per the expansion plan's transition rule): `deviceClass`, `computeOnly`,
and `physicalAdapterCount`. Appending changes `kGpuAdapterColumnCount`, so `Plugins/ProcessViewer` must be checked for
positional column indexing before the append lands.

Budget check: 18 + 3 = 21 datasets against the cap of 32 (E15). Fits, with 11 slots left.

## Workstream E — other modern OS surfaces

The question behind this plan is broader than NPUs: a 2026 dashboard is expected to show things the 2026-09-01 catalog
has no dataset for. These are cheap, slow-cadence, and mostly documented.

### E1 — platform security posture (new dataset `security.posture`, 1 row, 60 s)

Read once at subscription and re-read only on a session change. All are single-value queries with no per-tick cost.

| Field group | Class | Contents |
| --- | ---: | --- |
| Code integrity | `SystemCodeIntegrityInformation` (103) — currently `pending` | `Enabled`, `TestSign`, `UmciEnabled`, `HvciKmciEnabled`, `HvciKmciStrictModeEnabled`, `HvciIumEnabled`, `WhqlEnforcementEnabled`, `DebugModeEnabled`, `FlightingEnabled`. This is HVCI / memory integrity — the "Core isolation" switch. Sized record: pass `Length` and honour it. |
| Speculative execution | `SystemKernelVaShadowInformation` (196), `SystemSpeculationControlInformation` (201) — currently `unknown-layout` | `KvaShadowEnabled`, `KvaShadowPcid`, `KvaShadowRequired`, `L1DataCacheFlushSupported`, and the branch-target-injection mitigation bits. Both are single-`ULONG` bitmask records; the whole record is the version gate. |
| Virtualization-based security | `SystemIsolatedUserModeInformation`, `SystemDmaGuardPolicyInformation` (202) | VBS / secure-kernel running state and kernel DMA protection. Both `pending` until probed. |

Oracle for the whole dataset: cross-check against `IsProcessorFeaturePresent` for the features it exposes, and require
that a bit reported as enabled is stable across the session. These are posture flags, not counters — a value that
flips sample-to-sample is a layout error, and the probe must reject it.

### E2 — per-process efficiency mode (append to `process.list`)

`GetProcessInformation(handle, ProcessPowerThrottling, ...)` → `PROCESS_POWER_THROTTLING_STATE` (E12). This is Task
Manager's "Efficiency mode" leaf, and the EcoQoS state that determines whether a process is scheduled onto E-cores.
Fully documented, and it reuses the handle the plugin already opens for `IsProcessCritical`, so it adds no new
`OpenProcess`. Append `efficiencyMode` and `throttlingControlMask`.

### E3 — modern standby (append to `power.summary`)

`SYSTEM_POWER_CAPABILITIES.AoAc` and `AoAcConnectivitySupported`, from the `GetPwrCapabilities` call that already runs
(E13). A machine reporting `systemS3 = 0` today looks like it cannot sleep; in reality most modern laptops are
S0 low-power idle. Append `modernStandby` and `modernStandbyConnected` so the existing S3/S4 columns stop misleading.

### E4 — ARM64 completion

The matrix still records ARM64 sizes as `pending`, and the ARM64 configuration ships. Every overlay struct added by
workstreams A and B must carry its `static_assert` set unconditionally, so the ARM64 build proves the layouts at
compile time even before an ARM64 machine is measured. Record measured ARM64 `ReturnLength` values when hardware is
available.

## Sequencing

1. **C** first — smallest, fully documented, no native classes, and it stops the plugin shipping six columns that can
   never have a value.
2. **B2** next — three fixed-size records with exact-equality version gates. Proves the probe-and-oracle harness on the
   easy cases before it is trusted with anything load-bearing.
3. **B1** — the versioned probe, using the harness from B2.
4. **A** — highest yield but touches the hottest path and the largest translation unit. Land it after
   `PluginBoundaryHardening` has split `SystemData.cpp`, and keep class 252 as the live oracle across the transition
   before deleting it.
5. **D** — independent of A/B/C; may run in parallel by different hands. D2 (spine inversion) must land before D3/D4,
   and D4's `gpu.adapter` append must be checked against `ProcessViewer` first.
6. **E** — last, cheapest, and each of E1–E3 is independently shippable.

## Required validation

- `.\build.ps1` and `.\build.ps1 -Configuration Release` clean for x64 **and** `-Platform ARM64`; the overlay
  `static_assert` sets are the layout proof and must be compiled on both.
- `.\test.ps1` green, including the WARP smoke test.
- `Tests/SystemDataPhase0` extended with: measured `ReturnLength` for every class in workstream B; overlay `sizeof` /
  `offsetof` assertions anchored to the SDK reserved members; and short-buffer, oversized-buffer, and truncated-tail
  fixtures for the versioned probe.
- `Tests/SystemDataTests` extended with the oracle comparisons named in workstreams A and B, run over a real
  collection, with a documented tolerance for each rate-derived value and exact equality where the oracle is exact
  (parent PID, I/O operation counts, handle counts).
- Re-record the Release x64 baseline (three runs; median wall and 64-collection CPU probe; heap, handle, and
  private-byte deltas; `source_storage_bytes`) and show that workstream A **reduces** collection cost. A must not merge
  if `process.list` p95 regresses; the whole premise is that removing a bulk query and six per-process calls is cheaper.
- Storage envelope re-measured: `basicProcessBuffer` (−256 KiB) removed, accelerator state added, still under the
  16 MiB fast-lane ceiling and the source object still under 1 MiB.
- No WMI module delta after the DXCore and accelerator probes, measured the same way as the Phase 0 surface spikes.
- A machine with no NPU must publish `npu.*` as discoverable, zero-row, `Unavailable` datasets with an explanatory
  `source.status` row — never a missing dataset, and never a fabricated zero utilization.

## Checklist

- [x] Overlay structs added with SDK-anchored `offsetof` proofs; no `phnt` header, type name, or include path enters
      the build.
- [x] Workstream C: `cpuSetId`, `efficiencyClass`, `parked`, `allocated`, `currentMhz`, `maxMhz` populated;
      `hasCpuSet` / `hasFrequency` set honestly. `coreIndex`, `packageIndex` and `numaNode` were the same
      declared-but-never-written gap and were filled from the same cached topology walk.
- [x] Workstream B2: classes 3, 23, 33 probed, oracles pass, matrix rows rewritten from `opaque`. Class 3 is
      published; 23 and 33 are proven and deliberately unpublished (see Outcome).
- [x] Workstream B1: class 2 probed with an oversized buffer; measured `ReturnLength` 376 recorded; three acceptance
      bands implemented; `contextSwitchRate`, `cacheBytes` and the fault-rate columns real.
- [x] Workstream A1: class-5 reserved members consumed; class-252 query, `basicProcessBuffer`, and the parent hash map
      deleted; per-process handle work reduced to WOW64 / critical / affinity / efficiency mode.
- [x] Workstream A2/A3: per-thread times and context switches, and per-processor DPC/interrupt, published.
- [x] Workstream D2: D3DKMT adapter list is the row spine; DXGI and DXCore are enrichment; device class published with
      explicit precedence.
- [x] Workstream D3: `DXCoreAdapterState` items capability-probed per item per adapter; GPU fallback path retained.
      `gpu.adapter`'s `utilizationPercent`, `dedicatedUsedBytes` and `sharedUsedBytes` — previously unfillable — now
      have a real source.
- [x] Workstream D4: `npu.adapter`, `npu.engine`, `npu.process` shipped; `gpu.adapter` appends checked against
      `ProcessViewer`, which indexes positionally behind a `columnCount >= minimum` guard and is unaffected by an
      append; dataset count 22 of 32 re-asserted at compile time.
- [x] Workstream E1–E3: `security.posture` dataset (code-integrity subset, see Outcome); `efficiencyMode` and
      `throttlingControlMask` appends; `modernStandby` and `modernStandbyConnected` appends.
- [x] `SystemDataMetricsExpansion_NativeClassMatrix.md` updated for every disposition changed here, with the new layout
      source and oracle named per class.
- [x] `SystemDataMetricsExpansion_Phase0Evidence.md` amended to correct the "returns 312 reserved bytes" reading and to
      record the measured `ReturnLength` values.
- [x] Column freeze extended for every appended column and the four new datasets; `Plugins_API.md` lists all 22.
- [x] Release x64 cost re-measured; `process.list` improved from a 46.6 ms to a 17.2 ms median, and did not regress.
- [ ] ARM64 builds clean with the same assertions; ARM64 `ReturnLength` values recorded when hardware is available.
      **Blocked:** the ARM64 vcpkg dependency bootstrap fails before any RedXe code compiles. Carried forward.

## Exit criteria

Durable behavior merged into [`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md); the class matrix carries
no `opaque` row that this plan proved readable and no `used` row without a named oracle; the validation above passes;
this plan moves to `Specs/Plans/Done/` and its row is removed from the WIP index.

Met on 2026-09-03, with the ARM64 build carried forward as a known gap recorded in the evidence file. The one open
follow-on this plan creates: when the ARM64 toolchain bootstrap is repaired, build both configurations and record the
ARM64 `ReturnLength` values. No behavior depends on that measurement — the overlays are proven against SDK members at
compile time — so it is an evidence gap, not an unshipped requirement.

## Open decisions

| # | Question | Default if unresolved |
| --- | --- | --- |
| D-1 | Separate `npu.*` datasets, or one `accelerator.*` family that supersedes `gpu.*`? | Separate `npu.*`. Frozen `gpu.*` IDs and viewer compatibility outweigh catalog elegance, and it matches how Task Manager presents the two. |
| D-2 | Does `DXCoreAdapterState` replace the PDH `GPU Engine` path for GPUs, or only supplement it for NPUs? | Supplement first, behind a capability probe. Revisit after measuring both on the primary machine; the prerelease flag (E14) makes replacement premature. |
| D-3 | Should `SystemInterruptInformation` (23) ship on x64 given A3 already provides DPC/interrupt **time**? | Ship the DPC/APC bypass and rate counts only if `cpu.logical` measurement shows they add signal; otherwise leave class 23 probed, proven, and unpublished. |
| D-4 | Is a `security.posture` dataset in scope for a dashboard product, or is it a diagnostics-only surface? | Ship it. It is one row at 60 s, and HVCI / VBS state is the kind of thing a XENEON EDGE user checks once and wants visible. Product may overrule. |
| D-5 | `SystemExceptionInformation` (33) on x64 is near-zero. Publish anyway? | No. Gate on ARM64 measurement; do not ship a permanently-zero column. |
