# RedXe build-process contract

Status: current normative repository contract
Last reviewed: 2026-10-07
Owner: root build and test entrypoints

## Scope

This contract owns command-line build output selection and the running-output preflight. Compiler, warning, dependency,
and generated-output policies remain in `AGENTS.md` and the Visual Studio project files.

## Exact-output process preflight

Before dependency installation, cleaning, rebuilding, or building, `build.ps1` MUST inspect running `RedXe.exe`
processes. A build invocation does not prove ownership of an independently launched process and MUST NOT terminate
one. It MUST reject only a process whose `Win32_Process.ExecutablePath`, after absolute-path normalization, equals the
selected target `.build/<Platform>/<Configuration>/RedXe.exe` using an ordinal case-insensitive comparison.

- An exact-path match MUST fail the build with its process identifier, executable path, and command line so the user
  can identify, close, and retry it.
- Same-name processes from another checkout, platform, configuration, or path MUST remain running and MUST NOT block
  the build.
- The preflight MUST NOT call a process-termination API. Enumeration or selected-target path-normalization failure
  MUST fail the build with an explicit statement that no process was terminated.
- `-Run` performs the same preflight, builds successfully, and only then launches the new target executable.

This policy prevents an opaque `LNK1168` output replacement failure while ensuring the build never terminates an
independently launched or unrelated RedXe process.

## MSBuild selection

`build.ps1`, `vcpkg-install.ps1` and `restore-dxui.ps1` MUST select MSBuild the same way, through `Find-RedXeMSBuild` in
`Build/DxUiRestore.psm1`: `MSBUILD_EXE_PATH` when that file exists, then `msbuild.exe` on `PATH` (a Developer
PowerShell's), then the first x64 MSBuild of an installation `vswhere -all -prerelease` reports, then a scan of the Visual
Studio folders. `build.ps1` passes its choice to both scripts as `-MSBuildPath`; run on their own, they select the same
one. The vcpkg overlay triplets and the DxUi identity files therefore do not depend on which entrypoint ran last
([`Core_DxUiIntegration.md`](../Core/Core_DxUiIntegration.md)).

## Output and validation

Every native project and solution mapping supports Debug, Release and ASan Debug on x64 and ARM64.
ASan Debug uses `/MDd`, disabled optimization, program-database debug information and real AddressSanitizer
instrumentation in every first-party translation unit; a forced compiler guard rejects unsanitized builds.
The application and plugin output directories each have one sanitizer-runtime staging producer. The test
entrypoint requires an isolated use-after-free to produce the sanitizer's diagnostic; an ordinary crash is
not successful detection. ARM64 runtime tests require native ARM64 execution.

The native CI matrix runs the ordinary product test entrypoint for all six configurations on every push to `main`
and on manual dispatch; a pull request runs only the x64 Release leg, so PR feedback stays fast. The full matrix
(ASan and ARM64 included) is the release gate: the release workflow refuses a commit whose validation push run on
`main` has not succeeded (see [`Build_Packaging.md`](Build_Packaging.md)). DxUi is public:
HTTPS source restore requires no personal token or repository/organization secret. The advisory API check
uses the automatic read-only job token in CI and works anonymously locally. API unavailability or rate
limiting produces an advisory notice and leaves the exact pin unchanged.

Build outputs remain under `.build/<Platform>/<Configuration>/`, with intermediates under `.build/Intermediate/`.
The root `test.ps1` exits zero only after every required assertion has passed. Expected nonzero exits from isolated
negative-test children must not become the test entrypoint's success exit code.

A test process never waits on a dialog. Every native test executable calls `Common/FailureReports.h` first thing in
`wmain`, `RedXe.exe` calls it as soon as its command line has `--self-test`, before the self-test runs, and
`AVControlBroker.exe` calls it when it is started as a synthetic helper, which only tests do.
- In a Debug or ASan Debug build, a failed runtime check (an STL range check, a CRT assertion) writes its report to
  stderr and ends the process with exit code 3, as the CRT's Abort button would, instead of opening its modal
  Abort/Retry/Ignore box. The report MUST NOT go through a CRT stream, whose default "C" locale stops at the first
  character above U+00FF (a source path under such a user or folder name): it is written as UTF-8 to a pipe or a file
  and as UTF-16 to a console, through a fixed stack buffer.
- Exit code 3 MUST mean only a failed runtime check. An `abort()` that no such check reported (an `assert()`,
  `std::terminate` after an unhandled exception, or a direct call) writes one line saying so to stderr and ends the
  process with exit code 4, in every configuration. `assert()` and the CRT's runtime-error messages go to stderr, never
  to a message box, in the GUI-subsystem `RedXe.exe` as well. Windows Error Reporting's dialog and critical-error boxes
  are suppressed.
- `test.ps1` runs `PluginContractTests.exe --failure-report-self-test`, a hidden switch that fails such a check on
  purpose with a report that carries a character above U+00FF. It requires the whole report and exit code 3 in Debug
  and ASan Debug, and exit code 0 in Release, which has no such checks. The run is bounded by two minutes, so a routing
  that stopped working fails there.
- `test.ps1` runs `PluginContractTests.exe --abort-self-test`, a hidden switch that reports as a GUI-subsystem process
  does and fails an `assert()` (Debug and ASan Debug) or calls `abort()` (Release). It requires the abort line, the
  assertion's text where `assert()` is compiled in, and exit code 4, within two minutes.
- `test.ps1` runs `RedXe.exe --self-test --warp` bounded by the same budget as the test executables and keeps its
  output in `.build/<Platform>/<Configuration>/RedXe.self-test.log`. A failed self-test check MUST name itself, with
  its HRESULT when there is one, on stderr as well as on the debugger output, and end the run with exit code 6.
  `test.ps1` proves it on a copy of `RedXe.exe` without the `Settings` folder beside it, whose settings check fails.
- Every other run of `RedXe.exe` or `RedXeLauncher.exe` in `test.ps1` (`--help`, an unknown switch, the crash harness
  and its invalid directory) and in the package smoke of `Build/Package.psm1` goes through
  `Invoke-RedXeStreamingProcess` with a budget and a log, and keeps its exit-code check.
- A test executable never turns an HRESULT into its exit code: its low byte can be 0, which passes the run, or 3. A
  failed suite prints its HRESULT and returns 1.
- A new test executable calls the header first, so an unattended run on a developer's desktop never holds a dialog.
- The header also makes stdout and stderr unbuffered. A process terminated at its budget runs no exit code that would
  flush a CRT buffer, so every line a test process wrote MUST already be in the pipe, and its log names the case that
  hung rather than ending at a 4 KB buffer boundary several cases earlier. `test.ps1` runs
  `PluginContractTests.exe --unbuffered-output-self-test`, a hidden switch that writes a line and terminates its own
  process, and requires that line in the log.

`build.ps1` MUST begin with the framed RedXe product banner and identify the selected platform and configuration. The
banner MUST color-split RED from XE on color hosts and MUST include the XENEON EDGE build-signal tagline. In a plain
interactive console it MUST keep MSBuild attached directly so native color and message ordering are preserved. In
Codex, Windows Terminal, redirected, or non-interactive hosts it MUST capture and replay both MSBuild streams so
progress remains visible, coloring errors red, warnings yellow, and completed project outputs green without adding
terminal control sequences to the captured log. Every MSBuild invocation MUST write a uniquely named UTF-8 plain-text
log beneath `.build/logs/`, report diagnostic counts, and finish with its elapsed time and success or failure signal.

`Tests/BuildProcessTests/BuildProcessTests.ps1` MUST launch two harmless same-name fixture processes from distinct
paths, prove that the exact target blocks the build with identifying diagnostics, prove that both processes survive,
and remove its isolated artifacts. It MUST also validate terminal-path selection, output color classification,
diagnostic counting, banner identity, argument-safe streaming, combined logging, child exit-code propagation, and
the `-TimeoutSeconds` budget of `Invoke-RedXeStreamingProcess`: a child that outlives its budget is terminated with
its process tree (a descendant of the invocation, never an independently launched process), the partial output and a
`TIMEOUT:` record stay in the log, and the call throws naming the executable and the log. A bounded child is created
suspended and joins the kill-on-close job before its first instruction runs, so nothing it starts can escape the job;
an unbounded one starts through `Process.Start`, and both paths quote arguments, keep stream identity, propagate the
exit code, report the child's process identifier through `-ProcessId`, and decode output alike. The survivor check
follows parent processes, so a descendant that carries no marker (`ping.exe`) still counts. `build.ps1` keeps the
unbounded default; `test.ps1` applies a fifteen-minute budget to every standalone test executable and to the crash
harness, two minutes to its `--help`, unknown-switch and routing checks, and shows the HostPlugin and HostSmoke log
tails, which never stream to the console, for a run ended at its budget as well as for a failing exit code.

`Invoke-RedXeStreamingProcess` MUST also hold to the following. `BuildProcessTests.ps1` covers a child that never
stops writing, the exit grace with a drained last line, a stop of a silent child with and without a budget (stopped
within seconds, no survivor), and an edited launcher definition imported into a session that compiled the original.

- The budget is counted on a monotonic clock from the child's start, so neither the helper's own setup (compiling its
  job type on first use) nor a change of the system time moves it. It is checked on every pass of the read loop, so a
  child that never stops writing is terminated at its budget like a silent one.
- A bounded child that has exited while a process it started still holds its output open is terminated with that
  tree ten seconds after its exit, not left to its budget, and the call reports the child's exit code and that a
  process it started kept its output open, instead of a child that did not finish. Once the child has exited only this
  grace applies, so output it left in the pipes as its budget ran out is read, never taken for a hang.
- After a termination the helper drains both pipes, for at most five seconds, so output already written, a last line
  without a newline included, reaches the log and the callback before the `TIMEOUT:` record. The call throws only once
  no process of the job is left (or after five more seconds), so a caller that looks for survivors finds none.
- Every wait is sliced to at most half a second, because PowerShell honors Ctrl+C (a pipeline stop) only between
  statements: a stop takes effect within a slice whether or not the child writes. On a stop or an error before the
  child exited, the job ends a bounded child's tree and an unbounded child's tree is killed, so `build.ps1`'s MSBuild
  does not go on building unseen. An unbounded child that exited normally keeps its descendants, such as MSBuild's
  reusable nodes. Closing the terminal itself can end PowerShell before this cleanup runs.
- Every disposal runs even when an earlier one throws, and the job, whose last handle kills what the tree still runs,
  is created inside the guarded block and disposed last.
- The job also sets `JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION`: an unhandled exception ends the bounded process it
  happens in, even one that never calls `Common/FailureReports.h` (the crash harness, `--help`) and whatever error mode
  it inherited, instead of holding it in a Windows Error Reporting dialog. `BuildProcessTests.ps1` checks the flag
  from inside a bounded child.
- The compiled launcher's type names carry a digest of its C# source. A session cannot unload a compiled type, so an
  edited definition compiles under new names and a session that loaded an earlier one never runs stale code (nor
  records test evidence for it).

The stall fixture is the hardest containment shape: its child starts a grandchild that inherits the redirected pipe
and exits, so the pipe never reaches end of file and only job containment can reach the survivor. Its budget leaves
the child ample time to get that far on a loaded machine, and the child marks that it did, so the run MUST end on the
exit grace well before the budget, report the exited child, keep every line the child wrote (the last one without a
newline) in the log, and leave no survivor.

Tooling tests that create Git repositories MUST NOT depend on the developer's Git configuration. Their fixture commits
and restores run with commit signing, hooks and line-ending conversion off, so a signing prompt cannot hang a run and a
hook or `core.safecrlf` cannot fail one. `DxUiRestoreTests.ps1` gives Git an empty global configuration and no system one
for its whole run (`GIT_CONFIG_GLOBAL`, `GIT_CONFIG_NOSYSTEM`, restored afterwards). `ScopedTesting.Tests.ps1`, which
also runs Git on the product checkout, sets `commit.gpgsign`, `core.hooksPath` and `core.autocrlf` locally in each
fixture repository, which overrides global and system settings, and reports a failing fixture command with its output.

Changes to this contract require:

```powershell
.\test.ps1 -Configuration Debug -Platform x64
.\build.ps1 -Configuration Release -Platform x64
.\build.ps1 -Configuration Release -Platform ARM64
.\validate-skills.ps1
```


## Public DxUi access in CI

`RedSalamanders/DxUi` is public. Anonymous HTTPS Git access and public Actions API reads were verified
on 2026-09-09. Both consumers can restore their exact pin without configuring a secret. Sharing an
organization does not add any setup requirement. CI may use its automatic `github.token` for API rate
limits; this is provided by GitHub and is not a personal access token. Source URLs, logs and shipped
provenance contain no credentials. The existing advisory lookup also works without authentication.

## Scoped iteration and PR coverage

Active native test sources, helpers, fixtures and seams MUST use `Scope.Tests.Something.h/.cpp`. `Tests/native-test-files.json` owns active membership. Historical archived harnesses and sealed reproduction inputs keep their original identity; they do not define new source naming. Rename project entries, includes, current inventories and callers together. DxUi historical source/test origins retain their original identities and map to current paths separately.

`Test-Changes.ps1` is the ordinary iteration entrypoint. Its default is affected coverage, including committed changes since a local merge base and independent staged, unstaged, deletion, rename-side and untracked discovery. No ref is fetched. Unknown executable inputs widen coverage; prose alone does not require native tests. Explanations name paths, consumers and fallback reasons. Explicit selectors reject unknown scopes. Affected, filtered and environment-reduced coverage MUST NOT be reported as a full repository pass.

Successful whole-scope results are reused only for equal repository content, complete executable/DLL/PDB output closure, stable deployed settings/schema and DxUi provenance bytes, architecture, configuration, scope, runner and environment. Every required deployed input must exist and the binary closure must be nonempty before an identity is established. Changes or deletion of those deployed inputs invalidate reuse; generated reports and test-owned fixtures do not enter that stable runtime closure. Build attestation binds source inputs to those artifacts before SkipBuild. Invalid, missing, failed, interrupted or concurrently mutated evidence is never reusable. Force bypasses test-result reuse. Independent tooling receipts are shared across profiles because their execution has no profile argument. Receipt identity is content-based; staging/committing the same source tree does not itself invalidate it.

`-Mode Full` selects the full local obligation. `-Mode PrePush` accounts for full coverage across local execution and the forthcoming PR gate. Delegation requires a clean committed candidate, the enabled GitHub workflow, matching candidate workflow bytes and their reviewed manifest digest, and a matching native profile. Staged, unstaged and untracked inputs retain obligations locally; a dirty tree or changed HEAD during the coverage lookup also rejects delegation. Nightly and weekly jobs are not PR coverage. Workflow/API uncertainty keeps obligations local. A delegated run reports `CI_PENDING`, never repository `PASSED`; the PR must pass its required checks. Main/release acceptance remains separate because its merge tree, configuration matrix or requirements can differ.

RedSalamander defers only entries whose complete portable entry contracts equal the PR plan, retaining writers, in-product cases and extra scenarios locally. Its `-NonInteractive` option omits focus-taking entries and records incomplete coverage; it cannot alter an exact Resume. DxUi foreground suites remain explicit `test.ps1 -Interactive` work after agreement to the time. Noninteractive iteration and CI do not claim those manual gates. No screenshot or desktop automation is introduced.

`Tests/test-scopes.json` owns the standalone scope and verified PR-profile mapping; RedSalamander's `Tools/validation-impact.json` and canonical plan continue to own its affected execution. Specific standalone test-source rules use their semantic entry tags, while common support and unknown paths retain conservative fan-out. New source/build/runtime/runner dependencies must extend the relevant manifest and focused regression coverage.

AVControl, Launcher, Logicon and Zoom edits also select Settings because that suite compiles their models/settings or consumes their shared settings constraints.

CI invokes explicit full gates rather than the affected default. The forthcoming PR executes its candidate workflow; a reviewed local workflow digest and the enabled GitHub workflow gate delegation. Workflow edits invalidate that digest until the scope contract is reviewed and updated. RedXe's independent Windows tooling/skill tests execute once in its tooling job. DxUi's tooling has one Windows host qualification and one Linux portability qualification; its MSBuild-only staging fixture executes once on Windows; native runner/watchdog checks stay profile-specific. RedSalamander schedules skip unchanged commits only after successful matching workflow history; manual dispatch and unreadable/failed history execute. Hosted timing investigations still use paired, fixture-matched resource evidence; cached historical observations are not new measurements.

PR delegation MUST account for conditional workflow jobs. DxUi uses the existing NativeScope contract to keep native obligations local when a documentation-only PR skips native CI; always-running Windows/Linux tooling remains covered. Native success identities exclude validator prose while tooling identities include it.
