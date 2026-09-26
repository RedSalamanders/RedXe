[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repoRoot 'Build/DxUiUpdate.psm1') -Force -ErrorAction Stop

function New-RedXeDxUiUpdateFixture {
    param([string] $Root, [string] $Commit)
    [void](New-Item -ItemType Directory -Path (Join-Path $Root 'Dependencies') -Force)
    [ordered]@{ repository = 'https://github.com/RedSalamanders/DxUi'; commit = $Commit; apiRevision = 2; targets = @('DxUi') } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Root 'Dependencies/DxUi.lock.json') -NoNewline
}

function New-RedXeDxUiUpdateRequest {
    param([string] $Current, [string] $Candidate, [string] $Conclusion = 'success')
    return {
        param([string] $Route)
        if ($Route -eq 'commits/main') { return [pscustomobject]@{ sha = $Candidate } }
        if ($Route -eq "compare/$Current...$Candidate") { return [pscustomobject]@{ status = 'ahead' } }
        if ($Route -like 'actions/workflows/ci.yml/runs*') {
            return [pscustomobject]@{ workflow_runs = @([pscustomobject]@{ head_sha = $Candidate; status = 'completed'; conclusion = $Conclusion }) }
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
}
finally {
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}

Write-Host 'DxUi update command tests passed.' -ForegroundColor Green
