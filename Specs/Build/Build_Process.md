# RedXe build-process contract

Status: current normative repository contract
Last reviewed: 2026-10-01
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
`wmain`, and `RedXe.exe` calls it as soon as its command line has `--self-test` or `--screenshot`, before either
runs.
- In a Debug or ASan Debug build, a failed runtime check (an STL range check, a CRT assertion) writes its report to
  stderr and ends the process with exit code 3, as the CRT's Abort button would, instead of opening its modal
  Abort/Retry/Ignore box. An abort or a fail-fast also ends the process quietly: Windows Error Reporting's dialog and
  critical-error boxes are suppressed.
- `test.ps1` runs `PluginContractTests.exe --failure-report-self-test`, a hidden switch that fails such a check on
  purpose. It requires the report and exit code 3 in Debug and ASan Debug, and exit code 0 in Release, which has no
  such checks. The run is bounded by two minutes, so a routing that stopped working fails there.
- `test.ps1` runs `RedXe.exe --self-test --warp` bounded by the same budget as the test executables and keeps its
  output in `.build/<Platform>/<Configuration>/RedXe.self-test.log`.
- A new test executable calls the header first, so an unattended run on a developer's desktop never holds a dialog.

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
exit code, and decode output alike. The survivor check follows parent processes, so a descendant that carries no
marker (`ping.exe`) still counts. The budget is counted from
the child's start, so the helper's own setup (compiling its job type on first use) never shortens it. The stall
fixture proves containment only once its child has started the pipe-holding grandchild and exited: a run that did not
get that far inside the budget is inconclusive and is repeated once with a longer budget, and a line the child wrote
that is missing from the log fails. `build.ps1` keeps the unbounded default; `test.ps1` applies a fifteen-minute
budget to every standalone test executable.

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
