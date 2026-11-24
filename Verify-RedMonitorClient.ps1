<#
.SYNOPSIS
Verifies the pinned local RedMonitor header and notice through the reviewed tool bundle.
.PARAMETER RepositoryRoot
Consumer repository holding the vendor files and the client lock.
#>
[CmdletBinding()]
param([string] $RepositoryRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $RepositoryRoot) { $RepositoryRoot = $PSScriptRoot }
Import-Module (Join-Path $PSScriptRoot 'Build/RedMonitorClientTools.psm1') -ErrorAction Stop
$verify = Get-RedMonitorClientCommand -Root $PSScriptRoot -Name 'Verify-RedMonitorClient.ps1'
$PSBoundParameters['RepositoryRoot'] = $RepositoryRoot
& $verify @PSBoundParameters
