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
- [ ] Pass x64 Release, Debug, and ASan Debug product gates, including hidden WARP AV/host tests and real sanitizer probe.
- [ ] Build ARM64 Debug, Release, and ASan Debug; native ARM64 runtime acceptance is a separate host gate.
- [ ] Pass the pinned library's consumer/matrix validation, skill validation, and source diff checks.
- [ ] Record any failure and its resolution, commit the adoption, review the PR tree, and merge normally to `main`.

Rollback is a reviewed revert of this entire adoption (lock and supported revision together), then a rebuild and
product test against the previous revision 3 source. Keep the last qualified product package available.
