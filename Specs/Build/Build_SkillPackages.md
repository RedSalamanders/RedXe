# Skill packages and guidance validation

Status: normative repository tooling contract
Last reviewed: 2026-10-29

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

Both validators read package Markdown through `Build/SkillText.psm1` as strict UTF-8. UTF-16 or UTF-32 text, NUL bytes
and invalid UTF-8 are refused for every file, and a UTF-8 byte order mark is refused for `SKILL.md`, whose front matter
must start at byte 0. A file that cannot be read (encoding, sharing, access) is a problem or failure that names it, never
a pass on the text of the file before it. This holds whatever `$ErrorActionPreference` the caller runs under: a module
function sees the global preference rather than its caller's, so each module sets `Stop` for itself. A relative `-Root`
is resolved against the caller's PowerShell location, which `Set-Location` changes and the process directory does not.

Inline and reference-style Markdown destinations support balanced/escaped parentheses, angle-delimited paths,
parenthesized or quoted titles and a reference destination continued on the next line. Fenced examples and
matching inline code spans within one block do not create references. A code span pairs only inside its block: a blank
line, list item, heading, setext underline, thematic break, table row or block-quote edge ends the search, so an
unmatched backtick cannot hide the links after it. Protocol-relative URLs remain external. Footnote definitions
(`[^1]: note`) are text, not reference definitions. A fragment and then a query (`?plain=1`) end a local path before it is
decoded and checked. GitHub is case-sensitive, so each component of an existing local target is compared ordinally with
its on-disk spelling, and a link that differs only in letter case is rejected.
Opening backtick fences require an info string without backticks; a line-leading triple-backtick inline span
therefore cannot hide later prose. Tilde-fence info strings may contain backticks.
Fence markers use zero to three leading spaces (optionally followed by the supported list marker); four-space
or tab-indented code cannot open or close a fence over surrounding prose. This remains a bounded package parser.
Metadata and package validation share one implementation of the fence and code-span grammar (`Get-MarkdownFence` and
`Get-MarkdownProse` in `Build/SkillText.psm1`). An opening fence that no marker closes is a problem that names its file
and line, and the text after it is still checked as prose instead of disappearing into the fence.
The bounded parser extracts destinations without evaluating Markdown or examples. Package traversal and input/
reference admission inspect ancestry, including the supplied root itself and `.agents/skills`, before reading or listing
and MUST NOT follow reparse points; one helper (`Get-PackageReparseProblem`) performs every such check. Referenced
supporting Markdown is checked as part of the package. Unicode and percent-encoded local filenames are supported.
File URI paths are decoded once by the URI parser; literal hash and percent characters in filenames are preserved.
Metadata enumeration includes hidden skill directories. No validator fetches, builds, edits product files or takes desktop focus. The focused package regression script
creates/removes only its own unique directory below repository `.build/skill-package-fixtures`.

## Verification

Run `Tests/BuildProcessTests/SkillPackageIntegrity.Tests.ps1` for the package fixtures. They cover valid references,
missing/foreign targets, supporting-resource closure, canonical guidance, optional UI packaging, Markdown code spans
and their block scope, unclosed fences, footnotes, query suffixes, letter case, encodings and read failures under the
default error preference, relative roots, balanced filenames, and reparse inputs including a linked `.agents/skills`.
Each rejected defect has an accepted control beside it. A host without symbolic-link or junction creation, or with a
case-sensitive file system, reports that control skipped explicitly; it does not add the skipped control to its passing
count. Metadata regression coverage (unclosed fences, encodings, unreadable skills and relative roots included) is owned
by `Tests/BuildProcessTests/SkillValidation.Tests.ps1`.
The explicit independent-tooling runner includes both fixtures once. The skill tooling inputs
`Build/SkillPackageIntegrity.psm1`, `Build/SkillValidation.psm1`, `Build/SkillText.psm1`,
`Tests/BuildProcessTests/SkillPackageIntegrity.Tests.ps1` and `Tests/BuildProcessTests/SkillValidation.Tests.ps1`
select only `BuildProcess` during affected iteration. Each is a rule of `Tests/test-scopes.json` carrying
`"exclusive": true`, the one mechanism for such an exception: an exclusive rule names one exact path (the manifest
reader refuses a wildcard or a non-boolean flag), and a match replaces every broader rule and the prose shortcut for
that path alone. The enclosing `Tests/BuildProcessTests/**` rule, which still selects `BuildProcess` and `Packaging`,
therefore keeps serving every sibling, and `Build/ScopedTesting.psm1` holds no path list of its own.
`Tests/BuildProcessTests/ScopedTesting.Tests.ps1` covers the flag: an exact match is exclusive, an exact rule without
the flag adds to the broader rules, a sibling or nested path still receives them, and an invalid flag is refused.
Shared `Build/ScopedTesting.psm1`, unknown Build inputs and the broader build/runtime rules retain conservative full
selection; the isolated exception is not a prefix-wide exclusion.

For a current corpus check in a fresh PowerShell process (the default error preference suffices):

```powershell
Import-Module ./Build/SkillPackageIntegrity.psm1
$result = Get-SkillPackageIntegrity -Root $PWD
if ($result.Problems.Count) { throw ($result.Problems -join "`n") }
```

Report metadata and package verdicts separately from realistic decision evaluation. A structurally valid skill can
still make an incorrect recommendation. Category decisions for ordinary, opt-in, Debug-only and service plugins
must agree with `RedXe/BundledPlugins.h`, the owning [settings contract](../Core/Core_Settings.md#selection-storage-and-deployed-defaults)
and the existing Settings template tests. Preserve valid templates when correcting stale guidance.
