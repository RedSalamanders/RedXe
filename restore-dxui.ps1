<# .SYNOPSIS Restore the exact DxUi source pin (a sparse checkout, with Git long paths) and isolated build dependencies without modifying a sibling checkout. #>
[CmdletBinding()]
param([ValidateSet('x64','ARM64')][string] $Platform = 'x64', [string] $MSBuildPath = '', [switch] $CheckUpdates)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# The lock, the verified sparse long-path restore of its exact commit, the sibling-checkout rule, the folder names and the MSBuild
# the build runs live in one module, which build.ps1 and vcpkg-install.ps1 use too: vcpkg-install.ps1 imports the pinned source's
# Visual Studio and vcpkg helpers.
Import-Module (Join-Path $PSScriptRoot 'Build/DxUiRestore.psm1') -Force
$restored = Restore-RedXeDxUiPin -RepoRoot $PSScriptRoot
$pinPath = $restored.LockFile
$pin = $restored.Pin
$source = $restored.Source
$dependencyRoot = Get-RedXeDxUiDependencyRoot -RepoRoot $PSScriptRoot
& (Join-Path $source 'Tools/validate_consumer.ps1') -DxUiRoot $source -LockFile $pinPath
# Run on its own, the script identifies the MSBuild build.ps1 would pass, so the identity does not depend on which ran last.
if (-not $MSBuildPath) { $MSBuildPath = Find-RedXeMSBuild }
Import-Module (Join-Path $source 'Tools/ConsumerBuild.psm1') -Force
# The old pinned probe evaluates the library alone. Match the product's ARM64 host default
# while preserving every caller environment value even when probing throws.
$callerCompilerHost=[Environment]::GetEnvironmentVariable('PreferredToolArchitecture','Process')
try {
    if($Platform-eq 'ARM64' -and [string]::IsNullOrWhiteSpace($callerCompilerHost)){
        $env:PreferredToolArchitecture=if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()-eq 'Arm64'){'arm64'}else{'x64'}
    }
    $buildIdentity=Get-DxUiConsumerBuildIdentity -DxUiRoot $source -MSBuildPath $MSBuildPath -Platform $Platform
} finally {
    [Environment]::SetEnvironmentVariable('PreferredToolArchitecture',$callerCompilerHost,'Process')
}
# A changed compiler policy owns a separate writable archive output, even when the API/source pin stays unchanged.
[xml]$msvcPolicy = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'Directory.Build.props')
$requiredTools = $msvcPolicy.SelectSingleNode('/Project/PropertyGroup/RequiredMsvcToolsVersion').InnerText
if (-not ([string]$buildIdentity.Identity.vcToolsVersion).StartsWith($requiredTools + '.', [StringComparison]::Ordinal)) {
    throw "This product requires MSVC $requiredTools.x; selected $($buildIdentity.Identity.vcToolsVersion)."
}
$policyIdentity = [ordered]@{}
if ($buildIdentity.Identity -is [Collections.IDictionary]) {
    foreach ($entry in $buildIdentity.Identity.GetEnumerator()) { $policyIdentity[$entry.Key] = $entry.Value }
} else {
    foreach ($entry in $buildIdentity.Identity.PSObject.Properties) { $policyIdentity[$entry.Name] = $entry.Value }
}
$policyIdentity['releaseOptimizationPolicy'] = 'Gy-REF-GL-LTCG-full-v1'
$policyHasher = [Security.Cryptography.SHA256]::Create()
try { $policyFingerprint = ([BitConverter]::ToString($policyHasher.ComputeHash([Text.Encoding]::UTF8.GetBytes(($policyIdentity | ConvertTo-Json -Depth 8 -Compress))))).Replace('-','').ToLowerInvariant() }
finally { $policyHasher.Dispose() }
$buildIdentity = [pscustomobject]@{ Fingerprint=$policyFingerprint; Identity=$policyIdentity }
$output = Get-RedXeDxUiOutputRoot -RepoRoot $PSScriptRoot -Fingerprint $buildIdentity.Fingerprint
# The root's lease is renewed before anything uses it, the source's was by Restore-RedXeDxUiPin: no removal of superseded
# restores, this session's or another's, takes either for the lease window, even once another fingerprint's properties replace ours.
Update-RedXeDxUiLease -RepoRoot $PSScriptRoot -Path $output
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
