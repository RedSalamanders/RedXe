# Desk Clock private-memory peak investigation

Status: `HOLD` — deferred by the owner on 2026-10-13 for later study
Created: 2026-10-13
Owner: host renderer, Desk Clock, and whole-process resource accounting
Parent: [Runtime memory and latency, P4.6](RuntimeMemoryAndLatency_2026-09-07.md#p46-seven-late-heap-cuts-verified-measured-render-alternatives-rejected-2026-10-13)
Owning contracts: [performance and resources](../../Core/Core_PerformanceAndResources.md),
[native plugins](../../Plugins/Plugins_API.md), and [build/test process](../../Build/Build_Process.md)

This non-normative record owns the deferred attribution of Desk Clock's intermittent process-private growth.
The parent retains implementation/measurement history and other runtime gates. The owner requests merging the
implemented branch while this study stays in WIP. Deferral does not accept failed long-memory qualification.

## Metric and preserved peaks

Values are **peak growth above the fixture's warmed baseline**, not total process memory or live heap payload.
MiB means 1,048,576 bytes. Ordinary private/working-set guards remain 4/8 MiB. Clock fixtures use the existing
20-second scheduled warm-up and acquire DXGI readiness before every draw.

| Cohort | Measurement | Private peak growth | Working-set peak growth | Outcome |
| --- | --- | ---: | ---: | --- |
| Batched clock, ordinary Release WARP | 300 s | 6,213,632 bytes / 5.93 MiB | 4,702,208 bytes / 4.48 MiB | Private guard failed; owners, scheduling, hidden zero and teardown passed. |
| Old-clock control, ordinary Release WARP | 300 s | 6,770,688 bytes / 6.46 MiB | 3,948,544 bytes / 3.77 MiB | Private guard failed; original drawing/lifetime checks passed. |
| Current clock, long allocation recording | 350 s | 6,803,456 bytes / 6.49 MiB | 5,058,560 bytes / 4.82 MiB | Highest recorded clock growth in these cohorts. Instrumented failure; lossy ETL cannot attribute it. |
| Rejected strip experiment, first final Release guard | 60 s | 4,268,032 bytes / 4.07 MiB | 3,137,536 bytes / 2.99 MiB | Failed; preserve alongside the passing unchanged repeat. |
| Restored clock, final Release guard | 60 s | 2,441,216 bytes / 2.33 MiB | 2,072,576 bytes / 1.98 MiB | Passed; explains no earlier failure. |

Earlier 4,341,760-byte / 4.14-MiB and 5,308,416-byte / 5.06-MiB failures remain unexplained in the parent.
A later short pass is not a fix, an instrumented maximum is not an ordinary Release baseline, and differences
between uncontrolled long runs establish no causal saving. Visual quality, cadence and memory limits stay intact.

## Established evidence and limits

- Both ordinary long failures retain one provider, widget and device-resource set. The batched clock keeps two
  draws per frame, hidden zero work and full teardown.
- The accepted heap-only repeat captures seven cuts at requested ages 25, 50, 85, 150, 240, 300 and 325 seconds.
  Merged/raw collectors lose zero events/buffers; identity and cleanup are independently checked. All 21,817
  listed allocations and 1,274 stack IDs reconcile. Direct clock retention stays 896 bytes/two blocks; listed
  payload falls 3,363,312 -> 3,172,653 bytes. WARP's transform pool stays 996,240 bytes/five blocks.
  This passing instrumented cohort reproduces no failed private peak.
- Preserve xperf's separate 21,796 reference-counter discrepancy in two apphelp reallocation stacks and quantified
  unknown intermediate frames. Listed payload is accepted with these limits; complete OS-frame, cumulative-churn
  and virtual-commit attribution is not established.
- Passing read-only nine-cut region controls also reproduce no failure. From ages 20 to 345 seconds, process/private
  region commitment rises 1,114,112/1,110,016 bytes for control and 532,480/532,480 for the retained strip.
  Executable/image/mapped commitment does not grow. Control other-private commitment grows 1,183,744 bytes while
  guard-containing commitment falls 73,728. Nonatomic region metadata supplies no allocation stack or proven owner.
- Date compaction, a blended background draw and strips preserve pixels but are rejected after matched costs fail
  to justify adoption. Production geometry, glyph quality, scheduling and resource guards remain unchanged.
- Checkpoint `6703ccf` accounts for sixteen Release/Debug/ASan PrePush scopes per profile, 41/40/40 reference frames
  and nineteen GPU-query frames each; ARM64 Release compiles and skills/tooling pass. ASan retains default quarantine
  and diagnostic process-byte reporting. These gates do not accept the deferred long-memory peak.

## Retained evidence

Raw files are local ignored products under `.build/receipts/`. Preserve them before cleaning that directory; the
parent and this record retain durable results/limitations. Keep both passing and failed receipts.

- `p46-clock-batch-five-minute`, `p46-clock-batch-five-minute-control`, `p46-clock-batch-closure.json`: ordinary
  failures, one-second process samples and exact source/runtime identities.
- `runtime-2026-10-13-p46-clock-long-allocation`: instrumented 6.49-MiB failure, 4,004,747-event loss, interrupted
  stop and verified cleanup. ETL attribution is refused.
- `runtime-2026-10-13-p46-clock-snapshots-v3-repeat`: accepted seven-cut listed heap payload and stack/count proofs.
- `p46-clock-strip-regions-control`, `p46-clock-strip-regions-candidate`, `p46-clock-region-comparison.json`:
  passing nine-cut region controls and one-second samples.
- `p46-clock-strip-release-first-failure`, `p46-clock-strip-rejected-final-source`: rejected experiment's failure
  and immutable source/runtime controls.
- `p46-clock-heap-restored-qualified-profiles`, `p46-clock-heap-restored-qualification.json`: restored-source gates.
- `p46-clock-heap-attribution-closure.json`: 720 source/4,829 evidence witnesses, SHA-256
  `5914DFA53C1E6C0627B2CD05A7882B4AFF1E5E467652D0E1AC80607A03D47193`; long memory qualification is false.

## Work on resumption

- [ ] Recheck current code, contracts, fixture pacing, scene, dependency pin and build/profile identity. Preserve
  historical failures and name the new tested commit explicitly.
- [ ] Reproduce a failing **uninstrumented** current-clock cohort with the same scene, adapter, cadence and warm-up.
  Save timestamped process samples and bounded region cuts through the actual late peak. Keep cold, warm, hidden
  and teardown observations distinct; never reset the peak or widen a limit to pass.
- [ ] Join growing committed allocation bases to a measured owner in that failing cohort. Separate live heap
  payload, allocator reservation/commitment, executable/JIT pages, mapped/image regions and thread stacks.
  Treat those categories as hypotheses until identity and attribution are supported.
- [ ] If elevation is needed, prepare/test bounded PID/instance/cleanup checks and obtain fresh UAC approval.
  Earlier one-prompt approvals are used. Focus/input work requires agreement to its time under AGENTS.md.
- [ ] Optimize the attributed owner while preserving quality and behavior. Add independent tests and compare
  matched ordinary Release controls in both orders, preserving every failure.
- [ ] Close long memory qualification only after the resource contract passes and the reproduced failure has a
  supported explanation. Update the parent/normative contracts; move this record to Done and remove its index row.

The cold target-tier gap and physical first-changed-pixel/touch qualification stay separately owned by the parent.
They are not closed by this record or by merging the implemented branch.
