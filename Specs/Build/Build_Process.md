# RedXe build-process contract

Status: current normative repository contract
Last reviewed: 2026-10-07
Owner: root build and test entrypoints

## Scope

This contract owns the root build and test entrypoints: command-line build output selection and the running-output
preflight, MSBuild selection, test-process failure reporting, the streaming child-process runner, and scoped test
selection, reuse and PR coverage. Compiler, warning, dependency, and generated-output policies remain in `AGENTS.md` and
the Visual Studio project files.

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

The native CI matrix runs the full product test gate (`test.ps1 -Full -SkipTooling`) for all six configurations on every
push to `main` and on manual dispatch; a pull request runs only the x64 Release leg, so PR feedback stays fast. The full matrix
(ASan and ARM64 included) is the release gate: the release workflow refuses a commit whose validation push run on
`main` has not succeeded (see [`Build_Packaging.md`](Build_Packaging.md)). DxUi is public:
HTTPS source restore requires no personal token or repository/organization secret. The advisory API check
uses the automatic read-only job token in CI and works anonymously locally. API unavailability or rate
limiting produces an advisory notice and leaves the exact pin unchanged.

Build outputs remain under `.build/<Platform>/<Configuration>/`, with intermediates under `.build/Intermediate/`.
The root `test.ps1` exits zero only after every required assertion has passed. Expected nonzero exits from isolated
negative-test children must not become the test entrypoint's success exit code.

A test process never waits on a dialog. Every native test executable calls `Common/FailureReports.h` first thing in
`wmain`, `RedXe.exe` calls it as soon as its command line has `--self-test` or `--screenshot`, before either
runs, and `AVControlBroker.exe` calls it when it is started as a synthetic helper, which only tests do.
- In a Debug or ASan Debug build, a failed runtime check (an STL range check, a CRT assertion) writes its report to
  stderr and ends the process with exit code 3, as the CRT's Abort button would, instead of opening its modal
  Abort/Retry/Ignore box. The report MUST NOT go through a CRT stream, whose default "C" locale stops at the first
  character above U+00FF (a source path under such a user or folder name): it is written as UTF-8 to a pipe or a file
  and as UTF-16 to a console, through a fixed stack buffer. Every run of a RedXe process in `test.ps1` and in the
  package smoke of `Build/Package.psm1` therefore decodes its stderr as UTF-8, and its stdout too
  (`-StandardErrorEncoding` and `-StandardOutputEncoding`, below).
- In a process that calls the header, exit code 3 MUST mean only a failed runtime check and exit code 4 only an
  `abort()` that no such check reported: no other path of such a process, a fixture child mode or a watchdog
  included, ends with either. That `abort()` (an `assert()`, a direct call, or in a test executable `std::terminate`
  after an unhandled exception) writes one line saying so to stderr and ends the process with exit code 4, in every
  configuration. `RedXe.exe` keeps its own terminate handler, which writes a crash dump and ends with exit code 127
  ([`Core_CrashHandling.md`](../Core/Core_CrashHandling.md)), so in its self-test exit code 4 covers only an `assert()`
  or a direct `abort()`. The CRT resets the abort handler to its default just before calling it, and the handler
  re-arms itself first, so only a second `abort()` on another thread in that instant can still end with the CRT's own
  exit code 3. `assert()` and the CRT's runtime-error messages go to stderr, never to a message box, in the
  GUI-subsystem `RedXe.exe` as well. Windows Error Reporting's dialog and critical-error boxes are suppressed.
- `test.ps1` runs `PluginContractTests.exe --failure-report-self-test`, a hidden switch that fails such a check on
  purpose with a report that carries a character above U+00FF (U+0141). It requires the whole report, that character
  intact, and exit code 3 in Debug and ASan Debug, and exit code 0 in Release, which has no such checks. The run is
  bounded by two minutes, so a routing that stopped working fails there. It takes place in a background job, whose
  hidden console is its own, under console output code page 437, which has no U+0141, so it proves the UTF-8 decoding
  whatever code page the caller's console uses. A console another process shares is never changed.
- `test.ps1` runs `PluginContractTests.exe --abort-self-test`, a hidden switch that reports as a GUI-subsystem process
  does and fails an `assert()` (Debug and ASan Debug) or calls `abort()` (Release). It requires the abort line, the
  assertion's text where `assert()` is compiled in, and exit code 4, within two minutes.
- `test.ps1` runs `RedXe.exe --self-test --warp` bounded by the same budget as the test executables and keeps its
  output in `.build/<Platform>/<Configuration>/RedXe.self-test.log`. A failed self-test check MUST name itself, with
  its HRESULT when there is one, on stderr as well as on the debugger output, and end the run with exit code 6.
  `test.ps1` proves it on a copy of `RedXe.exe` without the `Settings` folder beside it, whose settings check fails,
  and requires that every return of `Application::RunSelfTest` other than its success goes through that one report
  (`FailSelfTest`) and that none writes to the debugger output alone. The self-test opens no log directory, so the
  host also writes its Warning and Error records to stderr as JSONL lines (`Plugins_API.md`): `test.ps1` requires that
  a copy with the `Settings` folder but without the `Plugins` folder fails a check with `0x8007007E` beside a
  `module-map-failed` record naming a plugin, and that the passing smoke log holds the one
  `window-kind-switch-failed` Warning of the self-test's rolled-back kind switch.
- Every other run of `RedXe.exe` or `RedXeLauncher.exe` in `test.ps1` (`--help`, an unknown switch, the end-to-end
  `--screenshot` runs, the crash harness and its invalid directory) and in the package smoke of `Build/Package.psm1`
  goes through `Invoke-RedXeStreamingProcess` with a budget and a log, and keeps its exit-code check. `RedXe.exe --help`
  writes the catalog to a redirected stdout as UTF-8
  ([`UI_XeneonDisplayWindowing.md`](../UI/UI_XeneonDisplayWindowing.md)): `test.ps1` runs it from a background job like
  the routing check's, under console output code page 437, and requires the catalog's title with its U+2014 intact,
  which only the UTF-8 decoding of stdout keeps.
- A test executable never turns an HRESULT into its exit code: its low byte can be 0, which passes the run, or 3. A
  failed suite prints its HRESULT and returns 1.
- A new test executable calls the header first, so an unattended run on a developer's desktop never holds a dialog.
  `BuildProcessTests.ps1` checks that every `wmain` under `Tests/` starts with
  `RedXeFailureReports::RouteAwayFromDialogs()` (comments aside), and that `RedXe/Main.cpp` routes an unattended run
  before its first argument check and before its `Application` exists; the rule is tried on a sample that breaks it.
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
exit code, report the child's process identifier through `-ProcessId`, and decode output alike. Both decode each
stream with the console output code page by default, as `Process.Start` does, so a build tool's output (MSBuild,
`cl`) reads the same on either path. `-StandardOutputEncoding` and `-StandardErrorEncoding` name another encoding for
one stream each: `test.ps1` and the package smoke pass UTF-8 for both streams of every RedXe process they run, whose
stderr carries `Common/FailureReports.h`'s UTF-8 reports and whose stdout carries `RedXe.exe`'s UTF-8 command-line
text (`--help`, an argument error) or a test executable's narrow text, which `/utf-8` compiles as UTF-8.
`BuildProcessTests.ps1` proves, from a background job whose own console uses code page 437, that each named encoding
decodes its own stream alone on both paths and that a stream without one keeps the code page. The survivor check
follows parent processes, so a descendant that carries no marker (`ping.exe`) still counts. `build.ps1` keeps the
unbounded default; `test.ps1` applies a fifteen-minute budget to every standalone test executable and to the crash
harness, two minutes to its `--help`, unknown-switch and routing checks, and shows the HostPlugin and HostSmoke log
tails, which never stream to the console, for a run ended at its budget as well as for a failing exit code.

`Invoke-RedXeStreamingProcess` (`Build/StreamingProcess.psm1`) MUST also hold to the following. `BuildProcessTests.ps1`
covers a child that never stops writing, the exit grace with a drained last line, a child that starts nothing and exits
while a slow callback presents its backlog for longer than the grace (every line presented, its exit code returned, no
timeout record), a stop of a silent child with and without a budget (stopped within seconds, no survivor), and an
edited launcher definition imported into a session that compiled the original.

- Both start paths MUST build the child's command line with the same quoter. It leaves an argument as it is unless it
  is empty or holds white space or a quote; otherwise it encloses it in quotes as `ProcessStartInfo.ArgumentList` does,
  escaping each quote with a backslash and doubling only the backslashes that precede a quote or the closing quote, so
  the child's C runtime parses every argument back unchanged. A call without arguments, or with `$null` for them,
  passes none. `BuildProcessTests.ps1` passes, on both paths, every argument shape that `test.ps1`, the package smoke
  and `build.ps1` use (switches, a switch and its value, a path with spaces, a `name=path` pair, an MSBuild property
  with a space) and the shapes a quoting mistake breaks (an empty argument, a tab, quotes, backslashes before a quote
  and at the end of a quoted argument, non-ASCII text), and requires the child to receive each one unchanged.
- The budget is counted on a monotonic clock from the child's start, so neither the helper's own setup (compiling its
  job type on first use) nor a change of the system time moves it. It is checked on every pass of the read loop, so a
  child that never stops writing is terminated at its budget like a silent one.
- A bounded child that has exited while a process it started still holds its output open is terminated with that
  tree ten seconds after its exit, not left to its budget, and the call reports the child's exit code and that a
  process it started kept its output open, instead of a child that did not finish. Once the child has exited only this
  grace applies, so output it left in the pipes as its budget ran out is read, never taken for a hang.
- An open pipe at the end of the grace is no such process by itself: a backlog the child left, or a slow callback
  presenting it, can outlast ten seconds. The grace therefore ends the run only while a process of the job still runs
  (the job's accounting reports an active process); with none left, the pipes hold only what the tree already wrote,
  and the helper keeps draining them to end of file and returns the child's exit code. Draining is bounded by progress,
  not by the budget: pipes that deliver no line for 30 seconds while no process of the job runs can only be held by a
  handle outside the job, so the helper abandons the wait and reports the exited child's code and the stalled output.
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

The independent tooling runner, `Tests/BuildProcessTests/Invoke-ToolingTests.ps1`, MUST work on a clean checkout before
any product build: the CI `tooling` job runs it after installing only Python and `Build/requirements-validation.txt`,
while `test.ps1` runs it as the `BuildProcess` suite after its own build step. It therefore restores the exact pinned
DxUi source first, the source only, because the restore tests inspect that source. It builds no product and installs no
vcpkg package. Its PowerShell suites create their fixtures under `.build`; the Python validator tests use a temporary
folder.

Changes to this contract require:

```powershell
.\test.ps1 -Full -Configuration Debug -Platform x64
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

Active native test sources, helpers, fixtures and seams MUST use `Scope.Tests.Something.h/.cpp`. `Tests/native-test-files.json` owns active membership. Sources under `Specs/` and `Measurements/` (archived harnesses, sealed reproduction inputs) are historical: they keep their original names, are never taken for active test sources and MUST NOT enter the inventory. Rename project entries, includes, current inventories and callers together. Outside `Tests/` and `SelfTest/` folders, a `.cpp` or `.h` file is an active test source, which the inventory MUST then list, when its name has a `.Tests.` segment in any spelling or a `Test`, `Mock` or `Fake` name component spelled as the naming rule spells it (`FakeClock.h`, `MockHost.cpp`, `WeatherTest.cpp`). Those letters inside a word (`Attestation.h`, `LatestRelease.cpp`, `Mockingbird.h`) MUST NOT make a product file a test source.

`Test-Changes.ps1` is the ordinary iteration entrypoint. Its default is affected coverage, including committed changes since a local merge base and independent staged, unstaged, deletion, rename-side and untracked discovery. No ref is fetched. Unknown executable inputs widen coverage; prose alone does not require native tests. A Markdown file selects only the tooling scope, whose validators read prose, and any other file under `docs/`, `Measurements/` or `Mockups/` selects nothing. Explanations name paths, consumers and fallback reasons. Explicit selectors reject unknown scopes. Affected, filtered and environment-reduced coverage MUST NOT be reported as a full repository pass.

Every `Test-Changes.ps1` run that does not fail ends with a coverage label, except `-Explain`, which ends with the plan. An empty affected plan MUST end with `NOTHING_SELECTED; repository NOT_EVALUATED` and exit 0: it evaluated nothing, so it is never a gate; `test.ps1 -Full` and `-Mode Full` are. A Git failure in scope discovery MUST name the Git command and its exit code. A base ref that does not resolve, or one that shares no history with `HEAD` (a shallow or unrelated clone), fails affected selection with that failure and the remedy: fetch, deepen, pass `-BaseRef`, or run the full gate; so does a tree without Git or a work tree. An ordinary `test.ps1` call (only `-Platform`, `-Configuration` or `-SkipBuild` bound) runs this affected selection; when it cannot compare (no Git, no work tree, no `origin/<default branch>`, no shared history) `test.ps1` MUST say why and run every suite, as `-Full` does.

Successful whole-scope results are reused only for equal repository content, complete executable/DLL/PDB output closure, stable deployed settings/schema and DxUi provenance bytes, architecture, configuration, scope, runner and environment. Every required deployed input must exist and the binary closure must be nonempty before an identity is established. Changes or deletion of those deployed inputs invalidate reuse; generated reports and test-owned fixtures do not enter that stable runtime closure. Build attestation binds source inputs to those artifacts before SkipBuild. The attestation and native identities leave out Markdown files and the `docs/`, `Measurements/`, `Mockups/` and `Specs/Plans/` folders, which no build reads; tooling identities include them, because the validators read prose. Invalid, missing, failed, interrupted or concurrently mutated evidence is never reusable. Force bypasses test-result reuse. Independent tooling receipts are shared across profiles because their execution has no profile argument. Receipt identity is content-based; staging/committing the same source tree does not itself invalidate it.

`Test-Changes.ps1` MUST resolve one build number per run and pass it to `build.ps1 -BuildNumber` and `test.ps1 -BuildNumber`, and the build attestation MUST bind it. The number is the commit count of the merge base of `HEAD` with `origin/<default branch>`, or the default build number ([`Build_Packaging.md`](Build_Packaging.md)) when that ref does not resolve. A commit therefore leaves the version stamp, the binaries and native receipts unchanged; moving to a newer base changes the stamp, relinks the version-stamped binaries and invalidates native receipts, and SkipBuild then refuses with "Run without -SkipBuild" before any test runs. Binaries built this way carry the base's version, not a release number: `test.ps1 -Full -SkipBuild` or an explicit `-Suites` run on them needs the same `-BuildNumber`, which the run prints.

Git output is decoded as UTF-8 whatever the console code page. A path Git lists that is not a file on disk leaves the source identity only when Git reports it deleted (or it is a nested repository's directory); any other unresolvable path fails the identity instead of silently dropping out of it.

The environment identity covers the machine, OS build and update revision (UBR), architecture, PowerShell, the Direct2D, Direct3D, WARP, DirectWrite, DXGI and WIC system runtimes by hash, the Git version, the Visual Studio and Windows SDK installations, and the sanitizer, CI, processor, `PATH` and toolset environment variables. Tooling receipts also cover the Python interpreter and PyYAML version that `validate-skills.ps1` and `Invoke-ToolingTests.ps1` select. Receipts store only digests, never these values.

When the inputs of a scope that passed change while the run executes, `Test-Changes.ps1` MUST NOT record that scope, MUST still record the scopes whose inputs did not change, MUST warn naming the changed scopes, and MUST end with `SELECTED_PASSED; NOT_RECORDED` and exit 0: the selected tests passed on the tree as it started, and the current tree is not covered. A source change during the build still fails the run before any test executes, because its binaries could mix both trees.

`-Scopes` accepts a comma list as `test.ps1 -Suites` does, also as the single argument `pwsh -File` passes. Both entrypoints apply one platform rule: ARM64 runtime tests need an ARM64 host, and x64 also runs on an ARM64 host under emulation; the architecture in the environment identity keeps that evidence apart from native x64 evidence.

`-Mode Full` selects the full local obligation. `-Mode PrePush` accounts for full coverage across local execution and the forthcoming PR gate. Each `prCoverage` entry of `Tests/test-scopes.json` is one PR check, named as GitHub reports it. A check that names a platform and configuration (`native (x64, Release)`) covers that native profile only; one that names neither (`tooling`) covers its profile-independent scopes for every local profile, so a Debug PrePush delegates `BuildProcess` too. Delegation requires a clean committed candidate, the enabled GitHub workflow, matching candidate workflow bytes and their reviewed manifest digest, a `pull_request` trigger, and no change on `origin/<default branch>` since the merge base to the workflow, `test.ps1`, the manifest, `Build/ScopedTesting.psm1` (which `test.ps1` reads the manifest through) or the tooling commands, because a PR runs the workflow and runner of its merge with the base. It also requires that the default branch require the check: the rules GitHub reports for that branch (`gh api repos/<repo>/rules/branches/<branch>`) MUST name it as a required status check, since a check that is not required lets a failing or pending run merge. Until the branch requires it, PrePush keeps that check's work local. Staged, unstaged and untracked inputs retain obligations locally; a dirty tree or changed HEAD during the coverage lookup also rejects delegation. Nightly and weekly jobs are not PR coverage. Workflow/API uncertainty keeps obligations local. Every refusal MUST print `PR delegation not used: <reason>`. A delegated run reports `CI_PENDING` (`LOCAL_OBLIGATIONS_PASSED; CI_PENDING`, or `LOCAL_OBLIGATIONS_NONE; CI_PENDING` when nothing runs locally), never repository `PASSED`. Main/release acceptance remains separate because its merge tree, configuration matrix or requirements can differ.

Every suite `Test-Changes.ps1`, `test.ps1` and CI run is noninteractive. Timing, native ARM64 execution, real devices, IME and assistive technology, foreground input and the live checks a domain contract names stay separate qualification work after agreement to the time: noninteractive iteration and CI do not claim those manual gates, and scoped selection adds no screenshot or desktop automation.

`Tests/test-scopes.json` owns the standalone scope and verified PR-profile mapping. A test project's folder selects its own suite, while common support and unknown paths retain conservative fan-out. New source/build/runtime/runner dependencies must extend the relevant manifest and focused regression coverage.

The manifest's scope names are the suite names. `test.ps1` takes `-Suites` and its default from them and runs each suite in exactly one `if ('<Scope>' -in $Suites)` block. `ScopedTesting.Tests.ps1` MUST check that every manifest scope has exactly one such block and every block names a manifest scope; that every rule matches at least one tracked path; and that every input of a project whose executable a suite runs selects that suite or the full fallback. Those inputs are the project's items, its project references and the quoted `#include` closure, resolved beside the including file, then through the project's include directories and `Common`. A rule that misses a consumer therefore fails the tooling suite instead of narrowing selection.

AVControl, Launcher, Logicon and Zoom edits also select Settings because that suite compiles their models/settings or consumes their shared settings constraints. A test project file (`Tests/<Project>/<Project>.vcxproj`) also selects PluginContract, HostPlugin, HostSmoke and Packaging: every project builds into the shared output folder, where the files it deploys and the product projects it references reach those suites. Its `.filters` file, which only the IDE reads, stays with its own suite. Skill metadata (`.agents/**`), `validate-skills.ps1`, `Build/validate_skills.py` and `Build/requirements-validation.txt` select only `BuildProcess`.

CI invokes explicit full gates rather than the affected default. The forthcoming PR executes its candidate workflow merged with the base; a reviewed local workflow digest, a runner the base has not changed since, the enabled GitHub workflow and checks the default branch requires gate delegation. Workflow edits invalidate that digest until the scope contract is reviewed and updated. RedXe's independent Windows tooling/skill tests execute once in its tooling job. Every RedXe workflow job that runs `test.ps1` MUST pass `-SkipTooling` or install `Build/requirements-validation.txt` itself; `ScopedTesting.Tests.ps1` checks each workflow. Hosted timing investigations still use paired, fixture-matched resource evidence; cached historical observations are not new measurements.

PR delegation MUST account for conditional workflow jobs: a check covers a scope only for the changes it runs on. Both RedXe PR checks run on every pull request, because `ci.yml` has no path filter and no job condition, so the scopes they cover do not depend on what changed. A workflow edit that adds a filter or a condition changes the reviewed digest, which keeps every obligation local until the manifest accounts for it.
