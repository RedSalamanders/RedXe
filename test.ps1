<#!
.SYNOPSIS
Builds RedXe and runs its GPU-independent WARP smoke test.

.DESCRIPTION
The smoke test creates a hidden Win32 window, initializes a Direct3D 11 WARP device and flip-model swap chain,
compiles the embedded shaders, draws one frame, presents it, and exits.
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

Write-Host 'Running hidden Direct3D 11 WARP smoke test...' -ForegroundColor Cyan
$process = Start-Process -FilePath $executable -ArgumentList @('--self-test', '--warp') -Wait -PassThru
if ($process.ExitCode -ne 0) {
    throw "Smoke test failed with exit code $($process.ExitCode)."
}

Write-Host 'Smoke test passed.' -ForegroundColor Green
