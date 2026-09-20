Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# PE machine probe shared with the camera package tooling.
Import-Module (Join-Path $PSScriptRoot "CameraPackage.psm1") -Force

# Portable ZIP packaging. See Specs/Build/Build_Packaging.md for the contract this module implements.

$script:PackageProduct = 'RedXe'
$script:PackageRootBinaries = @('RedXe.exe', 'RedXeLauncher.exe', 'yyjson.dll')
$script:PackageSettingsFiles = @('RedXe.settings.json', 'RedXe-debug.settings.json', 'RedXe.settings.schema.json')
$script:PackagePluginHelpers = @('AVControlBroker.exe', 'AVControlCamera.dll', 'AVControlCameraSetup.exe',
    'WeatherLocation.exe', 'libcurl.dll', 'yyjson.dll', 'weathericons-regular-webfont.ttf', 'OFL.txt')
$script:PackageInstallerFiles = @('Install-RedXe.ps1', 'install.cmd', 'uninstall.cmd')
$script:PackageBuildArtifactExtensions = @('.pdb', '.lib', '.exp', '.ilk', '.iobj', '.ipdb', '.obj', '.log', '.tlog')
# Licensed by Zoom and imported per developer; never redistributed (ThirdParty/ZoomPluginSdk/README.md).
$script:PackageExcludedPluginDirectories = @('ZoomSdk')
$script:PackageVcRuntimeRequired = @('msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')

function Get-RedXePackageName {
    param([Parameter(Mandatory)][string] $Version, [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string] $Platform)
    "$script:PackageProduct-$Version-$Platform-Portable.zip"
}

<#
.SYNOPSIS
Plugin DLL names the host catalog (RedXe/BundledPlugins.h) can load; every one must ship.
#>
function Get-RedXeBundledPluginModules {
    param([Parameter(Mandatory)][string] $RepoRoot)
    $catalog = Join-Path $RepoRoot 'RedXe\BundledPlugins.h'
    $content = Get-Content -LiteralPath $catalog -Raw
    $names = [regex]::Matches($content, 'RedXeBundledPluginSpec\{"[^"]+",\s*L"([^"]+\.dll)"\}') |
        ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
    if (@($names).Count -eq 0) { throw "No bundled plugin modules were found in $catalog." }
    return @($names)
}

<#
.SYNOPSIS
Every relative path a complete package must contain for the platform (ordinal, forward slashes).
#>
function Get-RedXePackageRequiredEntries {
    param([Parameter(Mandatory)][string] $RepoRoot)
    $entries = [Collections.Generic.List[string]]::new()
    foreach ($name in $script:PackageRootBinaries + $script:PackageInstallerFiles + @('README.txt', 'LICENSE.txt')) { $entries.Add($name) }
    foreach ($name in $script:PackageSettingsFiles) { $entries.Add("Settings/$name") }
    foreach ($name in (Get-RedXeBundledPluginModules -RepoRoot $RepoRoot) + $script:PackagePluginHelpers) { $entries.Add("Plugins/$name") }
    foreach ($name in $script:PackageVcRuntimeRequired) { $entries.Add($name); $entries.Add("Plugins/$name") }
    return @($entries | Sort-Object -Unique)
}

function Get-RedXePackageForbiddenPatterns {
    return @($script:PackageBuildArtifactExtensions | ForEach-Object { "*$_" }) + @('Plugins/ZoomSdk/*', '*Tests.exe', 'SystemDataPhase0.exe')
}

<#
.SYNOPSIS
The newest installed Visual C++ redistributable CRT directory for the target architecture.
#>
function Get-RedXeVcRuntimeDirectory {
    param([Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string] $Platform, [string[]] $SearchRoots)
    $architecture = if ($Platform -eq 'ARM64') { 'arm64' } else { 'x64' }
    if (-not $SearchRoots) {
        $SearchRoots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ } |
            ForEach-Object { Join-Path $_ 'Microsoft Visual Studio' } | Where-Object { Test-Path -LiteralPath $_ }
    }
    $pattern = '\\VC\\Redist\\MSVC\\(?<version>[^\\]+)\\' + $architecture + '\\Microsoft\.VC\d+\.CRT$'
    $candidates = foreach ($root in $SearchRoots) {
        foreach ($directory in Get-ChildItem -LiteralPath $root -Directory -Recurse -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue) {
            $match = [regex]::Match($directory.FullName, $pattern)
            if (-not $match.Success) { continue }
            $version = [version]'0.0'
            [void][version]::TryParse($match.Groups['version'].Value, [ref] $version)
            [pscustomobject]@{ Directory = $directory.FullName; Version = $version }
        }
    }
    $best = @($candidates | Sort-Object Version, Directory -Descending | Select-Object -First 1)
    if ($best.Count -eq 0) {
        throw "No Visual C++ redistributable CRT directory for $Platform was found under $($SearchRoots -join '; '). Install the MSVC v145 $Platform build tools."
    }
    foreach ($name in $script:PackageVcRuntimeRequired) {
        if (-not (Test-Path -LiteralPath (Join-Path $best[0].Directory $name) -PathType Leaf)) { throw "Required CRT DLL $name is missing from $($best[0].Directory)." }
    }
    return $best[0].Directory
}

function Copy-RedXePackageFile {
    param([Parameter(Mandatory)][string] $Source, [Parameter(Mandatory)][string] $Destination)
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Package input is missing: $Source" }
    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $parent)) { [void](New-Item -ItemType Directory -Path $parent -Force) }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function New-RedXePackageReadme {
    param([Parameter(Mandatory)][string] $Version, [Parameter(Mandatory)][string] $Platform)
    @"
RedXe $Version ($Platform) - portable package
=============================================

RedXe is the native Windows dashboard for the CORSAIR XENEON EDGE: pages of widgets on a 2560x720 canvas, or a bar
on any screen edge. Everything it needs is in this folder; nothing is written outside %LocalAppData%\RedXe.

RUN IT FROM HERE
----------------
  RedXe.exe                Open the dashboard (fullscreen on the XENEON, or a titled window).
  RedXe.exe --help         Every command-line switch, including --dock, --settings, and --screenshot.

INSTALL (optional)
------------------
  install.cmd              Copy this package to %LocalAppData%\Programs\RedXe, add a Start Menu shortcut, and list
                           RedXe under Settings > Apps > Installed apps.
  Install-RedXe.ps1        The same, with options: -StartAtSignIn (run when you sign in), -Launch, -Destination,
                           -InPlace (register this folder without copying), -WhatIf. Run it with PowerShell.
  uninstall.cmd            Remove the shortcut, the sign-in entry, the Apps entry, and the installed copy.
                           Your settings under %LocalAppData%\RedXe stay unless you pass -PurgeUserData.

INSTALLED WITH WINGET?
----------------------
  winget install RedSalamanders.RedXe   Installs this package and the "RedXe" command for new terminals.
  To add the Start Menu shortcut or start at sign-in, run install.cmd from the package folder,
  %LocalAppData%\Microsoft\WinGet\Packages\RedSalamanders.RedXe_Microsoft.Winget.Source_8wekyb3d8bbwe.
  Inside a winget package folder the installer registers that folder in place (winget keeps owning the files
  and the Apps entry); use "winget uninstall RedSalamanders.RedXe" to remove the package.

WHAT IS IN HERE
---------------
  RedXe.exe                The dashboard.
  RedXeLauncher.exe        The "RedXe" command alias target; starts RedXe.exe from this folder.
  Plugins\                 Every bundled widget and service, their helpers, and their runtime DLLs.
  Settings\                The shipped settings templates and the JSON schema. Your copy lives under
                           %LocalAppData%\RedXe\Settings after the first start; edit it while RedXe runs.
  msvcp140*.dll, vcruntime140*.dll
                           The Microsoft Visual C++ runtime for this CPU architecture, so no separate install is needed.
  LICENSE.txt              MIT license, the third-party notices, and the files under other terms (CC BY-NC-SA shader
                           ports, the OFL Weather Icons font).

REQUIREMENTS
------------
  Windows 10 version 2004 (build 19041) or later, x64 or ARM64 to match this package. A Direct3D 11 GPU is
  recommended; --warp renders on the Windows software driver.

The Zoom action pack in this package runs without the Zoom Plugin SDK (which Zoom licenses to developers only) and
reports zoom-sdk-unavailable; every other widget and service is complete.

https://github.com/RedSalamanders/RedXe
"@
}

<#
.SYNOPSIS
Stages a complete Release output profile into a versioned portable ZIP under .build/packages and validates it.
.OUTPUTS
An object with Version, Platform, ZipPath, Sha256Path, Sha256, and the ordinal entry list.
#>
function New-RedXePortablePackage {
    param(
        [Parameter(Mandatory)][string] $RepoRoot,
        [ValidateSet('Release')][string] $Configuration = 'Release',
        [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string] $Platform,
        [ValidateRange(0, 65535)][int] $BuildNumber = 0,
        [string] $OutputDirectory,
        [string[]] $VcRuntimeSearchRoots,
        [switch] $SkipExecution
    )
    Import-Module (Join-Path $RepoRoot 'Build\Versioning.psm1') -Force
    $BuildNumber = Resolve-RedXeBuildNumber -RepoRoot $RepoRoot -Requested $BuildNumber
    $version = (Get-RedXeVersion -RepoRoot $RepoRoot -BuildNumber $BuildNumber).Version
    $buildOutput = Join-Path $RepoRoot ".build\$Platform\$Configuration"
    if (-not (Test-Path -LiteralPath (Join-Path $buildOutput 'RedXe.exe') -PathType Leaf)) {
        throw "Build output not found: $buildOutput. Run .\build.ps1 -Configuration $Configuration -Platform $Platform first."
    }
    $expectedFileVersion = (Get-RedXeVersion -RepoRoot $RepoRoot -BuildNumber $BuildNumber).FileVersion
    foreach ($binary in @('RedXe.exe', 'RedXeLauncher.exe')) {
        $info = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $buildOutput $binary))
        if ($info.FileVersion -ne $expectedFileVersion -or $info.ProductName -ne $script:PackageProduct -or $info.IsDebug) {
            throw "$binary reports version '$($info.FileVersion)' (debug=$($info.IsDebug)); the package needs a Release build stamped $expectedFileVersion. Rebuild with -BuildNumber $BuildNumber."
        }
    }
    if (-not $OutputDirectory) { $OutputDirectory = Join-Path $RepoRoot '.build\packages' }
    [void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
    $zipName = Get-RedXePackageName -Version $version -Platform $Platform
    $zipPath = Join-Path $OutputDirectory $zipName
    $staging = Join-Path $OutputDirectory ('stage-' + [guid]::NewGuid().ToString('N'))
    [void](New-Item -ItemType Directory -Path $staging)
    try {
        foreach ($name in $script:PackageRootBinaries) { Copy-RedXePackageFile (Join-Path $buildOutput $name) (Join-Path $staging $name) }
        foreach ($name in $script:PackageSettingsFiles) { Copy-RedXePackageFile (Join-Path $buildOutput "Settings\$name") (Join-Path $staging "Settings\$name") }
        $provenance = Join-Path $buildOutput 'DxUi.provenance.json'
        if (Test-Path -LiteralPath $provenance -PathType Leaf) { Copy-RedXePackageFile $provenance (Join-Path $staging 'DxUi.provenance.json') }

        $pluginsSource = Join-Path $buildOutput 'Plugins'
        if (-not (Test-Path -LiteralPath $pluginsSource -PathType Container)) { throw "Plugins output not found: $pluginsSource" }
        foreach ($file in Get-ChildItem -LiteralPath $pluginsSource -File -Recurse) {
            $relative = $file.FullName.Substring($pluginsSource.Length).TrimStart('\', '/')
            $topDirectory = ($relative -split '[\\/]')[0]
            if ($relative -match '[\\/]' -and $script:PackageExcludedPluginDirectories -contains $topDirectory) { continue }
            if ($script:PackageBuildArtifactExtensions -contains $file.Extension.ToLowerInvariant()) { continue }
            Copy-RedXePackageFile $file.FullName (Join-Path $staging "Plugins\$relative")
        }

        $crt = Get-RedXeVcRuntimeDirectory -Platform $Platform -SearchRoots $VcRuntimeSearchRoots
        foreach ($dll in Get-ChildItem -LiteralPath $crt -File -Filter '*.dll') {
            Copy-RedXePackageFile $dll.FullName (Join-Path $staging $dll.Name)
            Copy-RedXePackageFile $dll.FullName (Join-Path $staging "Plugins\$($dll.Name)")
        }

        foreach ($name in $script:PackageInstallerFiles) { Copy-RedXePackageFile (Join-Path $RepoRoot "Installer\$name") (Join-Path $staging $name) }
        Copy-RedXePackageFile (Join-Path $RepoRoot 'LICENSE.txt') (Join-Path $staging 'LICENSE.txt')
        [IO.File]::WriteAllText((Join-Path $staging 'README.txt'), (New-RedXePackageReadme -Version $version -Platform $Platform), [Text.UTF8Encoding]::new($false))

        $entries = @(Get-ChildItem -LiteralPath $staging -File -Recurse | ForEach-Object { $_.FullName.Substring($staging.Length).TrimStart('\', '/').Replace('\', '/') } | Sort-Object)
        Assert-RedXePackageEntries -RepoRoot $RepoRoot -Entries $entries

        $stagedZip = "$zipPath.$([guid]::NewGuid().ToString('N')).tmp"
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::CreateFromDirectory($staging, $stagedZip, [IO.Compression.CompressionLevel]::Optimal, $false)
        try {
            Test-RedXePortablePackage -RepoRoot $RepoRoot -ZipPath $stagedZip -Platform $Platform -Version $version -SkipExecution:$SkipExecution | Out-Null
            Move-Item -LiteralPath $stagedZip -Destination $zipPath -Force
        } finally {
            if (Test-Path -LiteralPath $stagedZip) { Remove-Item -LiteralPath $stagedZip -Force }
        }
    } finally {
        Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
    }

    $sha256 = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $sha256Path = "$zipPath.sha256"
    [IO.File]::WriteAllText($sha256Path, "$sha256 *$zipName`n", [Text.UTF8Encoding]::new($false))
    [pscustomobject]@{
        Version = $version
        Platform = $Platform
        ZipPath = $zipPath
        Sha256 = $sha256
        Sha256Path = $sha256Path
        Entries = $entries
    }
}

function Assert-RedXePackageEntries {
    param([Parameter(Mandatory)][string] $RepoRoot, [Parameter(Mandatory)][AllowEmptyCollection()][string[]] $Entries)
    $missing = @(Get-RedXePackageRequiredEntries -RepoRoot $RepoRoot | Where-Object { $Entries -cnotcontains $_ })
    if ($missing.Count) { throw "The package is missing required entries: $($missing -join ', ')" }
    foreach ($pattern in Get-RedXePackageForbiddenPatterns) {
        $hits = @($Entries | Where-Object { $_ -like $pattern })
        if ($hits.Count) { throw "The package must not contain '$pattern': $($hits -join ', ')" }
    }
}

<#
.SYNOPSIS
Expands a portable ZIP into a fresh directory, checks its contents, and (when the host can run the platform) starts
the packaged RedXe.exe --self-test --warp and RedXeLauncher.exe --help from that extraction.
#>
function Test-RedXePortablePackage {
    param(
        [Parameter(Mandatory)][string] $RepoRoot,
        [Parameter(Mandatory)][string] $ZipPath,
        [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string] $Platform,
        [string] $Version,
        [switch] $SkipExecution
    )
    if (-not (Test-Path -LiteralPath $ZipPath -PathType Leaf)) { throw "Package not found: $ZipPath" }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $entries = @($archive.Entries | Where-Object { -not $_.FullName.EndsWith('/') } | ForEach-Object { $_.FullName } | Sort-Object)
    } finally { $archive.Dispose() }
    if ($entries | Where-Object { $_ -match '^([A-Za-z]:|[\\/])' -or $_ -match '(^|/)\.\.(/|$)' -or $_.Contains('\') }) {
        throw 'The package contains a rooted, traversal, or backslash entry.'
    }
    Assert-RedXePackageEntries -RepoRoot $RepoRoot -Entries $entries

    $extraction = Join-Path $RepoRoot ('.build\packages\smoke-' + [guid]::NewGuid().ToString('N'))
    [void](New-Item -ItemType Directory -Path $extraction -Force)
    try {
        [IO.Compression.ZipFile]::ExtractToDirectory($ZipPath, $extraction)
        foreach ($binary in @('RedXe.exe', 'RedXeLauncher.exe')) {
            $info = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $extraction $binary))
            if ($info.ProductName -ne $script:PackageProduct -or $info.IsDebug) { throw "$binary in the package is not a Release $script:PackageProduct binary." }
            if ($Version -and $info.FileVersion -ne "$Version.0") { throw "$binary in the package reports $($info.FileVersion), expected $Version.0." }
            if ((Get-CameraBinaryMachine (Join-Path $extraction $binary)) -ne $Platform) { throw "$binary in the package is not a $Platform image." }
        }
        $hostArchitecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
        $canExecute = ($Platform -eq 'x64' -and $hostArchitecture -in @('X64', 'Arm64')) -or ($Platform -eq 'ARM64' -and $hostArchitecture -eq 'Arm64')
        if ($SkipExecution -or -not $canExecute) {
            return [pscustomobject]@{ Entries = $entries; ExecutionSkipped = $true }
        }
        $selfTest = Start-Process -FilePath (Join-Path $extraction 'RedXe.exe') -ArgumentList @('--self-test', '--warp') -WorkingDirectory $extraction -WindowStyle Hidden -Wait -PassThru
        if ($selfTest.ExitCode -ne 0) { throw "The packaged RedXe.exe --self-test --warp exited with $($selfTest.ExitCode) from $extraction." }
        $helpLog = Join-Path $extraction 'launcher-help.log'
        $help = Start-Process -FilePath (Join-Path $extraction 'RedXeLauncher.exe') -ArgumentList @('--help') -WorkingDirectory $extraction -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $helpLog
        if ($help.ExitCode -ne 0 -or (Get-Content -LiteralPath $helpLog -Raw) -notmatch '--self-test') {
            throw "The packaged RedXeLauncher.exe --help exited with $($help.ExitCode) or printed no help."
        }
        return [pscustomobject]@{ Entries = $entries; ExecutionSkipped = $false }
    } finally {
        Remove-Item -LiteralPath $extraction -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Export-ModuleMember -Function Get-RedXePackageName, Get-RedXeBundledPluginModules, Get-RedXePackageRequiredEntries,
    Get-RedXePackageForbiddenPatterns, Get-RedXeVcRuntimeDirectory, New-RedXePortablePackage, Test-RedXePortablePackage,
    Assert-RedXePackageEntries
