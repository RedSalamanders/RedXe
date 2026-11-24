#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$IntermediateDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $root '.ci-engine/Tools/Modules/Ci.Policy.psm1') -ErrorAction Stop
$analysisRoot=Assert-CiContainedPath -Root $root -Path (Join-Path $root '.build/analysis')
$null=Assert-CiContainedPath -Root $analysisRoot -Path $OutputDirectory
$null=Assert-CiContainedPath -Root $analysisRoot -Path $IntermediateDirectory
