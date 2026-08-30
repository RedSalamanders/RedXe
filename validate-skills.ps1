<#
.SYNOPSIS
Validates every repository-local RedXe skill.

.DESCRIPTION
Locates the quick validator bundled with the Codex skill-creator skill, then validates each direct child of
.agents/skills. The command fails immediately when the validator is unavailable or any skill is invalid.
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

$validatorCandidates = @()
if ($env:CODEX_HOME) {
    $validatorCandidates += Join-Path $env:CODEX_HOME 'skills\.system\skill-creator\scripts\quick_validate.py'
}
if ($env:USERPROFILE) {
    $validatorCandidates += Join-Path $env:USERPROFILE '.codex\skills\.system\skill-creator\scripts\quick_validate.py'
}

$validator = $validatorCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $validator) {
    throw 'The bundled skill-creator quick_validate.py script was not found under CODEX_HOME or USERPROFILE.'
}

$python = Get-Command 'py.exe' -ErrorAction SilentlyContinue
$pythonUsesLauncher = $null -ne $python
if (-not $python) {
    $python = Get-Command 'python.exe' -ErrorAction SilentlyContinue
}
if (-not $python) {
    throw 'Python was not found. Install Python or run this command from a Codex environment that provides it.'
}

$skills = Get-ChildItem -LiteralPath $skillRoot -Directory | Sort-Object Name
if ($skills.Count -eq 0) {
    throw "No repository skills were found under: $skillRoot"
}

foreach ($skill in $skills) {
    Write-Host "Validating $($skill.Name)..." -ForegroundColor Cyan
    if ($pythonUsesLauncher) {
        & $python.Source -3 $validator $skill.FullName
    }
    else {
        & $python.Source $validator $skill.FullName
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Skill validation failed: $($skill.FullName)"
    }
}

Write-Host "Validated $($skills.Count) RedXe skills." -ForegroundColor Green
