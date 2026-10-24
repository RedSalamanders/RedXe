# Changelog

Entries are generated from [Changes](Changes/README.md). Existing release notes remain in their original locations.

## Unreleased

- Complete local MSVC 14.52 Release optimization qualification in all six build profiles and all three Full x64 test profiles, with reviewed size/performance evidence and separate native ARM64 and hosted provisioning gates.
- Record the agreed local x64 test and ARM64 cross-build scope for MSVC 14.52 qualification; retain native ARM64 execution and hosted CI provisioning as separate handoff gates.
- Use a 64-bit compiler host for ARM64 targets under MSVC 14.52 and forward that selection to pinned DxUi, avoiding the observed 32-bit SystemData compiler-heap failures while preserving x64 runtime flags.
- Prepare and verify the required MSVC 14.52 toolchain in active Windows CI, with read-only local checks and refusal controls; hosted installation and native ARM64 runtime results remain separate qualification gates.
- Require MSVC 14.52.x and enable Release function-level/whole-program compilation and unused-function elimination with full LTCG; forward exact tools and WPO into the adopted DxUi archive with a separate policy fingerprint. Validation and measured before/after evidence remain tracked in the active build-policy plan.
- Verify retained run inventories using fresh pinned-handle metadata and stable path/stream ordering; preserve before/after move diagnostics and exact safety checks, with stale-metadata and reversed-enumeration regressions.
- Preserve unique completed, failed and interrupted test output in verified per-run evidence without modifying shared read-only files or weakening qualification checks.
- Require explicit user authorization for each exact project-specific `.Perf` root, keep other generated output inside approved workspaces, and preserve evidence and live references during artifact cleanup.
- Resolve explicit relative Changes roots against the current PowerShell location; regression coverage verifies validation, preview and folding leave a different process-directory checkout untouched.
- Use the configured PATH Python for skill/tooling validation, keeping its installed dependencies instead of selecting a different Windows launcher installation; retain launcher fallback and regression coverage.
- Keep new entries under Unreleased when a released heading uses a tab separator, and terminate an owned timed-out tooling fixture child tree before cleanup. Add independent released-heading and process-timeout regressions, and preserve the concurrent top-level drive-root guidance layout.
- Standardize the Changes workflow across DxUi, RedSalamander, RedXe, RedPrism and RedTilde with shared validation, safe folding and behavioral fixtures. Fold before PrePush/final handoff and immediately after integration, even for one fragment; retain native and product qualification as separate gates.
