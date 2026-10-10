# Product-owned DxUi pin update. The library's ConsumerUpdate module stays read-only;
# this module owns RedXe's mutable lock and product-test command.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# The lock is read, and its API revision checked, in one place: the pin restore.
Import-Module (Join-Path $PSScriptRoot 'DxUiRestore.psm1') -Force -ErrorAction Stop

function New-RedXeDxUiUpdateRequest {
    $headers = @{ Accept = 'application/vnd.github+json'; 'X-GitHub-Api-Version' = '2022-11-28' }
    $token = if ($env:GH_TOKEN) { $env:GH_TOKEN } else { $env:GITHUB_TOKEN }
    if ($token) { $headers.Authorization = "Bearer $token" }
    return {
        param([Parameter(Mandatory)][string] $Route)
        Invoke-RestMethod -Uri "https://api.github.com/repos/RedSalamanders/DxUi/$Route" `
            -Headers $headers -TimeoutSec 10 -ErrorAction Stop
    }.GetNewClosure()
}

function Get-RedXeDxUiValidatedMainCandidate {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $LockFile,
        [scriptblock] $Request,
        [switch] $AllowApiRevisionChange
    )

    $pin = Read-RedXeDxUiLock -LockFile $LockFile
    if (-not $Request) { $Request = New-RedXeDxUiUpdateRequest }
    try {
        $head = & $Request 'commits/main'
        $candidate = [string] $head.sha
        if ($candidate -cnotmatch '^[0-9a-f]{40}$') { throw 'DxUi main did not return an exact commit.' }
        if ($candidate -ceq $pin.commit) {
            return [pscustomobject]@{ LockFile = $LockFile; Current = $pin.commit; Candidate = $candidate; IsCurrent = $true }
        }

        $comparison = & $Request "compare/$($pin.commit)...$candidate"
        if ($comparison.status -eq 'behind') { throw 'The product DxUi pin is ahead of DxUi main; review the lock manually.' }
        if ($comparison.status -ne 'ahead') { throw 'The product DxUi pin and DxUi main diverge; review the lock manually.' }

        $runs = @((& $Request "actions/workflows/ci.yml/runs?head_sha=$candidate&branch=main&event=push&per_page=1").workflow_runs)
        if ($runs.Count -ne 1 -or $runs[0].head_sha -cne $candidate -or
            $runs[0].status -ne 'completed' -or $runs[0].conclusion -ne 'success') {
            throw "DxUi main $($candidate.Substring(0, 12)) has no successful completed validation; the lock remains unchanged."
        }

        # The candidate's own compatibility number, from its capabilities.json. The product moves to another API revision only as
        # one reviewed adoption (the supported revision in DxUiRestore.psm1, the lock and the adapters), so a candidate at another
        # revision is refused unless that adoption is asked for.
        $capabilities = & $Request "contents/capabilities.json?ref=$candidate"
        $apiRevision = [string] ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String([string] $capabilities.content)) | ConvertFrom-Json).apiRevision
        if ($apiRevision -cnotmatch '^[1-9][0-9]{0,8}$') {
            throw "DxUi main $($candidate.Substring(0, 12)) names no API revision in capabilities.json; the lock remains unchanged."
        }
        $supported = Get-RedXeDxUiSupportedApiRevision
        if ([int] $apiRevision -ne $supported -and -not $AllowApiRevisionChange) {
            throw "DxUi main $($candidate.Substring(0, 12)) is at API revision $apiRevision and this product is adapted to revision $supported; the lock remains unchanged. Adopting revision $apiRevision is one reviewed change: run Update-DxUi.ps1 -AllowApiRevisionChange, then change the supported revision in Build/DxUiRestore.psm1 and the adapters the revision needs."
        }
        return [pscustomobject]@{ LockFile = $LockFile; Current = $pin.commit; Candidate = $candidate; IsCurrent = $false; ApiRevision = [int] $apiRevision }
    }
    catch [System.Exception] {
        throw "Cannot select a validated DxUi main candidate: $($_.Exception.Message)"
    }
}

function Set-RedXeDxUiLockCommit {
    param(
        [Parameter(Mandatory)][string] $LockFile,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string] $Commit,
        [Parameter(Mandatory)][ValidateRange(1, [int]::MaxValue)][int] $ApiRevision
    )

    $pin = Read-RedXeDxUiLock -LockFile $LockFile
    $pin.commit = $Commit
    $pin.apiRevision = $ApiRevision
    $json = ($pin | ConvertTo-Json -Depth 5) + [Environment]::NewLine
    $directory = Split-Path -Parent $LockFile
    $temporary = Join-Path $directory ('.' + [IO.Path]::GetFileName($LockFile) + ".$PID.$([guid]::NewGuid().ToString('N')).tmp")
    try {
        [IO.File]::WriteAllText($temporary, $json, [Text.UTF8Encoding]::new($false))
        [IO.File]::Move($temporary, $LockFile, $true)
    }
    finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
    }
}

function Invoke-RedXeDxUiUpdate {
    [CmdletBinding(SupportsShouldProcess)]
    param(
        [Parameter(Mandatory)][string] $RepoRoot,
        [switch] $UpdateOnly,
        [scriptblock] $Request,
        [scriptblock] $ValidationAction,
        [switch] $AllowApiRevisionChange
    )

    $lockFile = Join-Path $RepoRoot 'Dependencies/DxUi.lock.json'
    $candidate = Get-RedXeDxUiValidatedMainCandidate -LockFile $lockFile -Request $Request -AllowApiRevisionChange:$AllowApiRevisionChange
    if ($candidate.IsCurrent) {
        Write-Host "DxUi is already pinned to validated main $($candidate.Current.Substring(0, 12))." -ForegroundColor Green
        return [pscustomobject]@{ Updated = $false; Validated = $false; Candidate = $candidate.Candidate }
    }

    if (-not $PSCmdlet.ShouldProcess($lockFile, "Update DxUi pin $($candidate.Current.Substring(0, 12)) -> $($candidate.Candidate.Substring(0, 12))")) {
        return [pscustomobject]@{ Updated = $false; Validated = $false; Candidate = $candidate.Candidate; WhatIf = $true }
    }

    if (-not $UpdateOnly -and -not $ValidationAction) {
        throw 'A product validation action is required unless -UpdateOnly is selected.'
    }
    Set-RedXeDxUiLockCommit -LockFile $lockFile -Commit $candidate.Candidate -ApiRevision $candidate.ApiRevision
    Write-Host "DxUi pin updated: $($candidate.Current.Substring(0, 12)) -> $($candidate.Candidate.Substring(0, 12))." -ForegroundColor Yellow

    $supported = Get-RedXeDxUiSupportedApiRevision
    if ($candidate.ApiRevision -ne $supported) {
        # Asked for with -AllowApiRevisionChange: the lock names the new revision, which the build refuses until the adoption is done.
        Write-Host "DxUi API revision $supported -> $($candidate.ApiRevision): the build refuses this lock until the supported revision in Build/DxUiRestore.psm1 and the adapters the revision needs change with it. Product validation is not run before then." -ForegroundColor Yellow
        return [pscustomobject]@{ Updated = $true; Validated = $false; Candidate = $candidate.Candidate; ApiRevision = $candidate.ApiRevision }
    }

    if ($UpdateOnly) {
        Write-Host 'Product validation skipped by -UpdateOnly; retain external validation evidence with this branch.' -ForegroundColor Yellow
        return [pscustomobject]@{ Updated = $true; Validated = $false; Candidate = $candidate.Candidate }
    }

    Write-Host 'Running RedXe validation for the updated DxUi pin...' -ForegroundColor Cyan
    & $ValidationAction
    Write-Host 'DxUi pin update and RedXe validation passed.' -ForegroundColor Green
    return [pscustomobject]@{ Updated = $true; Validated = $true; Candidate = $candidate.Candidate }
}

Export-ModuleMember -Function Get-RedXeDxUiValidatedMainCandidate, Invoke-RedXeDxUiUpdate
