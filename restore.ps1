<#
.SYNOPSIS
Restores the exact RedXe and DxUi dependencies.
.PARAMETER Platform
x64 or ARM64 target architecture.
#>
#Requires -Version 7.0
[CmdletBinding()]
param([ValidateSet('x64','ARM64')][string]$Platform='x64')

Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'Build/DxUiRestore.psm1') -Force -ErrorAction Stop
$compilerHost=Enter-RedXeCompilerHost
$exitCompilerHost=Get-Command Exit-RedXeCompilerHost
$binarySources=$env:VCPKG_BINARY_SOURCES
try {
    $env:VCPKG_BINARY_SOURCES='clear'
    $msbuild=Find-RedXeMSBuild
    & (Join-Path $PSScriptRoot 'vcpkg-install.ps1') -Platform $Platform -MSBuildPath $msbuild
    & (Join-Path $PSScriptRoot 'restore-dxui.ps1') -Platform $Platform -MSBuildPath $msbuild
} finally {
    $env:VCPKG_BINARY_SOURCES=$binarySources
    & $exitCompilerHost -State $compilerHost
}
