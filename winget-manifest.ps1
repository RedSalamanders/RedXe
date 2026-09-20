<#!
.SYNOPSIS
Generates and validates the winget manifest for one release from its two portable ZIPs.

.DESCRIPTION
Writes RedSalamanders.RedXe.yaml, .installer.yaml, and .locale.en-US.yaml under
.build/packages/winget-manifest/<version>/ from Installer/winget/templates, filling in the version, the release
date, and the SHA256 of both packages, then runs `winget validate --manifest` on the result. Both packages must be
named exactly as package.ps1 names them because the manifest's InstallerUrl is derived from the version.

.PARAMETER Version
Three-part release version, for example 1.0.42. Its build component must be positive.

.PARAMETER X64ZipPath
Path to RedXe-<Version>-x64-Portable.zip. Default: the package.ps1 output.

.PARAMETER Arm64ZipPath
Path to RedXe-<Version>-ARM64-Portable.zip. Default: the package.ps1 output.

.PARAMETER ReleaseDate
Manifest ReleaseDate as yyyy-MM-dd. Default: today (UTC). The release workflow passes the GitHub release date.

.PARAMETER SkipValidation
Write the manifest without running winget validate (for hosts without winget).

.EXAMPLE
.\winget-manifest.ps1 -Version 1.0.42
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $Version,

    [string] $X64ZipPath,
    [string] $Arm64ZipPath,
    [string] $ReleaseDate,
    [string] $OutputDirectory,
    [switch] $SkipValidation
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSCommandPath
Import-Module (Join-Path $repoRoot 'Build\Package.psm1') -Force
Import-Module (Join-Path $repoRoot 'Build\Winget.psm1') -Force

$packages = Join-Path $repoRoot '.build\packages'
if (-not $X64ZipPath) { $X64ZipPath = Join-Path $packages (Get-RedXePackageName -Version $Version -Platform x64) }
if (-not $Arm64ZipPath) { $Arm64ZipPath = Join-Path $packages (Get-RedXePackageName -Version $Version -Platform ARM64) }

$manifest = New-RedXeWingetManifest -RepoRoot $repoRoot -Version $Version -X64ZipPath $X64ZipPath -Arm64ZipPath $Arm64ZipPath -ReleaseDate $ReleaseDate -OutputDirectory $OutputDirectory
Write-Host "Manifest: $($manifest.OutputDirectory)" -ForegroundColor Green
$manifest.Files | ForEach-Object { Write-Host "  $(Split-Path -Leaf $_)" -ForegroundColor DarkGray }
if (-not $SkipValidation) {
    Test-RedXeWingetManifest -ManifestDirectory $manifest.OutputDirectory | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
    Write-Host 'winget validate passed.' -ForegroundColor Green
}
Write-Host "Local install test: winget settings --enable LocalManifestFiles; winget install --manifest `"$($manifest.OutputDirectory)`"" -ForegroundColor DarkGray
exit 0
