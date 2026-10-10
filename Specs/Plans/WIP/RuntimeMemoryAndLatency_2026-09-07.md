# Runtime memory, lag, and whole-process resource budget

Status: `ACTIVE`
Created: 2026-09-07 (rewritten the same day after a live Release measurement)
Refreshed: 2026-10-10 against `a466363` on `codex/runtime-probe`; P0.1 `0971350`, P2.7 `a466363`
Owner: host renderer, scheduler, plugin runtime, DxUi consumer accounting, and every bundled plugin's retained cost

## Goal

This plan records measured whole-process resource and latency work. The September baseline led to adapter selection,
waitable presentation, surface-lifetime and chrome changes that are now present. It is historical evidence, not a
measurement of today's code. P0.1 establishes a repeatable production baseline before choosing another optimization.
Proposals below remain non-normative; current domain contracts and later accepted resource envelopes take precedence.

Product requirements restated by the owner on 2026-09-07:

1. RedXe MUST render on the adapter that owns the display it is on, and MUST follow the window when it moves to a
   display owned by another adapter.
2. Data providers use arena storage: one reserved region per provider, committed per table on first use, no
   per-table small allocations, no memset of tables nobody subscribed to.
3. `process.list` MUST NOT open a process handle per process.
4. A page change is uncommon and MUST be fluid; every byte the swipe needed beyond the settled page MUST be released
   once the page is stable.
5. All budgets are Release x64 on hardware.

This file does not change normative contracts by itself. Durable budgets discovered here MUST land in
[`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md) and
[`../../UI/UI_XeneonDisplayWindowing.md`](../../UI/UI_XeneonDisplayWindowing.md) at closeout, with plugin and
dashboard consumers updated in the same change.

Owning contracts:

- [`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md)
- [`../../UI/UI_XeneonDisplayWindowing.md`](../../UI/UI_XeneonDisplayWindowing.md) (adapter-of-output rule)
- [`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md)
- [`../../UI/UI_Dashboard.md`](../../UI/UI_Dashboard.md)
- [`../../Core/Core_Settings.md`](../../Core/Core_Settings.md)
- [`../../Core/Core_DxUiIntegration.md`](../../Core/Core_DxUiIntegration.md)
- [`../../Plugins/Plugins_AVControl.md`](../../Plugins/Plugins_AVControl.md) (AV surface and helper process only)

Coordinate, do not fork:

- [`RFC_Plugins_AVControl.md`](RFC_Plugins_AVControl.md) still owns real IME/AT, G1 audio, G2 camera, and matched
  text/UIA performance. This plan only accounts AV's retained surfaces, coordinator, and helper-process cost.
- The network lane and forecast fetch are owned by `Specs/Plugins/Plugins_API.md` and `Specs/Plugins/Plugins_Weather.md`
  (landed via [`../Done/WeatherPlugin_2026-09-04.md`](../Done/WeatherPlugin_2026-09-04.md)).
- [`PluginDashboardRemainingCloseout_2026-09-02.md`](PluginDashboardRemainingCloseout_2026-09-02.md) still owns
  leftover architecture gates. Do not open host primitive batching from this file.
- [`PluginBoundaryHardening_2026-09-03.md`](PluginBoundaryHardening_2026-09-03.md) already made `PluginHost`
  process-scoped. Remaining G2/H3 work stays there.


## Current-code checkpoint (2026-10-10)

### Execution ledger

The owner requested all remaining work with a commit per step on 2026-10-10. Conditional proposals still require
current evidence and preserve visual quality and the later normative contracts. Each implemented step records
its validation and measurement; failed or unavailable qualification remains explicit.

| Step | State | Evidence / commit |
| --- | --- | --- |
| P0.1 | Complete | Production probe and accepted Release baseline; `0971350`. |
| P2.7 | Complete | Sample-driven scheduling, full gates and matched 13.7–13.9% submission reduction; `a466363`. CPU saving remains unestablished. |
| P0.2 | In progress | PresentMon 2.5.1 works without elevation on this user's process. Fresh System trace and 65 s probe at `runtime-2026-10-10-p02-system`: 12.287 submissions/s, 2.706% of one core. WPR CPU start fails with profiling-policy error `0xc5585011`; sampled CPU stacks remain unavailable. Idle captures supply no input-to-photon observations. |
| P2.5 | Complete | `9b9cf15`: writer-owned date/policy cache removes both directory scans and path construction from ordinary lines. 1,000 same-day checks leave a late expired file untouched; policy and UTC changes reap it. Actual idle-policy flush passes. Release 16 scopes, Debug 15 native scopes and ARM64 Release compilation pass; `.build/receipts/runtime-2026-10-10-p25/`. No whole-process CPU reduction claimed for this infrequent log workload. |
| P2.4 | Complete | Render failure debug output shares the JSONL transition guard. 400 failing primary frames emit three reports; a new primary instance emits its own report. Whole-frame recovery resets identity; partial tile/raised recovery does not. Adds a 4-byte mask and no steady slot scan/allocation. Release 16 scopes, Debug 15 native scopes and ARM64 Release compilation pass; `.build/receipts/runtime-2026-10-10-p24/`. This removes repeated failure-path debug calls, not measured ordinary CPU. |

| Area | Verified current code | Consequence for this plan |
| --- | --- | --- |
| Device/presentation | `Renderer::FindAdapterForMonitor`, `EnsureDeviceForWindowMonitor`, unconditional `RecordDeviceIdentity`, waitable flip swap chain; `Application::CheckDeviceAdapter` and `WaitForFrameLatency` | P1.1–P1.3 implementation exists. Fresh adapter/PresentMode/input-latency attribution is P0.2, not a probe counter. |
| Chrome/native widgets | Both top-level styles use `WS_EX_NOREDIRECTIONBITMAP`; `DashboardHost` creates layered native containers. Release Gallery has Triangle and Matrix, no GdiOrbit. | The September P1.4 rejection is superseded. Two continuous widgets share one host presentation cadence; no template change belongs to P0.1. |
| DxUi/AV | `Dependencies/DxUi.lock.json` pins `271bd54be24eadf3f03ba5221d1be7be069860f2`, API 3. Views release hidden surfaces; caches/peaks are bounded. The I19 adoption envelope was accepted on 2026-09-13. | Old sibling-worktree and library receipts are dated history. AV real IME/AT/audio/camera gates remain with the HOLD RFC. P0.1 exports only 64-byte read-only POD statistics. |
| SystemData | `CollectProcessesFromWalk` still opens each process; source members still use fixed zero-initialized arrays. `PluginHost::_providers` owns sources until shutdown. | P2.1, P4.1 and source reclamation in P3.6 remain candidates. The later normative contract accepts the current bounded per-process baseline; changing columns needs a new measured contract change. |
| ProcessViewer | P2.7 parks settled viewers (`S_FALSE`), snaps unchanged/sub-pixel targets, and schedules only visible easing/pulses at 33 ms. Eases use a fixed start pose and one host timestamp for both views; auxiliary observations do not restart them. `PluginHost::Deliver` still coalesces the initial invalidation. | Matched control: 14.126 submissions/s, 2.986% of one core. Two after-runs: 12.163/12.184 submissions/s, 3.515/3.069% CPU. Submission reduction is 13.7–13.9%; CPU improvement is not established. Proposed System budgets remain open. |
| Logging/settings | P2.5 caches the writer's UTC date and swept policy; ordinary lines perform no directory enumeration or path allocation. Settings persistence flushes its temp file and checks the loaded document before replacement, but still runs synchronously. | P2.5 is complete. P2.6 must preserve the current flush, merge and stamp guarantees; the old proposed write-through replacement is obsolete. |
| Retained plugin work | Launcher still reserves 32 GPU icon slices even for an empty authored list. Viewer/Weather atlas upload paths and DeskClock's 1 ms animation deadline still exist. | P2.8/P4.3/P4.6 need current measurements; the September cost/line-number table below must not be used as current proof. |
| Probe | Opt-in `RuntimeProbe` shares existing message/frame-latency waits and the 32×1024 JSONL ring; disabled path has no diagnostic deadline/query. AV queries never load a module or prepare a view. | P0.1 changes diagnostics, not page rendering or service behavior. Invalid/unavailable counters stay explicitly marked. |

### P0.1 implementation and evidence

- [x] `--probe[=seconds]`, strict 1–3600 integer interval, default 5, duplicates rejected; valid option ignored by self-test.
- [x] Shared process-memory/thread helpers in `Common/ProcessResources.h`; CPU, handle, module, GDI and USER counts.
- [x] Read-only device-adapter memory, successful-present and DXGI-statistics deltas; resize/recreation/disjoint/clock-reset handling.
- [x] Live AV tile+overlay aggregation across instances and a staged page, without exposing DxUi C++ types.
- [x] One complete versioned JSONL record through the existing writer, including extreme numeric values; no public-log truncation.
- [x] Disabled and competing deadlines, no catch-up burst, CLI negatives, WARP queries, logger drain, AV aggregation/hide/destruction and wrong-thread rejection tests.
- [x] Durable behavior in Core performance, UI modes and AV contracts; end-user command line documented.
- [x] Debug/Release x64 full native coverage, independent tooling, Release ARM64 compilation and skill validation; identities and logs retained below.
- [x] Release inactive baseline: three fresh starting-page processes, 65-second phases, valid zero-present evidence and unchanged source/binary hashes.
- [x] Visible A/B/C and all-page return D baseline, followed by hidden/restored/minimized phases; valid display-on and page/state evidence, with unchanged production inputs.
- [x] Correct collector acceptance of minimized visible+suspended state; saved-capture replay accepts it and rejects six invalid-state/record variants.

`measure-runtime.ps1` retains portable settings, raw logs, page/phase maps, machine/window context and before/after
source/binary hashes under `.build/receipts/`. It uses fresh Release processes for A/B/C and one process for the
all-page return plus hidden/minimized portions of D/E. It posts only to its owned non-activating window; no physical
input or normal user-settings write. The safe measurement variant disables Logicon and the tray and uses a
normal XENEON popup shown without activation. It requires a 2560×720, 96-DPI panel and is labelled separately
from the exact shipped-services budget. The initial dock pilot was clamped to half height and used expensive
all-system thread iteration; it is excluded from the final baseline. Process-local PSS thread-ID snapshots and
PSAPI module counts replace that sampling path without adding a worker or changing normal frame policy.
`-InactiveOnly` collects three fresh hidden starting pages instead; its accepted receipt is evidence for E only.

The probe reports host-process resources, adapter-reported usage and counters. Helper-process costs, whole-machine
GPU attribution, PresentMode, presented-frame/input latency, an actively raised AV surface and sub-interval swipe
peaks require additional captures. A–H is the overall measurement matrix; P0.1 completion does not claim P0.2 or
real-client/AV release qualification.

### Release baseline and next cut

Receipt: `.build/receipts/runtime-2026-10-10-p01-release-baseline/` (`README.md`, raw per-run JSONL, initial settings,
page/phase/window context, `summary.json`, `samples.json`, composition and SHA-256 inventories). Release x64
1.0.275, `7b38d4b` plus this working change, Windows 11 26300, Ryzen 9 9950X3D, AC power, 2560×720 at 96 DPI.
`device-created` identifies AMD Radeon(TM) Graphics, LUID `00000000-0001C33F`, hardware, owning the window monitor.
The DxUi pin/provenance is `271bd54be24eadf3f03ba5221d1be7be069860f2`, API 3, build fingerprint `3f78ff279d03b117`.
Logicon/tray are disabled. Visible settled samples report display-on and no suspension/occlusion/navigation/motion.

Each 65-second settled phase has 13 valid samples and about 60 seconds of complete intervals. Private/working-set
values and counts are medians; CPU is process CPU as a percentage of one core; present rate uses successful
submission deltas. Short visit/return phases are in the receipt but do not establish settled budgets.

| Phase | Private MiB | Working-set MiB | Threads | Handles | Core CPU % | Submissions/s | Query p95 ms | AV surface bytes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A: fresh Matrix Focus | 37.32 | 49.48 | 42 | 1149 | 1.848 | 60.152 | 0.431 | 0 |
| B: fresh Plugin Gallery | 93.38 | 93.98 | 45 | 1358 | 2.630 | 61.283 | 1.992 | 1474560 |
| C: fresh System | 58.32 | 73.97 | 43 | 1180 | 2.291 | 21.711 | 0.547 | 0 |
| D: cold Matrix in return run | 39.20 | 49.61 | 42 | 1148 | 2.448 | 60.167 | 0.551 | 0 |
| D: returned Matrix, settled | 125.00 | 113.98 | 43 | 1400 | 2.447 | 60.152 | 1.828 | 0 |
| E: hidden after return | 124.98 | 113.97 | 42 | 1400 | 0.000 | 0 | 0.520 | 0 |
| E: minimized after restore | 125.00 | 113.99 | 43 | 1399 | 0.000 | 0 | 0.456 | 0 |

Hidden/minimized phases contain five samples each and about 20 seconds of complete intervals. Their first
boundary-straddling interval is excluded. CPU counters are coarse and p95 estimates have only 5–13 observations;
these are diagnostic receipts, not latency/timing acceptance. Gallery's settled AV tile has one surface allocation,
four preparations and 1474560 live bytes; composition does not allocate another surface. AV has no live widgets or
surfaces after the return. No builds/tests ran during the visible captures.

**P0.1 selected P2.7, ProcessViewer wake discipline.** Fresh System submits 10.9 times the proposed
2/s target and exceeds the 1.5% whole-process CPU target. Pre-P2.7 source showed every successful snapshot starting
320 ms of easing and every easing render requesting another frame; the host already invalidates after delivery.
First avoid easing unchanged/sub-pixel observations and idle scheduled wakes; then measure a bounded cadence for
necessary animations while preserving ranks, pulses and visible quality. This is the most direct measured behavior
to change before more invasive storage/lifetime work. The P2.7 comparison below now measures this cut; P0.2 thread
attribution remains required to choose a CPU optimization.

**Memory follow-up: P3.6, then P4.1 as a separate storage cut.** Returning to Matrix retains **85.80 MiB private**,
64.37 MiB working set, one thread, 252 handles, 31 modules and 22.25 MiB of device-adapter local usage above the
same process's cold Matrix. This misses the proposed A + 4 MiB target. These are different accounting domains,
not additive totals; retained images/driver state are not automatically leaked widget resources. Source reclamation
is a concrete part: current host ownership keeps SystemData until shutdown, and its Release row-cap receipt reports
16,437,760 bytes (15.68 MiB) of source storage. Attribute the remaining retained cost before adding broad trim hooks
or rebuilding the graphics device. AV surface retention is not supported by this capture.

The earlier inactive starting-page receipt remains at `.build/receipts/runtime-2026-10-10-p01-inactive-baseline/`.
It measured Matrix/Gallery/System at 35.74/62.86/51.39 MiB with zero presents. System's 15.65 MiB delta closely matches
the fixed source storage and supports P4.1 lazy per-family commit, without proving proposed savings. Its concurrent
BuildProcess tooling and display-off state exclude it from visible timing acceptance.

Collection provenance: A/B/C come from `runtime-2026-10-10-p01-visible-baseline`; its first D run was interrupted by
user navigation and is excluded. The repeat at `runtime-2026-10-10-p01-visible-return` has the complete expected
0→1→2→3→2→1→0 sequence. Both original rejection outcomes are preserved. The second rejection was an analyzer bug:
minimization keeps the host-visible flag while renderer suspension is set. Corrected analysis accepts that real
state, checks zero presents, and casts parsed UTC values directly. Each collection kept all 458 input hashes equal;
all 457 other inputs still match, with only the collector's analysis correction differing. `compose.ps1`, the copied
analysis body and raw-log hashes reproduce the combined accepted receipt without altering either original.

Future captures use the corrected collector directly:

```powershell
.\measure-runtime.ps1 -OutputRoot .build/receipts/runtime-p01-next-baseline
```

### P2.7 implementation and evidence (2026-10-10)

- [x] Remove dataset-interval redraws while settled; snapshot delivery already coalesces a host invalidation.
- [x] Retain labels, quality, counters and history samples, but snap unchanged/sub-pixel animation targets exactly.
- [x] Keep visible value/rank easing and warning pulses through a 33 ms visual deadline; no Render-side immediate
      frame request, no new timer, thread, GPU resource or allocation. Other continuous widgets/host motion may draw sooner.
- [x] Fixed 320 ms start/target interpolation, same-time tile/raised stability, coalesced target changes, no auxiliary
      restart, bounded 420 ms high-band pulses and hide/device-loss recovery. PID 0 CPU is not a high-load warning.
- [x] Production-DLL tests for all ten viewer types, pixel endpoints, ranks/labels, sub-pixel/heat projection,
      auxiliary deliveries, quality recovery, pulses, snapshot bursts, independent instances and CRT allocation checks;
      production-host scheduled soak added to the existing hidden WARP integration scenario.
- [x] Durable contract and plugin guidance refreshed; Process Viewer user guide explains still/tiny/current readings.
- [x] Final Debug/Release x64 full gates after the expanded changed-family fixtures, Release ARM64 build and skill validation.
- [x] Matched Release after-captures, raw counters, closure/context identity and actual CPU/submission comparison.

The pre-change control is `.build/receipts/runtime-2026-10-10-p27-system-before/`: 65 seconds, 13 valid samples,
60.172 seconds of complete intervals, 14.126 successful submissions/s and 2.986% of one CPU core. It uses the same
x64 Release 1.0.275 artifacts and measurement variant as P0.1 (2560×720, 96 DPI, XENEON-owning AMD adapter, AC,
services `{}`, tray off, 5-second probe). It is accepted with unchanged before/after input hashes. The older P0.1
21.711/s / 2.291% record remains a reference, not a substitute for this fresh matched control.

The fixed motion target/start storage is bounded at 1,320 bytes per instance (12.9 KiB for all ten), accepted to
preserve the same 320 ms interpolation at the lower cadence and prevent duplicate-view/auxiliary advances. The
existing sample and draw-list storage, dataset cadence, device sharing, atlas, layouts, fonts, paging and rendering
pipeline stay in place. P2.8 atlas work and P4.1 source storage are separate cuts.

Matched receipt: `.build/receipts/runtime-2026-10-10-p27-comparison/`. Both after-runs use the same portable scene
bytes, rectangle, DPI, OS, processor, AC power, probe interval, host executable, dependency archive and other
production DLLs as the control. The sole changed production input is `ProcessViewer.cpp` / `ProcessViewer.dll`;
test-source/project/inventory differences are retained separately. Every capture passed its within-run input
identity, page, display-on, validity and completeness checks. No builds/tests from this task overlapped these
captures; other machine activity was not controlled.

| Fresh System run | Complete interval seconds | Core CPU % | Successful submissions/s | Private MiB | Threads / modules |
| --- | --- | --- | --- | --- | --- |
| Pre-P2.7 control | 60.172 | 2.986 | 14.126 | 58.78 | 43 / 78 |
| P2.7 after 1 | 60.016 | 3.515 | 12.163 | 58.49 | 43 / 78 |
| P2.7 after 2 | 60.078 | 3.069 | 12.184 | 58.82 | 45 / 79 |

The observed submission reduction is **13.7–13.9%** against the fresh control. CPU is **2.8–17.7% higher** in these
two observations; this is not evidence of a CPU saving. They are live-system samples with changing observations,
coarse process CPU counters and different driver/runtime thread/module counts in after 2, not a controlled CPU
attribution experiment. Retain both outcomes and the older P0.1 variation; do not substitute its higher 21.711/s
rate to inflate the matched reduction. Successful submissions are not presented FPS or input latency.

The proposed **≤2 submissions/s and ≤1.5% process CPU targets remain unmet**, and the probe cannot accept the
proposed ≤0.3% UI-thread budget. Real value/rank changes and 420 ms warning pulses still require frames. P2.7's
parking/cadence implementation is complete; those budgets require measured follow-up rather than removing visual
behavior. Next CPU step is P0.2 thread/callback attribution before choosing P2.1 collection or P2.8 atlas work.
The memory follow-up remains P3.6 retained-cost attribution/source reclamation, then a separately measured P4.1 cut.

Current Release System-page idle validation is retained at `runtime-2026-10-10-p27-system-states`: a generated
collector copy adds only owned-window hide/restore/minimize phases to C, preserving the production analyzer. Both
25-second inactive phases have five valid samples and 20.031 seconds of complete intervals with **zero submissions**.
Hidden CPU is 0.000%, minimized CPU 0.078%, and private memory does not grow. The 20-second visible lead-in and
10-second recovery establish state transitions only and are excluded from the 65-second matched comparison.
Display-off/occlusion behavior is covered by the production-host scheduler tests; this live run does not manufacture
those external states. A Debug application-generated System screenshot is retained in the comparison's `visual/`.

Final validation on the measured production bytes, with the expanded changed-value tests for every family:

- `test.ps1 -Full -Configuration Release`: all 16 scopes passed, including independent tooling and WARP.
- `test.ps1 -Full -Configuration Debug -SkipTooling`: all 15 native scopes passed; current Release covers tooling.
- `build.ps1 -Configuration Release -Platform ARM64 -MaxCpuCount 4`: all projects and the updated harness compile,
  zero warnings/errors; native ARM64 execution is not claimed.
- `validate-skills.ps1`: all ten skills passed. `git diff --check`: passed.
- The scheduled System-page production-host soak reports 118 frames / 25 coalesced deliveries in 10 seconds in
  Release, 122 / 30 in Debug, with no continuous flag. The 32-frame hidden WARP fixture averages 0.399/0.388 ms;
  those timings are not hardware FPS or CPU-thread attribution.

Logs, source snapshot/patch, native test/artifact hashes and per-profile DxUi provenance are in the comparison's
`validation/`. Release/Debug/ARM64 build logs are `redxe-build-20261010_182146_951-pid82928-ad50a804.log`,
`redxe-build-20261010_182521_958-pid20784-a8ebec5d.log` and
`redxe-build-20261010_182457_478-pid80328-d65314a3.log`. Tests were expanded after the two matched captures;
the validation record checks that the production source/DLL bytes remain exactly the measured after bytes.
P2.7 implementation, tests and this comparison are complete. The entire plan remains ACTIVE for its other work
and unaccepted budgets.

### P0.1 validation closeout record

The generated validation record, final source snapshot/patch and copied logs are under
`.build/receipts/runtime-2026-10-10-p01-release-baseline/validation/`. The original implementation-validation
record is retained separately. Although native code/artifacts did not change during the analyzer correction,
the whole-scope identity includes that script, so fresh final gates cover the corrected collector bytes.

- `test.ps1 -Full -Configuration Release`: all 16 scopes, including WARP, application probe JSONL,
  command-line negatives, crash capture, AV aggregation, scheduler coverage and independent tooling.
- `test.ps1 -Full -Configuration Debug -SkipTooling`: all 15 native scopes passed.
- `test.ps1 -Configuration Release -Suites BuildProcess -SkipBuild`: independent tooling passed (30 scoped-testing
  cases, four Python skill-validator tests, restore/provenance/process helpers and repository skill validation).
  The full Release gate also covers it on the final bytes; no pending CI is credited.
- `build.ps1 -Configuration Release -Platform ARM64`: compilation passed; no native ARM64 execution claimed.
- `validate-skills.ps1`, PowerShell AST parsing and `git diff --check`: passed.

Implementation build logs: `redxe-build-20261010_142955_377-pid39076-9467b466.log` (Release),
`redxe-build-20261010_143504_309-pid30284-327aa8b7.log` (Debug), and
`redxe-build-20261010_143808_251-pid82408-19a81928.log` (ARM64 Release), all under `.build/logs/`.
The current pin and linked archive/binary hashes are preserved in the per-profile `DxUi.provenance.json` copies.
Final full-gate output is preserved as `Full-Release-closeout.log` and `Full-Debug-closeout.log` in the baseline's
validation folder; that record distinguishes the earlier implementation runs from final collector-byte closeout.
The corrected collector replay also passes the real visible+suspended minimized state and rejects missing
suspension, hidden visibility, inactive presents, display-off settled samples, missing records and wrong return pages.
P0.1 implementation and baseline closeout are complete. PresentMode/input latency, exact shipped-service qualification
and the AV HOLD gates remain open. The entire runtime optimization plan stays ACTIVE.

## Historical investigation (2026-09-07)

The following snapshots predate today's implementation. Their old paths, pins, line numbers, costs and defect
hypotheses are retained for provenance; they are not current findings. Current-code status is the checkpoint above.

DxUi checkout inspected 2026-09-07 (read-only; no reset, clean, or overwrite):

| Tree | HEAD | Notes |
| --- | --- | --- |
| `Z:/src/DxUI` / `Z:/src/DxUi` (same volume) | `7b571b734ee078cc5c68ad63ed2334c7453441e5` on `page-indicator` | Matched the lock on 2026-09-07; superseded |
| `Z:/src/DxUi-worktrees/av-high-contrast` | `24081e2a` on `codex/av-text-bridge` | Isolated AV text/UIA work; leave it |
| `Z:/src/DxUi-worktrees/text-services-baseline` | detached `ad888331` | Baseline; leave it |
| `Z:/src/DxUi-worktrees/av-input-baseline` | detached `1947a5b` plus local edits | Leave it |
| `Z:/src/DxUi-worktrees/input-pr-repair` | `e42126c8` on `codex/input-receipt-repair` | Leave it |

`restore-dxui.ps1` remains the only way RedXe consumes DxUi. Library changes are filed upstream, never made from here.

### Historical live evidence (2026-09-07, Release x64, hardware)

Machine: XENEON EDGE 2560×720 attached to the **AMD Radeon iGPU**; primary 4K display on the **NVIDIA RTX 5080**;
Ryzen 9 9950X3D, Windows 11 26200. Process `.build\x64\Release\RedXe.exe`, launched 13:31:28, visited Matrix Focus
(+0.1 s), Plugin Gallery (+2.8 s), System (+9.0 s), then settled on System for three minutes:

| Metric | Value |
| --- | --- |
| Private bytes / working set | 153 MiB / 155 MiB (peaks 185 / 177) |
| Threads | 147 (141 created in the first second) |
| Handles | 1,831 |
| Mapped modules | 121, 359 MiB of images: `nvgpucomp64.dll` 106 MiB, `nvwgf2umx.dll` 86 MiB, `amdxx64.dll` 48 MiB, `nvppex.dll` 7 MiB, `windows.storage.dll` 8.7 MiB, `shell32.dll` 7.6 MiB, `d2d1.dll` 6.2 MiB, `UIAutomationCore.dll` 4.1 MiB, `dcomp.dll` 2.1 MiB |
| CPU on the settled System page | 6% of one core: acquisition worker 2.9%, UI thread 2.1%, driver threads ~1% |
| GPU memory committed by this pid | 63.6 MiB on the RTX 5080 LUID, 18.2 MiB on the AMD LUID |
| GPU engines used by this pid | NVIDIA 3D, NVIDIA copy, **and** AMD 3D at the same time |

Root cause of the last three rows: `Renderer::CreateDevice` (`RedXe/Renderer.cpp:344`) calls
`D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, …)`, which selects adapter 0 (the NVIDIA). The window lives on
a monitor owned by the AMD. DXGI/DWM bridge this with an internal device on the AMD and a copy per present.

Per-adapter device cost measured the same day (one fresh process per row, `D3D11CreateDevice` only, nothing drawn):

| Device | Threads added | Private bytes added | Driver images | Creation time |
| --- | --- | --- | --- | --- |
| NVIDIA RTX 5080 (adapter 0, today's default) | +37 | +53 MiB | 200 MiB (`nvwgf2umx`, `nvgpucomp64`, `nvppex`, …) | 127–147 ms |
| AMD Radeon iGPU (adapter 1, owns the XENEON) | +32 | +22–26 MiB | 48 MiB (`amdxx64`) | 43–69 ms |
| WARP | +0 | +1.5 MiB | 6 MiB | 9–23 ms |
| Either with `D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS` | no change | no change | | | 

The pre-fix September process paid both hardware rows: ~69 driver threads and ~75–80 MiB of private bytes before a single widget
exists, plus the cross-adapter staging surfaces (17.4 MiB "shared" on the NVIDIA LUID and 16.5 MiB on the AMD LUID).
That is roughly half of the threads and half of the private bytes of the measured process. The threading flag is
worthless on both drivers and is not part of this plan.

Runtime log (`%LocalAppData%\RedXe\Logs\RedXe-2026-09-07.jsonl`): the top CPU thread (2.9% of a core) was created at
exactly +9.0 s, when the ten System viewers subscribed; it is the host acquisition worker. The 2026-09-06 log shows
153 `forecast-failed` warnings at two per second and 1,647 `widget-created` events in four runs, i.e. one cold Weather
fetch per Gallery visit during swipe testing.

### Historical cost hypotheses (2026-09-07; superseded where the checkpoint says so)

#### Host

| Cost | Evidence | Effect |
| --- | --- | --- |
| Device on adapter 0, not on the output's adapter; no re-check when the window moves | `Renderer.cpp:344`; no `WM_DISPLAYCHANGE` handler; `WM_MOVE` only refreshes accessibility (`Application.cpp:3806`) | cross-adapter present, two driver stacks, composed flip, extra frame of latency |
| `Present(1, 0)` blocks the UI thread; device-level latency 1; swap chain flags 0 | `Renderer.cpp:381`, `:401-410`, `:932`; `Application.cpp:3622` waits on the message queue only | input queued during the block waits a full frame |
| Top-level window has a DWM redirection surface it never paints | `Application.cpp:1043` (`WS_EX_APPWINDOW` only); only the edge and overlay child HWNDs call `BeginPaint` (`:1840`, `:3437`) | one 7 MiB DWM surface and copy per frame in composed mode |
| Full clear + full present, `FLIP_DISCARD` | `Renderer.cpp:817`, `:932` | 7 MiB cleared and presented per frame; partial presentation impossible with discard |
| One continuous tile redraws all tiles; `changedWidgets` discarded | `Application.cpp:3641-3645`, `:778`; `Renderer.cpp:876-884` | every tile renders at vsync on Gallery |
| `EnumDisplayMonitors` per frame and per wait iteration, before the cache check | `Application.cpp:1533-1542`, `:1646`, via `:3712` from `:760`/`:797`; twice per `WM_MOUSEMOVE` | 60+ monitor enumerations per second |
| Locked 64-slot string scan per widget per frame, twice | `DashboardHost.cpp:794`, `PluginHost.cpp:1415-1433`, `Renderer.cpp:757`, `:826` | ~4,000 compares per frame |
| Unthrottled `OutputDebugStringW` in the render loop | `Renderer.cpp:859` | global mutex per failing widget per frame |
| Staging: whole-document validation and per-widget JSON parse **twice**, `AppSettings` deep copy, 256 KiB `ProviderBuildKey` stack array, `OnDeviceCreated` + `SetVisible(TRUE)` + network activation for every neighbor widget, teardown and rebuild on direction flip, staged neighbor pins vsync with no finger down | `Application.cpp:1219-1280`, `:2215-2222`, `:3649`; `PluginManager.cpp:777`, `:839-842`, `:935-937`; `PluginManager.h:91-96`; `Renderer.cpp:90-106` | dropped frames at gesture start; one cold Weather fetch and one Launcher icon extraction per cancelled swipe |
| Synchronous `MOVEFILE_WRITE_THROUGH` settings write on the UI thread, up to 32× per promote, ~4 document copies per persist | `Settings.cpp:1259` (also `:875`, `:941`, `:962`), `:2229`, `:1764`, `:1780-1818`; `DashboardHost.cpp:649-664`; `Application.cpp:4235` | multi-ms stalls at the end of a swipe |
| UI thread can wait `INFINITE` on plugin callbacks | `PluginHost.cpp:1643`, `:1663`, `:1894`, `:2010`, `:2204` | hang on a slow `RunNetworkWork` |
| Two directory enumerations per log line | `PluginHost.cpp:777`, `:789`, `:900`, `:917` | log thread churn |
| Module discovery maps every plugin in `declare` and on every page at first `Initialize` | `PluginManager.cpp:786-832` | Weather + curl + zlib, AV, ProcessViewer, SystemData mapped on Matrix Focus |
| Data sources are created at `GetDataProvider` and released only at process exit | `PluginHost.cpp:1240-1322`, `:1843-1869` (only caller: `Shutdown`) | `SystemDataSource` survives every page change |
| `sourceDocument` retained (≤ 1 MiB); `WidgetInstanceSettings` 5,816 B each regardless of content; 19×256 `DataSetRuntime` static | `SettingsV4.cpp:1705`; `Settings.h:194-205`; `PluginHost.h:160`, `:227` | 0.3–4 MiB of settings shadow, duplicated during staging |
| Raise animation: 2–3 GDI regions + `SetWindowPos` per frame; edge band HWND created per hover | `Application.cpp:3136-3183`, `WidgetRaise.h:249-294`, `Application.cpp:1565-1582` | GDI churn |
| Scheduled deadlines on `GetTickCount64` | `Application.cpp:3701` | up to 15.6 ms jitter; acceptable for 1 Hz widgets; document, do not add a 1 ms timer |

#### DxUi library (`7b571b73`) and the AV consumer

| Cost | Evidence | Effect |
| --- | --- | --- |
| A hidden or zero-extent `EmbeddedHost` keeps its texture, SRV, and D2D bitmap | `src/Rendering/Embedded.cpp:191-212`, `:249-257`; `Tests/Embedded/EmbeddedTests.cpp:218-222` | surfaces survive hide until `Detach` |
| `Prepare` re-rasters the whole surface; `AdvanceAnimation` calls `MarkDirty()` even when nothing ticked | `Embedded.cpp:328-340`, `:232` | one caret blink = one full raster |
| `TextField::Tick` true while focused; `ComboBox::Tick` while open; indeterminate `ProgressBar::Tick` forever | `src/Controls/DxUi.TextInput.cpp:1639-1659`, `DxUi.ComboBox.cpp:1749-1756`, `DxUi.Controls.cpp:3352-3376` | host pinned awake at tick rate |
| Popup drop shadows: 2 command lists + 2 effects per dirty frame | `DxUi.Controls.cpp:138-188`, `:689-702` | popup churn |
| Unbounded brush and text-format caches | `include/DxUi/DxUi.h:3980`, `:3958` | slow growth |
| ~54 heap allocations per dirty `Prepare` (83-control fixture); only `composeAllocations == 0` is asserted | `.build/reports/Embedded-x64-Release.json`; `DxUi.Grid.cpp:1904`, `:1917` | allocation on the prepare path |
| 128 MiB replacement ceiling is prose | `Embedded.cpp:264-270`, `:300` | not enforced |
| AV: both views attached and both control trees built at device creation; hidden views still receive `SetState`/`SetProfiles`/`SetAppearance`; overlay surface never released after dismiss; `ProfileControls` never destroyed | `Plugins/AVControl/AVControl.cpp:208-210`, `:243-256`, `:167-185`; `AVControlView.cpp:95-96`, `:389-421`, `:631-638` | ~3.5 MiB overlay surface per collapsed widget after its first raise |
| AV: module-global `coordinator` (~428 KiB heap + ~215 KiB broker mapping + broker process + threadpool wait) released only in `RedXePluginShutdown` | `AVControl.cpp:33`, `:79`, `:646-648` | survives leaving the Gallery |
| AV: ~267 KiB copied per observation cycle (133 KiB `Inventory` twice) | `AVControlBroker.cpp:123`, `Coordinator.cpp:357` | copy churn |

#### Bundled plugins

| Plugin | Retained | Wake | Hot-path cost |
| --- | --- | --- | --- |
| **SystemData** | ~15.6 MiB, **all memset at construction** because every member carries `{}` (`SystemData.cpp:3695-3821`, `new` at `:3838`): 4 MiB NT walk buffer (`NativeQuery.h:220`), 3.0 MiB `_threadValues` (`:3759`), 2.4 MiB `_processValues` (`:3765`), 1 MiB PDH array (`GpuQuery.h:141`), 0.95 MiB walk sample, 0.6 MiB `cpu.logical` values, 0.4 MiB thread history, 0.3 MiB process history | none owned (correct); host worker at 1/2/5/10/60 s | `OpenProcess` per row per collection (`:3597-3601`) feeding `GetProcessInformation`, `GetProcessAffinityMask`, `IsWow64Process2`, `IsProcessCritical` (`:3394-3449`) = the 73.9 ms `process.list`; ~23 KiB stack zero+copy per batch (`:1354`, `:1504-1534`); 688 KiB memset on partial failure (`:1550-1551`). **No shipped viewer reads those four columns** (`ProcessViewer.cpp` reads columns 0–7 only). `thread.list` opens no handles. |
| **ProcessViewer** ×10 | 1 MiB CPU atlas as a namespace-scope object never freed (`ViewerGpu.cpp:31`, `:385`) + 1 MiB GPU atlas + 64 KiB instance buffer; ~18 KiB per widget | `_restDelay` = min catalog interval, returned whether or not anything changed (`:1011-1015`, `:1283`) ⇒ ~7 scheduled wakes/s; `BeginEase()` on **every** snapshot (`:1085`) then `RequestFrame` per frame for 320 ms (`:1260-1263`) ⇒ ~20 presents/s on the settled System page | full 1 MiB `UpdateSubresource` inside `Render` (`ViewerGpu.cpp:589`, `:604`); `dwrite.dll` load + isolated factory + system font collection per glyph miss (`:87-108`, `:479-484`); glyph raster on the acquisition worker under the lock `Render` holds (`:1092`, `:1248-1256`); 64 KiB draw-list zero-init per widget per frame (`:1247`) + 10.9 KiB sample copy under lock (`:1242`) |
| **MatrixRain** | 16 KiB immutable atlas per provider, 128 B CB, no CPU instance buffer | continuous | one 128 B map + 2 draws; 6,519 instances at Focus, 1,271 on Gallery. Clean. |
| **RotatingTriangle** | 56 B | continuous | one map + one draw. Clean; the flag is the cost. |
| **GdiOrbit** | ~1 MiB DIB | `SetTimer` 33 ms on a plugin child HWND, the only timer in the tree (`GdiOrbit.cpp:30`, `:191`) | CPU repaint + `BitBlt` at 30 Hz; the child HWND forbids independent flip |
| **StudioClock** | 160 B CB, shared immutable dots | 1 s / 1 min boundary | one map on change + one draw. Clean. |
| **DeskClock** | atlas 1.31 MiB at scale 1, 5.25 MiB at scale 2, new texture built before the old is released (6.56 MiB transient, `DeskClock.cpp:1111-1130`); per provider configuration (`:1838`) | 1 s boundary; **1 ms scheduled delay during the flap** (`:1520-1543`) | full 72-glyph DirectWrite re-raster on scale change; retained soak grew +9.2 MiB private in 300 s (`.build\fix-desk-clock-soak.log`) |
| **Launcher** (uncommitted edit on disk) | 8 MiB `Texture2DArray` per widget (`Launcher.cpp:77`, `:879-889`; was 2 MiB) + up to 8 MiB CPU BGRA (`:326`), allocated in `OnDeviceCreated` even for `"shortcuts": []` | event-driven; `RequestFrame` from `Render` during settle/pan/launch (`:1415`) | every `SetVisible(TRUE)` re-extracts every icon synchronously on the UI thread with no cache (`:1205` → `:1832` → `:1876-1882`), five `CoCreateInstance(WICImagingFactory2)` sites; `UploadIcons` zero-fills 256 KiB on the stack 32× and uploads all 32 slices (`:932-942`) |
| **Weather** | 1 MiB static CPU atlas never freed (`WeatherGpu.cpp:33`) + 1 MiB GPU; 8×256 KiB pre-sized cache slabs freed only in `RedXePluginShutdown` (`WeatherHttp.cpp:43-58`, `:224-228`, `:302`); two 256 KiB bodies per fetch (`Weather.cpp:579`, `:618`) | scheduled 5–60 min; `S_FALSE` hidden (correct) | `curl_easy_init`/`cleanup` per request, no connection reuse (`WeatherHttp.cpp:388`, `:430`); full-atlas upload inside `Render` (`WeatherGpu.cpp:766`); glyph re-raster on every `OnTargetSizeChanged` (`Weather.cpp:422`) |
| **AVControl** | see DxUi table; two 23 KiB views per widget | event-driven (correct) | copies above; UIA pin on first connect (documented, budget for it) |
| **All modules** | mapped at first `Initialize` for the whole document, never unmapped | | no-unload stays; map-on-first-use is the change |

September page shapes and retained-cost observations: Matrix Focus (one continuous 2560×720 Matrix);
Plugin Gallery (Triangle + Orbit + Matrix + StudioClock + DeskClock + Weather + Launcher + AV); System (ten scheduled
viewers). Leaving System keeps the 15.6 MiB source. Leaving Gallery keeps the AV coordinator and broker, the Weather
HTTP slabs, and both static atlas staging buffers. Every page keeps every module image.

## Acceptance budgets (measure first, then commit)

These are proposed phase targets, not already accepted normative requirements. Measure whole-process Release x64,
hardware, XENEON at 2560×720, 96 DPI, AC power. Record exact settings/services/topology and validity. Debug/WARP rows
are informational and cannot pass a hardware budget. The P0.1 safe variant and unavailable P0.2 metrics are identified
explicitly; they do not qualify the exact shipped configuration.

| Scenario | Private bytes | Threads | Wake / present | Other |
| --- | --- | --- | --- | --- |
| A. First page after startup, 60 s settled | record; after Phases 1 and 4 target **≤ 64 MiB** with Matrix Focus continuous (Matrix-only on the NVIDIA path measured 75 MiB on 2026-08-31) | record; target ≤ 80 after Phase 1, then attribute the remainder | display refresh if Matrix stays first; PresentMode `Hardware: Independent Flip` | exactly one adapter LUID in the GPU counters; no NVIDIA image mapped |
| B. Plugin Gallery, 60 s settled, no raise, no camera | record; target ≤ 110 MiB | ≤ 88 | one display-paced host presentation stream; no shipped Orbit timer | AV `surfaceBytes` = tile only |
| C. System page, 60 s settled | record; SystemData commits only subscribed tables (≤ 10 MiB for the shipped viewers) | ≤ 88 | ≤ 2 presents/s average at 1 Hz meters; process CPU ≤ 1.5% of a core; acquisition ≤ 1% | `process.list` ≤ 10 ms |
| D. All pages visited, back to first page, settled 60 s | **A + 4 MiB** | A | A | no `SystemDataSource`, no Launcher array, no AV overlay surface, no AV coordinator, no Weather slabs |
| E. Hidden / minimized / occluded / display-off, any page | no growth | no growth | 0 frames, 0 Orbit timers, 0 acquisition delivery (already tested for System) | |
| F. Mid-swipe peak Gallery↔System | settled + 8 MiB | | stage ≤ 2 ms UI-thread CPU per swipe; no dropped frame from first finger movement to settle | no second source, no second Launcher array, no cold Weather fetch, no icon extraction for a cancelled swipe |
| G. AV tile only, clean composition | AV CPU model ≤ 1+2 MiB as proposed | | 0 D2D in `Render` | `surfaceBytes` = tile extent × 4; `surfaceAllocations` unchanged across 1,000 composites (already tested) |
| H. Hardware presented frame, Gallery with current continuous widgets | p95 ≤ 16.7 ms at 60 Hz; input-to-present ≤ 2 frames (PresentMon) | | | WARP 8 ms System-page mean is **not** this gate |

If Phase 0 shows a scenario already under target, keep the measured number and do not spend complexity chasing a
smaller one.

## Sequenced work

Measure before each cut. P0.1 and P2.7 implementation and measurements are recorded above. P1 implementation already
exists; finish its open attribution gates under P0.2. Later proposed ABI/column/storage changes require their own
contract and validation updates.

### Phase 0 — receipts from the real process (no behavior change)

| ID | Item | Pass condition |
| --- | --- | --- |
| P0.1 | **Complete 2026-10-10: production probe, tests and Release A/B/C/D plus inactive receipts.** See the evidence checklist and versioned Core record. Unconditional device logging was already present and is preserved. | Selected P2.7; its matched comparison is recorded above. D retains 85.80 MiB private, supporting P3.6 attribution/source reclamation; P4.1 remains a storage candidate. F/raised-G/presented-H and external attribution stay with P0.2/P0.4. |
| P0.2 | **External captures per selected optimization:** PresentMon for PresentMode, pacing and input-to-present; WPR/WPA for CPU/thread/image attribution and whole-process GPU LUIDs. Use a quiet desktop, fresh matching sources/artifacts and the scenario's real settings/services. Do not reuse September thread totals. | Current thread/module attribution and hardware frame/input table in `.build/receipts/`. |
| P0.3 | **Soaks become tests.** Wire `--matrix-soak`, `--studio-clock-soak`, `--desk-clock-soak` (60 s variants) into `test.ps1` with asserted `private_bytes_delta` and `working_set_delta`. Investigate the Desk Clock +9.2 MiB before setting the bar. | Soaks fail on growth above the recorded bar. |
| P0.4 | Record Matrix instance count and present rate on current Focus/Gallery placements; compare current pinned DxUi library receipt identity with AV tile+overlay bytes. | Grid math and pin identity match the measured scene; library composition throughput is not hardware application FPS. |

### Phase 1 — render on the GPU that owns the panel; never block on Present

| ID | Item | Pass condition |
| --- | --- | --- |
| P1.1 | **Adapter-of-output device.** `Renderer::CreateDevice` takes the window's `HMONITOR` (`MonitorFromWindow(_window, MONITOR_DEFAULTTONULL)`), enumerates `IDXGIFactory1::EnumAdapters1` → `IDXGIAdapter1::EnumOutputs` → `IDXGIOutput::GetDesc().Monitor`, skips `DXGI_ADAPTER_FLAG_SOFTWARE`, and creates with `D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, …)`. Stores the adapter LUID and monitor. Falls back to the default-adapter path only when no output matches (hidden test HWND at −32000, headless), then WARP as before. **Landed 2026-09-07:** `RedXe/AdapterSelection.h` (pure policy), `Renderer::FindAdapterForMonitor`, `Renderer::RecordDeviceIdentity` (the `device-created` record), `Renderer::DeviceInfo`; `HostPluginTests` `TestAdapterSelectionPolicy` and `TestRendererDeviceIdentity`. **Receipt (Release on the live XENEON, `.build/receipts/2026-09-07-adapter-of-output.md`):** `device-created` = AMD Radeon Graphics, LUID 0001C3CC, adapter owns window monitor: yes; only `amdxx64.dll` mapped; one LUID in `GPU Process Memory` (22.3 MiB) and `GPU Engine` (AMD 3D 13%); Matrix Focus settles at 40.1 MiB private bytes, 61.7 MiB working set, 42 threads, 1,127 handles, 2.5% of one core (was 147 threads / 153 MiB after all pages, 75 MiB Matrix-only on the old path). | `device-created` reports `adapter owns window monitor: yes` (met); GPU counters show one LUID (met); no `nvwgf2umx.dll`/`nvgpucomp64.dll` mapped (met); threads −37 or better (met: −105 vs. the all-pages baseline); PresentMon `Hardware: Independent Flip` and UI-thread CPU < 1% on Matrix Focus **still to capture**: PresentMon 2.5.1 is installed, but the CLI needs elevation and the 2026-09-07 elevated attempts hung behind pre-existing `PresentMon` / `Gv_PresentMon` ETW sessions; capture from an elevated shell with `presentmon --session_name RedXeProbe --process_name RedXe.exe --output_file <csv> --timed 12 --v2_metrics` once those sessions are stopped, or read PresentMode from the PresentMon GUI. 2.5% process CPU was measured at 60 fps continuous. |
| P1.2 | **Device follows the window.** `Renderer::EnsureDeviceForWindowMonitor` compares the LUID of the adapter owning the window's current monitor with the device's; `Application::CheckDeviceAdapter` runs it on `WM_MOVE`, `WM_EXITSIZEMOVE`, `WM_DISPLAYCHANGE`, and after `WM_DPICHANGED`; a mismatch runs the existing `CreateDeviceResources()` rebuild (the same path `RecoverDevice` uses, which already notifies widgets through `OnDeviceLost`/`OnDeviceCreated`). Release popup, Debug titled window, and XENEON re-placement all use the same rule. **Landed 2026-09-07 with the live receipt:** moving the Debug titled window onto the RTX 5080 display logged `device-created` on the NVIDIA (LUID 0001AE51, owns window monitor: yes), moving it back logged the AMD again; the process stayed alive across both rebuilds and closed with exit code 0. | Dragging the Debug window from the 4K display to the XENEON and back rebuilds the device on the target adapter each time (log shows two `device-created` records, LUIDs differ); no rebuild on moves within one adapter. |
| P1.3 | **Waitable swap chain.** `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` (also passed to `ResizeBuffers`), `IDXGISwapChain2::SetMaximumFrameLatency(1)`, device-level latency call removed. `Application::WaitForFrameLatency` waits on the waitable handle together with the message queue before `PrepareWidgets`; a message arriving first is dispatched before the frame is built; `Present` no longer blocks. The idle path (`WaitUntilMessage`) and the scheduler table are unchanged. **Landed 2026-09-07**; `TestRendererDeviceIdentity` proves the object exists, is signaled before the first frame and after a presented frame, survives resize, and is released at shutdown. | ETW shows no blocking inside `Present`; PresentMon input-to-present ≤ 2 frames; `HostPluginTests` scheduler table green. |
| P1.4 | **Landed later through HostChromeWithoutGdi.** The top-level window uses `WS_EX_NOREDIRECTIONBITMAP` and native containers are layered children; the September plain-child rejection is historical. | Current code/tests preserve layered native rendering; per-page PresentMode remains P0.2. |
| P1.5 | **Template part landed.** GdiOrbit is opt-in and absent from shipped pages. Triangle and Matrix on Gallery share one host present stream; continuous widget count alone does not establish PresentMode. | Record per-page hardware PresentMode; any removal or cadence change is a product decision. |
| P1.6 | Product option, default off: `continuousFrameRate: "display" \| 30` → `Present(2, 0)` on continuous pages. Owner: the first-page decision (Matrix Focus stays first unless product says otherwise; this plan does not make that call). | Setting parsed, documented, measured on Matrix Focus. |

### Phase 2 — make settled pages idle

| ID | Item | Pass condition |
| --- | --- | --- |
| P2.1 | **Candidate: process-query cost.** `CollectProcessesFromWalk` still opens each process. Before changing it, identify which published columns require handles and how consumers preserve quality/compatibility. A separate bounded optional detail dataset is a proposal, not an existing contract. Current per-process-query cost was accepted by the later Core contract. | New matched Release collection/CPU receipts justify the change; updated dataset contract/tests preserve consumers and allocation-free collection. |
| P2.2 | Cache non-dock `ReachableClientRect` and check geometry generations before display enumeration in `RefreshPageEdgeAffordances`; invalidate on display/work-area/DPI/window changes. Dock already bypasses enumeration. | Non-dock idle trace has no repeated enumeration; work-area/monitor changes remain correct. |
| P2.3 | `RequiresPlaceholderAt`: per-slot atomic status written by `ReportWidgetStatus`; no locked string scan per frame (`DashboardHost.cpp:794`, `PluginHost.cpp:1415-1433`). | No `_widgetStatusLock` acquisition in `Render`/`PrepareWidgets`. |
| P2.4 | **Complete 2026-10-10.** Debug output shares the render-failure transition guard. A recovered drawn frame permits a fresh failure report; partial raised/tile success does not. Successful primary adoption resets slot identity. | 400 failing primary frames emit three diagnostics and preserve healthy siblings; the incoming instance reports once. Debug/Release full gates pass; ARM64 Release compiles. |
| P2.5 | **Complete 2026-10-10.** Writer-owned UTC date/policy cache: sweep on open, date change or changed retention value. Policy updates wake the idle writer and synchronize with flush; ordinary lines construct no path. | Same-day, unchanged/shortened policy, forward/backward UTC correction and actual idle-worker flush tests pass in Debug/Release; ARM64 Release compiles. Removes two enumerations per ordinary line; adds 20 bytes of cached date/policy, no thread or wake. |
| P2.6 | **Candidate: settings persistence latency.** Coalesce and move durable writes off the UI thread only if measured promote stalls justify a bounded owner/worker design. Temp-file flushing and loaded-document/stamp/merge protections already exist; preserve them. Reusing the log worker is undecided. | No UI-thread disk-write stall at promote; concurrent edits, shutdown, failure recovery and stamps retain current guarantees. |
| P2.7 | **Implemented and measured 2026-10-10: ProcessViewer wake discipline.** Settled viewers park; unchanged/sub-pixel values snap; real 320 ms easing and 420 ms pulses use a 33 ms deadline without immediate requests. Fixed-pose interpolation preserves tile/raised timestamps, ranks and endpoints. Tests cover all ten types, auxiliary deliveries, bursts, recovery, allocation and a scheduled production-host soak. | Fresh matched 14.126/s becomes 12.163/12.184/s (13.7–13.9% lower). CPU 2.986% becomes 3.515/3.069%; no CPU saving established. Proposed ≤2/s and ≤1.5% process CPU remain unmet; UI-thread ≤0.3% is unmeasured. Keep budget acceptance open and use P0.2 attribution to choose further work. |
| P2.8 | **Atlas and DirectWrite discipline (ProcessViewer, Weather).** Upload only dirty 48×48 cells with a destination box, from the snapshot/`Prepare` path, never from `Render`; one cached DirectWrite factory + font face per module for the module's lifetime (released by `RedXePluginTrim`, P3.6); rasterize outside the draw lock; Weather does not re-raster on raise/dismiss when DPI and cell size are unchanged. | No `UpdateSubresource` in `Render` (debug layer capture); no `dwrite.dll` load after the first glyph fill. |
| P2.9 | Measure callback drain stalls before redesigning waits. Network and subscription drains include `INFINITE` waits; a timeout cannot free state while a callback can still use it. Reuse current bounded/tombstoned control/device-lane patterns only where ownership permits. | Slow-fixture recovery bounds UI delay while preserving callback quiescence, cancellation and teardown lifetimes. |

### Phase 3 — fluid swipe, nothing left behind

The rule: a swipe MAY allocate whatever a smooth slide needs; the settled page MUST end with exactly the resources of a
cold visit to that page, and nothing from the page that was left.

| ID | Item | Pass condition |
| --- | --- | --- |
| P3.1 | Validate only the target page in `StageTransitionPage`; the document was validated at load. Drop the second per-widget parse at `PluginManager.cpp:839-842` by keeping the validated value doc from `ValidateAppSettings`. | Stage ≤ 2 ms UI-thread CPU for the shipped document (ETW). |
| P3.2 | `ProviderBuildKey`: 64-bit hash + index instead of an 8 KiB copy (`PluginManager.h:91-96`); the 256 KiB stack frame at `PluginManager.cpp:935-937` disappears. | `PluginManager::Initialize` stack frame < 32 KiB. |
| P3.3 | **Lazy neighbor.** No `OnDeviceCreated`, `SetVisible(TRUE)`, or `SetNetworkWidgetActive(true)` for the neighbor until its first visible pixel; no teardown on direction flip (`Application.cpp:2215-2222`), stage the other side lazily; a staged-but-still neighbor MUST NOT force `Render` (`Application.cpp:3649`). Stage on gesture recognition (two-finger horizontal lock), not after the first pan frame. | Scenario F; no `gpu-ready`, `forecast-*`, or icon extraction for a cancelled swipe. |
| P3.4 | **Time-sliced construction with a frame budget.** If P3.1–P3.3 still exceed 8 ms for a shipped page, construct neighbor widgets across frames under a 4 ms per-frame budget, drawing not-yet-ready tiles as a flat page background (not the failure placeholder) until they exist. | No frame > 16.7 ms during any shipped swipe (PresentMon). |
| P3.5 | **Product option: snapshot slide.** Keep the last presented frame of a visited page as one 2560×720 or 1280×360 texture for its two neighbors (7 MiB or 1.8 MiB each on UMA), slide textures, construct after promote, release the retired page's snapshot at the settle sweep. Only if P3.4 cannot hold the frame budget. | Decision recorded with the memory cost. |
| P3.6 | **Candidate: settle sweep / trim contract.** Release sources with no subscriptions after all callbacks are quiescent; trim module-owned caches/coordinator only when no live/staged widget needs them. `TrimIdle` / `RedXePluginTrim` do not exist yet. AV provider destruction already resets its coordinator when the last provider leaves, so confirm retention before adding another mechanism. Keep images mapped. | D approaches cold A without unsafe destruction; hidden WARP tests prove callback/lifetime safety and no per-frame sweep. |

### Phase 4 — retained memory and arenas

| ID | Item | Pass condition |
| --- | --- | --- |
| P4.1 | **Arena-backed `SystemDataSource`.** The source becomes a small object plus one `VirtualAlloc(MEM_RESERVE)` region of 16 MiB. Tables are page-aligned spans grouped by dataset family: *cheap* (summary, cpu.summary, memory, status, security; always committed, < 64 KiB), *process* (NT walk buffer, walk sample, `_processValues`, rows, names, history, `_currentCpu`; ~8 MiB), *thread* (`_threadValues`, rows, history; ~3.6 MiB), *cpu.logical* (~0.7 MiB), *gpu* (PDH array, engines, processes, values; ~1.9 MiB), *net/storage* (~1.0 MiB), *power/thermal/fan/npu* (~0.5 MiB). `CollectSnapshots` commits a family with `MEM_COMMIT` (zero pages, no memset) the first time one of its datasets is requested; the `{}` initializers on those members go away. Release through P3.6 (`VirtualFree`). Caps, `RedXeDataValue` layout, and the 16 MiB ceiling are unchanged; `thread.list` still works when a future widget subscribes. | Shipped System page: source commit ≤ 10 MiB (probe); construction ≤ 1 ms; `SystemDataTests` storage receipt reports committed bytes per family; scenario D has no source. |
| P4.2 | Host-side small allocations: `PluginHost::_providers[].dataSets[256]` sized to `descriptorCount` at provider creation (`PluginHost.h:160`, `:227`); `WidgetInstanceSettings.privateConfiguration` as a content-sized heap string instead of a 4,104-byte inline array; drop `sourceDocument` retention (re-read the file when patching); map plugin modules on first page use instead of at discovery (`PluginManager.cpp:786-832`). | Matrix Focus maps only `MatrixRain.dll`; settings shadow ≤ 64 KiB for the shipped document. |
| P4.3 | **Candidate: Launcher capacity/caching.** Fixed 32-slice GPU allocation and bounded CPU icon copies are the current accepted contract. Measure empty/pinned/authored launchers before proposing count-sized storage, extraction caching or control-lane work. | Empty/8/32-icon, device-loss and taskbar-pin receipts justify new bounds; quality, fallback and persistence remain correct. |
| P4.4 | **Surface lifetime landed and retained at the current pin.** Hidden tile and unraised overlay surfaces are released without detaching control trees. P0.1 measures their live bytes. Lazy control-tree attachment, hidden-view update skipping, profile-control destruction and inventory diffing remain separate proposals. | Tile-only extent × 4 and hidden 0 are automated; real raised state / unchanged-observation copy cost still need targeted captures. |
| P4.5 | **Weather.** Cache bodies allocated to `response.bytes` on store in one slab; one curl easy handle per widget reused across requests; one in-flight fetch per location process-wide with a shared failure memo; keep the ≥ 5 min clamp. | Cold Weather delta excludes 2 MiB of slabs; one TLS handshake per host per cycle; no `forecast-failed` bursts during swipe testing. |
| P4.6 | **Candidate: clocks.** Recheck today's shared multi-tier atlas and measured animation/soak behavior before changing lifetime or replacing the 1 ms animation deadline. GdiOrbit is already absent from shipped Gallery. | Matched scale/resize/soak and frame-cadence measurements justify any change; preserve typography and rollover quality. |

### Phase 5 — DxUi upstream and spec closeout

| ID | Item | Pass condition |
| --- | --- | --- |
| P5.1 | **Library surface/cache work and I19 adoption already landed.** The current lock is authoritative; the 2026-09-13 accepted bounded offscreen resource envelope is recorded in `Core_DxUiIntegration.md` and `Core_PerformanceAndResources.md`. Do not repeat or reopen that decision based on old pins. Popup-shadow/caret-delay proposals need current upstream evidence. | Remaining real text/UIA, hardware and presented-frame qualification stays in the AV HOLD RFC; this plan owns only host/consumer accounting. |
| P5.2 | Persist measured and accepted durable budgets/behavior in owning contracts during each implementation closeout. Adapter/waitable/chrome/surface and P0.1 probe behavior already have contracts. Proposed trim/arena/dataset/persistence changes stay proposals until implemented and validated. | No durable behavior or accepted budget lives only in a plan or receipt. |
| P5.3 | Shipped templates and `HostPluginTests` fixtures match the P1.5/P1.6 decisions; `docs/` only if a user scenario changes. | Templates, schema, fixtures agree. |
| P5.4 | Move this plan to `Specs/Plans/Done/` and remove the active index row. | |

## Validation (every implementation phase)

```powershell
.\build.ps1 -Configuration Release
.\Test-Changes.ps1 -Explain
.\test.ps1 -Full -Configuration Release       # final noninteractive coverage, not hardware latency proof
.\build.ps1 -Configuration Release -Platform ARM64
.\validate-skills.ps1
.\.build\x64\Release\RedXe.exe --probe=5
```

- Read `probe` and `device-created` records from `%LocalAppData%\RedXe\Logs\RedXe-<date>.jsonl`; compare to
  `.build/receipts/`.
- PresentMon on `RedXe.exe` per shipped page: `PresentMode`, `MsBetweenPresents` p95, input-to-present.
- WPR 60 s per scenario; WPA: CPU by thread, thread start stacks, image loads.
- `Get-Process RedXe` private bytes, working set, handles, threads; the `GPU Process Memory` and `GPU Engine`
  counters for the pid show exactly one LUID after Phase 1.
- Debug and Release x64 WARP smoke and the `HostPluginTests` scheduler table stay green. WARP Present time is never a
  hardware gate. `build.ps1` defaults to Debug; every number in this plan comes from `-Configuration Release`.
- No desktop automation. Hidden HWND + existing production host is sufficient for memory/wake probes; the adapter and
  present-mode checks need the real XENEON.

## Excluded

- Host-owned primitive batching IID.
- Raising System Data row caps, cadences, or the 16 MiB ceiling.
- Unloading plugin modules while COM objects can remain (no-unload stays; sources, textures, caches, and coordinators
  are released by the settle sweep).
- Implementing AV G1/G2/IME/AT or changing the DxUi pin from this plan.
- WebView or push providers.
- Trading Matrix Focus visual quality without the P1.6 product decision.
- `D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS` (measured: no effect on either driver).
