<#
.SYNOPSIS
Refreshes the pinned RedMonitor client header and notice through the reviewed tool bundle.
.PARAMETER Commit
Full immutable RedMonitor commit reviewed for native client adoption.
.PARAMETER RepositoryRoot
Consumer repository to update.
#>
#Requires -Version 7.0
[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{40}$')][string] $Commit,
    [string] $RepositoryRoot = $PSScriptRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Build/RedMonitorClientTools.psm1') -ErrorAction Stop
$update = Get-RedMonitorClientCommand -Root $PSScriptRoot -Name 'Update-RedMonitorClient.ps1'
$PSBoundParameters['RepositoryRoot'] = $RepositoryRoot
& $update @PSBoundParameters
