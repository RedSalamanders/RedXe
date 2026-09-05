<#!
.SYNOPSIS
Installs/removes the native Release camera COM source. Per-user device registration is a separate explicit step.
.DESCRIPTION
Requires native PowerShell 7 and, for mutations, elevation. -WhatIf validates the complete package and ownership
without writing. Never stops Frame Server, RedXe, capture applications or arbitrary processes. Close capture apps
before updating/removing a source that Windows has loaded. The fixed installation directory is below Program Files.
#>
[CmdletBinding(SupportsShouldProcess, ConfirmImpact='Medium')]
param(
    [Parameter(Mandatory)][ValidateSet('Install', 'Remove')][string] $Action,
    [string] $PackagePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Build/CameraPackage.psm1') -Force
$architecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
if ($PSVersionTable.PSVersion.Major -lt 7 -or [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString() -ne $architecture) {
    throw 'Run native PowerShell 7 for the OS architecture.'
}
$platform = switch ($architecture) { 'X64' { 'x64' } 'Arm64' { 'ARM64' } default { throw 'Only x64/ARM64 Windows is supported.' } }
$programFiles = [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFiles)
$base = Assert-CameraPlainPath (Join-Path $programFiles 'RedSalamanders')
$target = Assert-CameraPlainPath (Join-Path $base 'RedXe Camera')
$clsid = '{10F8F1A2-4A82-4D82-9C0E-E253B151BDCB}'
$classPath = "SOFTWARE\Classes\CLSID\$clsid"
$sourcePath = Join-Path $target 'AVControlCamera.dll'
$machine = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
$existing = $machine.OpenSubKey("$classPath\InprocServer32")
$oldPath = $null; $oldThreading = $null
try {
    if ($existing) {
        if ($existing.SubKeyCount -ne 0 -or $existing.ValueCount -ne 2 -or
            @($existing.GetValueNames() | Where-Object { $_ -cnotin @('', 'ThreadingModel') }).Count) {
            throw 'COM source key contains entries not owned by this installer.'
        }
        $oldPath = $existing.GetValue(''); $oldThreading = $existing.GetValue('ThreadingModel')
    }
}
finally { if ($existing) { $existing.Dispose() } }
if ($null -ne $oldPath -and ($oldPath -ine $sourcePath -or $oldThreading -cne 'Both')) { throw 'COM source registration is not owned by this installer.' }
if (Test-Path -LiteralPath $target) { $null = Test-CameraPackage $target $platform }
elseif ($null -ne $oldPath) { throw 'Registered source has no owned package. Repair explicitly before replacing it.' }

function Assert-ProtectedDirectory([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $acl = Get-Acl -LiteralPath $Path
    $trusted = @('S-1-5-18', 'S-1-5-32-544', 'S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464')
    $owner = $acl.GetOwner([Security.Principal.SecurityIdentifier]).Value
    if ($owner -notin $trusted) { throw "Installation directory has an unexpected owner: $Path" }
    $write = [Security.AccessControl.FileSystemRights]::Write -bor [Security.AccessControl.FileSystemRights]::Delete -bor
        [Security.AccessControl.FileSystemRights]::ChangePermissions -bor [Security.AccessControl.FileSystemRights]::TakeOwnership -bor
        [Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles
    foreach ($rule in $acl.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier])) {
        if ($rule.AccessControlType -eq 'Allow' -and ($rule.FileSystemRights -band $write) -and
            -not ($rule.PropagationFlags -band [Security.AccessControl.PropagationFlags]::InheritOnly) -and
            $rule.IdentityReference.Value -notin $trusted) { throw "Installation directory is writable by an untrusted principal: $Path" }
    }
}
Assert-ProtectedDirectory $programFiles
Assert-ProtectedDirectory $base
Assert-ProtectedDirectory $target
if ($Action -eq 'Install') {
    if (-not $PackagePath) { throw '-PackagePath is required for Install.' }
    $package = Assert-CameraPlainPath $PackagePath
    $candidate = Test-CameraPackage $package $platform
} elseif ($PackagePath) { throw '-PackagePath does not apply to Remove.' }
if (-not $PSCmdlet.ShouldProcess($target, "$Action native RedXe Camera source and its machine COM registration")) { $machine.Dispose(); return }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
try { $elevated = [Security.Principal.WindowsPrincipal]::new($identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
finally { $identity.Dispose() }
if (-not $elevated) { $machine.Dispose(); throw 'Machine source installation/removal requires an elevated terminal.' }

# Only exact, validated children under the fixed installation parent can be moved or removed. No recursive delete.
function Assert-ManagedChild([string] $Path) {
    $absolute = Assert-CameraPlainPath $Path
    if ([IO.Path]::GetDirectoryName($absolute) -ine $base) { throw 'Target escapes the fixed camera installation parent.' }
    return $absolute
}
function Remove-OwnedPackage([string] $Path) {
    $absolute = Assert-ManagedChild $Path
    $null = Test-CameraPackage $absolute $platform
    foreach ($name in @('camera-package.json') + @(Get-CameraPackageFiles)) { Remove-Item -LiteralPath (Join-Path $absolute $name) }
    Remove-Item -LiteralPath $absolute # Fails if an unexpected file appeared; never recurse.
}
$stage = $null; $backup = $null; $published = $false; $success = $false
try {
    if ($Action -eq 'Remove') {
        if (-not (Test-Path -LiteralPath $target)) { Write-Host 'Camera source is already absent.'; return }
        # Removing machine files affects every user's route. Per-user Remove is deliberately not impersonated.
        Write-Host 'Remove each user route with AVControlCameraSetup.exe --remove before machine removal.'
    } else {
        if (-not (Test-Path -LiteralPath $base)) { $null = New-Item -ItemType Directory -Path $base }
        Assert-ProtectedDirectory $base
        $stage = Assert-ManagedChild (Join-Path $base ('RedXe Camera.Stage.' + [guid]::NewGuid().ToString('N')))
        $null = New-Item -ItemType Directory -Path $stage
        foreach ($name in @('camera-package.json') + @(Get-CameraPackageFiles)) { Copy-Item -LiteralPath (Join-Path $package $name) -Destination (Join-Path $stage $name) }
        $staged = Test-CameraPackage $stage $platform
        foreach ($name in Get-CameraPackageFiles) {
            if ($staged.files[$name] -cne $candidate.files[$name]) { throw 'Source package changed while staging; nothing was installed.' }
        }
        Assert-ProtectedDirectory $stage
    }
    if (Test-Path -LiteralPath $target) {
        $backupDestination = Assert-ManagedChild (Join-Path $base ('RedXe Camera.Backup.' + [guid]::NewGuid().ToString('N')))
        $null = Assert-ManagedChild $target
        Move-Item -LiteralPath $target -Destination $backupDestination
        $backup = $backupDestination
    }
    if ($Action -eq 'Install') {
        $null = Assert-ManagedChild $target
        Move-Item -LiteralPath $stage -Destination $target
        $stage = $null; $published = $true
        $key = $machine.CreateSubKey("$classPath\InprocServer32", $true)
        try { $key.SetValue('', $sourcePath); $key.SetValue('ThreadingModel', 'Both') } finally { $key.Dispose() }
    } else { $machine.DeleteSubKey("$classPath\InprocServer32", $false) }
    $success = $true
} catch {
    $original = $_
    try {
        if ($null -eq $oldPath) { $machine.DeleteSubKey("$classPath\InprocServer32", $false) }
        else {
            $key = $machine.CreateSubKey("$classPath\InprocServer32", $true)
            try { $key.SetValue('', $oldPath); $key.SetValue('ThreadingModel', $oldThreading) } finally { $key.Dispose() }
        }
        if ($published) { Remove-OwnedPackage $target }
        if ($backup) { $null = Assert-ManagedChild $target; Move-Item -LiteralPath $backup -Destination $target; $backup = $null }
    } catch { Write-Warning "Rollback requires repair; preserve package directories. $($_.Exception.Message)" }
    throw $original
} finally { $machine.Dispose() }
if ($success -and $backup) {
    try { Remove-OwnedPackage $backup } catch { Write-Warning "Old package retained for manual cleanup after capture applications close: $backup" }
}
if ($success -and $Action -eq 'Install') {
    Write-Host "Installed $platform source. In a NON-elevated terminal, run:"
    Write-Host "& '$target\AVControlCameraSetup.exe' --register"
}
