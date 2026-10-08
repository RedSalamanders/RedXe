# Scoped testing and validation cost

Status: DONE (2026-10-07): qualification gates closed; review follow-ups stay open (see the [Closeout](#closeout))
Date: 2026-10-05
Owner: [current validation contract](../../Build/Build_Process.md)

Current requirements live in [`Build_Process.md`](../../Build/Build_Process.md) ("Scoped iteration and PR coverage"),
[`Tests/README.md`](../../../Tests/README.md) and the "Scoped testing policy" of [`AGENTS.md`](../../../AGENTS.md).
This plan records how the work was sequenced and qualified. Where it differs from them (the later review fixes changed
the build-number identity, PR delegation, Git output decoding and the test-source detector), they win. The
[Closeout](#closeout) holds the evidence that closed its qualification gates and the review follow-ups that stayed open.

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
- [x] Run focused tooling/scenario checks, applicable builds and noninteractive native tests; record unavailable/manual gates separately. The local record is below; the hosted runs and the one gate left manual, the local x64 Release rebuild, are in the Closeout.
- [x] Persist durable requirements in the owning contract and agent guidance; close only after required qualification. The Closeout maps each requirement to the contract that holds it.

## Qualification constraints

No test may seize the person's foreground or pointer without agreement to the time. Use existing harness desktop leases. Native ARM64 claims require ARM64 execution; cross-build evidence is separate. No consumer pin changes are implied by library changes. Retain baseline/candidate fixture and source provenance for resource comparisons.

## Implementation and reviewed qualification

All 41 active native tests/helpers and plugin test-contract headers use contextual names. The root affected/default command uses semantic test-source rules plus plugin/host/packaging fan-out; SystemData includes its referenced phase-zero project. Native bytes/pinned runtime dependencies and installed environment bind reusable local receipts. Profile-independent Windows build/provenance/restore/update/skill regressions have one shared runner and one CI tooling job. Native CI still runs the full x64 Release PR gate and six-profile main gate, with resource fixtures and the existing release-success requirement retained.

Qualification completed: the full noninteractive x64 Debug gate, eighteen scope/reuse/CI/public-runner regressions, independent tooling plus Python skill tests, ASan PluginContract/HostPlugin/HostSmoke with the real sanitizer detection and WARP/crash paths, x64 Debug/ASan builds, and all three ARM64 cross-builds. All affected C++ files pass the pinned formatter. A second identical BuildProcess invocation on ASan Debug reports REUSED from the independent Debug-profile success.

At the time of that qualification the x64 Release build guard refused because independently launched RedXe.exe PID 8060 used that exact output. It was left running. Full Release qualification was delegated to the forthcoming matching PR gate, and the local Release rebuild was left pending until the person closed the application; the [Closeout](#closeout) records how that gate closed. The then-current GitHub main [36950802759](https://github.com/RedSalamanders/RedXe/actions/runs/36950802759) passed on 71107b053be5; it did not prove this candidate workflow, and its reviewed digest and enabled-workflow state permitted only pending CI delegation.

Raw logs/receipts are under .build/logs/scoped-testing-* and .build/reports/scoped-tests. No dependency pin, application setting or runtime behavior changed. Pending at the time, and closed in the [Closeout](#closeout): required candidate CI/native profile coverage and the blocked local Release build if requested; native ARM64 execution is distinct from cross-compilation. The plan stayed active, with focused/CI_PENDING verdicts preserved, until qualification was accepted. That acceptance rests on the hosted runs in the Closeout and on the closure under `alignment#9` of the review plan on 2026-10-07. No owner statement records that the hosted Release legs replace the local Release rebuild, so the owner's confirmation of that reading is pending.

Final receipt review includes documentation, skill Markdown and source-origin mappings in validator identities. Uncommon inline/generator extensions invalidate build attestation. Documentation edits select only independent validators; native scopes remain omitted. The two standalone runner fixtures pass all eighteen cases, including those invalidation boundaries.

DxUi preserves one Windows tooling qualification and one Linux portability qualification, with the staging fixture only once. Conditional native PR coverage reuses its existing NativeScope contract; skipped native jobs are never delegated. Native receipts retain identical-code reuse across validator prose edits. The standalone fixtures cover these boundaries in eighteen cases.

The first candidate PR tooling job exposed a clean-checkout prerequisite: the restore contracts inspect the real pinned DxUi source, which ordinary builds had already provisioned locally. The independent runner now establishes that exact source pin before testing, without building the product or restoring vcpkg packages. The complete independent tooling runner passed locally after this repair, and the corrected hosted candidate was still pending then; its pull-request run passed (see the [Closeout](#closeout)).

Post-merge review repairs four Settings dependency omissions for AVControl, Launcher, Logicon and Zoom. Exact native evidence now includes stable deployed settings/schema and DxUi provenance bytes while excluding generated reports. A clean committed candidate barrier rejects uncommitted or concurrent changes during PR delegation. All 20 scoped-runner regressions pass, including asset mutation/deletion/restoration and candidate races. Original PR #31 passed its Release/tooling gate; the subsequent main run passed all six profiles and tooling. This tooling-only follow-up does not repeat local native qualification.

Final receipt review requires every deployed Settings/provenance input and a nonempty binary closure before identity creation. Missing initial assets, deleted inputs and JSON-only output profiles are rejected. All 21 owning scoped-runner cases pass after this repair; other independent tooling and Python fixtures retain their prior unchanged passing result.

## Closeout

Closed 2026-10-07 by `alignment#9` of the `MergedReviewFixes_2026-10-06` review plan. The plan's qualification gates are
closed and each durable requirement has a home, and its implementation items are done as written except where the last
bullet says otherwise: a test-only header escaped the naming migration that the implementation paragraph reports as
complete (`scoped-testing#22`). That does not certify the tooling it delivered. The review that followed found defects
in it, and the last bullet names the ones still open.

- **Implementation.** #31 (`508bd3d`) and #32 (`25433ae`) merged into `main` on 2026-10-06. The tooling batch of the
  review fixes that followed (#37) reworked the build-number identity, PR delegation, Git output decoding and the
  test-source detector; `Build_Process.md` states the result.
- **Qualification.** The `RedXe validation` push runs of the two merges, 37491745609 and 37501400691, passed `tooling`
  and all six native profiles (x64 and ARM64; Debug, Release and ASan Debug). The ARM64 legs run on a native ARM64
  runner (`windows-11-vs2026-arm`), so that is native ARM64 execution, not cross-compilation. The pull-request runs,
  37490349686 and 37499179375, passed `tooling` and `native (x64, Release)`.
- **The local x64 Release rebuild** that the qualification above left pending was refused by the build guard, because an
  independently launched `RedXe.exe` used that output. The plan asked for it only "if requested", and nothing in the
  plan or its pull requests records a request. #31's description names the candidate pull request's Release check as the
  replacement, and that check passed. On that reading no qualification gate is left open; no owner statement has
  confirmed the reading yet.
- **Durable requirements.** Each lives in a contract, and one was missing: this closeout added it to `Build_Process.md`.
  - The independent tooling runner restores the exact pinned DxUi source first, so the CI `tooling` job works on a clean
    checkout before any product build (the clean-checkout prerequisite that the first candidate pull request exposed):
    `Build_Process.md`, the paragraph after the Git-configuration rule for tooling tests; `Invoke-ToolingTests.ps1`.
  - Naming, the inventory and the test-source rule: `Build_Process.md` (first paragraph of "Scoped iteration and PR
    coverage"), `Tests/native-test-files.json`, enforced by `Assert-ScopedTestNames` in every `Test-Changes.ps1` run and
    by `ScopedTesting.Tests.ps1`.
  - Affected iteration, explanations, conservative fallbacks and coverage labels (a focused pass is never a full
    verdict): `Build_Process.md` (second and third paragraphs), `Tests/README.md`.
  - Exact local reuse, evidence identity, invalidation and post-run mutation: `Build_Process.md` (the paragraphs on
    reuse, the build number, Git output, the environment identity and changes during a run), `Tests/README.md`.
  - `-Mode PrePush`, PR delegation and the distinction between PR feedback and main/release acceptance:
    `Build_Process.md` ("CI invokes explicit full gates" and the `-Mode` paragraph), `Tests/README.md`, `AGENTS.md`.
  - Focus, platform and provenance constraints: `AGENTS.md` ("Focus-taking work requires agreement to the time; no scoped
    pass closes that gate", and the crash validation of `test.ps1 -Full` "requires no desktop automation"); native
    ARM64 execution in `Build_Process.md` ("Output and validation"); no pin change and paired fixture/build provenance
    for resource comparisons in `Tests/README.md`. This repository defines no desktop-lease harness and its gate is
    noninteractive, so the "existing harness desktop leases" constraint has no RedXe counterpart.
  - Agent guidance: `AGENTS.md` ("Scoped testing policy" and the build commands), the `build-redxe` skill and
    `README.md`.
- **Open follow-ups.** Review defects in what #31 and #32 delivered that were still open when this plan closed. They
  belong to the P6 and P7 batches of `MergedReviewFixes_2026-10-06`, whose register rows hold the current state of each:
  - `scoped-testing#18` and `alignment#13`: code, help text and test branches copied from DxUi and RedSalamander that do
    nothing or mislead in RedXe, such as the `-Scopes Tree` example in `Test-Changes.ps1`, which fails with an unknown
    test scope.
  - `scoped-testing#20`: the top-level `Mockups/` folder counts as code, so editing its one file selects every scope and
    invalidates native results.
  - `scoped-testing#21`: `test.ps1` assigns `$testTimeoutSeconds` twice.
  - `scoped-testing#22`: `Common/AddressSanitizerProbe.h`, a test-only fixture, escaped the naming migration, so
    `Tests/native-test-files.json` does not list it and an edit to it selects the full fallback.
  - `alignment#10`: the "Scoped iteration and PR coverage" section of `Build_Process.md`, which the **Durable
    requirements** bullet names as the home of the requirements, still carries RedSalamander and DxUi rules that RedXe
    neither owns nor implements.
