# Changelog

Entries are generated from [Changes](Changes/README.md). Existing release notes remain in their original locations.

## Unreleased

- Restore the native qualification fixture checks for the independent build and analyzer logs and the unattended harness commands.
- Accept the shared `--capture <png>` and `--crash-probe <directory>` commands and preserve the existing capture and crash commands.
- Generate RedXe's WinGet manifests through the pinned engine, preserving product descriptions, links and release metadata.
- Run RedXe's compiler-free checks through root `validate.ps1` for local use and pull requests.
- Add a native MSVC code-analysis mode with isolated build outputs.
- Include host smoke tests when their shared test executable changes.
- Update the restore validation to verify both standard and AddressSanitizer triplet sources.
- Add a root restore command for the pinned RedXe and DxUi dependencies on x64 and ARM64.
- Restore and link instrumented vcpkg dependencies in each ASan Debug architecture.
- Keep the ten newest new RedXe crash reports and stack files and claim the restart notice once. Legacy reports stay available.
- Use the CI engine as the sole hosted MSVC setup implementation. Preserve the existing verification command and installer timeout override.
- Declare Windows 11 build 22000.2600 as the minimum for RedXe WinGet packages.
- Include the diagnostic footer in the direct MSBuild UTF-8 log.
- Adopt the shared hosted MSVC action and pinned formatting and repository policy checks.
- Moved the build profile out of the 32 project files into `Directory.Build.props`: the toolset (`v145`), the
  Windows SDK (`10.0.26100.0`), the Unicode character set, the debug-library choice, the runtime library (`/MDd`,
  `/MD`) and the optimization level (`/Od`, `/O2`) (`RedHome` convergence CV-6-04 part 1, register C-11). A project
  file keeps only what is its own: the three projects that link the static runtime and the one that optimizes for
  size still say so. `Build/NativeBuildPolicy.targets` stays equal to the copy in the pinned DxUi commit, which
  `restore-dxui.ps1` requires; it takes DxUi's newer text with the next DxUi pin.
  MSBuild evaluated all 32 projects in all six profiles before and after the move. The build properties
  and the compiler, linker and project-reference metadata matched across all 192 comparisons.
- Include the pinned DxUi notices in source-built portable packages, including Unicode and WHATWG.
- Publish `SettingsStore.schema.json` with new settings templates and preserve validation for existing RedXe settings.
- Restore the separately pinned vcpkg executable and catalogue through the common engine checkout helper.
- Use the pinned shared toolchain and manifest arguments for the application vcpkg restore.
- Bind the root RedMonitor client commands to the producer-owned tooling hashes recorded in the existing lock.
- Validate RedXe skills, guidance and plan indexes with the pinned shared CI engine.
- Adopt the pinned shared scoped-testing runner, keeping RedXe's suite ownership, stable build stamps, and retained evidence.
  Native suite reports record capability skips, and PrePush accounts for all six build profiles.
- Moved the vcpkg tool pin in `vcpkg-tool.json` from `9e593bb1` to `aeb162b7`, the same commit as the baseline and
  as RedSalamander's pin (RedHome CV-4-07, register C-07). The managed checkout's scripts and triplets now come from
  that commit and its bootstrap downloads vcpkg-tool 2026-09-26 instead of 2026-07-27; package ABI hashes change, so
  the first restore rebuilds the dependencies. Compiler-free checks pass; native qualification runs on `main` after
  merge.
- Moved the vcpkg `builtin-baseline` in `vcpkg.json` from `9eec002d` to `aeb162b7` (microsoft/vcpkg, 2026-10-22),
  RedSalamander's baseline, so every repository resolves ports from one baseline (RedHome CV-4-07, register C-06).
  Between the two baselines only `7zip` and `sqlite3` change, and this repository uses neither, so no dependency
  version changes. Compiler-free checks pass; native qualification runs on `main` after merge.
- Gave `Specs/Build/Build_UnifiedCI.md` the skeleton every RedSalamanders repository now shares (convergence
  CV-6-03, B-08): the same headings in the same order, one common text above `## Project adapter` built from the
  paragraphs the six files already shared, five stale statements corrected to the engine's `Specs/CI.md`, and every
  repository-specific sentence moved unchanged into the fixed adapter subsections. The WIP, Done and catalog headers also describe the index row format in words instead of a literal example link. Prose only; no CI
  behavior changed. Validated with the repository's specification validators and `Tools/Test-Changelog.ps1`.
- Gave the specifications the same shape as every RedSalamanders repository (convergence CV-6-03): the common
  `Specs/README.md` structure and WIP and Done index formats, dated names for the two active RFC plans
  (`RFC_Plugins_AVControl_2026-09-05.md`, `RFC_Plugins_Actions_2026-09-17.md`), the run-evidence contract moved
  unchanged to `Specs/Testing/Testing_RunEvidence.md`, the common CODEOWNERS file, and a
  `Core_RedMonitorIntegration.md` that keeps only RedXe's adapter facts and links to RedMonitor's consumer
  contract. No rule, behavior, build input or test changed. Validated with `validate-skills.ps1` and
  `Tools/Test-Changelog.ps1`.
- Retired two repository-local agent skills now folded into shared skills in `RedHome` (convergence CV-2-12):
  `build-redxe` into `native-build` and `settings-store` into `app-settings`. Every rule and code line of both is in
  the shared skills, with RedXe's commands, paths, templates and product rules in each one's
  `references/repository-facts.md`. Claude Code (the `redhome` plugin), Codex, and Copilot list the shared skills
  from this repository. `AGENTS.md` points at them. No build, test or product input changes.
- Agent guidance now matches the other RedSalamanders repositories (`RedHome` convergence step CV-6-02).
  `.github/copilot-instructions.md` is the identical pointer to `AGENTS.md` that every repository uses. `AGENTS.md`
  is titled "RedXe development guidance", with the shared-skill pointer directly under `## Skills`. The drive-root,
  scoped-testing, Changes, native build and Unified CI sections use the text every repository shares, and a new
  shared "Interactive desktop tests" section states the focus rule. RedXe's own scoped-testing facts move into Build
  and validation, which no longer asks for `test.ps1 -Full` on every change: PrePush accounts for the remaining
  coverage. The common `.gitignore` block ignores `.claude/settings.local.json` and `.claude/scheduled_tasks.lock`,
  and a new `capabilities.json` records what RedXe supports and what is pending, each entry restating the docs or
  the WIP index. Nothing compiles differently.
- Converged the root files with every RedSalamanders repository (convergence CV-6-03, owner decision D18 and the
  B-12 default): `LICENSE.txt` is now only the common MIT text, and its files-under-other-terms section and library
  list, full license texts included, move verbatim to `THIRD-PARTY-NOTICES.md`, whose DxUi entry now also names what
  that revision compiles in. The duplicate `LICENSE` is removed. The portable package stages and requires the notices
  file; its README.txt, `Build_Packaging.md` and `README.md` say where each part is. The common `CONTRIBUTING.md` and
  pull request template are added. Validated with `validate-skills.ps1`, the packaging tooling tests and
  `Tools/Test-Changelog.ps1`; native packaging runs in main qualification.
- Moved the shared CI engine pin from `07bc6f1` to `f965c79`, engine `main` today, with `Update-ProjectPin.ps1`:
  `.ci/project.json` and the workflow callers name the same full commit. This is this repository's step of the
  `RedHome` convergence wave (CV-4-04, CV-4-05) that puts every repository on one engine commit. Between the two
  commits the engine adds local SDK build and binding guards, the shared changelog commands with their product shim
  templates (CV-5-01, CV-5-02) and the harness exclusions, and changes no workflow template. The pin also adds the
  agent harness paths `.codex/**`, `.github/agents/**`, `.github/copilot-instructions.md` to the prose exclusions,
  so editing them no longer changes native inputs (CV-6-01). Compiler-free checks pass; native qualification runs on
  `main` after merge.
- Named the workflows and the Dependabot configuration on their own lines in the common CODEOWNERS file, which
  every RedSalamanders repository shares (convergence CV-6-03, B-12); `/.github/` already owned both, so no owner
  changes. Prose only. Validated with `Tools/Test-Changelog.ps1`.
- Planned the code-convention rows of the RedHome convergence register (CV-6-08) that move RedXe, one pull request
  per row, in `Specs/Plans/WIP/CodeConventionsConvergence_2026-11-19.md`. Plan only; no code changes.
- Recorded what reading the code found for three code-convention rows (RedHome CV-6-08): occlusion (H-20) spans
  the renderer, scheduler, edge affordance, probe and two contracts; the settings save (H-25) is a guarded POSIX
  rename the settings contract requires, which needs an owner decision against the register's `ReplaceFileW`
  target; and JSON5 reading (H-09) first needs the document scanner behind the dock patches to read JSON5. Plan only.
- Run the changelog commands from the pinned shared CI engine (convergence CV-5-02). `Tools/Test-Changelog.ps1`,
  `Fold-Changelog.ps1`, `Submit-ChangelogFold.ps1`, `Test-ChangelogWorkflow.ps1` and the new `Release-Changelog.ps1`
  are now copies of the engine's shims (`Templates/Project/Tools` at `f965c79`): the same names and parameters, running
  the engine implementation in `.ci-engine`, and refusing with one message when that checkout is missing, at another
  commit or changed. `Tools/Modules/Documentation/Changelog.psm1` is removed. The behavior suite now also proves these
  shims equal the pinned templates and that `Test-Changes.ps1 -Mode PrePush` checks the changelog first. Two engine
  changes reach this repository: two pending fragments with the same new text are refused by the fold, and a Git error
  while reading the base changelog fails `-Base` instead of skipping it. The scoped-testing runner fixture gets its own
  manifest and `.ci-engine` clone; `Specs/Core/Core_Changes.md` now points to the engine contract. Tooling-only;
  native runtime coverage is unchanged.
- Import the build console from the pinned shared CI engine (convergence CV-5-03). `Build/BuildConsole.psm1` is now the
  engine's module shim (`Templates/Project/Build/BuildConsole.psm1`): `build.ps1` and the tooling tests keep importing
  the same path and get the same four functions from `.ci-engine/Tools/Modules/Ci.BuildConsole.psm1`. The zero-warning
  gate also refuses a structured native tool exception dump despite a green MSBuild footer, as RedTilde's did. The
  import refuses with one message when that checkout is missing, at another commit or changed. Tooling-only; no
  compiler or linker setting changes.
- `.git-blame-ignore-revs` lists the reformat of pull request #108 (the shared 155-line `.clang-format`) and its
  merge commit, so `git blame` with `blame.ignoreRevsFile` and GitHub's blame view attribute lines to the change
  before the reformat. The commit that moved one C4191 suppression is not listed, because it changes a line.
  This is the follow-up of step CV-6-05 of the `RedHome` convergence plan (register D-01).
- Restores read vcpkg from complete clones again. Since 2026-11-18 every fresh restore failed inside
  `vcpkg install` with "Could not resolve host: github.com" for a promisor object. vcpkg 2026-07-27 extracts
  each versioned port with `git read-tree` and `git checkout-index` in an environment that holds only
  `GIT_INDEX_FILE`, where Git resolves no host, so it cannot fetch a blob that a `--filter=blob:none` clone
  left on the server. That blob was first needed when upstream microsoft/vcpkg de36db0455 moved
  `vcpkg-cmake` on `master` away from the tree the baseline selects.
  - `Build/VcpkgRestore.psm1` clones the executable and catalogue checkouts completely, fetches a missing pin
    with its history instead of `--depth 1`, and completes a partial or shallow clone from an earlier restore
    in place, keeping its HEAD and working tree.
  - `restore-dxui.ps1` calls the new `Build/VcpkgToolCheckout.psm1` (the same file in every DxUi consumer)
    before the pinned DxUi's `vcpkg-install.ps1`. The call gives that installer a complete checkout of its own
    vcpkg pin, which the installer reuses instead of its partial clone. The DxUi pin is unchanged; DxUi's own
    installer is fixed in DxUi pull request 267.
  - Validation: `VcpkgRestore.Tests.ps1` (complete clones from a server that offers filtering; in-place
    completion; offline extraction as vcpkg performs it), the new `VcpkgToolCheckout.Tests.ps1`, and the
    `restore-dxui.ps1` wiring check in `DxUiRestoreTests.ps1`. Native compile: post-merge qualification.
- The `plugin-development` skill description now says when to use it ("Use when ..."), so every local skill
  description says what the skill does and then when to use it, the form the shared `RedHome` skills use, because an
  agent picks a skill from its description alone (`RedHome` convergence step CV-2-13). Only its front-matter
  description changes. The three `agents/openai.yaml` Codex UI metadata files are removed (CV-2-11, register A-11
  decided "none"), and `Specs/Build/Build_SkillPackages.md` says no skill carries one; the validator still treats the
  file as optional. `AGENTS.md` shrinks from 29,214 to under 21,000 bytes, below the 24,000-byte limit past which
  Antigravity truncates a rule file (CV-3-08): its source-tree map moves unchanged into the new
  `Specs/Core/Core_SourceLayout.md`, indexed in `Specs/README.md`, and `AGENTS.md` links to it and keeps every
  ownership rule. `validate-skills.ps1` passes; no build, test or product input changes.
- Adopted the shared 155-line `.clang-format` of DxUi, RedSalamander, and RedMonitor (SHA-256 `768439d2…` as
  committed) and reformatted every C and C++ source under the `formatRoots` of `.ci/project.json` with the pinned
  clang-format 22.1.3. The change is whitespace only, plus aligned consecutive assignments, `! x` spacing, and the
  160-column limit that the style sets; no include moved.
  `format.ps1` is now the shared script: it takes clang-format from `Install-CiFormatter` in the pinned
  `.ci-engine` instead of `PATH` or Visual Studio, covers the eight extensions CI formats under `formatRoots`
  (now including `RedXeLauncher`), and gains `-Check`, which verifies without writing.
  `Plugins/MatrixRain/GenerateGlyphAtlas.ps1` writes its constants aligned, so a regenerated atlas equals the
  formatted one, and the `Main.cpp` routing check in `BuildProcessTests.ps1` accepts the aligned assignment.
  In `AVControl.Tests.Module.cpp` the reformat moved one `GetProcAddress` cast below the line that its
  `#pragma warning(suppress : 4191)` covers, so the pragma now sits directly above the cast, as it does for
  the file's other exports (found by the zero-warning build; no other suppression in the tree moved).
  `Build_Process.md` gains the shared formatting paragraph, and the validation steps of `Core_CrashHandling.md`
  and `Plugins_API.md` run `format.ps1 -Check`.
  This is step CV-6-05 of the `RedHome` convergence plan (register D-01, D-02, D-05, D-06, E-02).
- `.gitattributes` and `.gitignore` start with the common blocks that every RedSalamanders repository now
  shares, and `.editorconfig` is the shared file. The `.gitattributes` block stores text with LF and checks out
  C and C++ sources, PowerShell, and Visual Studio files with CRLF and Markdown, JSON, and YAML with LF; the
  repository's own lines follow it unchanged and still take precedence. No tracked file needs renormalizing:
  every file with CRLF or mixed line endings in the index is already `-text`.
  This is step CV-6-05 of the `RedHome` convergence plan (register D-03, D-04, and the `.editorconfig` part of B-12).
- Aligned agent guidance and dependency automation with the other RedSalamanders repositories (convergence step 1,
  planned in `RedHome`): `CLAUDE.md` is the same thin pointer to `AGENTS.md` in every repository,
  `.claude/settings.json` enables the shared `redhome` Claude Code skill plugin from the `RedHome` marketplace, and
  Dependabot now ignores `RedSalamanders/.github/*` so it stops opening one notice per shared-workflow caller; engine
  pins keep moving through `Update-ProjectPin.ps1` only. No build, test or product input changes.
- Consolidated the seven shared skills (`direct3d11-rendering`, `modern-cpp-windows`, `performance-resources`,
  `spec-workflow`, `wil-raii`, `win32-windowing`, and `yyjson`) into the organization `RedHome` repository, retaining
  RedXe-specific skills under `.agents/skills/` and validating with `validate-skills.ps1` and `Tools/Test-Changelog.ps1`
  (documentation/tooling only; native qualification unchanged).
  Shared-skill discovery names `../RedHome/AGENTS.md`, the package directory, and the clone URL while preserving
  local product authority.
- Run the existing hidden WARP application, command-line and crash checks against an explicitly retained local DxUi SDK build with full runtime-byte leases and close-time verification. Keep local candidate evidence separate from ordinary qualification, and adopt the measured shared SDK admission engine without removing local or main gates.
- Add an explicit local Release SDK build for the application and plugin closure in isolated leased outputs. Keep ordinary dependencies separate and retain full ABI checks only in actual DxUi consumers; output-only plugin checks import no SDK archive. The result grants no test, repository or production qualification.
- Give each MSBuild operation a private compiler metadata namespace through the pinned shared engine, preserving live console/log output and caller state. Child environment transport retains direct process identity and suspended containment, with Unicode/override/removal and timeout/lifetime regression controls; existing native qualification gates remain required.
- Retain packaging's selected compiler-tool getter across nested provenance module
  reloads so production SDK packages reopen their original reader on x64 and ARM64.
- Preserve successful independent tooling evidence before native tests after verifying its complete unchanged source/tool/environment inputs. Native failures retain that proof for exact retries; failed or input-mutating independent checks publish no success and stop before native execution, while native and whole-run coverage remain separate.
- Bind local build and native/tooling evidence to the actual pinned engine checkout, closing the ignored-engine input gap. Missing, modified, foreign or wrong-version engines refuse reuse; generated engine build outputs remain excluded, and existing local/main gates remain required.
- Fix offscreen AV measurements after a verified DxUi SDK build by reopening its original dependency inputs and preserving module, archive and measurement provenance checks.
  Freeze the wrapper and local build/provenance helpers before execution and reject changed tooling, binary, fixture or dependency inputs after finalization before publishing provenance.
- Build and package Release inputs through the qualified SDK v2 reader, retaining
  original producer identities and validating the selected compiler, headers,
  libraries and per-file ABI settings. Keep source fallback and native consumer
  tests; refuse unknown package dependency recipes.
- Add an explicit byte-verified pinned Release SDK recipe with shared ABI guards, exact link provenance and package notice staging. Hosted main build roles can request the same inputs while retaining source fallback and every native test/package gate; Debug and ASan keep source.
- Enable authenticated qualified DxUi SDK reuse for eligible hosted Release builds, preserving the existing dependency pin and all native regression, detector and package checks.
- Add the active `Specs/Plans/WIP/ExplicitStateMachines_2026-11-10.md` execution plan cataloging RedXe's 27 implicit state machines and 24 verified defects (`RX-SM-01`..`RX-SM-24`) across host window/gesture/overlay/settings code, `PluginHost`, bundled widget/service/camera/broker plugins, `RedXeLauncher`, and `Build/*.psm1` tooling, organized into six non-breaking slices.
  Validation: relative link verification, `validate-skills.ps1`, `Tools/Test-Changelog.ps1 -Base origin/main`, and `Tools/Invoke-ProjectCi.ps1 -Phase Static` passed; production code, plugin ABIs, and `DxUi` pin are unchanged while implementation and native runtime gates remain pending.
- Preserve runtime Markdown and fixture README bytes in native test selection and receipts while keeping owned documentation on independent validation. Add scoped routing and byte-mutation controls; hosted native qualification remains separately recorded.
- Adopt the shared guard and explicit owned-prose exclusions so new runtime Markdown and notice inputs retain native qualification; preserve existing native gates and historical artifact identity.
- Adopt the shared engine's ABI-indexed vcpkg fallback cache and manual native dependency caching; keep compiler-free PRs, exact dependency pins and every native qualification gate.
- Move the DxUi pin from side-branch commit `616104f3` to DxUi main `f2e328a0` (API revision 4). Main carries DxUi's
  2 November delivery, what the old pin's four branch-only commits did, and submenu hover-timer cancellation on
  keyboard and UI Automation focus. Restore now refuses a pinned source whose own `Build/NativeBuildPolicy.targets`
  differs from RedXe's, so the policy hash in the build identity attests the copy that compiles the archive. One adapter
  change: DxUi now exposes `PageIndicator` to UI Automation as a slider whose value is the page and no longer names it
  "Page N of M", so AV Control names its indicator `Page`. AV Control's preparation callback only requests a frame,
  and the host rebinds its state after embedded preparation, so neither needs a change. x64 Debug builds with zero
  warnings and passes the full non-interactive gate. DxUi's own review still keeps main short of a production pin:
  its P0 (PRIOR-03) is unreachable from RedXe, but CTRL-01 drops supplementary characters typed as `VK_PACKET` into
  AV Control's profile-name field. DxUi's planned history rewrite will need a re-pin, and a production package of the
  merged commit is still due.
- Correct the WIP index so each row names the remaining work and records open release gates or unresolved owner scope decisions without granting deferrals. The delivery review is an evidence annex: its `8c882636` package command is superseded by the red-team package of `c61861c`. The Actions status names the unpublished publishers and marks the Zoom SDK sections as history. The AV continuation is not a resume list. The boundary plan drops its obsolete Python and ARM64 environment notes and keeps G2 and H3. The merged-review publication check follows `qualify.yml` and `publish.yml`. The runtime summary retains manual adapter/DPI and single-LUID qualification, the unexplained Release screenshot access violation, its other open gates, and historical September line numbers. AV release requirements and RedMonitor physical, paired-performance, and post-integration native ARM64 gates stay open. PresentMon remains the host-chrome C8 receipt pending capture or an explicit owner decision. Validate with `Tools/Test-Changelog.ps1`, `validate-skills.ps1`, read-only diff/link checks, and affected scoped checks. No product code or native test changes.
- Record the 31 October delivery red-team ship list for tip `c61861c`: engine AND-gate, production-receipt leftovers, production packaging of that tip, the pinned DxUi submenu hover-timer cancel, and the P1/P2 follow-ups. Keep the earlier delivery-review document as the product-evidence annex. No product behavior changes; RedMonitor physical, paired-performance, and native ARM64 gates stay open.
- Restore the independently pinned ports catalogue for vcpkg version resolution while preserving the executable pin, dependency floors and platform-private package roots. Verify dirty/foreign refusal, immutable pins, caller environment and deep port paths; native x64/ARM64 qualification remains required.
- Adopt shared CI v1.0.6: DxUi SDK ZIPs stay internal with no publication workflow; application publication remains manual and artifact-only. Document the organization token route and separate implementation closeout from source-bound hosted acceptance; no product or winget publication is performed.
- `--redmonitor-probe` prints one line naming its outcome on the console or redirected output, and still never shows
  a box. Without RedMonitor installed, `--redmonitor` now points at RedMonitor's releases page, and at building its
  package until a release exists. `test.ps1` checks the probe's no-listener line. The docs, the RedMonitor
  integration contract, the WIP plan index and the `AGENTS.md` module tree are updated. `Build_Process.md` now
  refers to the DxUi lock instead of naming a replaced commit.
- Package the Visual C++ runtime of the toolset that compiled the binaries. `package.ps1` took the newest CRT under
  Program Files, so on hosted runners, where the required MSVC 14.52 is installed under `RUNNER_TEMP` beside the image's
  older stock Visual Studio, a production ZIP would have shipped the stock 14.51 runtime with 14.52-built binaries.
  The CRT now comes from the installation of the MSBuild the build runs (`Find-RedXeMSBuild`), and packaging fails when
  it is older than `RequiredMsvcToolsVersion`. `PackagingTests.ps1` covers the default root, the floor and the wiring;
  each check fails against the old lookup. Not yet seen in a hosted production run.
- Make the DxUi output root and the zero-warning gate safe for parallel and repeated builds. Restores of one new
  build fingerprint that start together, such as parallel builds in one checkout, all get the root. Before, every
  restore but the first failed on the identity marker the first one wrote. `Initialize-RedXeDxUiOutputRoot` renews
  the root's lease, then checks or creates the root and its marker under the root's own mutex. It rewrites an empty
  marker that a killed restore left alone in its root, and any other refusal names the folder to delete. A build the
  zero-warning gate refuses now resets the MSBuild tracking logs of every project its diagnostics name, so an
  unchanged rerun compiles those files again and keeps failing instead of reporting green. The diagnostic summary
  uses the gate's grammar, so a warning without a code no longer prints as "0 warning(s)" before the gate refuses
  it. `-Run` checks the OS architecture instead of the PowerShell process's. The entry scripts declare PowerShell 7.
  `DxUiRestoreTests.ps1` runs 40 parallel first initializations (10 of them fail without the mutex). It also checks
  the lease of a refused root, both empty-marker cases and the 16-digit source folders, and replaces an ordering
  check that could not fail. `BuildProcessTests.ps1` covers the shared grammar, the tracking reset and the
  `build.ps1` gate wiring.
- Pin the common CI engine to v1.0.5: verify native portable winget registration, every installed command alias, declared smoke probes and owned cleanup; retain the compiler-free custom dependency status observer and recorded owner-only emergency PR merge route. Publication remains an explicit action requiring both matching native Release regression and production ZIP receipts.
- Bind DxUi provenance to the test or production role so shipping builds do not require unbuilt test modules, and refuse cross-role output during tests or packaging.
- Document the explicit organization-owner, PR-only emergency merge route with a recorded reason and exact-head match; preserve actual checks and native publication qualification.
- Retain stdout/stderr and real exit codes for windowless hosted native commands through the common pinned CI engine v1.0.4.
- Upgrade the shared CI engine to v1.0.3, fixing manual formatter staging and distinct artifact/publication outputs. Preserve failed pilot receipts and require fresh native qualification for the new engine identity.
- Introduce the pinned organization CI adapter: compiler-free PR feedback, quiet-window main admission, native Release test-to-production gates, ZIP qualification and explicit publication. Preserve canonical project test, dependency, toolchain and evidence contracts; hosted rollout acceptance remains in progress.
- Check Weather's per-timestamp daylight-saving conversion in fixed W. Europe and AUS Eastern zones through a test-only zone override, so the regression also fails on UTC hosts. The checks cover winter and summer clocks, both minutes of each 2026 transition, and a Sydney local day that differs from the UTC day. Production still converts with the active zone, and an unknown zone name is refused. `WeatherTests.exe` passes on x64 Debug.
- `test.ps1` now completes the run's evidence record before it prints its verdict: when completion fails after a passed
  run, the run fails with that error instead of following a printed success, and after a failed run it is a warning that
  names the run directory while the run's own failure stays the reported error (an exception thrown from the `finally`
  block used to replace it). In `Build/TestRunEvidence.psm1`, Windows failures name the OS reason before the path and
  keep the native error code, run ids use the invariant Gregorian calendar under any current culture (th-TH no longer
  writes year 2569), inventories and named streams are compared in ordinal order instead of the current culture's, `\x`
  and `C:x` repository arguments are refused with the drive-root and UNC ones, and a one-entry or empty native-artifact
  inventory and a one-element record stay JSON arrays instead of a bare object or null. Every negative control in
  `TestRunEvidenceTests.ps1` now asserts its specific error and cannot create output outside its scratch directory, and
  each fix has a control that fails when the fix is reverted (74 checks on ReFS, 75 on NTFS). `Tests/README.md` and the
  evidence contract no longer say the AVControl PNGs and CameraPackage output are retained in the evidence: they are
  written to `.build/test-artifacts`, the next run overwrites them and `test.ps1` does not pass
  `-CaptureNativeArtifacts`. The self-test and soak log paths now point at `.build/evidence/test-runs/<id>/payload`.
  Validated with `pwsh -NoProfile -File Tests/BuildProcessTests/TestRunEvidenceTests.ps1`, a parse check of `test.ps1`,
  ten one-fix-reverted variants of the module, and the runner's own finalization statements executed for passed and
  failed runs with and without a completion failure; `test.ps1` itself and a native build were not run.
- Hardened the PowerShell skill validators. `SKILL.md` files with a UTF-8 byte order mark or in UTF-16/UTF-32 are rejected
  again (NUL bytes and invalid UTF-8 too, for every package Markdown file), and a file that cannot be read is a problem or
  failure that names it under any caller `$ErrorActionPreference` instead of passing on the previous file's text. An
  unclosed code fence is reported with its file and line while the text after it is still checked. Package links skip
  footnote definitions, drop a `?query` before the existence check, reject a path that differs from the file on disk only
  in letter case (GitHub is case-sensitive), and no longer let a stray backtick pair across list items, headings, table
  rows or block quotes and hide a link; a relative `-Root` resolves against the PowerShell location, not the process
  directory. Metadata and package validation now share one fence and code-span implementation (`Build/SkillText.psm1`),
  and the duplicated ancestry check is the one reparse helper, which still refuses a linked `.agents/skills` before listing
  it. The scope planner drops its hard-coded tooling path list for an `"exclusive": true` rule flag in
  `Tests/test-scopes.json`: an exact path replaces the broader rules, a sibling keeps them, and a wildcard or non-boolean flag
  is refused. `SkillValidation.Tests.ps1`, `SkillPackageIntegrity.Tests.ps1` and `ScopedTesting.Tests.ps1` pair each fix with
  an accepted control, and `validate-skills.ps1` passes on the real corpus.
- Releases now publish a symbols ZIP per platform and the package carries only the RedMonitor client's MIT notice.
  `package.ps1` writes `RedXe-<version>-<platform>-Symbols.zip` beside the portable ZIP, with a `.sha256` sidecar in
  the portable ZIP's form (both written by one helper): exactly the PDBs of the first-party executables and DLLs the
  portable ZIP ships, taken from the same Release output at the package's relative paths, so a crash dump of the
  optimized /LTCG binaries can be symbolized. Runtime and vcpkg DLLs and test PDBs are left out; packaging fails naming
  the binary when a first-party binary has no PDB or one more than 2 s older than it (the Debug linker output shows the
  PDB landing within milliseconds on either side). `release.yml` uploads the symbols ZIP with the package, requires it
  for every built platform, verifies its sidecar, adds it to `checksums.sha256` and publishes it; the winget workflow
  and manifest tooling select the Portable ZIPs by exact name and are unaffected. `RedMonitor-LICENSE.txt` was the
  whole RedSalamander licence with notices for components RedXe does not ship; it is now the MIT section extracted from
  the unchanged, hash-pinned vendored file, packaging fails unless that section occurs exactly once, and the package
  smoke re-checks the shipped file. `PackagingTests.ps1` covers both from fake outputs, fixtures and the real vendored
  file (17 new checks, whole script passed under pwsh); defects injected into a scratch copy of the module were each
  caught, and the `release.yml` verification step ran against fabricated assets. Not run here: a real Release
  `package.ps1`, `test.ps1`, or a Release workflow dispatch, so the first published symbols ZIP still needs a look.
- Read `--redmonitor` and the now-catalogued `--redmonitor-probe` through the command-line catalog, alone on the line and before settings, plugins or windows exist. `--redmonitor` now names the folders it searched or the launch failure instead of exiting 1 silently, and the probe never shows a box. RedMonitor's client header and link libraries stay in one adapter translation unit. `test.ps1` covers both refusals, an absent viewer and the probe without a listener; RedMonitor's real ETW roundtrip passes against the Debug build.
- Fold changelog fragments on `main` after merge, identically in DxUi, RedSalamander, RedXe, RedPrism, RedTilde and RedMonitor. A change keeps its fragment and never edits `CHANGELOG.md`, so parallel pull requests no longer conflict on the changelog. `Tools/Submit-ChangelogFold.ps1` folds the default branch in a temporary worktree and opens a fold pull request, never touching the caller's checkout. PrePush (`-Base origin/main`) and the CI tooling job (`HEAD^1` for a pull request, the replaced tip for a push) refuse any `CHANGELOG.md` change that is not an exact fold of fragments already merged. Folding now flushes its staged changelog before publication and decodes it as strict UTF-8. A removal that fails after publication says how to finish. Reparse checks stop at the repository root, and the fold's lock and stage files are ignored by Git. The shared behaviour suite covers these cases with local Git fixtures and a bare remote.
- Bound hosted MSVC provisioning: the bootstrapper download now has a 300-second timeout with three retries; a stalled installer is ended after 900 seconds with its logs kept; and installer logs upload only when provisioning fails, kept for 14 days. Pull-request runs still cancel superseded checks, but every push to `main` now runs to completion for the release gate. The RedMonitor client gate runs under Windows PowerShell, so an IDE build needs no PowerShell 7, and skips when its inputs are unchanged. Its reparse checks stop at the checkout root and name the linked folder. `build.ps1` and `restore-dxui.ps1` share one compiler-host pair, and the dead ARM64 guard is removed. The compiler-family guards share one check, with controls refusing MSVC 14.51 in the MSBuild target. README, `AGENTS.md` and `.vsconfig` now name the Insiders 14.52 requirement. MSVC setup, RedMonitor, release-policy, compiler-host and DxUi restore suites pass; the hosted workflow changes run only in CI.
- Record the validated build-policy matrix, preserved binary-size evidence, completed quiet performance study and explicit merge handoff; retain unresolved performance uncertainty and any hosted CI limits.
- Record passing local x64 runtime qualification and ARM64 cross-builds for the common build policy, with immutable Release size evidence and remaining performance/CI gates explicit.
- Record the current native profile and pinned-archive policy audit, qualified native builds and explicit remaining runtime, performance and integration gates.
- Adopt the exact API4 DxUi maintenance commit qualified across six native builds, three ordinary x64 test profiles and three approved focus profiles; retain explicit product restore, build and runtime qualification gates.
- Preserve an existing short DxUi source folder when its complete Git commit differs from the requested pin; refuse the conflict under the restore mutex and test that its commit, tracked contents and local evidence remain intact.
- Clarify build guidance: leave `/WX` disabled and require every project-code warning to be fixed before the build validation gate can pass.
- Scope the ARM64 volatile-model diagnostic to the isolated use-after-free detection fixture, retaining the real sanitizer failure, selected volatile model and ordinary warning policy.
- Scope the MSVC ARM64 volatile-model notice to the deliberate isolated crash/sanitizer probe. Keep the selected volatile model, genuine crash/sanitizer detection and ordinary warnings unchanged.
- Retain the working-set soak budget for ordinary profiles without an unused-constant warning in ASan, where that diagnostic budget is intentionally not used.
- Lock DxUi API4 at fa3de6c980f4d7d2700e44a82ad9e751cbebcdc3 for the native warning/Release folding policy. Preserve the consumer API boundary and restore matching headers/archive through the normal entrypoint; complete product qualification is tracked by the indexed native policy plan.
- Keep Debug allocation hooks non-throwing and annotate the CRT-erased previous-hook type only at restoration. Make Logicon monitor layout conversions explicit while preserving COM ownership traits, and retain the Release-only GPU-query helper without a Debug unused-function warning.
- Make byte decoding, numeric drawing and HRESULT boundaries explicit for the zero-warning /Wall build; retain named-export and subclass ABI annotations at their reviewed call sites and remove unused private helpers. Native and runtime qualification remains tracked in the build-policy plan.
- Unify native builds on /Wall without /WX and require zero diagnostics before accepting success; align Debug /ZI and ASan /Zi, enable Release-only /OPT:ICF, print green artifact/log links with quoted launch commands, and shorten consumer DxUi source/output folders while preserving complete collision-checked identities. Native qualification remains tracked in the active build-policy plan.
- Keep four-space and tab-indented code from opening or closing Markdown fences over later links or unfinished instructions, with focused admission and valid-closer controls.
- Distinguish line-leading triple-backtick inline code from fenced examples so later missing links and unfinished instructions stay visible to validation; retain valid tilde-fence info strings and add focused controls.
- Preserve date-prefixed description prose while rejecting complete unquoted timestamp scalars, and keep isolated edits to the migrated metadata fixture in the independent tooling scope with retained shared-build fallback coverage.
- Close reviewed skill-validator gaps for paragraph/code boundaries, continued references, parenthesized titles, literal file-URI filenames, linked-root admission, quoted timestamps and hidden packages; add focused positive/negative controls and preserve separate native/model qualification gates.
- Correct category-aware widget/service template guidance and stale skill routes/matrix claims; route mutable layout/settings detail to its owning contracts, and add read-only package/reference checks with negative fixtures. These tooling checks do not establish model behavior or native product qualification.
- Run skill validation and its behavioral tests on PowerShell 7 without Python/PyYAML; retain multiline descriptions and fenced examples, reject unsupported YAML explicitly, and remove interpreter discovery from tooling receipts.
- Allow 20 minutes for hosted compiler provisioning and the complete independent tooling job after the measured 15-minute timeout; retain all test scopes and individual/native deadlines.
- Bind hosted build and restore to the verified MSVC 14.52 installation's MSBuild, preserving strict compiler-family checks and local read-only behavior; cover stale ambient discovery and missing-MSBuild refusal.
- Wait for the hosted Visual Studio GUI installer and use its explicit result before verifying MSVC 14.52; retain installer diagnostics and cover delayed completion, failures and strict tool/refusal checks without a real installation.
- Bind the Release policy regression to the directly guarded dependency-identity assignment, so the outer native-compiler-host environment guard does not create a false duplicate. Retain success/failure caller-environment restoration assertions.
- Apply the forecast timestamp's daylight-saving rules when formatting Weather times and local-day groups. Preserve the failed winter/summer fixture and add exact civil-hour/day-label round trips for all twelve months.
- Integrate the standalone RedMonitor pinned header client with MSVC 14.52 Release optimization and current main, preserving existing tests, source-bound qualification evidence and separate native/manual/release gates.
- Complete local MSVC 14.52 Release optimization qualification in all six build profiles and all three Full x64 test profiles, with reviewed size/performance evidence and separate native ARM64 and hosted provisioning gates.
- Record the agreed local x64 test and ARM64 cross-build scope for MSVC 14.52 qualification; retain native ARM64 execution and hosted CI provisioning as separate handoff gates.
- Use a 64-bit compiler host for ARM64 targets under MSVC 14.52 and forward that selection to pinned DxUi, avoiding the observed 32-bit SystemData compiler-heap failures while preserving x64 runtime flags.
- Prepare and verify the required MSVC 14.52 toolchain in active Windows CI, with read-only local checks and refusal controls; hosted installation and native ARM64 runtime results remain separate qualification gates.
- Require MSVC 14.52.x and enable Release function-level/whole-program compilation and unused-function elimination with full LTCG; forward exact tools and WPO into the adopted DxUi archive with a separate policy fingerprint. Validation and measured before/after evidence remain tracked in the active build-policy plan.

- Point RedMonitor contract and adoption-plan links to its merged main branch so retiring the delivery branch preserves documentation navigation.
- Record successful hosted tooling and all six native Full profiles for the RedMonitor/compiler-host integration, with independently verified runtime archives and ARM64 provenance; retain the earlier idle failure and separate physical, performance and release gates.
- Start the construction-lane idle measurement after finite completion notifications quiesce. Add a deterministic late-notification witness and separate CPU/wait diagnostics while keeping the existing 250-ms sample and 1-ms CPU budget. Leave the production event-blocked queue unchanged and preserve the earlier native ARM64 Release failure without assigning an unproved cause.
- Match the native compiler host during DxUi restore and RedXe compilation on ARM64 as well as x64, including emulated PowerShell. Preserve explicit overrides and restore the caller environment. Validate native/emulated host and override combinations through real MSBuild property evaluation; retain the earlier hosted ARM64 mismatch as failed evidence.
- Share only the pinned RedMonitor client header and MIT notice, verify them offline during builds, and discover the separately installed production Release helper for explicit launch or optional bare Debug startup; keep normalized bounded diagnostics and application-only packages.
- Add the exact pinned RedMonitor producer SDK, bounded application-labelled diagnostics and explicit optional adjacent viewer command; preserve existing logs, crash handling and application ownership, with consumer qualification tracked separately.
- Verify retained run inventories using fresh pinned-handle metadata and stable path/stream ordering; preserve before/after move diagnostics and exact safety checks, with stale-metadata and reversed-enumeration regressions.
- Preserve unique completed, failed and interrupted test output in verified per-run evidence without modifying shared read-only files or weakening qualification checks.
- Require explicit user authorization for each exact project-specific `.Perf` root, keep other generated output inside approved workspaces, and preserve evidence and live references during artifact cleanup.
- Resolve explicit relative Changes roots against the current PowerShell location; regression coverage verifies validation, preview and folding leave a different process-directory checkout untouched.
- Use the configured PATH Python for skill/tooling validation, keeping its installed dependencies instead of selecting a different Windows launcher installation; retain launcher fallback and regression coverage.
- Keep new entries under Unreleased when a released heading uses a tab separator, and terminate an owned timed-out tooling fixture child tree before cleanup. Add independent released-heading and process-timeout regressions, and preserve the concurrent top-level drive-root guidance layout.
- Standardize the Changes workflow across DxUi, RedSalamander, RedXe, RedPrism and RedTilde with shared validation, safe folding and behavioral fixtures. Fold before PrePush/final handoff and immediately after integration, even for one fragment; retain native and product qualification as separate gates.
