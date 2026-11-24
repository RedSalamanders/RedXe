# RedXe build-process contract

Status: current normative repository contract
Last reviewed: 2026-10-11
Owner: root build and test entrypoints

## Scope

`Tools/Build-LocalSdkCandidate.ps1` is an explicit local Release application build against
one independently hashed local SDK descriptor. It requires the selected app dependencies
to be restored first and unchanged during execution. `RedXeOutputRoot` owns application,
plugin, broker, camera and intermediate outputs; the ordinary `RedXeBuildRoot` dependency
tree remains separate. Every project refuses a noncanonical root unless the local SDK
contract and shared live output lease authorize it before writes. Non-DxUi plugin projects
call only the shared output verifier; they do not import SDK headers or link its archive.
RedXe and AVControl retain the complete SDK ABI guard. The existing application test surface
is unconditional, so the adapter explicitly supplies no test-property override. A retained
local build/link record never qualifies tests, native ARM64 runtime, packages or publication.

`test.ps1 -Configuration Release -SkipBuild -Suites HostSmoke` accepts an explicit
`-LocalSdkBuildReceipt` and independently retained `-LocalSdkBuildSha256`. Only this
application scope is supported by the application-target recipe: standalone suites,
Full, Rebuild, a different profile/build version, hosted execution or stale inputs
refuse. The shared runtime reader authenticates the original descriptor, actual
SDK/source/API/ABI/engine and complete payload, and holds it throughout the unchanged
hidden WARP, CLI and crash checks. Its actual executable resource supplies the build
number, which remains checked against the source version contract. Close-time
verification precedes any passing local result. Separate local SDK evidence never
satisfies ordinary pinned local, main, packaging or production gates.

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

`Build/VcpkgRestore.psm1` owns product vcpkg source restoration. `vcpkg-tool.json` selects the executable checkout
at `.build/vcpkg-tool`; the independent manifest `builtin-baseline` selects `.build/vcpkg-registry`, including its
versions database, ports and stock triplets. Both roles require full nonzero commits, the declared repository,
clean managed clones and verified final HEADs. Existing dirty, foreign, non-clone or reparse paths fail without
resetting or removing their contents. Both clones MUST be complete: vcpkg extracts each versioned port with
`git read-tree` and `git checkout-index` in an environment that holds only `GIT_INDEX_FILE`, where Git resolves no
host, so it cannot fetch an object a partial (`--filter=blob:none`) clone left on the server. A missing pin is
fetched with its history, never with `--depth`, and a partial or shallow clone an earlier restore made is completed in
place (unshallowed, then refetched without a filter, the filter setting removed only afterwards), keeping its HEAD
and working tree. The catalogue clone has independent Git objects; restoration changes neither
pin nor caller environment. `vcpkg-install.ps1` MUST pass `--vcpkg-root` explicitly because an executable located in
a valid checkout takes precedence over VCPKG_ROOT. Toolset overlays come from the selected catalogue and retain
the same MSBuild/SDK/CRT/platform policy. `VcpkgRestore.Tests.ps1` covers distinct pins, available port metadata,
warm restoration, preserved source/environment, dirty and foreign refusal, invalid identities, long port paths,
complete clones from a server that offers filtering, and in-place completion of earlier partial and shallow clones,
each read offline the way vcpkg reads it.

## Output and validation

Routine repository validators and their regression tests require PowerShell 7 and no Python or external YAML module.

Every native project and solution mapping supports Debug, Release and ASan Debug on x64 and ARM64.
Optimized Release projects MUST use full link-time code generation (`/LTCG`), preserving whole-program
optimization without `/LTCG:incremental`. The v145 incremental cache has reused old inlined member offsets after a
private `PluginHost` layout change, producing a Release page-activation hang despite successful compilation and
hidden tests. `Directory.Build.targets` sets the effective link mode after the toolset defaults;
`LinkConfiguration.Tests.ps1` evaluates every tracked native project's x64/ARM64 Release metadata and the host's
Debug/ASan modes without building. A changed source layout requires current artifacts and runtime qualification;
passing tests for the previous executable do not qualify a clean rebuild.
ASan Debug uses `/MDd`, disabled optimization, program-database debug information and real AddressSanitizer
instrumentation in every first-party translation unit; a forced compiler guard rejects unsanitized builds.
The application and plugin output directories each have one sanitizer-runtime staging producer. The test
entrypoint requires an isolated use-after-free to produce the sanitizer's diagnostic; an ordinary crash is
not successful detection. ARM64 runtime tests require native ARM64 execution.

Hosted CI follows [Build_UnifiedCI](Build_UnifiedCI.md): PR checks are compiler-free. After five quiet minutes on main,
native x64 and ARM64 test-enabled Release builds must execute `test.ps1 -Full -SkipTooling` and active assertion probes
before either production Release/ZIP job starts. Debug, ASan and extended qualification remain explicit manual
obligations; no scheduled native build replaces them. Publication requires the retained qualified artifact identity,
not a PR check or a new build (see [`Build_Packaging.md`](Build_Packaging.md)). DxUi is public:
HTTPS source restore requires no personal token or repository/organization secret. The advisory API check
uses the automatic read-only job token in CI and works anonymously locally. API unavailability or rate
limiting produces an advisory notice and leaves the exact pin unchanged.

Build outputs remain under `.build/<Platform>/<Configuration>/`, with intermediates under `.build/Intermediate/`.
The root `test.ps1` exits zero only after every required assertion has passed. Expected nonzero exits from isolated
negative-test children must not become the test entrypoint's success exit code.

The `HostPlugin` scope also runs isolated 60-second Matrix, Studio Clock and Desk Clock production-host WARP
soaks. Matrix warms for 1,024 frames at the same cadence as measurement: acquire DXGI readiness, draw, then sleep
16 ms, with a continuous monotonic animation timeline. Each clock warms for 20 seconds using its real scheduled
deadlines. During
the measured interval, five-second samples and the final sample of uninstrumented builds MUST stay within 4 MiB
private-byte and 8 MiB working-set growth above the warmed baseline. ASan reports memory diagnostically with
its normal quarantine/shadow settings; the uninstrumented process-byte bar does not apply to that allocator.
Every profile MUST retain its fixed object/resource counts,
preserve its scheduling/draw bounds and release device resources on shutdown. Clock no-render intervals MUST
produce no plugin-owned clock work. Each child has the normal bounded test-process budget and a separate
`HostPluginTests.<name>-soak.log`; any failed assertion or child failure fails the scope. These coarse,
whole-process WARP regression guards include runtime/driver caches; they do not establish hardware latency,
zero allocations or the separate five-minute qualification required by the plugin contracts.

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
  output in `.build/evidence/test-runs/<id>/payload/RedXe.self-test.log`. A failed self-test check MUST name itself,
  with its HRESULT when there is one, on stderr as well as on the debugger output, and end the run with exit code 6.
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
  the routing check's, under console output code page 437, and requires the U+2014 of the catalog's title line intact,
  which only the UTF-8 decoding of stdout keeps. The title's wording is the catalog's to change.
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
proves child-only environment overrides on both direct and suspended/contained paths:
literal Unicode, newline, equals and quote values, removal, inherited system values,
unchanged caller environment and exact child PID. Overrides reject invalid names/NUL
values before launch. The bounded path serializes a sorted Unicode environment block,
passes CREATE_UNICODE_ENVIRONMENT and frees its owned allocation on success/failure.
The direct path uses ProcessStartInfo.Environment with identical semantics.
MSBuild obtains a fresh private compiler metadata endpoint from the exact pinned
shared engine's Set-CiNativeProcessEnvironment helper, passes it through this boundary
for streaming, and uses Invoke-CiNativeCommand for interactive console launches.
Only the MSBuild child/descendants receive this endpoint; no parent environment mutation
or additional PowerShell wrapper is introduced for application/test processes.
The exact clean .ci-engine checkout is a prerequisite of build.ps1.

`BuildProcessTests.ps1` also
covers a child that never stops writing, a caller held up after the resume until the child has run past its budget (the
child is terminated at the first check and never finishes on its own), the exit grace with a drained last line, a child
that starts nothing and exits while a slow callback presents its backlog for longer than the grace (every line
presented, its exit code returned, no timeout record), a stop of a silent child with and without a budget (stopped
within seconds, no survivor), and an edited launcher definition imported into a session that compiled the original.

- Both start paths MUST build the child's command line with the same quoter. It leaves an argument as it is unless it
  is empty or holds white space or a quote; otherwise it encloses it in quotes as `ProcessStartInfo.ArgumentList` does,
  escaping each quote with a backslash and doubling only the backslashes that precede a quote or the closing quote, so
  the child's C runtime parses every argument back unchanged. A call without arguments, or with `$null` for them,
  passes none. `BuildProcessTests.ps1` passes, on both paths, every argument shape that `test.ps1`, the package smoke
  and `build.ps1` use (switches, a switch and its value, a path with spaces, a `name=path` pair, an MSBuild property
  with a space) and the shapes a quoting mistake breaks (an empty argument, a tab, quotes, backslashes before a quote
  and at the end of a quoted argument, non-ASCII text), and requires the child to receive each one unchanged.
- The budget is counted on a monotonic clock that the job starts immediately before it resumes the suspended child,
  the first moment the child can run. Neither the helper's own setup (compiling its job type on first use, creating
  the process) nor a change of the system time moves it, and a calling thread that runs again only some time after
  the resume finds that time already counted, so a child still running past its budget by then is terminated at the
  first check (one that has already exited is judged by the exit grace below). It is checked on every pass of the read
  loop, so a child that never stops writing is terminated at its budget like a silent one.
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
any product build: the CI `tooling` job runs it using PowerShell 7,
while `test.ps1` runs it as the `BuildProcess` suite after its own build step. It therefore restores the exact pinned
DxUi source first, the source only, because the restore tests inspect that source. It builds no product and installs no
vcpkg package. All tooling suites create their fixtures under `.build`; skill scalar/parser cases run in memory rather than a temporary
folder.

Changes to this contract require:

```powershell
.\test.ps1 -Full -Configuration Debug -Platform x64
.\build.ps1 -Configuration Release -Platform x64
.\build.ps1 -Configuration Release -Platform ARM64
.\validate-skills.ps1
```

## Formatting

The root `.clang-format` is the one style. `format.ps1` formats, and `format.ps1 -Check` verifies without writing
and lists every file that needs formatting. Both take clang-format from `Install-CiFormatter` in the pinned
`.ci-engine`, which installs the build that the engine's `Policy/clang-format.json` pins (22.1.3 today) under
`.build/shared-format` and verifies its SHA-256 and version; no Visual Studio, `PATH` or repository-pinned formatter
is used. Both cover the files CI formats: `.c`, `.cc`, `.cpp`, `.cxx`, `.h`, `.hh`, `.hpp` and `.hxx` files under the
`formatRoots` of `.ci/project.json` that its `formatExclude` does not match. Pull-request checks run the same formatter
over the files the pull request changes, or over every file when `.clang-format` or `.clang-format-ignore` changes;
drift fails the check and retains the patch. The manual `Commit PR formatting` workflow commits that formatting to an
open same-repository pull request with an ordinary push and requests fresh checks for the new head.

## Public DxUi access in CI

`RedSalamanders/DxUi` is public. Anonymous HTTPS Git access and public Actions API reads were verified on 2026-09-09.
RedXe restores its exact pin without configuring a secret. Sharing an organization does not add any setup requirement.
CI may use its automatic `github.token` for API rate limits; this is provided by GitHub and is not a personal access
token. Source URLs, logs and shipped provenance contain no credentials. The existing advisory lookup also works without
authentication.

## Scoped iteration and qualification

`Test-Changes.ps1` runs the pinned engine's [scoped-testing contract](https://github.com/RedSalamanders/.github/blob/main/Specs/ProjectCommands.md#scoped-testing).
`Tests/test-scopes.json` owns RedXe's suite names, path rules, native source inventory and PrePush profiles.
The planner uses local merge-base changes or its eligible local iteration baseline and includes staged, unstaged,
deleted, renamed and untracked inputs. Known prose selects independent tooling and formatting. The test
fixtures, embedded resources and unknown executable inputs retain native coverage.

Full selects one profile. PrePush runs x64 Debug, Release and ASan Debug with native tests and the three ARM64
cross-build profiles. Native runtime execution requires the matching host architecture. The hosted Release
adapter runs the existing full native suite batch after its build. The PR checks remain compiler-free.
Independent tooling uses `Tests/BuildProcessTests/Invoke-ToolingTests.ps1` and `format.ps1 -Check`.

The shared runner stamps both its build and test with the comparison merge base's commit count.
Direct `test.ps1 -SkipBuild` needs that stamp as `-BuildNumber` when testing an output built by `Test-Changes.ps1`.
Direct builds retain the default build number in [Build_Packaging.md](Build_Packaging.md).
Build and test share the engine's fail-fast profile lease. Starting a native run revokes the previous iteration
baseline. Starting a build invalidates its profile's scoped build receipt. `Test-Changes.ps1 -SkipBuild` requires
an exact attestation from the shared runner for that profile, source, artifact and environment.

Native reuse binds actual executable, library and symbol bytes, the deployed Debug and Release settings, the
deployed schema, and the DxUi provenance file listed in the manifest. The runner also needs a current
successful suite report with no capability skip. Changed source, runtime bytes or environment refuse the
affected run's success. `-Force` reruns selected scopes. Native reports and receipts follow
[Testing_RunEvidence.md](../Testing/Testing_RunEvidence.md).

A test project's sources and referenced inputs select every consuming suite. Project edits also select the
shared-host and packaging consumers of its deployed output. The tooling fixture checks that each declared native
suite has one implementation and that each rule matches actual tracked files. Real devices, desktop focus,
IME, assistive technology and performance acceptance keep their applicable product qualification.

## Test-run retention

[Generated test-run evidence](../Testing/Testing_RunEvidence.md) owns completed, failed and interrupted output storage and verification.

## MSVC 14.52 Release compilation and linkage

All native profiles require the installed default MSVC **14.52.x** family with the v145 platform toolset. Discovery remains installation-independent. Restore rejects another default family before dependency compilation; MSBuild validates the selected family before compilation. Record exact patch/compiler/linker identities; GL archives and their final links require exactly matching tools.

Release explicitly uses /Gy and /GL, /OPT:REF and full /LTCG, with ordinary incremental linking disabled. Debug/ASan diagnostic optimization settings are unchanged. Preserve existing project-specific size/speed, warning, ISA, CRT and subsystem choices. Evaluated-profile tests check all six native configurations, pinned archives, and negative compiler/linker controls; resource-only satellites contain no C++ bodies.

Project references explicitly forward WholeProgramOptimization and VCToolsVersion into the adopted DxUi archive. Dependency identity includes releaseOptimizationPolicy=Gy-REF-GL-LTCG-full-v1 and is rehashed before choosing writable outputs. This separates the new archive policy without changing the adopted API/source pin. Keep header/library/compiler/CRT identities matched.

LTO is scoped to each final EXE/DLL and does not guarantee reduced size or improved speed. Retain baseline/candidate image hashes, sizes, build/link durations, fixture and dependency identities, every runtime/resource sample and concurrent-activity observations. Different compiler policies are an intentional experiment, not identical-profile evidence reuse. Cross-built ARM64 and headless functional passes cannot close native ARM64, interactive, or existing product/performance acceptance gates.

### CI toolchain preparation and audit evidence

Active Windows compilation/tooling workflows use the local setup-msvc composite action. It reads RequiredMsvcToolsVersion, verifies the discovered default compiler, linker, ASan target libraries, SDK and x64 LLVM scanner tools, and provisions Visual Studio 2026 Insiders only on GitHub-hosted runners when necessary. The Microsoft-signed bootstrapper runs under RUNNER_TEMP; existing installations are retained. A channel that no longer supplies 14.52 fails rather than accepting another compiler. Local -VerifyOnly is read-only; automatic local installation is refused. Hosted provisioning has run in CI (run 40513243075 below).

[Full evaluated settings and differences](../../Measurements/Toolchain/2026-10-23-msvc1452/Settings.md) include both platform/configuration matrices and exact per-file metadata. [Microsoft installation guidance](https://learn.microsoft.com/en-us/cpp/overview/acquire-msvc) documents channel-specific acquisition. The published [runner image inventory](https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-VS2026-Readme.md) does not establish that the required toolset is installed; runtime versions alone are also not compiler proof.


### ARM64 compiler host

ARM64 uses an x64 compiler host on x64 Windows and a native arm64 compiler host on ARM64 Windows, respecting explicit caller selection. MSVC 14.52's 32-bit host reproducibly ran out of compiler heap in the large SystemData type inventory across all three ARM64 configurations. The existing x64 runtime profile is unchanged. This host selection is forwarded to the pinned archive and checked in all evaluated ARM64 profiles; cross-build success still cannot qualify native execution.

Hosted setup downloads the bootstrapper with a 300-second timeout and three retries, then waits for the hidden, owned GUI bootstrapper with both its --wait switch and a bounded wait on that process (900 seconds by default, below the 20-minute tooling job ceiling). A bootstrapper still running at the budget is ended with its process tree and the step fails after collecting its logs, so a stall cannot reach the job timeout that would discard them. It reads that process's explicit exit code (0 or 3010), then rechecks the required compiler family and complete target tools. Ambient LASTEXITCODE cannot establish installer success. Paths with spaces stay one argument; installer stdout, stderr and fresh dd_* logs are uploaded with 14-day retention only when provisioning fails, never from a green run. Delayed-GUI, stalled-installer, bounded-download, stale-exit, wrong-family, missing-tool, signature and local/read-only refusal controls exercise the actual setup script without running an installer.

`ci.yml` cancels a superseded run only for pull requests. Every push to `main` and each manual dispatch runs in a concurrency group of its own, so a newer push never cancels or replaces the run that `release.yml` requires for the exact released commit.

The hosted action exports MSBUILD_EXE_PATH through GITHUB_ENV from the exact installation it verified, preferring its native 64-bit MSBuild and requiring the chosen executable to exist. Build and restore therefore consume that verified toolchain ahead of the runner's older environment or PATH entries. Local -VerifyOnly does not publish or change compiler selection. The regression applies the exported value to the actual product discovery function and retains the older-ambient counterfactual; rejecting MSVC 14.51 remains required.

The hosted Windows tooling job has a 20-minute outer ceiling to include required MSVC provisioning and the complete independent tooling suite. Run 40513243075 used 8m45s in provisioning and reached the former 15-minute job limit while tooling was still running. All per-child/test limits, assertions, scopes and the 45-minute native-job ceiling are unchanged; timeout evidence remains a failed attempt, not a partial pass.

The [native policy, console and dependency-path contract](Build_ConsoleAndPaths.md) owns /Wall without /WX, zero-warning acceptance, Debug/ASan symbols, Release-only ICF, common terminal results and verified short dependency folders.

The native build policy adopted a DxUi API4 maintenance commit; `Dependencies/DxUi.lock.json` names the current pin and is the only place a DxUi commit is recorded. Since 2 November 2026 the pin is a DxUi main commit, which keeps this consumer's existing API adoption, the native warning repairs and stable payload type identity under Release COMDAT folding. That source also has its own copy of the native build policy, which compiles the archive; restore refuses a copy that is not RedXe's, because the build identity hashes RedXe's ([`Core_DxUiIntegration.md`](../Core/Core_DxUiIntegration.md)). Restore and compile matching headers/archive from that exact lock. The indexed NativeBuildPolicyCoherence plan records outstanding product and desktop-focus qualification; this pin selection alone is not a green product receipt.

## Repository skill validation

`validate-skills.ps1` checks the changelog and calls the pinned shared guidance validator. The independent
tooling runner and compiler-free CI use the same command. See the [skill package contract](Build_SkillPackages.md)
for ownership, scope selection and fixtures.

Manual source generators, historical measurement reproductions and upstream tools retain their prerequisites.
