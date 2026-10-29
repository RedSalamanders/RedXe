- Hardened the PowerShell skill validators. `SKILL.md` files with a UTF-8 byte order mark or in UTF-16/UTF-32 are rejected
  again (NUL bytes and invalid UTF-8 too, for every package Markdown file), and a file that cannot be read is a problem or
  failure that names it under any caller `$ErrorActionPreference` instead of passing on the previous file's text. An
  unclosed code fence is reported with its file and line while the text after it is still checked. Package links skip
  footnote definitions, drop a `?query` before the existence check, reject a path that differs from the file on disk only
  in letter case (GitHub is case-sensitive), and no longer let a stray backtick pair across list items, headings, table
  rows or block quotes and hide a link; a relative `-Root` resolves against the PowerShell location, not the process
  directory. Metadata and package validation now share one fence and code-span implementation (`Build/SkillText.psm1`),
  and the duplicated ancestry check is the one reparse helper, which still refuses a linked `.agents/skills` before listing
  it. The scope planner drops its hard-coded tooling path list for an `"exclusive": true` rule flag in
  `Tests/test-scopes.json`: an exact path replaces the broader rules, a sibling keeps them, and a wildcard or non-boolean flag
  is refused. `SkillValidation.Tests.ps1`, `SkillPackageIntegrity.Tests.ps1` and `ScopedTesting.Tests.ps1` pair each fix with
  an accepted control, and `validate-skills.ps1` passes on the real corpus.
