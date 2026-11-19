# Code conventions convergence (RedHome CV-6-08)

Status: ACTIVE — plan written; each row lands in its own pull request.
Owner: RedXe maintainers.
Prepared: 2026-11-19, after the 155-line reformat (#108).

- **Origin**: step CV-6-08 of the cross-repository
  [convergence plan](https://github.com/RedSalamanders/RedHome/blob/main/Specs/Plans/WIP/Convergence_2026-11-17.md)
  and section H of its
  [divergence register](https://github.com/RedSalamanders/RedHome/blob/main/Specs/Plans/WIP/Convergence_2026-11-17/DivergenceRegister.md).
  Each register row names one target for every repository; this plan lists the rows that move RedXe.
- **Owning contracts**: [display and windowing](../../UI/UI_XeneonDisplayWindowing.md), [settings](../../Core/Core_Settings.md),
  [performance and resources](../../Core/Core_PerformanceAndResources.md), [source layout](../../Core/Core_SourceLayout.md),
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

- [ ] **H-19 Device-loss results.** `Renderer::IsDeviceLost` accepts `DXGI_ERROR_DEVICE_REMOVED`,
  `DXGI_ERROR_DEVICE_RESET` and `DXGI_ERROR_DRIVER_INTERNAL_ERROR`. Target: the shared set, which adds
  `DXGI_ERROR_DEVICE_HUNG`. Direct2D does not draw to RedXe's swap chain, so `D2DERR_RECREATE_TARGET` stays out.
- [ ] **H-11 `--self-test` and WARP.** Only `--warp` sets `forceWarp`. Target: `--self-test` forces WARP too, as in
  RedPrism, so the hidden startup check runs on the same device on every machine.
- [ ] **H-12 Host windowing spec name.** The spec is `Specs/UI/UI_XeneonDisplayWindowing.md`. Target:
  `Specs/UI/UI_Windowing.md`, keeping its XENEON EDGE sections, with every link and source comment updated.
- [ ] **H-20 Occlusion.** The renderer registers a DXGI occlusion-status message and acts on `DXGI_STATUS_OCCLUDED`
  from its flip-model `Present`. DxUi measured that a flip-model `Present` never returns it on Windows 11 25H2
  (DxUi `Measurements/FlipModelOcclusion_2026-11-19/`). Target: occlusion from signals the application owns
  (minimize, visibility, cloaking, display power), as `d3d11-win32-host` states.
- [ ] **H-25 Replace sequence.** `CommitTemporaryFile` in `RedXe/Settings.cpp` publishes with `MoveFileExW` and
  write-through. Target: `ReplaceFileW` over an existing file after its identity check, `MoveFileExW(MOVEFILE_WRITE_THROUGH)`
  for a new one, with one fault-injection hook per step.
- [ ] **H-09 `yyjson` read flags.** Settings are read with comments, trailing commas and BOM. Target:
  `YYJSON_READ_JSON5 | YYJSON_READ_ALLOW_BOM`. JSON5 also admits `NaN` and `Infinity`, so every numeric field check
  must reject non-finite values before the flag changes, and the strict-rejection settings tests and the diagnostic
  locator in `RedXe/SettingsJsonText.h` are re-checked.
- [ ] **H-15 Settings reload.** The watcher already follows RedHome decision D19. Target: field-level fallback (an
  unusable value takes its default or is clamped, and only an unparseable file is invalid) instead of whole-candidate
  rejection, and unknown members kept across a save.
- [ ] **H-06 and H-08 Resource strings.** RedXe has no `STRINGTABLE`. Target: chrome strings in resources through the
  shared helper set, then the resource-string gate.
- [ ] **H-23 Keyboard state under hidden self-tests.** `Application` reads modifiers with `GetKeyState`. Target: an
  application-owned keyboard state wherever a hidden self-test drives keys; first establish whether one does.

## Rows that wait

- **H-01, H-02** wait for API 6 (D20); **H-05, H-14** wait for DxUi to publish its helpers with API 6.
- **H-16 Crash artifacts** waits for an owner choice of one shape.
