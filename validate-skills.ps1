<#
.SYNOPSIS
Validates repository-local skill metadata, instructions and package references.
.DESCRIPTION
Runs the repository-owned PowerShell 7 validator. Supports the documented YAML subset,
including multiline descriptions; requires no Python, external YAML module or Codex installation.
Package/input checks run before metadata files are opened. Structure and local-reference checks do not
evaluate model decisions, command signatures or native behavior.
.OUTPUTS
Validation diagnostics. Fails on any invalid skill.
.NOTES
Reads skills and validates changelog fragments; does not modify files or access the network.
.EXAMPLE
./validate-skills.ps1
#>
#Requires -Version 7.0
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$global:LASTEXITCODE = 0
& (Join-Path $PSScriptRoot 'Tools/Test-Changelog.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Changelog validation failed.' }
Import-Module (Join-Path $PSScriptRoot 'Build/SkillPackageIntegrity.psm1') -Force
$packages = Get-SkillPackageIntegrity -Root $PSScriptRoot
if ($packages.Problems.Count) { throw ($packages.Problems -join "`n") }
Write-Host "Skill package/reference checks passed: $($packages.Skills) packages, $($packages.References) local references; model decisions NOT_EVALUATED."
Import-Module (Join-Path $PSScriptRoot 'Build/SkillValidation.psm1') -Force
$count = Test-RepositorySkills -Root $PSScriptRoot
Write-Host "Validated $count repository skills." -ForegroundColor Green
$global:LASTEXITCODE = 0
