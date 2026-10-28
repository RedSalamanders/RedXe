# Skill packages and guidance validation

Status: normative repository tooling contract
Last reviewed: 2026-10-28

## Ownership and verdicts

`AGENTS.md` owns repository guidance. Skills under `.agents/skills/` supply task-specific procedure and route to
current domain contracts; they do not maintain another schema, plugin layout specification or delivery history.
Current values belong in the owned catalog/project/schema/lock artifacts. Historical plans cannot override them.

`validate-skills.ps1` is the public metadata/package validation entrypoint. It runs package/input admission before
opening metadata through the grammar validator, which rejects malformed headers, folder/name mismatches and empty
instructions. `Build/SkillPackageIntegrity.psm1` supplies read-only package and local-reference validation. It inspects
every Markdown resource in the skill packages and
the canonical root/CLAUDE/Copilot guidance routes, rejects missing local targets or references outside this checkout,
and rejects empty supplied UI metadata. UI metadata remains optional. External links, anchor existence, inline
command signatures, C++ compilation and model decisions are outside this package verdict.

Inline and reference-style Markdown destinations support balanced/escaped parentheses and angle-delimited paths.
Fenced examples and matching inline code spans do not create references; protocol-relative URLs remain external.
The bounded parser extracts destinations without evaluating Markdown or examples. Package traversal and input/
reference admission inspect ancestry before reading and MUST NOT follow reparse points. Referenced
supporting Markdown is checked as part of the package. Unicode and percent-encoded local filenames are supported.
No validator fetches, builds, edits product files or takes desktop focus. The focused package regression script
creates/removes only its own unique directory below repository `.build/skill-package-fixtures`.

## Verification

Run `Tests/BuildProcessTests/SkillPackageIntegrity.Tests.ps1` for the package fixtures. They cover valid references,
missing/foreign targets, supporting-resource closure, canonical guidance, optional UI packaging, Markdown code spans,
balanced filenames and reparse inputs. A host without symbolic-link creation reports that control skipped explicitly;
it does not add the skipped control to its passing count.
Metadata regression coverage remains owned by the repository's skill grammar tests.
The explicit independent-tooling runner includes the package fixture once. The exact module and fixture paths
`Build/SkillPackageIntegrity.psm1` and `Tests/BuildProcessTests/SkillPackageIntegrity.Tests.ps1` select only
`BuildProcess` during affected iteration. Shared `Build/ScopedTesting.psm1`, unknown Build inputs and the broader
build/runtime rules retain conservative full selection; the isolated exception is not a prefix-wide exclusion.

For a current corpus check in a fresh PowerShell process:

```powershell
Import-Module ./Build/SkillPackageIntegrity.psm1
$result = Get-SkillPackageIntegrity -Root $PWD
if ($result.Problems.Count) { throw ($result.Problems -join "`n") }
```

Report metadata and package verdicts separately from realistic decision evaluation. A structurally valid skill can
still make an incorrect recommendation. Category decisions for ordinary, opt-in, Debug-only and service plugins
must agree with `RedXe/BundledPlugins.h`, the owning [settings contract](../Core/Core_Settings.md#selection-storage-and-deployed-defaults)
and the existing Settings template tests. Preserve valid templates when correcting stale guidance.
