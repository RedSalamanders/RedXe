# Delivery red-team 2026-10-31

Status: not started
Date: 2026-10-31

This plan is a non-normative execution record. It does not change product behavior. The index marks it
`ACTIVE` because the work is executable; nothing in the checklist has been started.

It is the ship list from a two-pass review of the last 15 days of RedXe (`f86fa016` through
`c61861c`). [`DeliveryReview_2026-10-31.md`](DeliveryReview_2026-10-31.md) remains the product-review
evidence annex for the pinned DxUi menu defect, the probe-wording edit, and the first package command.
Execute those items from this file; keep the annex for line numbers and the catalog sentence.

**Verdict.** No RedXe product C++ High exists on `origin/main`. Two pipeline Highs and the pinned DxUi
menu defect must close before this tip is called qualified. Publication still fail-closes on
`conclusion == success`, so a failed test branch cannot winget-publish through the official function.

Do not treat the annex sentence “no RedXe product blocker or high defect” as the ship list. It is true
of RedXe C++ and false of the delivery pipeline.

## Baseline

Reviewed tip: `origin/main` `c61861ca771ac0c224273b47df3ccf25b5dad86a` (2026-10-31, merge of PR #73).
Window: `f86fa016` (PR #45) through that tip. Engine pin: `e32cf9f9` (`RedSalamanders/.github`).
DxUi lock: `616104f3` on `ej/wall-compat-redxe-20261028`, API revision 4. That commit is not an
ancestor of DxUi main. The keyboard-cancel commit `762983a0` is not an ancestor of the pin.

Re-read every anchor on the tip before editing. The local checkout `ej/unified-ci-20261030` at
`a459ff9` is stale and dirty; branch from `c61861c`, not from that worktree.

The annex reviewed product tip `8c882636`. Packaging, CRT-from-compiler, provenance roles, v1.0.6
policy, and VcpkgRestore landed after that tip. P0-3 packages `c61861c`, not `8c882636`.

## Checklist

P0 must close before this tip is called qualified. P1 before a public Release. P2 in the same closeout
window. P3 stays open.

- [ ] P0-1. Engine AND-gate: production starts only after both native Release test branches succeed.
- [ ] P0-2. Production receipts attest only the pair this run wrote, and `SmokePassed` comes from smoke.
- [ ] P0-3. Run production `package.ps1` on `c61861c` and match both ZIP hashes to their sidecars.
- [ ] P0-4. Port submenu hover-timer cancellation onto `ej/wall-compat-redxe-20261028` and re-pin RedXe.
- [ ] P1-1. HostPluginTests owns the RedMonitor provider for its whole process lifetime.
- [ ] P1-2. Delete the stale `PROCESSOR_ARCHITECTURE` ARM64 gate in `test.ps1`.
- [ ] P1-3. Make the three `--redmonitor-probe` exit-2 descriptions name a rejected write.
- [ ] P1-4. Pin-bump RedMonitor so `LaunchInstalledViewer` sets the viewer directory as CWD.
- [ ] P1-5. A `RedXe.exe` whose WMI path is blank cancels the build; it is not skipped.
- [ ] P1-6. ConstructionWork idle fails if the worker is already dead.
- [ ] P1-7. PackagingTests PE-checks architecture of the profile binaries and required CRT DLLs.
- [ ] P1-8. One winget alias spelling in `project.json`, templates, docs, and `Build_Packaging.md`.
- [ ] P1-9. Changelog and CI entrypoints have `#Requires`; fold fetch validates `Remote` and `Branch`.
- [ ] P2-1. Recursive deletes and plugin staging refuse reparse points under the stated PowerShell floor.
- [ ] P2-2. VcpkgRestore serializes like DxUi restore and names a half-clone recovery.
- [ ] P2-3. `Invoke-ToolingTests.ps1` fails when the folder has a suite the allowlist omitted.
- [ ] P2-4. TestRunEvidence sets `Closed` only after `completed.json` readback; NTFS rename is not a tautology.
- [ ] P2-5. Docs and tree drift: add `VcpkgRestore.psm1` to the `AGENTS.md` Build tree (`RedMonitor.props` is already listed), replace the `release.yml` clause in `Build_Process.md` line 333, stop `CaptureNativeArtifacts` hunting `DxUi.*.exe` names, and reconcile the skill TODO scan's multiline code-span mismatch.
- [ ] P2-6. Document the DxUi pin-bump provenance hazard: current pin has no `NativeBuildPolicy`.
- [ ] P2-7. Automatic admission binds the coordinator’s own successful quiet wait (engine).
- [ ] P2-8. setup-msvc Authenticode-only plus `vswhere -latest` after an isolated install (engine).
Standing limit, not a task: RedMonitor physical input, paired performance, and post-integration native ARM64 stay open on [`RedMonitor_Adoption_2026-10-23.md`](RedMonitor_Adoption_2026-10-23.md). Do not check them from this plan.

## P0-1. Engine AND-gate

High. Engine, then a RedXe pin bump. A RedXe-only YAML edit fails the template check.

Confirmed on engine `e32cf9f9`, `native.yml`: `production` `needs: [admission, test-gate]` but `if:` is
only `needs.admission.outputs.admitted == 'true'`. Any job `if:` drops GitHub’s implicit `success()`.
Production then downloads only the admission artifact and builds/uploads 90-day ZIPs even if the test
gate failed, skipped, or was cancelled. `complete` later records failure. `Get-CiPublicationPlan` still
requires `conclusion == success`. This violates
[`Build_UnifiedCI.md`](../../Build/Build_UnifiedCI.md) step 3 (both architectures before either
production job). It does not silently publish.

Repair:

1. In the engine, require `needs.test-gate.result == 'success'` and keep admission. Prefer also binding
   the test-gate artifact / regression receipts into production so a ZIP cannot exist without the AND.
2. Add an engine test that a failed, skipped, or cancelled test-gate does not start production and does
   not upload ZIPs.
3. Bump RedXe `.ci/project.json` `engine.ref` and every workflow caller together.

Acceptance: a forced failed x64 Release test branch produces no production ZIP. ARM64 production does
not start. `complete` records failure. Publication remains `conclusion == success`.

## P0-2. Production receipt leftovers

High. RedXe `Tools/Invoke-ProjectCi.ps1` lines 70-74.

After `package.ps1 -SkipBuild`, Production globs `*-$Platform-*.zip` under `.build/packages`. Historical
`RedXe-1.0.5-x64-Portable.zip` rides with a `1.0.7` run. `SmokePassed $true` is hardcoded even when
smoke is skipped.

Pass exactly `$package.ZipPath` and `$symbols.ZipPath` (or the names from `Get-RedXePackageName` /
`Get-RedXeSymbolArchiveName` for the resolved version). Refuse leftovers. Set `SmokePassed` from the
smoke object (`ExecutionSkipped` / exit), not a literal `$true`.

Acceptance: a dirty `.build/packages` with an older portable ZIP fails Production. A clean run’s
receipt lists exactly two archives for this version. Skipped ARM64-on-x64 smoke is not attested as
passed.

## P0-3. Production package of `c61861c`

Process. Operator. Retarget the annex P2 off `8c882636`.

CRT-from-compiler, provenance roles, v1.0.6 policy, and VcpkgRestore landed after the annex tip.
Nothing on `c61861c` has been packaged. Run from a checkout of `c61861c` itself:

```powershell
.\package.ps1 -Platform x64
```

No `-SkipBuild` and no `-SkipExecution`.

Acceptance: both ZIPs exist from that production-only Release build. Recompute each file’s SHA-256 and
match it to the sidecar that run wrote. Record the two paths, both hashes, and the
`DxUi.provenance.json` identity inside the portable ZIP. A mismatch or a packaging failure is a new
defect. After P0-1, P0-2, or P0-4 land, re-run on that later commit; this run does not qualify a later
pin.

## P0-4. Menu hover timers in the pinned library

Medium. Product ship item. The defect is already in the DxUi commit RedXe ships.

Execute the annex section “DX-2. Menu hover timers in the pinned library” on pin `616104f3`. Do not
retarget `Dependencies/DxUi.lock.json` to DxUi main: that drops four pin-only commits, and current
DxUi main imports its own `NativeBuildPolicy`, so RedXe’s provenance hash would attest the wrong file
(P2-6). Change only the lock `commit`. Keep `"apiRevision": 4`. Never run `Update-DxUi.ps1`.

Acceptance is the annex DX-2 acceptance, then a new P0-3 of the later RedXe commit.

## P1-1. HostPluginTests provider lifetime

Medium. `Tests/HostPluginTests/HostPlugin.Tests.Runner.cpp` `wmain` (9304-9470).

`MonitorDiagnostics::Initialize()` has one call site (`RedXe/Main.cpp:481`). HostPluginTests compiles
the same `MonitorDiagnostics.cpp` and `PluginHost.cpp` but never initializes and never calls
`PluginHost::ShutdownProcessRuntime()`. `Diagnostics()` is a function-local static constructed on the
first log, after `PluginHost::Instance()` at line 9383. C++ destroys it before the `PluginHost`
unique_ptr deleter runs `Shutdown()`, which logs `service-stopped`, `launch-stop-timeout`, and
`construction-stop-timeout`.

`TestServiceLifetime` stops services before return, so today’s green run often misses the
use-after-destruction. A leftover worker or a new test that leaves a service started is ASan/exit
undefined behavior on a delivery gate. Product `RedXe.exe` is ordered correctly.

Mirror `Main.cpp` on every HostPluginTests entry, including the `--construction-work` and soak early
returns: `Initialize()` before `Instance()`, `ShutdownProcessRuntime()` before return. Optionally make
a late `Log` no-op after a process-wide teardown flag.

Acceptance: ASan Debug HostPluginTests starts a service and returns without ASan reports.
`service-stopped` is written while the provider is still live.

## P1-2. Stale `PROCESSOR_ARCHITECTURE` gate

Medium. `test.ps1` lines 100-101.

Lines 75-77 already use `RuntimeInformation.OSArchitecture` (the real rule, matching `build.ps1`).
Lines 100-101 then throw when x64 PowerShell on ARM64 Windows reports `PROCESSOR_ARCHITECTURE=AMD64`.
`build.ps1` was fixed so that host can build ARM64; smoke then refuses after a successful build.

Delete lines 100-101. Keep 75-77.

Acceptance: on ARM64 Windows, x64 `pwsh` `.\test.ps1 -Platform ARM64` reaches the smoke. On x64
Windows, `-Platform ARM64` still throws at 75-77.

## P1-3. Probe exit 2 wording

Low. Documentation. Execute the annex section “P1. Probe exit 2 wording.” Do not change the exit code,
`RunProbe`, or `DescribeProbeResult`. `TraceLoggingWrite` cannot actually surface a rejected write
today; that is why this stays docs.

The three files that still say `2 no listener` only, at `c61861c`:

- `RedXe/CommandLine.h` lines 122-124
- `docs/usage.md` line 176
- `Specs/UI/UI_XeneonDisplayWindowing.md` line 52

Use the catalog sentence already written in the annex. Keep the first 24 characters contiguous.

## P1-4. Viewer current directory

Medium. RedMonitor `Client.h` `LaunchInstalledViewer` (line 314), then a RedXe ThirdParty pin.

`CreateProcessW` passes `lpCurrentDirectory = nullptr`, so the child inherits RedXe’s CWD.
`--redmonitor` and the Debug optional launch both reach it. `test.ps1` starts RedXe with
`-WorkingDirectory $repoRoot`. Release ordinary startup does not launch the viewer.

Pin bump: pass the viewer’s directory (strip the filename from `path`). RedXe cannot wrap the inline
`CreateProcessW` without forking the header. Keep discovery to the two production Release locations.

Acceptance: launch from a hostile CWD; the child CWD is the viewer install directory. The hash lock
matches. Plugins still do not compile `Client.h`.

## P1-5. Blank WMI path

Medium. `Build/BuildOutputProcess.psm1` lines 47-50.

`if (-not $candidate.ExecutablePath) { continue }` skips a running `RedXe.exe` whose path WMI cannot
read. The build then overwrites the output. The contract is: identify the exact path and do not kill.

If any named `RedXe.exe` has a null or empty `ExecutablePath`, throw with the PID and “path
unavailable; close it or rerun elevated.” Do not `continue`. Same for a `GetFullPath` failure.

Acceptance: a process named `RedXe.exe` with a blank path cancels the build and is not terminated.

## P1-6. ConstructionWork idle is non-vacuous

Medium. `Tests/HostPluginTests/HostPlugin.Tests.ConstructionWork.cpp` lines 242-250.

A worker that has already returned freezes `GetThreadTimes`; a zero delta greens “idle STA blocks on
events.” `Running()` is only joinability.

Before and after the 250 ms sample, require `WaitForSingleObject(Thread(queue), 0) == WAIT_TIMEOUT`
and a zero exit `FILETIME`. Optionally enqueue a canary after the window and require `Run`. Keep the
1 ms budget only after those proofs.

Acceptance: a killed or exited worker fails the idle check. A live blocking worker still passes.

## P1-7. PackagingTests architecture

Medium. `Tests/BuildProcessTests/PackagingTests.ps1` and `Build/Package.psm1` CRT discovery.

CRT fixtures are text files named `msvcp140.dll`. Discovery never opens a PE. The installer
round-trip copies whatever is in the profile folder. `CameraPackageTests` already rejects a wrong PE
machine; this suite does not.

After resolving profile binaries and CRT, assert `Get-CameraBinaryMachine` equals `$Platform` for
`RedXe.exe`, `RedXeLauncher.exe`, and each required CRT DLL (reject non-PE). Negative: an
opposite-machine PE in a correctly named CRT folder must refuse. Call `Test-RedXePortablePackage` on a
zip that plants a wrong-machine CRT.

Acceptance: text-file CRT fixtures fail. Opposite-machine PE in an `x64` CRT folder fails. Matching
PE and names pass.

## P1-8. Winget alias case

Medium. `.ci/project.json` `winget.alias` is `redxe`. Installer winget templates
`PortableCommandAlias` is `RedXe`.

Pick one spelling (templates `RedXe` matches the launcher product name) and make `project.json`,
templates, docs, and `Build_Packaging.md` identical. Add a BuildProcess test that the strings match.

Acceptance: one alias token everywhere. The test fails if they drift.

## P1-9. Changelog and CI `#Requires`

Medium. `Tools/Fold-Changelog.ps1`, `Tools/Test-Changelog.ps1`, `Tools/Submit-ChangelogFold.ps1`,
`Tools/Invoke-ProjectCi.ps1`, and `Build/Changelog.psm1`.

NOTES demand PowerShell 7 / 7.4; none of those files have `#Requires`. A 5.1 host dies inside
`ArgumentList` or the 3-arg `File.Move` after the fold lock is held. `Submit-ChangelogFold` interpolates
unsanitized `-Remote` / `-Branch` into a force-update (`+`) fetch.

Add `#Requires -Version 7.2` (7.4 for `Invoke-ProjectCi.ps1`). Validate `Remote` and `Branch` as a
single ref name (`^(?!-)[\w./-]+$`, no `:`). Put `--` before the remote operand.

Acceptance: Windows PowerShell 5.1 fails at parse. A `Remote` containing `:` or starting with `-` is
refused. Fold still works with `origin` / `main`.

## P2. Same closeout window

These are not ship-blocking. Keep them in this plan so they are not lost.

### P2-1. Junction-safe recursive deletes

Raise the floor to `#Requires -Version 7.1` (or 7.4 to match the CI notes). Before every
`Remove-Item -Recurse` in `Build/DxUiRestore.psm1` (270, 317, 498), `Build/Package.psm1` (319, 404),
and `Tools/Submit-ChangelogFold.ps1` (117): refuse if the directory or any ancestor down to the owned
`.build` prefix is a reparse point. Plugin staging (`Package.psm1:283`) must skip or refuse reparse
entries the way `Get-PackageReparseProblem` does.

### P2-2. VcpkgRestore mutex

Lease `.build/vcpkg-tool` and `.build/vcpkg-registry` with the DxUi mutex helper, or one lock on
`.build`. On verification failure of a managed checkout, refuse with an explicit “delete this folder”
message. Use `checkout --force --detach` only after dirty, foreign, and reparse checks. Compare the
prefix with `OrdinalIgnoreCase`. Optionally require the Microsoft vcpkg URL.

### P2-3. Tooling-test allowlist lock

After the foreach in `Tests/BuildProcessTests/Invoke-ToolingTests.ps1`, assert the invoked set equals
every `*.ps1` in that folder minus `{Invoke-ToolingTests.ps1, PackagingTests.ps1}`. Fail naming extras
and missing. Keep Packaging excluded with a comment that `test.ps1` owns it.

### P2-4. TestRunEvidence closeout

`Complete-TestRunEvidence` must set `Closed=$true` only after `completed.json` readback succeeds. The
rename-tamper test must, on NTFS refusal, require a sharing or access-denied native code and that
`Test-Path` still holds the original identity. On ReFS keep the existing reject. Log `SKIP` for the
hard-link case on non-NTFS.

### P2-5. Docs and tree drift

- `AGENTS.md` Build tree already lists `RedMonitor.props` (confirmed 2026-11-01). Add `Build/VcpkgRestore.psm1`. The file exists; the tree omits it.
- `Build_Process.md` line 333 still names `release.yml` inside the `ci.yml` cancellation sentence. `Build_Packaging.md` says that workflow was removed. Replace the `release.yml` clause with the current `qualify.yml` / `publish.yml` path. Re-read the line before editing; it is not a separate `release.yml` paragraph.
- SkillValidation skips fenced lines (`Build/SkillValidation.psm1`, `Get-MarkdownFence`) but still scans raw whole lines outside fences. A whole-line `[TODO: example]` inside a matching multiline code span is incorrectly rejected. Keep reconciliation open: scan `Get-MarkdownProse`, already used by package integrity, or explicitly narrow `Build_SkillPackages.md` to describe fence-only detection.
- `Save-TestRunNativeArtifacts` in `Build/TestRunEvidence.psm1` still matches `DxUi.(ControlTests|EmbeddedTests|FoundationTests|InteractiveLease).exe`. Drop that match or match RedXe test process names.

### P2-6. Pin-bump provenance hazard

Document in `Core_DxUiIntegration.md` and `Build_Process.md`: the current pin has no
`NativeBuildPolicy`; RedXe’s injected copy is what compiles the archive. A lock retarget to current
DxUi main would make the provenance hash attest the wrong file. P0-4 stays on the pin branch until a
recorded check that the four pin-only commits and UI Automation focus cancel are on the candidate.

### P2-7. Automatic admission binding

Engine. Tighten `Invoke-CiAdmission` so automatic mode requires the coordinator’s own successful
`Wait-CiMainQuiet` conclusion, not any 300-second-old same-SHA `ci.yml` run. Keep `qualify.yml`
`mode: manual` as the documented operator path, or make `automatic` the default and name the
break-glass.

### P2-8. setup-msvc latest after isolated install

Engine. Hosted provisioning trusts Authenticode Microsoft subject, then `vswhere -latest`. After an
isolated install this can pick a different Visual Studio. Track on the engine. Not a RedXe product
bug.

## P3. Standing limits

Process. Do not check boxes in [`RedMonitor_Adoption_2026-10-23.md`](RedMonitor_Adoption_2026-10-23.md)
from this review. Physical input/accessibility, paired-performance, and the post-integration native
ARM64 runtime gate stay open. P1-2 unblocks the ARM64 smoke path; it does not close that gate.

Focus-taking and interactive suites remain a separate gate. No scoped pass closes them.

Older ACTIVE WIP plans (MergedReviewFixes, RFC remaining publishers, PluginDashboard closeout,
BoundaryHardening G2/H3, HostChrome leftovers, RuntimeMemoryAndLatency) are not this 15-day delta.
Delivery can ship without closing them only if their unchecked items are already classified as
post-1.0. Confirm that classification explicitly; do not infer it.

## Suggested order

1. P0-3 package of `c61861c`. If it fails, that is the first defect. If it passes, record hashes.
2. RedXe-only change or small set of changes: P0-2, P1-2, P1-3, P1-5, P1-1, P1-6, P1-8, P1-9. Each
   implementing change keeps its own `Changes/` fragment.
3. Engine pull request, then RedXe pin bump: P0-1. Do not ship a production batch from `e32cf9f9`
   after the AND-gate is known missing if one engine commit can wait.
4. DxUi pin branch, then RedXe lock-only: P0-4. New P0-3 of that later RedXe commit.
5. RedMonitor pin (can parallel step 4): P1-4, then the RedXe ThirdParty bump.
6. Tests hardening (can parallel step 2): P1-7, then P2-3 and P2-4.
7. Tooling hygiene: P2-1, P2-2, P2-5, P2-6.
8. Engine follow-ups: P2-7, P2-8.

## Dropped

These are closed. Do not reopen them as work items. The annex Dropped list still applies; this review
also cleared:

- Engine quiet-window “Highs”: `qualify.yml` default `mode: manual` is the documented operator path.
  Automatic admission accepting any 300-second-old same-SHA run is a write-access spoof path, tracked
  as P2-7, not a silent hole in the coordinator wait.
- MSBuild “spec names `fa3de6c9`”: `Build_Process.md` line 341 already says the lock file is the only
  place a DxUi commit is recorded.
- MSBuild provenance hashing RedXe policy while DxUi compiles its own: not a ship defect on pin
  `616104f3` (no `NativeBuildPolicy` there). Tracked as P2-6 for a future main retarget.
- `prWorkflowDigest` freshness: `prCoverage` is empty; the runtime path never reads the field.
- RedMonitor `/t:ClCompile` skip of the pin gate: only `RedXe` and `HostPluginTests` compile
  `Client.h`. Untested on a real Visual Studio 18 `/t:ClCompile`, not an established skip.
- Weather DST, yyjson 0.13 call sites, Debug viewer auto-launch in Release, crash-handler install
  order, probe `Sleep`, symbols ZIP PDB fail-closed, action-name `char` versus `unsigned char`,
  soak working-set budget “unused,” deleted Settings `Parse*` / `CopyText`, MatrixRain `Hash`,
  GpuQuery `FindKmtHandle`.

## Closeout

P0-1 through P0-4 and P1-3 can finish independently once their owners exist. P3 stays a standing
limit on the RedMonitor plan. When those implementation items are done, their durable wording is in
the owning contracts, the RedMonitor gates are still open there, and this file plus the annex have
moved to `Specs/Plans/Done/` with their index rows removed.

P1-3’s usage sentence is the `docs/` edit. Do not copy the rest of this review into `docs/`. Each
implementing change adds its own `Changes/` fragment. The DxUi library change uses DxUi’s own
fragment. The engine change lives in `RedSalamanders/.github`.

## Contracts the later editors change

- Engine (`e32cf9f9` and after): `native.yml` production `if:`, then RedXe `.ci/project.json` and
  every workflow caller. Optional P2-7 and P2-8 on the same engine.
- RedXe Production adapter: `Tools/Invoke-ProjectCi.ps1`.
- DxUi, on `ej/wall-compat-redxe-20261028`: `src/Controls/DxUi.Menu.cpp`,
  `Tests/Controls/DxUi.Tests.Menu.cpp`, and `Specs/UI/UI_InputAndAccessibility.md`. Then RedXe
  `Dependencies/DxUi.lock.json` only, API revision staying 4.
- RedXe probe wording: `RedXe/CommandLine.h`, `docs/usage.md`, and
  `Specs/UI/UI_XeneonDisplayWindowing.md` in one change.
- RedXe HostPluginTests lifetime, `test.ps1` ARM64 gate, ConstructionWork idle, PackagingTests PE,
  WMI blank path, winget alias, changelog `#Requires`, junction deletes, VcpkgRestore mutex,
  tooling-test allowlist, TestRunEvidence closeout, and the P2-5 / P2-6 docs.
- RedMonitor `Client.h` `LaunchInstalledViewer`, then RedXe ThirdParty pin and lock hash.
- No edit to `Specs/Plans/WIP/RedMonitor_Adoption_2026-10-23.md` that checks the gates in P3.
