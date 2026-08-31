<#!
.SYNOPSIS
Builds, cleans, rebuilds, or runs the RedXe solution.

.DESCRIPTION
Locates a Visual Studio MSBuild installation (including prerelease Visual Studio instances), rejects a running RedXe
process whose executable is the exact selected target output, builds the requested configuration and platform,
and writes all outputs beneath .build.

.PARAMETER Configuration
Build configuration: Debug or Release.

.PARAMETER Platform
Target platform: x64 or ARM64.

.PARAMETER Clean
Runs the MSBuild Clean target.

.PARAMETER Rebuild
Runs the MSBuild Rebuild target.

.PARAMETER Run
Launches the resulting application after a successful build. Valid only for the current host architecture.

.PARAMETER MaxCpuCount
Maximum MSBuild worker count. Zero uses MSBuild's default.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',

    [ValidateSet('x64', 'ARM64')]
    [string] $Platform = 'x64',

    [switch] $Clean,
    [switch] $Rebuild,
    [switch] $Run,

    [ValidateRange(0, 256)]
    [int] $MaxCpuCount = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Clean -and $Rebuild) {
    throw 'Choose either -Clean or -Rebuild, not both.'
}
if ($Clean -and $Run) {
    throw '-Run cannot be combined with -Clean.'
}

$repoRoot = Split-Path -Parent $PSCommandPath
$solutionPath = Join-Path $repoRoot 'RedXe.sln'
$executable = Join-Path $repoRoot ".build\$Platform\$Configuration\RedXe.exe"
$buildOutputProcessModule = Join-Path $repoRoot 'Build\BuildOutputProcess.psm1'
Import-Module $buildOutputProcessModule -Force -ErrorAction Stop
Assert-BuildOutputProcessNotRunning -ProcessName 'RedXe.exe' -ExpectedExecutablePath $executable

function Find-MSBuild {
    if ($env:MSBUILD_EXE_PATH -and (Test-Path -LiteralPath $env:MSBUILD_EXE_PATH -PathType Leaf)) {
        return $env:MSBUILD_EXE_PATH
    }

    $pathCommand = Get-Command 'msbuild.exe' -ErrorAction SilentlyContinue
    if ($pathCommand) {
        return $pathCommand.Source
    }

    $vswhereCandidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }

    foreach ($vswhere in $vswhereCandidates) {
        $matches = & $vswhere -all -prerelease -products '*' -requires Microsoft.Component.MSBuild `
            -find 'MSBuild\**\Bin\amd64\MSBuild.exe' 2>$null
        $candidate = $matches | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
        if ($candidate) {
            return $candidate
        }
    }

    $roots = @(
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) }

    foreach ($root in $roots) {
        $candidate = Get-ChildItem -LiteralPath $root -Filter 'MSBuild.exe' -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\MSBuild\\Current\\Bin\\(amd64\\)?MSBuild\.exe$' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }

    throw 'MSBuild was not found. Install the Visual Studio Desktop development with C++ workload from .vsconfig.'
}

$msbuild = Find-MSBuild
$target = if ($Clean) { 'Clean' } elseif ($Rebuild) { 'Rebuild' } else { 'Build' }
$dependencyInstaller = Join-Path $repoRoot 'vcpkg-install.ps1'
if (-not $Clean) {
    & $dependencyInstaller -Platform $Platform
}

$workerArgument = if ($MaxCpuCount -gt 0) { "/m:$MaxCpuCount" } else { '/m' }
$arguments = @(
    $solutionPath,
    "/t:$target",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    $workerArgument,
    '/nologo',
    '/verbosity:minimal'
)

Write-Host "MSBuild: $msbuild" -ForegroundColor DarkGray
Write-Host "$target RedXe ($Platform|$Configuration)" -ForegroundColor Cyan
& $msbuild @arguments
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE."
}

if (-not $Clean) {
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Build succeeded but the expected executable was not found: $executable"
    }
    Write-Host "Ready: $executable" -ForegroundColor Green

    if ($Run) {
        if ($Platform -ne 'x64' -and $env:PROCESSOR_ARCHITECTURE -eq 'AMD64') {
            throw 'This host cannot directly run the ARM64 build. Build it without -Run or run it on ARM64 Windows.'
        }
        Start-Process -FilePath $executable
    }
}
