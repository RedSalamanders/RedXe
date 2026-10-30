# Unified organization CI contract

This specification owns the project's CI routing. The versioned implementation and policy tests live in
[RedSalamanders/.github](https://github.com/RedSalamanders/.github). The exact reviewed full commit is recorded
in .ci/project.json and every reusable workflow caller. Update those pins together through a reviewed PR.
Older references to automatic native PR matrices or nightly native runs are superseded by this contract.

PRs run changed-source clang-format feedback and the reviewed compiler-free static subset. Formatting drift
fails and retains a patch. The explicit Commit PR formatting action accepts an open same-repository PR,
checks its current head, commits only selected formatted source paths by ordinary fast-forward push and
explicitly dispatches fresh compiler-free checks for that exact head. Forks retain a patch for their author.
PR checks never count as native acceptance. Compiler-dependent tooling runs in main qualification.

After main has remained unchanged for five continuous minutes, its cheap coordinator submits one native
admission. Cumulative native identity uses the complete committed Git input tree, file modes, dependencies,
project adapter and exact engine/policy pins. It is not a truncated files API response or last-merge-only diff.
A code merge followed by prose still requires qualification. Excluded-only edits retain the previous actual
success, failure, cancellation or pending status. Failed unchanged inputs require an explicit fresh retry.
Unknown paths are meaningful; unreadable inputs fail closed. Renames inspect both paths and deletions count.
There is no maximum-deferral override while merges continue.

.ci/project.json declares exclusions and runtime exceptions. Prose, plans, historical measurements and Changes
fragments normally do not require a native build. Shipped schemas, test fixtures, runtime examples, generated
build inputs, resources, dependencies and tooling remain meaningful, including runtime files with .md suffix.
Do not exclude all of Specs or docs without retaining their product/runtime exceptions.

The native workflow has a separate non-cancelling serialized queue. New pushes may cancel only obsolete
cheap coordinators. A stale queued automatic candidate exits before provisioning a compiler. Each admitted
pipeline freezes one commit, input digest, engine and version. Both native x64 and native ARM64 execute:

1. Release with test targets/hooks enabled; matching source/dependency/toolchain/build evidence is retained.
2. Actual normal regression execution through the canonical test harness and an always-on Release failure probe.
3. A global AND gate verifies both architectures before either production Release job may begin.
4. Separate production-only Release outputs, clean-extraction native smoke and ZIP artifact retention.

These are separate test and production roles. They intentionally compile production code twice; neither a PR
matrix nor a second push matrix repeats the same role. Test receipts and production artifacts never share
mutable cached object outputs. Dependency binary caches include the engine, native architecture, manifests,
locks, restore/toolchain scripts, props/targets and adapter closure. Cache reuse cannot waive a test gate.

Receipts bind repository, exact source/input identity, workflow run, architecture, role, count unit, positive
discovery/execution, active failure probe and artifact digests. Canonical per-case evidence remains authoritative
where the harness provides it; suite counts are explicitly suite counts, not invented native case counts.
Missing/zero/filtered execution, an unexpected skip, timeout, disabled failure checks or an identity mismatch
cannot qualify production. A focused pass is not Full repository or release qualification.

Debug, ASan, paired benchmarks, repeated stress, and other extended obligations run through explicit manual
qualification. Their canonical warnings, native-host rules, budgets, evidence preservation and desktop leases
remain unchanged. There are no scheduled native builds. Scheduled dependency, security and status automation
remains enabled; CodeQL uses explicit C/C++ build mode none, which does not invoke a native compiler.
Compiler-free CodeQL does not analyze generated code as completely as a build-based scan:
[GitHub build-mode contract](https://docs.github.com/en/code-security/how-tos/find-and-fix-code-vulnerabilities/manage-your-configuration/codeql-for-compiled-languages).

Publication is an explicit trusted-main action, defaulting to prepare-only. Select an original successful
qualify.yml run with complete matching receipts and retained ZIPs for both native architectures. Publication
promotes those exact bytes without building, restamping, signing afterwards or substituting an older green
run for a later failure. Missing/expired artifacts require a fresh explicit qualification. Existing conflicting
tags, assets or winget submissions are retained for review and never overwritten. Product releases publish
ZIP assets only. Applications support winget; the DxUi SDK does not. winget requires anonymous immutable
release URLs, matching ZIP hashes, the configured nested executable, an explicit product license and a working
publication credential. Private visibility or unavailable credentials are readiness gates, never permission
to change visibility, billing, licensing or access.

The implementation remains unqualified until the organization rollout plan records actual hosted acceptance.
Source changes, tests written, static checks, and the shared engine's own behavior tests alone do not establish
that all six native product pipelines passed.

## Project adapter

The adapter retains Full noninteractive regression, plugin/host/WARP/crash scopes, the AV resource fixture and independent compiler tooling. Counts in normal-suites.json are selected/executed scope counts; individual logs remain authoritative. The Release probe exercises ReportSuiteFailure directly, independently of the intentionally Debug-only CRT report self-test. ProductionOnly excludes standalone Tests projects; package.ps1 -SkipBuild retains runtime/import closure, launcher smoke and the optional symbols ZIP. winget targets RedXeLauncher.exe.

## Emergency PR merge

An organization owner may explicitly bypass PR merge checks during a recorded emergency through the GitHub PR override or gh pr merge --admin --match-head-commit FULL_SHA. Record the reason, blocked checks, exact reviewed head, rollback and follow-up in the PR and merge commit. The default-branch ruleset uses OrganizationAdmin with pull_request bypass; automation receives no bypass permission and direct/force pushes stay protected. Failed checks remain their actual results, and normal main qualification still runs. Production ZIP/SDK and publication requirements remain unchanged.

Follow the common [emergency merge runbook](https://github.com/RedSalamanders/.github/blob/main/Specs/EmergencyMerge.md), including the reason file and exact-head CLI procedure. No emergency merge is an ordinary fallback after a failed merge attempt. Account restrictions must be resolved before enabling the ruleset; the pending RedPrism/RedTilde public visibility change remains the owner's operation.
