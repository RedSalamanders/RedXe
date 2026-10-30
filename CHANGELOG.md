# Changelog

Entries are generated from [Changes](Changes/README.md). Existing release notes remain in their original locations.

## Unreleased

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
