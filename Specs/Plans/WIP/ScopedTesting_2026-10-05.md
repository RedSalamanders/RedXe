# Scoped testing and validation cost

Status: ACTIVE: implementation and qualification in progress.
Date: 2026-10-05
Owner: [current validation contract](../../Build/Build_Process.md)

## Accepted request

All active native test sources and helpers use `Scope.Tests.Something.h/.cpp`; Scope identifies the component and Something the scenario, runner, fixture, or helper. Migrate project entries, includes, inventories, tooling and current documentation together. Immutable historical evidence and imported/external source are not active test files.

Everyday iteration selects affected scopes from committed changes since the merge base plus staged, unstaged, deleted, renamed and untracked paths. Report paths and reasons. Unknown executable inputs, shared infrastructure and dependency/build changes conservatively widen coverage. A focused pass is never a full repository verdict.

Reuse only successful local evidence for identical source/dependency closure, executable/runtime bytes, architecture, configuration, runner arguments and environment. Invalidate before execution; publish only after success and an unchanged post-run identity. Corrupt/missing evidence executes again. Explicit force runs, performance investigations and environment-dependent/manual acceptance remain available.

Before a PR push, full coverage is required across local and CI execution. Delegate only the identical scopes/configuration that the enabled PR workflow actually runs. A nightly gate is not a PR gate. A base/merge-tree change, configuration difference or modified runner invalidates equivalence. Keep main/release acceptance distinct from PR feedback.

## Review evidence

- DxUi: test.ps1 supports suite and named-test filters, but unconditionally runs tooling and a benchmark; CI runs six native profiles plus a separate validation job and paired benchmark. The initially running main validation subsequently passed; an earlier main ASan menu-popup fixture had failed even though its PR was green.
- RedXe: test.ps1 is an unconditional sequence of standalone suites, tooling, host WARP and crash checks. PR CI runs all tests on x64 Release; main runs all six profiles. Latest main CI passed.
- RedSalamander: governed Affected, ExplainPlan and exact Resume already exist. Reuse these contracts. PR CI runs Suite PR on x64/ARM64 Debug; long in-product CI suites run nightly and sanitizer suites weekly. Latest CI failed in format, migration and native jobs.
- RedPrism: test.ps1 orchestrates document, IO, host, compositor and in-product checks on the current UI branch. No checked-in or enabled GitHub workflow was found. Plan only; do not rename files, edit runners, build, or change CI during this work.

## Implementation sequence

- [x] Inventory active source files, test scopes, dependencies, desktop/network requirements and CI coverage.
- [x] Rename active native tests/helpers and reconcile all current callers; enforce naming.
- [x] Provide explainable affected/default iteration and explicit full/pre-push entrypoints.
- [x] Prove conservative fallback, rename/delete/untracked discovery, dependency fan-out and invalid selectors.
- [x] Prove unchanged reuse, source/binary/configuration/environment invalidation, failures and post-run mutations.
- [x] Align CI commands and pre-push delegation without weakening existing main/release gates.
- [ ] Run focused tooling/scenario checks, applicable builds and noninteractive native tests; record unavailable/manual gates separately.
- [ ] Persist durable requirements in the owning contract and agent guidance; close only after required qualification.

## Qualification constraints

No test may seize the person's foreground or pointer without agreement to the time. Use existing harness desktop leases. Native ARM64 claims require ARM64 execution; cross-build evidence is separate. No consumer pin changes are implied by library changes. Retain baseline/candidate fixture and source provenance for resource comparisons.

## Implementation and reviewed qualification

All 41 active native tests/helpers and plugin test-contract headers use contextual names. The root affected/default command uses semantic test-source rules plus plugin/host/packaging fan-out; SystemData includes its referenced phase-zero project. Native bytes/pinned runtime dependencies and installed environment bind reusable local receipts. Profile-independent Windows build/provenance/restore/update/skill regressions have one shared runner and one CI tooling job. Native CI still runs the full x64 Release PR gate and six-profile main gate, with resource fixtures and the existing release-success requirement retained.

Qualification completed: the full noninteractive x64 Debug gate, eighteen scope/reuse/CI/public-runner regressions, independent tooling plus Python skill tests, ASan PluginContract/HostPlugin/HostSmoke with the real sanitizer detection and WARP/crash paths, x64 Debug/ASan builds, and all three ARM64 cross-builds. All affected C++ files pass the pinned formatter. A second identical BuildProcess invocation on ASan Debug reports REUSED from the independent Debug-profile success.

The x64 Release build guard refused because independently launched RedXe.exe PID 8060 used that exact output. It was left running. Full Release qualification is delegated to the forthcoming matching PR gate; local Release rebuild remains pending until the person closes the application. Current GitHub main [36950802759](https://github.com/RedSalamanders/RedXe/actions/runs/36950802759) passed on 71107b053be5; it does not prove this candidate workflow. Its reviewed digest and enabled-workflow state permit only pending CI delegation.

Raw logs/receipts are under .build/logs/scoped-testing-* and .build/reports/scoped-tests. No dependency pin, application setting or runtime behavior changed. Pending closeout: required candidate CI/native profile coverage and the blocked local Release build if requested; native ARM64 execution is distinct from cross-compilation. Keep this plan active and preserve focused/CI_PENDING verdicts until qualification is accepted.

Final receipt review includes documentation, skill Markdown and source-origin mappings in validator identities. Uncommon inline/generator extensions invalidate build attestation. Documentation edits select only independent validators; native scopes remain omitted. The two standalone runner fixtures pass all eighteen cases, including those invalidation boundaries.

DxUi preserves one Windows tooling qualification and one Linux portability qualification, with the staging fixture only once. Conditional native PR coverage reuses its existing NativeScope contract; skipped native jobs are never delegated. Native receipts retain identical-code reuse across validator prose edits. The standalone fixtures cover these boundaries in eighteen cases.

The first candidate PR tooling job exposed a clean-checkout prerequisite: the restore contracts inspect the real pinned DxUi source, which ordinary builds had already provisioned locally. The independent runner now establishes that exact source pin before testing, without building the product or restoring vcpkg packages. The complete independent tooling runner passes locally after this repair; the corrected hosted candidate remains pending.

Post-merge review repairs four Settings dependency omissions for AVControl, Launcher, Logicon and Zoom. Exact native evidence now includes stable deployed settings/schema and DxUi provenance bytes while excluding generated reports. A clean committed candidate barrier rejects uncommitted or concurrent changes during PR delegation. All 20 scoped-runner regressions pass, including asset mutation/deletion/restoration and candidate races. Original PR #31 passed its Release/tooling gate; the subsequent main run passed all six profiles and tooling. This tooling-only follow-up does not repeat local native qualification.
