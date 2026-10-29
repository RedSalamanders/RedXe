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
- [x] Enforce and negatively test the common compiler/debug/linker policy in every supported native profile and pinned archive.
- [x] Unify green zero-warning build results, diagnostic/log links and automatic terminal executable hyperlinks where supported, preserving plain text and existing build/run semantics.
- [x] Shorten source-commit and output-fingerprint folders to 16 hex characters with full identity validation and collision refusal; retain existing data without cleanup.
- [x] Update source discovery, direct helper builds, provenance, test fixtures and dependency identities together.
- [x] Publish zero-warning guidance and normative build/path/terminal contracts, with meaningful tooling tests.
- [x] Execute affected tests, evaluated profiles, required native builds and applicable local x64 runtime qualification; retain every failed result and distinguish ARM64 cross-builds from runtime tests.
- [x] Freeze matching Release artifacts, report before/after image sizes and collect the prespecified quiet paired performance measurements; retain inconclusive outcomes.
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


## Local qualification checkpoint, 2026-10-29

The current five-repository audit passes 504 native profiles and 73,502 evaluated setting groups. All four consumers
pass their 24 required pinned-archive profiles; the additional Tilde lease profiles and negative controls pass.
Exact source identities, raw metadata and field differences are retained under the task's final-audit-20261029
evidence. Ordinary Debug and ASan formats remain separate from the recorded RedSalamander Full Embedded recovery mode.

The exact 616104 API4 SDK passes six native builds, 57 ordinary suite executions and three focus profiles with restoration. Product 47fb restores both architectures, passes six native builds and the complete source-restore suite. Product source 47fb0491 passes all three Full profiles, each with all 16 scopes; log identities are indexed in Xe-47fb-Full-three-verified1.json. Quiet measurements and final closeout remain pending. A controlled original/candidate Git comparison proves source-conflict preservation.


## Merge handoff, 2026-10-29

The developer explicitly requested merging all five repositories after the pending gates were identified.
This is an integration decision with the following recorded qualification limits; it does not change any test
verdict or establish complete production qualification. Focus-taking tests remain paused by the developer.

The prespecified quiet study completed once, without a selected repeat. Its native images and receipts remain
bound to the earlier source recorded in the evidence; these final handoff edits affect documentation only.
Baseline and candidate use the same discovered MSVC 14.52 toolchain; the comparison covers the complete change,
rather than isolating a compiler-version or individual-flag effect. Nonsignificance is not proof of equivalence.

| Scenario / metric | Baseline median | Candidate median | Paired change | Finding |
| --- | ---: | ---: | ---: | --- |
| Default / cpuUsPerCollection | 58349.6 | 58227.55 | -0.5431% | inconclusive - unstable controls |
| Default / wallUsPerCollection | 61201.8 | 59041 | -3.5493% | inconclusive - unstable controls |

Native samples, collection plan, machine-activity observations, preserved receipts and statistical analysis:
`
E:\RedXe.Perf\build-policy-evidence-20261028\studies\RedXe\20261029T192248-e5f0199a31c140b5add43f0c920c2122
`.

All candidate CI checks passed at native source `47fb0491139f2670ede8f759b8ee305b8e85bf22`. Local six-profile builds and three Full runtime profiles passed. The remaining performance uncertainty above is retained.
