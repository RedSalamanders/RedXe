# Active plan index

Files in this directory are non-normative execution or decision records. Current requirements remain in the owning
domain spec under `Specs/<Domain>/`.

Add every direct WIP plan to this table exactly once. Use `ACTIVE` for executable work, `HOLD` for paused work,
`DECISION` when product intent is unresolved, and `RETIRED` for a bounded tombstone. The Purpose cell is the
remaining work. A plan stays here while its own exit line is open.

Release scope follows the owning contracts and the [delivery plan's standing limits](DeliveryRedTeam_2026-10-31.md#p3-standing-limits).
`Owner decision required` means this reconciliation establishes no release-scope deferral. A `HOLD` status,
integration merge, or omission from release notes does not close qualification. AV's real IME/touch/screen-reader
and matched resource acceptance remain [release requirements](../../Plugins/Plugins_AVControl.md).

| Plan | Status | Next release | Purpose |
| --- | --- | --- | --- |
| [`DeliveryRedTeam_2026-10-31.md`](DeliveryRedTeam_2026-10-31.md) | `ACTIVE` | Blocks calling the tip qualified | Not started. Package `c61861c`, then the engine AND-gate on `e32cf9f9`, production receipts, the pinned DxUi submenu hover-timer cancel, and the P1 items that can ship a wrong binary. P2 is the same closeout window. RedMonitor physical, paired-performance, and post-integration native ARM64 stay open on their own plan and are not a checkbox here. |
| [`DeliveryReview_2026-10-31.md`](DeliveryReview_2026-10-31.md) | `ACTIVE` | Annex only | Evidence for the menu defect and the probe sentence. Do not execute it. Its package command for `8c882636` is superseded by the red-team package of `c61861c`. |
| [`MergedReviewFixes_2026-10-06.md`](MergedReviewFixes_2026-10-06.md) | `ACTIVE` | Owner live checks | Code batches from the review of PRs #21 to #32 are checked. Remaining: eight live checks (autohide, Explorer restart, tray, first install, Logicon sign-out and hardware, the next publication, hosted-Release confirmation) and `dxui-restore#2` (the pin still has no `-MSBuildPath`). The publication check follows `qualify.yml` and `publish.yml`. Register line numbers are from `25433ae`. |
| [`NativeBuildPolicyCoherence_2026-10-28.md`](NativeBuildPolicyCoherence_2026-10-28.md) | `ACTIVE` | Owner decision required | Merged with qualification limits. Paused focus-taking tests, the inconclusive paired study, and the closeout checkbox remain open. |
| [`RedMonitor_Adoption_2026-10-23.md`](RedMonitor_Adoption_2026-10-23.md) | `ACTIVE` | Owner decision required | Physical input, paired performance, release publication, and native ARM64 reopened on 27 October after the hosted pass at `a55901c`. |
| [`ClockPrivateMemoryPeakInvestigation_2026-10-13.md`](ClockPrivateMemoryPeakInvestigation_2026-10-13.md) | `HOLD` | Owner decision required | Owner-deferred five-minute Desk Clock private-growth investigation; qualification stays open. Ordinary peaks were 5.93 and 6.46 MiB. The 60-second CI soak is a different bar. |
| [`RFC_Plugins_AVControl.md`](RFC_Plugins_AVControl.md) | `HOLD` | Release gates open | Real IME/touch/screen-reader and matched resource acceptance remain release requirements. G1 audio and G2 camera retain their full-feature gates. The tile and its user page already exist. |
| [`AVControlContinuation_2026-09-05.md`](AVControlContinuation_2026-09-05.md) | `HOLD` | AV RFC owns the gates | Dated snapshot. Do not resume from its `Z:` paths or pin `1947a5b`. Open gates are the AV RFC checklist. |
| [`RFC_Plugins_Actions.md`](RFC_Plugins_Actions.md) | `ACTIVE` | Owner decision required | Phase 1 is normative. Remaining publishers are `screen`, `window`, `navigate`, and `clipboard`. `audio` waits on AV G1. Zoom SDK sections are history. |
| [`PluginDashboardRemainingCloseout_2026-09-02.md`](PluginDashboardRemainingCloseout_2026-09-02.md) | `DECISION` | Owner decision required | Optional System Data hosts, or an explicit drop. Push providers, settings migration beyond version 4, and WebView policy are undecided. |
| [`PluginBoundaryHardening_2026-09-03.md`](PluginBoundaryHardening_2026-09-03.md) | `ACTIVE` | Owner decision required | G2 (gesture state still in `Application.cpp`) and H3 (ProcessViewer and SystemData translation units). The September ARM64 and Python notes are obsolete. |
| [`HostChromeWithoutGdi_2026-09-07.md`](HostChromeWithoutGdi_2026-09-07.md) | `ACTIVE` | Owner decision required | Direct3D chrome is already normative. Remaining receipt: PresentMon `PresentMode` on Matrix Focus and Gallery, or the owner drops C8. That receipt is not a sentence in `UI_Dashboard.md`. |
| [`RuntimeMemoryAndLatency_2026-09-07.md`](RuntimeMemoryAndLatency_2026-09-07.md) | `ACTIVE` | Owner decision required | Open gates include physical input and first pixel, manual adapter/DPI moves and single-LUID accounting, five-minute clock growth (the HOLD child), returned private-byte attribution, shader/cold-target follow-up, and the unexplained Release screenshot access violation. September line numbers in that file are not current locations. |

On completion, merge durable requirements into the owning domain spec, move the plan to `Specs/Plans/Done/`, and remove
its row here.

[DxUi adoption is complete](../Done/DxUiAdoption_2026-09-09.md) for the accepted local scope.
Further ARM64 and ASan qualification is user-deferred and routed through RedSalamander's
`DxUi_DeferredPlatformQualification_2026-09-13.md`; AV release gates retain their owner above.
