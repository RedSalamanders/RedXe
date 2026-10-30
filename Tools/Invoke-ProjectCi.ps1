<#
.SYNOPSIS
Adapts the project commands to the pinned organization CI engine.
.PARAMETER Phase
Compiler-free PR checks, Release test build, regression execution, production ZIP, or explicit qualification.
.PARAMETER EngineRoot
Checkout of the exact shared engine commit in .ci/project.json.
.PARAMETER Platform
Matching native x64 or ARM64 hosted runner.
.PARAMETER Descriptor
Frozen main admission descriptor; required for native phases.
.PARAMETER Base
Immutable PR comparison base for changelog validation.
.PARAMETER Configuration
Debug, Release or ASan Debug for an explicit qualification only.
.PARAMETER QualificationScope
Normal regression or explicit Full qualification; never advances automatic Release state.
.PARAMETER DevelopmentEngine
Allows a local engine under development for compiler-free checks only.
.OUTPUTS
Canonical test evidence, source-bound receipts and qualified ZIPs; GitHub job output paths.
.NOTES
Requires PowerShell 7.4 and Git. Native work is hosted-only and uses the matching architecture.
Static never invokes compiler fixtures. No publication or source/pin updates.
See Specs/Build/Build_UnifiedCI.md for roles, input rules, evidence and manual actions.
.EXAMPLE
./Tools/Invoke-ProjectCi.ps1 -Phase Static -EngineRoot ../RedSalamanders.CI -Base origin/main -DevelopmentEngine
#>
[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('Static','ReleaseTestBuild','ReleaseTest','Production','Qualification')][string]$Phase,
    [Parameter(Mandatory)][string]$EngineRoot, [ValidateSet('x64','ARM64')][string]$Platform = 'x64',
    [string]$Descriptor = '', [string]$Base = 'origin/main',
    [ValidateSet('Debug','Release','ASan Debug')][string]$Configuration = 'Release', [ValidateSet('Normal','Full')][string]$QualificationScope = 'Normal', [switch]$DevelopmentEngine)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$EngineRoot = [IO.Path]::GetFullPath($EngineRoot)
Import-Module (Join-Path $EngineRoot 'Tools/Modules/Ci.Policy.psm1') -ErrorAction Stop
Import-Module (Join-Path $EngineRoot 'Tools/Modules/Ci.Project.psm1') -ErrorAction Stop
$context = New-CiProjectContext -Root $root -EngineRoot $EngineRoot -Phase $Phase -Platform $Platform -Descriptor $Descriptor -DevelopmentEngine:$DevelopmentEngine
function Run([string]$Script, [string[]]$CommandArguments = @()) {
    Invoke-CiProjectScript -Context $context -Script $Script -Arguments $CommandArguments
}
if ($Phase -eq 'Static') {
    $env:CI_NATIVE_BUILDS_DISABLED = 'true'
    Test-CiProjectConfiguration $context
    & (Join-Path $EngineRoot 'Tools/Test-Workflows.ps1') -Root $root
    $baseCommit = Resolve-CiCommit $root $Base
    Run 'Tools/Test-Changelog.ps1' @('-Base',$baseCommit)
    Run 'validate-skills.ps1'
    return
}
if ($Phase -ne 'Qualification') { $Configuration = 'Release' }
$number = [string]$context.batch.buildNumber
$profile = @('-Platform',$Platform,'-Configuration',$Configuration)
$started = [DateTime]::UtcNow
$receiptDirectory = Join-Path $root ".build/ci/regression/$Platform"
$evidenceDirectory = Join-Path $root '.build/evidence/test-runs'
if ($env:GITHUB_OUTPUT) { Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value ('evidence_directory=' + $evidenceDirectory) }
if ($Phase -in @('ReleaseTestBuild','Qualification','Production')) {
    $buildArguments = $profile + @('-BuildNumber',$number)
    if ($Phase -eq 'Production') { $buildArguments += '-ProductionOnly' }
    Run 'build.ps1' $buildArguments
    Import-Module (Join-Path $root 'Build/DxUiRestore.psm1')
    $pin=Read-RedXeDxUiLock -LockFile (Join-Path $root 'Dependencies/DxUi.lock.json')
    $source=Get-RedXeDxUiSourcePath -RepoRoot $root -Commit $pin.commit
    Run ([IO.Path]::GetRelativePath($root,(Join-Path $source 'validate-build-matrix.ps1'))) @('-Root',$root)
    if ($Phase -eq 'ReleaseTestBuild') { return }
}
if ($Phase -eq 'Production') {
    Run 'package.ps1' @('-Platform',$Platform,'-BuildNumber',$number,'-SkipBuild')
    $archives = @(Get-ChildItem (Join-Path $root '.build/packages') -Filter "*-$Platform-*.zip" -File | ForEach-Object FullName)
    Write-CiProductionReceipt -Context $context -Archives $archives -TestModeDisabled $true -SmokePassed $true
    return
}
if ($Platform -eq 'x64') { Run 'Tests/BuildProcessTests/Invoke-ToolingTests.ps1' }
Run 'test.ps1' ($profile + @('-Full','-SkipTooling','-SkipBuild','-BuildNumber',$number))
if ($Configuration -ne 'ASan Debug') { Run 'measure-av-views.ps1' @('-Configuration',$Configuration,'-SkipBuild','-OutputPath','.build/reports/av-native-views.json') }
$probe = Invoke-CiAssertionProbe -Context $context -Executable ".build/$Platform/$Configuration/PluginContractTests.exe" -ExpectedExitCode 1
if ($Phase -eq 'Qualification') { Write-Host 'Explicit native qualification finished.'; return }
$evidence = Get-CiSuiteEvidence -Context $context -StartedAt $started
$binaries = Get-CiProfileArtifacts -Context $context


if ($Phase -eq 'Qualification') { Write-Host 'Explicit qualification finished; it does not authorize production or publication.'; return }
$countUnit=if($evidence.Contains('countUnit')){$evidence.countUnit}else{'suite'}
$registered=if($evidence.Contains('registered')){[int]$evidence.registered}else{[int]$evidence.discovered}
$expectedSkipped=if($evidence.Contains('expectedSkipped')){[int]$evidence.expectedSkipped}else{0}
Write-CiRegressionReceipt -Context $context -Discovered $evidence.discovered -Executed $evidence.executed -AssertionProbePassed $probe -Artifacts $binaries -EvidenceDirectory $receiptDirectory -CanonicalEvidence $evidence.evidence -CountUnit $countUnit -Registered $registered -ExpectedSkipped $expectedSkipped
if ($env:GITHUB_OUTPUT) { Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value ('evidence_directory=' + $evidenceDirectory) }

