# System data expansion — Phase 0 evidence

Status: `COMPLETE` working artifact of
[`SystemDataMetricsExpansion_2026-09-01.md`](SystemDataMetricsExpansion_2026-09-01.md)
Created: 2026-09-01

This file is not a separately indexed WIP plan. Record measurements, backend gates, and the column/storage freeze
here. Copy lasting decisions back into the parent plan before Phase 1. Phase 0 ratification is complete; Phase 1 may
change `Plugins/SystemData/SystemData.cpp` and `Data.h` IIDs.

The native-class inventory lives in
[`SystemDataMetricsExpansion_NativeClassMatrix.md`](SystemDataMetricsExpansion_NativeClassMatrix.md).

## Reference machines

Budgets in the parent plan are acceptance ceilings on the primary machine unless a row says otherwise.

| Role | Host | Recorded |
| --- | --- | --- |
| Primary Release x64 | Windows 11 Pro `10.0.26200` x64, Gigabyte X870E AORUS ELITE WIFI7 ICE, AMD Ryzen 9 9950X3D 16 cores / 32 logical processors / 1 processor group, 64 GiB-class RAM (61.6 GiB reported), AMD Radeon Graphics iGPU plus NVIDIA GeForce RTX 5080, no battery, Hyper-V present, 4 KiB pages | 2026-09-01 |
| Battery / ACPI | One laptop with a present `GUID_DEVCLASS_BATTERY` device | closed as optional-absence: primary AC-only host publishes `batteryPresent` 0 and a zero-row `battery.list`; live laptop IOCTL is follow-on |
| Unsupported VM | One VM or session with WARP/software GPU and no thermal/fan/battery IOCTLs; Hyper-V is available on the primary host | closed as optional-absence: DXGI `software` is Good 0/1; on-box WARP is present on the primary host; a dedicated WARP-only VM is follow-on |
| ARM64 layout | Release/Debug ARM64 compile is required; native record `sizeof`/offset fixtures need an ARM64 run or WDK header sizes recorded per class in the matrix | 2026-09-01 Debug and Release ARM64 compile: same 64-bit `sizeof` as x64. Live ARM64 host is follow-on. |
| Intel hybrid | Optional extra host: P/E-core efficiency-class mapping. The primary AMD dual-CCD package does not replace that check. | closed as optional extra host, not a shipped-catalog blocker |

Selected SDK/WDK surface: Windows 11 SDK `10.0.26100.0`, MSVC toolset `v145`. Inventory enumerations from those headers
plus extra classes pinned by the legacy code source.

## Locked decisions

These are selected in the parent plan. Change them only with a row in this file and an edit to the parent.

| Decision | Selection |
| --- | --- |
| `system.summary` cadence | 1 s uses the cheap system-information batch only. Walking `SystemProcessInformation` is not a 1 s dependency. |
| Process/thread/handle totals | `SystemHandleCountInformation` named fields, with `K32GetPerformanceInfo` as the complete fallback. Do not parse `SystemPerformanceInformation` reserved bytes. |
| If cheap totals fail Phase 0 | Raise `system.summary` to 2 s. Do not put the bulk process walk on the 1 s path. |
| Sampling tolerance | See parent plan. Native/public disagreement beyond the rule disables that native field or layout; values are never averaged. |
| Process affinity | Group plus mask, or omit. Never a single `UInt64` as the complete affinity. |
| Quality enum | Keep `Good` / `Unavailable` / `Initializing` unless the device-lane gate proves a distinct `Stale` state. |
| Host subscription cap | Remains 32 during Phase 0. 128 is a Phase 1 capacity test, not a spike default. |
| Storage | 16 MiB fast + 1 MiB device are ceilings after the envelope. Compact previous samples. NT walk cap 4 MiB. |
| Spike code | Throwaway or a later test project. Not production `SystemData.cpp`. |

## How to re-record the shipped baseline

On the primary machine, after a Release x64 build:

```powershell
.\build.ps1 -Configuration Release
.\.build\x64\Release\SystemDataTests.exe --benchmark
```

`test.ps1` also runs that benchmark. Record three warmed runs. Methodology must match
[`SystemDataPlugin_2026-08-31.md`](../Done/SystemDataPlugin_2026-08-31.md): synthetic 2,048-row process population,
two steady collections for heap/handle/private-byte deltas, 64-collection CPU probe, `source_storage_bytes`.

Historical accepted numbers (do not treat as the Phase 0 re-record): median ~3.823 ms wall and ~3.906 ms CPU per
row-cap collection; 786,936 bytes source storage; zero busy-heap and handle growth.

| Run | Date | wall_us_per_collection | process_cpu_us_per_collection | handles_delta | heap_bytes_delta | private_bytes_delta | working_set_delta | source_storage_bytes |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 2026-09-01 | 3715.15 | 3906.25 | 0 | 0 | 0 | 4096 | 786936 |
| 2 | 2026-09-01 | 4003.65 | 3906.25 | 0 | 0 | 0 | 4096 | 786936 |
| 3 | 2026-09-01 | 3699.15 | 3662.11 | 0 | 0 | 0 | 4096 | 786936 |

Median wall 3715.15 µs; median 64-collection CPU probe 3906.25 µs. Heap/handle/private deltas were zero; working-set delta 4 KiB; source storage 786,936 bytes; zero source workers/timers. Matches the archived baseline (median ~3.823 ms wall / ~3.906 ms CPU, 786,936 bytes). Two-collection `process_cpu_us_total` is not used: `GetProcessTimes` often reports 0 or 15.625 ms for a ~8 ms pair.

Production live `CollectSnapshot` from `SystemDataPhase0` (not synthetic): `system.summary` 543–832 µs; Toolhelp `process.list` 34–44 ms at 656–664 rows. The synthetic row-cap path does not represent live Toolhelp cost.

## Spike results

Record the backend, p95, peak working set / private bytes, heap delta, handles, loaded modules, and gate
(`keep` / `fallback` / `unsupported`). One row per spike. Leave unused rows blank until run.

| ID | Surface | Host | p95 / setup | Memory / handles | WMI modules loaded | Gate | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| NT-CPU | `SystemProcessorPerformanceInformation` vs `GetSystemTimes` | primary | included in 0.18–0.68 ms cheap batch | none retained | no | `keep` | 32 logical, one group. Returned length = 48 × 32. Idle/kernel/user matched `GetSystemTimes` exactly on quiet samples. DPC/interrupt reserved fields not consumed. |
| NT-MEM | `SystemBasicInformation` + `SystemPerformanceInformation` vs `K32GetPerformanceInfo` | primary | cheap batch | none retained | no | `keep` counts; `opaque` performance blob | `SystemHandleCountInformation` matched K32 process/thread/handle exactly on quiet samples. `SystemPerformanceInformation` returns 312 reserved bytes; do not parse. `NumberOfProcessors` is `CCHAR` (32 here). |
| NT-PROC | `SystemProcessInformation` vs Toolhelp | primary | 17–21 ms quiet; 40 ms under load | 2.05–2.07 MiB buffer; cap 8 MiB | no | `keep` for `process.list` / `thread.list` at 2 s | ~644–656 processes, ~20.3k threads. `thread.list` cap 8,192 will truncate on this host. |
| NT-QIP | `ProcessBasicInformation` + Wow64 + BreakOnTermination | primary | 4.1–4.3 ms quiet for ~428 accessible of ~650 live; 9.8 ms under load | one transient handle | no | `keep` Wow64/critical; parent PID from class 252 instead of `Reserved3` | ~20 ms estimated if 2,048 accessible. 225+ access-denied is normal. |
| NT-252 | `SystemBasicProcessInformation` | primary | 51 KiB / 646 processes | none retained | no | `keep` identity/parent only | Named `InheritedFromUniqueProcessId`. Not a 1 s summary dependency and not redundant with class 5 for metrics. |
| CTR-CPU | PDH English vs PerfLib V2 per-CPU | primary Release x64 | PDH first open 110 ms / 4.0 MiB private / 4 handles; warm 131 µs. PerfLib V2 warm 142 µs / 20,008-byte blob | first-open private is OS PDH cost, not source-owned | no | `keep` PDH | `PdhAddEnglishCounterW` + `PdhCollectQueryData` only (`PdhCollectQueryDataEx` not called). UI language 1033. 34 `Processor Information(*)` instances; first `0,0`. PerfLib set exists; wildcard add needs a 48-byte `*` instance block or identifier `Status` is `ERROR_INVALID_PARAMETER`. Collect is not materially cheaper than PDH, so Tier C for CPU stays PDH. |
| CTR-DISK | PDH vs PerfLib disk vs `IOCTL_DISK_PERFORMANCE` | primary Release x64 | PDH first open 13.3 ms / 627 KiB / 106 handles; warm ~22 µs. IOCTL ~17 µs on the one accepting disk | PDH disk provider retained handles until query close | no | `keep` IOCTL where it works; PDH fallback | `PhysicalDisk` is not a PerfLib V2 set (176 sets enumerated; `FileSystem Disk Activity` exists and was not adopted). PDH instances=2 (`4 C:`). 5 `\\.\PhysicalDriveN` opened; 1 `IOCTL_DISK_PERFORMANCE` succeeded, 4 failed. Instance count 2 before and after IOCTL (`already_on`). `IOCTL_DISK_PERFORMANCE_OFF` not called. |
| CTR-GPU | GPU Engine / Adapter Memory / GPU Process Memory | primary Release x64 | PDH GPU Engine warm 820 µs / 2.6 MiB, 2027 instances; PerfLib V2 warm 559 µs / 786 KiB / 350,832-byte blob | GPU Engine formatted array needs >317 KiB | no | `keep` PerfLib V2 for GPU Engine; PDH fallback | Grammar `pid_<dec>_luid_0x<high>_0x<low>_phys_<dec>_eng_<dec>_engtype_<rest>` matched 2027/2027. First instance is WARP LUID `0x1cfc2`. GPU Adapter Memory 3 instances; GPU Process Memory 139. These process×engine rows feed `gpu.process` (cap 2,048; will truncate under load), not `gpu.engine` (cap 512; use D3DKMT nodes). |
| GPU-KMT | DXGI + optional DXCore + D3DKMT perf/statistics | primary Release x64 | DXGI/DXCore/D3DKMT enumerate 3 adapters; no per-sample D3D device | none retained beyond the probe | no | `keep` | Join by LUID only. NVIDIA `10de:2c02` discrete ~16 GiB; AMD `1002:13c0` integrated ~2 GiB; WARP `1414:008c` software. DXCore `IsHardware`/`IsIntegrated` match. `D3DKMTEnumAdapters2` with caller cap 32 (SDK `MAX_ENUM_ADAPTERS` is 16). NVIDIA: fan 0/3000 RPM, power 7.7%, 36.3 °C, max 88.0 °C. AMD: fan max 0, 49.0 °C. WARP perf `STATUS_INVALID_PARAMETER`. On-box WARP adapter is not a dedicated software-only VM. |
| NET | `GetIfTable2Ex` + `GetIfEntry2` + `NotifyIpInterfaceChange` | primary Release x64 | table 1.6 ms; `GetIfEntry2` 42 µs | `FreeMibTable` / cancel notify | no | `keep` | 58 rows, 27 up, 7 hardware (10 GbE + Hyper-V vNICs). Notify registered then cancelled immediately; no retained callback. |
| BAT | SetupAPI battery + `GetSystemPowerStatus` | primary; laptop pending | empty | none | no | `empty_on_primary` | AC=1, `BatteryFlag=128`, percent=255. Zero `GUID_DEVCLASS_BATTERY` devices and zero `GUID_DEVICE_BATTERY` interfaces. Laptop still required. |
| THRM | ACPI zone + storage temperature + GPU deci-C | primary; laptop pending | ACPI read completed immediately; storage temp 5/5 | overlapped handles closed | no | `keep` storage+GPU; ACPI value unusable | One `GUID_DEVICE_THERMAL_ZONE`; open+`IOCTL_THERMAL_READ_TEMPERATURE` with `THERMAL_WAIT_READ_TIMEOUT_IMMEDIATE` succeeded but tenths-K=0 (do not publish). `StorageDeviceTemperatureProperty` succeeded on all 5 opened disks. GPU temperatures from D3DKMT. Laptop still required for a real ACPI temperature. |
| FAN | `GUID_DEVICE_FAN` vs D3DKMT GPU RPM | primary | n/a | none | no | `presence_only_no_rpm` | Zero fan interfaces. NVIDIA D3DKMT current 0 with max 3000 is a valid idle GPU fan; AMD max 0 means no iGPU fan row. Presence must not invent motherboard RPM. |

Quiet cheap path (handle-count + per-logical CPU + K32): 181 µs then 675 µs. Quiet process walk: 16.8–21.1 ms (~31–93×). Contention run: 22 ms vs 40 ms. Gate stays `keep` for 1 s summary totals.

Surface spikes (Release x64, 2026-09-01): no WMI module delta after PDH, PerfLib, DXGI, DXCore, D3DKMT, disk IOCTL, IP Helper, or SetupAPI probes. `gpu.engine` cap 512 is D3DKMT-node scoped; GPU Engine counters are process×engine (~2027 today) and belong to `gpu.process`.

Harness: `Tests/SystemDataPhase0/SystemDataPhase0.exe` (Release x64). `test.ps1` runs it.

## Storage envelope (fast lane)

`RedXeDataValue` is 32 bytes on x64. Full previous snapshot grids at the `thread.list` 8,192-row cap plus `process.list`
plus a live NT walk (~2.2 MiB, start 1 MiB) exceed 8 MiB if prior samples are stored as complete value tables. Phase 0
therefore keeps **compact previous samples** (delta inputs only: identities and cumulative counters), keeps
`thread.list` at 8,192 (this host ~20k threads will `Truncated`), and **raises the fast-lane ceiling to 16 MiB**.
The NT walk reusable buffer starts at 1 MiB and caps at **4 MiB** (live 2.05–2.21 MiB); an 8 MiB walk buffer alone can
exhaust an 8 MiB total.

Shipped Phase 1 `sizeof(SystemDataSource)` stays under 1 MiB (`source.status` adds ~16 KiB). The 1 MiB
`SystemDataTests` object bound remains until `thread.list` lands.

After Phases 2–5, x64 Debug and Release `sizeof(SystemDataSource)` is **15,920,696** bytes (15.18 MiB), under the
16 MiB fast-lane ceiling with about 0.83 MiB headroom. Release `--benchmark` (2,048 synthetic process rows, two
measured collections plus a 64-collection CPU probe) reported zero heap/handle/private-byte growth and
`source_workers=0`. Wall time is not comparable to the seven-column 3.8 ms baseline because the row now has 31
columns and `ValidateSnapshot` runs inside the timed loop.

Release `--benchmark` 2026-09-01 closeout: `wall_us_per_collection=58085.2`,
`process_cpu_us_per_collection=54443.4`, `handles_delta=0`, `heap_bytes_delta=0`, `private_bytes_delta=0`,
`working_set_delta=0`, `source_storage_bytes=15920696`.

Release `--domains` 2026-09-01 (eight samples on a hot source after the contract suite):

| Dataset | Rows | wall_us_per_collection |
| --- | ---: | ---: |
| `source.status` | 18 | 1.5 |
| `system.summary` | 1 | 328.3 |
| `cpu.summary` | 1 | 120.1 |
| `cpu.logical` | 32 | 176.2 |
| `memory.summary` | 1 | 215.6 |
| `process.list` | 673 | 73908.6 |
| `thread.list` | 8,192 | 43757.8 |
| `network.interface` | 58 | 1253.2 |
| `network.protocol` | 6 | 22.0 |
| `storage.disk` | 5 | 2.9 |
| `storage.volume` | 8 | 273.7 |
| `gpu.adapter` | 3 | 328.7 |
| `gpu.engine` | 27 | 9611.3 |
| `gpu.process` | 2,044 | 1412.9 |
| `power.summary` | 1 | 150.0 |
| `battery.list` | 0 | 30.9 |
| `thermal.sensor` | 11 | 11318.1 |
| `fan.sensor` | 1 | 293.6 |
| full catalog batch | 18 datasets | 127535 |

`battery.list` is empty on this AC-only host. `thread.list` is truncated at 8,192. Collecting every dataset produced no
`wbemprox.dll` / `fastprox.dll` / `wbemcomn.dll` module delta. A 128-subscription cap was not adopted; the host remains
at 32.

| Bucket | Row cap | Current table | Previous sample | Strings / arena | Snapshot overlay | Measured / estimated total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `source.status` | 32 | 12,288 B values | n/a | static literals | included | ~13 KiB |
| `system.summary` / `cpu.summary` / `memory.summary` | 1 each | ~2 KiB | compact counters | n/a | included | ~4 KiB |
| `cpu.logical` | 1,024 | ~0.50 MiB (16×32 B) | ~48 KiB compact times | n/a | included | ~0.55 MiB |
| `process.list` | 2,048 | 0.44 MiB now (7 cols); ~1.4 MiB with appends | ~160 KiB compact | 128 KiB names | included | ~0.73 MiB now; ~1.8 MiB Phase 2 |
| `thread.list` | 8,192 | ~3.0 MiB at 12 cols | ~0.38 MiB compact | none (no names) | included | ~3.4 MiB; truncates on this host |
| `network.interface` + `network.protocol` | 256 + 16 | ~0.15 MiB | ~50 KiB compact | aliases borrowed then copied bounded | included | ~0.22 MiB |
| `storage.disk` + `storage.volume` | 128 + 256 | ~0.14 MiB | compact counters | bounded names | included | ~0.20 MiB |
| `gpu.adapter` + `gpu.engine` + `gpu.process` | 32 + 512 + 2,048 | ~0.55 MiB | compact engine times | display names | included | ~0.60 MiB. GPU Engine wildcard is 2027 process×engine rows today; `gpu.process` 2,048 will truncate. `gpu.engine` 512 is D3DKMT-node scoped. |
| Shared raw NT process buffer | n/a | reusable arena | n/a | image `UNICODE_STRING` in buffer | n/a | 2.05–2.21 MiB live; start 1 MiB, **cap 4 MiB** |
| OS PDH first-open (CPU) | | | | | | ~4.0 MiB private, reported separately from the 1 MiB device-lane budget |
| OS PerfLib GPU Engine blob | | | | | | 351 KiB query buffer; ~786 KiB private during the GPU Engine probe |
| **Fast-lane sum (all domains at cap)** | | | | | | **~11 MiB estimated**; ceiling **16 MiB** |

Device/sensor extra (SetupAPI, battery/thermal handles, DXCore, PDH/PerfLib, D3DKMT): budget 1 MiB source-owned plus
separately reported private-byte cost of OS libraries. No dedicated device-I/O thread in Phase 1: hung-IOCTL drain was
not proven; ACPI zone values stay unsupported; storage/GPU thermal may use slow cadence on the fast lane.

## Column freeze (required before Phase 1)

Existing `system.summary` and `process.list` columns stay first and unchanged. Percentages are finite Float64 in 0–100.
Unsigned counters use UInt64. Temperatures later use Celsius. `createTime` on threads is Unavailable until a named
WDK/SDK field is proven; identity is `(PID, TID)` plus Unavailable create time rather than inventing reserved bytes.

### `source.status` (32 rows, 5 s, Table, not LocalSensitive)

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `dataSetId` | Utf16 | | Catalog ID. |
| `availability` | UInt64 | | 0 Available, 1 Unsupported, 2 Degraded, 3 Backoff. |
| `backendTier` | UInt64 | | 0 none, 1 native, 2 win32, 3 counter. |
| `backendName` | Utf16 | | Static backend label. |
| `backendVersion` | Utf16 | | Static backend revision label. |
| `fallbackActive` | UInt64 | | 0 or 1. |
| `lastResult` | UInt64 | | 0 ok, 1 truncated, 2 failed, 3 empty. |
| `lastSuccessFileTime100ns` | UInt64 | 100ns | `Initializing` until the first success. |
| `lastDurationMicroseconds` | UInt64 | microseconds | Collector duration, not the batch timestamp. |
| `failureCount` | UInt64 | | Saturating diagnostic. |
| `truncationCount` | UInt64 | | Saturating diagnostic. |
| `retryFileTime100ns` | UInt64 | 100ns | 0 when not in backoff. |

### `system.summary` (1 row, 2 s in Phase 1; 1 s after cheap-path Phase 2, Table, not LocalSensitive)

Existing columns only in Phase 1. Later appends, if any, go at the end.

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `totalCpuPercent` | Float64 | percent | Existing. |
| `logicalProcessorCount` | UInt64 | | Existing. |
| `processCount` | UInt64 | | Existing; cheap totals path. |
| `threadCount` | UInt64 | | Existing; cheap totals path. |
| `handleCount` | UInt64 | | Existing; cheap totals path. |
| `totalPhysicalBytes` | UInt64 | bytes | Existing. |
| `availablePhysicalBytes` | UInt64 | bytes | Existing. |
| `usedPhysicalBytes` | UInt64 | bytes | Existing. |
| `committedBytes` | UInt64 | bytes | Existing. |
| `uptimeMilliseconds` | UInt64 | milliseconds | Existing. |

### `cpu.summary` (1 row, 1 s, Table, not LocalSensitive)

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `totalPercent` | Float64 | percent | Busy / (kernel+user) from per-logical sum or `GetSystemTimes`. |
| `userPercent` | Float64 | percent | |
| `kernelPercent` | Float64 | percent | Busy kernel (kernel − idle). |
| `idlePercent` | Float64 | percent | |
| `dpcPercent` | Float64 | percent | Unavailable until a named DPC field is proven (SDK reserved). |
| `interruptPercent` | Float64 | percent | Unavailable until a named interrupt-time field is proven. |
| `logicalProcessorCount` | UInt64 | | Active logical processors. |
| `coreCount` | UInt64 | | From topology. |
| `packageCount` | UInt64 | | From topology. |
| `numaNodeCount` | UInt64 | | From topology. |
| `currentFrequencyMhz` | UInt64 | MHz | Aggregate/unavailable if group mapping unproven. |
| `maximumFrequencyMhz` | UInt64 | MHz | |
| `contextSwitchRate` | Float64 | per second | Unavailable if `SystemPerformanceInformation` stays opaque. |
| `interruptRate` | Float64 | per second | Unavailable until named. |

### `cpu.logical` (1,024 rows, 1 s, Table, not LocalSensitive)

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `processorGroup` | UInt64 | | Identity with `groupRelativeIndex`. |
| `groupRelativeIndex` | UInt64 | | |
| `cpuSetId` | UInt64 | | Metadata, not identity. Unavailable if CPU sets fail. |
| `coreIndex` | UInt64 | | Cached topology. |
| `packageIndex` | UInt64 | | |
| `numaNode` | UInt64 | | |
| `efficiencyClass` | UInt64 | | From CPU sets; 0 is valid. |
| `parked` | UInt64 | | 0/1. |
| `allocated` | UInt64 | | 0/1. |
| `totalPercent` | Float64 | percent | Per logical 0–100. |
| `userPercent` | Float64 | percent | |
| `kernelPercent` | Float64 | percent | |
| `idlePercent` | Float64 | percent | |
| `dpcPercent` | Float64 | percent | Unavailable until named. |
| `interruptPercent` | Float64 | percent | Unavailable until named. |
| `interruptCount` | UInt64 | | Unavailable until named. |
| `interruptRate` | Float64 | per second | Unavailable until named. |
| `currentFrequencyMhz` | UInt64 | MHz | Unavailable if unmapped. |
| `maximumFrequencyMhz` | UInt64 | MHz | |

### `memory.summary` (1 row, 1 s, Table, not LocalSensitive)

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `totalPhysicalBytes` | UInt64 | bytes | Shared with `system.summary`. |
| `availablePhysicalBytes` | UInt64 | bytes | |
| `usedPhysicalBytes` | UInt64 | bytes | |
| `commitBytes` | UInt64 | bytes | |
| `commitLimitBytes` | UInt64 | bytes | K32 `CommitLimit`. |
| `commitPeakBytes` | UInt64 | bytes | K32 `CommitPeak`. |
| `cacheBytes` | UInt64 | bytes | Unavailable if only opaque native bytes exist. |
| `pagedPoolBytes` | UInt64 | bytes | K32 `KernelPaged`. |
| `nonPagedPoolBytes` | UInt64 | bytes | K32 `KernelNonpaged`. |
| `pageFaultRate` | Float64 | per second | Unavailable until a named cumulative fault field is proven. |
| `pageInRate` | Float64 | per second | Unavailable until named. |
| `pageOutRate` | Float64 | per second | Unavailable until named. |

### `process.list` appends after the existing seven columns (2,048 rows, 2 s, LocalSensitive)

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `parentProcessId` | UInt64 | | Named `InheritedFromUniqueProcessId` (class 252) or QIP `ProcessBasicInformation` inherited ID. Unavailable when denied. Never `Reserved3`. |
| `sessionId` | UInt64 | | SDK named `SYSTEM_PROCESS_INFORMATION.SessionId`. |
| `userTime100ns` | UInt64 | 100ns | Unavailable while SDK times stay Reserved. |
| `kernelTime100ns` | UInt64 | 100ns | Unavailable while SDK times stay Reserved. |
| `cpuUserPercent` | Float64 | percent | Share of machine; Unavailable without times. |
| `cpuKernelPercent` | Float64 | percent | Share of machine; Unavailable without times. |
| `peakWorkingSetBytes` | UInt64 | bytes | SDK named peak WS if present. |
| `virtualBytes` | UInt64 | bytes | SDK named virtual size. |
| `ioReadBytes` | UInt64 | bytes | Unavailable while I/O is Reserved. |
| `ioWriteBytes` | UInt64 | bytes | Unavailable while I/O is Reserved. |
| `ioOtherBytes` | UInt64 | bytes | Unavailable while I/O is Reserved. |
| `ioReadRate` | Float64 | bytes per second | From compact previous; Unavailable without I/O. |
| `ioWriteRate` | Float64 | bytes per second | |
| `pageFaultCount` | UInt64 | | Unavailable while reserved. |
| `pageFaultRate` | Float64 | per second | |
| `createTime100ns` | UInt64 | 100ns | Toolhelp/QIP/`GetProcessTimes` fallback; Unavailable if denied. Not parsed from Reserved CreateTime. |
| `cycleCount` | UInt64 | | Tier B `GetProcessInformation` / `PROCESS_CYCLE_TIME_INFORMATION` when available. |
| `basePriority` | UInt64 | | SDK named. |
| `affinityGroup` | UInt64 | | Never a lone `UInt64` affinity. Unavailable if no group APIs. |
| `affinityMask` | UInt64 | | Group-relative mask. |
| `wow64` | UInt64 | | 0/1 from `ProcessWow64Information` / `IsWow64Process2`. |
| `nativeArchitecture` | UInt64 | | `IMAGE_FILE_MACHINE_*` from `IsWow64Process2` process machine. |
| `subsystem` | UInt64 | | Unavailable until WDK `ProcessSubsystemInformation` layout exists; then `GetProcessInformation` fallback. |
| `criticalProcess` | UInt64 | | 0/1 from `ProcessBreakOnTermination` / `IsProcessCritical`. |

### `thread.list` (8,192 rows, 2 s, LocalSensitive)

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `processId` | UInt64 | | From `CLIENT_ID`. |
| `createTime100ns` | UInt64 | 100ns | Unavailable until a named field exists. |
| `threadId` | UInt64 | | From `CLIENT_ID`. |
| `userTime100ns` | UInt64 | 100ns | Unavailable while Reserved. |
| `kernelTime100ns` | UInt64 | 100ns | Unavailable while Reserved. |
| `cpuPercent` | Float64 | percent | Share of machine; Unavailable without times. |
| `priority` | UInt64 | | SDK named. |
| `basePriority` | UInt64 | | SDK named. |
| `threadState` | UInt64 | | SDK named `KTHREAD_STATE`. |
| `waitReason` | UInt64 | | SDK named `KWAIT_REASON`. |
| `contextSwitchCount` | UInt64 | | Unavailable until named. |
| `contextSwitchRate` | Float64 | per second | Unavailable until named. |

StartAddress, stack, and TEB are excluded.

### `network.interface` (256 rows, 1 s, LocalSensitive)

Stable key is `InterfaceLuid`. No MAC or IP columns.

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `interfaceLuid` | UInt64 | | `NET_LUID.Value`. Rows sorted by this key. |
| `alias` | Utf16 | | Mutable attribute. |
| `description` | Utf16 | | Mutable attribute. |
| `type` | UInt64 | | `IFTYPE`. |
| `operationalStatus` | UInt64 | | `IF_OPER_STATUS`. |
| `mediaConnectState` | UInt64 | | `NET_IF_MEDIA_CONNECT_STATE`. |
| `mtu` | UInt64 | bytes | |
| `receiveLinkSpeed` | UInt64 | bits per second | |
| `transmitLinkSpeed` | UInt64 | bits per second | |
| `inOctets` | UInt64 | bytes | |
| `outOctets` | UInt64 | bytes | |
| `inOctetRate` | Float64 | bytes per second | `Initializing` until a prior sample; reset on counter decrease. |
| `outOctetRate` | Float64 | bytes per second | |
| `inUcastPkts` | UInt64 | | |
| `outUcastPkts` | UInt64 | | |
| `inNUcastPkts` | UInt64 | | |
| `outNUcastPkts` | UInt64 | | |
| `inErrors` | UInt64 | | |
| `outErrors` | UInt64 | | |
| `inDiscards` | UInt64 | | |
| `outDiscards` | UInt64 | | |
| `utilizationPercent` | Float64 | percent | `8 * max(rx,tx) / linkSpeed * 100`; Unavailable when speed is 0. |

### `network.protocol` (16 rows, 1 s, not LocalSensitive)

Six aggregate rows: `ipv4`, `ipv6`, `tcp4`, `tcp6`, `udp4`, `udp6`. No endpoint tables.

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `protocolId` | Utf16 | | Static identity. |
| `addressFamily` | UInt64 | | 4 or 6. |
| `inCount` | UInt64 | | IP receives, TCP segments, UDP datagrams. |
| `outCount` | UInt64 | | |
| `inRate` | Float64 | per second | 32-bit IP Helper counters wrap; a decrease that is not a 32-bit wrap resets the row. |
| `outRate` | Float64 | per second | |
| `errorCount` | UInt64 | | |
| `discardCount` | UInt64 | | UDP `NoPorts` uses this column. |
| `retransmitCount` | UInt64 | | TCP only. |
| `currentEstablished` | UInt64 | | TCP only. |
| `resetCount` | UInt64 | | TCP `EstabResets` only. |

### `storage.disk` (128 rows, 1 s, LocalSensitive)

Identity from SetupAPI disk interfaces, volume extents, and `\\.\PhysicalDriveN`. Activity from overlapped
`IOCTL_DISK_PERFORMANCE` only. PDH `PhysicalDisk` is not mapped. `IOCTL_DISK_PERFORMANCE_OFF` is never issued.

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `diskNumber` | UInt64 | | `STORAGE_DEVICE_NUMBER.DeviceNumber`. |
| `displayName` | Utf16 | | Vendor/product; never the hardware serial. |
| `busType` | UInt64 | | `STORAGE_BUS_TYPE`. |
| `mediaKind` | UInt64 | | 0 unknown, 1 seek-penalty, 2 no-seek-penalty. |
| `capacityBytes` | UInt64 | bytes | `IOCTL_DISK_GET_LENGTH_INFO`. |
| `activePercent` | Float64 | percent | From idle-time delta, not read%+write%. |
| `idlePercent` | Float64 | percent | |
| `queueDepth` | UInt64 | | Instantaneous. |
| `readBytesPerSecond` | Float64 | bytes per second | Unavailable when IOCTL is rejected. |
| `writeBytesPerSecond` | Float64 | bytes per second | |
| `readOpsPerSecond` | Float64 | per second | |
| `writeOpsPerSecond` | Float64 | per second | |
| `splitCount` | UInt64 | | Delta this interval. |
| `avgReadLatencyMs` | Float64 | milliseconds | Unavailable when no completed reads. |
| `avgWriteLatencyMs` | Float64 | milliseconds | |
| `performanceAvailable` | UInt64 | | 0/1. |

### `storage.volume` (256 rows, 5 s, LocalSensitive)

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `volumeGuid` | Utf16 | | `FindFirstVolumeW` path. |
| `displayName` | Utf16 | | First mount path, or the GUID when unmounted. |
| `fileSystem` | Utf16 | | |
| `totalBytes` | UInt64 | bytes | |
| `freeBytes` | UInt64 | bytes | |
| `extentCount` | UInt64 | | |
| `firstDiskNumber` | UInt64 | | First extent; Unavailable when none. |

Activity is not published and is never inferred from whole-disk counters.

### `gpu.adapter` (32 rows, 1 s, not LocalSensitive)

Join DXGI and D3DKMT by LUID only. No D3D device. DXCore is not used; `integrated` is Unavailable.

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `adapterLuid` | UInt64 | | Rows sorted by this key. |
| `displayName` | Utf16 | | DXGI description. |
| `vendorId` | UInt64 | | |
| `deviceId` | UInt64 | | |
| `software` | UInt64 | | DXGI `DXGI_ADAPTER_FLAG_SOFTWARE`. |
| `integrated` | UInt64 | | Unavailable without DXCore. |
| `dedicatedBytes` | UInt64 | bytes | Capacity, not current use. |
| `sharedBytes` | UInt64 | bytes | |
| `utilizationPercent` | Float64 | percent | Unavailable (node running-time reserved). |
| `dedicatedUsedBytes` | UInt64 | bytes | Unavailable until machine scope is proven. |
| `sharedUsedBytes` | UInt64 | bytes | Unavailable. |
| `memoryClockHz` | UInt64 | hertz | D3DKMT perf; Unavailable on WARP. |
| `maxMemoryClockHz` | UInt64 | hertz | |
| `powerPercent` | Float64 | percent | Tenths of a percent from D3DKMT. |
| `temperatureC` | Float64 | celsius | Deci-Celsius from D3DKMT. |
| `warningTemperatureC` | Float64 | celsius | Caps. |
| `maxTemperatureC` | Float64 | celsius | Caps. |
| `fanRpm` | UInt64 | rpm | Unavailable when `MaxFanRpm` is 0. |
| `maxFanRpm` | UInt64 | rpm | AMD iGPU max 0 means no fan row. |

### `gpu.engine` (512 rows, 1 s, not LocalSensitive)

D3DKMT nodes. GPU Engine counters belong to `gpu.process`.

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `adapterLuid` | UInt64 | | |
| `physicalAdapterIndex` | UInt64 | | |
| `nodeOrdinal` | UInt64 | | |
| `engineClass` | UInt64 | | From node metadata when present. |
| `friendlyName` | Utf16 | | |
| `currentFrequencyHz` | UInt64 | hertz | Node perf. |
| `maxFrequencyHz` | UInt64 | hertz | |
| `voltageMv` | UInt64 | millivolts | |
| `utilizationPercent` | Float64 | percent | Unavailable. |

### `gpu.process` (2,048 rows, 2 s, LocalSensitive)

One cached `\\GPU Engine(*)\\Utilization Percentage` PDH query. First collect is empty (`Initializing` after the
second collect when a value is present). Dedicated/shared process GPU memory is not published.

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `processId` | UInt64 | | Parsed from the instance grammar. |
| `adapterLuid` | UInt64 | | |
| `physicalAdapterIndex` | UInt64 | | |
| `engineOrdinal` | UInt64 | | |
| `engineType` | Utf16 | | Remainder after `engtype_`. |
| `utilizationPercent` | Float64 | percent | Clamped 0–100. |

### `power.summary` (1 row, 5 s, not LocalSensitive)

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `acOnline` | UInt64 | | 0/1 from `ACLineStatus`; 255 is Unavailable. |
| `batteryPresent` | UInt64 | | 0 when `BatteryFlag` has 128; flag 255 is Unavailable. |
| `charging` | UInt64 | | `BatteryFlag` bit 8. |
| `batterySaver` | UInt64 | | `SystemStatusFlag` bit 0. |
| `chargePercent` | Float64 | percent | 255 is Unavailable. |
| `remainingSeconds` | UInt64 | seconds | `0xFFFFFFFF` is Unavailable. |
| `fullLifeSeconds` | UInt64 | seconds | |
| `systemS3` | UInt64 | | `GetPwrCapabilities`. |
| `systemS4` | UInt64 | | |
| `hiberFilePresent` | UInt64 | | |
| `thermalControl` | UInt64 | | |

### `battery.list` (32 rows, 5 s, LocalSensitive)

Empty on the primary AC-only desktop. No serial or unique-ID columns.

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `deviceId` | Utf16 | | Device interface path. |
| `deviceName` | Utf16 | | |
| `chemistry` | Utf16 | | Four-character chemistry. |
| `designedCapacity` | UInt64 | | Relative when `relativeCapacity` is 1. |
| `fullChargedCapacity` | UInt64 | | |
| `currentCapacity` | UInt64 | | |
| `rateMw` | Float64 | milliwatts | Signed; unknown sentinel Unavailable. |
| `voltageMv` | UInt64 | millivolts | |
| `temperatureC` | Float64 | celsius | Tenths-Kelvin conversion; implausible values dropped. |
| `relativeCapacity` | UInt64 | | 0/1 from `BATTERY_CAPACITY_RELATIVE`. |
| `cycleCount` | UInt64 | | 0 is Unavailable. |
| `estimatedTimeSeconds` | UInt64 | seconds | |
| `powerState` | UInt64 | | `BATTERY_STATUS.PowerState`. |

### `thermal.sensor` (128 rows, 10 s, LocalSensitive)

Re-projects GPU, storage, and battery readings. ACPI tenths-K=0 is not published.

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `sensorId` | Utf16 | | `gpu-<luid hex>`, `disk-<n>-<sensor>`, battery path, or ACPI path. |
| `deviceKind` | UInt64 | | 0 GPU, 1 storage, 2 battery, 3 ACPI. |
| `displayName` | Utf16 | | |
| `temperatureC` | Float64 | celsius | |
| `warningTemperatureC` | Float64 | celsius | |
| `criticalTemperatureC` | Float64 | celsius | |

### `fan.sensor` (128 rows, 10 s, LocalSensitive)

| columnId | Type | Unit | Notes |
| --- | --- | --- | --- |
| `sensorId` | Utf16 | | `gpu-fan-<luid hex>`. |
| `deviceKind` | UInt64 | | 0 GPU. |
| `displayName` | Utf16 | | Adapter name. |
| `fanRpm` | UInt64 | rpm | Idle 0 with non-zero max is valid. |
| `maxFanRpm` | UInt64 | rpm | |
| `active` | UInt64 | | 1 when current RPM is non-zero. |

### Batch ABI (frozen)

Copied into `Data.h` in Phase 1:

- `RedXeDataCollectMaximumDataSets` = 32
- `RedXeDataCollectRequest` `sizeof` **24**: `sizeBytes`, borrowed `dataSetIds`, `dataSetCount`
- `RedXeDataCollectResult` `sizeof` **32**: `sizeBytes`, `snapshotCount`, common `sequence`, common `timestampFileTime100ns`, borrowed `snapshots`
- `IRedXeDataSource` IID **`C3A81F6E-2D47-4B90-A1E5-6F8C9D0B3E21`** (`CollectSnapshots` replaces `CollectSnapshot`)
- `RedXeDataSetFlagDeviceLane` = `1 << 2`
- Successful snapshots are never null; zero-row tables are allowed. Unknown/duplicate/empty/oversized requests fail the whole call.
- `RedXeDataQuality` stays `Good` / `Unavailable` / `Initializing`. **No `Stale`.**
- No dedicated device-I/O thread in Phase 1.

## WMI prohibition check

For each spike that activates acquisition:

1. Snapshot loaded modules.
2. Run the spike.
3. Snapshot loaded modules again.

`wbemprox.dll`, `fastprox.dll`, and `wbemcomn.dll` must not appear in the delta. Also fail the spike if it includes
`Wbemidl.h`, links `wbemuuid.lib`, creates an `IWbem*` service, or shells `wmic.exe` / PowerShell CIM.

NT and surface spikes on the primary host produced no WMI module delta. Phase 6 `SystemDataTests` full-catalog
collection also produced no WMI module delta.

## Ratification checklist (Phase 1 entry)

- [x] Baseline re-record table has three Release x64 runs on the primary machine.
- [x] Native-class matrix has a disposition for every SDK `um\winternl.h` enumerator plus recorded legacy extras.
      WDK `km` headers are not installed; remaining non-SDK classes stay `unknown-layout`/`pending` and are **out** of
      runtime code. ARM64 `sizeof` is locked to the same 64-bit values as x64 via `static_assert` in
      `Tests/SystemDataPhase0` (`PROCESS_BASIC_INFORMATION` 48, `SYSTEM_PROCESS_INFORMATION` 256,
      `SYSTEM_THREAD_INFORMATION` 80, `SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION` 48, `SYSTEM_HANDLECOUNT_INFORMATION` 12,
      `SYSTEM_BASIC_INFORMATION` 64, `SYSTEM_PERFORMANCE_INFORMATION` 312).
- [x] Cheap totals path for `system.summary` is `keep` (`SystemHandleCountInformation` + `K32GetPerformanceInfo`).
- [x] NT process/CPU spikes have gates. Public QIP trio (`ProcessBasicInformation`, Wow64, BreakOnTermination) is
      **in**. WDK-only extras (`ProcessSubsystemInformation` 75, private I/O/VM classes) are **out** until layouts exist;
      use Tier B (`GetProcessInformation` / `IsWow64Process2` / `IsProcessCritical`). Estimated ~20 ms at 2,048
      accessible PIDs; keep off the 1 s path.
- [x] Counter, GPU, disk-perf side-effect, and (as available) battery/thermal/fan gates are recorded on the primary
      host. Laptop battery/ACPI-zone, dedicated WARP-only VM, and Intel hybrid P/E remain **Phase 4/5 validation**, not
      Phase 1 blockers.
- [x] Storage envelope: compact previous samples; NT buffer cap 4 MiB; `thread.list` 8,192 truncates; fast-lane ceiling
      **16 MiB**. Phase 1 source object stays under 1 MiB.
- [x] Column freeze lists `source.status`, `cpu.summary`, `cpu.logical`, `memory.summary`, `process.list` appends,
      `thread.list`, and the sized batch ABI.
- [x] Parent plan catalog, budgets, and batch records match this file.
- [x] Phase 1 may now revise `Data.h` and `SystemData.cpp` in the same change as this freeze.
