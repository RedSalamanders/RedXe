<#
.SYNOPSIS
Runs the compiler-free RedXe validation used by pull requests.
.PARAMETER Base
Git comparison base for the changelog check. Defaults to origin/main.
.PARAMETER EngineRoot
Shared CI engine checkout. Defaults to this repository's pinned .ci-engine.
.PARAMETER Platform
The CI adapter's selected platform for project context.
.PARAMETER DevelopmentEngine
Use a development engine for local compiler-free validation.
#>
[CmdletBinding()]
param(
    [string]$Base = 'origin/main',
    [string]$EngineRoot = (Join-Path $PSScriptRoot '.ci-engine'),
    [ValidateSet('x64','ARM64')][string]$Platform = 'x64',
    [switch]$DevelopmentEngine
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$EngineRoot = [IO.Path]::GetFullPath($EngineRoot)
Import-Module (Join-Path $EngineRoot 'Tools/Modules/Ci.Policy.psm1') -Scope Local -ErrorAction Stop
Import-Module (Join-Path $EngineRoot 'Tools/Modules/Ci.Project.psm1') -Scope Local -ErrorAction Stop
$context = New-CiProjectContext -Root $PSScriptRoot -EngineRoot $EngineRoot -Phase Static -Platform $Platform -DevelopmentEngine:$DevelopmentEngine

$previous = [Environment]::GetEnvironmentVariable('CI_NATIVE_BUILDS_DISABLED', 'Process')
try {
    $env:CI_NATIVE_BUILDS_DISABLED = 'true'
    Test-CiProjectConfiguration $context
    & (Join-Path $EngineRoot 'Tools/Test-Workflows.ps1') -Root $PSScriptRoot
    $baseCommit = Resolve-CiCommit $PSScriptRoot $Base
    Invoke-CiProjectScript -Context $context -Script 'Tools/Test-Changelog.ps1' -Arguments @('-Base', $baseCommit)
    Invoke-CiProjectScript -Context $context -Script 'validate-skills.ps1' -Arguments @()
    Invoke-CiProjectScript -Context $context -Script 'validate-specs.ps1' -Arguments @()
}
finally {
    [Environment]::SetEnvironmentVariable('CI_NATIVE_BUILDS_DISABLED', $(if ($null -eq $previous) { [NullString]::Value } else { $previous }), 'Process')
}
