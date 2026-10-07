[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repoRoot 'Build/DxUiUpdate.psm1') -Force -ErrorAction Stop

function New-RedXeDxUiUpdateFixture {
    param([string] $Root, [string] $Commit)
    [void](New-Item -ItemType Directory -Path (Join-Path $Root 'Dependencies') -Force)
    [ordered]@{ repository = 'https://github.com/RedSalamanders/DxUi'; commit = $Commit; apiRevision = 3; targets = @('DxUi') } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Root 'Dependencies/DxUi.lock.json') -NoNewline
}

function New-RedXeDxUiUpdateRequest {
    param([string] $Current, [string] $Candidate, [string] $Conclusion = 'success', [int] $ApiRevision = 3)
    return {
        param([string] $Route)
        if ($Route -eq 'commits/main') { return [pscustomobject]@{ sha = $Candidate } }
        if ($Route -eq "compare/$Current...$Candidate") { return [pscustomobject]@{ status = 'ahead' } }
        if ($Route -like 'actions/workflows/ci.yml/runs*') {
            return [pscustomobject]@{ workflow_runs = @([pscustomobject]@{ head_sha = $Candidate; status = 'completed'; conclusion = $Conclusion }) }
        }
        if ($Route -eq "contents/capabilities.json?ref=$Candidate") {
            # The contents API returns the file base64-encoded, in lines.
            $capabilities = "{`n  `"apiRevision`": $ApiRevision,`n  `"description`": `"A fixture long enough for the encoding to span several lines.`"`n}`n"
            return [pscustomobject]@{ content = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($capabilities), [Base64FormattingOptions]::InsertLineBreaks) }
        }
        throw "Unexpected DxUi update route: $Route"
    }.GetNewClosure()
}

$testParent = [IO.Path]::GetFullPath((Join-Path $repoRoot '.build/BuildProcessTests'))
$testRoot = [IO.Path]::GetFullPath((Join-Path $testParent ('DxUiUpdate-' + [guid]::NewGuid().ToString('N'))))
$expectedPrefix = $testParent.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $testRoot.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to create a DxUi-update fixture outside '$testParent': $testRoot"
}

try {
    $current = 'a' * 40; $candidate = 'b' * 40
    New-RedXeDxUiUpdateFixture -Root $testRoot -Commit $current
    $state = [pscustomobject]@{ Ran = $false }
    # The updater reports through Write-Host; 6>$null keeps the fixture's fake pins from reading as a real
    # "DxUi pin updated" line in test.ps1 output.
    $result = Invoke-RedXeDxUiUpdate -RepoRoot $testRoot -Request (New-RedXeDxUiUpdateRequest -Current $current -Candidate $candidate) -ValidationAction { $state.Ran = $true } 6>$null
    if (-not $result.Updated -or -not $result.Validated -or -not $state.Ran) { throw 'Default DxUi update did not run RedXe validation.' }
    $updated = (Get-Content -Raw -LiteralPath (Join-Path $testRoot 'Dependencies/DxUi.lock.json') | ConvertFrom-Json).commit
    if ($updated -ne $candidate) { throw "DxUi update wrote '$updated' instead of '$candidate'." }

    $updateOnlyRoot = Join-Path $testRoot 'update-only'; $current = 'c' * 40; $candidate = 'd' * 40
    New-RedXeDxUiUpdateFixture -Root $updateOnlyRoot -Commit $current
    $state = [pscustomobject]@{ Ran = $false }
    $result = Invoke-RedXeDxUiUpdate -RepoRoot $updateOnlyRoot -UpdateOnly -Request (New-RedXeDxUiUpdateRequest -Current $current -Candidate $candidate) -ValidationAction { $state.Ran = $true } 6>$null
    if (-not $result.Updated -or $result.Validated -or $state.Ran) { throw '-UpdateOnly did not skip RedXe validation.' }

    $rejectedRoot = Join-Path $testRoot 'rejected'; $current = 'e' * 40; $candidate = 'f' * 40
    New-RedXeDxUiUpdateFixture -Root $rejectedRoot -Commit $current
    $rejected = $false
    try { Invoke-RedXeDxUiUpdate -RepoRoot $rejectedRoot -UpdateOnly -Request (New-RedXeDxUiUpdateRequest -Current $current -Candidate $candidate -Conclusion 'failure') } catch { $rejected = $_.Exception.Message -like 'Cannot select a validated DxUi main candidate*' }
    if (-not $rejected) { throw 'The updater accepted an unvalidated DxUi candidate.' }
    $preserved = (Get-Content -Raw -LiteralPath (Join-Path $rejectedRoot 'Dependencies/DxUi.lock.json') | ConvertFrom-Json).commit
    if ($preserved -ne $current) { throw 'The updater changed a lock for an unvalidated DxUi candidate.' }

    # A validated candidate at another API revision is refused, in either mode, unless the adoption is asked for.
    $revisionRoot = Join-Path $testRoot 'revision'; $current = '1' * 40; $candidate = '2' * 40
    New-RedXeDxUiUpdateFixture -Root $revisionRoot -Commit $current
    $revisionRequest = New-RedXeDxUiUpdateRequest -Current $current -Candidate $candidate -ApiRevision 4
    foreach ($updateOnly in @($false, $true)) {
        $state = [pscustomobject]@{ Ran = $false }
        $message = ''
        try { Invoke-RedXeDxUiUpdate -RepoRoot $revisionRoot -UpdateOnly:$updateOnly -Request $revisionRequest -ValidationAction { $state.Ran = $true } 6>$null }
        catch { $message = $_.Exception.Message }
        if ($message -notlike 'Cannot select a validated DxUi main candidate: *is at API revision 4 and this product is adapted to revision 3*-AllowApiRevisionChange*') {
            throw "The updater did not refuse a DxUi candidate at another API revision (UpdateOnly=${updateOnly}): $message"
        }
        $lock = Get-Content -Raw -LiteralPath (Join-Path $revisionRoot 'Dependencies/DxUi.lock.json') | ConvertFrom-Json
        if ($lock.commit -ne $current -or $lock.apiRevision -ne 3 -or $state.Ran) { throw 'The updater changed the lock for a DxUi candidate at another API revision.' }
    }
    $state = [pscustomobject]@{ Ran = $false }
    $result = Invoke-RedXeDxUiUpdate -RepoRoot $revisionRoot -AllowApiRevisionChange -Request $revisionRequest -ValidationAction { $state.Ran = $true } 6>$null
    $lock = Get-Content -Raw -LiteralPath (Join-Path $revisionRoot 'Dependencies/DxUi.lock.json') | ConvertFrom-Json
    if (-not $result.Updated -or $result.Validated -or $state.Ran -or $lock.commit -ne $candidate -or $lock.apiRevision -ne 4) {
        throw 'An asked-for API revision change did not record the commit and its revision without running validation that cannot pass yet.'
    }
}
finally {
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}

Write-Host 'DxUi update command tests passed.' -ForegroundColor Green
