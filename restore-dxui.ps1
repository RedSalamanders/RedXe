<# .SYNOPSIS Restore the exact DxUi source pin (a sparse checkout, with Git long paths) and isolated build dependencies without modifying a sibling checkout. #>
#Requires -Version 7.0
[CmdletBinding()]
param([ValidateSet('x64','ARM64')][string] $Platform = 'x64', [string] $MSBuildPath = '', [switch] $CheckUpdates)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Build/DxUiPaths.psm1') -Scope Local -ErrorAction Stop
# Use one compiler host for dependency identity, restore, compilation and provenance.
Import-Module (Join-Path $PSScriptRoot 'Build/DxUiRestore.psm1') -Force -ErrorAction Stop
$compilerHost = Enter-RedXeCompilerHost
# Captured now: a module that re-imports DxUiRestore with -Force removes this script's copy of the module.
$exitCompilerHost = Get-Command Exit-RedXeCompilerHost
try {
# The lock, the verified sparse long-path restore of its exact commit, the sibling-checkout rule, the folder names and the MSBuild
# the build runs live in one module, which build.ps1 and vcpkg-install.ps1 use too: vcpkg-install.ps1 imports the pinned source's
# Visual Studio and vcpkg helpers.
$restored = Restore-RedXeDxUiPin -RepoRoot $PSScriptRoot
$pinPath = $restored.LockFile
$pin = $restored.Pin
$source = $restored.Source
$dependencyRoot = Get-RedXeDxUiDependencyRoot -RepoRoot $PSScriptRoot
& (Join-Path $source 'Tools/validate_consumer.ps1') -DxUiRoot $source -LockFile $pinPath
# Run on its own, the script identifies the MSBuild build.ps1 would pass, so the identity does not depend on which ran last.
if (-not $MSBuildPath) { $MSBuildPath = Find-RedXeMSBuild }
Import-Module (Join-Path $source 'Tools/ConsumerBuild.psm1') -Force
# The pinned probe evaluates the library alone, under the compiler host selected above for the whole restore.
$buildIdentity=Get-DxUiConsumerBuildIdentity -DxUiRoot $source -MSBuildPath $MSBuildPath -Platform $Platform
# A changed compiler policy owns a separate writable archive output, even when the API/source pin stays unchanged.
$requiredTools = Get-RedXeRequiredMsvcFamily -RepoRoot $PSScriptRoot
if (-not (Test-RedXeMsvcFamily -Required $requiredTools -Version $buildIdentity.Identity.vcToolsVersion)) {
    throw "This product requires MSVC $requiredTools.x; selected $($buildIdentity.Identity.vcToolsVersion)."
}
$policyIdentity = [ordered]@{}
if ($buildIdentity.Identity -is [Collections.IDictionary]) {
    foreach ($entry in $buildIdentity.Identity.GetEnumerator()) { $policyIdentity[$entry.Key] = $entry.Value }
} else {
    foreach ($entry in $buildIdentity.Identity.PSObject.Properties) { $policyIdentity[$entry.Name] = $entry.Value }
}
$policyIdentity['releaseOptimizationPolicy'] = 'Gy-REF-GL-LTCG-full-v1'
foreach($field in (Get-DxUiNativePolicyFields -RepoRoot $PSScriptRoot).GetEnumerator()){$policyIdentity[$field.Key]=$field.Value}
$policyHasher = [Security.Cryptography.SHA256]::Create()
try { $policyFingerprint = ([BitConverter]::ToString($policyHasher.ComputeHash([Text.Encoding]::UTF8.GetBytes(($policyIdentity | ConvertTo-Json -Depth 8 -Compress))))).Replace('-','').ToLowerInvariant() }
finally { $policyHasher.Dispose() }
$buildIdentity = [pscustomobject]@{ Fingerprint=$policyFingerprint; Identity=$policyIdentity }
# The root is leased before it is checked or created, as the source was by Restore-RedXeDxUiPin: no removal of superseded restores,
# this session's or another's, takes either for the lease window, even once another fingerprint's properties replace ours. Its
# marker is checked or written under the root's own mutex, so parallel restores of one new fingerprint all get it.
$output = Initialize-RedXeDxUiOutputRoot -RepoRoot $PSScriptRoot -BuildIdentity $buildIdentity
# DxUi's own vcpkg-install.ps1 takes no MSBuild: it builds DxUi's dependencies with the newest Visual Studio installation that has
# MSBuild, which can differ from the build's (Core_DxUiIntegration.md).
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
# Now that this platform's properties name the root it builds with: remove the roots and source clones earlier restores left.
Remove-RedXeDxUiSupersededRestores -RepoRoot $PSScriptRoot -Commit $pin.commit
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

} finally {
    & $exitCompilerHost -State $compilerHost
}
