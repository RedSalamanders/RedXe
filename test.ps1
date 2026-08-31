<#!
.SYNOPSIS
Builds RedXe and runs its GPU-independent WARP smoke test.

.DESCRIPTION
The test validates the factory, generic widget, and rendering-interface ABI, then creates a hidden Win32 window,
initializes a Direct3D 11 WARP device and flip-model swap chain, creates shaders from embedded build-time bytecode,
renders one frame, presents it, and exits. Loading the plugin must not load d3dcompiler_47.dll.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',

    [ValidateSet('x64', 'ARM64')]
    [string] $Platform = 'x64',

    [switch] $Rebuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSCommandPath
$buildArguments = @{
    Configuration = $Configuration
    Platform = $Platform
}
if ($Rebuild) {
    $buildArguments.Rebuild = $true
}

& (Join-Path $repoRoot 'build.ps1') @buildArguments
if ($LASTEXITCODE -ne 0) {
    throw "Build entrypoint failed with exit code $LASTEXITCODE."
}

$executable = Join-Path $repoRoot ".build\$Platform\$Configuration\RedXe.exe"
if ($Platform -ne 'x64' -and $env:PROCESSOR_ARCHITECTURE -eq 'AMD64') {
    throw 'The ARM64 smoke test must run on ARM64 Windows. The build itself completed successfully.'
}

$contractTests = Join-Path $repoRoot ".build\$Platform\$Configuration\PluginContractTests.exe"
Write-Host 'Running plugin ABI and rendering-interface contract tests...' -ForegroundColor Cyan
$contractProcess = Start-Process -FilePath $contractTests -Wait -PassThru
if ($contractProcess.ExitCode -ne 0) {
    throw "Plugin contract tests failed with exit code $($contractProcess.ExitCode)."
}

Write-Host 'Running hidden Direct3D 11 WARP smoke test...' -ForegroundColor Cyan
$process = Start-Process -FilePath $executable -ArgumentList @('--self-test', '--warp') -Wait -PassThru
if ($process.ExitCode -ne 0) {
    throw "Smoke test failed with exit code $($process.ExitCode)."
}

Write-Host 'Smoke test passed.' -ForegroundColor Green
