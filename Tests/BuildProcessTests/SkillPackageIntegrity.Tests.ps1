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
function Write-FixtureBytes([string] $Relative, [byte[]] $Bytes) {
    $path = Join-Path $fixture $Relative
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    [IO.File]::WriteAllBytes($path, $Bytes)
}
function Get-DefaultPreferenceIntegrity([string] $Root) {
    # A module function sees the global error preference, not this script's. Every check runs under PowerShell's default
    # one, as the corpus recipe in Build_SkillPackages.md does, so a read failure cannot hide behind Stop.
    $previous = $global:ErrorActionPreference
    $global:ErrorActionPreference = 'Continue'
    try { Get-SkillPackageIntegrity $Root } finally { $global:ErrorActionPreference = $previous }
}
function Expect-Integrity([string] $Name, [string] $ExpectedProblem = '') {
    $result = Get-DefaultPreferenceIntegrity $fixture
    if ($ExpectedProblem) {
        if (-not @($result.Problems | Where-Object { $_ -like "*$ExpectedProblem*" }).Count) { throw "$Name did not reject the intended defect: $($result.Problems -join '; ')" }
    } elseif ($result.Problems.Count) { throw "$Name unexpectedly failed: $($result.Problems -join '; ')" }
    $script:checks++
    return $result
}
try {
    # The one masking implementation both validators share keeps every line's number and length, so reported lines match.
    Import-Module (Join-Path $repository 'Build/SkillText.psm1') -Force
    $tick = '`'
    $sample = "# Title ${tick}x`n`n- item with ${tick}code that spans`n  two lines${tick} and ${tick}more${tick} [a](b.md)`n`n${tick}${tick}${tick}text`n[hidden](c.md)`n${tick}${tick}${tick}`nafter [d](e.md)`r`nend"
    $prose = Get-MarkdownProse $sample
    $source = $sample.Replace("`r`n", "`n") -split "`n"
    $masked = $prose.Text -split "`n"
    if ($masked.Count -ne $source.Count -or @(0..($source.Count - 1) | Where-Object { $masked[$_].Length -ne $source[$_].Length }).Count) { throw 'Masked prose changed the line structure' }
    if ($prose.Text.Contains('hidden') -or $prose.Text.Contains('spans') -or $prose.Text.Contains('more') -or -not $prose.Text.Contains('[a](b.md)') -or -not $prose.Text.Contains('[d](e.md)') -or $prose.Unclosed.Count) { throw "Masked prose hid or kept the wrong text: $($prose.Text)" }
    $unclosed = Get-MarkdownProse "kept [a](b.md)`n${tick}${tick}${tick}text`nstill [c](d.md)"
    if ($unclosed.Unclosed.Count -ne 1 -or $unclosed.Unclosed[0].Line -ne 2 -or $unclosed.Unclosed[0].Token -cne '```' -or -not $unclosed.Text.Contains('[c](d.md)')) { throw 'An unclosed fence was not reported once with the text after it kept' }
    $checks += 2
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
    # A footnote definition is text, not a link reference definition; a link inside one is still checked.
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + "Claim[^1].`n`n[^1]: See the note, not a path.`n")
    $null = Expect-Integrity 'footnote definition is not a reference'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + "Claim[^note].`n`n   [^note]: Indented note.`n")
    $null = Expect-Integrity 'indented footnote definition is not a reference'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + "Claim[^1].`n`n[^1]: See [missing](missing.md).`n")
    $null = Expect-Integrity 'a link inside a footnote is still checked' 'missing local reference: missing.md'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[note]: missing.md')
    $null = Expect-Integrity 'a reference definition that is not a footnote is still checked' 'missing local reference: missing.md'
    # A fragment and then a query end a local path (GitHub's ?plain=1 view); a query-only link names the same document.
    foreach ($suffix in @('?plain=1', '?plain=1#scope', '#scope?x', '?a=b&c=d')) {
        Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[Contract](../../../Specs/contract%20with%20spaces.md' + $suffix + ')')
        $result = Expect-Integrity "the suffix $suffix is not part of the path"
        if ($result.References -ne 1) { throw "A target with the suffix $suffix was not counted as one reference" }
    }
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[Missing](references/missing.md?plain=1#scope)')
    $null = Expect-Integrity 'a query suffix does not hide a missing target' 'missing local reference: references/missing.md'
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[Same page](?tab=readme) [Section](#scope)')
    $result = Expect-Integrity 'a query-only link names the same document'
    if ($result.References -ne 0) { throw 'A query-only link was counted as a local reference' }
    # GitHub is case-sensitive, so a link that only a case-insensitive host resolves is broken.
    if ([IO.File]::Exists((Join-Path $fixture 'agents.md'))) {
        foreach ($case in @(
            @{Name='file name'; Link='../../../Specs/Contract%20With%20Spaces.md'; Requested='Specs/Contract With Spaces.md'; OnDisk='Specs/contract with spaces.md'},
            @{Name='directory'; Link='../../../specs/contract%20with%20spaces.md'; Requested='specs/contract with spaces.md'; OnDisk='Specs/contract with spaces.md'},
            @{Name='directory link'; Link='../../../specs/'; Requested='specs/'; OnDisk='Specs'},
            @{Name='root file'; Link='../../../agents.md'; Requested='agents.md'; OnDisk='AGENTS.md'}
        )) {
            Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[Link](' + $case.Link + ')')
            $null = Expect-Integrity "a $($case.Name) that differs only in case is rejected" "local reference differs in case from the file on disk: $($case.Requested) (on disk: $($case.OnDisk))"
        }
        Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[Directory](../../../Specs/) [File](../../../Specs/contract%20with%20spaces.md) [Root](../../../AGENTS.md)')
        $null = Expect-Integrity 'links that match the on-disk case are accepted'
    } else {
        Write-Host 'SKIP letter-case fixtures: this host has a case-sensitive file system, where a wrong-case link is already missing.'
    }
    # A code span pairs only inside one block; a stray backtick cannot hide the links after it.
    $tick = '`'
    foreach ($case in @(
        @{Name='list items'; Text="- uses $tick for quoting`n- see [x](missing.md) and ${tick}code$tick"; Line=6},
        @{Name='ordered list items'; Text="1. uses $tick`n2. see [x](missing.md) and ${tick}code$tick"; Line=6},
        @{Name='a heading'; Text="# Heading with a $tick stray`nsee [x](missing.md) and ${tick}code$tick"; Line=6},
        @{Name='a table row'; Text="| uses $tick |`n| [x](missing.md) ${tick}code$tick |"; Line=6},
        @{Name='a block quote edge'; Text="uses $tick`n> see [x](missing.md) and ${tick}code$tick"; Line=6},
        @{Name='a thematic break'; Text="uses $tick`n***`nsee [x](missing.md) and ${tick}code$tick"; Line=7}
    )) {
        Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + $case.Text)
        $null = Expect-Integrity "a stray backtick cannot pair across $($case.Name)" "SKILL.md:$($case.Line): missing local reference: missing.md"
    }
    foreach ($case in @(
        @{Name='a list item'; Text="- item with ${tick}code that`n  continues [x](missing.md)$tick still code`n- next"},
        @{Name='a block quote'; Text="> quote ${tick}a`n> b [x](missing.md)$tick c"},
        @{Name='a paragraph'; Text="plain ${tick}a`nb [x](missing.md)$tick c"},
        @{Name='a heading'; Text="# Heading ${tick}a [x](missing.md)$tick c"},
        @{Name='a table row'; Text="| ${tick}a [x](missing.md)$tick |"}
    )) {
        Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + $case.Text)
        $null = Expect-Integrity "a code span still hides a link inside $($case.Name)"
    }
    # An unclosed fence is a problem that names its file and line; the text after it is still checked.
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '```text' + "`nexample`n[Missing](missing.md)`n")
    $null = Expect-Integrity 'an unclosed fence is reported with its line' 'SKILL.md:5: unclosed code fence (opened with'
    $null = Expect-Integrity 'text after an unclosed fence is still checked' 'SKILL.md:7: missing local reference: missing.md'
    Write-Fixture '.agents/skills/example-skill/references/procedure.md' ("Intro.`n`n" + '~~~~' + "`n[Example](missing.md)")
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + 'Read.')
    $null = Expect-Integrity 'an unclosed fence in supporting Markdown names that file' 'procedure.md:3: unclosed code fence (opened with'
    Write-Fixture '.agents/skills/example-skill/references/procedure.md' ("Intro.`n`n" + '~~~~' + "`n[Example](missing.md)`n" + '~~~~')
    $null = Expect-Integrity 'the same fence, closed, hides its example'
    [IO.File]::Delete((Join-Path $skill 'references/procedure.md'))
    # Only UTF-8 text is read. A UTF-8 byte order mark breaks SKILL.md front matter, so only SKILL.md refuses one.
    $little = [Text.UnicodeEncoding]::new($false, $true)
    $plain = $prefix + 'Read.'
    foreach ($case in @(
        @{Name='SKILL.md with a UTF-8 byte order mark'; Relative='.agents/skills/example-skill/SKILL.md'; Bytes=[byte[]](@(0xEF,0xBB,0xBF) + $utf8.GetBytes($plain)); Expect='SKILL.md: expected UTF-8 without a byte order mark'; Valid=$plain},
        @{Name='SKILL.md in UTF-16'; Relative='.agents/skills/example-skill/SKILL.md'; Bytes=[byte[]]($little.GetPreamble() + $little.GetBytes($plain)); Expect='SKILL.md: expected UTF-8 but found a UTF-16 or UTF-32 byte order mark'; Valid=$plain},
        @{Name='SKILL.md in UTF-16 without a byte order mark'; Relative='.agents/skills/example-skill/SKILL.md'; Bytes=[Text.UnicodeEncoding]::new($false, $false).GetBytes($plain); Expect='SKILL.md: expected UTF-8 text but found NUL bytes'; Valid=$plain},
        @{Name='supporting Markdown in UTF-16'; Relative='.agents/skills/example-skill/references/procedure.md'; Bytes=[byte[]]($little.GetPreamble() + $little.GetBytes('# Procedure')); Expect='procedure.md: expected UTF-8 but found a UTF-16 or UTF-32 byte order mark'; Valid='# Procedure'},
        @{Name='guidance with invalid UTF-8'; Relative='CLAUDE.md'; Bytes=[byte[]](0x23, 0x20, 0xC3, 0x28); Expect='CLAUDE.md: expected UTF-8 but found invalid bytes'; Valid='# Guidance'}
    )) {
        Write-FixtureBytes $case.Relative $case.Bytes
        $null = Expect-Integrity "$($case.Name) is rejected and named" $case.Expect
        Write-Fixture $case.Relative $case.Valid
        $null = Expect-Integrity "$($case.Name), saved as UTF-8, is accepted"
    }
    foreach ($relative in @('.agents/skills/example-skill/references/procedure.md', 'CLAUDE.md')) { [IO.File]::Delete((Join-Path $fixture $relative)) }
    foreach ($relative in @('.agents/skills/example-skill/references/procedure.md', 'AGENTS.md')) {
        Write-FixtureBytes $relative ([byte[]](@(0xEF,0xBB,0xBF) + $utf8.GetBytes('# Guidance')))
        $null = Expect-Integrity "a UTF-8 byte order mark is harmless in $relative"
    }
    Write-Fixture 'AGENTS.md' '# Canonical fixture guidance'
    [IO.File]::Delete((Join-Path $skill 'references/procedure.md'))
    # A file that cannot be read is a problem that names it, and the text of the file before it is never reused.
    Write-Fixture '.agents/skills/example-skill/SKILL.md' ($prefix + '[Missing](missing.md)')
    Write-FixtureBytes 'AGENTS.md' ([byte[]](0xC3, 0x28))
    $result = Expect-Integrity 'an unreadable file is reported by name' 'AGENTS.md: expected UTF-8 but found invalid bytes'
    if (@($result.Problems | Where-Object { $_ -match '^AGENTS\.md:\d' }).Count) { throw 'An unreadable file was checked with the text of the file before it' }
    Write-Fixture 'AGENTS.md' '# Canonical fixture guidance'
    Write-Fixture '.agents/skills/example-skill/references/locked.md' '# Locked'
    $lease = [IO.File]::Open((Join-Path $skill 'references/locked.md'), [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try { $null = Expect-Integrity 'a file in use is reported by name' 'locked.md: cannot be read' } finally { $lease.Dispose() }
    [IO.File]::Delete((Join-Path $skill 'references/locked.md'))
    $lease = [IO.File]::Open((Join-Path $skill 'agents/openai.yaml'), [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try { $null = Expect-Integrity 'UI metadata in use is reported by name' 'Cannot read optional skill UI metadata' } finally { $lease.Dispose() }
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
    # A relative root is the caller's PowerShell location; Set-Location leaves the process directory alone.
    $process = [Environment]::CurrentDirectory
    Push-Location $scratchParent
    try {
        [Environment]::CurrentDirectory = $repository
        $result = Get-DefaultPreferenceIntegrity (Split-Path $fixture -Leaf)
        if ($result.Problems.Count -or $result.Skills -ne 1) { throw "A relative root was not resolved against the PowerShell location: $($result.Problems -join '; ')" }
        # Negative control: the process directory holds the package, the PowerShell location does not.
        [Environment]::CurrentDirectory = $fixture
        $result = Get-DefaultPreferenceIntegrity '.'
        if ($result.Skills -ne 0 -or -not @($result.Problems | Where-Object { $_ -like '*Missing .agents/skills*' }).Count) { throw 'A relative root followed the process directory' }
    } finally { [Environment]::CurrentDirectory = $process; Pop-Location }
    $checks += 2
    # Ancestry is checked before the traversal lists anything: a linked .agents/skills is refused, not enumerated.
    $linkedRoot = Join-Path $fixture 'linked-root'
    $junction = Join-Path $linkedRoot '.agents/skills'
    [void][IO.Directory]::CreateDirectory((Join-Path $linkedRoot '.agents'))
    Write-Fixture 'linked-root/AGENTS.md' '# Canonical fixture guidance'
    $junctionCreated = $false
    try {
        $null = New-Item -ItemType Junction -Path $junction -Target (Join-Path $fixture '.agents/skills') -ErrorAction Stop
        $junctionCreated = $true
    } catch [System.Management.Automation.RuntimeException] {
        Write-Host "SKIP linked-skills fixture: this host cannot create a junction ($($_.Exception.Message))."
    }
    if ($junctionCreated) {
        try {
            $linked = Get-DefaultPreferenceIntegrity $linkedRoot
            if ($linked.Skills -ne 0 -or -not @($linked.Problems | Where-Object { $_ -like 'Reparse package path:*' }).Count) { throw "A linked skills directory was traversed: $($linked.Problems -join '; ')" }
            $checks++
        } finally { [IO.Directory]::Delete($junction) }
    }
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
