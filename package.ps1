<#!
.SYNOPSIS
Builds the portable ZIP package for one platform and validates it from a clean extraction.

.DESCRIPTION
Runs build.ps1 for the Release profile (unless -SkipBuild), stages the shipping files described in
Specs/Build/Build_Packaging.md, bundles the Visual C++ runtime, adds the in-package installer, writes
.build/packages/RedXe-<version>-<Platform>-Portable.zip with a .sha256 sidecar, and accepts the archive only after
expanding it into a fresh directory and running the packaged RedXe.exe --self-test --warp and RedXeLauncher.exe
--help from there (execution is skipped when this host cannot run the platform).

.PARAMETER Platform
x64 or ARM64.

.PARAMETER BuildNumber
Third version component (Common/Version.h supplies major.minor). 0 makes a local 1.0.0 package that winget-manifest.ps1
refuses; the release workflow passes GITHUB_RUN_NUMBER.

.PARAMETER SkipBuild
Package the existing .build/<Platform>/Release output, which must already be stamped with -BuildNumber.

.PARAMETER SkipExecution
Validate the archive's contents only; do not start the packaged executables.

.EXAMPLE
.\package.ps1 -Platform x64 -BuildNumber 42
#>
[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64')]
    [string] $Platform = 'x64',

    [ValidateRange(0, 65535)]
    [int] $BuildNumber = 0,

    [switch] $SkipBuild,
    [switch] $SkipExecution
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSCommandPath
if (-not $SkipBuild) {
    & (Join-Path $repoRoot 'build.ps1') -Configuration Release -Platform $Platform -BuildNumber $BuildNumber
    if ($LASTEXITCODE -ne 0) { throw "Build entrypoint failed with exit code $LASTEXITCODE." }
}

Import-Module (Join-Path $repoRoot 'Build\Package.psm1') -Force
Write-Host "Packaging Release | $Platform ..." -ForegroundColor Cyan
$package = New-RedXePortablePackage -RepoRoot $repoRoot -Platform $Platform -BuildNumber $BuildNumber -SkipExecution:$SkipExecution
Write-Host "Package: $($package.ZipPath)" -ForegroundColor Green
Write-Host "SHA256:  $($package.Sha256)" -ForegroundColor DarkGray
Write-Host ("Size:    {0:N2} MB, {1} files" -f ((Get-Item -LiteralPath $package.ZipPath).Length / 1MB), $package.Entries.Count) -ForegroundColor DarkGray
exit 0
