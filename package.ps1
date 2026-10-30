<#!
.SYNOPSIS
Builds the portable ZIP package for one platform and validates it from a clean extraction.

.DESCRIPTION
Runs build.ps1 for the Release profile (unless -SkipBuild), stages the shipping files described in
Specs/Build/Build_Packaging.md, bundles the Visual C++ runtime, adds the in-package installer, writes
.build/packages/RedXe-<version>-<Platform>-Portable.zip with a .sha256 sidecar, and accepts the archive only after
expanding it into a fresh directory and running the packaged RedXe.exe --self-test --warp and RedXeLauncher.exe
--help from there (execution is skipped when this host cannot run the platform).

Then writes .build/packages/RedXe-<version>-<Platform>-Symbols.zip, with the same kind of sidecar: the exact PDB of
every first-party executable and DLL the portable ZIP ships, taken from the same Release output, for symbolizing the
minidumps of that release. Packaging fails, naming the binary, when such a PDB is missing or older than its binary.

.PARAMETER Platform
x64 or ARM64.

.PARAMETER BuildNumber
Third version component (Common/Version.h supplies major.minor). Default 0 means the commit count of HEAD, which is
also what the release workflow stamps.

.PARAMETER SkipBuild
Package existing production-only Release output, with matching role provenance and -BuildNumber.

.PARAMETER SkipExecution
Validate the archive's contents only; do not start the packaged executables.

.EXAMPLE
.\package.ps1 -Platform x64 -BuildNumber 42
#>
#Requires -Version 7.0
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
Import-Module (Join-Path $repoRoot 'Build\Versioning.psm1') -Force
$BuildNumber = Resolve-RedXeBuildNumber -RepoRoot $repoRoot -Requested $BuildNumber
if (-not $SkipBuild) {
    & (Join-Path $repoRoot 'build.ps1') -Configuration Release -Platform $Platform -BuildNumber $BuildNumber -ProductionOnly
    if ($LASTEXITCODE -ne 0) { throw "Build entrypoint failed with exit code $LASTEXITCODE." }
}
Import-Module (Join-Path $repoRoot 'Build/DxUiProvenance.psm1')
Assert-RedXeDxUiProvenance -OutputRoot (Join-Path $repoRoot ".build/$Platform/Release") -LockFile (Join-Path $repoRoot 'Dependencies/DxUi.lock.json') -Platform $Platform -Configuration Release -ProductionOnly

Import-Module (Join-Path $repoRoot 'Build\Package.psm1') -Force
Write-Host "Packaging Release | $Platform ..." -ForegroundColor Cyan
$package = New-RedXePortablePackage -RepoRoot $repoRoot -Platform $Platform -BuildNumber $BuildNumber -SkipExecution:$SkipExecution
Write-Host "Archiving symbols Release | $Platform ..." -ForegroundColor Cyan
$symbols = New-RedXeSymbolArchive -RepoRoot $repoRoot -Version $package.Version -Platform $Platform -PackageEntries $package.Entries `
    -OutputDirectory (Split-Path -Parent $package.ZipPath)
Write-Host "Package: $($package.ZipPath)" -ForegroundColor Green
Write-Host "SHA256:  $($package.Sha256)" -ForegroundColor DarkGray
Write-Host ("Size:    {0:N2} MB, {1} files" -f ((Get-Item -LiteralPath $package.ZipPath).Length / 1MB), $package.Entries.Count) -ForegroundColor DarkGray
Write-Host "Symbols: $($symbols.ZipPath)" -ForegroundColor Green
Write-Host "SHA256:  $($symbols.Sha256)" -ForegroundColor DarkGray
Write-Host ("Size:    {0:N2} MB, {1} PDBs" -f ((Get-Item -LiteralPath $symbols.ZipPath).Length / 1MB), $symbols.Entries.Count) -ForegroundColor DarkGray
if ($symbols.Excluded.Count) {
    Write-Host ("No PDB (third-party): {0}" -f ((@($symbols.Excluded | Split-Path -Leaf) | Sort-Object -Unique) -join ', ')) -ForegroundColor DarkGray
}
exit 0
