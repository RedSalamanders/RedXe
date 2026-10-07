<#!
.SYNOPSIS
Installs the RedXe vcpkg manifest dependencies for x64, ARM64, or both.

.DESCRIPTION
Uses the repository and commit pinned by vcpkg-tool.json. The managed vcpkg checkout, downloads, build trees,
packages, and installed trees all live beneath .build. Each platform receives a private install root so one
manifest install cannot purge the other platform's package metadata.

vcpkg builds with the Visual Studio installation and MSVC toolset that MSBuild uses, not the newest toolset it finds.
The installation is the one that holds the MSBuild the build runs (MSBuildPath; run on its own, the script finds the
MSBuild build.ps1 would, through Find-RedXeMSBuild) and the toolset is that installation's default
(VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt); a missing or malformed version file fails before vcpkg is
cloned. Each platform gets an overlay triplet, .build\vcpkg-triplets\<platform>\<triplet>.cmake: the pinned vcpkg
checkout's triplet plus the two pins (VCPKG_VISUAL_STUDIO_PATH and VCPKG_PLATFORM_TOOLSET_VERSION), rewritten only when
its contents change. Changing the triplet changes vcpkg's package ABI hash, so the first install after a change rebuilds
the packages. The toolset reader and the overlay writer are DxUi's (Tools/VisualStudio.psm1 and Tools/VcpkgTriplet.psm1,
part of its consumer interface), imported from the pinned DxUi source only once that is the clean checkout of the pinned
commit: a missing, unfinished or changed restore is restored again first.

.PARAMETER Platform
Target platform: x64, ARM64, or All.

.PARAMETER MSBuildPath
The MSBuild the build runs; build.ps1 passes its own. vcpkg builds with the Visual Studio installation that holds it.
#>
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
$toolRoot = Join-Path $buildRoot 'vcpkg-tool'
$toolStampPath = Join-Path $buildRoot 'vcpkg-tool.commit'
$toolIdentityPath = Join-Path $repoRoot 'vcpkg-tool.json'
$manifestPath = Join-Path $repoRoot 'vcpkg.json'

if (-not (Get-Command 'git.exe' -ErrorAction SilentlyContinue)) {
    throw 'Git was not found. Install Git for Windows and ensure git.exe is on PATH.'
}
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "The vcpkg manifest was not found: $manifestPath"
}

$toolIdentity = Get-Content -Raw -LiteralPath $toolIdentityPath | ConvertFrom-Json
$repository = [string] $toolIdentity.repository
$commit = [string] $toolIdentity.commit
if ([string]::IsNullOrWhiteSpace($repository) -or $commit -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'vcpkg-tool.json must contain a repository URL and a full 40-character commit hash.'
}

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
Write-Host "Visual Studio: $installation" -ForegroundColor Cyan
Write-Host "MSVC toolset:  $($toolset.Version), the default MSBuild compiles with; vcpkg is pinned to $($toolset.MajorMinor)" -ForegroundColor Cyan

New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $toolRoot '.git') -PathType Container)) {
    if (Test-Path -LiteralPath $toolRoot) {
        throw "The managed vcpkg path exists but is not a Git checkout: $toolRoot"
    }

    Write-Host "Cloning pinned vcpkg tooling into $toolRoot" -ForegroundColor Cyan
    & git.exe clone --filter=blob:none $repository $toolRoot
    if ($LASTEXITCODE -ne 0) {
        throw "git clone failed with exit code $LASTEXITCODE."
    }
}

$origin = & git.exe -C $toolRoot remote get-url origin
if ($LASTEXITCODE -ne 0 -or $origin.TrimEnd('/') -ne $repository.TrimEnd('/')) {
    throw "The managed vcpkg checkout does not use the pinned repository: $repository"
}

$dirty = & git.exe -C $toolRoot status --porcelain
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to inspect the managed vcpkg checkout.'
}
if ($dirty) {
    throw "The managed vcpkg checkout contains local changes: $toolRoot"
}

$head = & git.exe -C $toolRoot rev-parse HEAD 2>$null
if ($LASTEXITCODE -ne 0 -or $head -ne $commit) {
    Write-Host "Selecting pinned vcpkg commit $commit" -ForegroundColor Cyan
    & git.exe -C $toolRoot fetch --depth 1 origin $commit
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to fetch pinned vcpkg commit $commit."
    }
    & git.exe -C $toolRoot checkout --detach $commit
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to check out pinned vcpkg commit $commit."
    }
}

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
    $overlay = Update-DxUiVcpkgOverlayTriplet -StockTripletPath (Join-Path $toolRoot "triplets/$triplet.cmake") -Toolset $toolset `
        -OutputDirectory (Join-Path $buildRoot "vcpkg-triplets\$platformScope")
    Write-Host "Overlay triplet $($overlay.Path) $(if ($overlay.Changed) { 'written' } else { 'unchanged' })" -ForegroundColor Cyan

    Write-Host "Installing RedXe dependencies ($triplet)" -ForegroundColor Cyan
    $arguments = @(
        'install',
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
