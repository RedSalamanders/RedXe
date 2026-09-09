# Slider and touch branch recovery

Status: ACTIVE; recovery from `codex/slider-touch-review` during branch cleanup

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
- [ ] Run Debug and Release tests, skills validation and Release ARM64 build.

The September branch also proposed a 24 DIP gray slider surround with accent diameters
14/20/16 DIP for rest/hover/press, model acknowledgement snapping an unchanged value,
and rejecting non-finite numeric configuration. Current `271bd54` retains the 20 DIP surround
and 6/16/12 DIP accent. Adoption of the larger appearance belongs to a separately validated
DxUi update; no older dependency pin or historical screenshot establishes current qualification.
Retain this proposal with the AV continuation gates, including physical touch, presented latency,
and accessibility checks. No new worker, timer or render allocation is introduced by this port.

The September validation record remains historical in
[`SliderTouchReview_2026-09-09.md`](../Done/SliderTouchReview_2026-09-09.md).
