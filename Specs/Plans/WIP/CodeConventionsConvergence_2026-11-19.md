# Code conventions convergence (RedHome CV-6-08)

Status: ACTIVE — the local convention changes have merged. API 6 adoption and the source-bound replays remain open.
Owner: RedXe maintainers.
Prepared: 2026-11-19, after the 155-line reformat (#108).

- **Origin**: step CV-6-08 of the cross-repository
  [convergence plan](https://github.com/RedSalamanders/RedHome/blob/main/Specs/Plans/WIP/Convergence_2026-11-17.md)
  and section H of its
  [divergence register](https://github.com/RedSalamanders/RedHome/blob/main/Specs/Plans/WIP/Convergence_2026-11-17/DivergenceRegister.md).
  Each register row names one target for every repository; this plan lists the rows that move RedXe.
- **Owning contracts**: [display and windowing](../../UI/UI_Windowing.md), [settings](../../Core/Core_Settings.md),
  [performance and resources](../../Core/Core_PerformanceAndResources.md), [source layout](../../Core/Core_SourceLayout.md),
  [DxUi integration](../../Core/Core_DxUiIntegration.md), [crash handling](../../Core/Core_CrashHandling.md),
  [build process](../../Build/Build_Process.md) and [changes workflow](../../Core/Core_Changes.md). The shared skills
  in `../RedHome/.agents/skills/` (`d3d11-win32-host`, `yyjson`, `app-settings`, `file-io-safety`, `localization`,
  `test-evidence`) state the target rules once.

## Rules

- One register row per pull request, with its `Changes/` fragment. A style or flag change never shares a pull
  request with a functional change.
- Merge gate (RedHome plan decision D21): the pull request checks pass and the six profiles (x64 and ARM64 ×
  Debug, Release, ASan Debug) build with zero warnings. Tests run where they can and are otherwise recorded as
  "tests deferred (D21)". A compile is not a runtime pass.
- No user-visible behavior changes beyond the row's target. A switch's catalog text in `RedXe/CommandLine.h`, the
  "Command line" section of `docs/usage.md` and the mode table of the display spec change together.

## Rows

- [x] **H-19 Device-loss results.** `Renderer::IsDeviceLost` includes `DXGI_ERROR_DEVICE_HUNG`. RedXe continues to
  own the recovery boundary for its swap chain. [#165](https://github.com/RedSalamanders/RedXe/pull/165)
- [x] **H-11 `--self-test` and WARP.** Hidden startup uses WARP for `--self-test`.
  [#167](https://github.com/RedSalamanders/RedXe/pull/167)
- [x] **H-12 Host windowing spec name.** [UI_Windowing.md](../../UI/UI_Windowing.md) owns the active contract and
  its XENEON EDGE behavior. The original path routes archived references.
  [#169](https://github.com/RedSalamanders/RedXe/pull/169)
- [x] **H-20 Occlusion.** Production render scheduling follows window visibility, minimization, DWM cloaking,
  suspend and display power. [#171](https://github.com/RedSalamanders/RedXe/pull/171)
- [x] **H-25 Guarded replacement.** D23 keeps the accepted target version guarded through publication. The normal
  rename holds the guard, and the classic fallback performs a final name and version check. Cold malformed-file
  recovery guards both the backup and creation of defaults. [#161](https://github.com/RedSalamanders/RedXe/pull/161) ·
  [#182](https://github.com/RedSalamanders/RedXe/pull/182)
- [x] **H-09 `yyjson` read flags.** RedXe reads JSON5 with BOM support. Its source cursor understands the same
  syntax for diagnostics and source-preserving dock updates. [#173](https://github.com/RedSalamanders/RedXe/pull/173)
- [x] **H-15 Settings reload.** The watcher follows RedHome decision D19. Optional root and dock preferences take
  field defaults and keep unknown members across writes. The settings store recovers malformed JSON with a backup
  and the template. A readable file that fails the product's compatibility or dashboard validation stays in place
  while the app uses the deployed configuration in memory. The next valid version can apply through the watcher.
- [x] **H-06 and H-08 Resource strings.** Native window captions, tray chrome, startup and crash prompts, and settings
  and plugin-action notices use the common resource helpers. Root validation checks positional formats.
  [#178](https://github.com/RedSalamanders/RedXe/pull/178) · [#186](https://github.com/RedSalamanders/RedXe/pull/186) ·
  [#189](https://github.com/RedSalamanders/RedXe/pull/189)
- [x] **H-16 Crash artifacts.** Under D24, healthy interactive startup claims one pending notice and keeps the
  newest ten owned dump and sidecar pairs. Legacy artifacts are preserved.
  [#143](https://github.com/RedSalamanders/RedXe/pull/143)
- [x] **H-23 Keyboard state applicability.** Interactive widget forwarding reads current modifiers. Hidden tests
  exercise the message policy as pure functions. Recheck H-23 if a hidden fixture begins driving widget keys.
- [x] **H-27 Member naming.** The root `.clang-tidy` and changed fields follow the common private and protected
  member naming rule. [#184](https://github.com/RedSalamanders/RedXe/pull/184)

## API 6 receiving work

- Select a qualified exact API 6 revision under D20. Apply the public consumer ABI and failure-host changes
  (H-01 and H-02) and the shared logging and posted-payload interfaces (H-05 and H-14) where RedXe uses them.
- Adopt the producer's compatible restore and provenance tooling, pinned native build policy, neutral triplet
  property and MSVC family guard. Preserve the public RedXe commands and documented profile exceptions. Bind the
  archive, headers and policy to the same commit. [DxUi consumption contract](https://github.com/RedSalamanders/DxUi/blob/main/Specs/Build/Build_ToolchainAndConsumption.md) ·
  [Shared toolset guard](https://github.com/RedSalamanders/DxUi/pull/340)
- Verify all six evaluated build profiles and exact native builds, clean restore and rejection of an incompatible
  pin. Run affected native x64 and ARM64 and clean-package checks under the owning RedXe contracts before closing
  the receiving work.

The [RedHome D21 ledger](https://github.com/RedSalamanders/RedHome/blob/main/Specs/Plans/WIP/Convergence_2026-11-17/DeferredTests.md)
retains the separate runtime, hardware, tray and performance replays at their own sources.
