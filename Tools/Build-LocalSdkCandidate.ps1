<#
.SYNOPSIS Builds RedXe against an explicit local SDK into separate unpinned outputs.
.DESCRIPTION Uses the shared broker's source, API, actual ABI, immutable reader, output lease and linker-input checks. Builds the application target with its test surface; does not execute it.
.PARAMETER Descriptor Absolute producer local SDK snapshot descriptor.
.PARAMETER DescriptorSha256 Independently retained SHA-256 of that descriptor.
.PARAMETER Platform x64 or ARM64 Release target. Cross-compilation establishes no native runtime result.
.PARAMETER BuildNumber Resource build number for this operation.
.OUTPUTS Fresh .build/local-unpinned operation, build log, actual link proof and local-only build record. The existing application test surface is unconditional.
.NOTES Requires local Windows, exact clean shared engine, Visual Studio and matching API/ABI. Run vcpkg-install.ps1 for the selected platform first. This command reads existing app dependencies and does not restore them; do not modify them during the build. It never replaces ordinary outputs, changes managed pins, runs tests, packages or grants qualification. Failed outputs are retained.
.EXAMPLE ./Tools/Build-LocalSdkCandidate.ps1 -Descriptor Z:/src/DxUi/.build/local-sdk-capture/example/local-unpinned-snapshot.json -DescriptorSha256 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
#>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Descriptor,
    [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string]$DescriptorSha256,
    [ValidateSet('x64','ARM64')][string]$Platform='x64',[ValidateRange(0,65535)][int]$BuildNumber=0)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if(-not $IsWindows -or $env:GITHUB_ACTIONS -ceq 'true' -or $env:CI_NATIVE_BUILDS_DISABLED -ceq 'true'){throw 'Local SDK candidate builds require local Windows native execution.'}
$root=Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $root 'Build/DxUiRestore.psm1')
Import-Module (Join-Path $root '.ci-engine/Tools/Modules/Ci.LocalSdkBuild.psm1') -ErrorAction Stop
$buildContract=[ordered]@{buildFile='RedXe.sln';metadataProject='RedXe/RedXe.vcxproj';target='RedXe';sdkProperty='RedXeDxUiSdkRoot';engineProperty='RedXeCiEngineRoot';outputProperty='RedXeOutputRoot';testsProperty='';versionProperty='RedXeBuildNumber';executable='RedXe.exe'}
$result=Invoke-CiLocalSdkApplicationBuild -ProjectRoot $root -Descriptor $Descriptor -ExpectedDescriptorSha256 $DescriptorSha256 -Platform $Platform -MSBuildPath (Find-RedXeMSBuild) -BuildContract $buildContract -BuildNumber $BuildNumber
Write-Host 'LOCAL_UNPINNED_BUILD: tests, repository and production qualification remain NOT_EVALUATED.'
$result
