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
the packages. The toolset reader and the overlay writer are DxUi's (Tools/VisualStudio.psm1 and Tools/VcpkgTriplet.psm1,
part of its consumer interface), imported from the pinned DxUi source only once that is the clean checkout of the pinned
commit: a missing, unfinished or changed restore is restored again first.

.PARAMETER Platform
Target platform: x64, ARM64, or All.

.PARAMETER MSBuildPath
The MSBuild the build runs; build.ps1 passes its own. vcpkg builds with the Visual Studio installation that holds it.

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

    [string] $MSBuildPath = ''
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
# so a broken installation fails at once and every triplet below is pinned to what MSBuild uses. DxUi's helpers come from the
# pinned source, which Restore-RedXeDxUiPin returns only as the clean checkout of the pinned commit (restored first, or again,
# as restore-dxui.ps1 would), so build.ps1 keeps its order: dependencies, then the DxUi restore.
Import-Module (Join-Path $repoRoot 'Build/DxUiRestore.psm1') -Force
$dxUi = Restore-RedXeDxUiPin -RepoRoot $repoRoot
Import-Module (Join-Path $dxUi.Source 'Tools/VisualStudio.psm1') -Force
Import-Module (Join-Path $dxUi.Source 'Tools/VcpkgTriplet.psm1') -Force
if (-not $MSBuildPath) { $MSBuildPath = Find-RedXeMSBuild }
$installation = Get-RedXeVisualStudioInstallation -MSBuildPath $MSBuildPath
$toolset = Get-DxUiDefaultToolset -Installation $installation
$requiredTools = Get-RedXeRequiredMsvcFamily -RepoRoot $repoRoot
if (-not (Test-RedXeMsvcFamily -Required $requiredTools -Version $toolset.Version)) { throw "This product requires default MSVC $requiredTools.x; selected $($toolset.Version)." }
Write-Host "Visual Studio: $installation" -ForegroundColor Cyan
Write-Host "MSVC toolset:  $($toolset.Version), the default MSBuild compiles with; vcpkg is pinned to $($toolset.MajorMinor)" -ForegroundColor Cyan

$sources = Initialize-RedXeVcpkgSources -RepoRoot $repoRoot
$toolRoot = $sources.ToolRoot
$registryRoot = $sources.RegistryRoot
if ($sources.ToolCommit -cne $commit -or $sources.Baseline -cne $toolIdentity.Baseline) { throw 'vcpkg pin declarations changed during dependency setup.' }
$vcpkg = Join-Path $toolRoot 'vcpkg.exe'
$stampedCommit = if (Test-Path -LiteralPath $toolStampPath -PathType Leaf) {
    (Get-Content -Raw -LiteralPath $toolStampPath).Trim()
} else {
    ''
}
if (-not (Test-Path -LiteralPath $vcpkg -PathType Leaf) -or $stampedCommit -ne $commit) {
    Write-Host 'Bootstrapping vcpkg...' -ForegroundColor Cyan
    & (Join-Path $toolRoot 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg bootstrap failed with exit code $LASTEXITCODE."
    }
    Set-Content -LiteralPath $toolStampPath -Value $commit -Encoding ascii
}

$platforms = if ($Platform -eq 'All') { @('x64', 'ARM64') } else { @($Platform) }
foreach ($targetPlatform in $platforms) {
    $platformScope = $targetPlatform.ToLowerInvariant()
    $triplet = if ($targetPlatform -eq 'ARM64') { 'arm64-windows' } else { 'x64-windows' }
    $installRoot = Join-Path $buildRoot "vcpkg_installed\$platformScope"
    $buildTreesRoot = Join-Path $buildRoot "vcpkg_buildtrees\$platformScope"
    $packagesRoot = Join-Path $buildRoot "vcpkg_packages\$platformScope"
    $downloadsRoot = Join-Path $buildRoot 'vcpkg_downloads'
    # The pinned checkout's own triplet, copied and pinned; vcpkg takes an overlay in place of the triplet of the same name.
    $overlay = Update-DxUiVcpkgOverlayTriplet -StockTripletPath (Join-Path $registryRoot "triplets/$triplet.cmake") -Toolset $toolset `
        -OutputDirectory (Join-Path $buildRoot "vcpkg-triplets\$platformScope")
    Write-Host "Overlay triplet $($overlay.Path) $(if ($overlay.Changed) { 'written' } else { 'unchanged' })" -ForegroundColor Cyan

    Write-Host "Installing RedXe dependencies ($triplet)" -ForegroundColor Cyan
    $arguments = @(
        'install',
        "--vcpkg-root=$registryRoot",
        "--triplet=$triplet",
        "--x-manifest-root=$repoRoot",
        "--x-install-root=$installRoot",
        "--x-buildtrees-root=$buildTreesRoot",
        "--x-packages-root=$packagesRoot",
        "--downloads-root=$downloadsRoot",
        "--overlay-triplets=$($overlay.Directory)"
    )
    & $vcpkg @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg install failed for $triplet with exit code $LASTEXITCODE."
    }
}

Write-Host 'vcpkg dependencies are ready.' -ForegroundColor Green
