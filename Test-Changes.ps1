<#
.SYNOPSIS
Runs affected test scopes, reusing identical successful local results.
.DESCRIPTION
The default compares committed work since the local merge base plus staged, unstaged,
deleted, renamed and untracked files. Unknown inputs select full coverage. Full selects
every noninteractive scope. PrePush delegates matching scopes only to an enabled and
reviewed candidate PR workflow; remaining obligations execute locally. CI always uses Force.
.PARAMETER Mode
Affected (default), Full, or PrePush. Partial/delegated coverage is labeled explicitly.
.PARAMETER BaseRef
Local comparison ref; defaults to the repository's remote default branch. Never fetches.
.PARAMETER Scopes
Explicit component scopes for focused iteration; incompatible with Full and PrePush.
.PARAMETER Configuration
Debug, Release or ASan Debug.
.PARAMETER Platform
x64 or ARM64. Runtime tests require matching native architecture.
.PARAMETER Explain
Prints the selected scopes, changed paths and reasons without building or testing.
.PARAMETER Force
Executes even when identical successful local evidence exists.
.PARAMETER SkipBuild
Uses only artifacts with a matching scoped-runner build attestation. Changed source or
artifact bytes fail before tests; run once without this switch to establish attestation.
.OUTPUTS
Console plan and local receipts under .build/reports/scoped-tests. Exit 0 means the
selected local work passed; focused/delegated work never establishes repository success.
.NOTES
Requires PowerShell 7 and Git; build toolchain for native work, gh for PR delegation.
Builds selected profile, runs repository-owned tests and writes bounded local receipts.
Does not activate the person's desktop. Uses the canonical build/test entrypoints.
.EXAMPLE
./Test-Changes.ps1 -Explain
.EXAMPLE
./Test-Changes.ps1 -Scopes Tree -Configuration Debug
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
if ($Scopes.Count -and $Mode -ne 'Affected') {throw '-Scopes is only valid for focused Affected iteration.'}
if (-not $BaseRef) {$BaseRef='origin/'+$manifest.defaultBranch}
Write-Host "Native test naming: $(Assert-ScopedTestNames $root) files checked."
$changed=if ($Mode -eq 'Affected' -and -not $Scopes.Count) {@(Get-ScopedChangedPaths $root $BaseRef)} else {@()}
$plan=Get-ScopedTestPlan -Manifest $manifest -ChangedPaths $changed -Scopes $Scopes -Full:($Mode -ne 'Affected')
Write-Host "Test plan: $Mode ($Platform $Configuration); scopes: $($plan.scopes -join ', ')"
foreach ($reason in $plan.reasons) {Write-Host "  $($reason.path): $($reason.reason) => $($reason.scopes -join ', ')"}

$deferred=@(if ($Mode -eq 'PrePush') {Get-ScopedPrCoverage $root $manifest $Platform $Configuration})
if ($deferred.Count) {Write-Host "DEFERRED_CI (forthcoming PR, pending success): $($deferred -join ', ')"}
if ($Explain) {exit 0}
$selected=@($manifest.scopes | Where-Object {$_.name -in $plan.scopes -and $_.name -notin $deferred})
if (-not $selected.Count) {Write-Host 'No local test execution is required by this plan.';exit 0}
$native=@($selected | Where-Object {$_.native})
$reports=Join-Path $root ".build/reports/scoped-tests/$Platform-$($Configuration -replace ' ','')"
if ($native.Count) {
    $architecture=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    if (($Platform -eq 'ARM64' -and $architecture -ne 'Arm64') -or ($Platform -eq 'x64' -and $architecture -ne 'X64')) {throw 'Native runtime testing requires the selected host architecture.'}
    $sourceBefore=Get-ScopedSourceIdentity $root -CompiledOnly
    $attestation=Join-Path $reports 'build.json'
    if ($SkipBuild) {
        $artifact=Get-ScopedArtifactIdentity $root $Platform $Configuration
        if (-not (Test-ScopedReceipt $attestation (Get-ScopedDigest ($sourceBefore+"`n"+$artifact)))) {throw 'SkipBuild refused: source/build identity changed or no attestation exists. Run without -SkipBuild.'}
    } else {
        & (Join-Path $root 'build.ps1') -Platform $Platform -Configuration $Configuration
        if ($LASTEXITCODE -ne 0) {throw "Build failed with exit $LASTEXITCODE."}
        # Nested entrypoints import their own modules; restore this caller's surface.
        Import-Module $modulePath -Force
        if ((Get-ScopedSourceIdentity $root -CompiledOnly) -cne $sourceBefore) {throw 'Source changed during the build; no attestation was published.'}
        $artifact=Get-ScopedArtifactIdentity $root $Platform $Configuration
        Write-ScopedReceipt $attestation (Get-ScopedDigest ($sourceBefore+"`n"+$artifact))
    }
}
$pending=[Collections.Generic.List[object]]::new()
$identities=@{}
$commonNative=if ($native.Count) {Get-ScopedRunIdentity $root $Platform $Configuration '(common)'} else {''}
$commonTooling=Get-ScopedDigest ((Get-ScopedSourceIdentity $root)+"`n"+(Get-ScopedEnvironmentIdentity))
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
        & (Join-Path $root 'test.ps1') -Platform $Platform -Configuration $Configuration -SkipBuild -Suites $runtime
        if ($LASTEXITCODE -ne 0) {throw "Selected runtime tests failed with exit $LASTEXITCODE."}
    }
    Import-Module $modulePath -Force
    $afterNative=if ($runtime.Count) {Get-ScopedRunIdentity $root $Platform $Configuration '(common)'} else {$commonNative}
    $afterTooling=Get-ScopedDigest ((Get-ScopedSourceIdentity $root)+"`n"+(Get-ScopedEnvironmentIdentity))
    foreach ($scope in $pending) {
        $after=Get-ScopedDigest ($(if ($scope.native) {$afterNative} else {$afterTooling})+"`n"+$scope.name)
        if ($after -cne $identities[$scope.name]) {throw "Inputs changed while $($scope.name) ran; no reusable success was published."}
    }
    foreach ($scope in $pending) {
        if ($scope.reuse) {Write-ScopedReceipt (Get-ScopeReceiptPath $scope) $identities[$scope.name]}
    }
}
$coverage=if ($deferred.Count) {'LOCAL_OBLIGATIONS_PASSED; CI_PENDING'} elseif ($plan.full) {'FULL_NONINTERACTIVE_PASSED'} else {'SELECTED_PASSED; repository NOT_EVALUATED'}
Write-Host $coverage -ForegroundColor Green
exit 0
