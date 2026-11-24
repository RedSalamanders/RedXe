# Compatibility entry point for local callers. Hosted workflows use the pinned engine action directly.
[CmdletBinding()]
param(
    [ValidateSet('x64','ARM64')][string]$Platform='x64',
    [switch]$VerifyOnly,
    [ValidateRange(1,3600)][int]$InstallerTimeoutSeconds=900
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$engine=Join-Path $project '.ci-engine'
$pin=(Get-Content -LiteralPath (Join-Path $project '.ci/project.json') -Raw|ConvertFrom-Json).engine.ref
$setup=Join-Path $engine '.github/actions/setup-msvc/Setup.ps1'
if(-not (Test-Path -LiteralPath $setup -PathType Leaf)){
    throw 'Check out the engine commit from .ci/project.json in .ci-engine before invoking MSVC setup.'
}
$selected=git -C $engine rev-parse HEAD
if($LASTEXITCODE -or $selected -cne $pin -or (git -C $engine --no-optional-locks status --porcelain)){
    throw "MSVC setup requires the clean pinned CI engine at $pin."
}
& $setup -ProjectRoot $project -Platform $Platform -VerifyOnly:$VerifyOnly -InstallerTimeoutSeconds $InstallerTimeoutSeconds
