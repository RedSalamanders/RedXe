<#
.SYNOPSIS
Validates every repository-local RedXe skill.

.DESCRIPTION
Runs the repository-owned metadata validator for .agents/skills on local machines and clean CI runners.
Requires Python and the dependencies in Build/requirements-validation.txt; no Codex installation is needed.
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSCommandPath
$skillRoot = Join-Path $repoRoot '.agents\skills'
if (-not (Test-Path -LiteralPath $skillRoot -PathType Container)) {
    throw "The repository skill directory was not found: $skillRoot"
}

$validator = Join-Path $repoRoot 'Build\validate_skills.py'

$python = Get-Command 'py.exe' -ErrorAction SilentlyContinue
$pythonUsesLauncher = $null -ne $python
if (-not $python) {
    $python = Get-Command 'python.exe' -ErrorAction SilentlyContinue
}
if (-not $python) {
    throw 'Python was not found. Install Python and Build/requirements-validation.txt.'
}

$skills = Get-ChildItem -LiteralPath $skillRoot -Directory | Sort-Object Name
if ($skills.Count -eq 0) {
    throw "No repository skills were found under: $skillRoot"
}

if ($pythonUsesLauncher) {
    & $python.Source -3 $validator
}
else {
    & $python.Source $validator
}
if ($LASTEXITCODE -ne 0) {
    throw 'Skill validation failed. Check the diagnostics and install Build/requirements-validation.txt if needed.'
}

Write-Host "Validated $($skills.Count) RedXe skills." -ForegroundColor Green
