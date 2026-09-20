<#!
.SYNOPSIS
Builds, cleans, rebuilds, or runs the RedXe solution.

.DESCRIPTION
Locates a Visual Studio MSBuild installation (including prerelease Visual Studio instances), rejects a running RedXe
process whose executable is the exact selected target output, builds the requested configuration and platform,
and writes all outputs beneath .build. Interactive builds preserve native MSBuild color when possible and otherwise
replay colored output while capturing a plain-text log beneath .build/logs.

.PARAMETER Configuration
Build configuration: Debug, Release or ASan Debug.

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

.PARAMETER BuildNumber
Third version component stamped into every version resource (Common/Version.h supplies major.minor). The release
workflow passes GITHUB_RUN_NUMBER; a local build keeps 0. Range 0..65535.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'ASan Debug')]
    [string] $Configuration = 'Debug',

    [ValidateSet('x64', 'ARM64')]
    [string] $Platform = 'x64',

    [switch] $Clean,
    [switch] $Rebuild,
    [switch] $Run,

    [ValidateRange(0, 256)]
    [int] $MaxCpuCount = 0,

    [ValidateRange(0, 65535)]
    [int] $BuildNumber = 0
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
$buildPresentationModule = Join-Path $repoRoot 'Build\BuildPresentation.psm1'
Import-Module $buildOutputProcessModule -Force -ErrorAction Stop
Import-Module $buildPresentationModule -Force -ErrorAction Stop

$useInteractiveTerminal = Test-RedXeInteractiveTerminal
Write-RedXeBuildBanner -UseColor $useInteractiveTerminal
Write-Host ("Target: {0} | {1}" -f $Platform, $Configuration) -ForegroundColor Magenta
if ($BuildNumber -gt 0) {
    Import-Module (Join-Path $repoRoot 'Build\Versioning.psm1') -Force -ErrorAction Stop
    Write-Host ("Version: {0}" -f (Get-RedXeVersion -RepoRoot $repoRoot -BuildNumber $BuildNumber).Version) -ForegroundColor Magenta
}
Write-Host ''

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
$operationStopwatch = [Diagnostics.Stopwatch]::StartNew()
if (-not $Clean) {
    Write-Host '[1/2] Dependencies' -ForegroundColor Cyan
    & $dependencyInstaller -Platform $Platform
    & (Join-Path $repoRoot 'restore-dxui.ps1') -Platform $Platform -MSBuildPath $msbuild -CheckUpdates
    Write-Host ''
}

$workerArgument = if ($MaxCpuCount -gt 0) { "/m:$MaxCpuCount" } else { '/m' }
$arguments = @(
    $solutionPath,
    "/t:$target",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/p:RedXeBuildNumber=$BuildNumber",
    $workerArgument,
    '/nologo',
    '/verbosity:minimal'
)

$logPath = New-RedXeBuildLogPath -RepoRoot $repoRoot -Target $target
$invocationPlan = Get-RedXeBuildInvocationPlan `
    -UseInteractiveTerminal $useInteractiveTerminal `
    -LogPath $logPath

$stageLabel = if ($Clean) { '[1/1]' } else { '[2/2]' }
Write-Host "$stageLabel $target" -ForegroundColor Cyan
Write-Host "MSBuild: $msbuild" -ForegroundColor DarkGray

if ($invocationPlan.UseDirectConsole) {
    $directArguments = @($arguments + @($invocationPlan.AdditionalArguments))
    & $msbuild @directArguments
    $exitCode = $LASTEXITCODE
}
else {
    $exitCode = Invoke-RedXeStreamingProcess `
        -FilePath $msbuild `
        -Arguments $arguments `
        -WorkingDirectory $repoRoot `
        -LogPath $logPath `
        -OutputLineCallback {
        param(
            [string] $Line,
            [bool] $IsError
        )

        Write-RedXeBuildStreamingLine -Line $Line -IsError $IsError
    }
}

$operationStopwatch.Stop()
Write-Host ''
Write-Host "Captured log: $logPath" -ForegroundColor DarkGray
Write-RedXeBuildDiagnosticSummary -LogPath $logPath
$duration = Format-RedXeBuildDuration -Duration $operationStopwatch.Elapsed
if ($exitCode -ne 0) {
    Write-Host "BUILD SIGNAL LOST after $duration" -ForegroundColor Red
    throw "MSBuild failed with exit code $exitCode."
}

if (-not $Clean) {
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Build succeeded but the expected executable was not found: $executable"
    }
    Import-Module (Join-Path $repoRoot 'Build/DxUiProvenance.psm1') -Force
    $provenance=Write-RedXeDxUiProvenance -RepoRoot $repoRoot -Platform $Platform -Configuration $Configuration
    Write-Host "DxUi module provenance: $provenance" -ForegroundColor DarkGray
    Write-Host "BUILD SIGNAL LOCKED in $duration" -ForegroundColor Green
    Write-Host "Ready: $executable" -ForegroundColor Green

    if ($Run) {
        if ($Platform -ne 'x64' -and $env:PROCESSOR_ARCHITECTURE -eq 'AMD64') {
            throw 'This host cannot directly run the ARM64 build. Build it without -Run or run it on ARM64 Windows.'
        }
        Start-Process -FilePath $executable
    }
}
else {
    Write-Host "CLEAN SIGNAL LOCKED in $duration" -ForegroundColor Green
}
