<#!
.SYNOPSIS
Creates an identified native Release camera package below .build without installing or registering anything.
#>
[CmdletBinding()]
param([ValidateSet('x64', 'ARM64')][string] $Platform = 'x64')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Build/CameraPackage.psm1') -Force
$source = Join-Path $PSScriptRoot ".build/$Platform/Release/Plugins"
$files = [ordered]@{}
foreach ($name in Get-CameraPackageFiles) { $files[$name] = Test-CameraBinary (Join-Path $source $name) $Platform }
# Each package is immutable. Avoid overwriting an artifact someone may already be reviewing or installing.
$target = Join-Path $PSScriptRoot ('.build/packages/RedXeCamera-{0}-{1}' -f $Platform, [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $target
foreach ($name in Get-CameraPackageFiles) { Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $target $name) }
[ordered]@{schema=1; product='RedXe Camera'; platform=$Platform; sourceClsid='{10F8F1A2-4A82-4D82-9C0E-E253B151BDCB}'; files=$files} |
    ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $target 'camera-package.json') -Encoding utf8NoBOM
$null = Test-CameraPackage $target $Platform
Write-Output $target
