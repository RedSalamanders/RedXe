#Requires -Version 7.0
[CmdletBinding()] param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repository 'Build/SkillPackageIntegrity.psm1') -Force
$scratchParent = [IO.Path]::GetFullPath((Join-Path $repository '.build/skill-package-fixtures'))
$fixture = Join-Path $scratchParent ([guid]::NewGuid().ToString('N'))
$skill = Join-Path $fixture '.agents/skills/example-skill'
$utf8 = [Text.UTF8Encoding]::new($false)
$checks = 0
$ownedLinks = [Collections.Generic.List[string]]::new()
function Write-Fixture([string] $Relative, [string] $Text) {
    $path = Join-Path $fixture $Relative
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    [IO.File]::WriteAllText($path, $Text, $utf8)
}
function Expect-Integrity([string] $Name, [string] $ExpectedProblem = '') {
    $result = Get-SkillPackageIntegrity $fixture
    if ($ExpectedProblem) {
        if (-not @($result.Problems | Where-Object { $_ -like "*$ExpectedProblem*" }).Count) { throw "$Name did not reject the intended defect: $($result.Problems -join '; ')" }
    } elseif ($result.Problems.Count) { throw "$Name unexpectedly failed: $($result.Problems -join '; ')" }
    $script:checks++
    return $result
}
try {
    Write-Fixture 'AGENTS.md' '# Canonical fixture guidance'
    Write-Fixture 'Specs/contract with spaces.md' '# Owning contract'
    $prefix = "---`nname: example-skill`ndescription: Exercise package checks.`n---`n"
    $body = '[Contract](../../../Specs/contract%20with%20spaces.md#scope) [Web](https://example.invalid/) [Section](#local)'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + $body)
    $result = Expect-Integrity 'valid package without optional UI metadata'
    if ($result.Skills -ne 1 -or $result.References -ne 1) { throw 'Package/reference accounting was incorrect' }
    Write-Fixture '.agents/skills/example-skill/agents/openai.yaml' ''
    $null = Expect-Integrity 'empty supplied UI metadata' 'Empty optional skill UI metadata'
    Write-Fixture '.agents/skills/example-skill/agents/openai.yaml' 'interface: {}'
    $null = Expect-Integrity 'nonempty optional UI metadata'
    Write-Fixture 'Specs/contract(v1).md' '# Versioned contract'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[Balanced](../../../Specs/contract(v1).md)')
    $null = Expect-Integrity 'balanced parentheses in a destination'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[Escaped](../../../Specs/contract\(v1\).md)')
    $null = Expect-Integrity 'escaped parentheses in a destination'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[Missing](../../../Specs/missing(v1).md)')
    $null = Expect-Integrity 'missing balanced destination is still checked' 'missing(v1).md'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '`[example](missing.md)` and ' + $body)
    $null = Expect-Integrity 'inline-code pseudo-link ignored'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '``inline ` [example](missing.md)' + "`n" + 'still code`` and ' + $body)
    $null = Expect-Integrity 'matching multiline code span ignored'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '```literal```' + "`n`n[Missing](missing.md)")
    $null = Expect-Integrity 'line-leading triple backticks cannot hide later paragraphs' 'missing local reference'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '```[example](missing.md)```' + "`n`n" + $body)
    $null = Expect-Integrity 'line-leading triple-backtick code span remains inline code'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '~~~info`' + "`n[Example](missing.md)`n~~~`n" + $body)
    $null = Expect-Integrity 'tilde fence permits a backtick in its info string'
    foreach ($indent in @('    ', "`t")) {
        Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + $indent + '```' + "`n`n[Missing](missing.md)")
        $null = Expect-Integrity 'indented code does not open a fence over later prose' 'missing local reference'
    }
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '```text' + "`n    " + '```' + "`n[Example](missing.md)`n" + '```' + "`n" + $body)
    $null = Expect-Integrity 'overindented fence marker does not close a valid fence'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + 'first `' + "`n `t`n[Missing](missing.md)`n`n" + 'last `')
    $null = Expect-Integrity 'unmatched code delimiters cannot cross paragraphs' 'missing local reference'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[Read](missing.md (Owning contract))')
    $null = Expect-Integrity 'parenthesized title does not hide a missing target' 'missing local reference'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[Read](../../../Specs/contract(v1).md (Owning contract))')
    $null = Expect-Integrity 'valid parenthesized title'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + "[procedure]:`n  references/missing.md`n[Read][procedure]")
    $null = Expect-Integrity 'continued reference definition checks missing target' 'missing local reference'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + "[procedure]:`n  <../../../Specs/contract with spaces.md>`n[Read][procedure]")
    $null = Expect-Integrity 'valid continued reference definition'
    foreach ($name in @('guide#v1.md', 'literal%20.md')) {
        Write-Fixture "Specs/$name" '# Literal filename'
        Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[File](' + ([Uri](Join-Path $fixture "Specs/$name")).AbsoluteUri + ')')
        $null = Expect-Integrity "file URI preserves literal filename $name"
    }
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[Network](//example.invalid/reference)')
    $null = Expect-Integrity 'protocol-relative external URL is not a local target'
    Write-Fixture 'owned-skill.md' ($prefix + 'Read the owned fixture.')
    $skillPath = Join-Path $skill 'SKILL.md'
    [IO.File]::Delete($skillPath)
    $linkCreated = $false
    try {
        $null = New-Item -ItemType SymbolicLink -Path $skillPath -Target (Join-Path $fixture 'owned-skill.md') -ErrorAction Stop
        $ownedLinks.Add($skillPath); $linkCreated = $true
    } catch {
        if ($_.CategoryInfo.Category -ne 'PermissionDenied' -and $_.Exception -isnot [NotSupportedException]) { throw }
        Write-Host 'SKIP reparse-input fixture: this host cannot create a symbolic link.'
    }
    if ($linkCreated) {
        $null = Expect-Integrity 'reparse skill entrypoint is refused before reading' 'reparse'
        [IO.File]::Delete($skillPath)
    }
    $rootAlias = Join-Path $fixture 'root-alias'
    $rootLinkCreated = $false
    try {
        $null = New-Item -ItemType SymbolicLink -Path $rootAlias -Target $fixture -ErrorAction Stop
        $rootLinkCreated = $true
    } catch {
        if ($_.CategoryInfo.Category -ne 'PermissionDenied' -and $_.Exception -isnot [NotSupportedException]) { throw }
        Write-Host 'SKIP linked-root fixture: this host cannot create a symbolic link.'
    }
    if ($rootLinkCreated) {
        try {
            $lease = [IO.File]::Open((Join-Path $skill 'agents/openai.yaml'), [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
            try { $linked = Get-SkillPackageIntegrity $rootAlias } finally { $lease.Dispose() }
            if ($linked.Skills -ne 0 -or -not @($linked.Problems | Where-Object { $_ -like '*reparse*' }).Count) { throw 'Linked root was traversed before package admission' }
            $checks++
        } finally { [IO.Directory]::Delete($rootAlias) }
    }
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + $body)
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[Missing](references/missing.md)')
    $null = Expect-Integrity 'missing skill reference' 'missing local reference'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[contract]: references/missing.md')
    $null = Expect-Integrity 'missing reference-style destination' 'missing local reference'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[contract]: <../../../Specs/contract with spaces.md>' + "`n" + '[Named contract][contract]')
    $null = Expect-Integrity 'valid reference-style destination with spaces'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[Foreign](../../../../foreign.md)')
    $null = Expect-Integrity 'foreign checkout dependency' 'reference escapes this repository'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[Absolute](' + (Join-Path $scratchParent 'foreign.md').Replace('\', '/') + ')')
    $null = Expect-Integrity 'absolute local path outside fixture root' 'reference escapes this repository'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[File URI](' + ([Uri](Join-Path $scratchParent 'foreign.md')).AbsoluteUri + ')')
    $null = Expect-Integrity 'local file URI outside fixture root' 'reference escapes this repository'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '```text' + "`n[Example](missing.md)`n" + '```' + "`n" + $body)
    $null = Expect-Integrity 'fenced example is not a package reference'
    Write-Fixture '.agents/skills/example-skill/references/procedure.md' '[Missing](missing-contract.md)'
    $null = Expect-Integrity 'supporting reference closure' 'missing-contract.md'
    Write-Fixture '.agents/skills/example-skill/references/procedure.md' '[Contract](../../../../Specs/contract%20with%20spaces.md)'
    $null = Expect-Integrity 'supporting reference closure repaired'
    Write-Fixture 'AGENTS.md' '[Missing](missing-guidance.md)'
    $null = Expect-Integrity 'canonical guidance route' 'AGENTS.md:1: missing local reference'
    [IO.File]::Delete((Join-Path $fixture 'AGENTS.md'))
    $null = Expect-Integrity 'missing canonical guidance' 'Missing canonical AGENTS.md'
    Write-Fixture 'AGENTS.md' '# Canonical fixture guidance'
    [IO.File]::Delete((Join-Path $skill 'SKILL.md'))
    $null = Expect-Integrity 'missing skill package' 'No SKILL.md packages found'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    if (-not $resolved.StartsWith($scratchParent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Refused fixture cleanup outside its repository-local scratch parent' }
    foreach ($link in $ownedLinks) { if ([IO.File]::Exists($link) -and ([IO.File]::GetAttributes($link) -band [IO.FileAttributes]::ReparsePoint)) { [IO.File]::Delete($link) } }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
Write-Host "Skill package integrity: $checks behavioral fixtures passed; model decisions were not evaluated."
