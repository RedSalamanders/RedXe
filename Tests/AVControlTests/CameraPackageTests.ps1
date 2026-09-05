[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string] $Configuration = 'Debug', [ValidateSet('x64', 'ARM64')][string] $Platform = 'x64')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repo 'Build/CameraPackage.psm1') -Force
$testRoot = Join-Path $repo ('.build/test-artifacts/CameraPackage/' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $testRoot -Force
$script:checks = 0
function Check([bool] $Value, [string] $Message) { ++$script:checks; if (-not $Value) { throw $Message } }
function Reject([scriptblock] $Action, [string] $Message) {
    $rejected = $false
    try { & $Action } catch { $rejected = $true }
    Check $rejected $Message
}
$bad = Join-Path $testRoot 'bad.dll'
[IO.File]::WriteAllBytes($bad, [byte[]]::new(64))
Reject { Get-CameraBinaryMachine $bad } 'Reject non-PE binaries.'
$bytes = [byte[]]::new(64); $bytes[0] = 0x4d; $bytes[1] = 0x5a
[BitConverter]::GetBytes([uint32]::MaxValue).CopyTo($bytes, 60)
[IO.File]::WriteAllBytes($bad, $bytes)
Reject { Get-CameraBinaryMachine $bad } 'Reject an out-of-file PE header before seeking/reading.'
$source = Join-Path $repo ".build/$Platform/$Configuration/Plugins"
foreach ($name in Get-CameraPackageFiles) {
    $path = Join-Path $source $name
    Check ((Get-CameraBinaryMachine $path) -eq $Platform) 'Package has the intended PE machine.'
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($path)
    Check ($version.IsDebug -eq ($Configuration -eq 'Debug')) 'Debug flag identifies the actual configuration.'
    if ($Configuration -eq 'Debug') { Reject { Test-CameraBinary $path $Platform } 'Debug camera binaries cannot be installed.' }
    else { Check ((Test-CameraBinary $path $Platform).Length -eq 64) 'Release package binary passes identity and architecture validation.' }
}
if ($Configuration -eq 'Release') {
    $package = & (Join-Path $repo 'package-camera.ps1') -Platform $Platform
    $manifest = Test-CameraPackage $package $Platform
    Check ($manifest.files.Count -eq 2) 'Package consists of exactly the independent source and setup program.'
    $wrong = if ($Platform -eq 'x64') { 'ARM64' } else { 'x64' }
    Reject { Test-CameraPackage $package $wrong } 'Reject wrong architecture package.'
    $copy = Join-Path $testRoot 'Copy'
    $null = New-Item -ItemType Directory -Path $copy
    foreach ($name in @('camera-package.json') + @(Get-CameraPackageFiles)) { Copy-Item -LiteralPath (Join-Path $package $name) -Destination (Join-Path $copy $name) }
    $dll = Join-Path $copy 'AVControlCamera.dll'
    $stream = [IO.File]::OpenWrite($dll)
    try { $stream.Position = $stream.Length - 1; $stream.WriteByte(0xA5) } finally { $stream.Dispose() }
    Reject { Test-CameraPackage $copy $Platform } 'Reject changed package bytes before machine registration.'
    Copy-Item -LiteralPath (Join-Path $package 'AVControlCamera.dll') -Destination $dll -Force
    Set-Content -LiteralPath (Join-Path $copy 'unexpected.dll') -Value 'extra'
    Reject { Test-CameraPackage $copy $Platform } 'Reject unexpected package entries; never delete unrelated files.'
}
# No registry writes, MF camera API, hardware access, elevation or installation occurs in this suite.
Write-Host "PASS CameraPackage: $script:checks checks (validation only; no installation)"
