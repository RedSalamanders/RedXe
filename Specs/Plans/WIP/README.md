# Active work

Files in this directory are non-normative execution or decision records; current requirements stay in the owning
domain spec under `Specs/<Domain>/`. This index is routing only. Every direct plan in this directory is listed
exactly once, as `- **STATUS**: [Title](File.md) — purpose`. `ACTIVE` is executable work, `HOLD` paused work,
`DECISION` unresolved product intent, and `RETIRED` a bounded tombstone that names its retirement date, reason,
current owner, and a `git show` recovery command.

Release scope follows the owning contracts and the [delivery plan's standing limits](DeliveryRedTeam_2026-10-31.md#p3-standing-limits).
`Owner decision required` means this reconciliation establishes no release-scope deferral. A `HOLD` status,
integration merge, or omission from release notes does not close qualification. AV's real IME/touch/screen-reader
and matched resource acceptance remain [release requirements](../../Plugins/Plugins_AVControl.md).

- **ACTIVE**: [Delivery red-team 2026-10-31](DeliveryRedTeam_2026-10-31.md) — Next release: Blocks calling the tip qualified. P2-6 is done and the DxUi submenu hover-timer cancel moved to a DxUi main pin on 2 November (P0-4), which still needs its package. Package `c61861c`, then the engine AND-gate on `e32cf9f9`, production receipts, and the P1 items that can ship a wrong binary. P2 is the same closeout window. RedMonitor physical, paired-performance, and post-integration native ARM64 stay open on their own plan and are not a checkbox here.

- **ACTIVE**: [Delivery review 2026-10-31](DeliveryReview_2026-10-31.md) — Next release: Annex only. Evidence for the menu defect and the probe sentence. Do not execute it. Its package command for `8c882636` is superseded by the red-team package of `c61861c`.

- **ACTIVE**: [Review fixes for PRs #21 to #32](MergedReviewFixes_2026-10-06.md) — Next release: Owner live checks. Code batches from the review of PRs #21 to #32 are checked. Remaining: eight live checks (autohide, Explorer restart, tray, first install, Logicon sign-out and hardware, the next publication, hosted-Release confirmation) and `dxui-restore#2` (the pin still has no `-MSBuildPath`). The publication check follows `qualify.yml` and `publish.yml`. Register line numbers are from `25433ae`.

- **ACTIVE**: [Native build policy, terminal results and short DxUi paths](NativeBuildPolicyCoherence_2026-10-28.md) — Next release: Owner decision required. Merged with qualification limits. Paused focus-taking tests, the inconclusive paired study, and the closeout checkbox remain open.

- **ACTIVE**: [RedXe RedMonitor adoption](RedMonitor_Adoption_2026-10-23.md) — Next release: Owner decision required. Physical input, paired performance, release publication, and native ARM64 reopened on 27 October after the hosted pass at `a55901c`.

- **HOLD**: [Desk Clock private-memory peak investigation](ClockPrivateMemoryPeakInvestigation_2026-10-13.md) — Next release: Owner decision required. Owner-deferred five-minute Desk Clock private-growth investigation; qualification stays open. Ordinary peaks were 5.93 and 6.46 MiB. The 60-second CI soak is a different bar.

- **HOLD**: [AV Control: Direct3D audio and webcam profiles](RFC_Plugins_AVControl_2026-09-05.md) — Next release: Release gates open. Real IME/touch/screen-reader and matched resource acceptance remain release requirements. G1 audio and G2 camera retain their full-feature gates. The tile and its user page already exist.

- **HOLD**: [AV Control continuation checkpoint](AVControlContinuation_2026-09-05.md) — Next release: AV RFC owns the gates. Dated snapshot. Do not resume from its `Z:` paths or pin `1947a5b`. Open gates are the AV RFC checklist.

- **ACTIVE**: [Actions: normalized bindings, plugin-published namespaces, and the Zoom publisher](RFC_Plugins_Actions_2026-09-17.md) — Next release: Owner decision required. Phase 1 is normative. Remaining publishers are `screen`, `window`, `navigate`, and `clipboard`. `audio` waits on AV G1. Zoom SDK sections are history.

- **DECISION**: [Plugin-dashboard remaining closeout](PluginDashboardRemainingCloseout_2026-09-02.md) — Next release: Owner decision required. Optional System Data hosts, or an explicit drop. Push providers, settings migration beyond version 4, and WebView policy are undecided.

- **ACTIVE**: [Plugin boundary and host runtime hardening](PluginBoundaryHardening_2026-09-03.md) — Next release: Owner decision required. G2 (gesture state still in `Application.cpp`) and H3 (ProcessViewer and SystemData translation units). The September ARM64 and Python notes are obsolete.

- **ACTIVE**: [Host chrome without GDI: Direct3D edge bands and raise overlay, no chrome HWNDs, no redirection surface](HostChromeWithoutGdi_2026-09-07.md) — Next release: Owner decision required. Direct3D chrome is already normative. Remaining receipt: PresentMon `PresentMode` on Matrix Focus and Gallery, or the owner drops C8. That receipt is not a sentence in `UI_Dashboard.md`.

- **ACTIVE**: [Runtime memory, lag, and whole-process resource budget](RuntimeMemoryAndLatency_2026-09-07.md) — Next release: Owner decision required. Open gates include physical input and first pixel, manual adapter/DPI moves and single-LUID accounting, five-minute clock growth (the HOLD child), returned private-byte attribution, shader/cold-target follow-up, and the unexplained Release screenshot access violation. September line numbers in that file are not current locations.

- **ACTIVE**: [Explicit state machines and lifetime safety across RedXe](ExplicitStateMachines_2026-11-10.md) — Next release: Owner decision required. Plan only; implementation not started. Six slices replacing 27 implicit state machines and closing 24 verified defects (`RX-SM-01`..`RX-SM-24`) across host swipe/raise/dock/settings, `PluginHost`, bundled plugins (`DeskClock`, `StudioClock`, `SystemData`, `ProcessViewer`, `AVControl`, `Logicon`, `Weather`, `5H4D3R5`, `Launcher`, `GdiOrbit`, `MatrixRain`, `RotatingTriangle`), `RedXeLauncher`, and `Build/*.psm1` without breaking ABI or settings schemas.

On completion, merge durable requirements into the owning domain spec, move the plan to `Specs/Plans/Done/`, and remove
its row here.

[DxUi adoption is complete](../Done/DxUiAdoption_2026-09-09.md) for the accepted local scope.
Further ARM64 and ASan qualification is user-deferred and routed through RedSalamander's
`DxUi_DeferredPlatformQualification_2026-09-13.md`; AV release gates retain their owner above.
