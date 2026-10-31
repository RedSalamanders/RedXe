# Active plan index

Files in this directory are non-normative execution or decision records. Current requirements remain in the owning
domain spec under `Specs/<Domain>/`.

Add every direct WIP plan to this table exactly once. Use `ACTIVE` for executable work, `HOLD` for paused work,
`DECISION` when product intent is unresolved, and `RETIRED` for a bounded tombstone. The Purpose cell is the
remaining work. A plan stays here while its own exit line is open.

| Plan | Status | Next release | Purpose |
| --- | --- | --- | --- |
| [`DeliveryRedTeam_2026-10-31.md`](DeliveryRedTeam_2026-10-31.md) | `ACTIVE` | Blocks calling the tip qualified | Not started. Package `c61861c`, then the engine AND-gate on `e32cf9f9`, production receipts, the pinned DxUi submenu hover-timer cancel, and the P1 items that can ship a wrong binary. P2 is the same closeout window. RedMonitor physical, paired-performance, and post-integration native ARM64 stay open on their own plan and are not a checkbox here. |
| [`DeliveryReview_2026-10-31.md`](DeliveryReview_2026-10-31.md) | `ACTIVE` | No. Annex only | Evidence for the menu defect and the probe sentence. Do not execute it. Its package command for `8c882636` is superseded by the red-team package of `c61861c`. |
| [`MergedReviewFixes_2026-10-06.md`](MergedReviewFixes_2026-10-06.md) | `ACTIVE` | Owner live checks | Code batches from the review of PRs #21 to #32 are checked. Remaining: eight live checks (autohide, Explorer restart, tray, first install, Logicon sign-out and hardware, the next publication, hosted-Release confirmation) and `dxui-restore#2` (the pin still has no `-MSBuildPath`). The publication check follows `qualify.yml` and `publish.yml`. Register line numbers are from `25433ae`. |
| [`NativeBuildPolicyCoherence_2026-10-28.md`](NativeBuildPolicyCoherence_2026-10-28.md) | `ACTIVE` | No | Merged. Closes after the paused focus-taking tests and the recorded inconclusive paired study are accepted or repeated, and the closeout checkbox is done. |
| [`RedMonitor_Adoption_2026-10-23.md`](RedMonitor_Adoption_2026-10-23.md) | `ACTIVE` | No, unless the release notes claim it | Physical input, paired performance, release publication, and native ARM64 reopened on 27 October after the hosted pass at `a55901c`. |
| [`ClockPrivateMemoryPeakInvestigation_2026-10-13.md`](ClockPrivateMemoryPeakInvestigation_2026-10-13.md) | `HOLD` | No | Deferred five-minute Desk Clock private growth: ordinary peaks 5.93 and 6.46 MiB. The 60-second CI soak is a different bar. |
| [`RFC_Plugins_AVControl.md`](RFC_Plugins_AVControl.md) | `HOLD` | No. Full feature | Real IME and screen reader, matched text/UIA performance, G1 audio, G2 camera. The tile and its user page already exist. |
| [`AVControlContinuation_2026-09-05.md`](AVControlContinuation_2026-09-05.md) | `HOLD` | No | Dated snapshot. Do not resume from its `Z:` paths or pin `1947a5b`. Open gates are the AV RFC checklist. |
| [`RFC_Plugins_Actions.md`](RFC_Plugins_Actions.md) | `ACTIVE` | No. Full feature | Phase 1 is normative. Remaining publishers are `screen`, `window`, `navigate`, and `clipboard`. `audio` waits on AV G1. Zoom SDK sections are history. |
| [`PluginDashboardRemainingCloseout_2026-09-02.md`](PluginDashboardRemainingCloseout_2026-09-02.md) | `DECISION` | No | Optional System Data hosts, or an explicit drop. Push providers, settings migration beyond version 4, and WebView policy are undecided. |
| [`PluginBoundaryHardening_2026-09-03.md`](PluginBoundaryHardening_2026-09-03.md) | `ACTIVE` | No | G2 (gesture state still in `Application.cpp`) and H3 (ProcessViewer and SystemData translation units). The September ARM64 and Python notes are obsolete. |
| [`HostChromeWithoutGdi_2026-09-07.md`](HostChromeWithoutGdi_2026-09-07.md) | `ACTIVE` | No | Direct3D chrome is already normative. Remaining receipt: PresentMon `PresentMode` on Matrix Focus and Gallery, or the owner drops C8. That receipt is not a sentence in `UI_Dashboard.md`. |
| [`RuntimeMemoryAndLatency_2026-09-07.md`](RuntimeMemoryAndLatency_2026-09-07.md) | `ACTIVE` | No | Open gates are physical input and first pixel, five-minute clock growth (the HOLD child), returned private-byte attribution, and shader/cold-target follow-up. September line numbers in that file are not current locations. |

On completion, merge durable requirements into the owning domain spec, move the plan to `Specs/Plans/Done/`, and remove
its row here.

[DxUi adoption is complete](../Done/DxUiAdoption_2026-09-09.md) for the accepted local scope.
Further ARM64 and ASan qualification is user-deferred and routed through RedSalamander's
`DxUi_DeferredPlatformQualification_2026-09-13.md`; AV release gates retain their owner above.
