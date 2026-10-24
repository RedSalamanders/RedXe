# MSVC 14.52 and Release optimization

Status: DONE for local x64 tests and ARM64 cross-builds
Date: 2026-10-23
Repository: RedXe
Base: 596b6eb476d0a9bf7e68fc078a73533c6a400d23

## Checklist

- [x] Isolate this work on ej/msvc1452-release-20261023; preserve the original checkout.
- [x] Retain the original x64 Release build, image sizes/hashes and frozen runtime closure under .build/msvc1452.
- [x] Audit all six profiles before changing policy. Both sides select installed/default MSVC 14.52.37025; there is no retained 14.51 installation, so no compiler-version speedup is claimed.
- [x] Require MSVC 14.52.x and enable Release /Gy /GL /OPT:REF and full /LTCG with non-incremental linking.
- [x] Forward the exact compiler and WPO policy to the unchanged adopted DxUi API pin; give the changed archive policy a separate fingerprint/output root.
- [x] Verify evaluated native profiles, transitive archive settings and broken-policy controls; update build/consumer specs and documentation.
- [x] Execute the local build/native/tooling matrix; passing receipts, failed attempts and remaining acceptance limits are recorded below.
- [x] Compare final image/archive sizes, build duration and unchanged-fixture runtime/resource samples. Retain original failures, interrupted/noisy attempts and activity observations.
- [x] Publish local reviewed evidence and settings differences, fold the changelog and validate. Move to Done only after required local qualification is complete.

## Measurement and scope

The DxUi plan of the same name coordinates the five repositories. This repository owns its application measurements and its existing dependency API adoption. The original and candidate binaries must use identical fixtures and the same exact toolset; the Release compiler policy is the intended experimental difference. An initial sample collected alongside other builds is diagnostic, not a quiet-host/non-regression pass. Retain frozen baseline images and every attempt; do not silently rebaseline.

Resource-only projects have no C++ bodies. Independently owned pinned repositories and DLL boundaries remain separate build/optimization units. Existing feature/performance holds are not closed by this compiler-policy change.

Generated raw evidence: .build/msvc1452/audit-before, baseline-build.json, baseline-bin and initial native benchmark reports. Review and retain selected local evidence before closeout.
## Qualification workspace and current gates

The user authorized E:\RedXe.Perf on 2026-10-23 for worktrees/builds/evidence. Final qualification lives in E:\RedXe.Perf\msvc1452 on ej/msvc1452-qualification-20261023; original Z: baselines remain retained and hash-verified frozen copies exist under E:\RedXe.Perf\msvc1452-frozen. This repository's DxUi API pin is unchanged. Source edits during initial builds were correctly rejected by attestation; retain those attempts and rerun against frozen inputs.

The user approved interactive tests when ready. DxUi preserves its Start/Cancel and desktop-restoration checks. The user explicitly approved the visible 30-second automatic-start countdown for remaining retries. Native ARM64 execution is unavailable on this x64 host. Hosted CI provisions and verifies the required 14.52 family through .github/actions/setup-msvc; the download/install branch remains pending a real GitHub-hosted execution. Local -VerifyOnly and refusal controls must pass without installing software. All required gates stay open until their receipts are reviewed.

- [x] Verify the 64-bit ARM64 compiler-host correction for all three configurations; retain C1060 attempts and original Native32Bit lastbuildstate.

## Local qualification checkpoint

Updated 2026-10-23T13:06:21.8133932Z. All six local builds, all three refreshed x64 native matrices and independent tooling passed. ARM64 uses the matching 64-bit compiler host and dependency identity. A read-only clang-format 22 survey found existing differences in Common/Version.h and Common/WindowCapture.cpp; neither source was edited.

Native ARM64 runtime and a real hosted CI provisioning run remain unavailable/pending. Local qualification is complete within the agreed scope. Cross-builds do not establish native ARM64 support or close existing feature/performance holds. Paired runtime/resource analysis and reviewed image sizes are recorded in Measurements/Toolchain/2026-10-23-msvc1452/; the consolidated report is retained externally under the approved E:/DxUi.Perf/reports/Msvc1452ReleaseOptimization_2026-10-23/. All first-party native C++/header/shader files remain unchanged.


## Agreed local completion scope (2026-10-24)

The user explicitly limited this task to local x64 tests and ARM64 cross-builds. Native ARM64 runtime and actual hosted CI provisioning remain separate handoff gates; they do not keep this local compiler-policy plan active after its local obligations pass. The original reviewed size/performance study is retained with its instability and causal limits. The requested final validation refresh is recorded separately from those original receipts. No native application or dependency source is changed by this documentation reconciliation.

- [x] Refresh the three Full x64 runs and all required cross-build evidence against the final handoff source identity.
- [x] Reconcile final receipts, move the locally completed plan to Done and validate its indexes/links.

## Local closeout (2026-10-24)

The scope-refresh matrix passed six builds and Full x64 Debug, Release and ASan Debug with the original native deadlines and all ordinary/tooling assertions. Exact commands and times are retained in Measurements/Toolchain/2026-10-23-msvc1452/LocalValidation.json. This closes this repository's local compiler-policy work. Native ARM64 execution and actual hosted provisioning remain separate handoff gates. No first-party native source or adopted DxUi API pin changes in this reconciliation. The original reviewed size/runtime study remains historical and retains its instability limits; concurrent functional-run timing supplies no new performance claim. The coordinating five-repository DxUi plan remains active for the other repositories.

Plan archival changes prose and indexes. Final handoff rechecks those files and refreshes any build/test receipt whose owning identity includes prose. The refreshed evidence is stored under Measurements, without relabeling earlier receipts.