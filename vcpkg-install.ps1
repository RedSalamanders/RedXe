<#!
.SYNOPSIS
Installs the RedXe vcpkg manifest dependencies for x64, ARM64, or both.

.DESCRIPTION
Uses vcpkg-tool.json for the executable and vcpkg.json's independent builtin-baseline for the ports catalogue.
Build/VcpkgRestore.psm1 verifies both managed checkouts. --vcpkg-root selects the catalogue explicitly; the executable
remains in .build/vcpkg-tool and .build/vcpkg-registry supplies ports, versions and stock triplets.
The managed vcpkg checkouts, downloads, build trees,
packages, and installed trees all live beneath .build. Each platform receives a private install root so one
manifest install cannot purge the other platform's package metadata.

vcpkg builds with the Visual Studio installation and MSVC toolset that MSBuild uses, not the newest toolset it finds.
The installation is the one that holds the MSBuild the build runs (MSBuildPath; run on its own, the script finds the
MSBuild build.ps1 would, through Find-RedXeMSBuild) and the toolset is that installation's default
(VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt); a missing or malformed version file fails before vcpkg is
cloned. Each platform gets an overlay triplet, .build\vcpkg-triplets\<platform>\<triplet>.cmake: the pinned vcpkg
catalogue's triplet plus the two pins (VCPKG_VISUAL_STUDIO_PATH and VCPKG_PLATFORM_TOOLSET_VERSION), rewritten only when
its contents change. Changing the triplet changes vcpkg's package ABI hash, so the first install after a change rebuilds
the packages. The pinned CI engine reads the selected toolset, generates the overlay and validates the manifest
install arguments. The separate DxUi restore continues to use its own compatible consumer bootstrap.

.PARAMETER Platform
Target platform: x64, ARM64, or All.

.PARAMETER MSBuildPath
The MSBuild the build runs; build.ps1 passes its own. vcpkg builds with the Visual Studio installation that holds it.

.PARAMETER Asan
Install instrumented dependencies in the separate ASan Debug tree for the selected platform.

.OUTPUTS
Dependency setup progress; platform-private installed packages beneath .build.
.NOTES
Requires PowerShell 7, Git and the pinned native toolchain. Clones, bootstraps, downloads and builds beneath .build.
Refuses dirty or foreign managed sources. Does not edit pins or the caller's VCPKG_ROOT environment variable.
.EXAMPLE
./vcpkg-install.ps1 -Platform ARM64
#>
#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64', 'All')]
    [string] $Platform = 'x64',

    [string] $MSBuildPath = '',

    [switch] $Asan
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSCommandPath
$buildRoot = Join-Path $repoRoot '.build'
$toolStampPath = Join-Path $buildRoot 'vcpkg-tool.commit'
$manifestPath = Join-Path $repoRoot 'vcpkg.json'

if (-not (Get-Command 'git.exe' -ErrorAction SilentlyContinue)) {
    throw 'Git was not found. Install Git for Windows and ensure git.exe is on PATH.'
}
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "The vcpkg manifest was not found: $manifestPath"
}

Import-Module (Join-Path $repoRoot 'Build/VcpkgRestore.psm1') -Force
$toolIdentity = Get-RedXeVcpkgIdentity -RepoRoot $repoRoot
$commit = $toolIdentity.ToolCommit

# vcpkg picks the newest MSVC toolset of the Visual Studio installation it prefers; MSBuild compiles with the default
# toolset of the installation the build runs. Find both the way the build does, here, before vcpkg is cloned or bootstrapped,
# so a broken installation fails at once and every triplet below is pinned to what MSBuild uses.
Import-Module (Join-Path $repoRoot 'Build/DxUiRestore.psm1') -Force
Import-Module (Join-Path $repoRoot '.ci-engine/Tools/Modules/Ci.EngineBinding.psm1') -ErrorAction Stop
foreach ($name in @('VisualStudio.psm1', 'VcpkgTriplet.psm1', 'VcpkgCheckout.psm1', 'VcpkgInstall.psm1')) {
    Import-Module (Resolve-CiPinnedEngineModule -ProjectRoot $repoRoot -Name $name) -Scope Local -ErrorAction Stop
}
if (-not $MSBuildPath) { $MSBuildPath = Find-RedXeMSBuild }
$installation = Get-RedXeVisualStudioInstallation -MSBuildPath $MSBuildPath
$toolset = Get-CiDefaultMsvcToolset -Installation $installation
$requiredTools = Get-RedXeRequiredMsvcFamily -RepoRoot $repoRoot
if (-not (Test-RedXeMsvcFamily -Required $requiredTools -Version $toolset.Version)) { throw "This product requires default MSVC $requiredTools.x; selected $($toolset.Version)." }
Write-Host "Visual Studio: $installation" -ForegroundColor Cyan
Write-Host "MSVC toolset:  $($toolset.Version), the default MSBuild compiles with; vcpkg is pinned to $($toolset.MajorMinor)" -ForegroundColor Cyan

$sources = Initialize-RedXeVcpkgSources -RepoRoot $repoRoot
$toolRoot = $sources.ToolRoot
$registryRoot = $sources.RegistryRoot
if ($sources.ToolCommit -cne $commit -or $sources.Baseline -cne $toolIdentity.Baseline) { throw 'vcpkg pin declarations changed during dependency setup.' }
$vcpkg = Initialize-CiVcpkgExecutable -ProjectRoot $repoRoot -ToolRoot $toolRoot -StampPath $toolStampPath -Commit $commit

$platforms = if ($Platform -eq 'All') { @('x64', 'ARM64') } else { @($Platform) }
foreach ($targetPlatform in $platforms) {
    $platformScope = $targetPlatform.ToLowerInvariant()
    $triplet = if ($targetPlatform -eq 'ARM64') { 'arm64-windows' } else { 'x64-windows' }
    if ($Asan) {
        $platformScope += '-asan'
        $triplet += '-asan'
        $tripletSource = Join-Path $repoRoot "Build/vcpkg-triplets/$triplet.cmake"
    } else {
        $tripletSource = Join-Path $registryRoot "triplets/$triplet.cmake"
    }
    $installRoot = Join-Path $buildRoot "vcpkg_installed\$platformScope"
    $buildTreesRoot = Join-Path $buildRoot "vcpkg_buildtrees\$platformScope"
    $packagesRoot = Join-Path $buildRoot "vcpkg_packages\$platformScope"
    $downloadsRoot = Join-Path $buildRoot 'vcpkg_downloads'
    # Keep the overlay header stable so standard triplets retain their package identity.
    $header = '# DxUi: build with the Visual Studio installation and default MSVC toolset MSBuild uses (generated by vcpkg-install.ps1; do not edit).'
    $overlay = Update-CiVcpkgOverlayTriplet -ProjectRoot $repoRoot -SourceTripletPath $tripletSource `
        -Toolset $toolset -OutputDirectory (Join-Path $buildRoot "vcpkg-triplets\$platformScope") -Header $header
    Write-Host "Overlay triplet $($overlay.Path) $(if ($overlay.Changed) { 'written' } else { 'unchanged' })" -ForegroundColor Cyan

    Write-Host "Installing RedXe dependencies ($triplet)" -ForegroundColor Cyan
    $arguments = Get-CiVcpkgManifestInstallArguments -ProjectRoot $repoRoot -Triplet $triplet -RegistryRoot $registryRoot `
        -InstallRoot $installRoot -BuildTreesRoot $buildTreesRoot -PackagesRoot $packagesRoot `
        -DownloadsRoot $downloadsRoot -OverlayTriplets $overlay.Directory
    if ($Asan) {
        # The diagnostic tree stays in this repository, including its package state.
        $arguments += '--binarysource=clear'
    }
    & $vcpkg @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg install failed for $triplet with exit code $LASTEXITCODE."
    }
}

Write-Host 'vcpkg dependencies are ready.' -ForegroundColor Green
