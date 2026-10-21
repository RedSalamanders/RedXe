<#
.SYNOPSIS
Runs the shared Changes behavior fixtures without a native build or external test framework.
.DESCRIPTION
Creates uniquely owned fixture directories beneath an existing caller-owned scratch root.
Never reads or changes the application's changelog. Removes only its own direct fixture children.
.PARAMETER ScratchRoot
Existing test scratch parent supplied by the repository's test harness.
.OUTPUTS
Console case results; throws if any assertion fails. Fixture files are removed on completion.
.NOTES
Requires PowerShell 7. No network, native code, application processes or desktop input.
.EXAMPLE
./Tools/Test-ChangelogWorkflow.ps1 -ScratchRoot <existing-test-scratch-directory>
#>
[CmdletBinding()]
param([Parameter(Mandatory)][string] $ScratchRoot)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $repository 'Tools/Modules/Documentation/Changelog.psm1')
$fold = Join-Path $repository 'Tools/Fold-Changelog.ps1'
$validate = Join-Path $repository 'Tools/Test-Changelog.ps1'
$parent = [IO.Path]::GetFullPath($ScratchRoot).TrimEnd([IO.Path]::DirectorySeparatorChar)
if (-not [IO.Directory]::Exists($parent)) { throw "Scratch root must already exist: $parent" }
# A repository adapter supplies its existing scratch root. Honor the stricter marked-sandbox policy where installed.
$sandboxModule = Join-Path $repository 'Tools/Modules/Testing/TestSandbox.psm1'
if ([IO.File]::Exists($sandboxModule)) {
    Import-Module $sandboxModule
    $testRoot = Get-RSTestSandboxRoot -RepoRoot $repository
    [void](Assert-RSTestSandboxContainedPath -TestRoot $testRoot -Path $parent -RequireDescendant)
}
$results = [Collections.Generic.List[string]]::new()
$failures = [Collections.Generic.List[string]]::new()

function Assert([bool] $Condition, [string] $Message) { if (-not $Condition) { throw $Message } }
function Refuses([scriptblock] $Body) {
    $refused = $false
    try { & $Body | Out-Null } catch { $refused = $true }
    Assert $refused 'Expected the operation to refuse'
}
function Put([string] $Root, [string] $Name, [string] $Text) {
    $path = Join-Path $Root $Name
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    [IO.File]::WriteAllText($path, $Text, [Text.UTF8Encoding]::new($false))
}
function Snapshot([string] $Root) {
    return (@(Get-ChildItem -LiteralPath $Root -Recurse -File | Where-Object Name -ne '.changelog-fold.lock' | Sort-Object FullName | ForEach-Object {
        [IO.Path]::GetRelativePath($Root, $_.FullName) + ':' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }) -join "`n")
}
function Case([string] $Name, [scriptblock] $Body) {
    $root = Join-Path $parent ('changelog-' + [guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($root)
    try {
        Put $root 'Changes/README.md' "# Changelog fragments`n"
        Put $root 'CHANGELOG.md' "# Changelog`n`n## Unreleased`n`n- Existing.`n`n## 1.0`n`n- Released.`n"
        & $Body $root
        $results.Add($Name)
        Write-Host "PASS changelog: $Name"
    } catch { $failures.Add("${Name}: $($_.Exception.Message)"); Write-Host "FAIL changelog: $($failures[-1])" }
    finally {
        $resolved = [IO.Path]::GetFullPath($root)
        if ([IO.Path]::GetDirectoryName($resolved) -cne $parent -or [IO.Path]::GetFileName($resolved) -notmatch '^changelog-[0-9a-f]{32}$') {
            throw "Refusing fixture cleanup outside its exact scratch parent: $resolved"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}

Case 'editing permits one valid dated Unicode change' {
    param($r)
    Put $r 'Changes/2026-10-21-unicode.md' "- Preserve café and 日本語.`n  Validation remains explicit.`n  - Pending native gates.`n"
    & $validate -Root $r
    Assert ((Get-ChangelogProblems $r).Count -eq 0) 'Valid change was rejected'
}
foreach ($bad in @(
    @{Name='notes.md'; Text='- Fine.'}, @{Name='2026-02-30-invalid.md'; Text='- Fine.'},
    @{Name='2026-10-21-Upper.md'; Text='- Fine.'}, @{Name='2026-10-21-empty.md'; Text=''},
    @{Name='2026-10-21-blank-bullet.md'; Text='- '}, @{Name='2026-10-21-paragraph.md'; Text='No bullet'},
    @{Name='2026-10-21-two.md'; Text="- One.`n- Two."}, @{Name='2026-10-21-margin.md'; Text="- One.`nAt margin."}
)) {
    Case "reject $($bad.Name) without mutation" {
        param($r)
        Put $r ('Changes/' + $bad.Name) $bad.Text
        $before = Snapshot $r
        Refuses { & $fold -Root $r }
        Refuses { & $validate -Root $r }
        Assert ((Snapshot $r) -ceq $before) 'Invalid input mutated files'
    }
}
Case 'single-fragment final gate is read-only and requires folding' {
    param($r)
    Put $r 'Changes/2026-10-21-single.md' '- Single.'
    $before = Snapshot $r
    Refuses { & $fold -Root $r -Check }
    Refuses { & $validate -Root $r -RequireFolded }
    Assert ((Snapshot $r) -ceq $before) 'Read-only gate mutated files'
    & $fold -Root $r
    & $fold -Root $r -Check
    Assert ([IO.File]::ReadAllText((Join-Path $r 'CHANGELOG.md')).Contains('- Single.')) 'Single entry was lost'
}
Case 'preview writes nothing' {
    param($r)
    Put $r 'Changes/2026-10-21-preview.md' '- Preview.'
    $before = Snapshot $r
    & $fold -Root $r -WhatIf
    Assert ((Snapshot $r) -ceq $before) 'WhatIf mutated files'
}
Case 'newest first, stable topic order, preserve released history and README' {
    param($r)
    Put $r 'Changes/2026-10-20-old.md' '- Older.'
    Put $r 'Changes/2026-10-21-a.md' '- A.'
    Put $r 'Changes/2026-10-21-b.md' "- B.`n  Detail."
    & $fold -Root $r
    $text = [IO.File]::ReadAllText((Join-Path $r 'CHANGELOG.md'))
    Assert ($text -ceq "# Changelog`n`n## Unreleased`n`n- B.`n  Detail.`n- A.`n- Older.`n- Existing.`n`n## 1.0`n`n- Released.`n") 'Fold order/history differs'
    Assert (@(Get-ChildItem -LiteralPath (Join-Path $r 'Changes') -Force).Count -eq 1) 'Fold left fragments or removed README'
    $before = Snapshot $r
    & $fold -Root $r
    Assert ((Snapshot $r) -ceq $before) 'Empty repeat mutated files'
}
Case 'preserve BOM, CRLF and Unicode bytes' {
    param($r)
    $path = Join-Path $r 'CHANGELOG.md'
    [IO.File]::WriteAllText($path, "# Changelog`r`n`r`n## Unreleased`r`n`r`n", [Text.UTF8Encoding]::new($true))
    Put $r 'Changes/2026-10-21-encoding.md' "- café 日本語.`n  More.`n"
    & $fold -Root $r
    $bytes = [IO.File]::ReadAllBytes($path)
    Assert ($bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191) 'BOM was lost'
    Assert ([IO.File]::ReadAllText($path) -ceq "# Changelog`r`n`r`n## Unreleased`r`n`r`n- café 日本語.`r`n  More.`r`n") 'Encoding or EOL changed'
}
Case 'retry after publication cleans fragments without duplicate entries' {
    param($r)
    Put $r 'Changes/2026-10-21-retry.md' "- Existing.`n"
    & $fold -Root $r
    $text = [IO.File]::ReadAllText((Join-Path $r 'CHANGELOG.md'))
    Assert ([regex]::Matches($text, '(?m)^- Existing\.$').Count -eq 1) 'Retry duplicated a published entry'
}
Case 'released matching text does not suppress a new entry' {
    param($r)
    Put $r 'Changes/2026-10-21-again.md' '- Released.'
    & $fold -Root $r
    Assert ([regex]::Matches([IO.File]::ReadAllText((Join-Path $r 'CHANGELOG.md')), '(?m)^- Released\.$').Count -eq 2) 'Released history suppressed a new entry'
}
Case 'missing README, invalid headings, unexpected files and directories fail' {
    param($r)
    Remove-Item -LiteralPath (Join-Path $r 'Changes/README.md')
    Assert ((Get-ChangelogProblems $r).Count -gt 0) 'Missing README passed'
    Put $r 'Changes/README.md' '# Restored'
    foreach ($text in @('# No heading', "## Unreleased`n`n## Unreleased`n")) {
        Put $r 'CHANGELOG.md' $text
        Refuses { & $fold -Root $r }
    }
    Put $r 'CHANGELOG.md' "# Changelog`n`n## Unreleased`n"
    Put $r 'Changes/notes.txt' 'Unsupported'
    Assert ((Get-ChangelogProblems $r).Count -gt 0) 'Unexpected file passed'
    Remove-Item -LiteralPath (Join-Path $r 'Changes/notes.txt')
    [void][IO.Directory]::CreateDirectory((Join-Path $r 'Changes/nested'))
    Assert ((Get-ChangelogProblems $r).Count -gt 0) 'Nested directory passed'
}
Case 'overlapping fold fails without touching inputs' {
    param($r)
    Put $r 'Changes/2026-10-21-lock.md' '- Locked.'
    $lease = [IO.File]::Open((Join-Path $r '.changelog-fold.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    try {
        $before = Snapshot $r
        Refuses { & $fold -Root $r }
        Assert ((Snapshot $r) -ceq $before) 'Contended fold changed input'
    } finally { $lease.Dispose() }
}
Case 'Unreleased heading at EOF is supported' {
    param($r)
    Put $r 'CHANGELOG.md' "# Changelog`n`n## Unreleased"
    Put $r 'Changes/2026-10-21-eof.md' '- EOF.'
    & $fold -Root $r
    Assert ([IO.File]::ReadAllText((Join-Path $r 'CHANGELOG.md')) -ceq "# Changelog`n`n## Unreleased`n`n- EOF.`n") 'EOF heading merged incorrectly'
}
Case 'public final runner rejects pending fragments before build or test discovery' {
    param($r)
    $scoped = [IO.File]::Exists((Join-Path $repository 'Test-Changes.ps1'))
    $runnerName = if ($scoped) { 'Test-Changes.ps1' } else { 'test.ps1' }
    Put $r $runnerName ([IO.File]::ReadAllText((Join-Path $repository $runnerName)))
    foreach ($relative in @('Tools/Fold-Changelog.ps1','Tools/Modules/Documentation/Changelog.psm1')) {
        Put $r $relative ([IO.File]::ReadAllText((Join-Path $repository $relative)))
    }
    Put $r 'Changes/2026-10-21-final-gate.md' '- Pending handoff.'
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @('-NoProfile','-File',(Join-Path $r $runnerName))) { $start.ArgumentList.Add($argument) }
    if ($scoped) { $start.ArgumentList.Add('-Mode'); $start.ArgumentList.Add('PrePush') }
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        Assert ($process.WaitForExit(30000)) 'Final gate child did not exit within its budget'
        $output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        Assert ($process.ExitCode -ne 0 -and $output.Contains('pending changelog fragment')) "Final runner did not refuse pending fragments: $output"
    } finally { $process.Dispose() }
}
Write-Host "Changelog behavior: $($results.Count) passed, $($failures.Count) failed."
if ($failures.Count) { throw ($failures -join "`n") }
$global:LASTEXITCODE = 0
