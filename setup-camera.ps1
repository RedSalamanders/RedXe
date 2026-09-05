<#!
.SYNOPSIS
Checks, registers, or removes the current user's RedXe Camera route, with a bounded owned-process timeout.
#>
[CmdletBinding()]
param([ValidateSet('Check', 'Register', 'Remove')][string] $Action = 'Check')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$architecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
$platform = switch ($architecture) { 'X64' { 'x64' } 'Arm64' { 'ARM64' } default { throw 'Only x64/ARM64 Windows is supported.' } }
$installed = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFiles)) 'RedSalamanders/RedXe Camera/AVControlCameraSetup.exe'
$executable = if (Test-Path -LiteralPath $installed) { $installed } else { Join-Path $PSScriptRoot ".build/$platform/Release/Plugins/AVControlCameraSetup.exe" }
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build the native Release configuration or install the camera package first.' }
$start = [Diagnostics.ProcessStartInfo]::new($executable)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.ArgumentList.Add('--' + $Action.ToLowerInvariant())
$child = [Diagnostics.Process]::new()
$child.StartInfo = $start
try {
    if (-not $child.Start()) { throw 'Could not launch the owned camera setup process.' }
    $output = $child.StandardOutput.ReadToEndAsync()
    $errors = $child.StandardError.ReadToEndAsync()
    if (-not $child.WaitForExit(15000)) {
        $child.Kill() # Exact process object started here; no name lookup or external process termination.
        $null = $child.WaitForExit(1000)
        throw 'Camera setup exceeded 15 seconds. Its process was stopped; run Check before retrying.'
    }
    Write-Host ($output.GetAwaiter().GetResult()) -NoNewline
    $errorText = $errors.GetAwaiter().GetResult()
    if ($errorText) { Write-Host $errorText -NoNewline }
    if ($child.ExitCode -ne 0) { throw "Camera $Action failed with exit code $($child.ExitCode)." }
} finally { $child.Dispose() }
