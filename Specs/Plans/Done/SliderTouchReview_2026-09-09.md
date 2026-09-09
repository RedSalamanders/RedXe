# Slider touch and appearance review

Status: COMPLETE

Owners: `Specs/UI/UI_Dashboard.md`, `Specs/Plugins/Plugins_AVControl.md`,
`Specs/Core/Core_DxUiIntegration.md`, `Specs/Core/Core_PerformanceAndResources.md`.

The user requested a review of RedXe and DxUi and implementation of slider touch/visual fixes.
Unrelated review findings are recorded under `.build/review-2026-09-09` without expanding this implementation.

- [x] Fix cancellation before pointer-up dispatch and on capture loss; preserve normal release and other contacts.
- [x] Validate the production cancellation policy and AV slider gestures with synthetic tests.
- [x] Adopt the locally validated DxUi commit with a visible accent thumb and reliable model acknowledgement.
- [x] Fix minimum-tile overlap while retaining every mute, slider and Profile target at least 48 DIP wide.
- [x] Update normative contracts and the AV user guide; inspect generated native AV captures.
- [x] Run Debug/Release x64 tests, Release ARM64 build and skill validation.
- [x] Record physical-touch acceptance honestly; retain the existing AV hardware release gate.
- [x] Move this plan to Done when implementation and available validation are complete.

No timer, worker, surface or per-frame allocation is added. DxUi keeps the existing drawing operations.
The dependency commit remains local until the user authorizes publication.

Validation: full Debug/Release `test.ps1` passed, including production host/plugin integration, AV native controls,
settings, WARP and isolated crash/stack-overflow capture. Release ARM64 built with zero warnings, as did both x64
configurations. Formatting and skill validation passed. Native dark/light/high-contrast, portrait and minimum
captures were inspected; the AV guide uses the new screenshot. Physical touch and displayed latency remain unverified
and are still owned by the existing AV release gate.

RedXe pins DxUi implementation commit `3f50bbcc531e3057571a29d2718d2147ac6bad25`. The library passed all 18 x64 suites
in both configurations (8/9 desktop-dependent Menu skips), both ARM64 cross-builds and relocated consumer checks.
Its follow-up `2b4f7bf` retains measurements and closes its plan without changing compiled source. Alternating
Debug/Release comparisons showed no consistent candidate slowdown on this variable desktop. The user explicitly
chose to retain the appearance with the observed approximately 0.5 MiB Release clean-private-memory caveat; DxUi's
performance contract records that narrow tradeoff. Surface and C++ allocation counts did not grow.
