# DxUi adoption and build matrix (I19 consumer slice)

## Progress checklist

- [ ] Requalify the candidate at DxUi ecad707 after the native module registration correction; retain the green 52da33d product receipts as the previous qualified pin.

- [x] Requalify the consumer at DxUi 52da33d: full local x64 Debug, Release and ASan Debug product suites pass with zero build warnings/errors, and product 1bf6662 passes all six native CI profiles (34368430957), including real ARM64 ASAN detection. The previous 8c548fe pin remains available for rollback; paired resources and hardware/manual gates stay open.
- [x] Audit current pin d192e474e540adc2656e69b8e8150b0f7a05d63f and the three archive consumers.
- [x] Unchanged x64 Debug product test.ps1 passes at 75545887d002b5075d170d98b7bfd3a77d8d7f61.
- [x] Unchanged Release test.ps1 passes at the audited product revision from the isolated Z:\RxI19 checkout.
- [x] All 25 native projects and the solution build all six configurations with ASAN compiler enforcement; native ARM64 runtime and detection qualify in the retained six-profile CI receipts.
- [x] Exact candidate restore, compiler/SDK/flag isolation, advisory check and actual linked-module provenance pass in all six local builds.
- [x] Provenance tests reject wrong repository/pin/API/profile, incomplete/duplicate module closure and changed binaries.
- [x] Candidate 8c548fe2de3cdb5af858c457e9bef47d81506878 builds x64 Debug with zero warnings/errors and passes the complete product test.ps1.
- [x] Candidate complete x64 Debug, Release and ASan Debug suites pass, including deliberate sanitizer fault detection.
- [x] Retained prior Release package passes AV Control, host/plugin integration and hidden WARP application checks after the candidate qualification.
- [x] Product 2ef0bcb with DxUi 8c548fe passes all six native x64/ARM64 CI profiles (34359693390), including ASAN detection and module provenance.
- [ ] Paired product resource acceptance remains open.
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

Six native CI jobs are implemented. Anonymous HTTPS clone and public Actions API access to DxUi were
verified on 2026-09-09. No custom token or secret is required; the workflow uses the automatic job token
only for advisory API requests. Native CI at product 2ef0bcb passes all six configurations (34359693390).
The 52da33d upgrade is also qualified in all six native configurations at product 1bf6662 (34368430957);
the retained receipt is `Measurements/DxUiAdoption/2026-09-09/native-ci-1bf6662.json`.

Candidate Debug evidence is retained under `Measurements/DxUiAdoption/2026-09-09/`. Its build took 108 seconds;
an unchanged repeat took 16 seconds with no native compilation. This is observed warm-build behavior, not a
matched cold-build speed comparison. The first test attempt exposed duplicate hidden-window arguments in the
updated runner; those were removed, and the full rerun passed. Native product behavior was unchanged by that fix.
The prior Release output was archived with its PDBs and hash in the local `RedXe-I19-baseline-20260909`
temporary directory before replacing the short checkout. The retained package was hash-verified and extracted into a new directory after candidate tests. AV Control, host/plugin and hidden WARP application checks all passed from that directory. The rollback receipt and logs are retained alongside all six candidate build/provenance receipts. Paired product resources remain open; the earlier 8c548fe native matrix passed.
