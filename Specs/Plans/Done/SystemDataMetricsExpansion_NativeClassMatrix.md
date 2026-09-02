# System data expansion — native information-class matrix

Status: `COMPLETE` working artifact of
[`SystemDataMetricsExpansion_2026-09-01.md`](SystemDataMetricsExpansion_2026-09-01.md)
Created: 2026-09-01
Last measured: 2026-09-01 (primary x64). Legacy code extras inventoried; WDK `km` still absent.

This file is not a separately indexed WIP plan. SDK `10.0.26100.0` `um\winternl.h` enumerators are inventoried below.
WDK `km` headers were not present on the primary machine (`...\Include\10.0.26100.0\km` does not exist), so
non-SDK classes remain `pending` / `unknown-layout`. Legacy code extras are listed after the SDK table.
Evidence and gates go in
[`SystemDataMetricsExpansion_Phase0Evidence.md`](SystemDataMetricsExpansion_Phase0Evidence.md).

## Inventory method

1. Enumerate `SYSTEM_INFORMATION_CLASS` from Windows 11 SDK `10.0.26100.0` `winternl.h` and from the selected WDK
   `ntddk.h` / `ntexapi.h` available to VS 2026 (`v145`).
2. Enumerate `PROCESSINFOCLASS` from the same headers.
3. Assign exactly one disposition: `used`, `duplicate`, `sensitive`, `mutating`, `kernel-only`, `opaque`,
   `unbounded`, `unsupported`, `unknown-layout`, or `pending`.
4. Only `used` classes may enter runtime code, and only while a dependent dataset is active.
5. Record x64 and ARM64 `sizeof` and consumed field offsets. A successful `NtQuery*` return is not layout proof.

Do not loop numeric classes at runtime. This matrix is the allowlist.

## `NtQuerySystemInformation`

SDK `um\winternl.h` exposes exactly these enumerators. x64 sizes are from `SystemDataPhase0` on the primary machine.
ARM64 sizes are not yet measured.

| Class | Number | Disposition | Layout source | Min build / arch | Size x64 / ARM64 | Fields consumed | Units / sentinels | Access | Cost note | Tier B fallback | Owning columns |
| --- | ---: | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `SystemBasicInformation` | 0 | `used` | SDK `winternl.h` | Win10+ x64 measured | 64 / pending | `NumberOfProcessors` (`CCHAR`) only | count; saturates at 127 | none | ~µs | `GetActiveProcessorCount` | topology cross-check; not the 1,024-logical cap |
| `SystemPerformanceInformation` | 2 | `opaque` | SDK `BYTE Reserved1[312]` | Win10+ x64 query succeeds | 312 / pending | none | reserved | none | returned 312 | `K32GetPerformanceInfo` | none until a named layout exists |
| `SystemTimeOfDayInformation` | 3 | `opaque` | SDK `BYTE Reserved1[48]` | Win10+ x64 query succeeds | 48 / pending | none | reserved | none | returned 48 | `GetTickCount64` | none until a named layout exists |
| `SystemProcessInformation` | 5 | `used` | SDK `winternl.h` chain | Win10+ x64 measured | 256 per process record / pending | `NextEntryOffset`, `NumberOfThreads`; do not parse `Reserved*` or `ImageName` until column freeze | 100 ns times remain reserved in the public struct | none | ~17–21 ms for ~650 processes / ~20k threads; ~2.05 MiB buffer | Toolhelp | `process.list`, `thread.list` |
| `SystemProcessorPerformanceInformation` | 8 | `used` | SDK `winternl.h` | Win10+ x64 measured | 48 per logical / pending | `IdleTime`, `KernelTime`, `UserTime`. Do not consume `Reserved1`/`Reserved2` (DPC/interrupt) yet | 100 ns cumulative | none | included in ~0.2–0.7 ms cheap batch | `GetSystemTimes` (summary only) | `cpu.summary`, `cpu.logical` named times |
| `SystemInterruptInformation` | 23 | `opaque` | SDK `BYTE Reserved1[24]` | | 24 / pending | none | reserved | none | not queried | none | none |
| `SystemExceptionInformation` | 33 | `opaque` | SDK `BYTE Reserved1[16]` | | 16 / pending | none | reserved | none | not queried | none | none |
| `SystemRegistryQuotaInformation` | 37 | `pending` | SDK named allowed/used | | pending | none yet | bytes | none | not queried this pass | none required | optional later |
| `SystemLookasideInformation` | 45 | `opaque` | SDK `BYTE Reserved1[32]` | | 32 / pending | none | reserved | none | not queried | none | none |
| `SystemCodeIntegrityInformation` | 103 | `pending` | SDK named options bitmask | | pending | none yet | documented bits | none | not queried this pass | none | optional later |
| `SystemPolicyInformation` | 134 | `opaque` | SDK reserved pointers/ulongs | | pending | none | reserved | none | not queried | none | none |
| `SystemBasicProcessInformation` | 252 | `used` | SDK `winternl.h` | Win11 26100.4770+; x64 measured | variable chain; 51,312 bytes / 646 processes | `NextEntryOffset`, `UniqueProcessId`, `InheritedFromUniqueProcessId`, `SequenceNumber`, `ImageName` | parent PID is a named HANDLE | none | ~51 KiB vs ~2 MiB full walk; identity/parent only | Toolhelp `th32ParentProcessID` | parent PID; not a 1 s summary dependency |
| `SystemHandleCountInformation` | 253 | `used` | SDK `winternl.h` | Win10+ x64 measured | 12 / pending | `ProcessCount`, `ThreadCount`, `HandleCount` | object counts | none | exact match vs `K32GetPerformanceInfo` on quiet samples | `K32GetPerformanceInfo` | `system.summary` counts |
| `SystemExtendedProcessInformation` | | `unknown-layout` | not in SDK `winternl.h` | | | | | | | Toolhelp | pending WDK/legacy |
| `SystemKernelVaShadowInformation` | | `unknown-layout` | not in SDK `winternl.h` | | | | | | | none | pending WDK |
| `SystemSpeculationControlInformation` | | `unknown-layout` | not in SDK `winternl.h` | | | | | | | none | pending WDK |
| `SystemQueryPerformanceCounterInformation` | | `unknown-layout` | not in SDK `winternl.h` | | | | | | | none | pending WDK |

There are no further `SYSTEM_INFORMATION_CLASS` enumerators in SDK `10.0.26100.0` `um\winternl.h`.

## Legacy code extras (not in SDK `winternl.h`)


headers are still not installed, so layouts stay unproven. Do not query these by numeric class until a current header
names the fields.

| Class | Number | Disposition | Layout source | Notes |
| --- | ---: | --- | --- | --- |
| `SystemBasicInformation` | 0 | `used` | SDK (see above) | Legacy `perfpage.cpp` / `main.cpp`. |
| `SystemPerformanceInformation` | 2 | `opaque` | SDK reserved | Legacy `perfpage.cpp` / `main.cpp` / `procpage.cpp`. |
| `SystemProcessInformation` | 5 | `used` | SDK (see above) | Legacy `procpage.cpp`. |
| `SystemProcessorPerformanceInformation` | 8 | `used` | SDK (see above) | Legacy `perfpage.cpp`. |
| `SystemFileCacheInformation` | | `unknown-layout` | legacy `procpage.cpp` only | Not in SDK `winternl.h`. Pending WDK `ntexapi.h`. Do not parse or probe by guessed number. |

No `SystemExtendedProcessInformation` use in this legacy code tree.

## `NtQueryInformationProcess`

Open at most one transient process handle at a time. Do not issue a class whose data is already validated in
`SystemProcessInformation`. `ProcessDebugPort`, `ProcessImageFileName`, `ProcessCommandLineInformation`,
`ProcessTelemetryIdInformation`, PEB dereference, environment, and debug-object classes stay `sensitive` / excluded.

SDK `um\winternl.h` exposes exactly five enumerators.

| Class | Number | Disposition | Layout source | Min build / arch | Size x64 / ARM64 | Unique fields | Access | Cost at 2,048 rows | Tier B fallback | Owning columns |
| --- | ---: | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `ProcessBasicInformation` | 0 | `used` | SDK `winternl.h` | Win10+ x64 48 bytes | 48 / pending | `UniqueProcessId` only. Do not follow `PebBaseAddress`. Do not treat `Reserved3` as parent PID; use class 252’s named `InheritedFromUniqueProcessId`. | `PROCESS_QUERY_LIMITED_INFORMATION` | ~4.3 ms for 428 accessible of 653 live; ~20 ms estimated if 2,048 accessible | PID already in bulk row | PID cross-check only |
| `ProcessDebugPort` | 7 | `sensitive` | SDK | | | excluded | | | none | none |
| `ProcessWow64Information` | 26 | `used` | SDK | Win10+ x64 | `ULONG_PTR` / pending | WOW64 non-zero | same | included in the QIP live pass | `IsWow64Process2` | WOW64 / architecture |
| `ProcessImageFileName` | 27 | `sensitive` | SDK | | | excluded | | | none | none |
| `ProcessBreakOnTermination` | 29 | `used` | SDK | Win10+ x64 | `ULONG` / pending | critical flag | same | included in the QIP live pass | `IsProcessCritical` | critical-process |
| `ProcessSubsystemInformation` | 75 | `unknown-layout` | not in SDK `winternl.h` | | | | | | `GetProcessInformation` if present | pending WDK |
| I/O, VM, times, handle, session, cycle, priority classes | | `unknown-layout` | not in SDK `winternl.h` | | | | | | documented `GetProcess*` | pending WDK; prefer bulk class 5 named fields first |

There are no further `PROCESSINFOCLASS` enumerators in SDK `10.0.26100.0` `um\winternl.h`.

## Layout proof checklist (per `used` class)

- [x] Numeric value confirmed in SDK `10.0.26100.0` `um\winternl.h` for every `used` class above.
- [x] Unused/reserved bytes are not interpreted (`SystemPerformanceInformation`, DPC/interrupt reserved CPU fields, `PebBaseAddress`, `ProcessBasicInformation` reserved slots).
- [x] Returned-length rules checked on x64 for handle-count (exact 12), processor-performance (48 × active logical), process walk (`NextEntryOffset` chain), basic-process walk, and `ProcessBasicInformation` (exact 48).
- [ ] ARM64 sizes recorded.
- [ ] Malformed-buffer / short-buffer / unknown-enum fixtures in adapter tests (Phase 1+).
- [x] Tier B fallback named for every `used` class.
- [ ] Owning dataset column IDs frozen in the Phase 0 column freeze (existing summary/process columns only so far).
