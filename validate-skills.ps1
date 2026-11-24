<#
.SYNOPSIS
Checks RedXe agent guidance and pending changelog fragments through the pinned CI engine.
.EXAMPLE
./validate-skills.ps1
#>
#Requires -Version 7.0
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$global:LASTEXITCODE = 0
& (Join-Path $PSScriptRoot 'Tools/Test-Changelog.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Changelog validation failed.' }
& (Join-Path $PSScriptRoot 'Tools/Test-AgentGuidance.ps1') -Root $PSScriptRoot
$global:LASTEXITCODE = 0
