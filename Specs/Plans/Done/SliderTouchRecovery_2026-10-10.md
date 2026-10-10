# Slider and touch branch recovery

Status: Done, 2026-10-10; recovery from `codex/slider-touch-review` during branch cleanup

Owners: `Specs/UI/UI_Dashboard.md`, `Specs/Plugins/Plugins_AVControl.md`,
`Specs/Core/Core_DxUiIntegration.md`.

The source branch at `2882305` used API revision 2 and obsolete test filenames. Its useful
RedXe behavior is ported to the current revision-3 pin without taking its dependency downgrade.

- [x] Filter canceled pointer messages and capture loss before coordinate fallback; also clear
  the matching dock press so a canceled contact cannot leave stale reveal/settle state.
- [x] Preserve an output or microphone slider captured before its first preview when only
  the other endpoint changes. Keep cancellation when the captured endpoint changes.
- [x] Keep mute, slider and Profile targets at least 48 DIP at 160×180; hide a cramped label
  while retaining its accessible value.
- [x] Port policy and AV regression tests into current native test filenames.
- [x] Reconcile domain contracts and user-guide text with the current library implementation.
- [x] Run Debug and Release tests, skills validation and Release ARM64 build.

Validation on 2026-10-10: `test.ps1 -Full -Configuration Debug -Platform x64` passed all 16 scopes,
including independent BuildProcess tooling. `test.ps1 -Full -Configuration Release -Platform x64
-SkipTooling` passed all 15 native/runtime scopes; the unchanged, profile-independent tooling was
already passed by the Debug invocation. Both include production host integration, WARP, application
screenshot capture and isolated fatal/stack-overflow diagnostics. AV passed 9,672 synthetic checks
in Release. The minimum 160×180 and 320×180 native test captures were inspected.

`build.ps1 -Configuration Release -Platform ARM64` passed. All three builds reported zero warnings
and errors, and all ten repository skills validated. Debug tested `bc49fa4`; Release and ARM64 used
`e3733df`, whose additional changes are merge bookkeeping and historical measurement files. The
subsequent closeout changes only plan documentation. Build logs are under `.build/logs/`:
`redxe-build-20261010_100839_466-pid42912-45f4966c.log`,
`redxe-build-20261010_101418_153-pid57364-49d1346c.log` and
`redxe-build-20261010_101824_081-pid59228-39e1d010.log`.

ARM64 was cross-built, not executed. Physical touch and presented latency remain unverified and
stay with the existing [AV release gates](../WIP/RFC_Plugins_AVControl.md). This recovery closes
the RedXe port; it does not close AV production qualification or adopt a new upstream appearance.

The September branch also proposed a 24 DIP gray slider surround with accent diameters
14/20/16 DIP for rest/hover/press, model acknowledgement snapping an unchanged value,
and rejecting non-finite numeric configuration. Current `271bd54` retains the 20 DIP surround
and 6/16/12 DIP accent. Adoption of the larger appearance belongs to a separately validated
DxUi update; no older dependency pin or historical screenshot establishes current qualification.
The proposal is retained with the AV continuation gates, including physical touch, presented latency,
and accessibility checks. No new worker, timer or render allocation is introduced by this port.

The September validation record remains historical in
[`SliderTouchReview_2026-09-09.md`](SliderTouchReview_2026-09-09.md).
