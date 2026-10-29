# Native build policy, terminal results and short DxUi paths

Status: ACTIVE
Date: 2026-10-28
Owner: build maintainers

The user's agreed policy is /Wall with /WX disabled and zero warnings accepted;
Debug uses EditAndContinue, ASan Debug uses ProgramDatabase. Release enables
explicit COMDAT folding (/OPT:ICF); Debug and ASan Debug disable it.
The owning build contract is [the toolchain contract](../../Build/Build_Process.md).
Five repositories implement the same policy without implicitly advancing DxUi pins.

## Checklist

- [x] Preserve the pre-change effective-profile audit and isolate changes from concurrent tooling work.
- [x] Restore RedTilde ARM64 through its normal entrypoint and pass the required policy/lease checks.
- [ ] Enforce and negatively test the common compiler/debug/linker policy in every supported native profile and pinned archive.
- [x] Unify green zero-warning build results, diagnostic/log links and opt-in terminal executable hyperlinks, preserving plain text and existing build/run semantics.
- [x] Shorten source-commit and output-fingerprint folders to 16 hex characters with full identity validation and collision refusal; retain existing data without cleanup.
- [x] Update source discovery, direct helper builds, provenance, test fixtures and dependency identities together.
- [x] Publish zero-warning guidance and normative build/path/terminal contracts, with meaningful tooling tests.
- [ ] Execute affected tests, evaluated profiles, required native builds and applicable runtime qualification; retain every failed result and distinguish cross-builds from runtime tests.
- [ ] Fold/check the changelog; validate formatting/specs/tooling and close this plan only after all required gates pass.

## Decisions and limits

Source pins and full SHA256 identities remain authoritative; folder prefixes are
locators. No source clone is reset for a prefix collision. Existing historical
output is retained in place. Build success is green only after successful
native exit, zero diagnostics and required attestation. Hyperlinks never launch
an application automatically and fall back to quoted launch commands.
The existing explicit RedSalamander Embedded compiler-debug recovery mode remains
an attested exception; ordinary Debug/ASan profiles use the shared defaults.

## Evidence

Baseline audit: E:/DxUi.Perf/flag-coherence-20261028-aea0f188.
Current task evidence: E:/DxUi.Perf/build-policy-evidence-20261028.
RedTilde ARM64 restore and required policy/lease logs are under that task's
RedTilde-ARM64-restore folder; both exited zero before implementation.
Candidate dependency: `616104f3b2b693a824a3a76b18b851055accf777` (API4), replacing `fa3de6c980f4d7d2700e44a82ad9e751cbebcdc3`.
The exact SDK source passes six zero-warning native builds, all57 ordinary suite executions and all three approved
focus profiles with zero capability skips and verified desktop restoration. The maintenance ref is published and
verified. This is library qualification; normal product restore, six product native builds and three Full x64
profiles remain required before product acceptance. No adapter/API revision changes are included.

Final path review found that the legacy automatic-repair path could replace a short source locator holding another
complete commit. The restore now checks that identity under its existing mutex before replacement and refuses the
conflict without deleting tracked or local contents. A real local Git fixture checks refusal and preservation;
ordinary incomplete/same-pin repair and legacy full-commit fixture behavior remain covered. Focused tooling and
the final product native/runtime matrix must include this repair alongside the qualified SDK adoption.
The first focused restore run passed the new preservation fixture, then correctly failed when its final real-pin
check found no SDK restored in this integration checkout. That failed receipt is retained; normal restore of the
new qualified pin precedes the complete retry.
