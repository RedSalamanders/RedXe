# Runtime memory, lag, and whole-process resource budget

Status: `ACTIVE`
Created: 2026-09-07 (rewritten the same day after a live Release measurement)
Refreshed: 2026-10-12 against current `codex/runtime-probe` code, including the bounded Launcher construction lane, full-quality incoming-page barrier, accepted clock allocation diagnostic and production-paced clock soak fixtures; one commit per implemented step
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
- [`../../Plugins/Plugins_Weather.md`](../../Plugins/Plugins_Weather.md)
- [`../../Plugins/Plugins_Logicon.md`](../../Plugins/Plugins_Logicon.md) (Debug monitor preparation)
- [`../../UI/UI_Dashboard.md`](../../UI/UI_Dashboard.md)
- [`../../Core/Core_Settings.md`](../../Core/Core_Settings.md)
- [`../../Core/Core_DxUiIntegration.md`](../../Core/Core_DxUiIntegration.md)
- [`../../Plugins/Plugins_AVControl.md`](../../Plugins/Plugins_AVControl.md) (AV surface and helper process only)
- [`../../Build/Build_Process.md`](../../Build/Build_Process.md) (automated production-host resource guards)

Coordinate, do not fork:

- [`RFC_Plugins_AVControl.md`](RFC_Plugins_AVControl.md) still owns real IME/AT, G1 audio, G2 camera, and matched
  text/UIA performance. This plan only accounts AV's retained surfaces, coordinator, and helper-process cost.
- The network lane and forecast fetch are owned by `Specs/Plugins/Plugins_API.md` and `Specs/Plugins/Plugins_Weather.md`
  (landed via [`../Done/WeatherPlugin_2026-09-04.md`](../Done/WeatherPlugin_2026-09-04.md)).
- [`PluginDashboardRemainingCloseout_2026-09-02.md`](PluginDashboardRemainingCloseout_2026-09-02.md) still owns
  leftover architecture gates. Do not open host primitive batching from this file.
- [`PluginBoundaryHardening_2026-09-03.md`](PluginBoundaryHardening_2026-09-03.md) already made `PluginHost`
  process-scoped. Remaining G2/H3 work stays there.


## Current-code checkpoint (2026-10-12)

### Execution ledger

The owner requested all remaining work with a commit per step on 2026-10-10. Conditional proposals still require
current evidence and preserve visual quality and the later normative contracts. Each implemented step records
its validation and measurement; failed or unavailable qualification remains explicit.

| Step | State | Evidence / commit |
| --- | --- | --- |
| P0.1 | Complete | Production probe and accepted Release baseline; `0971350`. |
| P3.4 callback attribution and construction | Offscreen discrete construction implemented; whole-page/physical budgets open | The host-owned STA retains ordinary native pixels and fresh checks, copied CPU jobs, stale-generation rejection and a full-quality readiness barrier. Foundation commit `845f675` precedes production Launcher adoption. Both matched orders account for 18 stages/402 callbacks: Launcher UI visibility medians fall 68.354 -> 0.035 ms and 73.821 -> 0.033 ms. Whole Gallery readiness is mixed; initial/touch setup stays synchronous. The four-ms slice, physical first-pixel and return budgets remain open. |
| P4.3 WIC batch | Rejected after matched measurements | A temporary shared factory passes all functional/profile gates and 20 reference frames, but Launcher visibility medians rise in both capture orders: 88.21 → 96.48 ms and 82.97 → 92.89 ms. Production code, diagnostics and experiment tests are restored. Fewer factory creations alone do not justify the observed latency regression; shell extraction remains the measured target. |
| P4.3 single shell output | Complete; remaining activation budget open | The shell handler now supplies one consumed 256-pixel icon instead of two, with failure-safe ownership. Shipped-DLL tests and all profile obligations pass; final tooling covers the completed documents. Five shell WARP frame, thirteen icon-slice and fifteen glyph/empty-frame checksums match the control. Visibility medians fall in both capture orders, 106.05 → 87.71 ms and 216.33 → 93.99 ms, with substantial uncontrolled tail variability. Release 16 scope accounting, Debug15/ASan15, clean ARM64 Release compilation and skills10 are recorded below. Construction/first-pixel budgets stay open. |
| P4.3 shell attribution / folder binding | Complete; remaining activation budget open | Six alternating attribution pairs select repeated parent parsing: 28.18 → 4.31 ms per eight icons; total repeated extraction 68.39 → 43.00 ms. Both final hardware orders reduce visibility medians, 120.68 → 66.57 ms and 104.79 → 62.91 ms. Exact pins/alpha and bounded one-unit fallback RGB differences are recorded below. Release16, Debug15, ASan15 accounting, ARM64 compilation and skills10 cover the sealed final candidate and documents. The initial System Data oracle failure remains retained. Handler extraction and initial setup remain next; construction/first-pixel budgets are open. |
| P4.3 filename-handler reuse | Complete; `5a2e6c3` | Fresh location/shield/file checks permit a four-entry/1-MiB maximum BGRA cache (512 KiB in the shipped scene). Two eligible icons improve in every native pair by 1.9–5.9 ms combined. Both hardware visibility medians improve, 80.90 → 66.30 / 57.12 → 56.23 ms, but whole-batch/cold/page totals remain mixed. Pins and ten rendered frames are exact; fallback slices retain a one-unit RGB limit. Debug15/ASan15, Release16 accounting including final tooling, ARM64 compile and skills10 have sealed closeout identities. The initial Desk Clock guard failure remains retained. Opaque handlers and initial setup stay open. |
| P4.3 verified-file opaque handler reuse | Complete; remaining activation budget open | Exact opaque token/identifier identity, fresh no-shield/file checks and eight entries/2 MiB eliminate repeated handler extraction/decode for the shipped pins. All six native pairs improve, repeated median 52.54 → 39.69 ms. The intentional scene retention increase is 1.5 MiB. Both matched hardware orders complete, but visibility moves 61.13 → 69.48 and 61.05 → 54.01 ms; no page-activation or steady CPU saving is established. Raw pin/fallback/frame quality is exact, with the separate canonical hash mismatch retained. Release16, Debug15/ASan15, ARM64 compilation and skills10 require the sealed final closeout below. The initial Desk Clock guard failure stays retained. Fresh shield checks and initial setup remain next. |
| P4.3 combined shield/location and cold setup attribution | Complete; remaining activation budget open | One authoritative checked location replaces two queries; refused/pending/truncated outputs reset before an uncached ordinary fallback. Fifteen phases isolate cold parent creation and fresh checked location. Six final native pairs preserve all pins/720 hits per arm; repeated median improves 40.75 → 39.08 ms, cold totals stay mixed. Both hardware orders improve visibility, 84.31 → 64.18 / 65.63 → 60.22 ms. Returned CPU is lower but cold CPU mixed; private grows 1.25/0.55 MiB. Release16 accounting, Debug15/ASan15, ARM64 compile, skills10 and final-tooling seals are recorded below. Initial Matrix/Desk guard failures and strict fallback-hash mismatch remain retained. |
| P4.3 Launcher cold parent creation | Attribution/tests qualified; production shortcuts rejected | Path-to-PIDL parsing dominates parent creation; item creation from PIDL is only microseconds. Metadata native repeats improve 36.69 → 33.37 ms, but visibility worsens 46.72 → 71.17 / 45.59 → 58.08 ms and return CPU/submissions rise in both orders. The parse/create hook is unused and repeats are mostly worse. Ordinary production parsing, native quality and lifetime are preserved; seventeen-phase diagnostic failure tests and normal-route regression coverage are added. Release16 accounting, Debug15/ASan15, ARM64 compile, skills10 and final source/runtime seals are recorded below. |
| Release link correctness | Complete | `cdfcfca`: a production capture exposed stale member offsets in incremental LTCG despite passing hidden gates. Full LTCG retains optimization and prevents cache reuse. All 68 evaluated project configurations, 16 Release scopes, clean ARM64 Release compilation and ten skills pass. The recovered candidate completes 18 hardware page activations with 402 callbacks. The failed executable, linker cache, local dump and stacks remain preserved; no baseline is accepted for it. |
| P4.2 provider records | Complete | Count-sized scheduling ownership removes 140,576 logical bytes with the real System provider live, and adds one cold allocation. Validation precedes publication and records retire outside the lock at source quiescence. Descriptor caps/final delivery, allocation failure/retry, facade identity and lease/drain scenarios pass. Provider-only Debug15/ASan15 and recovered full-LTCG Release16, clean ARM64 compile and skills10 have sealed identities. Matched hardware arms have identical plugin DLLs and 18 complete stages/402 callbacks each. Returned private is 63.30 → 63.60 MiB; no process-memory, CPU or staging-latency saving is established. |
| P4.2 private JSON | Implemented and measured; screenshot investigation open | 24-byte immutable values replace 4,104-byte inline reserves, with exact payloads and no default ownership. Release shipped logical storage is 209,460 → 56,450 bytes, plus 288 bytes of shared control blocks and unmeasured allocator overhead. Ownership/bounds/two-phase failure/parser/persist/stable concurrent-copy tests pass Debug15, recovered Release16 and ASan15; ARM64 compile and skills10 pass. Matched arms each have 18 stages/402 callbacks and unchanged plugin DLLs. Cold/returned private is 36.59/63.45 → 35.96/62.63 MiB; working set rises, and no causal CPU or latency saving is established. The initial Release screenshot access violation remains unexplained despite subsequent full/isolated/retained passes. |
| P4.5 | Content-sized bodies and last-widget release complete | Production-path lower offline transport, exact transactional body storage and last-widget leases are implemented. Eight two-byte responses retain 24 bytes versus 2,097,160 before, including the final Release DLL; the new real-DLL storage assertion fails on the preserved old cache. Debug/ASan 15 scopes, ARM64 Release, ten skills and unchanged images pass. Release 16 obligations are accounted across runs, preserving the initial Desk Clock peak failure. Matched Gallery establishes no causal CPU saving; current D is 36.85 → 62.29 MiB private and 25.01 → 32.26 local, hidden/minimized zero. Curl reuse and failure coalescing remain separate measured candidates. |
| P4.6 | Hardware baseline and owned heap diagnostic captured; clock fixture pacing corrected | Five-minute hardware Desk/Studio runs retain current deadlines, atlas and quality. The replacement allocation ETL is loss-free and identifies fluctuating WARP pool payload; it does not explain the old failed peak. Clock fixtures now acquire DXGI readiness before initial/warm/measured draws. Fresh Release15, Debug15 accounting, ASan15, ARM64 compile and skills10 pass. Four counterbalanced Release guards pass with comparable cadence but mixed CPU/memory; no product saving is established. |
| P4.3 glyph extraction | Complete | Temporary isolated factory/face/coverage batch, 400 unchanged glyph references and real Debug coverage-OOM recovery pass. Matched eight/32-icon callbacks fall about 7.2->2.4 / 29->6.5 ms. Final Debug15/Release16/ASan15/ARM64/skills10 and all image references pass. Settled fixed-eight CPU/submissions stay zero, adapter18.01 MiB; no physical-input or D-retention claim. runtime-2026-10-11-p43-glyph-*. |
| P4.3 icon storage | Complete | Count-sized 0/2/8 MiB storage for 0/8/32 entries, 15 identical WARP frames, native rendered pixels, transactional faults, all scope accounting and matched fixed-eight/Gallery observations pass. The initial intermittent Matrix guard failure is retained. Fixed-eight private 73.23 -> 41.21 MiB and local adapter 38.01 -> 18.01 MiB; no CPU saving or D-return claim. runtime-2026-10-11-p43-* and p43-matched-comparison.json. |
| P3.3 | Plan reconciled; physical latency remains open | Current first-follow-frame/directional-two-page policy retained. Rejected September eager staging, three-live-page reversal and animation suppression conflict with UI_Dashboard. Existing policy/partially-offscreen/native-neighbor/retirement tests pass; P0.2 still owns real input and first-presentation timing. |
| P3.1 | Implementation complete; latency qualification remains open | Checked borrowed selection, exact bounded shared validation witness and staged visibility rollback pass. Both Release cohorts accept 36 stages. Selection and provider work fall, but GPU/visibility dominates and total latency improvement is unestablished. Release16, Debug/ASan15, ARM64 Release and ten skills pass; 145 Viewer/eight Weather references match. runtime-2026-10-11-p31-final and -stage-*. |
| P1.6 | Complete | Default-display/minor4 setting, overlapping one-shot/DXGI waits, motion/invalidation bypass and live apply pass. Corrected permit ownership recovers after hide/minimize at about 29.3/s; inactive zero. Final pairs reduce CPU 32.8/35.6% and submissions about 51%; all cohort frames displayed, independent flip/no hybrid. Cap private medians are 0.59/1.15 MiB higher, adapter usage unchanged; no memory saving or physical-input latency claim. Release16, Debug/ASan15, ARM64 Release and ten skills pass; 145 Viewer/eight Weather references match. runtime-2026-10-11-p16*. |
| P2.7 | Complete | Sample-driven scheduling, full gates and matched 13.7–13.9% submission reduction; `a466363`. CPU saving remains unestablished. |
| P0.2 | CPU attribution captured; other gates open | Accepted sampled-only System capture, 65 s normal-user Release probe: 2.392% of one core and 12.364 submissions/s. ETL has zero lost events/buffers and matching private/Microsoft symbols. A 56 s steady window has 1,251 named CPU stacks: acquisition worker 900, native process walk 578, `OpenProcess` 20 inclusive hits. These overlap, are sampled, and cover a subset of all 1,336 owned stacks. `runtime-2026-10-10-p02-cpu-sampled`. Earlier broad trace dropped 704,274 events and remains rejected. Input/presented latency and adapter-move qualification stay open. |
| P0.2 raised-state observation | Complete | Bit 8 observes the actual active raised layer, preserving v1, arrays and slot capacity. Hidden real Gallery/System raise/settle/render/dismiss checks and serialization pass Release 16, Debug 15 and ASan Debug 15 scopes; ARM64 Release compiles and ten skills pass. Disabled probes add no sampling, storage, allocation or wake. This enables strict input outcome checks; physical-input and first-pixel qualification remain open. |
| P0.2 synthetic input sequence | Complete interaction evidence; latency open | All twelve foreground/pointer-guarded wheel, raise, dismiss and blocked-edge outcomes match production page/raised state; app and trace exit zero, 4,622 owned PresentMon frames. Input-to-photon columns are unavailable. Next submission is not first changed pixels. `runtime-2026-10-11-p0-focused-verified-state`. |
| P0.2 adapter/DPI move | Programmatic evidence captured; visible/manual gate open | Owned non-activating Debug window: AMD/NVIDIA/AMD, 96/144/96 DPI, 2560x720/3840x1080/2560x720 client. Returned application screenshot is valid. Shared/dedicated usage follows the current adapter; old-adapter committed counters retain 4 KiB then 256 KiB. Literal single-LUID committed accounting and visible physical dragging remain unqualified. `runtime-2026-10-11-p0-adapter-move-owned`. |
| P3.6 allocation attribution | Captured; selected AV cut implemented | Zero-loss 140-second normal-user owned-alias heap/virtual-allocation trace resolves private application symbols. Cold/returned outstanding heap payload is about 28.46/46.01 MiB; top-100 AMD caller groups grow about 12.10 MiB. The old DLL retains AV coordinator/preferences stacks with zero widget probe counts; its global owner is now removed at last-provider release. Revised allocator-wrapper classification also identifies PDH/perflib and DirectWrite groups. Driver-private functions, extent accounting and whole-process return budget remain open. |
| P3.6 AV coordinator cache | Complete ownership cut; whole-memory acceptance open | The same new test executable fails the exact preserved old DLL at last widget-held provider release and passes the new DLL. Last-provider and cold allocation-failure cleanup preserve survivors and retiring host-work leases; fresh revisit and disconnected UIA remain safe. Release16/Debug15/ASan15, ARM64 compile and skills10 pass with 468 sealed inputs and deployed settings/PDB closure. Both hardware arms accept 18 stages/402 callbacks and equal initial/final scenes. Returned private is 62.18 → 64.60 MiB, with a 1.99-MiB cold difference already present before AV visits; no process-memory/CPU/latency saving is established. |
| P2.5 | Complete | `9b9cf15`: writer-owned date/policy cache removes both directory scans and path construction from ordinary lines. 1,000 same-day checks leave a late expired file untouched; policy and UTC changes reap it. Actual idle-policy flush passes. Release 16 scopes, Debug 15 native scopes and ARM64 Release compilation pass; `.build/receipts/runtime-2026-10-10-p25/`. No whole-process CPU reduction claimed for this infrequent log workload. |
| P2.4 | Complete | Render failure debug output shares the JSONL transition guard. 400 failing primary frames emit three reports; a new primary instance emits its own report. Whole-frame recovery resets identity; partial tile/raised recovery does not. Adds a 4-byte mask and no steady slot scan/allocation. Release 16 scopes, Debug 15 native scopes and ARM64 Release compilation pass; `.build/receipts/runtime-2026-10-10-p24/`. This removes repeated failure-path debug calls, not measured ordinary CPU. |
| P2.2 | Complete | 24-byte UI-thread reachable-rectangle cache. Production self-test proves 1,000 unchanged queries add zero display walks; a work-area notification adds exactly one. Window lifetime/geometry/style, DPI, topology and settings notifications invalidate before dispatch. All 16 Release scopes, 15 Debug native scopes, ARM64 Release compilation and 10 skills pass. Matched 65 s captures are in `runtime-2026-10-10-p22-before` / `-after`; no CPU saving established. |
| P3.2 | Complete | 1,024-byte key array, collision-safe comparison against original instance bytes, no new allocation/re-serialization. Compile-time transaction arrays <28 KiB. Actual PDB frames: Release Initialize/Reconfigure 26,208 bytes, StageActivePage 8,992; Debug 27,912/12,784 (prior Debug Initialize/Reconfigure 289,544). Forced collision, same-length distinct/equal configuration, background and invalid-index tests plus actual provider reuse/reconfiguration pass. Release 16 scopes, Debug 15 native scopes, ARM64 Release compilation and skills pass; `runtime-2026-10-10-p32`. P2.2 commit `abee7cc`. |
| P0.3 | Complete | Three isolated 60 s soaks now run in the HostPlugin scope. Clocks warm at scheduled cadence for 20 s; Matrix warms for 1,024 frames. Five-second/end samples enforce uninstrumented peak growth of 4 MiB private / 8 MiB working set, plus resource/cadence/teardown bounds in every profile. Release and Debug each pass 16 scopes; ASan Debug passes 15 native scopes with default quarantine and diagnostic byte reporting; ARM64 Release compiles and 10 skills pass. Final Release private peaks: Matrix 0, Studio 1,306,624, Desk 942,080 bytes. `runtime-2026-10-10-p03`. |
| P2.3 | Complete | Move-only 16-byte status pins; one atomic read per placeholder decision, no locked ID scan in Render/Prepare. Adds at most 1.5 KiB across two full pages, no allocation/thread/wake. 10,000 reads complete under the exclusively held status lock; concurrent reports, move/reuse/shutdown, same-ID replacement and 64-slot capacity pass. Slot destruction/move drains network registration and interface/callback lifetime before final release, including rollback. 100,000 Release reads: cached 72.8 us / last-slot cold lookup 80.6 ms, diagnostic only. Release 16 scopes; Debug and ASan Debug 15 native scopes; ARM64 Release compile; 10 skills. `runtime-2026-10-10-p23`; P3.2 commit `55da17f`. |
| P3.6 | In progress; source and GPU binding cuts complete | `7c1f72b`: quiescent last-consumer source reclamation preserves facade identity, inactive/staged subscriptions and external leases. Matched retained private 87.61 → 71.75 MiB. The separate GPU binding cut proves last-owner release, identical pixels and no device/swap-chain rebuild. All 16 Release scopes, 15 Debug and ASan Debug native scopes, ARM64 Release compilation and 10 skills pass. Its matched retained private is 69.54 → 69.08 MiB and local usage 47.26 → 46.26 MiB, versus cold 25.01. Submissions remain about 60/s, hidden/minimized zero; no CPU saving established. Remaining return budget and cache attribution stay open. `runtime-2026-10-10-p36-source`, `-p36-before`, `-p36-after`, `-p36-gpu`. |
| P3.6 Debug monitor face copy | Complete; broader P3.6 remains open | Lazy 753,424-byte virtual-memory owner returns 753,664 private-commit bytes on hide in focused Debug and final Debug/ASan tests. Cold black faces, clean reuse, allocation failure/latch, extent recovery, repeated hide/loss, final release and exact recovery pixels pass. Release 16 scopes, Debug/ASan 15 native scopes, ARM64 Release and 10 skills pass. Debug-only ownership; no Release CPU/return-budget claim. `runtime-2026-10-11-p36-monitor-scratch` and `-control`. |
| P2.1 | Complete; later arena cut recorded as P4.1 | The single-capacity query and eight-column activity consumer cuts pass all 16 Release scopes, 15 Debug/ASan native scopes, ARM64 Release and 10 skills. Activity opens zero native process handles; combined requests share one walk/history and leading values. All 145 Viewer references match. Counterbalanced System CPU is 1.895 → 1.689% and 1.898 → 1.612%; submissions vary in opposite directions. Live data is unpinned, so these observations establish no causal CPU/submission saving. Fixed source storage is 16,487,056 bytes, including 49,296 added bytes accepted for legacy compatibility. `runtime-2026-10-10-p21-*`, `runtime-2026-10-11-p21-activity*`. |
| P4.1 | Complete | One 16-MiB reservation, 4-KiB cold / 9.461-MiB shipped / 15.766-MiB full commitment; tested faults, history and final ownership. Public constructor median about 2.3 microseconds versus 1.8–2.0 ms. Matched System private reduction 7.24/7.25 MiB; CPU saving unestablished. All required scopes accounted, ARM64/skills and 145 pixels pass. Three settled D observations prove zero arenas/native owners; broader retention remains P3.6. runtime-2026-10-11-p41-arena*. |
| P2.8 / P3.6 CPU atlas lifetime | Complete; preparation cuts recorded below | Cold construction, shared users, idempotent device loss, last release and failure/retry pass against real DLLs. Both one-MiB CPU owners reach zero. Unicode recovery pixels and eight Weather images remain identical. Matched D private: cold 38.23 → 35.51 MiB, returned 106.39 → 105.06; returned working set 98.16 → 96.16. Submissions about 60/s and inactive zero, local usage unchanged. Return-over-cold 68.16 → 69.55 MiB still misses the budget; no causal CPU saving claimed. Release 16 scopes, Debug/ASan 15 native scopes, ARM64 Release compilation and 10 skills pass. `runtime-2026-10-10-p28-before`, `runtime-2026-10-11-atlas-after` and `-p28-atlas`. |
| P2.8 Viewer preparation | Complete | Worker delivery freezes at UI preparation; a dirty glyph uploads 2,304 bytes, Render uploads none, and one temporary font session serves a preparation batch with zero retained owners. Added pending storage is 11,160 bytes per widget. All 145 preceding frame checksums match. Fresh matched control/candidate: CPU 2.134 / 2.055%, submissions 12.244 / 11.954/s, private 57.62 / 57.92 MiB, local 26.26 in both. Rate improvement remains unestablished. Release 16 scopes, Debug 15 native scopes accounted across the initial run and final HostPlugin/HostSmoke run, ASan 15 native scopes, ARM64 Release compilation and 10 skills pass. `runtime-2026-10-11-p28-prepare-paired` and `-p28-prepare`. |
| P2.8 Weather preparation | Complete | One 9,904-byte UI paint snapshot; one temporary batch font session, zero retained owners, 2,304-byte dirty-cell uploads and no worker font/Render upload work. Real worker, failure/recovery and eight identical PNGs pass. Release 16 scopes, Debug/ASan 15 native scopes, ARM64 Release and 10 skills pass. Two counterbalanced Gallery pairs remain about 60.15 submissions/s; CPU has no established saving or repeatable regression. `runtime-2026-10-11-p28-weather`, `-paired` and `-paired-reverse`. |
| P2.8 glyph capacity | Complete | Real preceding DLLs fail reused-cell ink and exhausted-label regressions; candidate passes with unchanged atlas/storage sizes. Eight Weather PNGs match. Release 16 scopes, Debug 15 native scopes accounted across the initial run and HostSmoke repeat, ASan 15 native scopes, ARM64 Release and 10 skills pass. The initial Debug capture crash remains unexplained and retained. `runtime-2026-10-11-p28-capacity`. |
| P2.8 Debug monitor and shared sampling | Complete | Unchanged failed preparation suppresses font/raster/upload work; all recovery, real font cleanup and held-control release tests pass. A shared glyph-cell sampling fix resolves 16 visible neighboring-ink pixels after cache/device changes. All 145 Viewer references and eight Weather PNGs match. Final Release 16 scopes, Debug/ASan 15 native scopes, ARM64 Release and ten skills pass. Two counterbalanced System pairs establish no causal CPU saving or repeatable private-memory regression; local usage is 26.26 MiB throughout. QPC-joined PresentMon cohorts show hardware-composed independent flip, all cohort frames displayed and no hybrid presents. Input latency remains unqualified. `runtime-2026-10-11-p28-monitor`, `-paired`, `-paired-reverse`, `-presented` and `-presented-reverse`. |
| P0.4 production raised Gallery | Accounting captured; presented-frame qualification remains open | One normal, non-activating hardware Release run proves AV tile 1,474,560 bytes, raised total 5,160,960, dismissal to tile, hidden zero and restored tile. Cold/raised settled 65 s each; all source/binary hashes, page, geometry, adapter, power and foreground checks pass. Visible submissions stay about 60/s; hidden zero. `runtime-2026-10-11-p04-raised-gallery`. |

### Release link correction during P4.2 validation (2026-10-11)

The first complete-interval provider candidate capture hung when Gallery activation acquired the network lock.
Its normal-user executable `F9B16A26D3B3DDD56353A5761F991CB13773C110DA2DB92A3AFB6BB19CA26D19`
had passed all 16 hidden Release scopes, but the UI stack stopped in `DashboardHost::SetWidgetsVisible`.
Disassembly accessed `_networkLock` at 167,520 bytes and `_networkSlots` at 167,328, while the new complete
`PluginHost` layout is 103,200 bytes with those fields at 26,392 and 26,200. The incremental-LTCG log reports
190/6,482 functions regenerated, with the remainder copied. A clean rebuild emits the current offsets.
The toolset's evaluated x64 default and link command both select `/LTCG:incremental`; the new regression fails on
that default. The shared targets now require `/LTCG` for optimized Release. Whole-program optimization remains.

The configuration regression checks 68 real-project metadata evaluations without compiling; recovered Release
passes all 16 scopes, clean ARM64 Release compiles with no warnings/errors, and ten skills pass. The native inputs
and each tested Release artifact are sealed in `p42-provider-recovery-*.json`. Prior Debug/ASan provider receipts
retain their earlier source closure and are not relabeled as the build-policy closure. The tested candidate is
preserved byte for byte; a new old-layout control is built under the same policy and passes a fresh hidden smoke.
Only the host, host-test executable and host provenance differ; all deployed plugin DLLs match.

`runtime-2026-10-11-p42-provider-candidate-complete` remains rejected, with its local full dump, owned-thread stacks
and suspect executable/PDB/LTCG cache preserved. Only its owned hung PID 92928 was terminated after capture.
Earlier short-phase controls remain rejected because they did not contain a complete probe interval. The
complete-interval control is valid by itself, but is not paired with the hung candidate. The fresh full-LTCG arms
each complete 18 hardware stages and 402 callbacks, including the formerly hung Gallery activation. This correction changes the build contract in
`Specs/Build/Build_Process.md`; it does not establish a CPU saving or close P0.2 latency qualification.

### P4.2 count-sized provider records (2026-10-11, complete)

Each catalog slot now owns a 48-byte provider value; scheduling records allocate exactly the validated descriptor
count at 24 bytes each. A cold or retired slot has no record allocation. Full descriptor/column/duplicate validation
precedes publication. A local candidate preserves transactional failure and facade identity on rehydration;
acquisition reuses the array and quiescent retirement releases records and source outside the registry lock.

The fixed 23-slot storage falls from 142,232 to 1,104 bytes. The real System provider adds 552 bytes for its 23
records: 1,656 total versus 142,232, removing 140,576 logical bytes. This adds one cold heap allocation per live
provider, with allocator overhead excluded. Test witnesses compute owned logical records and bounds; they are not
independent heap-extent measurements. PDBs independently confirm 24/48-byte records/owners and a 103,200-byte
production host, versus the old 244,328-byte host. Tests cover invalid/zero/257 counts, late invalid descriptors and
columns before allocation, validated 256-record failure/retry, real final-descriptor delivery, first-publication
failure, stable retired facade identity, inactive/shared/staged leases, blocked collection, reserved callbacks,
shutdown and revisit. The implementation changes no callback ABI, snapshot borrow, rendering or scheduling policy.

Provider-only Debug15 and ASan15 native scopes pass with frozen sources/artifacts. The recovered full-LTCG Release
passes all 16 scopes, clean ARM64 Release compiles with no warnings/errors, and ten skills pass. Both matched arms
use full LTCG, version 1.0.282, the same settings/collector and byte-identical deployed plugin DLLs/dependencies.
The new old-layout control has a fresh hidden smoke; the candidate uses the exact immutable bytes of the recovered
Release full gate. Restored source timestamps force the next build to regenerate the changed layout.

| Hardware Matrix phase | Old provider layout | Count-sized records |
| --- | --- | --- |
| Cold private / working set | 36.09 / 49.94 MiB | 36.37 / 49.83 MiB |
| Returned private / working set | 63.30 / 87.38 MiB | 63.60 / 87.32 MiB |
| Cold / returned CPU, percent of one core | 2.030 / 2.317 | 2.134 / 2.005 |
| Cold / returned submissions per second | 60.119 / 60.152 | 60.119 / 60.151 |
| Cold / returned adapter-local usage | 25.01 / 32.26 MiB | 25.01 / 32.26 MiB |
| Hidden / minimized submissions per second | 0 / 0 | 0 / 0 |

Each arm has 18 complete stages, 402 callbacks and unchanged capture identities. The logical reduction is accepted;
process-memory, CPU and staging improvements are unestablished. Gallery stage medians are 116.34 → 135.14 ms, with
variable maxima of 499.37 / 338.09 ms. The unchanged Launcher DLL still dominates: visibility median 96.53 → 103.95 ms
(CPU median 85.94 → 93.75 ms). Live data, shell and driver caches are uncontrolled; these observations do not assign
that difference to the provider change. No four-ms slice, input-to-present or first-pixel gate is claimed.

Read-only region accounting agrees with raw region totals and the owned PID. Private committed regions grow
27,795,456 / 27,836,416 bytes (about 26.5 MiB) from cold to returned. Resident private grows
25,939,968 / 26,030,080 bytes; mapped commit grows 3,518,464 and image commit 18,829,312 in both arms. Address walks
and working-set queries are non-atomic; resident shareable pages do not prove actual sharing, and nonresident
copy-on-write private charges are excluded. These partitions do not identify a heap owner or authorize trimming,
module unloading or a driver-cache claim. The returned A+4-MiB budget and adapter-local retention remain open.

Receipts: `runtime-2026-10-11-p42-provider-full-ltcg-control`, `-candidate`, their callback/region summaries,
`p42-provider-full-ltcg-matched-comparison.json`, `p42-provider-full-ltcg-control-closure.json`,
`p42-provider-recovery-*.json`, `p42-provider-final-*.json` and `runtime-2026-10-11-p42-provider`. The earlier rejected
short-phase and hung captures remain separate. The following record covers the private JSON candidate.

### P4.2 immutable private JSON (2026-10-11, implementation and matched measurement)

`JsonObjectSettings` now holds a 24-byte immutable shared owner and an exact payload plus terminator. Default `{}`
owns nothing. Copies/moves allocate nothing, moved-from values expose a safe default, and equality compares logical
bytes across distinct owners. Changed non-default values add two cold allocations; both complete before replacement.
Cold JSON/plugin validation, the 4096-byte object/8192-byte envelope/1-MiB authored source limits and `sourceDocument`
persistence remain. No frame/collection allocation, shader, settings-visible behavior or visual trade is added.

The real shipped Release fixture reports 56,450 bytes versus 209,460 before, including 1,882 unique payload bytes
across twelve owners. PDBs independently confirm 24-byte values and 24-byte shared control blocks: 56,738 bytes with
those blocks, excluding allocator overhead. Debug/ASan report 56,522 logical bytes because their container layout
differs. These are owned logical extents, not independent physical-heap measurements or a universal 64-KiB guarantee.
Authored source and validation witness remain 4,876 and 6,166 bytes. Tests prove exact terminators/caps, immutable
sharing/replacement, distinct-owner equality, last-payload release, safe moves, first/replacement allocation faults
and retries, parser/persist rollback and 8,192 concurrent copies of a stable value. Fault phases are test-only seams,
not a global allocator spy; shared-owner failure is injected before its construction with RAII payload cleanup.

Debug Full15, recovered Release Full16 and ASan Full15 pass on 384 frozen native inputs with artifacts unchanged
after execution. ARM64 Release compiles with zero warnings/errors; ten skills pass. The initial Debug test fixture
incorrectly assumed resolved plugin defaults were absent; it failed, was corrected to retain independent prior
bytes, and the focused/final gates pass. Its failed log is retained. The initial Release Full run completed native
integration and all three resource guards, then its ordinary screenshot child PID 6708 exited with `0xC0000005`
without a PNG or retained crash stack. That failure is unresolved, not erased by the later successful Full16 run.
Separate exact-artifact Release/ASan screenshot checks, three alternating debugger captures per arm, and five
alternating normal captures per arm pass. No code change or root-cause attribution is made from these passes.
Final uninstrumented private guard peaks are Matrix 3,526,656, Studio 790,528 and Desk 3,088,384 bytes, all below
4 MiB; their current stable object/resource assertions pass. ASan byte growth is diagnostic under quarantine.

Both hardware arms use full LTCG, version 1.0.282, equal scenes/collectors, all identical deployed plugin DLLs and
18 complete stages/402 callbacks. The control's 382 recorded native inputs are accounted against `3a5874c`, with
exact recorded checkout bytes and EOL handling; candidate source/artifact identities remain unchanged throughout.

| Hardware Matrix phase | Inline private JSON | Immutable private JSON |
| --- | --- | --- |
| Cold private / working set | 36.59 / 49.86 MiB | 35.96 / 51.63 MiB |
| Returned private / working set | 63.45 / 87.71 MiB | 62.63 / 89.34 MiB |
| Cold / returned CPU, percent of one core | 1.952 / 2.031 | 1.926 / 2.186 |
| Cold / returned submissions per second | 59.869 / 60.101 | 59.736 / 60.069 |
| Cold / returned adapter-local usage | 25.01 / 32.26 MiB | 25.01 / 32.26 MiB |
| Hidden / minimized submissions per second | 0 / 0 | 0 / 0 |
| Hidden / minimized CPU, percent of one core | 0 / 0 | 0.078 / 0 |

This single-order pair observes lower private commit and higher working set; live data, shell/driver/system mappings
and heap history are uncontrolled. It establishes no causal CPU, staging or whole-process memory saving. Gallery
stage medians are 138.50 → 119.47 ms (maxima 400.89 / 381.19); the unchanged Launcher DLL still dominates visibility
at 111.05 → 97.62 ms. Those observations do not prove a JSON latency improvement or qualify the construction/input
budgets. Private committed regions grow 27,459,584 / 27,254,784 bytes; the non-atomic maps do not identify live heap
owners or retained free segments. Returned process private remains about 27 MiB above cold and local usage +7.25 MiB.
Heap allocation/ownership attribution and Launcher shell extraction remain the next measured investigations.

Receipts: `runtime-2026-10-11-p42-json-control`, `-candidate`, `p42-json-matched-comparison.json`, their callback and
region summaries, `p42-json-recovered-scope-accounting.json`, final native/artifact seals, control committed-source
accounting, runtime closure and PDB owner layouts. `p42-json-release-screenshot-failure`, `p42-json-screenshots-normal`
and the individual debugger roots retain failure/repetition evidence. The full plan stays ACTIVE for the unexplained
screenshot failure, retention and remaining physical-input/presentation/manual AV gates.

### P3.4 callback attribution and completeness (2026-10-11)

The opt-in probe now records individual device creation, target size, visibility and first-preparation wall/CPU
timings by process stage identity and placement ordinal. Promotion before preparation, cancellation, setup rollback,
device loss and shutdown have real production-host fixtures. Normal launches perform no callback timing/query/logging.
The renderer adds one eight-byte owner; the local stage sample adds 40 bytes and its six-sample collector uses
376 bytes. One optional 784-byte table retains at most two pending identities and is freed when empty. No per-widget
field, worker, periodic wake, font cache, snapshot or visual-policy change is introduced.

The initial one-record-per-callback prototype passed all profile gates but its hardware capture is incomplete:
all 18 stage parents and resource phases are present, while the first System stage has no first-preparation records
or terminal. Its parent reports zero setup drops, which cannot establish later completeness. The analysis rejects
that cohort; the raw logs, identities and prototype artifacts are retained in `runtime-2026-10-11-p34-stage-candidate`
and `p34-unbatched-artifacts`. The six-sample format reduces a production 32-widget setup/preparation burst to at
most 24 lines in the unchanged 32-slot ring, retaining every sample and explicit refusal counts without a UI flush.
Final batched Debug15/Release16/ASan15 scope accounting, ARM64 Release compilation and all ten skills pass against
sealed native inputs and post-build artifact identities. Release Matrix/Studio/Desk peak private growth is
1,024,000 / 1,380,352 / 2,322,432 bytes under the unchanged four-MiB guards; ASan memory is diagnostic only.
`p34-batched-final-*` preserves exact gates and `p34-batched-debug-types.txt` preserves compiler object layouts.

`runtime-2026-10-11-p34-stage-batched` accepts all 18 expected stages, 402 callbacks and 18 unique terminals,
with matching placement ordinals, preparation masks/counts, successful stages and zero reported sample refusals.
The preserved control is `runtime-2026-10-11-p34-stage-control`. All runtime DLLs/helpers/fonts match; only the
host binary differs, and DxUi provenance changes only its consumer hash. The pin, public headers, archive,
toolchain fingerprint and other consumers match. Both use build 282, services/tray disabled, the same portable
Release scene and collector, AC power, hardware XENEON 2560x720 at 96 DPI, three directional page cycles and
one-second probes. Production imports/live data can change; these are observations, not causal rate comparisons.

| Incoming page | Stages | Stage wall median / maximum | Stage CPU median | First-Prepare wall sum median / maximum |
| --- | ---: | ---: | ---: | ---: |
| Matrix Focus | 3 | 0.394 / 0.417 ms | 0 ms quantized | 0 / 0 ms |
| Plugin Gallery | 6 | 120.114 / 400.310 ms | 101.563 ms | 1.694 / 5.234 ms |
| System | 6 | 10.024 / 11.126 ms | 15.625 ms | 0.008 / 0.016 ms |
| Shaders | 3 | 23.715 / 27.436 ms | 31.250 ms | 0 / 0 ms |

Launcher `SetVisible(TRUE)` accounts for 96.009 ms median / 244.195 ms maximum across six Gallery stages,
with quantized CPU median 85.938 ms. Shader device creation is 23.422 ms median / 27.093 ms maximum;
Desk device creation 10.739 ms median, and the first Viewer and Weather device callbacks about 9.5 / 9.2 ms.
AV device creation has a 129.020 ms cold maximum but 0.680 ms median; first AV preparation is 1.397 ms median.
Inspect Launcher's visibility import/extraction path next, before a general construction worker, slice or lazy AV
attachment. The old multi-second Gallery outlier is not reproduced or attributed by this capture.

Control/candidate cold Matrix CPU is 1.887 / 2.133% of one core and successful submissions 60.062 / 60.114 per
second; returned Matrix is 2.217 / 1.724% and 60.165 / 59.955 per second. Returned private is 62.04 / 62.29 MiB,
local adapter usage 32.26 MiB in both (cold 25.01). Hidden/minimized submissions are zero in both; hidden CPU
0.068 / 0.067%, minimized zero at this resolution. No CPU or retained-memory saving is claimed for instrumentation.
No first-presentation or four-ms construction-slice budget is qualified by these non-activating observations;
callback timers exclude factories, host drain waits, retirement and rollback.

### P4.5 content-sized Weather cache and leases (2026-10-11)

The preceding eight-entry cache allocated 256 KiB+1 per used URL despite smaller content, independently of bounded
heap-owned response buffers, and survived last widget destruction until module shutdown. The implemented change keeps
response buffers unchanged, stores only exact body bytes plus terminator with transactional metadata/body replacement,
reuses equal-sized storage and clears the cache after the last widget's strong callback references drain. A survivor
or hide must retain shared entries. No polling, worker or process font/driver trim is selected.

The existing offline response hook bypasses cache and cannot validate those rules. A lower transport hook now feeds
the actual lookup/revalidation/store path; it never initializes or runs curl. Existing fixture behavior stays intact.
New controls/direct GET exports require the automated-host environment. Borrowed transport pointers and POD sizes
are pinned. Normal curl receives a bounded terminated URL and rejects embedded NUL; cache metrics count allocated
body bytes including terminators. Curl and request-header owners use WIL, preserving the existing request options.
The actual eight-response fixture reports 2,097,160 retained bytes before and 24 after. New real-DLL tests cover
record/URL/body bounds, empty/maximum payloads, expiry and huge-age saturation, validators/304, equal-size reuse,
transactional new/grow/shrink/eviction failure, eight-slot capacity, cancellation/status/transport failures and legacy
bypass. Two real widgets prove hide/survivor retention and final strong network-reference release. The preceding
instrumented cache fails the actual four-byte storage assertion; an initial incomplete loader fixture is retained
as rejected evidence. The final Release DLL also reports eight entries and 24 retained body bytes. Compiler PDB
records preserve `WeatherWidget` at 21,496 bytes and `CacheEntry` at 2,344; no per-widget lease field is added.

Debug Full and ASan Debug each pass all 15 native scopes; ARM64 Release compiles without warnings/errors and all
ten skills pass. All eight Weather PNGs and 145 Viewer reference frames remain identical. The initial Release Full
run fails Desk Clock's unchanged four-MiB private peak guard at 5,394,432 bytes (end 3,534,848). A diagnostic VM-map
repeat passes at 2,928,640, and a separate unchanged-artifact clean guard passes at 1,843,200. Remaining HostSmoke
passes: all 16 Release obligations are accounted across these runs, without relabeling the initial Full as passed.
Five-second VM sampling does not attribute that peak to a heap or driver; P4.6 retains the variability investigation.

One matched build-282 Gallery pair changes only Weather.dll/pdb among its copied runtime closure. Over complete
60-second intervals, process CPU is 2.369 / 2.577 percent of one core, submissions 60.152 / 60.152 per second,
private 58.51 / 58.75 MiB, working set 76.86 / 77.08 and local adapter usage 34.24 / 34.24. Live location/network
responses and shell imports are not pinned: this observation establishes neither a causal CPU saving nor an
attributed process-memory regression. The exact retained-body and last-owner tests establish the selected cut.
The current D-cycle visits all four shipped pages and returns to Matrix, with 65-second cold/returned settled
phases. Private memory is 36.85 → 62.29 MiB (+25.44), working set 49.54 → 85.21, local adapter usage
25.01 → 32.26 (+7.25), threads 42 → 43 and mapped modules 71 → 102. CPU is 1.797 → 2.083 percent of one core;
submissions 60.150 → 60.152 per second. Hidden and minimized phases submit zero frames and report zero interval
CPU at the probe's resolution; restore resumes presentation. Geometry, settings/services, awake display, adapter and
source/artifact identity checks pass. This is a current observation after several committed cuts, without an
otherwise-matched old D run: it cannot attribute the earlier retention reduction to this cache change, and still
does not accept the proposed cold-plus-four-MiB budget. Curl connection reuse and negative failure coalescing remain
conditional on separate request/cancellation and ownership measurements.

Receipts: `.build/receipts/p45-final-scope-accounting.json`, `p45-content-release-capacity.json`,
`p45-object-size.json`, `p45-fixed-cache-red`, `runtime-2026-10-11-p45-images`, `-p45-gallery-control`,
`-p45-gallery-candidate` and `-p45-cycle-candidate`. Failed/diagnostic clock logs and the missing-loader red attempt
remain retained with their limitations.

### P4.3 WIC factory experiment (2026-10-11, rejected)

One lazy temporary WIC owner per refresh replaces separate decode, copy, crop and scale factories. The experiment
preserves extraction order, dimensions, alpha/padding and all 20 PNG/shell reference frames. Real-DLL sharing,
empty/glyph-only zero creation, transient creation failure/retry, clean preparation and device-recovery tests pass.
Its native inputs and artifacts are sealed: Debug 15, Release 16 and default-quarantine ASan Debug 15 scopes pass,
alongside ARM64 Release compilation and 10 skills. Release private-growth peaks are Matrix 1,830,912, Studio
1,593,344 and Desk 1,441,792 bytes, within the ordinary four-MiB guard. These functional passes do not accept latency.

Two fresh XENEON Release pairs run the identical 20-second, one-second-probe, three-cycle sequence in opposite
orders. Every run has 18 complete stage parents, 402 callbacks, 18 preparation terminals and no dropped records.
Scene/collector/OS/CPU/power/geometry/build-number checks match; only Launcher.dll changes among runtime binaries.
The final portable documents also have identical hashes. Each pair observes six Launcher visibility callbacks:

| Metric | Control / candidate, first order | Control / candidate, reverse order |
| --- | --- | --- |
| Visibility median, ms | 88.2095 / 96.483 | 82.9675 / 92.8905 |
| Visibility maximum, ms | 368.447 / 166.976 | 175.811 / 208.182 |
| Quantized callback CPU median, ms | 78.125 / 85.9375 | 78.125 / 85.9375 |
| Whole Gallery stage median, ms | 105.620 / 117.0385 | 103.224 / 118.497 |

The candidate's visibility median is higher in both observations. Live shell/driver/data state is not pinned, so
this does not identify the causal mechanism, but it does not establish the required improvement. Reject the
batching change and retain the preceding implementation; no permanent icon cache or worker is substituted.
First-order cold/returned Matrix CPU is 2.301/1.808 percent in control and 2.053/1.724 in candidate; submissions
are about 60.16/s in both. Hidden/minimized submissions are zero throughout. Cold/returned local usage is
25.01/32.26 MiB in both; private values vary by less than one MiB within each pair. None proves causal steady CPU,
return-memory, physical-input or first-changed-pixel savings.

Receipts: `.build/receipts/p43-wic-final-scope-accounting.json`, `p43-wic-pixel-comparison.json`,
`p43-wic-matched-comparison.json`, `p43-wic-matched-comparison-reverse.json`, `runtime-2026-10-11-p43-wic`,
`runtime-2026-10-11-p43-wic-stage-{control,candidate}` and their `-reverse` counterparts.
`p43-wic-rejected-candidate` preserves the rejected source and Release artifacts. The first test-build syntax
failure and the missing reference-DLL dependency attempt remain in their original logs. This experiment is a
completed decision, not an accepted optimization or qualification of the restored tree's current build outputs.

### P4.3 single-output shell extraction (2026-10-11)

Current inspection finds that `IExtractIconW::Extract` receives both large and small output pointers, with both
requested edges set to 256, but drawing consumes only the large result. The
[Windows extraction contract](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-iextracticonw-extract)
allows the unused output to be null. Request one 256-pixel large icon and a zero small edge, keeping the existing
handler/image-factory/image-list order, WIC conversion/cropping, glyph quality, shaders and count-sized storage.
WIL owns the returned handle before status inspection, including partial outputs from a failing handler. This
adds no retained icon/font cache, worker, timer, per-widget state or steady render/preparation work. The rejected
WIC factory batch stays rejected.

A private automated-only export borrows a supplied shell handler for one call through the actual production
handler/decoder. Real-DLL tests check exact location/index/size and omitted small output, full opaque BGRA, null
success, location refusal, `S_FALSE`/failure with a supplied handle, repeated release without GDI/USER growth,
borrow lifetime, null outputs and access refusal. A fixed thirteen-entry scene includes all eight hashed taskbar
links, a direct executable, URI, missing file, glyph and invalid-binding warning. The identical Release test
executable loads each preserved DLL: five WARP frames at 96/144/192 DPI, all thirteen 256-pixel texture slices and
fifteen empty/eight/32-glyph frames have matching complete frame and per-slice checksums. Existing pin/import/drop, preparation failure/retry,
device-loss and allocation-free composition checks pass the initial focused scopes.

Both build-282 hardware pairs have identical initial/final portable scenes, pin contents, collector, XENEON
2560x720/96-DPI geometry, AC power and DxUi closure. Only Launcher DLL/PDB differ; provenance is identical because
Launcher does not consume DxUi. Every arm has 18 complete stages and 402 callbacks, with complete first-preparation
terminals and zero dropped records. The second pair runs candidate before control.

| Control / candidate metric | First order | Reverse order |
| --- | --- | --- |
| Launcher visibility median, ms | 106.053 / 87.708 | 216.333 / 93.994 |
| Launcher visibility maximum, ms | 239.028 / 226.504 | 1,214.457 / 345.597 |
| Quantized visibility CPU median, ms | 93.750 / 78.125 | 132.813 / 93.750 |
| Whole Gallery stage median, ms | 132.583 / 108.931 | 253.216 / 120.769 |
| Returned Matrix CPU, percent of one core | 2.303 / 2.230 | 2.799 / 2.450 |
| Returned Matrix submissions per second | 60.291 / 59.941 | 59.989 / 60.152 |
| Returned private / working set, MiB | 63.06 / 87.35 → 62.66 / 86.77 | 63.43 / 87.09 → 62.60 / 87.32 |
| Cold private, MiB | 35.25 / 34.95 | 36.26 / 35.46 |
| Returned adapter-local usage, MiB | 32.26 / 32.26 | 32.26 / 32.26 |
| Hidden/minimized submissions per second | 0 / 0 in both arms | 0 / 0 in both arms |

The visibility median is lower in both observations, but the reverse control's large wall-time outliers and the
variable CPU/memory values prevent a fixed percentage or causal steady CPU/memory claim. One candidate cold
activation still costs 345.597 ms. In a separate STA diagnostic, sixteen alternating eight-pin API pairs return
128 discarded small icons in the old path and none in the new one. `IExtractIconW::Extract` alone, after location
resolution, has batch medians 65.763/54.574 ms. This establishes actual redundant output work and a measured
extraction reduction; managed COM overhead, binding, location, decoding and first pixels are outside that timing.

Release's three focused scopes plus twelve remaining native scopes pass without repeating the focused work. The
final BuildProcess closeout covers the completed plan/contracts and supplies the sixteenth Release obligation.
Debug Full and ASan Debug each pass fifteen native scopes; all builds have zero warnings/errors, ARM64 Release
compiles and ten skills validate. ARM64 execution remains unqualified on this x64 host. ASan uses its default
quarantine and its process byte growth is diagnostic, separate from ordinary resource-budget acceptance.
The sealed 468-input closure includes tracked/unignored non-prose inputs and the schema. Each profile's
EXE/DLL/PDB/fonts, DxUi provenance and deployed settings/schema are also sealed. Inputs and artifacts remain
unchanged through testing and the matched measurements; native inventory stays at 62. No appearance, input, launch, settings or user-guide change
is selected. Durable requirements are in `Plugins_API.md` and `Core_PerformanceAndResources.md`.

Live shell/driver/data histories remain uncontrolled. These observations do not qualify physical-input latency or
first changed pixels. The proposed 4-ms construction slice
and 16.7-ms frame budget remain open. Next work must attribute the remaining handler/location/decode cost before
selecting caching or host-owned background construction that preserves correct first icons and two-page lifetime.

Receipts: `p43-single-shell-{inputs,pixel-comparison,matched-comparison,matched-outcome}.json`,
`runtime-2026-10-11-p43-single-shell-{control,candidate}` and their reverse-order counterparts,
`p43-single-shell-api-pairs.json`, `p43-single-shell-pin-inputs.json`, `p43-single-shell-scope-accounting.json`,
profile artifact seals and `p43-single-shell-closeout.json`. Initial compiler
and malformed helper-CLI failures are retained. The initial comparison expected an unnecessary provenance change;
the repaired exact DLL/PDB comparison uses the same immutable captures, without another capture.

### P4.3 Launcher shell attribution and folder binding (2026-10-12, complete)

The call-local diagnostic measures the `a2c28cb` Launcher DLL extraction path to time shell-item parsing,
binding, location lookup, extraction and full WIC conversion/cropping. Its eight fixed phases also cover fallback
image-factory/interface work, bitmap decoding and image-list work. Exact-size caller-owned records distinguish
completed phases from unavailable work, preserve completed failure results and hash pixels outside the timed
interval. The single-target form retains full-path binding as a reference; the batch form uses the production
refresh-local owner. Automated-only access introduces no host calls, global timing state or ordinary-path clocks.

The initial six fresh STA processes make 768 calls against eight content-hashed taskbar links. The repeated
eight-icon median is 71.668 ms: parsing 29.422, binding 9.254, location 2.154, extraction 27.374 and full decode
2.547 ms. First-batch medians are 161.135 ms total, 70.980 parsing and 37.172 binding. Every call selects the icon
handler; no fallback is taken. Parsing/binding is selected before changing production code; the rejected WIC
factory experiment remains rejected. These early cohorts alone are not a matched final performance comparison.

One refresh now shares one `IShellFolder` for adjacent drive-qualified targets and parses/binds each child through
the same `IExtractIconW` route. The exact-spelling bounded key avoids case-sensitive directory aliasing. Drive
roots keep their separator; UNC roots, device paths, shell namespaces and other unsupported forms use the original
full-path route. A refused relative bind also falls back, while a handler that already refused extraction is not
called twice before image-factory/image-list fallback. Parent changes replace the owner, and all temporary owners
release within the refresh. There is no retained icon/location cache, worker, timer, per-widget field, lower-quality
source, shader or layout change. Parent and mutable-child path storage add about two KiB of bounded cold stack.

Shipped-DLL tests cover phase availability, exact-size/atomic refusal, borrowed extractors, location/index/large-only
protocol, partial/null/failing HICON ownership, same-parent reuse, identical child names in mixed parents,
exact-case keys, roots, Unicode/maximum path lengths, UNC/device/namespace routing, missing targets and URI
fallback, changed shortcut/icon location, the 32-entry bound and GDI/USER stability. Final Release's three focused
scopes and twelve remaining native scopes pass. Profile qualification and completed-document tooling are recorded
below; every canonical build has zero compiler warnings/errors.

Quality verification retains the strict raw-hash failures rather than relabeling them. All eight pins, the direct
executable, glyph and warning slices are byte-identical; the large-scene five frames and fifteen glyph/empty frames
also match. Raw readback identifies only 504 URI pixels and fourteen stock-icon pixels with a one-unit RGB
difference and identical alpha. The candidate's full-path reference produces the same pixels as its batch; the
WIC conversion/crop/scale source is unchanged. A second small-icon scene exposes both fallback icons: every slice
has identical alpha, and all five complete frames have at most a one-unit RGB difference (at most 87 changed pixels).
This numerical limit is explicit in the quality receipt; exact checksums for all thirteen slices are not claimed.
The difference's underlying cause remains unassigned. No filtering, source-resolution or geometry trade is selected.

Both matched Release orders pass. Both arms use the shipped four-page layout with eight frozen authored shortcuts,
avoiding imported-pin persistence during staging; ordinary pin import remains covered by integration tests.
The rejected earlier sequence never settles Gallery and records
an aborted preparation; it is not performance evidence. The 4-ms construction and first-changed-pixel budgets
remain open until supported by current evidence.

The final extraction comparison runs six pairs of fresh STA processes in alternating orders, 1,536 icon calls
total. Every eight-pin checksum is identical; all calls select the same handler source. Each process runs sixteen
batches, with the first separated from its fifteen repeated batches. Every repeated pair improves, including the
three candidate-first pairs. Medians across the ninety repeated batches per arm are:

| Eight-icon wall-clock phase, ms | Control | Candidate |
| --- | --- | --- |
| Per-call extraction total | 68.390 | 42.999 |
| Parsing, including shared parent setup | 28.181 | 4.307 |
| Parent/handler binding | 8.816 | 8.550 |
| Icon location | 2.072 | 2.112 |
| Handler extraction | 26.217 | 25.117 |
| Full WIC decode/crop | 2.486 | 2.347 |
| First batch, six fresh-process median | 151.663 | 130.825 |

Per-call totals exclude hash work, final BGRA destruction and shared-folder batch teardown; the full hardware
visibility callback includes ordinary refresh work. The earlier unmatched final-candidate cohort varies under
live OS/shell load, with a 23.782-second cold outlier. It is retained, not used to establish the final speedup.

Every final XENEON hardware arm has eighteen complete stages, 402 callbacks, complete first-preparation terminals
and zero dropped records. Build 282, OS/CPU/AC power, 2560x720/96-DPI geometry, initial/final portable scenes,
collector, pin contents, fonts, settings/schema and pinned DxUi closure match. Only Launcher DLL/PDB differ among
runtime files. The second pair starts the candidate first.

| Control / candidate metric | First order | Reverse order |
| --- | --- | --- |
| Launcher visibility median, ms | 120.679 / 66.567 | 104.789 / 62.905 |
| Launcher visibility maximum, ms | 228.315 / 166.992 | 179.231 / 157.452 |
| Quantized visibility CPU median, ms | 93.750 / 70.313 | 93.750 / 62.500 |
| Whole Gallery stage median, ms | 142.305 / 87.594 | 126.551 / 83.260 |
| Returned Matrix CPU, percent of one core | 2.475 / 2.474 | 2.228 / 2.526 |
| Returned submissions per second | 60.056 / 60.058 | 59.816 / 60.118 |
| Returned private / working set, MiB | 64.54 / 87.37 → 63.57 / 87.57 | 62.63 / 86.52 → 60.48 / 85.59 |
| Cold private, MiB | 37.00 / 35.43 | 36.46 / 35.30 |
| Returned adapter-local usage, MiB | 32.26 / 32.26 | 32.26 / 32.26 |
| Hidden/minimized submissions per second | 0 / 0 in both arms | 0 / 0 in both arms |

The visibility median improves in both observations, supporting the selected parsing cut. Live data, shell and
driver/cache histories remain uncontrolled; the CPU/memory variation does not establish a fixed percentage or
causal steady CPU/return-memory saving. Candidate visibility still reaches 166.992 ms, far beyond the proposed
4-ms construction slice. After the parsing cut, measured handler extraction dominates repeated work, followed by
binding; first-batch parent/handler setup also remains expensive. Measure correct-first-icon reuse or a host-owned
construction lane against dynamic icon/location changes and two-page lifetime before selecting another change.
No retained icon cache or worker is accepted by this step.

Release's three focused and twelve remaining native scopes pass; final BuildProcess tooling on the completed
documents supplies the sixteenth obligation. Debug's full fifteen native scopes pass. ASan's fifteen native
obligations are accounted across the retained stopped run and eleven-scope resume, using default quarantine;
sanitized byte peaks stay diagnostic. ARM64 Release compiles with zero warnings/errors, and ten skills validate.
All profile EXE/DLL/PDB/font/provenance/deployed-settings and schema hashes remain unchanged after tests.
The 469-input native closure adds the bounded parent-path helper; native test inventory stays at 62. Final tooling
covers the completed 713-file source/prose closure. Durable requirements are in `Plugins_API.md` and
`Core_PerformanceAndResources.md`. This completes the measured folder-binding step; the overall plan remains active.

The first ASan full run stops at System Data's class-5 creation-time oracle after four native scopes pass.
An isolated unchanged-source repeat passes; its wrapper then fails from an exit-command quoting error, so it is
not used for obligation accounting. The initial assertion compares a saved process snapshot with handles opened
later by PID, without pinning process identity. Process turnover is possible, but the failed log lacks PID/time
details and the cause remains unassigned. The initial failure and wrapper error stay retained; the resumed
eleven-scope run includes System Data again and passes, including the new Launcher scenarios and production-host
soaks/smoke/crash checks. No assertion or production System Data code is weakened.

Receipts: `p43-shell-phases-paired-attribution{,-summary}.json`,
`p43-shell-phases-qualified-{candidate-inputs,pixel-comparison,matched-comparison,matched-outcome}.json`,
their reverse-order counterparts, `p43-shell-phases-qualified-{scope-accounting,closeout-inputs,closeout}.json`,
the sealed profile artifact manifests and `runtime-2026-10-12-p43-shell-phases-qualified-{control,candidate}`.
Initial SDK/enum/PIDL compiler failures, the aborted capture, strict raw-hash failures, missing diagnostic-loader
dependency attempts and superseded candidate captures remain retained. Only the final candidate's accepted
measurements support the selected implementation.

### P4.3 Launcher handler extraction (2026-10-12, implementation qualified)

The qualified `313a49c` Release bundle and its 713-file source closure are sealed before edits, at build 282.
Fresh STA forwarding diagnostics attribute substantial handler time to UAC shield evaluation: asking for
`GIL_CHECKSHIELD` moves cost from Extract to GetIconLocation without reducing total work. This is API-controlled
phase attribution, not an internal CPU-stack resolution. Default extraction with opaque indices refuses six
handlers, jumbo-list pixels differ for all eight, and raw shortcut icon overrides cannot generically bypass the
handler contract. Those alternatives are rejected.

The selected change retains up to four full-quality 256-pixel BGRA images process-wide, at most 1 MiB plus less than
16 KiB of fixed bookkeeping. Only filename-backed, explicitly cacheable handlers with a fresh known no-shield
result are eligible; the six opaque handlers keep the original path. Each refresh binds a new handler and checks
location, signed identifier, flags, exact target spelling, expanded resource filename and both files' identity,
creation/write/change times, attributes and size. Failed probes or changing inputs invalidate eligibility. No
COM, GDI or GPU owner is retained, and process shutdown frees all bytes. Cache allocation is best effort and
cannot replace successful ordinary extraction with failure. Decoding, cropping, filtering and shaders are unchanged.

The shipped-DLL tests cover exact cold/hit pixels and source edges, fresh location/shield checks, cache/opaque/
overlay/unknown refusal, changed identifiers/resources/instances, changed target/resource content with restored
write time, file replacement, four-entry eviction and 32-target pressure, clone/store allocation failure,
borrowed-extractor and partial/null/failing HICON ownership, GDI/USER stability and shutdown release. Cache hits
retain their own normal BGRA buffer, so existing widget/device-loss lifetime remains unchanged. The intentional
inactive retention is accepted for the measured eligible-icon savings below: the shipped scene retains 512 KiB,
while the maximum 1-MiB cache stays independent of the 32-entry GPU atlas. No cache work occurs in Render or clean
preparation. The 4-ms construction and first-pixel budgets remain open.

Release builds without warnings/errors, and the three focused scopes pass. Six alternating fresh-STA pairs retain
1,536 calls, matching all eight pin hashes and source edges (JDownloader's cropped edge remains 249 pixels).
There are exactly 180 cache hits: Explorer and Edge in each of the fifteen repeated batches of six processes.
Their per-icon repeated medians improve from 4.565 to 2.122 ms and 8.804 to 6.800 ms. Whole eight-icon medians
are 76.125 versus 77.357 ms, with three pairs improving and three failing to improve. First batches are
239.189 versus 248.510 ms. These noisy totals do not establish an aggregate extraction speedup. New phases expose
fresh shield checks (3.949 ms per repeated batch), file validation (0.716 ms) and BGRA cloning (0.131 ms). The combined
eligible-icon repeated median improves in every pair, including every candidate-first pair: 9.882 → 7.948,
15.474 → 10.221, 13.847 → 9.358, 13.992 → 9.178, 11.518 → 7.521 and 12.477 → 6.563 ms. These 1.9–5.9-ms cuts
support the selected reuse; they do not establish a universal whole-batch percentage.

Both matched Release orders have eighteen complete stages, 402 callbacks, complete preparation terminals and zero
dropped records. Build 282, portable four-page scenes with eight frozen authored pins, collector, OS/CPU/AC power,
2560×720/96-DPI geometry, fonts, settings/schema and pinned DxUi closure match. Among production runtime files,
only Launcher DLL/PDB differ; the test executables are independently sealed for each private diagnostic ABI.

| Control / candidate metric | First order | Reverse order |
| --- | --- | --- |
| Launcher visibility median, ms | 80.900 / 66.297 | 57.118 / 56.234 |
| Launcher visibility maximum, ms | 164.376 / 258.944 | 154.028 / 176.672 |
| Quantized visibility CPU median, ms | 78.125 / 62.500 | 62.500 / 54.688 |
| Whole Gallery stage median, ms | 103.731 / 89.085 | 78.769 / 80.830 |
| Returned Matrix CPU, percent of one core | 2.922 / 2.672 | 2.277 / 2.145 |
| Returned submissions per second | 59.958 / 59.915 | 59.914 / 59.804 |
| Returned private / working set, MiB | 64.04 / 86.85 → 64.34 / 87.89 | 63.52 / 86.99 → 63.28 / 86.84 |
| Cold private, MiB | 36.15 / 35.46 | 35.68 / 35.31 |
| Returned adapter-local usage, MiB | 32.26 / 32.26 | 32.26 / 32.26 |
| Hidden/minimized submissions per second | 0 / 0 in both arms | 0 / 0 in both arms |

Visibility medians improve in both observations, but maxima rise and the reverse whole-page median does not
improve. Live shell/driver/cache and OS load remain uncontrolled, and callback CPU is quantized. Cold Matrix CPU
already differs before Launcher is visited, so the observed returned CPU values do not establish a causal steady
CPU saving. GPU storage/submission policy is unchanged. Correct-first-icon and construction-lane work remains open.
Repeated handler extraction still dominates at 38.092 ms per eight icons, followed by binding at 16.060 ms;
opaque handlers and initial parent/handler setup are the next measured targets.

Quality checks preserve all eight pin hashes/source edges, the direct executable, glyph/warning slices, five large
frames and fifteen glyph/empty frames. The strict URI/stock slice comparisons still fail and are retained separately.
Raw readback limits those differences to one RGB level on 504 URI pixels and fourteen stock pixels, with exact
alpha; their cause remains unassigned. All five small-icon frames in a scene exposing every fallback are byte-identical
in this step. The WIC conversion/crop/scale source is unchanged. No source resolution, filtering, geometry or
alpha trade is selected.

An isolated image-factory diagnostic also rejects `SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK` through the current bitmap
decoder: all eight pin hashes differ, with raw RGB deltas 84–255 and unchanged alpha. Its cache-only/memory-only
variants return no image in all 128 calls per variant. This rejects those tested paths, not every possible future
image-factory/alpha implementation. No alternative replaces the original handler/fallback route.

The initial Release remainder stops at Desk Clock's warmed 4-MiB private-growth guard: peak growth is 6,455,296 bytes.
That fixture places only Desk Clock and never constructs Launcher. The unchanged guard passes on an isolated repeat;
that run uses a different working directory and is diagnostic only. The canonical-directory repeat passes with
1,167,360 bytes of peak growth and accounts for the exact unfinished guard. The failed run stays retained, cause
unassigned; no test threshold or Desk Clock/renderer code changes. Debug and ASan each pass all fifteen native scopes,
with default sanitizer quarantine; sanitized byte peaks remain diagnostic. ARM64 Release compiles without warnings/errors,
and ten skills validate. Release's three focused scopes, eleven complete remainder scopes and the exact final
HostPlugin guard account for all fifteen native obligations. The completed-document BuildProcess run is the
sixteenth Release obligation; `p43-handler-closeout.json` MUST record it and unchanged final source/runtime seals
before commit. The 469-input native closure and 62-file native test inventory remain unchanged in membership.
Durable behavior is persisted in `Plugins_API.md` and `Core_PerformanceAndResources.md`; this implementation
step does not complete the overall active plan.

Additional owned-shortcut diagnostics compare ordinary and shield-aware extraction without launching a tool or
showing UAC. An admin-flag explicit Explorer icon is unchanged. Regedit returns `GIL_SHIELD | GIL_NOTFILENAME` when
queried for shield state; its full-quality checksum and 224-pixel source edge remain identical through the final
production batch versus control. That batch performs no shield/cache-read phase because the original location is
already opaque. Synthetic shipped-DLL tests cover filename handlers returning shield/refusal/unknown state.

Receipts: `p43-handler-{control,candidate-inputs,candidate-files,paired-attribution,paired-attribution-summary}.json`,
`p43-handler-eligible-pairs.json`, `p43-handler-{strict-pixel-comparison,pixel-comparison}.json`,
`p43-handler-matched-{comparison,outcome}{,-reverse}.json`, the four
`runtime-2026-10-12-p43-handler-{control,candidate}{,-reverse}` captures, factory diagnostic CSV/raw summaries,
and `p43-handler-release-desk-clock-canonical-repeat.json`, sealed profile artifact manifests,
`p43-handler-{scope-accounting,closeout-inputs,closeout,commit}.json` and the owned admin/Regedit diagnostics.
Initial sandbox CIM refusal, diagnostic include/path
errors and the stopped inefficient analysis helper are retained as execution failures, not performance evidence.

### P4.3 verified-file opaque handlers (2026-10-12, complete)

The clean qualified `5a2e6c3` Release control, build 282, is verified against its 713-file source closure and
144 runtime files before edits. The original handler/WIC/crop/scale/shader pipeline is retained.

The image-factory alternative is rejected after testing a straight-alpha decoder. It reduces the prior 84–255
RGB deltas to one level on seven pins and two on the cropped JDownloader icon, with exact alpha; pin hashes still
differ. Four alternating fresh-STA pairs compare the current folder-batched/cache control against an image-factory
batch made with `SHCreateItemWithParent`. Repeated medians improve, 52.44 → 30.31, 51.93 → 36.49, 52.41 → 35.19
and 53.91 → 35.30 ms, but all first batches worsen: 206 → 1,802, 290 → 884, 193 → 326 and 183 → 523 ms.
Owned admin/Regedit links retain alpha with a one-level RGB difference. These diagnostics do not qualify a
production replacement, and no decoder/alpha change lands.

Direct parent parsing/binding alternatives also keep every pin checksum/source edge but establish no consistent
total saving. `SHParseDisplayName` plus `SHBindToObject` improves two of four first/repeated pairs and fails to
improve two. Direct desktop-folder parsing reduces its first parsing phase from about 39–43 to 15–16 ms, but
moves cost to binding: every first batch worsens, 145.56 → 162.76, 144.04 → 149.85, 152.24 → 206.31 and
132.95 → 160.31 ms. Neither phase movement nor an isolated faster phase is accepted as total latency reduction.

The selected change follows the documented Shell cache contract: `GIL_NOTFILENAME` forbids treating a token/index
as a generic file-icon pair; `GIL_DONTCACHE` forbids caching its pixels. Cacheable opaque tokens are admitted only
when their expanded spelling also names a verified ordinary file. Their unmodified token, signed identifier and
flags remain exact identity, in addition to the target, expanded file spelling and both files' volume/file identity,
creation/write/change times, size and attributes. A fresh native handler, ordinary location and known no-shield
check still run on every refresh. Token-only/empty/namespace/directory/missing locations, refused shield checks,
shield/overlay/unknown/no-cache flags and changing files retain ordinary extraction. The cache never interprets
an opaque index or substitutes generic extraction. No COM, GDI, GPU, thread or timer owner is retained.

Capacity rises from four to eight, at most 2 MiB of decoded pixels and less than 24 KiB of fixed bookkeeping,
to accommodate the eight eligible shipped pins without replacement on every refresh. This intentional inactive
retention rises from 512 KiB in the preceding scene to 2 MiB; the process observations are recorded below.
Every record still owns its normal BGRA buffer. Best-effort clone/store failures retain valid ordinary pixels,
post-copy/post-decode file checks withdraw unstable entries, and shutdown releases every cache buffer.

Both filename and opaque shipped-DLL scenarios cover fresh shield checks, exact cold/hit pixels, negative signed
identifiers, distinct literal tokens expanding to the same file, unsupported/token-only paths, no-cache/unknown/
overlay/shield refusal, changed identifiers/resources/instances, same-size file writes with restored write time,
file replacement, eight-entry eviction under 32-target pressure, clone/store OOM, partial/null/failing handles,
GDI/USER stability and shutdown. A preserved prototype with eight entries but no exact location-token key fails
the new shipped-DLL scenarios with `0x8007000D`, while the selected candidate passes. This negative control is
expected regression detection, not a native qualification pass. The native test inventory remains 62 files;
no public ABI or rendering path changes.

Release builds without warnings/errors and the three focused scopes pass. Six final alternating fresh-STA pairs
retain 1,536 calls and every pin hash/source edge (JDownloader stays 249 pixels). Control has 180 cache hits and
candidate 720. Combined repeated median is 52.540 → 39.689 ms; each paired repeated median improves:
56.079 → 42.552, 54.351 → 45.067, 49.373 → 38.350, 49.556 → 40.232, 48.600 → 37.666 and 50.697 → 36.589 ms.
First-batch medians are 179.330 → 193.535 ms, with mixed pair outcomes, so no cold saving is claimed.
Repeated HICON extraction/decode falls from 27.138/2.173 ms to zero. Fresh shield checks remain at 19.194 ms per
eight icons, binding 8.080, parsing 5.177, file validation 3.074, ordinary location 2.992 and cloning 0.337.
Fresh shield work and cold parent/handler setup are the next measured costs. Construction/first-pixel budgets
remain open; a host-owned bounded background route needs separate lifetime, correctness and latency qualification.

Five large frames, fifteen glyph/empty frames, all pins, the direct executable and glyph/warning slices match the
canonical control. The independent canonical URI/stock slice hashes still differ; that exact failure remains
retained separately. Both raw-readback scenes have all thirteen slices and five frames byte-identical, including
the small scene exposing every fallback. This repeat does not assign the earlier hash mismatch's cause or turn
the failed exact comparison into a pass. Decoder, source resolution, alpha, geometry and filtering are unchanged.
An owned Regedit shortcut supplies sixteen calls per arm, with identical `C792760AEE10DED2` pixels and 224-pixel
source edges. All sixteen candidate calls perform a fresh shield check, return native handler pixels and perform
zero cache reads; nothing is launched and no UAC prompt is shown.

The final hardware comparison uses both orders against the preserved qualified control, with identical build 282,
CPU/OS/power, 2560×720/96-DPI canvas, settings/schema, fonts, DxUi provenance, collector, pin links and eight icon-file
identities before and after every arm. Only the production Launcher DLL/PDB differs; test executables are sealed
separately. Every arm has eighteen complete stages and 402 callbacks, with no dropped callbacks or missing
preparation terminals. The four-page sequence is Matrix Focus, Plugin Gallery, System and Shaders, followed by
returned, hidden, restored and minimized phases. The owned window never takes foreground or injects input.

| Observation, control → candidate | Control first | Candidate first |
| --- | --- | --- |
| Launcher visibility median / maximum, ms | 61.126 / 236.520 → 69.481 / 226.927 | 61.047 / 199.887 → 54.011 / 263.410 |
| Launcher callback CPU median, ms (quantized) | 62.500 → 62.500 | 46.875 → 46.875 |
| Gallery stage median, ms | 83.784 → 91.130 | 83.195 → 80.752 |
| Cold / returned CPU, percent of one core | 2.048 / 2.596 → 2.315 / 2.675 | 2.462 / 2.241 → 2.169 / 2.005 |
| Returned submissions/s | 59.869 → 60.021 | 59.665 → 59.675 |
| Cold / returned private, MiB | 35.43 / 61.54 → 34.57 / 63.42 | 35.96 / 62.98 → 34.73 / 62.20 |
| Returned working set, MiB | 86.57 → 88.02 | 86.88 → 87.61 |
| Returned adapter-local usage, MiB | 32.26 → 32.26 | 32.26 → 32.26 |
| Hidden / minimized submissions/s | 0 / 0 → 0 / 0 | 0 / 0 → 0 / 0 |

Visibility, page totals, tails and process CPU remain order-dependent; the cold page also differs before Launcher
is constructed. No causal page-activation, cold-start, whole-process memory or steady CPU saving is established.
Returned private differences are +1.88 and -0.78 MiB, while working set rises in both orders; these observations
do not erase the known 1.5-MiB decoded-pixel increase. The bounded retention is accepted for the consistent native
repeat-work reduction with original full-quality pixels. Submission rate, local GPU usage and inactive scheduling
stay intact. This is not physical-input/first-changed-pixel qualification, and it does not close construction budgets.

Release's three focused scopes and twelve remainder obligations account for all fifteen native scopes. The initial
remainder stops at Desk Clock's warmed 4-MiB private-growth guard: peak growth is 5,668,864 bytes, final growth
4,837,376. That fixture places only Desk Clock and never constructs Launcher. Both the guard executable and actual
Desk Clock DLL are byte-identical to the control. The exact canonical-directory/argument repeat passes with
667,648 bytes of peak growth; the initial failure stays retained, cause unassigned. No test threshold, Desk Clock
or renderer code is weakened. Debug and ASan Debug each pass all fifteen native scopes, with default sanitizer
quarantine; sanitized byte peaks are diagnostic. ARM64 Release compiles without warnings/errors, and ten skills
validate. Release's final-document BuildProcess run is the sixteenth obligation: `p43-opaque-closeout.json` MUST
record its success and unchanged final source/runtime seals before commit. The 469-input native closure,
713-file full source/prose closure, complete runtime artifacts and deployed settings remain sealed.
The two normative contracts persist this behavior. This internal optimization needs no user-guide change and
does not finish the active plan.

Receipts: `p43-opaque-{control,candidate-inputs,candidate-files,paired-attribution,paired-attribution-summary}.json`,
six `p43-opaque-paired-{1..6}-{control,candidate}.csv` pairs,
`p43-opaque-{strict-pixel-comparison,pixel-comparison,regedit-comparison,token-negative}.json`,
`p43-opaque-matched-{comparison,outcome}{,-reverse}.json`, `p43-opaque-hardware-summary.json`, the four
`runtime-2026-10-12-p43-opaque-{control,candidate}{,-reverse}` captures, preserved initial/canonical-repeat guard
logs, `p43-opaque-release-desk-clock-canonical-repeat.json`, sealed profile artifact manifests and
`p43-opaque-{scope-accounting,closeout-inputs,closeout,commit}.json`.
Rejected alternatives use `p43-alpha-*`, `p43-direct-folder-*`, `p43-desktop-folder-*` and `p43-opaque-cache-*`.
The initial sandbox MSVC database-service failure, corrected diagnostic-generation assertion and first negative
control wrapper's wrong HRESULT expectation are retained execution failures, not measurement or qualification evidence.

### P4.3 Launcher cold parent creation (2026-10-12, attribution complete; parsing alternatives rejected)

The clean qualified `e55b3bf` control is frozen before edits: 713 source files, 144 Release runtime files,
eight real taskbar links and eight external icon resources. Build 282, settings/fonts/PDB/DxUi closure and
the nonactivating collector are preserved. Cold parent creation is the expensive `SHCreateItemFromParsingName`
boundary; parent-folder binding remains negligible. Earlier known-folder/direct-folder/desktop-folder routes
stay rejected. No production parsing optimization is accepted by this investigation.

A split diagnostic uses `SHParseDisplayName` plus `SHCreateItemFromIDList` while retaining native folder/handler
binding. Four alternating pairs (1,024 calls) preserve all pin hashes and source edges. Path-to-PIDL parsing
takes almost all the parent interval; creating an item from the PIDL takes 5.5–6.3 microseconds cold. Parent
medians move 48.7643 → 46.2049 ms, but the fourth cold pair and repeated directions are mixed. Decomposing the
API is useful attribution, not proof that replacing it improves application latency.

The metadata alternative supplies fresh `FindFirstFileW` data through `IFileSystemBindData`/`IBindCtx` to the same
creation API, retaining native parent/child binding and all icon/shield/decoder behavior. Only literal ordinary
directories qualify; roots, patterns, dot segments, reparse points and failed/unsupported data use ordinary parsing.
Allocation/context/registration/partial/null failure releases owners before retry. The initial four-pair cohort
improves repeated totals 34.6682 → 30.9146 ms. Review adds conservative literal-path guards, freezes a new source,
and recollects evidence instead of reusing changed-source results.

Six final metadata pairs contain 1,536 calls and 720 validated decoded hits per arm, with exact pin hashes/edges.
Repeated median is 36.6931 → 33.3687 ms, five of six paired medians improving. Cold total is 158.4965 → 152.1512 ms,
but three cold pairs worsen and three improve. Parent creation falls 48.9046 → 14.8047 ms (fresh data/context
0.0936/0.0077 ms); extractor binding rises 42.5065 → 68.6539 ms. Cost moves between phases. Live Shell/OS/cache
histories remain uncontrolled and all tails stay in the receipt.

Both matched metadata hardware orders have 18 complete stages, 402 callbacks and zero drops per arm. Source,
runtime dependencies, portable scenes, collector, 2560×720/96-DPI XENEON geometry, OS/CPU/power and pin/resource
inputs match; only Launcher DLL/PDB differ. Hidden/minimized submission rates are zero.

| Metadata control → candidate | Control-first order | Candidate-first order |
| --- | --- | --- |
| Launcher visibility median / max, ms | 46.7185 / 167.090 → 71.1720 / 204.296 | 45.5880 / 166.724 → 58.0845 / 186.835 |
| Whole Gallery stage median, ms | 69.7760 → 97.9275 | 67.2055 → 84.6535 |
| Returned CPU, percent of one core | 3.071 → 5.426 | 2.353 → 3.049 |
| Returned submissions per second | 62.050 → 96.126 | 59.832 → 83.169 |
| Returned private / working set, MiB | 63.24 / 88.33 → 63.77 / 88.01 | 65.28 / 89.20 → 68.00 / 90.24 |
| Cold CPU / private MiB | 2.217 / 35.04 → 2.608 / 35.11 | 2.534 / 35.33 → 1.902 / 36.87 |
| Returned adapter-local MiB | 32.26 → 32.26 | 32.26 → 32.26 |

The repeated native reduction does not justify worse application visibility/Gallery medians in both orders.
CPU/submission/retention observations also worsen on returns. Their cause is unassigned; changed submission
counts are not hardware FPS or proof of a causal CPU change. The metadata route is **rejected for production**.
Its matching-context receipts remain accepted as valid observations, not as deployment acceptance.

A final documented `STR_PARSE_AND_CREATE_ITEM` prototype uses a call-local SDK `IParseAndCreateItem` sink without
supplying filesystem metadata. Every sink SetItem/GetItem count is zero in four alternating pairs (1,024 calls).
Pins stay exact, but three repeated pair medians worsen; first totals improve in three pairs and worsen in one,
while initialization again moves to binding. An unused hook and phase movement establish no reliable saving.
This route is rejected too. No callback/lifetime/cancellation or visual-policy shortcut is selected.

Ordinary production parsing is restored. The final private 312-byte record retains the original fifteen phases
and adds diagnostic metadata/context phases. Clear completion bits mean unavailable, not zero work; these two
phases are unavailable in every normal batch. Automated-only `RedXeLauncherTestShellParent` measures the rejected
metadata variant and call-local failure cases through native extraction; the host never calls it or selects its
route. No override, metadata/context owner, payload, worker, timer, GPU resource or per-widget field is retained.
The bounded diagnostic variant uses one metadata object below one KiB plus a temporary system context; all
references end with the call/refresh. Normal batches add at most eight bytes of construction-stack bookkeeping,
and ordinary render/clean preparation still allocate nothing and query no attribution clocks.

The diagnostic DLL grows 5,632 on-disk bytes and 8,192 bytes of PE image address span versus the control;
these are not measured residency or whole-process commitment.
That bounded image cost is accepted for native failure/ownership attribution and a normal-route regression guard,
not for an unproved latency saving. The final ordinary source/runtime closure is frozen anew and qualified below.

Current-DLL tests cover Unicode/exact-spelling parents, recreation, real junctions without elevation,
root/pattern/dot fallback, metadata/OOM/context/registration/partial/null refusals, unsupported data, authorization/
size/target output rules and handle/GDI/USER stability. Normal batches explicitly reject diagnostic phase activity.
An isolated current-ABI negative DLL accidentally enables metadata for normal batches; `ValidateShellBatch`
rejects it with `0x8007000D`. Both negative receipts retain `qualification=false`. Existing cache/shield/metadata,
32-target, import/drop/launch, device-loss and WARP coverage remains.

Six final ordinary-path alternating pairs contain 1,536 calls and 720 decoded hits per arm, with exact pin
hashes/source edges. Production metadata/context completion bits remain clear. Repeated total medians are
37.4749 → 37.9695 ms; cold medians are
172.1725 → 155.1125 ms. These are fresh baseline observations of the
same ordinary algorithm, not a claimed improvement. Candidate cold parent creation is
45.5627 ms, extractor binding 40.6424 ms and fresh combined
shield/location 41.4198 ms. A 642.974-ms candidate cold outlier remains retained.
No tail is excluded to manufacture acceptance.

Final ordinary-path canonical five large frames, eleven nonfallback slices and fifteen glyph/empty frames
match exactly. The separate strict URI/stock slice hash comparison remains failed and retained. Independent raw
readbacks contain 26 slices and ten rendered frames with every channel exact (maximum delta zero), including
the fallbacks when displayed. The rejected metadata candidate's final strict/raw comparisons also match;
earlier strict failures remain retained with no assigned cause. The existing one-level fallback tolerance is
unchanged. Thirty-two owned admin/Regedit icons match source edge 224/hash `C792760AEE10DED2`, with fresh shields,
zero decoded-cache reads and no launch or UAC. Exact hashes and numerical quality are separate receipts.

Two fresh ordinary-path hardware orders preserve the same Build-282 runtime/settings/fonts/PDB/DxUi,
collector, pin/resource identities and 2560×720/96-DPI XENEON scene; only Launcher DLL/PDB differ.
Each arm has 18 complete stages, 402 callbacks and zero drops. Hidden/minimized submissions are zero.

| Ordinary control → candidate | Control-first order | Candidate-first order |
| --- | --- | --- |
| Launcher visibility median / max, ms | 68.1920 / 176.1370 → 65.9970 / 1083.7020 | 77.3375 / 202.9560 → 61.9715 / 1313.2700 |
| Launcher callback CPU median, ms | 62.5000 → 62.5000 | 54.6875 → 46.8750 |
| Whole Gallery stage median, ms | 92.1820 → 120.2875 | 120.2335 → 105.0225 |
| Cold CPU, percent of one core | 2.216 → 2.875 | 2.559 → 3.271 |
| Returned CPU, percent of one core | 1.806 → 3.290 | 2.922 → 4.614 |
| Returned submissions per second | 59.836 → 59.583 | 60.545 → 117.859 |
| Cold private, MiB | 36.70 → 35.45 | 36.44 → 35.54 |
| Returned private / working set, MiB | 65.16 / 88.30 → 65.07 / 88.93 | 65.23 / 88.61 → 63.75 / 88.27 |
| Returned adapter-local, MiB | 32.26 → 32.26 | 32.26 → 32.26 |

These are baseline observations of retained ordinary parsing, with diagnostic bookkeeping added. Live data,
Shell/driver cache histories and callback CPU quantization limit attribution; submissions are not hardware FPS.
Visibility medians are lower, but candidate visibility maxima exceed one second and cold/returned CPU rises in
both orders. Returned submissions nearly double in the reverse order. Their cause is unassigned; the unchanged
ordinary parsing algorithm does not justify dismissing these observations or attributing them to the diagnostic.
The rates and timings do not establish a production latency or whole-process resource saving. No outlier is
removed and no budget gate is accepted from them. The initial ordinary control capture stopped at the existing
`visit-3` evidence guard before collecting a candidate; its generic “Inactive probe presented or lacks evidence”
error has no assigned cause. That control-only attempt is rejected and retained. Both fresh orders use new roots,
the unchanged collector and unchanged evidence thresholds.

Final Release accounting includes Launcher/PluginContract/HostSmoke (three focused native scopes),
twelve remaining native scopes and final BuildProcess tooling: 16 obligations. Debug and ASan Debug each cover
all fifteen native scopes on the frozen final ordinary source. ARM64 Release compilation and ten skill validations
pass; ARM64 runtime execution is not claimed. Native inputs (469 files), deployed settings/fonts/PDB/runtime
closures and both preserved hardware arms remain byte-identical after their applicable tests. Final tooling
covers the completed prose/source closure (713 files), and the 62-file native inventory and formatting pass.
Current normal-route and reparse negative controls, diagnostic refusal tests and strict pixel-hash failure remain
separate evidence with their original outcomes. Physical-input, first-changed-pixel, total cold-construction and
whole-process budgets remain open. The WIP stays ACTIVE.

Next: a bounded host-owned construction mechanism that keeps the current page animated until full-quality
Launcher content is ready. Select it against native STA ownership, cancellation/replacement, two-page lifetime,
device loss and total input-to-first/settled presentation. Repeating API shortcuts or moving cost into Prepare
does not bound an indivisible Shell call. Cold-construction, first-pixel, process-resource and 4-ms/16.7-ms
budgets remain open; this is an attribution/test step, not completion of the runtime plan.

Evidence is local under `.build/receipts/`: `p43-parent-control{,-source-files}.json`,
`p43-parent-{split,filedata,parsecreate}-{diagnostic,pairs,summary}.json`, the pre-review `p43-parent-*` cohort,
rejected metadata `p43-parent-final-*` source/runtime/native/quality/hardware receipts and captures, and
`p43-parent-qualified-*` ordinary-path source/runtime/native/quality/hardware/profile/closeout/commit seals.
Strict hash failures and negative controls remain separate from numerical quality and successful qualification.
Diagnostic generator/include, log redirection and recovered documentation-encoding errors are execution failures,
not performance evidence. Original committed history is restored from the verified archive with explicit UTF-8.
The auxiliary ARM64 artifact seal initially assumed an x64 file count and then inherited a stale process exit
code. Its corrected required-product checks seal all 73 actual compile artifacts/settings/fonts/PDB files;
neither helper failure changes the successful native compilation or its inputs/outputs.

### P4.3 combined shield/location and cold setup attribution (2026-10-12, complete)

The clean qualified `ed15ff6` control is verified against its final 713-file source/prose closure and 144 runtime
files, and its sources are archived before edits. Build 282 and the same eight pin/icon-file identities are retained.

Four alternating fresh-STA cohorts reject two request-flag changes. `GIL_FORSHORTCUT` sends all pins through the
image-factory fallback and changes hashes/source edges; repeated medians rise from 37.50–74.33 to 78.67–88.65 ms.
`GIL_ASYNC` on the shield request retains pixels but returns pending for six pins. The shield phase falls to about
1 ms per batch, while repeated handler extraction returns at 26–28 ms and hits fall to thirty per process.
Repeated totals are 41.72 → 51.89, 39.65 → 48.96, 37.50 → 48.81 and 74.33 → 50.04 ms. The outlier control and
mixed cold totals stay recorded. Removing `GIL_FORSHELL` retains pixels but establishes no consistent first/repeated
saving in another four-cohort comparison; the original request context is retained.

The selected documented query also returns location/index/cache flags. Calling it directly preserves the original
native handler, decode/crop/alpha/filtering and eight-entry/2-MiB cache. A valid terminated `S_OK` response is used
without a redundant ordinary query. Failed/pending/refused/truncated requests reset every path/index/flag before
one ordinary fallback, with no cache reads or stores for that refresh. Malformed ordinary locations fail before
native extraction. Fresh explicit no-shield, exact opaque identity, before/after file checks and best-effort OOM
behavior remain mandatory. No worker, timer, retained owner, new payload or steady-frame work is added.

Exploratory combined-query pairs have exact eight-pin hashes/source edges and 120 hits per process. Repeated totals
are 35.30 → 35.76, 37.84 → 35.52, 38.45 → 36.31 and 41.62 → 34.44 ms; cold totals are 229.43 → 184.47,
170.61 → 188.07, 165.17 → 180.63 and 177.56 → 164.75 ms. These prototypes select a simpler one-query path but
do not yet qualify a production or cold-start saving. The new fifteen-phase, 280-byte private caller-owned record
separates parent-item creation, parent-folder binding and relative child parsing, retaining the twelve existing
phase indices. The phases are disjoint and clocks still run only for the automated call. Normal refreshes query no
attribution clocks; public ABI and native test inventory remain unchanged.

The final selected production candidate has six alternating fresh-STA pairs (1,536 calls), with all eight hashes,
source edges and 720 hits per arm unchanged. Repeated batch median falls from 40.7547 to 39.0762 ms (4.1%); four
of six paired medians improve. Cold median is 191.8477 → 200.1243 ms, with three improving and three worsening
pairs, so no cold-start gain is established. Candidate cold medians identify parent-item creation at 69.0867 ms,
the combined shield/location request at 59.5703 ms, extractor binding at 32.5071 ms, extraction at 18.4391 ms,
decode at 4.2344 ms and child parsing at 1.8011 ms. Parent-folder binding is only 0.0346 ms. Old setup aggregates
remain comparable, but the three new split phases are unavailable in the twelve-phase control, not zero.
The repeated combined shield/location request remains 21.3972 ms per eight-pin batch; removing the ordinary
request does not eliminate shield evaluation or make these callbacks fit the proposed four-ms construction slice.

A separate known-folder prototype verifies the actual `FOLDERID_UserPinned` path before constructing its TaskBar
child, then falls back to the original path on any refusal. Four alternating pairs preserve every pin hash/source
edge but reject the route: cold parent median is 49.3666 → 66.6670 ms, with three of four parent pairs worse;
total cold pairs are mixed and all four repeated medians worsen. No known-folder code or retained Shell owner
is selected. Normal code retains the original path walk and call-local folder ownership.

Shipped-DLL tests cover one fresh query on cold/hit paths, authoritative shield tokens and negative identifiers,
ordinary-query failure when that query is unrequested, six refusal/failure/pending HRESULTs with poisoned partial
outputs, clear ordinary fallback without caching, unsupported shield queries, unterminated shield/ordinary results,
and the existing flags/file-change/exact-token/pressure/OOM/ownership/shutdown scenarios. Mixed-parent and exact-case
tests verify the split phase counts and complete output/failure masks.

The new tests also reject a diagnostic that reintroduces the redundant ordinary query while retaining the current
fifteen-phase/280-byte private ABI: `ValidateIconCache` fails with `0x8007000D`, while the selected DLL passes.
Eight native pin images, five large frames, five all-visible frames and fifteen glyph/empty frames are exact.
The independent strict canonical URI/stock hash comparison remains failed, with its cause unassigned. Raw alpha
is exact in all 26 shell slices; two fallback slices in the large scene differ by one RGB level in 14 and 504
pixels, within the existing tolerance. The all-visible scene and the eleven nonfallback slices per scene are exact.
WIC conversion/crop/scale source is unchanged; no tolerance, quality setting or fallback order is weakened.

Both matched hardware orders accept equal collectors, initial/final scenes, 2560x720/96-DPI XENEON geometry,
power/display context, eight link/resource identities and the pinned DxUi/runtime closure. Build number is 282;
only production Launcher DLL/PDB bytes differ. Every arm has 18 complete stages and 402 callbacks.

| Hardware observation, control → candidate | Control first | Candidate first |
| --- | --- | --- |
| Launcher visibility median / maximum, ms | 84.3100 / 263.858 → 64.1755 / 182.849 | 65.6320 / 233.066 → 60.2160 / 168.496 |
| Gallery stage median, ms | 115.6420 → 97.4790 | 86.4325 → 84.9235 |
| Cold / returned CPU, % of one core | 2.486 / 3.143 → 2.731 / 2.599 | 2.633 / 2.599 → 2.339 / 2.004 |
| Returned submissions/s | 59.665 → 59.723 | 60.040 → 60.135 |
| Cold / returned private, MiB | 35.64 / 63.91 → 35.40 / 65.16 | 35.24 / 65.62 → 35.63 / 66.17 |
| Returned working set, MiB | 88.59 → 89.63 | 90.09 → 90.06 |
| Returned local GPU usage, MiB | 32.26 → 32.26 | 32.26 → 32.26 |
| Hidden / minimized submissions/s | 0 / 0 → 0 / 0 | 0 / 0 → 0 / 0 |

Visibility and Gallery medians improve in both observations. Returned CPU is lower in both, while cold CPU is mixed
and callback CPU is quantized/order-dependent. There is no new retained payload, but returned whole-process private
is 1.25/0.55 MiB higher; process-memory acceptance stays open. Live data, Shell/driver caches and OS work remain
uncontrolled. This establishes neither a causal steady CPU saving nor physical-input/first-changed-pixel budgets.

All 469 native inputs and complete deployed artifact/settings/font/PDB closures remain immutable through builds,
tests and captures. Release accounts three focused scopes plus eleven remainder scopes and the four canonical
HostPlugin commands. The initial remainder run fails Matrix rendering/resource-count checks after 513 frames;
the unchanged host repeat passes Matrix and Studio but fails the Desk private peak at 4,538,368 bytes against
4 MiB. The exact canonical Desk repeat passes at 835,584 bytes. Those failed aggregate runs remain failed; the
successful command components, exact arguments/cwd and unchanged source/runtime identities are recorded separately.
Both causes remain unassigned, alongside earlier intermittent growth. Debug and ASan each pass all fifteen native
scopes; ASan's resource measurements are diagnostic under instrumentation. ARM64 Release compiles with zero
warnings/errors and ten skills validate. Release's sixteenth obligation is final BuildProcess tooling over the
finished 713-file source/prose closure; its final seal is recorded by `p43-shield-closeout.json` before commit.

The next measured costs are cold parent-item creation and the fresh combined shield/location request, not the
0.0346-ms parent-folder bind. Shortcut/async/plain request flags and the known-folder route remain rejected.
Selecting a host-owned construction lane requires current callback/first-present measurements, cancellation,
two-page ownership, partial initialization and full-quality pixel qualification. Merely moving this work into
`Prepare` cannot bound an indivisible callback to four ms. The proposed four-ms construction and 16.7-ms frame
budgets stay open; this completed step does not finish the active runtime plan.

Receipts use `p43-shield-{control,candidate-inputs,candidate-files,paired-attribution,paired-attribution-summary}.json`,
the six final native CSV pairs, `p43-shield-known-folder-{diagnostic,pairs,summary}.json`,
`p43-shield-{query-regression,regedit-comparison,pixel-strict-comparison,pixel-comparison}.json`,
`p43-shield-matched-{comparison,outcome}{,-reverse}.json`, `p43-shield-hardware-summary.json`, four
`runtime-2026-10-12-p43-shield-{control,candidate}{,-reverse}` captures, retained initial Matrix/Desk logs,
`p43-shield-release-{host-components,desk-clock-canonical-repeat}.json`, profile manifests, and
`p43-shield-{scope-accounting,closeout-inputs,closeout,commit}.json`. The original diagnostic output-path quoting
failure and known-folder generator escape warnings are retained execution failures, never measurement evidence.

### P0.2 raised-widget observation (2026-10-11, complete)

The optional production probe now exposes bit 8 from the host's actual active raised layer. It stays set through
raise/dismiss motion and clears at completed dismissal. A sample with motion clear can therefore verify a settled
raise before a controller sends Escape. The existing eight state bits, v1 record, arrays and 1,024-byte logger slot
remain unchanged. The disabled path still returns before any state query, sampling or logging; this adds no owned
storage, allocation, timer, thread or wake.

The hidden production application self-test raises the first eligible real widget on each page, settles and renders
it, checks the bit, dismisses immediately and checks it clear. Gallery's Triangle and System's viewer exercise the
real paths; full-canvas pages skip unsupported raises. Structured serialization checks state 262 in addition to the
existing extreme-width test. Release's two focused scopes plus fourteen remaining obligations pass; Debug and ASan
Debug each pass fifteen native scopes. All builds have zero warnings/errors, ARM64 Release compiles, and ten skills
validate. The 384 native inputs and profile artifact identities remain unchanged after testing; deployed settings
and schema match the frozen build sources. Receipts: `p02-raised-state-scope-accounting.json`,
`p02-raised-state-inputs.json`, `p02-raised-state-*-artifacts.json`, `p02-raised-state-runtime-settings.json` and
`p02-raised-state-*.log`. ASan byte peaks remain diagnostic quarantine observations, not hardware memory budgets.
The next foreground capture must verify all page and raised outcomes; this probe does not measure first changed
pixels, physical touch or sensor-to-photon latency.

### P0.2 verified synthetic input and adapter/DPI evidence (2026-10-11)

The newly agreed three-minute capture completes all twelve named wheel, System/Gallery raise/dismiss and blocked-edge
actions. Every injection is guarded by foreground PID and pointer-window ownership; page and settled raised state
are observed before continuing. App and PresentMon exit zero. Source, EXE/DLL/PDB/font/provenance and deployed
settings/schema identities match before and after teardown. The scene uses the Release page layout with services
and tray disabled; normal Launcher/location persistence changes its authored document and is retained, so this is
not a fixed-scene causal comparison. PresentMon records 4,622 owned frames, predominantly Hardware Composed:
Independent Flip with a short Composed: Flip interval, and no reported hybrid presents. All input-to-photon columns
are unavailable; default input tracking is enabled in this PresentMon version. The next-submission delays observed
for later Gallery wheels are about 98.82/114.81/115.25 ms, consistent with the separately measured staging stall,
but neither those timestamps nor displayed-frame counts prove the first changed pixels. Double-click timestamps
include the controller's 80-ms gap between clicks. Reversal/canceled physical swipe, touch and sensor-to-photon
qualification remain open. Receipts: `runtime-2026-10-11-p0-focused-verified-state`, its `summary.json`, raw CSV,
event/outcome/scene records and both identity lists.

An owned, non-activating Debug triangle scene additionally moves while hidden from XENEON to the primary display and
back. The Per-Monitor-V2 probe records DPI 96/144/96 and physical clients 2560x720/3840x1080/2560x720. Exactly three
device logs identify AMD Radeon / NVIDIA RTX 5080 / AMD Radeon with ownership of the current monitor. No error logs
occur, exit is zero, and the application-generated returned WGC image shows the rendered triangle. GPU Process
Memory shared/dedicated usage is zero on the old adapter after each move; its Total Committed counter still reports
4,096 bytes on AMD at the primary and 262,144 bytes on NVIDIA after returning. Historical engine instances are
retained as raw/cooked counters; one cumulative sample does not establish current engine activity. This verifies
programmatic device/DPI recovery and excludes a resident old-adapter surface in those counters. It does not pass
literal single-LUID committed accounting or visible physical dragging/presentation. Source and Debug runtime inputs
remain unchanged. Receipts: `runtime-2026-10-11-p0-adapter-move-owned`, including counters and `returned.png`.

### P3.6 allocation attribution and corrected AV lifetime premise (2026-10-11)

One separately approved administrator controller records a normal-user app copied under a fresh unique executable
name. Its temporary IFEO heap-tracing entry is removed afterward; its named recorder is stopped without changing
other sessions. The 140.187-second heap/virtual-allocation ETL has zero lost events/buffers, allocation/reallocation
stacks, matching private application symbols and Microsoft symbols. The app visits all four Release pages, returns,
hides/restores/minimizes, then stays alive until recording stops. The unchanged source/runtime identities, settings,
phase times, memory maps, ETL hash and cleanup result are retained in `runtime-2026-10-11-p36-heap`.

Cumulative phase cuts use xperf's [microsecond time ranges](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/heap).
Reported outstanding payload is 29,139.3 KiB cold and 47,112.9 KiB returned (about +17.55 MiB). Among the top hundred
stack groups, the first non-allocator AMD module contributes 27,570.4/39,959.7 KiB (about +12.10 MiB); those private
driver functions remain unresolved. Direct application caller groups do not explain the whole residual. AVControl
coordinator/preferences construction stacks also remain outstanding after the production probe reports zero AV
widgets/surfaces. Inspection of the preserved control confirms `Provider::~Provider` only decrements its count: the module-global coordinator
is released by `RedXePluginShutdown`, contrary to this plan's earlier last-provider claim. Its safe last-provider
release, including failed construction, survivors, retained widgets and revisit, is implemented in the following step.

Heap tracing perturbs resource use: process private is 49.24/82.77 MiB here, so these values are not the uninstrumented
baseline or a saving. xperf warns about decommitting more heap extents than committed; its extent totals conflict
with process commitment and are rejected. Virtual allocation event totals overlap and do not equal process private
bytes. Top-hundred aggregation truncates smaller stacks, and rounded KiB/counts do not certify exact object sizes or
leaks. The initial phase-analysis command used a fractional seconds argument and failed; its log is preserved, and
the corrected `analysis-cuts-v2` uses integer microseconds against the same immutable ETL. No recorder repeat was
needed. These are ownership clues, not permission to unload modules or force device rebuilds on visible pages.
Microsoft's [Trim contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgidevice3-trim)
places driver trimming at extended idle and warns of first-render reallocation cost; visible Matrix remains
continuous, so trimming on every page settle is not selected. Whole-process cold-return +4-MiB acceptance remains
open. Reports: `allocation-attribution.json`, `analysis`, `analysis-cuts-v2`, and `probe` in that receipt root.
The separately retained `allocation-attribution-v3.json` skips CRT wrapped allocator calls and LocalAlloc/GlobalAlloc
wrappers. Returned top-hundred caller groups include 2,542.8 KiB in PDH, 1,407.9 KiB in advapi32/perflib and
510.7 KiB in DirectWrite; AMD and AV group totals are unchanged. These resolved library stacks are diagnostic
ownership clues, not proof that a live RedXe source, query or text object leaked.

### P3.6 last-provider AV coordinator cache release (2026-10-11)

The shared coordinator and graphics weak cache now release when the last provider goes away. Widgets retain their
provider; independent providers and staged/surviving widgets keep the existing model. A failed provider allocation
also drops a newly unused cache. Host-owned queued/in-flight control work keeps its retiring coordinator until
synchronized completion, and its callback wait drains at destruction. A revisit constructs fresh state for its
host even while old work drains. The existing process-lifetime image pin still protects disconnected UIA calls.
No render, surface-size, shader, sampling, widget-setting or steady-state scheduling policy changes.

Shipped-DLL tests cover independent and widget-held owners, invalid/foreign-host factories, cold and live provider
allocation faults with retries, prepared teardown, queued work across retirement/revisit and disconnected UIA.
The one-shot allocation fault is an explicit UI-thread synthetic-only export; normal backend selection rejects it.
It exercises the actual published-coordinator rollback branch, without replacing the CRT allocator. Backend changes
and shutdown clear the fault. It adds one module bool and a factory-only branch; no render allocation or wake.

The first expected red failure stopped `test.ps1` before its outcome receipt was written. Its log remains, but that
missing receipt is not an exact-artifact proof. The portable repeat overlays the preserved old AV DLL/PDB onto the
new green test runtime. DLL SHA-256 is `68F16F96BB084AE1D2E7ED41C5EA71D7BA7E9036CD7A566B46718ED80AFFDB5C`;
the identical red/green test executable is `BE8A482590AF8F8A14D171DA6722A3F2F95E254848236D4B906DE057D63E3865`.
Exit 1 and the exact last-widget-owner failure are verified, with immutable artifacts before/after. The candidate
DLL passes 9,699 AV checks, its watchdog and camera-package checks, host integration and WARP/screenshot/crash gates.

Release's focused three scopes plus remaining thirteen cover all sixteen obligations. Debug and ASan each pass
fifteen native scopes; ARM64 Release compiles with zero warnings/errors and all ten repository skills pass.
The 468-input closure covers all tracked/unignored non-prose inputs and the schema, including `.def`, `.rc`, manifests,
shader includes and tooling. Runtime seals include EXE, DLL, PDB, fonts, provenance and deployed settings/schema.
All inputs/artifacts remain unchanged through qualification and measurement. The native inventory stays at 62 files.
ASan byte growth is diagnostic under its default quarantine, separately from ordinary resource-budget acceptance.

The old raised-state control and qualified candidate use build 282, the same DxUi pin/archive/toolchain fingerprint,
2560x720 at 96 DPI, AC power, three directional cycles and one-second probes. Every cold/returned phase lasts 65 s;
each visit holds four seconds after verified settle. Both arms have all 18 stages/402 callbacks and normal exits.
Initial and final scenes match across arms; production import/persistence changes each scene identically. Relevant
runtime differences are AVControl DLL/PDB and its consumer hash in provenance; the host and other runtime files match.

| Hardware Matrix phase | Retained AV cache control | Last-provider release |
| --- | --- | --- |
| Cold private / working set | 34.42 / 49.35 MiB | 36.41 / 49.94 MiB |
| Returned private / working set | 62.18 / 87.37 MiB | 64.60 / 87.74 MiB |
| Cold / returned CPU, percent of one core | 1.754 / 2.029 | 1.878 / 1.534 |
| Cold / returned submissions per second | 60.149 / 60.151 | 60.129 / 60.119 |
| Cold / returned adapter-local usage | 25.01 / 32.26 MiB | 25.01 / 32.26 MiB |
| Hidden / minimized submissions per second | 0 / 0 | 0 / 0 |
| Hidden / minimized CPU, percent of one core | 0.064 / 0.065 | 0.065 / 0.065 |
| Returned handles / threads | 1,384 / 43 | 1,380 / 43 |

This one-order observation proves no process-memory, CPU or stage-latency saving. The 1.99-MiB cold private difference
precedes AV visits; cold-return growth is 27.76/28.19 MiB and non-atomic private regions grow 28,413,952/28,864,512 bytes.
Allocator/driver/library history is uncontrolled. Freeing the explicit owner does not force committed heap segments
back to Windows. Fresh coordinator/model allocation on an AV revisit is the intentional cold-path cost of honoring
last-owner reclamation and avoiding reuse of retired borrowed-host state; it remains shared while providers survive.
Gallery stage wall medians are 114.580/102.314 ms, still dominated by unchanged Launcher visibility at 97.518/85.203 ms.
AV first-preparation medians are 1.308/1.291 ms; its device-creation cold maxima are 97.982/123.970 ms. Those observations
do not qualify the first-pixel/construction budgets. Keep whole D return +4 MiB, residual driver/cache attribution,
physical input/presentation, real AV gates and the unexplained earlier screenshot failure open.

Ownership and validation live in `Plugins_AVControl.md` and `Core_PerformanceAndResources.md`. Receipts:
`p36-av-coordinator-{inputs,scope-accounting,portable-red-outcome}.json`, per-profile artifact seals, preserved
`p36-av-coordinator-control` / `-qualified-candidate`, `runtime-2026-10-11-p36-av-coordinator-{control,candidate}`,
their callback/region summaries and `p36-av-coordinator-matched-{comparison,outcome}.json`.

### P0.2 mouse/key capture attempts and next attribution (2026-10-11)

Four own-PID Release captures retain exact source/binary and PresentMon identities, injected x64 INPUT records and
production logs. None qualifies the complete input sequence. The first controller mistakenly tried to raise the
already-full-canvas Matrix tile; Escape took the normal application-close path. It retains 2,777 owned frames but
zero reported input metrics, with unknown trace exit status. The corrected second run retains 1,817 frames and 73
input-metric rows, then stops on foreground mismatch. The quiet retry adds fresh settled-page checks and records
899 frames, one input-metric row, and expected Gallery/System pages before Edge PID 82560 replaces owned PID 52772
as foreground. The foreground guard stops further input; its app and trace exit zero. A fourth approved capture
adds verified cursor placement, pointer-window ownership and paired button up/down. Windows rejects foreground
acquisition before any input injection: owned PID 28384, foreground Edge PID 82560. It retains 183 owned frames,
zero injected input events and normal app/trace exits; its pointer guard is prepared but unexercised. Partial input rows are not
accepted latency or first-changed-visual evidence. Real touch, reversal/canceled swipe and adapter-move qualification
remain open. Receipts: `runtime-2026-10-11-p0-focused`, `-p0-focused-eligible`, `-p0-focused-quiet`,
`-p0-focused-owned-pointer` and
`p0-focused-rejected-accounting.json`.

The first attempt's existing page-stage collector independently records a Gallery stage at 4,825,258 us, with
4,668,114 us between layout and GPU-setup completion and 109.375 ms quantized UI CPU. This is a diagnostic cold
stage, not a matched latency improvement or a widget attribution. P3.4's later complete callback capture above
identifies Launcher visibility as the repeatable target; it does not reproduce that multi-second outlier. Moving an
indivisible slow callback behind a nominal four-ms host slice cannot make its first presented frame fit that slice.

### P4.6 current clock cadence and memory (2026-10-11)

Two non-activating five-minute normal-user hardware runs use isolated full-canvas clocks on the XENEON, services
off, matching native source/DxUi/runtime closure, AC/display-awake/rectangle/page checks and one fresh PresentMon
session. Desk Clock averages 1.435 percent of one core and 26.688 submissions/s, with median private/working-set/local
usage 34.60 / 49.61 / 24.51 MiB. Studio Clock is 0.063 percent, 1.056/s and 30.34 / 46.58 / 22.01 MiB. All 7,841
Desk and 309 Studio owned frames in QPC-joined 294-second cohorts are displayed with hardware-composed independent
flip and zero hybrid presents. This compares current widgets, not old/new optimization variants.

A 100-ms display-interval partition separates intended clock idle gaps from short intervals. Desk's 7,546 short
intervals have median 16.6245 ms, p95 16.6587 and maximum 35.7328; 111 exceed 25 ms. Its 294 idle gaps have median
565.2541 ms. Studio's 292 idle gaps have median 997.4826 ms; 16 short intervals accompany boundary changes.
This is cadence evidence, not rollover pixel verification, callback execution time or physical-input latency.
The overall Desk interval p95 of 33.234 ms includes idle gaps and must not be called an animation-callback cost.
Private memory spans only 192,512 Desk / 159,744 Studio bytes across the 60 probe samples; final values are
24,576 / 94,208 bytes below their first samples. No sustained growth is established by these hardware observations.
They do not attribute or erase the separate failed WARP peak guard recorded under P4.5.

Keep the shared multi-tier atlas and normative one-ms Desk active-flip deadline. Replacing it with 16 ms could miss
presentation slots; no measured lower-cost replacement is selected. Current native scale/resize, sharing, scheduling,
rollover and teardown tests passed the unchanged P4.5 profile accounting. Receipts:
`.build/receipts/runtime-2026-10-11-p46-clock-presented` (raw CSV, identities, phase/probe logs, per-widget summaries,
`burst-summary.json`) and `p46-clock-memory.json`. Input/real-touch and longer WARP peak attribution remain separate.

### P4.3 temporary glyph extraction batch (2026-10-11, complete)

The preceding glyph-only Release callback takes about 7-8 ms for eight icons and 29 ms for 32, with no GPU or shell
extraction. Common/Actions/GlyphIcon.cpp creates an isolated DirectWrite factory and traverses the system font
collection separately for each icon. The candidate uses one lazy factory/face and a reusable clipped coverage buffer
inside RefreshDisplay, released when that extraction returns. The one-glyph wrapper used elsewhere still has its
per-call lifetime. No retained cache, thread, timer or shell-extraction change is selected. Family fallback, metrics,
hinting, centering and alpha averaging remain unchanged; initialization failure is remembered only for the local batch.

A pre-change native build records 400 frames: all 80 named glyphs at edges 1/24/48/256/512. Candidate tests compare
independent and batched calls including guard bytes, missing/invalid glyphs and small output after large coverage.
Debug exercises a real CRT coverage-growth failure, keeps the preceding buffer usable and retries successfully.
All 400 named-glyph references match. The first build had a test CRT-hook signature error, corrected before
qualification. First ASan runs complete all 400 pixels but fail a CRT-hook assertion because its intercepted
allocator bypasses that Debug hook; fault injection is explicitly Debug-only, while ASan keeps all functional
and instrumented lifetime checks. Matched Release control/candidate/candidate/control medians for nine fresh
instances per size/pair are eight icons 7,139/2,397/2,395/7,348 us and 32 icons 29,283/6,539/6,494/28,538 us.
Empty medians remain about 6.5 us. Latest compiler type records confirm a 40-byte temporary font owner and
an unchanged 122,824-byte LauncherWidget; incremental PDBs also retain older type records, which are not the
current instance definition. Transactional coverage growth can temporarily hold two clipped buffers; the
Launcher 256-edge maximum overlap is 384 KiB, released at extraction return. These callback measurements include CPU decoding and exclude GPU setup,
shell/WIC extraction and physical presentation. Final Debug15, Release16, ASan15, ARM64 Release compilation with zero warnings and ten skills pass on sealed
native inputs. All 400 glyph references, 15 Launcher WARP frames, native RGB/noncorner RGBA, 145 Viewer frames
and eight Weather images match. A receipt helper originally compared pre-build Debug provenance after rebuilding;
that guard correctly rejected the changed provenance. The continued helper captures the rebuilt Debug closure
and runs only remaining Release/ASan/ARM64/skill obligations; no already-passed Debug suites are repeated.
Scope accounting is p43-glyph-final-scope-accounting.json. A final fixed-eight control/candidate pair holds AMD, 2560x720/96-DPI geometry, AC, scene and artifact hashes
constant, with services/tray disabled and no activation or physical input. Twenty seconds each yields four
samples (about 15 seconds between endpoints): CPU/submissions zero, local adapter 18.01 MiB both, private
41.95/40.82 MiB and working set 52.59/52.70 MiB. These short settled observations show no material steady
regression; they do not establish whole-process CPU savings, attributable retained memory or presented latency.
The preceding control closure and matched callback byte identities are retained. Shader, 0/8/32 texture storage, preparation and Shell/WIC paths are unchanged.

### P3.3 current swipe contract reconciliation (2026-10-11)

The September sequencing proposals conflict with `UI_Dashboard.md`: it requires a first follow-finger frame before
staging, only current plus the directionally adjacent page, retirement on reversal, and continuing animations during
pan/settle/staging. Current `Application::FlushPendingTransitionStage`, reversal handling and
`DashboardRequiresContinuousFrames`, together with Renderer partially-offscreen drawing, implement that policy.
Existing `TestPageSwipePolicy`, `TestSwipeRendersPartiallyOffscreenGpuWidgets`, `TestNativeWindowNeighborSwipe` and
GPU retirement failures pass in the final P4.3 source closure. Retaining both sides would add a third live page and
violate the current resource bound; suppressing staged frames would freeze required continuous content.

Keep that policy and correct the old plan. A canceled gesture that displayed neighbor pixels can have legitimately
activated its widgets; blocked or canceled-before-stage gestures still must activate nothing. The proposed timing
and retention limits stay open. This reconciliation is not new physical-input or presented-frame evidence. P3.4
must include preparation and the first live presentation; P3.5 remains conditional because snapshots freeze animation
and add retained GPU memory. Specification-only validation checks all ten repo skills; native source bytes are unchanged.

### P4.3 count-sized Launcher icons (2026-10-11, complete)

The preceding real Launcher DLL binds a 32-slice, 256-pixel BGRA texture for zero, eight and 32 icons. The candidate
binds none/eight/32: 0/2/8 MiB, preserving the format, filtering, shader, grid, pagination, glyphs and hit targets.
Small instance buffers remain reusable. Device creation may upload retained decoded data for recovery; visibility
and drop mark the CPU display dirty, and preparation creates/uploads only used slices. Clean calls allocate/upload
nothing. A missing used icon needs one temporary transparent 256-KiB heap buffer, replacing repeated 256-KiB stack
clears. Texture/view replacement publishes only after both exist; failures keep prior owners and suppress unchanged
retry. Visibility and changed extent recovery pass. Empty grids keep the original background/hint draw.

Real WARP tests cover 0/8/32 icons, five size/DPI combinations, controlling COM identity, visibility upload isolation,
pin import/queue/persist/drop/pagination, device loss without extraction, both texture and view failure paths,
explicit retry, hidden suppression and allocation-free clean composition. All 15 pixel checksums match the preceding
DLL. Native eight/32-icon scenes initially used invalid `glyph:Settings` and fell back to defaults; those captures
are rejected. Correct bare `Settings` scenes load successfully. Final eight/32-icon native captures preserve every RGB
pixel and noncorner RGBA byte; 36 alpha-only pixels differ in the bottom eight-pixel rounded corners. Full PNG byte
equality is therefore not claimed. All 15 controlled WARP frames match every channel. The control harness's first copied-DLL runs lacked its adjacent yyjson dependency and are also rejected.
Tests drain deferred D3D runtime references only for last-owner observation; production adds no `Flush`.
Release compiler symbols measure LauncherWidget 122,808 -> 122,824 bytes and its included GPU owner 48 -> 56.
The 16-byte per-widget cost replaces an empty 8-MiB texture and unused uploads, with at most 16 MiB of old/new
texture payload during replacement. Driver/context references and allocator overhead are additional.

Initial matched Gallery control/candidate receipts record CPU 2.057/2.160% of one core, submissions 60.133/60.153/s,
private 91.61/60.32 MiB and local adapter usage 54.24/34.24 MiB. Services/tray are disabled; page, geometry, power,
source/artifact immutability and matching initial scenes pass. Live taskbar/icons, weather and background load are
unpinned, and the process/driver difference exceeds the six-MiB eight-icon payload reduction; no causal aggregate
saving is claimed from this pair alone. A fixed eight-authored-glyph scene then runs candidate/control for 65 seconds
each, with identical other production dependencies, scene, AMD adapter, 2560x720/96-DPI geometry and AC power.
It records private 73.23 -> 41.21 MiB, working set 68.89 -> 52.66 MiB and local adapter 38.01 -> 18.01 MiB.
Both settled streams submit zero frames; CPU is 0.078/0.000% of one core. These are observed process/driver costs
of the storage/upload cut; the exact driver allocation mechanism is unmeasured, and the guaranteed eight-icon
logical texture reduction is only 6 MiB. A second Gallery control/candidate pair records private 91.41/58.95 MiB,
CPU 2.551/2.318%, submissions 60.134/60.150/s and local 54.24/34.24 MiB. The two Gallery pairs disagree on CPU
direction and retain live inputs, so no CPU improvement is established. These cold-page measurements do not
qualify D return retention, staging CPU or physical input latency.

Repeating the preceding native eight-icon capture produces the candidate PNG hash exactly, demonstrating that
the earlier corner-alpha difference also occurs with unchanged preceding code. The 32-icon repeat retains only
that same 36-pixel corner-alpha difference; all rendered RGB and noncorner RGBA match.

A separate real Release DLL visibility measurement without GPU or shell work records eight-glyph wall times
8,227/7,224/7,165 us and 32-glyph 28,569/28,925/31,949 us. Thread CPU ticks are quantized. Every glyph currently
creates an isolated DirectWrite factory/face. This selects a separate temporary extraction-batch experiment; no
font-cache or shell-worker saving is established here. See p43-glyph-control-timings.json.

Debug Full15 passes. Initial Release Full passes through integration, then Matrix exceeds its unchanged four-MiB
guard with a 4,235,264-byte peak (3,305,472 at end); this failure is retained, never a passed Full result. An external
read-only five-second region diagnostic measures a 1,830,912-byte peak and passes; it is separate diagnostic evidence.
The first diagnostic helper hit a process-startup race and stopped its owned child; the guarded repeat is accepted.
Clean Release HostPlugin/HostSmoke then pass completely, with Matrix peak 2,338,816 bytes (1,753,088 at end).
Source and Release artifacts are unchanged. The original intermittent growth remains unexplained, as in P1.6.
Final ASan Full15, ARM64 Release compilation with zero warnings and ten skills pass. The unchanged-source
accounting covers Debug15, Release16 and ASan15; the initial Release guard failure remains a failed run. Viewer145
frames and Weather eight image references stay identical. Root formatting touched two unrelated files; restoring
those formatter-only changes recovers every qualified native input hash before final capture and measurement.
Receipts are `runtime-2026-10-11-p43-*` and `p43-final-scope-accounting.json`.

### P3.1 checked selection and validation evidence (2026-10-11)

The current host copied the complete typed settings document just to change its selector, then repeated typed and
published-schema validation for every page. Staging now borrows the document with a checked target index. Promotion
keeps its existing selector-only semantics, including source-only reload and edits during a gesture. One immutable
logical witness is shared by the two page managers. Exact bounds/byte/scalar comparison includes inactive widgets,
layout, services and plugin configuration; source text and a valid active selector are excluded. No hash-only trust
or mutable pointer identity is used. A miss revalidates the full document. Missing referenced modules cannot issue
proof, terminal runtime shutdown cannot reuse it, and documents above 64 KiB or cache allocation failure keep the
cold path. The shipped Release witness is 6,166 bytes plus shared metadata and 16 bytes per manager; the old duplicated
document retained 209,460 bytes of typed/vector/source capacity. These logical measurements establish no process
memory reduction. The optional staging probe uses bounded local storage and the existing logger ring.

New tests cover real cold/warm target construction, source/selector-only reuse, inactive malformed JSON, invalid
configuration/identifiers/extents/counts/layout/services, valid edits, failed reconfiguration, non-wrapping/wrapping
adjacency, optional cache allocation failure and a valid maximum-page document above the witness budget. The oversized
fixture initially used invalid long IDs and placements against the parser's 1x1 adaptive canvas; its final fixture
uses legal 128-byte IDs and an explicit 32x9 non-overlapping grid. Failed iteration logs remain. A 4,096-construction
Debug batch records 398,343 / 85,032 us wall and 3,906,250 / 937,500 raw CPU 100-ns ticks for cold/warm validation.
The final Release batch records 30,325 / 13,825 us wall and 312,500 / 156,250 raw CPU 100-ns ticks. These batches include actual providers and cleanup, without GPU setup; their CPU totals are quantized and do not qualify total staging CPU.

Fault tests also confirm that the previous renderer retained a borrowed staged page after failed visibility, and
retained its context bindings after failed hide. Cold retirement now clears the borrow unconditionally, returns the
original failure and releases initialized resources. Application rollback detaches before local dashboard teardown.
The preceding renderer fails both tests; the candidate keeps the primary's exact pixels, device and swap chain.

`runtime-2026-10-11-p31-stage-control-repeat` and `-stage-candidate` each accept 36 Release stages, the same scene SHA,
all source/binary immutability guards, the same XENEON size/DPI/adapter and no physical input. The first control helper
run is rejected because its loop counter collided with a Boolean parameter. Every accepted stage has success,
all seven phase bits and valid CPU readings. Repeat medians (us) are:

| Target | Selection control / candidate | Validation+providers control / candidate | Total control / candidate |
| --- | --- | --- | --- |
| Matrix | 89 / 0 | 247 / 74 | 803 / 513 |
| Gallery | 39 / 0 | 521 / 463 | 101,714 / 151,044 |
| System | 71 / 0 | 411 / 273 | 9,622 / 10,713 |
| Fourth page | 64 / 0 | 264 / 67 | 19,689 / 24,557 |

Zero selection readings are microsecond-resolution wall samples. Gallery's candidate p95 is 910,071 us; GPU and
visibility costs dominate and vary, so no total staging improvement is established. The proposed 2-ms CPU / 8-ms
staging targets remain unmet. Cold/returned Matrix process CPU is 1.478/1.477% control versus 2.297/2.383% candidate;
submissions are 60.112/60.170 versus 59.747/58.477 per second. The live scene and background load are unpinned; returned
module/thread totals also differ (102/42 versus 110/46), so these values establish no causal steady CPU/rate change.
Cold/returned private is 36.60/107.75 versus 35.39/108.77 MiB;
local usage remains 25.01/46.26 MiB. Return retention is still open. Final Release16, Debug/ASan15 native scopes, ARM64 Release compilation and ten skills pass. p31-final-scope-accounting.json binds unchanged source and Debug artifacts; all 145 Viewer frames and eight Weather images match the preceding references. runtime-2026-10-11-p31-final retains source, binary identities and logs. Physical input latency and the 2-ms/8-ms goals remain separate open qualification.

### P1.6 optional continuous pacing (2026-10-11, complete)

Settings minor 4 adds `continuousFrameRate: "display" | 30`, omitted/display by default in both templates.
A 30-fps selection keeps one monotonic 33⅓-ms deadline and a lazy reusable one-shot waitable timer. The message-aware
wait overlaps DXGI readiness, acquired once and retained across message/diagnostic interruptions. It never polls
the signaled swap-chain handle. Explicit invalidation and page,
overlay or dock motion bypass the cap; inactive states cancel/reset it, delayed frames do not catch up, and disabling
releases the timer. An acquired DXGI permit survives these deadline resets and mode changes until a render consumes
it; buffer resize retains it, while replacement of the waitable object invalidates it. Adds 24 bytes of application
pacing storage, an 8-byte renderer lifetime counter and the typed enum; no worker, periodic timer,
steady heap allocation or GPU resource. Pacing-only live reload keeps widgets and swap-chain resources.
Matrix dimensions, density, glyphs, bloom and shaders remain unchanged; geometry is size/DPI-derived and animation
uses elapsed time. Native parser/schema/template, 60/120-Hz model, actual message/timer/diagnostic/reuse/release
checks and production live-toggle self-tests are added. The initial focused settings runs exposed hard-coded
current/future minor fixtures; these now derive their versions from the catalog. Failed logs are retained.
The first overlapping-wait recovery stalled after hide because it discarded an
acquired DXGI signal; powered-on/window checks passed, but the behavioral result is rejected and retained as
`runtime-2026-10-11-p16-overlap-recovery/qualification.json`. Native tests now exercise the real auto-reset signal,
deadline cancellation, resize and waitable-object replacement, and the collector rejects a visible Matrix phase
with zero progress. The corrected probe-only recovery repeat gives 29.309/s cold, 29.262/s restored and 29.320/s
after minimize; hidden/minimized both zero, private 35.11 to 34.95 MiB, local usage 25.01 MiB throughout.
`runtime-2026-10-11-p16-permit-recovery`. P0.2 physical input/swipe latency is separate.

Two intervening Matrix guard runs exceeded the unchanged 4-MiB private limit (7,761,920 and 11,440,128 bytes).
Inspection found unpaced warm-up, no DXGI readiness waits and an animation-time rewind before paced measurement.
The guard now uses identical readiness-bounded 16-ms-sleep cadence throughout and one monotonic timeline, retaining
the same 1,024-frame warm-up and 4-MiB/8-MiB thresholds. The first corrected Debug run has 57,344-byte private and
106,496-byte working-set peaks. A later paced run still exceeded the private limit (7,102,464 bytes), so pacing alone
does not explain the failure. A read-only five-second virtual-region diagnostic did not reproduce it: private peak
growth 1,863,680 bytes, stable handles and bounded regions. It is diagnostic evidence only. The subsequent clean
Debug Full15 run passes, including a 1,126,400-byte Matrix private peak. These observations do not attribute earlier
growth to a heap or driver stack; the intermittent guard failure remains unresolved. Final Release Full16 and
Debug/ASan Full15 native scopes pass, with normal sanitizer quarantine, ARM64 Release compilation and ten skills.
`p16-permit-scope-accounting.json` binds the unchanged source and Debug artifacts. Final Release Matrix peak is
929,792 bytes. Logs and `runtime-2026-10-11-p16-matrix-map` are retained.

Final counterbalanced Release x64 pairs use one binary/source set, identical scenes except the pacing field,
services disabled/tray off, 65 seconds/13 samples per side, XENEON 2560×720 at 96 DPI on its AMD adapter and AC.
Display/cap CPU is 1.666/1.120% of one core, then 1.979/1.275% in reverse order: reductions 32.77/35.57%.
Submissions are 60.135/29.393 and 60.150/29.320 per second, about 51% fewer. Private medians are 36.11/36.70
and 34.98/36.13 MiB; working sets 49.31/49.38 and 49.32/48.88. Both use 42 threads, 71 modules, zero AV
surfaces and 25.01 MiB local adapter usage. The observed cap private increases (0.59/1.15 MiB) have no established
byte attribution; bounded native added storage is 32 bytes plus the settings enum. The optional CPU/submission
reduction accepts this measured cost while retaining display pacing by default. No process-memory saving is claimed.
Earlier serialized-wait pairs remain historical, superseded for this implementation.

Common nonadministrator PresentMon sessions cover each pair. QPC-joined cohorts show 3,549/1,734 and 3,550/1,730
frames, all displayed through hardware-composed independent flip, one swap chain per process, zero hybrid presents.
Displayed rates are 60.137/29.383 and 60.155/29.319/s. Displayed-interval medians are about 16.624/33.249 ms;
default p95 is 16.656/16.657 ms and cap p95 47.754/49.868 ms (cap maxima 49.926/50.859 ms). This is an optional
30-fps ceiling, not a uniform interval guarantee. CPU-start-to-display medians are 32.963/45.185 and
32.942/45.709 ms; they are not physical-input latency. All 145 Viewer checksums and eight Weather PNGs match
the preceding accepted references; deterministic Matrix tests pass with production Matrix/shader sources unchanged.
`runtime-2026-10-11-p16-final-paired[-reverse]`, `-final-presented[-reverse]`, `-final-images` and
`-permit-recovery` preserve scenes, raw data, scripts and source/artifact identities. Recovery identities still
match after all profile gates. P0.2 physical input/swipe and the broader return budget remain open.

### P4.1 source arena (2026-10-11, complete)

The source owns one 16-MiB reservation, with its 2,816-byte controller in the first 4-KiB committed page.
Page-aligned acquisition state and individual output tables commit on first use. Shared GPU/network/power caches
retain their bounded shape; native thread scratch and the legacy process-value suffix are separate. Activity's
eight-column prefix and later 39-column detail share one address. Combined activity views commit only when needed.
Preflight precedes history/sampling mutation; failures preserve sequence, history and pending copied samples.
Typed nontrivial owners construct after commitment and destroy before final WIL release. No public ABI, worker,
timer, settings or renderer change is introduced. Maximum used layout is 16,592,896 bytes, full commitment
16,531,456, both below 16 MiB. The 105,840-byte layout increase pays for page alignment/control reserve; unused
pages stay uncommitted. Private diagnostics are 104 bytes; cold module ownership/fault counters add 28 bytes before
linker alignment.

The preceding DLL fails the cold-page regression. Tests cover actual VirtualQuery regions, reserve/control/first-use/
partial/detail-expansion faults, reuse, rate/pending-sample rollback, six independent acquisition/owner cases,
optional thread scratch, malformed native ranges even without scratch, copied fixture ownership, COM identity and
final reservation/native-owner release. Initial compile/assertion errors and the address-lifetime review correction
are retained; the stopped earlier Full run supplies no qualification. Final Release Full16, Debug/ASan Full15 native,
ARM64 Release and ten skills pass. Hardware exposed a fixture mismatch: its former batch requested gpu.engine instead
of System Pulse's system.summary. The corrected actual 15-subscription fixture passes fresh SystemData runs in all
three x64 profiles and ARM64 compilation; frozen input comparison proves that only this test changed after Full.
Production inputs/artifacts and the other scope closures remain unchanged. Actual default commitment is 9,920,512
bytes (9.461 MiB). The prior 10,076,160-byte batch remains a different subscription set, not a shipped-page receipt.
All 145 deterministic Viewer frame checksums remain identical.

Two public-factory comparisons each use three warmups and 64 cold sources per fresh helper. With identical FFI
overhead, control/candidate constructor medians are 1,830.05/2.35 microseconds and reversed-order 1,978.3/2.3;
p95 is 3,066.9/3.5 and 3,379.2/4.3. VirtualQuery confirms one 16-MiB candidate reservation, 4,096 cold bytes,
9,920,512 for shipped subscriptions and 16,531,456 for all 23 datasets. Repeats add no pages. Constructor and
default-page targets are met; helper process private bytes include bookkeeping, and the old CRT region is not
asserted to be exclusively owned by the source.

Matched System65s pairs change only SystemData.dll among runtime artifacts. Scene, geometry/DPI, build 282,
provenance, OS/CPU, AC power and AMD adapter/LUID match. Control-first CPU is 1.248/2.312 percent of one core,
submissions 5.507/11.985 per second, private 58.67/51.43 MiB and working set 76.28/67.48. Reversed-order
control/candidate CPU is 2.005/1.691, submissions 12.147/12.184, private 58.50/51.25 and working set 74.65/67.44.
Private reduction repeats at 7.24/7.25 MiB; CPU/submission direction does not. The first pair also has 45/42 threads
and 79/78 modules, versus 43/43 and 78/78 in the reverse. Live data is unpinned, so no causal CPU or cadence saving
is claimed. Local usage stays 26.26 MiB. QPC PresentMon cohorts contain 298/706 and 715/717 frames: all displayed,
hardware-composed independent flip, no hybrid presents. Sparse intervals do not qualify physical input latency.

The completed D control is paired with a repeated candidate after fixing only the external observer's executable
guard; the failed capture is retained. Three read-only exact-PID/DLL/PDB observations on System each show one arena,
16-MiB reservation, 9,920,512 committed bytes and one GPU/network/power owner. After return, all are zero in three
settled observations while the module stays mapped. Control/candidate cold private is 35.79/36.69 MiB, returned
104.46/105.65; return-over-cold 68.67/68.96 still misses +4 MiB. Local usage is 25.01 cold and 46.26 returned
in both; hidden/minimized submissions are zero. This proves source retirement, not whole-process/driver-cache
reclamation. The final PDB copy initially failed its hash check with a zeroed prefix; cause is unestablished. A repeat
copy restores the exact saved PDB; the runtime DLL never changes. This recovery is recorded explicitly.

Receipts: runtime-2026-10-11-p41-arena, -control, -construction-shipped and -construction-shipped-reverse, -images,
-paired/-paired-reverse, -presented/-presented-reverse and -cycle-paired-recovery; earlier failed/superseded evidence
remains retained. P3.6 return/cache attribution and P0.2 input/swipe qualification remain open.

### P2.1 activity dataset (2026-10-11)

The default Process Viewer and GPU Processes name join now consume the eight-column `process.activity` prefix,
preserving all leading fields, cadence and presentation. Legacy `process.list` retains its 39-column contract and
optional handle-based detail. Both requests share one process collection/history replacement in either order, and
their leading value pointers are identical. Activity-only native collection opens zero process handles and uses an
eight-value stride in the existing table. Toolhelp fallback keeps accessible leading-field and later detail I/O
quality. One extra 2,048-row view array costs 49,152 bytes; it avoids a second value table and remains under the
16-MiB source ceiling. Release diagnostics measure 16,487,056 source bytes, a total increase of 49,296 including
metadata, snapshot and bounded test state. Steady row-cap collection reports zero heap/handle/private-byte growth.
This fixed-storage activity receipt precedes the completed P4.1 arena cut above.

The preceding DLL fails the new catalog check. Focused Debug passes live zero-handle collection, deterministic
request ordering, PID reuse, CPU/I/O history, aborted-batch recovery, row/name caps and fallback quality. The
64-byte private diagnostics and bounded copied-walk/clock seam remain test-only. A Windows `small` macro collision
in the initial test compile was corrected; the failed build is retained. Final Release passes all 16 scopes;
Debug and ASan Debug pass all 15 native scopes with default sanitizer quarantine, ARM64 Release compiles with
zero warnings/errors, and all 10 skills pass. All 145 deterministic Viewer references match the preceding
implementation byte for byte. Release row-cap collection reports zero heap blocks/bytes, handles, private bytes
or working-set growth; its live timing is diagnostic and does not establish a causal improvement.

Two 65-second System pairs change only SystemData.dll and ProcessViewer.dll among runtime artifacts. Remaining
binary/provenance hashes, build 282, scene, rectangle/DPI, OS/CPU, AC power and AMD adapter/LUID match; candidate
DLL/PDB restoration and frozen native input hashes pass. Each side has 13 complete probe samples, 43 threads,
78 modules, zero AV surface bytes and 26.26 MiB local adapter usage. Control-first CPU is 1.895 → 1.689% of one
core, submissions 12.760 → 13.106/s, private 59.23 → 57.60 MiB and working set 74.75 → 74.20. Reversed-order
control/candidate CPU is 1.898 → 1.612%, submissions 12.198 → 11.097/s, private 57.44 → 57.42 and working set
73.95 → 73.91. CPU is lower in both observations; submission direction reverses and the first memory reduction
does not repeat. Live process data is unpinned, so no causal whole-process saving or proposed budget acceptance
is claimed.

One nonadministrator PresentMon session covers each pair. QPC-joined control/candidate cohorts contain
748/774 and reversed-order 722/659 frames, all displayed through hardware-composed independent flip, with no
hybrid presents. The reversed candidate has a 1,928.487 ms maximum display-change interval versus control
581.911 ms. These sparse update intervals include idle time; they do not qualify physical input or callback
latency. P0.2 retains those gates. Receipts: `runtime-2026-10-11-p21-activity`, `-control`, `-images`,
`-paired`, `-paired-reverse`, `-presented` and `-presented-reverse`.

### P2.8 Viewer preparation cut (2026-10-11)

Viewer delivery now updates bounded pending storage with no font/GPU work. UI-thread preparation freezes the paint
sample and uploads changed 48×48 cells; Render uploads none. The additional pending sample is 11,160 bytes per
widget, or 111,600 bytes for the default System page plus bounded interface/phase bookkeeping, accepted for complete
labels/history and coherent tile/raised drawing. One temporary font session is reused across preparation strings
and released before drawing, including failure. This replaces the proposed persistent per-GPU-user font cache:
batching avoids per-string sessions while retaining zero steady font ownership. Weather remains a separate cut.

Real worker delivery, late tile/raised labels, one/two dirty-cell byte counts, temporary font sharing/release,
invalid preparation, injected failure with no automatic retry/input/drawing, and extent recovery pass in the focused
viewer test. The same helper is compiled into the Debug Logicon monitor: its face/glyph uploads move to Prepare,
and the new synthetic-lane WARP test passes late-state and identical-pixel device-recovery checks. Monitor failure
retry behavior and Viewer dynamic-cell reuse after atlas capacity wraps need separate follow-up tests.

All 145 deterministic tile/raised frame checksums across the ten viewer families match the preceding drawing
implementation, including Unicode, rank/ease endpoints and recovery. The reference-only harness permits the
preceding implementation without Prepare; production retains one current optional-interface contract. The control
uses the preceding three production sources plus an unused private fault-export shim for the current test header.
`runtime-2026-10-11-p28-prepare-pixels` retains its source hashes,
control DLL/PDB, identical harness hash and frame checksums.

The first post-comparison runtime run `-p28-prepare-after` is rejected for candidate qualification: preserved source
timestamps left the archived control DLL in the incremental Release output. Its raw collector result is retained
alongside `candidate-qualification.json`; the full gate caught the missing Prepare interface. A forced rebuild and
focused confirmation precede `-p28-prepare-after-candidate` and `-p28-prepare-after-repeat`. CPU is 2.186 / 1.770%
of one core and successful submissions 13.876 / 12.080 per second; local usage stays 26.26 MiB. The original clean
`-p28-prepare-before` is 2.135% / 12.430, but the forced rebuild also changes unrelated artifact bytes. A fresh
control with matched remaining artifacts supplies the final paired comparison below.

`runtime-2026-10-11-p28-prepare-paired` swaps only the identified Viewer DLL between control and candidate,
restores the candidate DLL/PDB with hash checks, confirms Prepare through the ordinary focused test and repeats
the 145 identical reference checksums. All other runtime artifacts, the scene, 2560×720 rectangle, 96 DPI,
hardware adapter/LUID, OS, CPU, AC power and build 282 match. The 65-second runs each contain 13 complete samples:
CPU 2.134 / 2.055% of one core, successful submissions 12.244 / 11.954/s, private 57.62 / 57.92 MiB,
working set 74.12 / 74.04, local usage 26.26 in both, 43 threads, 1,177 handles and 78 modules. AV surfaces are zero.
These rate changes are within the observed run variation; no causal whole-process CPU or submission saving is claimed.

All 16 Release scopes and 15 ASan native scopes pass. Debug's initial functional run plus the final HostPlugin/
HostSmoke run account for all 15 native scopes. The initial isolated Debug Matrix guard failed at 9,318,400 bytes
peak private growth (5,910,528 at the end); both quiet repeats peak at zero under the unchanged four-MiB guard.
The cause of that outlier remains unresolved and its failed log is retained. ARM64 Release compilation and all
10 skills pass. `runtime-2026-10-11-p28-prepare` saves source/artifact identities and all successful/failed gate logs.

### P2.8 Weather preparation cut (2026-10-11)

Weather now exposes the optional preparation interface on the same controlling COM identity. Network delivery
updates the latest CPU snapshot without atlas access; preparation freezes a separate 9,904-byte paint snapshot
and Render reads it without a worker lock/copy. Late tile/raised deliveries wait for the next preparation.
Dirty cells upload from their stored coordinates and sizes, preserving the reserved 96 px hero/icon twins.
One temporary font session serves the changed label batch and releases before drawing, including injected failure.
Clean/failed preparation does no automatic work until new data, changed extent or device recreation permits recovery.
The bounded paint-model/revision cost is accepted for complete forecast/alert data, with no new thread or timer.

The new interface test fails against the preceding DLL as expected. Focused Release tests pass real worker
delivery, coalescing, late tile/raised paint, one/two dirty-cell byte counts, batch fonts, failed input/drawing,
no retry, new-data/extent recovery, hidden preparation and identical-pixel device re-acquisition.
All eight preceding layout PNGs remain byte-identical; `runtime-2026-10-11-p28-weather-images-before` retains them.
Forced Debug/Release rebuilds pass: Release all 16 scopes and Debug all 15 native scopes. ASan Debug passes all
15 native scopes with its default quarantine; ARM64 Release compiles with zero warnings/errors, and all 10 skills
pass. The host fixture now prepares every optional prepared widget before drawing and preserves setup failures.
`runtime-2026-10-11-p28-weather` saves the final sources, binaries and gate logs.

Two 65-second Gallery pairs change only the Weather DLL, retain matching scene/profile/geometry/adapter and verify
restoration plus eight identical PNGs. The second pair runs candidate before control. Each run has 13 samples.
The production forecast responses are live, not pinned; these are whole-process observations, not a causal CPU claim.

| Pair | Control / candidate CPU, percent of one core | Control / candidate submissions/s | Control / candidate private MiB | Control / candidate working set MiB |
| --- | --- | --- | --- | --- |
| Control first | 1.900 / 2.760 | 60.152 / 60.150 | 92.02 / 91.58 | 92.79 / 93.18 |
| Candidate first | 2.343 / 1.978 | 60.136 / 60.152 | 90.94 / 92.14 | 92.86 / 92.78 |

All four runs report 45 threads, 96 modules, local usage 54.24 MiB and AV tile surfaces 1,474,560 bytes. CPU moves
in opposite directions between pairs; no saving or repeatable regression is established. Retain the first higher
candidate reading. `runtime-2026-10-11-p28-weather-paired` and `-paired-reverse` contain every raw interval and
comparison. The demonstrated change is removal of worker font work, Render snapshot copying/locking and full-atlas
uploads: one new cell uploads 2,304 bytes in Prepare, and a clean preparation or Render adds zero font/upload work.
That cut exposed Weather's exhausted dynamic cursor and Viewer's reused-cell ink. The separate capacity cut below
resolves both with real-DLL regressions. The later Debug monitor cut completes failure retry and P2.8.

### P2.8 glyph capacity follow-up (2026-10-11)

The preceding real DLLs fail two new regressions: Viewer retains old accent ink when a dynamic cell wraps to an
unsupported glyph, and Weather stops uploading new label characters once the complete small-cell rows fill.
Viewer now clears only a reused 48×48 cell before rasterization; fresh/static cells avoid extra clearing. Weather
wraps its dynamic cursor at the 315 complete small-cell positions above the 96 px reserve, preserving every static
slot and hero/icon twin. Neither atlas or widget storage grows; the required 2,304-byte clear is confined to actual
cell reuse and adds no steady Render work. Continued glyph rasterization after exhaustion restores required labels.

Focused Release passes both red/green regressions. Viewer compares reused unsupported-glyph pixels with a fresh
blank glyph; Weather delivers 441 distinct label characters through real worker calls, then proves one new accent
uploads 2,304 bytes and matches a fresh device rebuild. All eight preceding Weather PNGs remain byte-identical.
Release passes all 16 scopes. Debug's first full run passes the other 14 native scopes and all resource soaks,
but its triangle-only screenshot child terminates with `0xC0000005`. The same Debug executable passes an isolated
capture and the complete HostSmoke repeat, accounting for all 15 native scopes; the original failure and logs remain
retained and its cause is unresolved. ASan Debug passes all 15 native scopes with its default quarantine.
ARM64 Release compiles with zero warnings/errors and all 10 skills pass. `runtime-2026-10-11-p28-capacity` saves final
source/artifact hashes and logs; supporting receipts include `-capacity-control`, `-capacity-images` and
`-capacity-capture-investigation`. This is a quality/capacity fix; no new whole-process CPU saving is claimed.
Debug monitor failure retry remains the next preparation follow-up.

### P2.8 Debug monitor preparation follow-up (2026-10-11)

The preceding monitor with the new real-font fault seam fails the unchanged-failure retry regression. The candidate
latches a 32-byte publication/extent key plus a flag, releases all temporary font owners on failure and suppresses
100 unchanged preparation calls before font/raster/upload work. New published content (including a name-only host
update), DPI/tile/raised extent, visibility or device changes permit recovery. Hidden/unprepared drawing and new
control taps are suppressed, while Up/Cancel still releases a held virtual control; reshow/recreation requires
successful preparation. Snapshot and visible-stamp publication/copy share the service lock. No attached monitor
means no monitor hash or frame-request work. Private 32-byte Debug glyph diagnostics leave the existing 232-byte
service diagnostic and production probe unchanged; Release has a resource-free `E_NOTIMPL` stub.

The deeper device-recovery test also exposed neighboring atlas ink: after several cached accents, 16 unrelated
label-edge pixels differ by up to 84 channel levels when the atlas is rebuilt. Clamping one linear glyph sample to
its own half-texel cell bounds fixes the exact-pixel regression without shrinking/clipping glyphs, enlarging an
atlas/instance buffer or adding draws/texture samples. The bounded shader clamp and flat bounds varying are
justified by this visible quality defect. Final Release Full (16 scopes), Debug and ASan Debug Full native
(15 scopes each, default sanitizer quarantine), ARM64 Release compilation and ten skill validations pass. The queued
Debug run built before the final publication/diagnostic changes was cancelled and is not accepted evidence; the
final Debug gate rebuilt the current sources. All 145 preceding Viewer references and eight Weather PNGs match.
Focused Debug WARP passes failure/font release, clean work, name-only publication, recovery, exact device pixels,
actual mode-chip hit controls and virtual-control Cancel/Up release. The fixture originally checked physical key
state for a virtual press; it now proves press/release edges reach only the fake host. `-p28-monitor-control`
retains the failing implementation and `-p28-monitor-release-control` the preceding Release Viewer and shaders.

The shared shader has a matched 65-second System comparison in both orders, with one common PresentMon session per
pair. Only the Viewer runtime DLL differs; scenes, all other runtime binaries, pin/provenance, version 1.0.282,
AC power, OS/CPU, 2560×720/96-DPI geometry, page and AMD output-owned device match. Candidate restoration is verified.
Live process data is not pinned; services/tray are disabled and no physical input or activation is sent.

| Capture order | Control / candidate CPU, one core | Control / candidate submissions/s | Control / candidate private MiB | Control / candidate working set MiB |
| --- | --- | --- | --- | --- |
| Control first | 2.341 / 2.888% | 12.667 / 13.556 | 57.98 / 67.81 | 73.95 / 89.98 |
| Candidate first | 2.835 / 2.185% | 12.450 / 12.168 | 57.79 / 57.78 | 74.09 / 74.37 |

Both pairs keep 43 median threads, 78 modules, zero AV surface bytes and 26.26 MiB local adapter usage. The first
private-memory difference did not repeat; its cause remains unattributed. CPU direction reverses, so no saving or
repeatable regression is established. Both observations are retained rather than selecting the favorable run.
QPC phase joins, trimmed by five initial seconds and one trailing second, confirm hardware-composed independent
flip with no hybrid presents and every cohort frame displayed. Sparse settled-page intervals and CPU-start-to-display
values include idle waiting and MUST NOT be presented as input latency or callback stalls. Physical input latency,
other-page presentation modes and swipe qualification remain separate P0.2 work. No unreported ETW-loss claim is made.

### P3.6 Debug monitor face-copy lifetime (2026-10-11)

Current code retained a 753,424-byte heap-owned monitor face copy after hide and device loss. The preceding behavior
with truthful private ownership accounting fails the real-DLL hide regression; `runtime-2026-10-11-p36-monitor-scratch-control`
retains its source, Debug DLL/PDB/runner and failing log. The candidate uses one lazy virtual-memory owner, releases it
on hide/device loss/creation/final destruction, and reconstructs the latest grid before drawing. A fresh copy uploads
initialized opaque black even when the service has no composed grid. GPU texture and service-grid lifetimes are unchanged.
One cold allocation fault follows the existing preparation latch; 100 failed retries do no allocation/font work, and
changed extent recovers exact pixels. One hundred clean preparations reuse the copy without font/upload work.

Focused Debug WARP passes cold faces, hide/reshow, repeated hide/device loss, failed allocation/extent recovery,
unchanged retries, final zero ownership and exact recovery pixels. Independent process-memory sampling reports
753,664 private-commit bytes returned on hide, the 184-page rounded allocation for 753,424 payload bytes on this host.
Private monitor diagnostics grow from 32 to 40 bytes, with eight bytes of ownership/allocation counters and one fault
flag in the Debug module; Release remains a zero-resource diagnostic/fault stub. The 232-byte service record and
production probe remain unchanged. Final Debug and default-quarantine ASan tests report the same 753,664-byte release.
All 16 Release scopes, 15 Debug and 15 ASan native scopes, ARM64 Release compilation with zero warnings/errors, and
10 skill checks pass. Frozen native input hashes match the completed runs. Receipts: `runtime-2026-10-11-p36-monitor-scratch`
and `-control`. This is Debug-only resource ownership, so no additional Release hardware pair is required for this cut;
no Release CPU or whole-process return-budget improvement is claimed. P3.6's broader retained-cache attribution remains open.

### P0.4 composition accounting (2026-10-11)

The current 2560×720 / 96-DPI scene and production Matrix grid calculation give 182 columns, 128 active columns
and 5,248 instances per glyph draw on Focus; Gallery's 640×720 placement gives 45 columns, 26 active columns and
1,066 instances. The block and tail density calculations each round upward. These are calculated counts, not GPU
query measurements. With the default bloom, each Matrix widget issues two glyph draws and six draws in total.
The accepted D receipt reports successful host submissions of 60.150/s on Focus and 60.100/s during Gallery's
ten-second visit; the latter is not a separate settled 65-second baseline or a presented-FPS result.

Gallery's actual AV tile reports 1,474,560 surface bytes, matching 512×720×4. The production hardware capture below
confirms raising it to 1280×720 adds 3,686,400 bytes, for a total of 5,160,960.
The canonical AV WARP fixture instead uses a 640×360 tile and 1280×720 overlay and measures 4,608,000 bytes.
All six clean/dirty DPI scenarios retain those bytes; clean preparation is zero, and hiding releases both surfaces
with no further preparation/composition. `measure-av-views.ps1 -Configuration Release -SkipBuild` verifies the actual
linked archive against the current pin `271bd54be24eadf3f03ba5221d1be7be069860f2`, fingerprint
`3f78ff279d03b1172eca35216e33deca9bcfca3dd81b6c4ec85bad58efa754c0` and archive hash
`5A623134D8862241B74A32C6FE79754F020C39CB4B56D415092F1F0D2C075BC8`.

The canonical run's dirty frame-completion p95 ranges are 1.713–1.905 ms at 96 DPI, 1.795–1.920 at 144 and
1.799–1.873 at 192. An earlier direct run had 27.850–163.190 ms at 192 DPI; its repeat on the same executable had
1.853–1.953. All runs are retained; the outlier's cause is unresolved. These absolute offscreen measurements neither
change the accepted I19 adoption envelope nor establish hardware application latency or FPS. Receipts:
`runtime-2026-10-11-p04-accounting`, `-p04-accounting-repeat`, `-p04-accounting-canonical` and `-p04`.

The production raised capture uses the exact committed `793fe52` candidate, x64 Release 1.0.282, a 2560×720 / 96-DPI
XENEON window and AMD adapter LUID `00000000-0001C33F` owning that monitor. It disables services/tray in a portable
Release-template copy, posts only owned AV padding double-activation and Escape, and preserves the foreground HWND.
The helper and before/after input hashes are retained. Each settled cold/raised phase has 13 five-second samples
and about 60 s of complete deltas; dismissal/hidden/restore have five samples and about 20 s of deltas each.

| Phase | AV surface bytes | Private / working set MiB | Local usage MiB | CPU, percent of one core | Successful submissions/s |
| --- | --- | --- | --- | --- | --- |
| Cold Gallery | 1,474,560 | 92.21 / 93.12 | 54.24 | 2.630 | 60.034 |
| Raised | 5,160,960 | 97.99 / 93.30 | 59.24 | 2.317 | 60.136 |
| Dismissed | 1,474,560 | 93.21 / 93.34 | 54.24 | 2.891 | 60.100 |
| Hidden | 0 | 93.21 / 93.13 | 54.24 | 0.078 | 0 |
| Restored | 1,474,560 | 93.53 / 101.63 | 54.49 | 2.188 | 60.150 |

Every phase has one AV widget and the expected exact surface count; no settled phase navigates or becomes inactive.
Local adapter usage rises five MiB while raised and returns to cold usage after dismissal; hiding releases AV's
owned surfaces while the process's reported local usage remains 54.24 MiB. The latter is not proof of a surface
leak or full driver reclamation. Live Weather responses and transient thread counts remain unpinned, helper-process
cost is excluded, and no causal CPU saving, real AV interoperability or presented/input latency is claimed.
`runtime-2026-10-11-p04-raised-gallery` retains all seven phases, raw JSONL, scene, helper, context and identities.

### P2.8 / P3.6 CPU atlas lifetime cut (2026-10-11)

Before this cut, ProcessViewer and Weather retained an inline one-MiB CPU atlas after their last GPU user released it.
This independent cut replaces each with one lazy virtual-memory owner shared by its DLL's GPU users. Cold widgets
and last-user teardown retain zero atlas bytes; initialization failure after atlas construction also releases it.
Surviving users keep the same allocation, and revisit/device-loss re-acquisition uses the same rasterization.

The pre-fix viewer lifetime test failed as expected and its exact source is retained under
`runtime-2026-10-11-atlas-red`. Final Release viewer and Weather lifetime tests pass, including injected failure
after CPU atlas construction and a successful retry. All eight Weather WARP images match the previous Release
outputs byte for byte; Process Viewer Unicode pixels match across shared recovery and complete re-acquisition.

The matched D-cycle pair, `runtime-2026-10-10-p28-before` / `runtime-2026-10-11-atlas-after`, has identical scene,
window rectangle/DPI, platform and DxUi provenance. Only the two plugin DLLs change among runtime artifacts.
Cold Matrix private is 38.23 / 35.51 MiB; returned private 106.39 / 105.06 and working set 98.16 / 96.16 MiB.
Return-over-cold remains 68.16 / 69.55 MiB and does not meet the budget. Local usage remains 25.01 MiB cold and
46.26 returned. Returned submissions 60.150 / 60.102 per second, hidden/minimized zero; CPU 2.057 / 1.692% is one
pair and establishes no causal saving. Exact ownership tests establish release of both one-MiB CPU allocations;
whole-process bytes retain unrelated heap/image/driver costs. All 16 Release scopes, 15 Debug and 15 ASan Debug
native scopes, ARM64 Release compilation and 10 skills pass. Ordinary soaks retain the four-MiB private/eight-MiB
working-set guards; ASan retains its default quarantine with diagnostic byte reporting. Exact source, artifact and
gate receipts are saved in `runtime-2026-10-11-p28-atlas`. Preparation, dirty-cell uploads and font ownership remain
separate cuts, and no trim export is needed for these two CPU allocations.

### P3.6 source cut and remaining retention (2026-10-10)

Provider lookup publishes its external COM lease under the subscription lock. A last external release or subscription
removal requests one cold trim through the existing event. Only the acquisition worker can trim after it starts;
before then no collection exists and reclamation is synchronous. Retirement clears descriptor plans under lock and
destroys the source outside it. Revisit and shutdown checks cover source publication, event handle lifetime and cleared
outputs. The fixed facade remains until shutdown; this cut adds no thread, timer or per-frame sweep.

Tests use production source creation/revisit and controlled source/sink gates to prove external identity leases,
inactive/staged subscriptions, collection in progress, running/reserved callbacks and late releases after shutdown.
Ten Release repetitions produce one or two active-sibling callbacks, exactly one reserved callback, and one source
destruction: the active sibling may sample again before its release starts. Initial Release/ASan failures were an
overly strict exactly-one assertion for that still-active sibling; final gates use the corrected drain assertion.
An initial overlapping Debug Desk Clock soak peaks at 4,333,568 private bytes and fails the unchanged four-MiB guard;
the separate final run peaks at 4,071,424 and passes. All failed logs remain beside the final receipts. No guard was
relaxed. ASan keeps default quarantine and diagnostic process-byte reporting.

Matched build 282 scenes have equal initial-settings SHA-256. Before/after returned Matrix: private 124.72 / 110.34
MiB, working set 114.88 / 98.75, CPU 1.848 / 2.005 percent of one core, submissions 60.152 / 60.135 per second.
Cold private 37.11 / 38.59 makes retained private 87.61 / 71.75 MiB. Threads remain 43, modules 102, and local
adapter usage 47.26 versus cold 25.01; AV surfaces are zero. Hidden/minimized phases submit zero frames and retain
the memory reduction. These are source/artifact-bound production observations, not heap/driver attribution.

The GPU cut clears immediate-context bindings only at successful page retirement, after retiring widgets quiesce
and before their device resources are released. Hidden WARP tests bind otherwise unused shader, vertex/index and
constant-buffer slots, relinquish local owners and prove final destruction after cancellation/replacement/promotion.
The test alone flushes deferred driver work. Production adds no flush or per-frame reset. Captures inside Render
before flip-discard Present prove identical primary/incoming pixels, and failed adoption preserves bindings and
recovers. All 16 Release scopes, 15 Debug and 15 ASan Debug native scopes, ARM64 Release compilation and 10 skills pass.

Matched GPU before/after build 282 has equal initial-settings SHA-256, rectangle/DPI and rendering adapter. Cold
private is 38.14 / 37.88 MiB; returned private 107.68 / 106.96 makes retained private 69.54 / 69.08. Returned working
set is 97.36 / 97.37 MiB, handles 1376 in both, threads 43 and modules 102. Local adapter usage falls 47.26 / 46.26
MiB versus cold 25.01 in both; AV surfaces are zero. Submissions remain 60.152 / 60.133 per second. CPU is 2.004 /
1.406 percent of one core, but this single pair does not establish a CPU saving. Hidden/minimized phases submit
zero. The first after capture changed pages during the settled return phase and is rejected; the idle repeat is
accepted. Receipts: `runtime-2026-10-10-p36-gpu-before`, `-p36-gpu-after-idle`, `-p36-gpu-after` and `-p36-gpu`.
The residual memory and +21.25 MiB local delta keep the whole cold +4 MiB goal and cache attribution open. Arena work
must refresh the old 64 KiB cheap estimate: current compiler layouts show native state without its four-MiB process
buffer plus the cheap sample already occupy 363,840 bytes before output tables.

### Remaining-proposal reconciliation (2026-10-10)

The accepted CPU capture points first to the bulk NT walk, not per-process handle opens. The current walk owns a
four-MiB buffer but restarted at one MiB on every collection and doubled after a length mismatch. The single-capacity
cut removes that retry without changing published datasets; deterministic/real-DLL oracles and all required profile
gates pass. Standalone wall time improves; matched production CPU and submission savings remain
unestablished. A separate eight-column activity dataset remains an option for the two consumers that use only
PID/name or the leading seven columns, and for P4.1 value-prefix storage. It must preserve the later accepted
39-column `process.list` contract and one shared walk/history epoch. No CPU saving is established yet.

Settings pages and widget lists already use vectors. `sourceDocument` is the loaded-document authority for plugin
patch persistence; dropping it or re-reading an unverified concurrent edit would change correctness. Discovery also
validates contracts for configured plugins, including inactive pages, so mapping only the current page is not a
compatible storage-only change. Measure content-sized private settings and provider descriptors first.

Weather cache bodies are already lazy `unique_ptr` allocations: a cold cache does not contain eight 256-KiB bodies.
Used entries still reserve the maximum body size, so content-sized storage is a distinct measured candidate. Curl
handle reuse/coalescing must preserve cancellation, cache validators and failure cadence. Viewer/Weather atlas
ownership needs a concrete last-user release path before introducing a speculative module-trim export.

### P0.3 growth investigation (2026-10-10)

The preserved P2.2 five-minute Desk Clock diagnostic reproduces the cold +9.2 MiB private observation. A second
600-second run samples that exact candidate every five seconds. Relative to its 20.117-second sample, private
growth ends at 970,752 bytes and peaks at 3,674,112; working set ends at +2,437,120 and peaks at +4,222,976.
Most growth is in the first 15 seconds. Stable provider/widget/device counts and complete teardown support a
coarse warmed WARP guard of 4 MiB private / 8 MiB working set. This local trace does not supply heap/driver stack
attribution or establish hardware budgets. Receipts: `runtime-2026-10-10-p03-calibration` and `-p03-desk-trend`.

The initial ASan Studio Clock guard fails at +9,637,888 private bytes; a quiet default-quarantine repeat fails
at +10,055,680, with stable object/resource counts and 60/61 scheduled frames. The same binary with only
`quarantine_size_mb=0:thread_local_quarantine_size_kb=0` peaks at +86,016 bytes. This diagnostic supports separating
instrumented allocator retention from the ordinary process-byte guard, without attributing allocation stacks.
The full ASan gate keeps default quarantine, raw memory samples and all functional/lifetime assertions; only its
process-byte budget is diagnostic. These failed runs remain in `runtime-2026-10-10-p03-asan-calibration` and the
initial full log. Release/Debug apply the ordinary 4/8 MiB peak guard. No `ASAN_OPTIONS` was configured for the
default runs; only the two quarantine options differ in the diagnostic.

Final current-source gates: 16 Release scopes, 16 Debug scopes, 15 ASan Debug native scopes, ARM64 Release compilation
and 10 skills pass. Final Release private/working-set peaks are Matrix 0/0, Studio 1,306,624/1,916,928 and Desk
942,080/290,816 bytes. Debug peaks are 0/0, 2,166,784/1,613,824 and 344,064/360,448. All retain their fixed resource
counts and release device resources. ASan functional checks pass with diagnostic private peaks of 8,060,928,
10,141,696 and 175,292,416 bytes respectively; this is not a zero-allocation or leak-attribution claim. The
default-quarantine full gate remains intact. Exact candidate/artifact identities, final/failed logs and all
per-profile soak logs are retained at `runtime-2026-10-10-p03`; the final logs are named `p03-*-final.log`.
The Studio Clock `frames` counter includes the already-displayed warmed boundary frame; measured Render calls
are that count minus one. Desk Clock and Matrix count measured Render calls directly. None is a hardware presented-frame counter.

### P2.2 matched comparison (2026-10-10)

Both captures use Release 1.0.278, the same portable scenes, XENEON rectangle/DPI, owning AMD adapter, AC,
services `{}`, tray off and five-second probe. Each capture has unchanged source/artifact identities internally;
the after candidate contains P2.2. No builds or tests ran during either capture. Thread CPU is cumulative Windows
thread time sampled every two seconds and attributed to the HWND-owning UI thread, not WPR stack attribution.
The analyzer uses samples within phase seconds 5–65; intervals differ slightly from the probe's complete intervals.

| Scenario | Process CPU before / after, % of one core | UI CPU before / after | Submissions/s before / after |
| --- | --- | --- | --- |
| Focus | 2.082 / 2.213 | 1.486 / 1.491 | 60.069 / 60.167 |
| Gallery | 3.522 / 2.473 | 2.843 / 1.811 | 57.356 / 60.151 |
| System | 3.013 / 3.252 | 0.522 / 1.017 | 10.090 / 11.488 |

The Gallery control has a 1.46-second probe-query outlier and fewer complete intervals; its apparent improvement is
not causal evidence for P2.2. Focus is unchanged at this precision; System changes with real observations and warning
pulses. All retained steady PresentMon rows are `Hardware Composed: Independent Flip`. Focus frame-time p95 is
16.786/16.790 ms, p99 19.352/17.242 ms; this is a diagnostic, not acceptance of the proposed strict frame budget.
System's long frame gaps are event-driven idle periods. No input-to-photon observations were generated. WPR CPU
stacks remain unavailable (`0xc5585011`). Raw CSV, cumulative thread records, analyses, settings and probe identities
are retained in the two capture directories; `.build/receipts/runtime-2026-10-10-p22/` holds validation identities.

P2.2 removes a known repeated API walk without altering reachable-band geometry or introducing a worker/wake.
Next independent storage cut is P3.2; most observed System CPU remains on a non-UI thread whose role needs stack
attribution before treating it as proof of a particular collection cost.

| Area | Verified current code | Consequence for this plan |
| --- | --- | --- |
| Device/presentation | `Renderer::FindAdapterForMonitor`, `EnsureDeviceForWindowMonitor`, unconditional `RecordDeviceIdentity`, waitable flip swap chain; `Application::CheckDeviceAdapter` and `WaitForFrameLatency` | P1.1–P1.3 implementation exists. Fresh adapter/PresentMode/input-latency attribution is P0.2, not a probe counter. |
| Chrome/native widgets | Both top-level styles use `WS_EX_NOREDIRECTIONBITMAP`; `DashboardHost` creates layered native containers. Release Gallery has Triangle and Matrix, no GdiOrbit. | The September P1.4 rejection is superseded. Two continuous widgets share one host presentation cadence; no template change belongs to P0.1. |
| DxUi/AV | `Dependencies/DxUi.lock.json` pins `271bd54be24eadf3f03ba5221d1be7be069860f2`, API 3. Views release hidden surfaces; caches/peaks are bounded. The I19 adoption envelope was accepted on 2026-09-13. | Old sibling-worktree and library receipts are dated history. AV real IME/AT/audio/camera gates remain with the HOLD RFC. P0.1 exports only 64-byte read-only POD statistics. |
| SystemData | Default viewers use eight-column `process.activity` with zero native per-PID opens; legacy 39-column detail remains. Both share one walk/history. One 16-MiB reservation commits acquisition families/output tables on first use: 4 KiB cold, 9.461 MiB shipped, 15.766 MiB full. P3.6 reclaims unleased sources after collection/callback quiescence, retaining facade identity. | P2.1 and P4.1 are complete, preserving activity/detail contracts, caps and rollback. Constructor/default-commit targets pass; settled D has zero source arenas/native owners. Remaining return/cache attribution stays in P3.6. |
| ProcessViewer | P2.7 parks settled viewers (`S_FALSE`), snaps unchanged/sub-pixel targets, and schedules only visible easing/pulses at 33 ms. Eases use a fixed start pose and one host timestamp for both views; auxiliary observations do not restart them. `PluginHost::Deliver` still coalesces the initial invalidation. | Matched control: 14.126 submissions/s, 2.986% of one core. Two after-runs: 12.163/12.184 submissions/s, 3.515/3.069% CPU. Submission reduction is 13.7–13.9%; CPU improvement is not established. Proposed System budgets remain open. |
| Logging/settings | P2.5 caches the writer's UTC date and swept policy; ordinary lines perform no directory enumeration or path allocation. Settings persistence flushes its temp file and checks the loaded document before replacement, but still runs synchronously. | P2.5 is complete. P2.6 must preserve the current flush, merge and stamp guarantees; the old proposed write-through replacement is obsolete. |
| Retained plugin work | Launcher count-sized icons, glyph batching, single shell output, folder binding, verified-file opaque reuse and combined queries remain production optimizations. Eight decoded entries retain 2 MiB. Ordinary parsing remains after rejected shortcuts. Offscreen discrete navigation now uses copied CPU jobs on the host STA, stale-generation rejection and a full-quality upload barrier, with a short cache SRW lock. Initial/touch setup remains synchronous. Viewer/Weather prepare changed glyphs; DeskClock retains its 1-ms animation deadline. | Both sealed Release orders remove Launcher UI visibility stalls with exact raw pixels; whole-page readiness and process memory are mixed. Profile accounting and the one-thread cold resource cost are recorded below. Physical first-pixel, full-page/retention and P4.6 budgets stay open; prior failed guard/hash receipts remain. The September cost/line-number table is historical. |
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
| F. Mid-swipe peak Gallery↔System | settled + 8 MiB | | stage ≤ 2 ms UI-thread CPU per swipe; no dropped frame from first finger movement to settle | one process-scoped source; no activation for blocked/canceled-before-stage gestures; a shown neighbor may legitimately own an icon array and fetch while live |
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
| P0.3 | **Automated warmed soaks implemented 2026-10-10; intermittent clock qualification remains open.** The three isolated 60 s variants run in the HostPlugin scope. Twenty scheduled seconds warm the clocks; Matrix keeps its 1,024-frame warm-up. Five-second/end samples guard uninstrumented peak growth at 4 MiB private / 8 MiB working set. ASan retains default quarantine and functional bounds, with diagnostic byte reporting. | The owned diagnostic attributes fluctuating WARP pool payload in its recording. Fresh post-pacing Release Full still fails the Desk guard at 4.14 MiB; an unchanged-closure affected repeat passes. Debug/ASan native Full pass. Neither repeat nor the prior trace explains the failed peak; longer plugin qualification remains separate. |
| P0.4 | **Current grid, tile, raised Gallery and pinned-library accounting captured.** See the 2026-10-11 accounting checkpoint. | Source-calculated Matrix counts, settled hardware Gallery submissions, exact tile/raised/dismissed/hidden/restored AV bytes and linked-archive identity agree. Presented FPS/input latency remain unqualified. |

### Phase 1 — render on the GPU that owns the panel; never block on Present

| ID | Item | Pass condition |
| --- | --- | --- |
| P1.1 | **Adapter-of-output device.** `Renderer::CreateDevice` takes the window's `HMONITOR` (`MonitorFromWindow(_window, MONITOR_DEFAULTTONULL)`), enumerates `IDXGIFactory1::EnumAdapters1` → `IDXGIAdapter1::EnumOutputs` → `IDXGIOutput::GetDesc().Monitor`, skips `DXGI_ADAPTER_FLAG_SOFTWARE`, and creates with `D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, …)`. Stores the adapter LUID and monitor. Falls back to the default-adapter path only when no output matches (hidden test HWND at −32000, headless), then WARP as before. **Landed 2026-09-07:** `RedXe/AdapterSelection.h` (pure policy), `Renderer::FindAdapterForMonitor`, `Renderer::RecordDeviceIdentity` (the `device-created` record), `Renderer::DeviceInfo`; `HostPluginTests` `TestAdapterSelectionPolicy` and `TestRendererDeviceIdentity`. **Receipt (Release on the live XENEON, `.build/receipts/2026-09-07-adapter-of-output.md`):** `device-created` = AMD Radeon Graphics, LUID 0001C3CC, adapter owns window monitor: yes; only `amdxx64.dll` mapped; one LUID in `GPU Process Memory` (22.3 MiB) and `GPU Engine` (AMD 3D 13%); Matrix Focus settles at 40.1 MiB private bytes, 61.7 MiB working set, 42 threads, 1,127 handles, 2.5% of one core (was 147 threads / 153 MiB after all pages, 75 MiB Matrix-only on the old path). | `device-created` reports `adapter owns window monitor: yes` (met); GPU counters show one LUID (met); no `nvwgf2umx.dll`/`nvgpucomp64.dll` mapped (met); threads −37 or better (met: −105 vs. the all-pages baseline); PresentMon `Hardware: Independent Flip` and UI-thread CPU < 1% on Matrix Focus **still to capture**: PresentMon 2.5.1 is installed, but the CLI needs elevation and the 2026-09-07 elevated attempts hung behind pre-existing `PresentMon` / `Gv_PresentMon` ETW sessions; capture from an elevated shell with `presentmon --session_name RedXeProbe --process_name RedXe.exe --output_file <csv> --timed 12 --v2_metrics` once those sessions are stopped, or read PresentMode from the PresentMon GUI. 2.5% process CPU was measured at 60 fps continuous. |
| P1.2 | **Device follows the window.** `Renderer::EnsureDeviceForWindowMonitor` compares the LUID of the adapter owning the window's current monitor with the device's; `Application::CheckDeviceAdapter` runs it on `WM_MOVE`, `WM_EXITSIZEMOVE`, `WM_DISPLAYCHANGE`, and after `WM_DPICHANGED`; a mismatch runs the existing `CreateDeviceResources()` rebuild (the same path `RecoverDevice` uses, which already notifies widgets through `OnDeviceLost`/`OnDeviceCreated`). Release popup, Debug titled window, and XENEON re-placement all use the same rule. **Landed 2026-09-07 with the live receipt:** moving the Debug titled window onto the RTX 5080 display logged `device-created` on the NVIDIA (LUID 0001AE51, owns window monitor: yes), moving it back logged the AMD again; the process stayed alive across both rebuilds and closed with exit code 0. | Dragging the Debug window from the 4K display to the XENEON and back rebuilds the device on the target adapter each time (log shows two `device-created` records, LUIDs differ); no rebuild on moves within one adapter. |
| P1.3 | **Waitable swap chain.** `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` (also passed to `ResizeBuffers`), `IDXGISwapChain2::SetMaximumFrameLatency(1)`, device-level latency call removed. `Application::WaitForFrameLatency` waits on the waitable handle together with the message queue before `PrepareWidgets`; a message arriving first is dispatched before the frame is built; `Present` no longer blocks. The idle path (`WaitUntilMessage`) and the scheduler table are unchanged. **Landed 2026-09-07**; `TestRendererDeviceIdentity` proves the object exists, is signaled before the first frame and after a presented frame, survives resize, and is released at shutdown. | ETW shows no blocking inside `Present`; PresentMon input-to-present ≤ 2 frames; `HostPluginTests` scheduler table green. |
| P1.4 | **Landed later through HostChromeWithoutGdi.** The top-level window uses `WS_EX_NOREDIRECTIONBITMAP` and native containers are layered children; the September plain-child rejection is historical. | Current code/tests preserve layered native rendering; per-page PresentMode remains P0.2. |
| P1.5 | **Template part landed.** GdiOrbit is opt-in and absent from shipped pages. Triangle and Matrix on Gallery share one host present stream; continuous widget count alone does not establish PresentMode. | Record per-page hardware PresentMode; any removal or cadence change is a product decision. |
| P1.6 | **Complete: optional `continuousFrameRate: "display" \| 30`, display by default.** A message-blocked one-shot deadline overlaps acquired DXGI readiness and preserves its lifetime through hide/resize/live toggles. Input invalidation and page/overlay/dock motion bypass the cap; Matrix Focus stays first and its quality/settings/shaders are unchanged. | Parser/schema/templates/docs, native 60/120-Hz/message/timer/permit tests and production recovery pass. Final pairs establish 32.8/35.6% lower CPU and about 51% fewer submissions; cap process private +0.59/+1.15 MiB is recorded above. Full scopes, ARM64, skills and reference images pass. Physical input latency remains P0.2. |

### Phase 2 — make settled pages idle

| ID | Item | Pass condition |
| --- | --- | --- |
| P2.1 | **Complete: single-capacity query and compact activity consumers.** Query the owned four-MiB buffer once. Default viewers consume eight leading fields with zero native per-PID opens; legacy 39-column detail, strict parsing, caps, quality and Toolhelp fallback remain. Combined requests share one walk/history and borrow leading values. | Red/green, live native zero-opens, both request orders, PID reuse/rates, abort recovery, row/name caps and fallback pass full Release/Debug/ASan gates, ARM64 compilation and skills. All 145 Viewer pixels match. Counterbalanced CPU is lower in both observations, but live data is unpinned and submission direction reverses; no causal saving claimed. Fixed storage grows 49,296 bytes for compatibility; lazy commitment remains P4.1. |
| P2.2 | **Complete 2026-10-10.** Cache non-dock reachable geometry; notification invalidation precedes all message handlers. Dock retains its direct client rule. | Production self-test: 1,000 unchanged queries, zero extra display walks; work-area notification adds exactly one. Geometry/policy tests and full Debug/Release gates pass. Matched comparison above establishes no CPU saving. |
| P2.3 | **Complete 2026-10-10.** Fixed atomic slot bindings remove locked string lookup from placeholder decisions. Cold registration/release and report/reason lookup retain the bounded table. | Reads complete while the status lock is held; full Release/Debug/ASan gates, capacity/concurrency/reuse/reconfiguration and rollback lifetime tests pass. No whole-process CPU improvement is claimed. |
| P2.4 | **Complete 2026-10-10.** Debug output shares the render-failure transition guard. A recovered drawn frame permits a fresh failure report; partial raised/tile success does not. Successful primary adoption resets slot identity. | 400 failing primary frames emit three diagnostics and preserve healthy siblings; the incoming instance reports once. Debug/Release full gates pass; ARM64 Release compiles. |
| P2.5 | **Complete 2026-10-10.** Writer-owned UTC date/policy cache: sweep on open, date change or changed retention value. Policy updates wake the idle writer and synchronize with flush; ordinary lines construct no path. | Same-day, unchanged/shortened policy, forward/backward UTC correction and actual idle-worker flush tests pass in Debug/Release; ARM64 Release compiles. Removes two enumerations per ordinary line; adds 20 bytes of cached date/policy, no thread or wake. |
| P2.6 | **Promotion premise rejected against current code.** `PromoteTransitionPage` adopts the incoming owners, updates only the active selector, refreshes layout and retires the old page; it performs no settings save. Widget and dock persistence elsewhere remain conditional candidates if their own measured stalls justify a bounded worker. Temp-file flushing and loaded-document/stamp/merge protections already exist; preserve them. | Do not add a persistence worker to promotion without a real write/stall trigger. Any separate persistence change must preserve concurrent edits, shutdown, failure recovery and stamps. |
| P2.7 | **Implemented and measured 2026-10-10: ProcessViewer wake discipline.** Settled viewers park; unchanged/sub-pixel values snap; real 320 ms easing and 420 ms pulses use a 33 ms deadline without immediate requests. Fixed-pose interpolation preserves tile/raised timestamps, ranks and endpoints. Tests cover all ten types, auxiliary deliveries, bursts, recovery, allocation and a scheduled production-host soak. | Fresh matched 14.126/s becomes 12.163/12.184/s (13.7–13.9% lower). CPU 2.986% becomes 3.515/3.069%; no CPU saving established. Proposed ≤2/s and ≤1.5% process CPU remain unmet; UI-thread ≤0.3% is unmeasured. Keep budget acceptance open and use P0.2 attribution to choose further work. |
| P2.8 | **Complete: CPU lifetime, Viewer/Weather/Debug monitor preparation/batching, failure suppression and capacity recovery.** Dirty 48×48 cells upload from Prepare, paint stays frozen across delivery and Render, and one temporary font session serves each changed batch with zero steady ownership. Reused cells clear old ink, Weather recycles above its reserved rows, and shared linear sampling cannot fetch neighboring ink. Debug hidden/unprepared input waits, while held Up/Cancel still releases. | All 145 Viewer references and eight Weather PNGs match; real-DLL red/green and final Release/Debug/ASan scopes, ARM64 compilation and skills are recorded above. Counterbalanced Gallery and System observations establish no causal CPU saving or repeatable regression. A trim export remains conditional on actual retained ownership under P3.6. |
| P2.9 | Measure callback drain stalls before redesigning waits. Network and subscription drains include `INFINITE` waits; a timeout cannot free state while a callback can still use it. Reuse current bounded/tombstoned control/device-lane patterns only where ownership permits. | Slow-fixture recovery bounds UI delay while preserving callback quiescence, cancellation and teardown lifetimes. |

### P3.4 continuation — host-owned STA construction (2026-10-12, foundation implemented)

The ordinary native Shell path remains the quality reference after the rejected parent metadata/parse-create
experiments. Its indivisible parent creation, handler binding and fresh shield/location calls cannot fit a UI
slice merely by moving them into `Prepare`. The next mechanism is one lazy host-owned STA construction lane:
16 immutable job slots, duplicate coalescing without reruns, a three-second queue-to-result budget, UI-posted
completion and cancellation checked between Shell calls. An indivisible native call can outlast cancellation;
shutdown waits at most one second once and retains runtime/work/module storage until it exits or process end.
Private test owners join before releasing their storage. This does not change the existing MTA device-work lane.

The current foundation adds the queue, in-place host ABI service and session-end/shutdown lifetime handling.
Production Launcher extraction still uses its existing synchronous path at this foundation checkpoint. No Launcher
latency saving, full-quality incoming-page readiness, first-pixel qualification or plan completion is claimed here.
Integration must copy bounded CPU inputs/results, reject stale/canceled generations, preserve fresh shield checks
and native pixels, and keep the current page animated until a full-quality directional neighbor is ready. It must
retain the current-plus-one page bound, cancellation/reversal behavior and device-loss recovery.

`HostPlugin.Tests.ConstructionWork.cpp` exercises real STA execution, blocked-worker owner responsiveness, fixed
capacity/refusal, duplicate execution counts, queue-age expiry, late-success refusal, cooperative cancellation,
bounded repeated stop, UI completion that stops the lane, final-release thread, idle CPU and production-host
posting/module retention. The frozen pre-change control is `5086a8c` with 713 source closure files; the copied
runtime/evidence root and its 244 file hashes are recorded in `.build/receipts/p34-construction-control.json`.
The focused Debug and Release runs pass 53 checks, including a queue-owned final reference released on the UI
thread and zero measured worker CPU over a 250-ms idle interval. Fresh Debug and ASan Debug native Full runs pass
all 15 scopes. Release's first Full run reaches the end-to-end screenshot check and exits with `0xc0000005`;
the preceding native suites/resource guards pass, and a fresh explicit HostSmoke repeat passes capture, command
line and both crash harnesses. This is partitioned coverage, not a passing first Full run. Twenty-four alternating
control/candidate captures under an isolated native debugger produce PNGs without reproducing the fault. The
previous unexplained screenshot failures and this failure remain open; debugger timing and a successful retry
do not explain their cause. ARM64 Release compiles with zero warnings/errors; this is not native ARM64 execution.

Compiler PDB records and the queue test establish 464 bytes of added fixed storage per process host: `PluginHost`
is 99,120 -> 99,584 bytes. The queue creates no events or worker before first submission; production Launcher does
not submit at this checkpoint. The matched, owned nonactivating 2560x720/96-DPI XENEON Release D-cycle has 18 stages
and 402 callbacks per arm, identical scenes, collector, power/OS/DxUi context and sealed runtime inputs. Cold
Matrix CPU is 2.192 -> 2.193% of one core and presents are 60.024 -> 61.301/s; returned CPU is 2.525 -> 2.550% and
presents are 60.071 -> 59.707/s. Cold private bytes are 35.31 -> 36.09 MiB and returned bytes 63.85 -> 65.82 MiB;
these varying process totals do not measure the 464-byte object addition or establish a causal regression/saving.
Hidden and minimized presentations are zero in both arms. Launcher visibility remains synchronous (median
41.897 -> 37.502 ms, maximum 231.485 -> 146.333 ms); this unchanged path's variation is not an optimization.

Evidence is retained under `.build/receipts/p34-construction-*`: native/artifact manifests, profile build/test logs,
the failed screenshot log and successful repeat, debugger outcomes, PDB type dumps, skills/tooling logs and
`p34-construction-matched-comparison.json`. Full-quality readiness, Launcher integration, physical input and
first-pixel qualification remain separate next work. The plan stays active.

### P3.4 continuation — Launcher construction integration (2026-10-12, implementation and matched qualification)

Discrete wheel, edge and named page navigation now stage Launcher CPU construction on the host's single STA.
The current page remains at rest and keeps its required animation until every incoming widget has full-quality
pixels uploaded, or reports a local unavailable tile. The ordinary shared ease-out begins only then. Initial
activation and follow-finger touch staging retain synchronous full-quality construction. A three-second incoming
page deadline cancels navigation and retires the unexposed neighbor. Starting a touch gesture during that wait
also retires it before normal directional staging. There is no snapshot, frozen animation, reduced resolution,
partial icon grid, per-widget worker or polling timer.

Launcher jobs own copied, bounded identities and CPU results. Native COM owners are created and destroyed in the
STA call; no widget, provider, immediate context or host mutation enters Run. Completion rejects retired or
replaced generations on the UI thread. Pin import validation and the bounded settings queue remain on that
thread. Rejected imported bindings get the same Warning pixels during Prepare. The existing eight-entry,
two-MiB cache gains a short SRW lock for bookkeeping and pixel copies, never held over Shell calls or file probes.
Fresh shield/location checks and ordinary extraction/decode quality remain intact. One numeric completion log
attributes queue delay, worker CPU/wall time and native phases without logging shortcut targets.

The shipped Launcher passes 28 checks covering exact glyph/native/warning/drop-rollback pixels, replacement,
hide and retired owners, admission refusal/retry, import publication, device loss, native-phase cancellation and
pending input rejection. The production host WARP fixture passes nine checks with 32 incoming Launchers: the
blocked STA leaves the current GPU page rendering, a full 16-slot queue admits deferred widgets through posted
completions, and the first-preparation mask survives until all 32 widgets are ready. The foundation suite now
passes 54 checks, including refusal without creating a worker during ordinary activation.

The frozen foundation control is `845f675`; its 716 source and 144 runtime hashes are retained in
`.build/receipts/p34-launcher-control.json`. The final native closure has 472 inputs. Fresh x64 Debug and ASan Debug
native Full runs pass all 15 scopes, including screenshot and crash checks. Release's first native Full fails the
Desk Clock private peak guard: 5,308,416 bytes (5.06 MiB) against four MiB; the working-set guard and stable clock
object/device counts pass. Its preceding suites, Matrix and Studio guards pass. An explicit remaining HostSmoke
run passes WARP, both screenshot outcomes, help/error and crash checks. Four isolated clock observations in
control/candidate/candidate/control order pass, with private peaks 647,168 / 0 / 2,760,704 / 2,940,928 bytes.
This is partitioned scope accounting, not a passing original Release Full or an explanation of that failure.
The clock source is unchanged; rebuilt PE `.text`, `.data`, `.pdata`, `.rsrc` and `.reloc` sections match the control.
DLL/PDB identities still differ, and a passing retry does not attribute the earlier peak. ARM64 Release compiles
with zero warnings/errors; no native ARM64 run is claimed. Ten repository skills and the final independent
BuildProcess scope pass, including 30 scoped-testing cases. Final source and all four profile artifact manifests
remain unchanged from their successful obligations; the failed original Release clock guard remains open.

The canonical large-icon comparison retains a failed strict hash result for URI/stock fallbacks. Independent
native WARP readbacks through the production STA Run/UI completion/Prepare route prove all 26 icon slices and ten
rendered DPI/size frames byte-identical, with exact alpha and zero maximum channel difference. Fifteen ordinary
glyph/empty frames also match. The established fallback-only one-unit tolerance remains, but these raw captures
need no tolerance. The ordinary WIC decoder source is unchanged. No resolution, shadow, typography or animation
quality is reduced. Both the failed strict receipt and accepted raw readbacks are retained.

Each owned Release D-cycle has 18 stages and 402 callbacks, six successful eight-icon STA jobs in the candidate,
and no construction job in the control. Both orders use identical initial/final scenes, collector, pins and
external icon bytes, power/OS/display context and DxUi provenance. Every capture preserves its sealed source and
runtime hashes. The XENEON is 2560x720 at 96 DPI on AC power; navigation is nonactivating messages.

| Observation | Control -> candidate, forward | Control -> candidate, reverse |
| --- | --- | --- |
| Launcher UI visibility median / maximum | 68.354 / 991.565 -> 0.035 / 0.104 ms | 73.821 / 355.077 -> 0.033 / 0.086 ms |
| Launcher first GPU preparation median | 0.490 -> 0.460 ms | 0.465 -> 0.367 ms |
| Gallery synchronous staging median | 102.937 -> 136.185 ms | 107.911 -> 30.959 ms |
| Gallery setup through logged full readiness median / maximum | 106.437 / 1,240.017 -> 272.740 / 742.882 ms | 111.411 / 695.829 -> 93.662 / 428.917 ms |
| Cold Matrix process CPU, percent of one core | 3.021 -> 2.510 | 2.926 -> 2.487 |
| Returned Matrix process CPU, percent of one core | 2.673 -> 2.599 | 2.920 -> 2.525 |
| Cold / returned submissions per second | 59.774 / 59.628 -> 59.836 / 59.834 | 60.551 / 59.392 -> 59.990 / 59.817 |
| Cold / returned private bytes | 36.43 / 66.37 -> 36.94 / 68.11 MiB | 35.72 / 66.74 -> 36.49 / 66.50 MiB |
| Cold / returned process threads | 42 / 43 -> 42 / 44 | 42 / 43 -> 42 / 44 |

The selected improvement is removing Launcher native work from the UI callback during discrete navigation.
Repeated STA jobs take 57.423 / 55.126 ms median in the two orders; their fresh shield phases take 29.231 / 27.271 ms,
binding 15.836 / 15.358 ms and parent creation 6.542 / 7.047 ms. Repeated extraction/decode is zero in this cached
scene. Queue delay is zero for all six jobs in each order. Work still occurs and full readiness is mixed; this does
not establish faster whole-page navigation, physical input-to-photon, a four-ms page slice or causal steady CPU
savings. Millisecond completion logs are readiness observations, not first displayed changed pixels. Hidden and
minimized submissions are zero in every arm, and no construction timeout occurs.

The intentional resource cost is one lazy, event-blocked process STA and two queue events after first incoming
Launcher activation. The process thread difference is one in both return observations; private-memory differences
are +1.74 and -0.24 MiB, so neither an exact process-memory charge nor a saving is established. Release compiler
records prove the host itself stays 99,584 bytes, `PluginManager` grows by 256 bytes per page, `Application` by
16 bytes and each Launcher by 40 bytes. A lifetime record is 16 bytes plus shared-control/allocator overhead; each
job has 65,000 fixed bytes plus up to eight MiB of CPU pixels for 32 icons. Queue capacity is 16; an incoming page
can own up to 32 bounded jobs including deferred admission, so 16 slots is not a bound on all job/result payload.
Jobs/results release after publication or stale rejection. The existing two-MiB cache adds an eight-byte SRW lock
and no second cache. This bounded cold cost keeps the current page animating while indivisible native work runs;
it is not an allocation on Render or a periodic idle wake. Whole D cold-A +4 MiB and thread-return proposals remain
unmet. Initial/touch GPU/native setup and the remaining full-page resource/first-pixel costs stay open.

The first agreed foreground sequence stops safely after four verified outcomes when another application gains
focus; its rejected receipt remains. The agreed repeat verifies all twelve wheel/page/raise/dismiss/blocked-edge
outcomes, with normal app/PresentMon exits, unchanged source/runtime identity and 4,671 owned frames. The first
two action cohorts include composed flip as well as hardware-composed independent flip; later cohorts use the
latter, with no hybrid presents in the joined cohorts. Input-to-photon columns are unavailable. Next-submission
delays range from 0.008 to 34.713 ms and are not first changed visual pixels or physical-touch latency. Receipts are
`runtime-2026-10-12-p34-launcher-focused` and `-focused-repeat`; only the repeat is passing interaction evidence.

The approved allocation attempt runs a temporary byte-identical Release HostPluginTests alias as a normal user.
WPR start reports success, but stop/cancel reports no active profile (`0xc5583000`) and no ETL is saved. The
controller's failed cleanup result remains; independent read-only verification proves the owned recorder is
inactive and its unique IFEO image key is absent. This yields no allocation attribution. A prepared replacement
keeps the recorder lifetime separate from a command's containment job, verifies profile status before publishing
readiness and during capture, and retains the fixture stdout even on recorder failure. Its execution needs a new
single-prompt UAC agreement at that checkpoint; the approved replacement result is recorded below. The cause of
the first disappearance is not established from the changed helper.

Receipts are `p34-launcher-native-inputs.json`, profile manifests/logs, `p34-launcher-qualification.json`,
`p34-launcher-final-closure.json`, the strict
and qualified pixel comparisons, clock guard/binary proofs, both matched comparisons,
`p34-launcher-qualified-comparison.json`, the foreground captures and `runtime-2026-10-12-p34-clock-allocation`
under `.build/receipts/`. Physical touch, first-changed pixels, residual process retention, clock peak attribution
and the unexplained earlier screenshot fault remain open. The plan remains active.

### P3.4 remaining GPU setup attribution (2026-10-12)

Production Launcher adoption is committed as `31caca3`, after foundation `845f675`. The same two accepted
candidate captures expose the remaining synchronous `OnDeviceCreated` wall time; each has 18 stages and 402
callbacks. Shaders medians are 26.761 / 21.981 ms (three calls per order), Desk Clock 14.506 / 15.492 ms,
Weather 17.647 / 13.724 ms and System Pulse 12.818 / 14.176 ms (six each). AV Control medians are
0.927 / 0.864 ms, but its largest calls are 200.246 / 165.607 ms. All these calls succeed. These are whole
callbacks, with quantized thread CPU counters: they do not separate CPU rasterization, D3D resource creation,
driver work or scheduler delay. They do not establish an internal cause or a first-pixel result.

The next construction investigation must separate those phases before changing shared atlas/resource lifetime
or moving more work. The clock allocation trace addresses the separate intermittent WARP private peak. Neither
a passing repeat nor this callback table closes that guard. `.build/receipts/p34-residual-gpu-attribution.json`
binds the original summary hashes and retains both orders; no additional build or runtime arm is credited.

### P3.4 Shaders device setup attribution and background rasterization (2026-10-13)

Call-local shipped-DLL seams now exercise the same setup implementation as `OnDeviceCreated`, with six non-overlapping
QPC phases and no retained timing state. Production passes no timing record. The pixel seam exports exactly 349,525
RGBA texels across the ten production background mip levels, refuses undersized storage before writing and preserves
caller sentinels. Native tests cover refused records/context, bounded pixels, phase/result accounting and owner
retirement, while retaining all catalog rendering and lookup-table tests.

Release control samples run on one isolated device per backend, with one first setup and 31 repeated fresh slideshow
setups. Each call retires its temporary widget/shared owners; the fixture clears its dedicated context afterward.
The default hardware device is the RTX 5080 (`00000000:0001ADF4`), not the XENEON's owning AMD adapter. These are
native setup observations, not a displayed-page or input-latency baseline:

| Phase | WARP repeated median (ms) | RTX 5080 repeated median (ms) |
| --- | ---: | ---: |
| Total, including retirement | 23.388 | 18.149 |
| Core D3D resources | 5.834 | 1.798 |
| Background pixels | 15.258 | 15.165 |
| Background mips | 0.278 | 0.276 |
| Background upload/view | 0.254 | 0.469 |
| Constant buffer | 0.031 | 0.023 |
| Lookup-table bake API calls | 1.041 | 0.016 |

Background pixels are the measured target: the original routine tests all 56 lights at each of 512x512 pixels.
The candidate uses one reusable 6-KiB RGB stack row and conservative two-radius light ranges around the unchanged
1.9-radius contribution cutoff. It keeps the original per-pixel light order, arithmetic, reflections, conversion
and mip filters. No lower resolution, changed lighting, new heap allocation or retained cache is selected. The
temporary row's stack lifetime is bounded; stack commitment can retain its high-water pages. Initial sequential
Release diagnostics put the background medians at 1.264 / 1.256 ms (WARP / RTX 5080), with total setup including
retirement at 8.826 / 3.562 ms. These first diagnostics are separate from the counterbalanced hardware comparison.

All 349,525 texels across all ten actual old/new DLL mip levels match byte for byte in Release, Debug and ASan Debug:
1,398,100 bytes, zero changed bytes, SHA-256 `a137ee14a99ee73faf16f0a715b851cb5e3a4efa492315ba9aa2dbc7beedf88e`.
The regular PluginContract scope also compares the candidate with an independent frozen pixel-major oracle;
it does not reuse the production row/range implementation. All original catalog, lookup-table, resource and
allocation-free rendering checks remain. New files follow the native inventory and `Scope.Tests.Something` naming.

Fresh Debug and ASan Debug native Full runs each pass all fifteen scopes, including WARP, screenshots and crash
checks. Sanitizer quarantine remains enabled and its process-byte reporting is diagnostic. ARM64 Release compiles
with zero warnings/errors; native ARM64 execution remains unavailable. All ten skills pass. The original Release
Full reaches the Desk Clock soak and fails its unchanged 4-MiB private-growth guard at 4,341,760 bytes (4.14 MiB);
working-set growth is 2,560,000 bytes and object/device/draw/hidden/teardown checks pass. Its explicit remaining
HostSmoke scope passes. This repeats the intermittent guard class after fixture pacing was corrected, without
explaining either this peak or the earlier 5.06-MiB peak. The clock DLL and host test executable are byte-identical
to the frozen control. An unchanged-source/artifact Release HostPlugin repeat passes all host checks and three
guards: Matrix peak 933,888 / 159,744 bytes private/working set, Studio 2,375,680 / 1,540,096 and Desk
991,232 / 180,224. Desk renders 1,653 frames, with stable resources, bounded draws, hidden zero work and complete
teardown. All fifteen Release native scopes are accounted for by the original Full, affected host repeat and
remaining HostSmoke; the original Full remains a failed run. A passing repeat does not explain the failed peak.

The control keeps the original rasterization, native source and all three runtime/payload profiles in
`p34-shader-control/`. The sealed Release candidate differs only in `5H4D3R5.dll/.pdb` and
`PluginContractTests.exe/.pdb`: host, other plugin artifacts, settings, fonts and DxUi identity are identical.
The native source comparison has seven modified inputs and the two new oracle files, with all clock/host inputs
unchanged. Receipts are `p34-shader-attribution-control-*`, `p34-shader-candidate-*`,
`p34-shader-source-pair-identity.json` and `p34-shader-release-clock-guard-failure.log` under `.build/receipts/`.
GPU API phase times include driver/scheduler cost and do not measure GPU completion.

Two fresh native orders per backend repeat the selected CPU saving. Each arm uses one first setup and 31 repeated
fresh owners on its own isolated device; all six phases execute once and succeed. Repeated median times are:

| Backend / order | Background control → candidate (ms) | Total control → candidate (ms) |
| --- | ---: | ---: |
| WARP / control then candidate | 14.963 → 1.239 | 22.396 → 8.484 |
| WARP / candidate then control | 14.737 → 1.234 | 22.403 → 7.945 |
| Default RTX 5080 / control then candidate | 14.683 → 1.238 | 17.367 → 3.398 |
| Default RTX 5080 / candidate then control | 14.654 → 1.227 | 17.202 → 3.553 |

Background rasterization is 91.6–91.7% lower in these observations. The separate production XENEON comparisons
run on the owning AMD Radeon Graphics adapter (`00000000-0001C33F`). Both Release orders use identical portable
scenes, collectors, version 1.0.282, DxUi identity, pinned shortcuts and icon resources, checked before/after.
Each arm has all 18 stages and 402 successful callbacks; Shaders has three device callbacks per arm:

| Release order | Shaders device median control → candidate (ms) | Shaders device max control → candidate (ms) |
| --- | ---: | ---: |
| Control then candidate | 20.664 → 4.568 | 26.749 → 10.380 |
| Candidate then control | 23.555 → 4.850 | 23.659 → 13.682 |

The measured setup median falls 77.9 / 79.4%. This supports accepting the bounded CPU rasterization change with
full-resolution, byte-identical content. It does not qualify a four-ms whole-page slice or physical first pixel.
Settled Matrix observations remain separate from setup cost:

| Order / phase | One-core CPU control → candidate (%) | Submissions/s control → candidate | Private MiB control → candidate |
| --- | ---: | ---: | ---: |
| Forward / cold | 2.266 → 2.340 | 60.118 → 60.116 | 36.12 → 35.59 |
| Forward / returned | 2.697 → 2.500 | 59.978 → 60.071 | 66.27 → 67.36 |
| Reverse / cold | 1.878 → 2.414 | 60.255 → 59.974 | 35.41 → 34.61 |
| Reverse / returned | 2.031 → 2.055 | 60.037 → 60.118 | 63.66 → 64.87 |

Both arms keep 42 cold / 44 returned threads, local usage 25.01 cold / 32.26 MiB returned, and zero hidden/minimized
submissions. CPU is mixed and no causal steady CPU saving is established. Candidate returned private is **1.09 /
1.21 MiB higher** in the two pairs; this observed cost must remain explicit. Cold differences precede shader
activation, while live data, font/Shell/driver caches and scheduling remain uncontrolled. The intended 6-KiB row
cost is justified by the repeated large setup reduction, unchanged heap/resource payloads and exact pixels; it
does not explain the returned-private difference or establish a memory win. Full return-retention acceptance stays
in P3.6 and requires further attribution, with the original cold-plus-four-MiB target unmet.

The largest remaining candidate device medians are Desk Clock 11.689 / 12.300 ms, Weather 10.523 / 11.033 ms and
System Pulse 9.409 / 11.006 ms (six calls each). AV Control medians are 0.894 / 1.040 ms but maxima are
124.971 / 144.499 ms. These are whole callbacks. Separate CPU atlas/font work from D3D/API/driver work before
selecting another change; preserve the current multi-tier clock atlas, glyph coverage, natural lighting and live
animation. The proposed four-ms slice, physical-touch/first-pixel, earlier screenshot fault and clock peak remain
open. The current-fixture clock allocation capture's first approved UAC attempt was canceled before recording.
The explicitly authorized replacement is now loss-free, with verified cleanup; the P4.6 batching step below
records its driver attribution and candidate qualification.

The counterbalanced native logs, all four hardware roots, paired context/source/runtime identities, profile and
actual-payload proofs are bound by `p34-shader-native-matched-comparison.json`, both
`p34-shader-matched-comparison*.json` and `p34-shader-qualified-comparison.json`. Closeout requires the final
BuildProcess scope in `p34-shader-final-tooling.log` and immutable source/profile/evidence manifests in
`p34-shader-sealed/` and `p34-shader-closure.json` before the step's commit. The complete plan remains active.

### P4.6 clock allocation capture and fixture pacing (2026-10-12)

The approved replacement captures a normal-user, byte-identical Release HostPluginTests alias with one separate
administrator recorder. All periodic checks keep the owned profile live; the 94.381-second ETL has zero lost
events/buffers. Matching private and Microsoft symbols resolve the owned PID's allocation stacks. The recording
stops while the fixture remains alive, saves successfully, and removes its unique IFEO image key. Independent
read-only verification confirms that key is absent and the owned recording is inactive. Native/runtime hashes
match before and after the capture. The first failed recorder and its failed cleanup result remain preserved.

At process ages 25 / 50 / 85 seconds, reported outstanding heap payload is 4,067.0 / 4,215.7 / 3,984.2 KiB; at
recording end it is 5,148.8 KiB. Within the top hundred stack groups, first non-allocator WARP caller payload is
3,281.5 / 3,446.6 / 3,214.8 / 4,372.0 KiB. The largest direct `Task_Transform` pool stack retains two allocations
totalling 1,821.4 KiB at each earlier cut, then three totalling 2,732.2 KiB at recording end. Direct application
caller groups stay at 99.0 KiB within those top hundred; this truncated view does not establish a project-owned leak.
Heap extent totals are rejected after xperf's decommit warning, and virtual-allocation event totals do not equal
process private commitment. These are instrumented ownership observations, not an uninstrumented memory saving.

The traced fixture exits zero after 20-second warm-up and 110-second measurement, with 3,153 measured frames,
stable clock owners and 2,342,912-byte peak private growth. It does not reproduce or explain the earlier
5,308,416-byte Release Full failure. Source inspection identifies a separate fixture mismatch: Studio and Desk
Clock slept to their deadlines but built frames without first acquiring DXGI readiness. Initial, warm-up and
measured draws now use the application's frame-permit bound. Tests cover missing/unsignaled refusal, one-use
permits and propagation of draw failure. The 20-second warm-up, scheduled deadlines, 4/8-MiB guards and clock
rendering/assets remain unchanged. The Release Desk and Studio DLLs are byte-identical to the sealed control.
Only `HostPlugin.Tests.Runner.cpp` changes among 472 native inputs. This is a test-fixture correction and does not
claim to explain the old peak.

Fresh Release Full and ASan Debug Full each pass all fifteen native scopes. Debug Full initially fails the unchanged
synthetic AV bridge's established-consumer check; its original log remains. LauncherAlias, Packaging and PluginContract
passed before that failure. The remaining twelve scopes, including all 9,699 AV checks, pass against unchanged native
inputs and 145 deployed Debug artifacts. This accounts for fifteen Debug scopes without calling the failed Full a pass
or explaining its intermittent AV failure. Release/ASan runtime manifests contain 144/75 inputs. Builds report zero
warnings/errors; ARM64 Release is compile-only, and all ten skills pass. Current Release/Debug Desk guard private peaks
are 3,235,840 / 1,990,656 bytes; all scheduling, draw, owner, hidden-work and teardown bounds pass. ASan retains its
normal quarantine/shadow settings and reports process bytes diagnostically.

With all build/test work stopped, four isolated uninstrumented Release arms use the same scene, WARP backend,
20-second warm-up, sixty-second measurement and two-second hidden interval:

| Order | Fixture | Measured frames | Peak private / working-set growth (MiB) | Whole-fixture CPU (% of one core) |
| --- | --- | ---: | ---: | ---: |
| 1 | Sealed control | 1,733 | 0.55 / 0.91 | 69.53 |
| 2 | Candidate | 1,727 | 3.08 / 2.19 | 64.01 |
| 3 | Candidate | 1,731 | 1.87 / 1.56 | 67.28 |
| 4 | Sealed control | 1,735 | 1.89 / 1.36 | 61.37 |

Every arm exits zero with stable resources and unchanged source/runtime hashes. Cadence is comparable; CPU moves
in opposite directions across the two pairs and memory peaks vary. This establishes no CPU/private saving.
CPU includes startup, warm-up and hidden time; frame counts are fixture draws, not displayed hardware submissions.
Real-clock phases and background workload are uncontrolled. The unchanged 4/8-MiB guard passes these arms, but neither
the repeats nor the fixture correction explain the prior 5,308,416-byte failed peak.

Receipts are `runtime-2026-10-12-p34-clock-allocation-repeat` (ETL, loss/symbol/process/cut reports, cleanup and
`allocation-attribution.json`) and `p46-clock-pacing-*` under `.build/receipts/`. The first Python summarizer
refused xperf's ANSI image metadata as UTF-8; its error is retained, and corrected owned alias/PID matching uses
raw ASCII bytes against the same immutable ETL. No recording repeat was needed. The owner has no 120/240-fps
camera available, so physical touch-to-first-changed-pixel remains unqualified; synthetic input and next submission
cannot substitute for it. `p46-clock-pacing-closure.json` seals the completed step, including final independent tooling,
source/runtime snapshots, all four arm logs and the retained original Debug failure. This physical gate, broader
retention, GPU setup and the earlier screenshot fault keep the WIP active.

### P4.6 current-clock control and refused allocation attempt (2026-10-13)

The owner authorized one current-clock allocation UAC prompt. The prepared helper verified the current 474 native
inputs and copied the sealed build-282 Release runtime to a fresh receipt root. Windows returned cancellation from
`Start-Process -Verb RunAs` before the administrator recorder started. No recorder directory or ETL was created.
Independent read-only inspection confirms that the unique clock alias is not running and its IFEO image key is
absent. Preserve `runtime-2026-10-13-p46-current-clock-allocation/refused-at-uac.json` and
`p46-current-clock-capture-controller.log`; this attempt supplies no allocation attribution. Replacement authorization
was pending at this checkpoint; the owner's later explicit authorization and successful recording are below.

While that request is pending, an isolated uninstrumented current Release WARP control passes the unchanged
20-second warm-up, 60-second measurement and two-second hidden interval. It submits 1,655 measured fixture frames,
with 1,596 active and 60 idle delays, stable owners, bounded draws, hidden zero work and complete device teardown.
Peak private/working-set growth is 3,633,152 / 2,564,096 bytes (3.46 / 2.45 MiB), inside the unchanged 4/8-MiB guards.
Whole-fixture CPU is 45.33 percent of one core across 82.172 seconds, including startup and warm-up. These fixture
frames are not displayed XENEON submissions. Source and runtime hashes remain unchanged. This single passing control
does not reproduce or explain the earlier post-pacing 4,341,760-byte peak.

Three isolated current Release Desk Clock native controls also pass factory, scheduling, deterministic rollover,
WARP composition, allocation and lifecycle checks. Their disabled/static/animation submission medians are
0.0006 / 0.0425 / 0.0530 ms per frame; the representative active WARP timestamp median is 1.1268 ms. These uncapped
API timings are not production CPU or cadence measurements. Ten deterministic BMP baselines cover static, five flip
phases, midnight cross-fade/final, the 2560x720 composition and reference extent. Preserve their exact bytes and
hashes for a future candidate comparison. No clock rendering, atlas, shader, settings, deadline or memory limit has
changed, and no optimization or new regression-test implementation is accepted by this control-only checkpoint.

Receipts are `p46-current-clock-uninstrumented-control0` and `p46-current-clock-native-control` under
`.build/receipts/`. After the authorized current-fixture trace succeeds, verify owned PID/symbols, event loss and
cleanup, attribute the remaining cost, then select a measured change. Add independent regression coverage and compare
both Release orders before committing the optimization. The old failed peaks and the complete WIP remain open.

### P4.6 current-clock allocation attribution and compatible batching (2026-10-13)

The owner explicitly authorized one replacement UAC prompt. The normal-user current paced Release fixture records
a 92.475-second ETL with zero lost events/buffers, all periodic profile checks live and successful owned stop. Its
unique IFEO image key is absent and WPR independently reports the owned instance inactive. The initial cleanup
assertion refused WPR's `WPR is not recording` wording; the exact response is preserved, the read-only assertion
was corrected and no recording or cleanup mutation was repeated. The first canceled UAC receipt remains intact.
All 474 captured native inputs and 145 copied runtime files match before/after recording; the native inputs are
also archived byte for byte from the verified sealed source. Microsoft/private symbols and kernel command-line
metadata bind the stacks to PID 85932 and its unique normal-user clock alias.

The analysis includes all 2,284 reported stack groups, with one explicitly unresolved group. Heap extent totals
are rejected after xperf's decommit warning; virtual-allocation totals are not process private commitment.

| Process-age cut | Outstanding heap payload (KiB) | First non-allocator WARP caller payload (KiB) | Direct clock caller payload (KiB) |
| --- | ---: | ---: | ---: |
| 25 seconds | 4,157.4 | 3,422.9 | 0.9 |
| 50 seconds | 5,120.5 | 4,394.4 | 0.9 |
| 85 seconds | 5,119.4 | 4,397.2 | 0.9 |
| Recording end | 4,206.6 | 3,484.6 | 0.9 |

Across the recording, WARP accounts for 528,682.7 of 544,475.0 KiB allocated payload (97.1 percent). Its dominant
`Task_Transform` pool stack allocates 324 blocks / 295,072.9 KiB (54.2 percent of total payload). Direct clock caller
groups allocate four blocks / 1,506.9 KiB and retain two / 0.9 KiB. These are allocation-work and ownership
observations, not CPU-stack attribution or process-commit savings. The traced fixture passes with 3,181 measured
frames and 2,887,680-byte peak private growth. It does not reproduce or explain the earlier 4,341,760-byte
post-pacing failure or the older 5,308,416-byte peak.

The selected experiment batches the compatible clock passes into one instanced draw after the separate opaque
background. A compact integer mapping preserves the original card, moving-face, separator and date primitive order.
Static/active frames still submit 40/46 instances and use the same changed-only 400-byte upload, atlas tiers,
font contours, colors, shading, duration, deadlines and pacing. Total draws fall from three/four to two/two, and
three immutable 16-byte offset buffers and their bindings are removed. No cache, owner, worker or wake is added.

All ten sealed current Release BMP baselines remain byte-identical in the pilot. Twelve control and twelve candidate
native observations in counterbalanced order produce static submission medians 0.04725 -> 0.03625 ms and active
medians 0.05035 -> 0.03770 ms (23.3 / 25.1 percent lower). Single-frame WARP timestamp medians are effectively
unchanged at 1.20695 / 1.20745 ms, with broad individual variation. These uncapped API probes do not establish
displayed hardware cadence, whole-process CPU savings, lower driver allocation payload or memory acceptance.

The independent native oracle freezes the original vertex shader and three-pass offset-buffer sequence, borrowing
the same visual constants/atlas/pixel pipeline so serviced fonts and timing cannot change the reference. The Release
candidate passes 31 complete-frame comparisons across static, flip/date phases, landscape/portrait/minimum/DPI
extents and both initialized atlas tiers. The first expanded fixture failed its high-tier assertion because it
notified size before the initial clock sample; that receipt is retained. The corrected fixture exercises the
established initialized resize path and passes.

Separate cold-setup gap: `DeskClockWidget::OnTargetSizeChanged` currently gates atlas resizing on its wall-clock
`_initialized` state, so the first notification after device creation can be ignored although device resources
exist. A first large unchanged extent can therefore retain the base tier. This predates batching and is not fixed
or accepted by the initialized-tier oracle. Add a cold maximum-extent regression and qualify a resource-readiness
guard in a separate step; preserve the existing tier thresholds and full-quality glyphs.

Receipts are `runtime-2026-10-13-p46-current-clock-allocation-repeat`, `p46-clock-batch-pilot-native`,
`p46-clock-batch-native-pilot-matched`, and `p46-clock-batch-release-oracle-test*` under `.build/receipts/`.
Fresh Release, Debug and ASan Debug Full each pass all fifteen scopes on 475 unchanged native inputs and
144/145/75 unchanged deployed artifacts. The original-draw oracle compares 31 Release and 30 Debug/ASan frames;
the additional Release target-display readback accounts for the difference. Source builds report zero warnings
or errors; ARM64 Release compiles cleanly, with native ARM64 runtime unexecuted. All ten skill checks pass.
Release's sixty-second Desk Clock guard peaks at 1,331,200 private bytes and 1,462,272 working-set bytes, with
1,685 measured frames and exactly 3,370 draws. This passing repeat does not explain the prior failed peaks.

The final native comparison uses the identical new `--submission-probe` executable, host/dependency/scene bytes
and both orders; only the clock DLL/PDB differ. Each backend has eight control and eight candidate observations:

| Backend | Static API median (ms), control -> candidate | Active API median (ms), control -> candidate | Active GPU timestamp median (ms), control -> candidate |
| --- | ---: | ---: | ---: |
| WARP | 0.03020 -> 0.02465 | 0.04620 -> 0.03640 | 1.01440 -> 0.90630 |
| Default RTX 5080 | 0.00305 -> 0.00265 | 0.01030 -> 0.01120 | 0.01080 -> 0.01040 |

WARP submission medians are 18.4/21.2 percent lower. The RTX active API median is adversely 0.9 microseconds higher;
there is no all-backend CPU win. The bounded adverse observation is accepted for the simpler draw/resource set,
exact pixels and the WARP/AMD benefits, and is recorded in the performance contract. Native timestamps are
single offscreen draws, not displayed cadence or input latency; the default RTX adapter is not the XENEON adapter.

Four nonactivating production observations use the same 65-second cold phase, 25-second hidden/minimized phases,
ten-second recoveries, one-second probe interval, scene/host/settings/DxUi bytes and build stamp 282. Every
`device-created` record names the monitor-owning AMD Radeon, LUID `00000000-0001C33F`. Both orders preserve visible
submission cadence and hidden/minimized zero submissions:

| Order | CPU (% of one core), control -> candidate | Submissions/s, control -> candidate | Private (MiB), control -> candidate | Working set (MiB), control -> candidate |
| --- | ---: | ---: | ---: | ---: |
| Control then candidate | 1.531 -> 0.896 | 26.347 -> 26.461 | 33.86 -> 41.05 | 49.64 -> 62.19 |
| Candidate then control | 1.683 -> 1.145 | 26.246 -> 26.513 | 34.27 -> 34.44 | 49.91 -> 49.93 |

Observed CPU is 41.5/32.0 percent lower, with uncontrolled live-clock phases and background workload. Local GPU usage
is 25.51 MiB in every cold phase. Process private changes are +7.19/+0.17 MiB; the large first-pair difference is not
repeated, and neither pair establishes a memory win or attributes every driver/font/allocator byte. No new owner,
cache, timer or worker exists in the changed production code. Memory acceptance and the original peaks remain open.
Raw captures, shared native logs and byte/context verification are retained in `p46-clock-batch-matched/` and its
`comparison.json`.

The five-minute uninstrumented candidate retains stable owners, two draws per frame, active/idle scheduling,
hidden zero work and complete teardown, but fails its unchanged four-MiB private-growth guard at 6,213,632 bytes
(5.93 MiB); working-set growth is 4,702,208 bytes, below eight MiB. It measures 6,086 frames / 12,172 draws. The
equal-duration sealed old-clock control also fails that private guard at 6,770,688 bytes (6.46 MiB), with working
set 3,948,544 bytes, 5,813 frames and otherwise passing original draw/resource/hidden/teardown bounds. Both
20-second warm-up / 300-second measurements preserve native/runtime identity. This demonstrates that the late
peak can occur without batching; the two runs do not attribute it or prove a candidate memory saving.

Preserve `p46-clock-batch-five-minute` and `p46-clock-batch-five-minute-control`, including both failures and their
one-second process samples. Long memory qualification is `HOLD`; do not call a passing sixty-second repeat an
explanation or widen the 4/8-MiB guards. The short allocation recording ends before this late window. A prepared
330-second recording of the current normal-user candidate, using a unique image alias and bounded owned recorder,
was intended to cover the complete five-minute interval while the fixture remained alive. The owner granted its
additional UAC prompt; the failed attempt and revised snapshot preparation are recorded below.

The batching implementation has exact pixels, all three Full profiles and matched Release CPU/submission evidence;
it is not complete memory qualification. Final independent tooling passes. `p46-clock-batch-sealed/` and
`p46-clock-batch-closure.json` bind the source, all four runtime profiles, raw pixel/control/trace evidence and both
preserved long failures before this step's commit. The seal explicitly reports long memory qualification `HOLD`,
no explanation of the earlier peaks and the cold-setup gap still open. The complete plan stays active.

### P4.6 late-window recorder failure and snapshot preparation (2026-10-13)

The approved additional UAC prompt starts the owned recorder; the sealed current clock remains unelevated and takes
no focus or input. The first nonzero loss appears at the 75-second controller status check. Later checks retain
millions of dropped events. Stop is requested through the existing owned controller while the verified clock PID
8560 is alive, rather than collecting more unusable events. The WPR stop command exceeds its 150-second bound and
is stopped; the controller cancels only its owned instance and disables/removes only its unique alias configuration.
Independent read-only WPR, IFEO and process checks confirm the instance inactive, the unique key absent and every
owned recorder/fixture process exited.

The retained ETL is 7,115,636,736 bytes and reports 4,004,747 lost events. Normal xperf processing refuses it.
An explicitly loss-tolerant statistics-only run enumerates just the header after the interrupted merge; it is
**not** accepted heap/virtual-allocation or symbol evidence. Do not infer a retained allocation, driver leak, CPU
bottleneck or cause of the earlier process-private peaks from this file. Preserve the ETL, every status/error log,
the initial statistics refusal, cleanup proof, fixture output and one-second samples under
runtime-2026-10-13-p46-clock-long-allocation.

The normal-user 20-second warm-up / 350-second instrumented fixture also fails its unchanged four-MiB private guard
at 6,803,456 bytes (6.49 MiB). Working-set peak growth is 5,058,560 bytes, below eight MiB. Its 7,284 frames /
14,568 draws retain the exact two-draw bound, active/idle scheduling, stable resource owners, hidden zero work
and complete teardown. All 475 native inputs and 145 copied runtime files independently match after capture.
This is a failed instrumented diagnostic, not a matched uninstrumented Release acceptance baseline. The earlier
5.93/6.46-MiB five-minute controls, 4.14/5.06-MiB historical failures and their explanations remain open. Production
rendering, visual quality, warm-up, cadence, resource lifetime and memory limits are unchanged in this step.

The next diagnostic uses the installed WPR heap-snapshot profile, exported and schema-validated without a prompt.
It requests live heap payload/stacks at process ages 25, 50, 85, 150, 240, 300 and 325 seconds, recording the actual
request times. Unlike the failed continuous allocation stream, it emits snapshot data at selected cuts
([Microsoft WPR snapshot workflow](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/record-heap-snapshot)).
The prepared helper keeps the same sealed normal-user fixture, one fresh alias and one owned recording instance.
It validates the exact PID, image, creation time and arguments before/after each snapshot, rejects global or
collector event loss, retains raw inputs if merging fails and cleans only its own configuration. It takes no
focus/input after UAC. Fifteen policy/preparation checks and both PowerShell parsers pass; preparation alone starts
no tracing, registry configuration, fixture or UAC. Real OS snapshots, stack completeness, symbols and cleanup are
still unexecuted. Snapshot instrumentation changes allocation overhead; its private bytes cannot close the
uninstrumented memory gate. Heap snapshots also do not attribute non-heap virtual commitment.

p46-clock-snapshot-preparation/, p46-clock-snapshot-owned.ps1 and
collect-p46-clock-snapshot-controller.ps1 retain that initial revised diagnostic. The owner grants the fresh UAC
authorization and its failed attempt is recorded below; these original helper names remain immutable evidence.
After a usable recording, select a change from the measured cause, add independent regression coverage and compare
both matched Release orders before the next optimization commit. No further production memory optimization is
selected from this failed recorder. Long memory qualification remains **HOLD**; the complete plan remains active.

### P4.6 snapshot request scope failure and corrected preparation (2026-10-13)

The newly approved UAC starts the owned snapshot recording. Its first 15-second profile/collector report is live
with zero lost events. The helper then omits the final owned recording-instance argument on the first 25-second
single-snapshot request. WPR queries its default session and returns no profiles / 0xc5583000. This is a helper
defect, not a Windows profiling-policy rejection. No snapshot or accepted ETL is saved. The owned controller
cancels its own recording, disables the unique image configuration and exits. Independent read-only checks confirm
its WPR instance inactive, its IFEO key absent and both the controller and normal-user fixture gone.

The normal-user 350-second fixture finishes with exit zero: 3,497,984-byte peak private growth (3.34 MiB),
3,026,944-byte peak working-set growth, 10,108 frames and 20,216 draws. Resource owners, active/idle scheduling,
hidden zero and teardown pass. All 475 native and 145 copied runtime inputs remain byte-identical. This passing
instrumented repeat explains none of the earlier failures and closes no allocation or long-memory gate.
Preserve runtime-2026-10-13-p46-clock-snapshots, including the actual request error, live-profile report,
cleanup proof, unchanged input witnesses and completed fixture output.

The installed WPR advanced help requires the same final instance-name option on every subsequent logging-session
command. A new regression independently inspects start, status, snapshot, stop and cancel requests: the original
helper is rejected and the corrected helper passes. Seventeen policy/preparation checks and the installed profile
schema pass without tracing, registry mutation, fixture launch or UAC. Original helpers and the prior evidence
seal are preserved; the corrected names are p46-clock-snapshot-owned-v2.ps1 and
collect-p46-clock-snapshot-controller-v2.ps1, with p46-clock-snapshot-v2-preparation/.

The corrected six-minute diagnostic keeps the same seven early/late heap snapshots and adds virtual-allocation
stacks to distinguish retained heap payload from non-heap commitment. Continuous heap allocation/reallocation
events and sampled CPU stacks stay disabled. Only the owned normal-user clock PID is selected for subsequent
attribution; this remains instrumented evidence, not an uninstrumented Release acceptance baseline. Real snapshots,
event-loss accounting, PID/symbol/stack completeness and independent cleanup remain to be verified.

The previous administrator controller has exited and its one-prompt approval was used. The owner authorizes the
prepared replacement after the documentation checks/commit; that attempt is recorded below. No further production
optimization is selected without a usable recording; visual quality, cadence, warm-up and the 4/8-MiB guards remain.
Keep long memory qualification **HOLD** and the complete WIP active.

### P4.6 collector loss isolated and heap-only late capture prepared (2026-10-13)

The approved corrected helper successfully captures the 25-, 50- and 85-second heap cuts for the exact normal-user
PID 93548. At the 90-second status check, the system/virtual-allocation collector reports 25,777 lost events and
the heap-snapshot collector reports zero. The helper detects loss, stops early and saves the trace without waiting
for the remaining cuts. The completed 2,521,825,280-byte merged ETL reports 102,193 lost events. Owned stop succeeds
and independent checks confirm the instance inactive, the unique IFEO key absent and all owned processes gone.

Retained raw collectors isolate the volume: the system file is 2,478,833,664 bytes, while the heap event file is
7,340,032 bytes. Independent processing of the latter reports zero lost events/buffers and three instances for
PID 93548, with 264,708 / 261,340 / 257,328 bytes of reported event data. These are partial early instance/collector
diagnostics; no late heap payload, virtual-allocation cause, symbol attribution or original peak explanation is
accepted. Preserve the merged/raw files, periodic collector status, actual request times, independent cleanup,
partial statistics/instance report and before/after identity under runtime-2026-10-13-p46-clock-snapshots-v2.

The completed 350-second instrumented fixture passes with 188,416-byte peak private growth, 471,040-byte peak
working-set growth, 6,414 frames and 12,828 draws. Its stable owners, active/idle scheduling, hidden zero and
teardown pass; all 475 native and 145 copied runtime files match. This passing repeat and its observer effects
explain none of the earlier failures. Production code, quality, cadence, warm-up and guards remain unchanged.

The next prepared helper isolates the loss-free heap path for all seven early/late cuts. Its profile contains only
CPU configuration, process/thread and image metadata plus heap-snapshot events. Global virtual-allocation,
continuous heap allocation/reallocation and sampled CPU streams are absent. It retains the exact PID/image/birth/
argument checks, the final owned instance on every recording request, per-collector loss rejection, preserved raw
inputs and unique-key cleanup. The heap-only schema and seventeen policy/preparation checks pass without starting
tracing or UAC. New names p46-clock-snapshot-owned-v3.ps1, collect-p46-clock-snapshot-controller-v3.ps1 and
p46-clock-snapshot-v3-preparation/ preserve every earlier helper and receipt.

One fresh UAC authorization is required after the current checks/commit because the approved controller has
exited. A successful heap-only trace may attribute live payload at the late cuts; non-heap commitment and the
uninstrumented guard failures still need independent evidence. No further production optimization is selected
from the lossy combined capture. Long memory qualification remains **HOLD**; the complete WIP remains active.

### Phase 3 — fluid swipe, nothing left behind

The rule: a swipe MAY allocate whatever a smooth slide needs; the settled page MUST end with exactly the resources of a
cold visit to that page, and nothing from the page that was left.

| ID | Item | Pass condition |
| --- | --- | --- |
| P3.1 | **Checked borrowed selection and exact immutable validation witness implemented.** Full logical document equality includes inactive pages; bounded witness misses, optional-cache failure and oversized valid documents take cold full validation. No duplicate parsed document or target-only trust. | Malformed inactive pages, selector/source-only reuse, rollback and exact gates pass. Lower selection/provider cost does not qualify the proposed 2 ms UI CPU / 8 ms stage targets; GPU/visibility and later preparation still need attribution. |
| P3.2 | **Complete 2026-10-10.** 64-bit hash filter plus original widget index, 32-byte keys. Full original-byte comparison preserves identity under collisions, with no extra serialization/allocation. | Compiler PDB frame records prove Initialize/Reconfigure <32 KiB in both x64 profiles; forced-collision and actual reuse/reconfigure tests pass. |
| P3.3 | **Reconciled against current quality and lifetime contracts.** Keep first follow-finger presentation before deferred staging (`FlushPendingTransitionStage`), current plus one directional neighbor, and retire the previous neighbor on reversal. Continuous widgets keep animating during pan/settle. Blocked edges and canceled-before-staging gestures activate nothing; a canceled gesture that already exposed a neighbor can legitimately have activated it. | Current policy, partial/offscreen rendering, native-neighbor and retirement tests pass. Physical F/input/presented latency remains under P0.2. September eager recognition, both-neighbor retention and blanket staged-animation suppression are rejected. |
| P3.4 | **Attribution, offscreen discrete Launcher construction and bounded Shaders background rasterization implemented.** Ordinary native parsing/quality stays intact on one lazy host-owned STA, with copied jobs, stale-generation cancellation, bounded admission and a full-quality readiness barrier. The current page keeps animating; initial/touch setup remains synchronous. Shader lighting uses a 6-KiB stack row and conservative ranges with exact original mip bytes. A 4-ms host slice cannot bound an indivisible GPU/native callback above 4 ms. | Both orders remove Launcher visibility stalls (68.354/73.821 -> 0.035/0.033 ms median). Shader device medians fall 20.664/23.555 -> 4.568/4.850 ms on XENEON AMD; CPU background time falls 91.6–91.7% in both native orders, with all x64 mip payloads exact. Returned private is 1.09/1.21 MiB higher in the shader pairs, with unchanged resource payloads and mixed CPU; attribution remains open. Desk/Weather/Pulse medians and AV outliers are the next measured construction targets. Physical input/first-pixel, the whole-page 4-ms slice/16.7-ms frame and retention remain unqualified. |
| P3.5 | **Conditional snapshot product option.** A 2560x720 BGRA snapshot costs 7.031 MiB (1280x360 costs 1.758 MiB) per retained image, in addition to live pages/driver references. Sliding a frozen snapshot replaces visible animation; downsampling can reduce quality. Neither trade is accepted by this implementation request. | Decide only from measured P3.4 limits and a concrete visual/first-present comparison. Keep live animation and the two-page bound until such a decision; no snapshot code is selected. |
| P3.6 | **Source, GPU binding and AV coordinator ownership cuts complete; full retention remains in progress.** Sources retire after their last external facade/subscription lease and quiescent collection/callback use. Context bindings clear at successful page retirement without rebuilding the device or swap chain. AV coordinator/graphics caches release with the last provider, preserving survivors, failed-allocation rollback and retiring host work. Mapped plugin images remain. Driver trimming on visible settles is not selected. | Full recorded profile gates pass. Source retention 87.61 → 71.75 MiB; separate GPU pair 69.54 → 69.08 MiB. The AV pair has 18 stages/402 callbacks each, returned private 62.18 → 64.60 MiB and local usage 32.26 MiB in both; its cold difference already precedes AV visits. No causal process-memory/CPU/latency saving is established. Submissions stay near 60/s and hidden/minimized zero. Whole D cold-A +4 MiB and residual driver/cache attribution remain open. |

### Phase 4 — retained memory and arenas

| ID | Item | Pass condition |
| --- | --- | --- |
| P4.1 | **Complete: arena-backed SystemDataSource.** One 16-MiB reservation with first-use page-aligned families/tables, optional thread scratch and in-place activity/detail value expansion. Preflight and typed teardown preserve histories, caps, ownership and allocation-free reuse. | Actual shipped commit 9.461 MiB, full 15.766 MiB, cold 4 KiB; constructor median about 2.3 us. Fault/region/history/lifetime tests and all required scope accounting pass; 145 frames match; matched private reduction 7.24/7.25 MiB; three settled D observations show zero arenas/native owners. Whole retention remains P3.6. |
| P4.2 | **Count-sized provider records and immutable private JSON implemented.** Records are 142,232 → 1,656 logical bytes with System live. Release shipped settings are 209,460 → 56,450 logical bytes plus 288 bytes of shared-control blocks; allocator overhead is excluded. Loaded `sourceDocument`, inactive-page validation, caps, transactional persistence and quiescent ownership remain. Pages/widgets already use vectors. Current-page-only module mapping requires independent justification. | Provider bounds/failure/retry/drain and private-object ownership/cap/parser/persist/failure/stable concurrent-copy tests pass their recorded exact profile gates, ARM64 compilation and skills. Matched hardware comparisons are recorded above; JSON returned private is 63.45 → 62.63 MiB with higher working set and no causal CPU/latency saving. Its initial screenshot access violation remains unexplained. The old only-Matrix-module and universal ≤64-KiB shadow goals stay unqualified. |
| P4.3 | **Count-sized icons, glyph batch, single shell output, folder binding, verified-file opaque reuse and combined queries implemented; cold-parent shortcuts rejected.** Empty/8/32 entries use 0/2/8 MiB GPU payload. Eight decoded buffers retain 2 MiB with exact token/identifier and fresh shield/file checks; the prior expansion adds 1.5 MiB. Ordinary parsing remains. P3.4 now constructs incoming discrete Launcher CPU pixels on the host STA, sharing that cache under a short SRW lock. | Current-DLL phase/ownership/normal-route regression, cache/shield/metadata/OOM/capacity/shutdown, import/drop/device-loss, stale jobs, exact STA pixels, matched measurements and profile accounting are recorded above. Initial/touch cold setup, worst whole-page activation, process memory and first-pixel budgets remain open. Historical failed guard/hash receipts remain; no causal D-return saving is claimed. |
| P4.4 | **Surface lifetime landed and retained at the current pin.** Hidden tile and unraised overlay surfaces are released without detaching control trees. The raised hardware P0.4 run confirms exact tile/overlay bytes, dismissal, hidden zero and restore. Lazy control-tree attachment, hidden-view update skipping, profile-control destruction and inventory diffing remain separate proposals. | Native fixtures and production hardware accounting agree; unchanged-observation copy cost and any new control-tree proposal still require measurements. Real AV release gates remain in the HOLD RFC. |
| P4.5 | **Content-sized Weather cache and last-widget release implemented.** Cold bodies remain unallocated; each used entry owns payload plus terminator, with equal-size reuse and transactional new/grow/shrink/eviction. Surviving/hidden widgets retain shared data; final strong callback leases release it. Existing validators, 304, cancellation and refresh clamps remain. Curl connection reuse and failure coalescing are separate conditional candidates. | Real-DLL lower offline transport proves 2,097,160 → 24 bytes for eight two-byte responses, capacity/OOM/expiry/validator/failure and last-owner scenarios, with red/green against the old cache. Exact images and profile accounting are recorded above. Matched Gallery observations establish no causal whole-process CPU or memory saving. |
| P4.6 | **Clock pacing, current short-trace attribution and compatible batching implemented.** Original pixels and both initialized atlas tiers pass; three/four draws become two/two with fewer owned buffers. All three Full profiles, ARM64 compilation, skills and matched Release CPU/submission observations are recorded above. GdiOrbit remains absent from shipped Gallery. | The short loss-free ETL attributes WARP payload in its recording, not the old 5.06/4.14-MiB peaks. Both uninstrumented five-minute guards fail at 5.93/6.46 MiB. The long recorder loses 4,004,747 events and times out; the original snapshot request misses its owned instance and saves no evidence. Both cleanup proofs pass. Corrected requests capture three early instances, but the system/virtual stream loses 102,193 events; the raw heap collector has zero loss, while the missing late cuts and passing instrumented guard explain none of the failures. A heap-only seven-cut helper passes seventeen preparation checks; one fresh UAC authorization is required after checks/commit. Long memory qualification stays HOLD. Preserve typography, rollover quality, cold target-tier follow-up and unchanged 4/8-MiB guards. Hardware callback/physical-first-pixel and historical failures remain open. |

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
