<# .SYNOPSIS Restore the exact DxUi source pin (a sparse checkout, with Git long paths) and isolated build dependencies without modifying a sibling checkout. #>
[CmdletBinding()]
param([ValidateSet('x64','ARM64')][string] $Platform = 'x64', [string] $MSBuildPath = '', [switch] $CheckUpdates)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# The lock, the sparse long-path clone of its exact commit and the sibling-checkout rule live in one module, which
# vcpkg-install.ps1 uses too: it imports the pinned source's Visual Studio and vcpkg helpers.
Import-Module (Join-Path $PSScriptRoot 'Build/DxUiRestore.psm1') -Force
$restored = Restore-RedXeDxUiPin -RepoRoot $PSScriptRoot
$pinPath = $restored.LockFile
$pin = $restored.Pin
$source = $restored.Source
$dependencyRoot = Join-Path $PSScriptRoot '.build/dependencies/DxUi'
& (Join-Path $source 'Tools/validate_consumer.ps1') -DxUiRoot $source -LockFile $pinPath
if (-not $MSBuildPath) {
    if ($env:MSBUILD_EXE_PATH) { $MSBuildPath=$env:MSBUILD_EXE_PATH }
    else {
        $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $installation=& $vswhere -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild -property installationPath
        $MSBuildPath=Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
    }
}
Import-Module (Join-Path $source 'Tools/ConsumerBuild.psm1') -Force
$buildIdentity=Get-DxUiConsumerBuildIdentity -DxUiRoot $source -MSBuildPath $MSBuildPath -Platform $Platform
# A 16-digit fingerprint prefix names the folder: under the full 64 digits, vcpkg's deepest tool files pass MAX_PATH.
# The full fingerprint stays in DxUi.identity.<Platform>.json and the product provenance.
$output = (Join-Path $dependencyRoot $buildIdentity.Fingerprint.Substring(0, 16)) + [IO.Path]::DirectorySeparatorChar
& (Join-Path $source 'vcpkg-install.ps1') -Platform $Platform -OutputRoot $output
$escape = { param([string] $value) [System.Security.SecurityElement]::Escape($value) }
$sourceXml = & $escape $source
$outputXml = & $escape $output
$pinXml = & $escape $pinPath
$identityProperties = '<DxUiConsumerToolset>' + (& $escape $buildIdentity.Identity.toolset) + '</DxUiConsumerToolset>' +
    '<DxUiConsumerVCToolsVersion>' + (& $escape $buildIdentity.Identity.vcToolsVersion) + '</DxUiConsumerVCToolsVersion>' +
    '<DxUiConsumerSdkVersion>' + (& $escape $buildIdentity.Identity.windowsSdkVersion) + '</DxUiConsumerSdkVersion>' +
    '<DxUiConsumerPreferredToolArchitecture>' + (& $escape $buildIdentity.Identity.preferredToolArchitecture) + '</DxUiConsumerPreferredToolArchitecture>'
$props = "<Project><PropertyGroup>$identityProperties<DxUiRoot>$sourceXml</DxUiRoot><DxUiConsumerOutputRoot>$outputXml</DxUiConsumerOutputRoot><DxUiConsumerLockFile>$pinXml</DxUiConsumerLockFile></PropertyGroup></Project>"
$propsPath = Join-Path $dependencyRoot "DxUi.resolved.$Platform.props"
if (-not (Test-Path -LiteralPath $propsPath) -or [IO.File]::ReadAllText($propsPath) -ne $props) {
    [IO.File]::WriteAllText($propsPath, $props, [Text.UTF8Encoding]::new($false))
}
$identityPath=Join-Path $dependencyRoot "DxUi.identity.$Platform.json"
$identityJson=$buildIdentity | ConvertTo-Json -Depth 5
if (-not (Test-Path -LiteralPath $identityPath) -or [IO.File]::ReadAllText($identityPath) -ne $identityJson) {
    [IO.File]::WriteAllText($identityPath,$identityJson,[Text.UTF8Encoding]::new($false))
}
if ($CheckUpdates) {
    Import-Module (Join-Path $source 'Tools/ConsumerUpdate.psm1') -Force
    Import-Module (Join-Path $PSScriptRoot 'Build/BuildPresentation.psm1') -Force
    # The pinned DxUi helper owns the read-only update decision. RedXe owns only
    # the interactive severity color and product regression command for its notice.
    foreach ($record in @(Show-DxUiUpdateNotice -LockFile $pinPath 6>&1)) {
        $notice = if ($record -is [Management.Automation.InformationRecord]) {
            [string] $record.MessageData
        }
        else {
            [string] $record
        }
        $foregroundColor = Get-RedXeDxUiUpdateNoticeForegroundColor -Notice $notice
        if ($foregroundColor) {
            Write-Host (Format-RedXeDxUiUpdateNotice -Notice $notice) -ForegroundColor $foregroundColor
        }
    }
}
Write-Host "DxUi restored at exact pin $($pin.commit); sibling checkout unchanged."
