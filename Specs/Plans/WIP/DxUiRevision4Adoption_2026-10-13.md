# DxUi revision 4 adoption

Status: ACTIVE (2026-10-13)

Owner: `Specs/Core/Core_DxUiIntegration.md`. AV release qualification remains in
`Specs/Plans/WIP/RFC_Plugins_AVControl.md`.

## Scope and decision

Move the RedXe consumer lock from DxUi `271bd54be24eadf3f03ba5221d1be7be069860f2` (API revision 3) to
`fa4da2cb112e029646835f81f14ee2e0185129a7` (API revision 4). The latter is the current DxUi `main` commit and
its [push CI](https://github.com/RedSalamanders/DxUi/actions/runs/38351048921) completed successfully. The
compare endpoint reports the new commit ahead by 203 commits. Its `capabilities.json` names revision 4.

The new optional intrinsic layout, forms, Tree and interaction APIs do not change RedXe's AV layout or host/plugin
boundary. The current AV adapter must compile against the new headers and the native AV tests must exercise it.
The existing AV real-client and resource gates remain open in their owning plan. The pinned DxUi
`vcpkg-install.ps1` still does not expose `-MSBuildPath`; `MergedReviewFixes_2026-10-06.md` retains that follow-up.

## Checklist

- [x] Verify the candidate is current main, ahead of the old pin, CI green, and at API revision 4.
- [x] Update the lock, supported revision, consumer contract, and revision-sensitive test fixtures together.
- [x] Pass x64 Release, Debug, and ASan Debug product gates, including hidden WARP AV/host tests and real sanitizer probe.
- [x] Build ARM64 Debug, Release, and ASan Debug; native ARM64 runtime acceptance is a separate host gate.
- [x] Pass the pinned library's consumer/matrix validation, skill validation, and source diff checks.
- [ ] Record any failure and its resolution, commit the adoption, review the PR tree, and merge normally to `main`.

## Validation checkpoint

- The final committed `a23d155` candidate returned `FULL_NONINTERACTIVE_PASSED` in all three x64 PrePush profiles.
  The ASan run included the deliberate detection probe. ARM64 Debug, Release and ASan Debug built with zero warnings
  and errors; the ARM64 ASan executable imports `clang_rt.asan_dynamic-aarch64.dll` and sanitizer symbols.
- The pinned DxUi `validate-build-matrix.ps1` reported seven project/solution files and zero mapping errors.
  `validate-skills.ps1` passed all ten skills. The current source diff passes `git diff --check`.
- An early local Release run passed its native scopes but was marked `NOT_RECORDED` because the revision-sensitive
  fixture files changed during it. The committed candidate's subsequent Release run recorded the full pass.
- PR #45's first hosted native Release job passed. Its independent tooling job failed while evaluating
  `Plugins/AVControl/AVControl.vcxproj`: `DxUiRoot` was empty because that job restores only the pinned source and has
  no platform output props yet. `LinkConfiguration.Tests.ps1` now supplies the exact restored pin as an MSBuild
  property. Its 68 local evaluations and the full independent tooling suite pass; a fresh hosted rerun is pending.

Rollback is a reviewed revert of this entire adoption (lock and supported revision together), then a rebuild and
product test against the previous revision 3 source. Keep the last qualified product package available.
