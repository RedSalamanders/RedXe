# RedXe RedMonitor adoption

Status: ACTIVE qualification. The canonical detailed execution plan is
[RedMonitor extraction and adoption](https://github.com/RedSalamanders/RedMonitor/blob/main/Specs/Plans/WIP/RedMonitorExtraction_2026-10-21.md).
The current contract is [RedMonitor integration](../../Core/Core_RedMonitorIntegration.md).

- [x] Inspect current main and preserve diagnostics, crash and provider ownership.
- [x] Implement the bounded labelled native adapter and explicit installed-helper command.
- [x] Replace managed viewer builds/staging with one vendored client header, MIT notice and immutable lock.
- [x] Add local-only verification and explicit two-file update tooling.
- [x] Update contracts, usage and application-only packaging with the client MIT notice.
- [x] Run offline client controls and repository validators/format checks on the revised boundary.
- [x] Build all six profiles and run required x64 Debug/Release/ASan suites, including actual ASan detection.
- [x] Verify the actual app ETW Unicode/severity/PID/TID probe, missing-helper behavior and clean app-only package.
- [x] Retain reviewed receipts, fold changes, commit and push the implementation branch.
- [x] Pass the native ARM64 Debug/Release/ASan Full matrix after the host-selection repair.
- [ ] Close physical input/accessibility, controlled performance and release gates.

Earlier 2026-10-23 managed-viewer checkpoints remain historical evidence in Measurements and .build.
They do not qualify this revised dependency boundary. Tests never require an installed helper.
No ordinary build invokes the dependency updater. The viewer owns its separate Release packaging.
ARM64 repair: default PreferredToolArchitecture to x64 on an AMD64 host while preserving explicit overrides. Earlier all three ARM64 builds passed with an explicit x64-host override after default-host C1060; the corrected normal wrapper and standalone restore share one environment scope, preserve overrides and restore the caller environment. All three ARM64 profiles and all three x64 Full noninteractive profiles passed; independent tooling and app-only portable extraction passed.

Final PrePush passed all 15 requested scopes. Actual application ETW probes and explicit absent-helper commands passed in all three x64 profiles. The portable ZIP contains the SDK MIT notice and no viewer executable or satellites. Reviewed source, dependency, binary, run and package identities are retained in Measurements/RedMonitor_Adoption_2026-10-23. These checks do not establish native ARM64 runtime, physical input/accessibility or paired performance qualification.

Hosted run 39943731651 passed tooling and all three x64 profiles, but failed all three native ARM64 builds on the DxUi/compiler-host identity guard before runtime tests. The follow-up selects the native OS host for both restore and compilation (including emulated shells), with six real MSBuild host/override controls. Earlier local cross-builds and the pushed be0e2bf checkpoint remain valid only for their recorded scope; the revised tooling and hosted native ARM64 profiles require new qualification.

Run 39947763357 passed tooling, all x64 jobs, and native ARM64 Debug/ASan. Native ARM64 Release built but failed a combined idle-CPU/notification assertion; its original log does not identify which predicate failed. A deterministic witness now forces a notification to arrive after the owner drained the result. The idle test settles that finite tail before one unchanged 250-ms/10000-100ns-budget sample and emits failure diagnostics. The production queue is unchanged. The revised test requires new focused/native qualification; a passing repeat alone will not establish the cause of the earlier failure.

Final hosted [run 39954330299](https://github.com/RedSalamanders/RedXe/actions/runs/39954330299),
at `a55901c1fad009dab2fbd068002846a33ead2160`, passed tooling and all six native Full jobs.
Each profile executed all 15 scopes; actual ARM64 execution now qualifies the corrected build/test
boundary. Debug/Release also passed the native AV resource fixture. Downloaded started/completed
receipts, all recorded payload stream hashes and DxUi provenance were independently checked; reviewed
identities are in Measurements/RedMonitor_Adoption_2026-10-23/native-hosted-validation.json.
The original failed idle run remains failed and its cause remains unproven. No timing budget was
relaxed. Physical input/IME/AT, paired performance and release publication remain open.

## Main integration - 2026-10-27

The approved integration combines current main with the retained MSVC 14.52 work and the standalone header-only RedMonitor client. Qualification of this combined source is recorded separately from earlier branch receipts. Historical passes and remaining native, interactive, performance and release gates are retained; the merge does not close them automatically.
