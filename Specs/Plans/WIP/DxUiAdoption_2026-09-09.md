# DxUi adoption and build matrix (I19 consumer slice)

## Progress checklist

- [x] Audit current pin d192e474e540adc2656e69b8e8150b0f7a05d63f and the three archive consumers.
- [x] Unchanged x64 Debug product test.ps1 passes at 75545887d002b5075d170d98b7bfd3a77d8d7f61.
- [x] Unchanged Release test.ps1 passes at the audited product revision from the isolated Z:\RxI19 checkout.
- [ ] All 25 native projects and the solution support six configurations with real ASAN instrumentation.
- [ ] Exact candidate restore, compiler/SDK/flag isolation, advisory check and actual linked-module provenance pass.
- [x] Provenance tests reject wrong repository/pin/API/profile, incomplete/duplicate module closure and changed binaries.
- [x] Candidate 8c548fe2de3cdb5af858c457e9bef47d81506878 builds x64 Debug with zero warnings/errors and passes the complete product test.ps1.
- [ ] Candidate product tests, paired resources, native x64/ARM64 CI and rollback pass.
- [ ] Normative specs, user-facing compatibility notes and I19 checklist reconciled.

Status: ACTIVE. Implements RedXe's C1/C3/C6 slice of RedSalamander I19, authorized 2026-09-09.
The cross-repository checklist remains in RedSalamander's
`Specs/Plans/WIP/DxUi_SharedLibraryAdoptionAndReleasePlan_2026-09-09.md`.
Owning contracts: [DxUi integration](../../Core/Core_DxUiIntegration.md),
[build process](../../Build/Build_Process.md), [performance/resources](../../Core/Core_PerformanceAndResources.md).

Retain static linking, current Slider behavior, host-owned scheduling and COM/POD ownership boundaries.
Use exact reviewed pins and a manual upgrade/fix/retest loop; no automatic lock updater or periodic service.
ASan Debug uses the debug CRT with actual instrumentation, not an alias for Debug. Existing real IME,
touch, screen-reader and AV hardware/resource HOLD gates retain their AV owner and are not waived here.

Baseline Debug log: `.build/logs/I19-baseline-Debug-20260909-134249.log`. Release preflight correctly
rejected the independently running original output (PID 50484), without terminating it. The clean
baseline checkout at `Z:\src\RedXe-worktrees\dxui-i19-adoption` hit a Windows dependency-restore path-length
failure. The short checkout `Z:\RxI19` completed the unchanged Release suite; its log is
`.build/logs/I19-baseline-Release-20260909-134952.log`. Baseline test logs establish their actual tested configuration;
concurrent build activity is not accepted as paired performance evidence.

Six native CI jobs are implemented. Private DxUi access needs the repository/organization `DXUI_READ_TOKEN`
secret; no repository secret was visible during setup. This is a pending environment gate, not a passed CI result.

Candidate Debug evidence is retained under `Measurements/DxUiAdoption/2026-09-09/`. Its build took 108 seconds;
an unchanged repeat took 16 seconds with no native compilation. This is observed warm-build behavior, not a
matched cold-build speed comparison. The first test attempt exposed duplicate hidden-window arguments in the
updated runner; those were removed, and the full rerun passed. Native product behavior was unchanged by that fix.
The prior Release output was archived with its PDBs and hash in the local `RedXe-I19-baseline-20260909`
temporary directory before replacing the short checkout. Rollback execution and paired resources remain open.
