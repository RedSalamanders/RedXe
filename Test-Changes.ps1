<#
.SYNOPSIS
Runs affected test scopes, reusing identical successful local results.
.DESCRIPTION
The default compares committed work since the local merge base plus staged, unstaged,
deleted, renamed and untracked files. Unknown inputs select full coverage. Full selects
every noninteractive scope. PrePush delegates matching scopes only to an enabled and
reviewed candidate PR workflow whose checks the default branch requires; remaining
obligations execute locally. CI never runs this script: it runs test.ps1 -Full -SkipTooling
and the tooling tests.
.PARAMETER Mode
Affected (default), Full, or PrePush. Partial/delegated coverage is labeled explicitly.
.PARAMETER BaseRef
Local comparison ref; defaults to the repository's remote default branch. Never fetches.
.PARAMETER Scopes
Explicit component scopes for focused iteration, also as one comma list; incompatible with Full and PrePush.
.PARAMETER Configuration
Debug, Release or ASan Debug.
.PARAMETER Platform
x64 or ARM64. ARM64 runtime tests require an ARM64 host, which also runs x64 under emulation.
.PARAMETER Explain
Prints the selected scopes, changed paths and reasons without building or testing.
.PARAMETER Force
Executes even when identical successful local evidence exists.
.PARAMETER SkipBuild
Uses only artifacts with a matching scoped-runner build attestation. Changed source, build
number or artifact bytes fail before tests; run once without this switch to establish attestation.
.OUTPUTS
Console plan and local receipts under .build/reports/scoped-tests. Exit 0 means the
selected local work passed; focused/delegated work never establishes repository success.
Except under Explain, the last line is a coverage label. SELECTED_PASSED; NOT_RECORDED means inputs changed during
the run: the selected work passed on the tree as it started, the current tree is not covered
and changed scopes are not reusable. NOTHING_SELECTED; repository NOT_EVALUATED means no test
input changed. CI_PENDING means delegated work awaits the PR's required checks.
.NOTES
Requires PowerShell 7 and Git; build toolchain for native work, gh for PR delegation.
PrePush prints why any PR check's work stays local.
Builds selected profile, runs repository-owned tests and writes bounded local receipts.
Does not activate the person's desktop. Uses the canonical build/test entrypoints.
.EXAMPLE
./Test-Changes.ps1 -Explain
.EXAMPLE
./Test-Changes.ps1 -Scopes Settings -Configuration Debug
.EXAMPLE
./Test-Changes.ps1 -Mode PrePush -Configuration Release
#>
[CmdletBinding()]
param(
    [ValidateSet('Affected','Full','PrePush')][string] $Mode='Affected',
    [string] $BaseRef='',
    [string[]] $Scopes=@(),
    [ValidateSet('Debug','Release','ASan Debug')][string] $Configuration='Debug',
    [ValidateSet('x64','ARM64')][string] $Platform='x64',
    [switch] $Explain,
    [switch] $Force,
    [switch] $SkipBuild
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$modulePath=Join-Path $root 'Build/ScopedTesting.psm1'
Import-Module $modulePath -Force
$manifest=Read-ScopedTestManifest $root
# pwsh -File passes 'A,B' as one string, unlike a call from PowerShell; split it as test.ps1 splits -Suites.
$Scopes=@($Scopes | ForEach-Object {$_ -split ','} | ForEach-Object {$_.Trim()} | Where-Object {$_})
if ($Scopes.Count -and $Mode -ne 'Affected') {throw '-Scopes is only valid for focused Affected iteration.'}
if (-not $BaseRef) {$BaseRef='origin/'+$manifest.defaultBranch}
# The comparison first: without Git, the base ref or a shared history, its message says what to do instead.
$changed=if ($Mode -eq 'Affected' -and -not $Scopes.Count) {@(Get-ScopedChangedPaths $root $BaseRef)} else {@()}
Write-Host "Native test naming: $(Assert-ScopedTestNames $root) files checked."
$plan=Get-ScopedTestPlan -Manifest $manifest -ChangedPaths $changed -Scopes $Scopes -Full:($Mode -ne 'Affected')
Write-Host "Test plan: $Mode ($Platform $Configuration); scopes: $($plan.scopes -join ', ')"
foreach ($reason in $plan.reasons) {Write-Host "  $($reason.path): $($reason.reason) => $($reason.scopes -join ', ')"}

$refusal=''
$deferred=@(if ($Mode -eq 'PrePush') {Get-ScopedPrCoverage $root $manifest $Platform $Configuration -Refusal ([ref]$refusal)})
if ($deferred.Count) {Write-Host "DEFERRED_CI (forthcoming PR, pending success): $($deferred -join ', ')"}
if ($refusal) {Write-Host "PR delegation not used$(if ($deferred.Count) {' for the rest'}): $refusal" -ForegroundColor Yellow}
if ($Explain) {exit 0}
$selected=@($manifest.scopes | Where-Object {$_.name -in $plan.scopes -and $_.name -notin $deferred})
if (-not $selected.Count) {
    Write-Host 'No local test execution is required by this plan.'
    # Every exit ends with a coverage label: an empty plan evaluated nothing, and delegated work is still pending.
    if ($deferred.Count) {Write-Host 'LOCAL_OBLIGATIONS_NONE; CI_PENDING' -ForegroundColor Green}
    else {Write-Host "NOTHING_SELECTED; repository NOT_EVALUATED (no test input changed since the merge base with $BaseRef; ./test.ps1 -Full runs every suite)" -ForegroundColor Yellow}
    exit 0
}
$native=@($selected | Where-Object {$_.native})
$reports=Join-Path $root ".build/reports/scoped-tests/$Platform-$($Configuration -replace ' ','')"
if ($native.Count) {
    Assert-ScopedRuntimePlatform $Platform
    # One version stamp for the build, the attestation and test.ps1's version check; a commit leaves it unchanged.
    $buildNumber=Get-ScopedBuildNumber $root ('origin/'+$manifest.defaultBranch)
    Write-Host "Build number: $buildNumber"
    $sourceBefore=Get-ScopedSourceIdentity $root -CompiledOnly
    $attestation=Join-Path $reports 'build.json'
    if ($SkipBuild) {
        $artifact=Get-ScopedArtifactIdentity $root $Platform $Configuration
        if (-not (Test-ScopedReceipt $attestation (Get-ScopedDigest ($sourceBefore+"`n"+$artifact+"`n"+$buildNumber)))) {throw 'SkipBuild refused: source, build number or build identity changed, or no attestation exists. Run without -SkipBuild.'}
    } else {
        & (Join-Path $root 'build.ps1') -Platform $Platform -Configuration $Configuration -BuildNumber $buildNumber
        if ($LASTEXITCODE -ne 0) {throw "Build failed with exit $LASTEXITCODE."}
        # Nested entrypoints import their own modules; restore this caller's surface.
        Import-Module $modulePath -Force
        if ((Get-ScopedSourceIdentity $root -CompiledOnly) -cne $sourceBefore) {throw 'Source changed during the build; no attestation was published.'}
        $artifact=Get-ScopedArtifactIdentity $root $Platform $Configuration
        Write-ScopedReceipt $attestation (Get-ScopedDigest ($sourceBefore+"`n"+$artifact+"`n"+$buildNumber))
    }
}
$pending=[Collections.Generic.List[object]]::new()
$identities=@{}
$commonNative=if ($native.Count) {Get-ScopedRunIdentity $root $Platform $Configuration '(common)'} else {''}
$commonTooling=Get-ScopedDigest ((Get-ScopedSourceIdentity $root)+"`n"+(Get-ScopedEnvironmentIdentity -Tooling))
function Get-ScopeReceiptPath($Scope) {if ($Scope.native) {Join-Path $reports ($Scope.name+'.json')} else {Join-Path $root ('.build/reports/scoped-tests/independent/'+$Scope.name+'.json')}}
foreach ($scope in $selected) {
    $identity=Get-ScopedDigest ($(if ($scope.native) {$commonNative} else {$commonTooling})+"`n"+$scope.name)
    $identities[$scope.name]=$identity
    $receipt=Get-ScopeReceiptPath $scope
    if (-not $Force -and $scope.reuse -and (Test-ScopedReceipt $receipt $identity)) {Write-Host "REUSED $($scope.name) (identical successful local evidence)"}
    else {
        # A failed or interrupted rerun must never leave an earlier success reusable.
        if (Test-Path -LiteralPath $receipt) {Remove-Item -LiteralPath $receipt}
        $pending.Add($scope)
    }
}
$mutated=@()
if ($pending.Count) {
    $runtime=@($pending | Where-Object {$_.native} | ForEach-Object {$_.name})
    $tooling=@($pending | Where-Object {-not $_.native})
    if ($tooling.Count) {
        foreach ($command in $manifest.toolingCommands) {
            $global:LASTEXITCODE=0
            & (Join-Path $root $command)
            if (-not $? -or $LASTEXITCODE -ne 0) {throw "Tooling failed: $command (exit $LASTEXITCODE)."}
        }
    }
    if ($runtime.Count) {
        & (Join-Path $root 'test.ps1') -Platform $Platform -Configuration $Configuration -SkipBuild -Suites $runtime -BuildNumber $buildNumber
        if ($LASTEXITCODE -ne 0) {throw "Selected runtime tests failed with exit $LASTEXITCODE."}
    }
    Import-Module $modulePath -Force
    $afterNative=if ($runtime.Count) {Get-ScopedRunIdentity $root $Platform $Configuration '(common)'} else {$commonNative}
    $afterTooling=Get-ScopedDigest ((Get-ScopedSourceIdentity $root)+"`n"+(Get-ScopedEnvironmentIdentity -Tooling))
    # A scope whose inputs changed while it ran passed on the tree as it started: it is not recorded, the others are.
    $mutated=@($pending | Where-Object {(Get-ScopedDigest ($(if ($_.native) {$afterNative} else {$afterTooling})+"`n"+$_.name)) -cne $identities[$_.name]} | ForEach-Object {$_.name})
    foreach ($scope in $pending) {
        if ($scope.reuse -and $scope.name -notin $mutated) {Write-ScopedReceipt (Get-ScopeReceiptPath $scope) $identities[$scope.name]}
    }
    if ($mutated.Count) {Write-Warning "Inputs changed while $($mutated -join ', ') ran: they passed on the tree as it started and are not recorded for reuse. Run again to cover the current tree."}
}
$coverage=if ($mutated.Count) {'SELECTED_PASSED; NOT_RECORDED'} elseif ($deferred.Count) {'LOCAL_OBLIGATIONS_PASSED; CI_PENDING'} elseif ($plan.full) {'FULL_NONINTERACTIVE_PASSED'} else {'SELECTED_PASSED; repository NOT_EVALUATED'}
Write-Host $coverage -ForegroundColor $(if ($mutated.Count) {'Yellow'} else {'Green'})
exit 0
