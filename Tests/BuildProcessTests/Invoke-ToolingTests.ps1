<#
.SYNOPSIS Runs the profile-independent build and skill regression tests once.
.DESCRIPTION Requires Windows, PowerShell 7 and Git.
Restores the exact pinned DxUi source before checking its build-tool wiring on a clean checkout.
Creates bounded test-owned fixtures under .build; does not build or run the product.
#>
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repository=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$changesScratch = Join-Path $repository '.build/changelog-fixtures'
[void][IO.Directory]::CreateDirectory($changesScratch)
& (Join-Path $repository 'Tools/Test-ChangelogWorkflow.ps1') -ScratchRoot $changesScratch
# The restore contract checks the pinned source's VisualStudio helper. Independent
# tooling runs before a product build too, so establish that source-only prerequisite.
Import-Module (Join-Path $repository 'Build/DxUiRestore.psm1') -Force
$null=Restore-RedXeDxUiPin -RepoRoot $repository
foreach($name in @('BuildConsole.Tests.ps1','ReleaseBuildPolicy.Tests.ps1','MsvcSetup.Tests.ps1','TestRunEvidenceTests.ps1','BuildProcessTests.ps1','LinkConfiguration.Tests.ps1','DxUiProvenanceTests.ps1','DxUiUpdateTests.ps1','DxUiRestoreTests.ps1','ScopedTesting.Tests.ps1','SkillValidation.Tests.ps1','SkillPackageIntegrity.Tests.ps1','CompilerHost.Tests.ps1','RedMonitorConsumption.Tests.ps1')) {
    $global:LASTEXITCODE=0
    & (Join-Path $PSScriptRoot $name)
    if ($LASTEXITCODE) {throw "Tooling test failed: $name (exit $LASTEXITCODE)."}
}
& (Join-Path $repository 'validate-skills.ps1')
if ($LASTEXITCODE) {throw 'Repository skill validation failed.'}
Write-Host 'All independent tooling tests passed.'
exit 0
