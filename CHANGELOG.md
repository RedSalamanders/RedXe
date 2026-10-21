# Changelog

Entries are generated from [Changes](Changes/README.md). Existing release notes remain in their original locations.

## Unreleased

- Resolve explicit relative Changes roots against the current PowerShell location; regression coverage verifies validation, preview and folding leave a different process-directory checkout untouched.
- Use the configured PATH Python for skill/tooling validation, keeping its installed dependencies instead of selecting a different Windows launcher installation; retain launcher fallback and regression coverage.
- Keep new entries under Unreleased when a released heading uses a tab separator, and terminate an owned timed-out tooling fixture child tree before cleanup. Add independent released-heading and process-timeout regressions, and preserve the concurrent top-level drive-root guidance layout.
- Standardize the Changes workflow across DxUi, RedSalamander, RedXe, RedPrism and RedTilde with shared validation, safe folding and behavioral fixtures. Fold before PrePush/final handoff and immediately after integration, even for one fragment; retain native and product qualification as separate gates.
