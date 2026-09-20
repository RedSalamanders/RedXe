<#
.SYNOPSIS
Installs or removes RedXe from the portable package this script ships in.

.DESCRIPTION
RedXe runs from any folder. This script adds what a plain folder cannot: a copy under a stable per-user location, a
Start Menu shortcut, an optional start-at-sign-in entry, and an entry under Settings > Apps so Windows can list and
remove it. Everything is per user; no elevation is required and nothing is written outside your profile.

Install (default) copies the package beside this script to %LocalAppData%\Programs\RedXe (or -Destination), replaces
a previous copy made by this script, creates the shortcut, registers the Apps entry, and optionally starts RedXe at
sign-in. Inside a winget package folder (winget install RedSalamanders.RedXe) the package is registered in place:
winget keeps owning the files and the Apps entry, so only the shortcut and sign-in entry are added.

Remove deletes the shortcut, the sign-in entry, the Apps entry, and the files an earlier Install copied. Your settings,
logs, and crash dumps under %LocalAppData%\RedXe stay unless -PurgeUserData is given.

A running RedXe.exe from the target folder blocks Install and Remove; it is reported, never terminated.

.PARAMETER Action
Install (default) or Remove.

.PARAMETER Destination
Folder that receives the copy. Default: %LocalAppData%\Programs\RedXe. It must be empty, missing, or a previous
RedXe installation made by this script.

.PARAMETER InPlace
Register the folder this script is in without copying it anywhere. Implied inside a winget package folder.

.PARAMETER StartAtSignIn
Install only: start RedXe when you sign in (HKCU Run entry). Remove always deletes that entry.

.PARAMETER Launch
Install only: start RedXe when the installation is complete.

.PARAMETER PurgeUserData
Remove only: also delete %LocalAppData%\RedXe (settings, logs, crash dumps).

.PARAMETER StartMenuDirectory
Where the shortcut goes. Default: the current user's Start Menu Programs folder. Used by the repository tests.

.PARAMETER RegistryRoot
Registry path that holds the Run and Uninstall keys. Default: HKCU:\Software\Microsoft\Windows\CurrentVersion.
Used by the repository tests.

.EXAMPLE
.\Install-RedXe.ps1 -StartAtSignIn -Launch

.EXAMPLE
.\Install-RedXe.ps1 -Action Remove

.EXAMPLE
.\Install-RedXe.ps1 -WhatIf
#>
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [ValidateSet('Install', 'Remove')]
    [string] $Action = 'Install',

    [string] $Destination,
    [switch] $InPlace,
    [switch] $StartAtSignIn,
    [switch] $Launch,
    [switch] $PurgeUserData,

    [string] $StartMenuDirectory,
    [string] $RegistryRoot = 'HKCU:\Software\Microsoft\Windows\CurrentVersion'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$productName = 'RedXe'
$publisher = 'RedSalamanders'
$manifestName = 'redxe-install.json'
$packageDirectory = [IO.Path]::GetFullPath($PSScriptRoot)
$isWingetPackage = $packageDirectory -match '\\Microsoft\\WinGet\\Packages\\'

function Get-RelativePackagePath {
    param([string] $Root, [string] $FullPath)
    $prefix = $Root.TrimEnd('\') + '\'
    if (-not $FullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Not inside ${Root}: $FullPath" }
    return $FullPath.Substring($prefix.Length)
}

function Get-PackageFiles {
    param([string] $Root)
    return @(Get-ChildItem -LiteralPath $Root -File -Recurse -Force |
        Where-Object { $_.Name -ne $manifestName } |
        ForEach-Object { Get-RelativePackagePath -Root $Root -FullPath $_.FullName } |
        Sort-Object)
}

function Assert-PackageFolder {
    param([string] $Root)
    foreach ($required in @('RedXe.exe', 'RedXeLauncher.exe', 'Plugins', 'Settings')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Root $required))) {
            throw "This is not a complete RedXe package: '$required' is missing from $Root."
        }
    }
}

function Get-InstallManifest {
    param([string] $Root)
    $path = Join-Path $Root $manifestName
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    return (Get-Content -LiteralPath $path -Raw | ConvertFrom-Json)
}

function Assert-NotRunning {
    param([string] $Root)
    $expected = (Join-Path $Root 'RedXe.exe')
    # Get-Process (not CIM) so -WhatIf stays quiet: the CimCmdlets auto-import announces its aliases under WhatIf.
    # Path is readable for this user's processes, which is the only scope a per-user install can replace.
    $running = @(Get-Process -Name 'RedXe' -ErrorAction SilentlyContinue | Where-Object {
        $path = $null
        try { $path = $_.Path } catch { $path = $null }
        $path -and ([IO.Path]::GetFullPath($path) -ieq $expected)
    })
    if ($running.Count -gt 0) {
        $ids = ($running | ForEach-Object { $_.Id }) -join ', '
        throw "RedXe is running from $Root (PID $ids). Close it (press Escape in the dashboard) and run this script again; it was not terminated."
    }
}

function Get-ProductVersion {
    param([string] $Root)
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $Root 'RedXe.exe'))
    if ($info.ProductName -ne $productName) { throw "RedXe.exe in $Root is not a RedXe binary." }
    $parts = $info.FileVersion -split '\.'
    if ($parts.Count -ge 3) { return "$($parts[0]).$($parts[1]).$($parts[2])" }
    return $info.FileVersion
}

function Get-UserDataDirectory {
    return Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)) $productName
}

if (-not $StartMenuDirectory) {
    $StartMenuDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::Programs)
}
$shortcutPath = Join-Path $StartMenuDirectory "$productName.lnk"
$runKey = Join-Path $RegistryRoot 'Run'
$uninstallKey = Join-Path $RegistryRoot "Uninstall\$productName"

if ($isWingetPackage -and -not $InPlace) {
    Write-Host 'This package was installed by winget; registering it in place.' -ForegroundColor DarkGray
    $InPlace = $true
}
if ($InPlace -and $Destination) { throw '-InPlace and -Destination cannot be combined.' }

if ($Action -eq 'Install') {
    if ($PurgeUserData) { throw '-PurgeUserData applies to -Action Remove only.' }
    Assert-PackageFolder -Root $packageDirectory
    $version = Get-ProductVersion -Root $packageDirectory

    if ($InPlace) {
        $target = $packageDirectory
    }
    else {
        if (-not $Destination) {
            $Destination = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)) "Programs\$productName"
        }
        $target = [IO.Path]::GetFullPath($Destination)
        if ($target -ieq $packageDirectory) { $InPlace = $true }
    }

    Assert-NotRunning -Root $target

    $previous = $null
    if (-not $InPlace -and (Test-Path -LiteralPath $target)) {
        $previous = Get-InstallManifest -Root $target
        $existing = @(Get-ChildItem -LiteralPath $target -Force)
        if ($existing.Count -gt 0 -and -not $previous) {
            throw "$target is not empty and was not created by this installer. Choose an empty -Destination or remove the folder yourself."
        }
    }

    $files = Get-PackageFiles -Root $packageDirectory
    if ($InPlace) {
        Write-Host "Registering $productName $version in place: $target"
    }
    else {
        Write-Host "Installing $productName $version to $target"
    }

    if (-not $InPlace -and $PSCmdlet.ShouldProcess($target, "Copy $($files.Count) package files")) {
        if ($previous) {
            foreach ($stale in @($previous.files) | Where-Object { $files -notcontains $_ }) {
                $stalePath = Join-Path $target $stale
                if (Test-Path -LiteralPath $stalePath -PathType Leaf) { Remove-Item -LiteralPath $stalePath -Force }
            }
        }
        foreach ($relative in $files) {
            $destinationPath = Join-Path $target $relative
            $parent = Split-Path -Parent $destinationPath
            if (-not (Test-Path -LiteralPath $parent)) { [void](New-Item -ItemType Directory -Path $parent -Force) }
            Copy-Item -LiteralPath (Join-Path $packageDirectory $relative) -Destination $destinationPath -Force
        }
    }

    $executable = Join-Path $target 'RedXe.exe'
    if ($PSCmdlet.ShouldProcess($shortcutPath, 'Create Start Menu shortcut')) {
        if (-not (Test-Path -LiteralPath $StartMenuDirectory)) { [void](New-Item -ItemType Directory -Path $StartMenuDirectory -Force) }
        $shell = New-Object -ComObject WScript.Shell
        try {
            $shortcut = $shell.CreateShortcut($shortcutPath)
            $shortcut.TargetPath = $executable
            $shortcut.WorkingDirectory = $target
            $shortcut.IconLocation = "$executable,0"
            $shortcut.Description = 'RedXe XENEON dashboard'
            $shortcut.Save()
        }
        finally { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell) }
    }

    if ($StartAtSignIn -and $PSCmdlet.ShouldProcess("$runKey\$productName", 'Start at sign-in')) {
        if (-not (Test-Path -LiteralPath $runKey)) { [void](New-Item -Path $runKey -Force) }
        Set-ItemProperty -LiteralPath $runKey -Name $productName -Value ('"{0}"' -f $executable)
    }

    if (-not $InPlace -and $PSCmdlet.ShouldProcess($uninstallKey, 'Register under Settings > Apps')) {
        $sizeKb = [int][math]::Ceiling((($files | ForEach-Object { (Get-Item -LiteralPath (Join-Path $target $_)).Length } | Measure-Object -Sum).Sum) / 1KB)
        $uninstallScript = Join-Path $target 'Install-RedXe.ps1'
        $uninstallCommand = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "{0}" -Action Remove' -f $uninstallScript
        if (-not (Test-Path -LiteralPath $uninstallKey)) { [void](New-Item -Path $uninstallKey -Force) }
        $values = [ordered]@{
            DisplayName = $productName
            DisplayVersion = $version
            Publisher = $publisher
            InstallLocation = $target
            DisplayIcon = "$executable,0"
            UninstallString = $uninstallCommand
            QuietUninstallString = $uninstallCommand
            URLInfoAbout = 'https://github.com/RedSalamanders/RedXe'
            InstallDate = (Get-Date).ToString('yyyyMMdd')
        }
        foreach ($name in $values.Keys) { Set-ItemProperty -LiteralPath $uninstallKey -Name $name -Value $values[$name] -Type String }
        Set-ItemProperty -LiteralPath $uninstallKey -Name 'NoModify' -Value 1 -Type DWord
        Set-ItemProperty -LiteralPath $uninstallKey -Name 'NoRepair' -Value 1 -Type DWord
        Set-ItemProperty -LiteralPath $uninstallKey -Name 'EstimatedSize' -Value $sizeKb -Type DWord
    }

    if ($PSCmdlet.ShouldProcess((Join-Path $target $manifestName), 'Write installation manifest')) {
        $manifest = [ordered]@{
            product = $productName
            version = $version
            installedAt = (Get-Date).ToUniversalTime().ToString('o')
            inPlace = [bool] $InPlace
            shortcut = $shortcutPath
            registryRoot = $RegistryRoot
            files = $files
        }
        [IO.File]::WriteAllText((Join-Path $target $manifestName), ($manifest | ConvertTo-Json -Depth 3), [Text.UTF8Encoding]::new($false))
    }

    if ($WhatIfPreference) { return }
    Write-Host "Installed $productName $version." -ForegroundColor Green
    Write-Host "  Folder:     $target"
    Write-Host "  Start Menu: $shortcutPath"
    if ($StartAtSignIn) { Write-Host '  Starts at sign-in.' }
    if ($Launch) { Start-Process -FilePath $executable -WorkingDirectory $target }
    return
}

# Remove
if ($Launch -or $StartAtSignIn) { throw '-Launch and -StartAtSignIn apply to -Action Install only.' }
if ($InPlace) {
    $target = $packageDirectory
}
elseif ($Destination) {
    $target = [IO.Path]::GetFullPath($Destination)
}
elseif (Get-InstallManifest -Root $packageDirectory) {
    $target = $packageDirectory
}
else {
    $target = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)) "Programs\$productName"
}
$manifest = Get-InstallManifest -Root $target
if (-not $manifest -and -not $InPlace) {
    Write-Host "No installation manifest found in $target; removing only the shortcut and registry entries." -ForegroundColor DarkGray
}
# Undo exactly what Install recorded, even when it used non-default locations.
if ($manifest -and -not $PSBoundParameters.ContainsKey('StartMenuDirectory') -and $manifest.shortcut) { $shortcutPath = [string] $manifest.shortcut }
if ($manifest -and -not $PSBoundParameters.ContainsKey('RegistryRoot') -and $manifest.registryRoot) {
    $runKey = Join-Path ([string] $manifest.registryRoot) 'Run'
    $uninstallKey = Join-Path ([string] $manifest.registryRoot) "Uninstall\$productName"
}
if (Test-Path -LiteralPath (Join-Path $target 'RedXe.exe')) { Assert-NotRunning -Root $target }

if ((Test-Path -LiteralPath $shortcutPath) -and $PSCmdlet.ShouldProcess($shortcutPath, 'Delete Start Menu shortcut')) {
    Remove-Item -LiteralPath $shortcutPath -Force
}
if ((Test-Path -LiteralPath $runKey) -and (Get-ItemProperty -LiteralPath $runKey -Name $productName -ErrorAction SilentlyContinue) -and
    $PSCmdlet.ShouldProcess("$runKey\$productName", 'Delete start at sign-in entry')) {
    Remove-ItemProperty -LiteralPath $runKey -Name $productName
}
if ((Test-Path -LiteralPath $uninstallKey) -and $PSCmdlet.ShouldProcess($uninstallKey, 'Delete Settings > Apps entry')) {
    Remove-Item -LiteralPath $uninstallKey -Recurse -Force
}

$removedInPlace = $manifest -and $manifest.inPlace
if ($manifest -and -not $removedInPlace -and $PSCmdlet.ShouldProcess($target, "Delete $(@($manifest.files).Count) installed files")) {
    $kept = @()
    foreach ($relative in @($manifest.files) + @($manifestName)) {
        $path = Join-Path $target $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        try { Remove-Item -LiteralPath $path -Force } catch { $kept += $path }
    }
    foreach ($directory in Get-ChildItem -LiteralPath $target -Directory -Recurse -Force | Sort-Object { $_.FullName.Length } -Descending) {
        if (@(Get-ChildItem -LiteralPath $directory.FullName -Force).Count -eq 0) { Remove-Item -LiteralPath $directory.FullName -Force }
    }
    if (@(Get-ChildItem -LiteralPath $target -Force).Count -eq 0) {
        Remove-Item -LiteralPath $target -Force
    }
    else {
        Write-Warning "$target still contains files that this installer did not create$(if ($kept) { ' or could not delete: ' + ($kept -join ', ') }); remove the folder yourself."
    }
}
elseif ($removedInPlace -and (Test-Path -LiteralPath (Join-Path $target $manifestName)) -and
    $PSCmdlet.ShouldProcess((Join-Path $target $manifestName), 'Delete installation manifest')) {
    Remove-Item -LiteralPath (Join-Path $target $manifestName) -Force
}

$userData = Get-UserDataDirectory
if ($PurgeUserData -and (Test-Path -LiteralPath $userData) -and $PSCmdlet.ShouldProcess($userData, 'Delete settings, logs, and crash dumps')) {
    Remove-Item -LiteralPath $userData -Recurse -Force
}

if ($WhatIfPreference) { return }
Write-Host "Removed $productName." -ForegroundColor Green
if (-not $PurgeUserData -and (Test-Path -LiteralPath $userData)) {
    Write-Host "  Your settings, logs, and crash dumps remain under $userData (use -PurgeUserData to delete them)."
}
if ($removedInPlace -and $isWingetPackage) {
    Write-Host '  The package files belong to winget: winget uninstall RedSalamanders.RedXe'
}
