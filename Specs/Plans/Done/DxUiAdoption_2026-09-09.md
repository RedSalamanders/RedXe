# DxUi adoption and build matrix (I19 consumer slice)

## Completion checklist

Status: **DONE, 2026-09-13**, for the implemented consumer adoption and accepted local
qualification scope. ARM64 and ASan follow-up is explicitly deferred by the user;
AV hardware and real-client acceptance retain their existing AV owner.

- [x] Consume canonical `DxUi.lib` through exact-pin restore and isolated build imports.
- [x] Preserve Slider behavior, host scheduling and module-local C++ ownership across the COM/POD ABI.
- [x] Support Debug, Release and ASan Debug for x64/ARM64; retain historical build/runtime receipts.
- [x] Pass the corrected local x64 product suites and module-provenance checks.
- [x] Retain the prior package and successful rollback smoke.
- [x] Implement advisory updates and the manual upgrade/fix/retest loop, using public dependency access.
- [x] Retain matched resources and the user's explicit acceptance on 2026-09-13.
  Release dirty composition adds 2.6-5.2 microseconds at 96 DPI; completed throughput
  varies from -1.76% to +0.17%. Surface/hidden-work budgets and all thresholds stay unchanged.
- [x] Persist the accepted trade-off and remaining capability boundaries in the integration/resource contracts.
- [x] Transfer further ARM64 and ASan qualification to the user-owned cross-product follow-up,
  `Specs/Plans/WIP/DxUi_DeferredPlatformQualification_2026-09-13.md` in RedSalamander.
- [x] Review end-user documentation: this closeout changes dependency, validation and governance
  records only; it introduces no new visible user flow or gallery change.

The native source tested locally is `f727932`; the selected library pin is `ecad707`.
The current documentation closeout changes no compiled inputs. No hosted CI is used.
Publishing/merging a consumer release remains a separate action; this record does not
claim that RedSalamander's ongoing final Full suite or product merge is complete.

## Historical implementation and qualification record

The dated progress below is retained as history. Former open wording is superseded
by the completion scope above and the current domain contracts.

## Progress checklist

Current closeout scope (2026-09-13): the user deferred ARM64 and ASan to later
personal qualification and requested completion of the remaining I19 work.
RedSalamander owns the cross-product deferral in
`Specs/Plans/WIP/DxUi_DeferredPlatformQualification_2026-09-13.md`.
Existing RedXe x64 product and paired resource evidence remains applicable;
no new native code is changed by this documentation closeout.
The measured resource trade-off has been presented for the remaining developer decision.

- [x] Candidate at DxUi ecad707 passes all three local x64 product suites with zero warnings/errors.
- [x] Reproduce and fix the cumulative Process Viewer diagnostic assertion exposed in duplicate native CI. Repeating the lifecycle case fails before the fixture correction and the full Debug suite passes afterward; production behavior is unchanged.
- [x] Corrected repeated lifecycle test passes the complete local Debug, Release and ASan Debug suites.
- [x] Corrected repeated lifecycle test passes all six native CI profiles at a812cec (34374884161).
- [x] Retain the native CI attempts for the new resource fixture at 80b02a3 that were denied before any
  step ran (34377362060 / 34377358597). The user requested local validation instead; do not wait for billing
  resolution or launch CI during this pass. This does not erase the green historical a812cec product matrix.
- [x] Remove duplicate feature-branch push matrices. PR updates retain all six native jobs; main pushes and manual
  dispatch remain supported. The two earlier duplicate runs are retained as evidence, not a requirement to spend twice.
- [x] Run four matched AV measurements per Debug/Release profile against original pin d192e474 and candidate
  ecad707 in ABBA order on September 12. Fixture/product source identity and surface/hidden-work assertions pass;
  all eight receipts and A/A, B/B comparisons are retained under `Measurements/DxUiAdoption/2026-09-12`.
- [x] Repeat eight matched AV runs locally on September 13 with identical fixture/product inputs and processor
  affinity mask 255. The earlier 11.9–14.4% Release dirty-throughput loss does not recur: all Release candidate
  scenarios range from -1.76% to +0.17%. Surface storage and hidden work are unchanged. Raw receipts, A/A and B/B
  comparisons, the incomplete first attempt and its subsequent Debug rebuild are retained in
  [the local record](../../../Measurements/DxUiAdoption/2026-09-13/README.md).
- [x] Cross-build frozen `f727932` locally in ARM64 Debug, Release and ASan Debug, with zero warnings
  and errors. All three archive consumers per profile match their actual module bytes and DxUi pin.
  Retain the [six build/provenance receipts](../../../Measurements/DxUiAdoption/2026-09-13/arm64-builds/README.md).
  This qualifies the new fixture's compilation; native ARM64 runtime on this revision remains unverified.
- [ ] Resolve performance acceptance: Release dirty composition still costs an extra 2.6–5.2 microseconds
  at 96 DPI in both pairs; other component timings and process memory vary. Developer advice on the measured
  tradeoffs is pending. No threshold or baseline is waived or replaced.

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

Historical status: ACTIVE. Implements RedXe's C1/C3/C6 slice of RedSalamander I19, authorized 2026-09-09.
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
