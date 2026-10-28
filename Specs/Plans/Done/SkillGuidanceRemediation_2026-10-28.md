# Skill and guidance remediation

Date: 2026-10-28. Source inspected: `0281d6e` plus current working-tree bytes.
Status: complete for the bounded guidance/package-tooling scope; independent root review remains separate.
Scope: audit findings A06, A08, A09 and A15 for this repository. No product code, dependency pins, templates or
native runtime behavior changes. The separate PowerShellTooling migration retains ownership of its metadata
parser, public wrapper, tooling runner, scope planner, CI and migration documentation.

## Checklist

- [x] Recheck the ordinary/opt-in/Debug-only/service categories against the catalog, templates and native test oracle.
- [x] Correct the two focused skills and reconcile their normative Settings/Plugin owners.
- [x] Repair local routes and stale matrix/validator claims; route mutable layout/settings detail to its owners.
- [x] Add read-only package/reference integrity and isolated negative fixtures.
- [x] Publish the durable package contract and a dated Changes fragment.
- [x] Integrate the companion checks before metadata reads and include its fixture once in the tooling runner.
- [x] Verify exact tooling-only input selection while shared/unknown Build policy retains full fallback.
- [x] Run focused combined checks, fold/check the changelog, and move this record to Done.

## Verification recorded

- `Tests/BuildProcessTests/SkillPackageIntegrity.Tests.ps1`: 22 behavioral fixtures pass, including absolute/file-URI
  local-reference boundaries, reference-style destinations, balanced/escaped filenames, inline-code pseudo-links,
  protocol-relative URLs and a same-fixture reparse entrypoint. No controls were skipped on this host.
- Initial actual-corpus companion check reproduced exactly five missing skill targets; after route cleanup:
  10 skills, 40 local references, zero problems at that point.
- Read-only catalog/declaration check: Debug 19 widget declarations/1 service; Release 18/1. Opt-in remains absent
  and Debug-only declaration is excluded from Release. Actual placement/runtime behavior remains covered by the
  unchanged native Settings template tests, not by this declaration-only probe.
- `Test-Changes.ps1 -Explain` runs without mutation. The concurrent planner/CI migration and unmapped companion
  originally widened native obligations. Exact companion module/fixture inputs now select only BuildProcess;
  shared `Build/ScopedTesting.psm1` and unknown Build inputs still select full coverage.
- Current public `validate-skills.ps1`: package admission passes before metadata reads; ten metadata packages pass.
  The combined metadata regression script reports 58 cases; scoped-planner regressions report 31 cases. Package
  fixtures report 22. No product/native execution is inferred from these independent tooling results.

## Completion boundary

PR review follow-up reproduced cross-paragraph backtick hiding, omitted continued reference destinations,
parenthesized titles, file-URI double decoding, root-reparse read ordering, quoted timestamp coercion and hidden
metadata omission. The focused fixes pass 30 package and 60 metadata controls, including positive title/continued
destination/literal filename cases and a linked-root check while UI metadata is exclusively locked. The earlier
22/58-control and x64 Release PrePush successes remain historical receipts for their prior source; the follow-up
candidate requires its own PrePush and CI acceptance. No full CommonMark conformance is claimed.
The second PR pass reproduced date-prefixed prose rejection and unnecessary Packaging selection for an isolated
migrated metadata fixture. Complete scalar timestamp matching and the exact fixture exception now pass 65 metadata
controls and the 31-case planner suite. The successful Release PrePush at `9940f90` remains bound to that earlier
source; final source qualification is recorded separately.
The next bounded review reproduced a line-leading triple-backtick inline span being misclassified as an unclosed
fence, hiding later missing links and TODO instructions. The same admission check reproduced overindented fence
markers hiding later prose; markers now use zero to three leading spaces. These fixes pass 36 package and 70
metadata controls, including valid tilde info and overindented-closer counterexamples. Earlier PrePush/CI results
keep their original identities.

`Specs/Build/Build_SkillPackages.md`, `Specs/Core/Core_Settings.md` and `Specs/Plugins/Plugins_API.md` own the durable
requirements. Metadata and local-reference fixtures do not prove model effectiveness. Root independent review and
any later product/platform qualification remain separate; this plan closes only the stated guidance/tooling scope.
