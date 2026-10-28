# PowerShell tooling migration

Date: 2026-10-28. Status: complete. Priority: tooling dependency reduction.
Planned at: 0281d6e1485867cf294f2d78934c2d5465d1d5b9. Drift check: git diff 0281d6e1485867cf294f2d78934c2d5465d1d5b9 -- Build Tools Tests .github/workflows.

## Scope and ownership

RedXe owns its supported commands, CI callers, validation identity and developer contract.
The cross-project change removes Python from routine first-party skill validation and formatter installation.
Historical evidence, RedPrism manual Clipper generators, native product code, dependency pins and upstream
Ghostty/Meson Python requirements remain outside this migration. DxUi provides a reference, not an implicit dependency.

## Checklist

- [x] Implement the repository's bounded PowerShell tooling change with explicit prerequisite and format contracts.
- [x] Add behavioral regression coverage, including malformed input and dependency-free execution.
- [x] Update owning Build/Testing specs, guidance, CI/scope manifests and changelog.
- [x] Run focused tooling and governed affected checks; record actual results and limits.
- [x] Fold the changelog and move this completed plan to Done after all required work passes.

## Verification and done criteria

Run Test-Changes.ps1 -Explain, the focused tooling regression scripts and affected checks for the changed surface.
Run Tools/Test-Changelog.ps1, Tools/Fold-Changelog.ps1 and Tools/Fold-Changelog.ps1 -Check.
PowerShell syntax, failing fixtures, actual repository validation and caller/dependency closure must pass.
RedSalamander also validates its tooling inventory and specification inventory in the governed sandbox.
No product/native runtime qualification is inferred from tooling tests. No desktop focus is needed.

## Stop conditions

Preserve concurrent work. Stop on unexpected generated native changes, evidence mutation, required unapproved
sandbox initialization or desktop interaction. Record an unavailable gate without manufacturing a pass.

## Executed evidence and limits

Skill validator: 58 behavioral cases passed, including actual repository validation without Python discovery, multiline text and Unicode code-point limits. Scoped-testing regression: 30 passed. Test-Changes.ps1 -Scopes BuildProcess -Force passed with SELECTED_PASSED; repository NOT_EVALUATED. Full native/product qualification was not run.

The user authorized the implementation with go. Changes are local on ej/powershell-tooling. Concurrent skill-audit edits were preserved; this record claims only the bounded tooling migration, not their completion. No source generators, upstream qualification Python, product code or dependency lock was changed by this migration.
