<#
.SYNOPSIS Runs the profile-independent build and skill regression tests once.
.DESCRIPTION Requires Windows, PowerShell, Git, Python and Build/requirements-validation.txt.
Creates bounded test-owned fixtures under .build; does not build or run the product.
#>
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repository=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
foreach($name in @('BuildProcessTests.ps1','DxUiProvenanceTests.ps1','DxUiUpdateTests.ps1','DxUiRestoreTests.ps1','ScopedTesting.Tests.ps1')) {
    $global:LASTEXITCODE=0
    & (Join-Path $PSScriptRoot $name)
    if ($LASTEXITCODE) {throw "Tooling test failed: $name (exit $LASTEXITCODE)."}
}
& (Join-Path $repository 'validate-skills.ps1')
if ($LASTEXITCODE) {throw 'Repository skill validation failed.'}
$python=Get-Command py.exe -ErrorAction SilentlyContinue
$arguments=@('-m','unittest','discover','-s',(Join-Path $repository 'Tests/SkillValidationTests'),'-v')
if($python) {$arguments=@('-3')+$arguments} else {$python=Get-Command python.exe -ErrorAction Stop}
& $python.Source @arguments
if($LASTEXITCODE) {throw 'Skill validator regression tests failed.'}
Write-Host 'All independent tooling tests passed.'
exit 0
