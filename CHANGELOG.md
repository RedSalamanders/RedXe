# Changelog

Entries are generated from [Changes](Changes/README.md). Existing release notes remain in their original locations.

## Unreleased

- Apply the forecast timestamp's daylight-saving rules when formatting Weather times and local-day groups. Preserve the failed winter/summer fixture and add exact civil-hour/day-label round trips for all twelve months.
- Integrate the standalone RedMonitor pinned header client with MSVC 14.52 Release optimization and current main, preserving existing tests, source-bound qualification evidence and separate native/manual/release gates.
- Complete local MSVC 14.52 Release optimization qualification in all six build profiles and all three Full x64 test profiles, with reviewed size/performance evidence and separate native ARM64 and hosted provisioning gates.
- Record the agreed local x64 test and ARM64 cross-build scope for MSVC 14.52 qualification; retain native ARM64 execution and hosted CI provisioning as separate handoff gates.
- Use a 64-bit compiler host for ARM64 targets under MSVC 14.52 and forward that selection to pinned DxUi, avoiding the observed 32-bit SystemData compiler-heap failures while preserving x64 runtime flags.
- Prepare and verify the required MSVC 14.52 toolchain in active Windows CI, with read-only local checks and refusal controls; hosted installation and native ARM64 runtime results remain separate qualification gates.
- Require MSVC 14.52.x and enable Release function-level/whole-program compilation and unused-function elimination with full LTCG; forward exact tools and WPO into the adopted DxUi archive with a separate policy fingerprint. Validation and measured before/after evidence remain tracked in the active build-policy plan.

- Point RedMonitor contract and adoption-plan links to its merged main branch so retiring the delivery branch preserves documentation navigation.
- Record successful hosted tooling and all six native Full profiles for the RedMonitor/compiler-host integration, with independently verified runtime archives and ARM64 provenance; retain the earlier idle failure and separate physical, performance and release gates.
- Start the construction-lane idle measurement after finite completion notifications quiesce. Add a deterministic late-notification witness and separate CPU/wait diagnostics while keeping the existing 250-ms sample and 1-ms CPU budget. Leave the production event-blocked queue unchanged and preserve the earlier native ARM64 Release failure without assigning an unproved cause.
- Match the native compiler host during DxUi restore and RedXe compilation on ARM64 as well as x64, including emulated PowerShell. Preserve explicit overrides and restore the caller environment. Validate native/emulated host and override combinations through real MSBuild property evaluation; retain the earlier hosted ARM64 mismatch as failed evidence.
- Share only the pinned RedMonitor client header and MIT notice, verify them offline during builds, and discover the separately installed production Release helper for explicit launch or optional bare Debug startup; keep normalized bounded diagnostics and application-only packages.
- Add the exact pinned RedMonitor producer SDK, bounded application-labelled diagnostics and explicit optional adjacent viewer command; preserve existing logs, crash handling and application ownership, with consumer qualification tracked separately.
- Verify retained run inventories using fresh pinned-handle metadata and stable path/stream ordering; preserve before/after move diagnostics and exact safety checks, with stale-metadata and reversed-enumeration regressions.
- Preserve unique completed, failed and interrupted test output in verified per-run evidence without modifying shared read-only files or weakening qualification checks.
- Require explicit user authorization for each exact project-specific `.Perf` root, keep other generated output inside approved workspaces, and preserve evidence and live references during artifact cleanup.
- Resolve explicit relative Changes roots against the current PowerShell location; regression coverage verifies validation, preview and folding leave a different process-directory checkout untouched.
- Use the configured PATH Python for skill/tooling validation, keeping its installed dependencies instead of selecting a different Windows launcher installation; retain launcher fallback and regression coverage.
- Keep new entries under Unreleased when a released heading uses a tab separator, and terminate an owned timed-out tooling fixture child tree before cleanup. Add independent released-heading and process-timeout regressions, and preserve the concurrent top-level drive-root guidance layout.
- Standardize the Changes workflow across DxUi, RedSalamander, RedXe, RedPrism and RedTilde with shared validation, safe folding and behavioral fixtures. Fold before PrePush/final handoff and immediately after integration, even for one fragment; retain native and product qualification as separate gates.
