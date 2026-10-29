Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# PE machine probe shared with the camera package tooling.
Import-Module (Join-Path $PSScriptRoot "CameraPackage.psm1") -Force
# The bounded child runner for the package smoke. Without -Force: a nested forced import would remove the copy that
# test.ps1 imported for itself.
Import-Module (Join-Path $PSScriptRoot "StreamingProcess.psm1")

# Portable ZIP packaging. See Specs/Build/Build_Packaging.md for the contract this module implements.

$script:PackageProduct = 'RedXe'
$script:PackageRootBinaries = @('RedXe.exe', 'RedXeLauncher.exe', 'yyjson.dll')
$script:PackageSettingsFiles = @('RedXe.settings.json', 'RedXe-debug.settings.json', 'RedXe.settings.schema.json')
$script:PackagePluginHelpers = @('AVControlBroker.exe', 'AVControlCamera.dll', 'AVControlCameraSetup.exe',
    'WeatherLocation.exe', 'libcurl.dll', 'yyjson.dll', 'weathericons-regular-webfont.ttf', 'OFL.txt')
$script:PackageInstallerFiles = @('Install-RedXe.ps1', 'install.cmd', 'uninstall.cmd')
$script:PackageBuildArtifactExtensions = @('.pdb', '.lib', '.exp', '.ilk', '.iobj', '.ipdb', '.obj', '.log', '.tlog')
# Reject stale SDK files from older local builds; the browser-only Zoom plugin does not use them.
$script:PackageExcludedPluginDirectories = @('ZoomSdk')
$script:PackageVcRuntimeRequired = @('msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
# Required DLLs that vcpkg builds. Directory.Build.targets copies the DLLs into the output and never their PDBs, so, like
# the Visual C++ runtime above, they are shipped but have no PDB there and are not part of the symbols ZIP.
$script:PackageVcpkgRuntimeBinaries = @('libcurl.dll', 'yyjson.dll')
# The linker commits a PDB and its image within milliseconds of each other, in either order (this repository's Debug
# output shows the PDB between 0.5 ms before and 6 ms after its image). A PDB older than its binary by more than this
# was not written by the link that produced the binary.
$script:SymbolPdbSkew = [TimeSpan]::FromSeconds(2)
# ThirdParty/RedMonitor/LICENSE.txt is hash-pinned and is the whole RedSalamander licence: the MIT licence, then the
# notices of components RedXe does not ship. Only the MIT section is packaged. It is the heading, the copyright line and
# the permission, condition and warranty paragraphs (the first lines of each are pinned), and it must occur exactly once;
# a restructured vendored file fails packaging instead of shipping other text. Matched against LF-normalized text.
$script:RedMonitorMitSectionPattern =
    '(?m)^MIT License\n\n' +
    'Copyright \(c\) \d{4} [^\n]+\n\n' +
    'Permission is hereby granted, free of charge, to any person obtaining a copy\n(?:[^\n]+\n)+\n' +
    'The above copyright notice and this permission notice shall be included in all\n(?:[^\n]+\n)+\n' +
    'THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n(?:[^\n]+\n)*?SOFTWARE\.(?:\n|\z)'

function Get-RedXePackageName {
    param([Parameter(Mandatory)][string] $Version, [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string] $Platform)
    "$script:PackageProduct-$Version-$Platform-Portable.zip"
}

<#
.SYNOPSIS
The symbols ZIP that is released beside the portable ZIP of the same version and platform.
#>
function Get-RedXeSymbolArchiveName {
    param([Parameter(Mandatory)][string] $Version, [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string] $Platform)
    "$script:PackageProduct-$Version-$Platform-Symbols.zip"
}

<#
.SYNOPSIS
Writes the "<path>.sha256" sidecar every released ZIP carries and returns its hash and path.
.DESCRIPTION
One line, "<lowercase SHA-256> *<file name>" and a line feed, UTF-8 without a byte order mark: the form release.yml
verifies. The portable ZIP and the symbols ZIP both write their sidecar here, so the two cannot differ.
#>
function Write-RedXePackageChecksum {
    param([Parameter(Mandatory)][string] $Path)
    $sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    $sha256Path = "$Path.sha256"
    [IO.File]::WriteAllText($sha256Path, "$sha256 *$(Split-Path -Leaf $Path)`n", [Text.UTF8Encoding]::new($false))
    [pscustomobject]@{ Sha256 = $sha256; Sha256Path = $sha256Path }
}

<#
.SYNOPSIS
The MIT licence section of the vendored RedMonitor licence file: the text the package ships as RedMonitor-LICENSE.txt.
.DESCRIPTION
Deterministic: the section is located in the LF-normalized file and returned verbatim with one final line feed, so the
result does not depend on the checkout's line endings. Throws unless the section occurs exactly once.
#>
function Get-RedXeRedMonitorLicenseNotice {
    param([Parameter(Mandatory)][string] $LicensePath)
    if (-not (Test-Path -LiteralPath $LicensePath -PathType Leaf)) { throw "The RedMonitor licence file is missing: $LicensePath" }
    $text = [IO.File]::ReadAllText($LicensePath, [Text.UTF8Encoding]::new($false)).Replace("`r`n", "`n")
    $sections = @([regex]::Matches($text, $script:RedMonitorMitSectionPattern))
    if ($sections.Count -ne 1) {
        throw "RedMonitor-LICENSE.txt needs the MIT licence section of the RedMonitor client (the 'MIT License' heading, its copyright line, and the permission, condition and warranty paragraphs) exactly once in $LicensePath, but it occurs $($sections.Count) times. The vendored file is hash-pinned and is never edited; adjust RedMonitorMitSectionPattern in Build/Package.psm1 if the file was restructured."
    }
    return $sections[0].Value.TrimEnd("`n") + "`n"
}

<#
.SYNOPSIS
Fails unless the file holds exactly the MIT licence section and no other text (the packaged RedMonitor-LICENSE.txt).
#>
function Assert-RedXeRedMonitorLicenseNotice {
    param([Parameter(Mandatory)][string] $Path)
    $packaged = [IO.File]::ReadAllText($Path, [Text.UTF8Encoding]::new($false)).Replace("`r`n", "`n")
    if ($packaged -cne (Get-RedXeRedMonitorLicenseNotice -LicensePath $Path)) {
        throw "RedMonitor-LICENSE.txt must hold only the MIT licence section of the RedMonitor client, but it carries other text: $Path"
    }
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
    foreach ($name in $script:PackageRootBinaries + $script:PackageInstallerFiles + @('README.txt', 'LICENSE.txt', 'RedMonitor-LICENSE.txt')) { $entries.Add($name) }
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
  RedXe.exe                Open the dashboard: fullscreen on the XENEON. With no XENEON connected, the first start
                           (no settings file yet, not in a Remote Desktop session) runs it as an auto-hiding bar on a
                           screen edge instead, on the second screen when there is one; set "edge" to "none" in the
                           settings file to be offered a window instead.
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
  RedMonitor-LICENSE.txt   The MIT notice of the RedMonitor diagnostics client that RedXe.exe is built with.

REQUIREMENTS
------------
  Windows 10 version 2004 (build 19041) or later, x64 or ARM64 to match this package. A Direct3D 11 GPU is
  recommended; --warp renders on the Windows software driver.

The Zoom action pack opens meeting links in the default browser without a Zoom Workplace installation or
Marketplace application registration. Browser joining depends on the meeting host's Zoom settings.

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
        # The vendored file is the whole RedSalamander licence with notices for components RedXe does not ship; the
        # package carries only the client's MIT section, and packaging fails unless that section is found exactly once.
        [IO.File]::WriteAllText((Join-Path $staging 'RedMonitor-LICENSE.txt'),
            (Get-RedXeRedMonitorLicenseNotice -LicensePath (Join-Path $RepoRoot 'ThirdParty/RedMonitor/LICENSE.txt')), [Text.UTF8Encoding]::new($false))
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

    $checksum = Write-RedXePackageChecksum -Path $zipPath
    [pscustomobject]@{
        Version = $version
        Platform = $Platform
        ZipPath = $zipPath
        Sha256 = $checksum.Sha256
        Sha256Path = $checksum.Sha256Path
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
        # The RedMonitor notice is the client's MIT section alone, never the vendored file's third-party notices.
        Assert-RedXeRedMonitorLicenseNotice -Path (Join-Path $extraction 'RedMonitor-LICENSE.txt')
        $hostArchitecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
        $canExecute = ($Platform -eq 'x64' -and $hostArchitecture -in @('X64', 'Arm64')) -or ($Platform -eq 'ARM64' -and $hostArchitecture -eq 'Arm64')
        if ($SkipExecution -or -not $canExecute) {
            return [pscustomobject]@{ Entries = $entries; ExecutionSkipped = $true }
        }
        # Bounded and logged like test.ps1's runs, so a packaged self-test that hangs fails here with its log instead of
        # holding package.ps1 or the release job until the workflow's timeout. The logs outlive the extraction. As in
        # test.ps1, both streams are decoded as UTF-8, which RedXe writes to them (Common/FailureReports.h on stderr,
        # the command-line text on stdout).
        $smokeLogStem = Join-Path $RepoRoot ('.build\logs\package-smoke-' + [guid]::NewGuid().ToString('N'))
        $utf8 = [Text.UTF8Encoding]::new($false)
        $selfTestLog = "$smokeLogStem-self-test.log"
        $selfTestExit = Invoke-RedXeStreamingProcess -FilePath (Join-Path $extraction 'RedXe.exe') -Arguments @('--self-test', '--warp') `
            -WorkingDirectory $extraction -TimeoutSeconds 900 -LogPath $selfTestLog -StandardOutputEncoding $utf8 -StandardErrorEncoding $utf8 `
            -OutputLineCallback { param([string] $Line, [bool] $IsError) }
        if ($selfTestExit -ne 0) { throw "The packaged RedXe.exe --self-test --warp exited with $selfTestExit from $extraction (log: $selfTestLog)." }
        $helpLog = "$smokeLogStem-launcher-help.log"
        $helpExit = Invoke-RedXeStreamingProcess -FilePath (Join-Path $extraction 'RedXeLauncher.exe') -Arguments @('--help') `
            -WorkingDirectory $extraction -TimeoutSeconds 120 -LogPath $helpLog -StandardOutputEncoding $utf8 -StandardErrorEncoding $utf8 `
            -OutputLineCallback { param([string] $Line, [bool] $IsError) }
        if ($helpExit -ne 0 -or (Get-Content -LiteralPath $helpLog -Raw) -notmatch '--self-test') {
            throw "The packaged RedXeLauncher.exe --help exited with $helpExit or printed no help (log: $helpLog)."
        }
        return [pscustomobject]@{ Entries = $entries; ExecutionSkipped = $false }
    } finally {
        Remove-Item -LiteralPath $extraction -Recurse -Force -ErrorAction SilentlyContinue
    }
}

<#
.SYNOPSIS
The executables and DLLs a complete package must contain that this repository builds, as package entries.
.DESCRIPTION
The required .exe/.dll entries (Get-RedXePackageRequiredEntries) without the vcpkg DLLs and the Visual C++ runtime that
the build copies in. Each has a PDB beside it in the Release output, so a missing one is an error. Any other shipped
executable (for example Plugins/LogiconProbe.exe) contributes its PDB when it has one.
#>
function Get-RedXePackageFirstPartyBinaries {
    param([Parameter(Mandatory)][string] $RepoRoot)
    $foreign = @($script:PackageVcpkgRuntimeBinaries) + @($script:PackageVcRuntimeRequired)
    return @(Get-RedXePackageRequiredEntries -RepoRoot $RepoRoot | Where-Object {
            $_ -match '\.(exe|dll)$' -and $foreign -notcontains $_.Substring($_.LastIndexOf('/') + 1)
        })
}

<#
.SYNOPSIS
Selects the PDB of every first-party executable and DLL in a package listing, from the Release output it was built from.
.DESCRIPTION
Each shipped .exe/.dll is matched by base name to the PDB beside it in the build output, in the same relative folder, so
Plugins/MatrixRain.dll gives Plugins/MatrixRain.pdb. A binary with no PDB there is third-party (the Visual C++ runtime,
the vcpkg DLLs) and is left out, unless it is a required first-party binary (Get-RedXePackageFirstPartyBinaries), which
fails naming the binary. A PDB older than its binary by more than the linker's own few milliseconds fails too. Only the
package's own entries are looked up, so test executables and anything else in the output are never considered.
.OUTPUTS
An object with Files (Entry: the PDB's path in the ZIP; Source: the file; Binary: the package entry it belongs to) and
Excluded (the shipped binaries without a PDB), both in ordinal order.
#>
function Get-RedXeSymbolFiles {
    param(
        [Parameter(Mandatory)][string] $RepoRoot,
        [Parameter(Mandatory)][string] $BuildOutput,
        [Parameter(Mandatory)][AllowEmptyCollection()][string[]] $PackageEntries
    )
    $firstParty = @(Get-RedXePackageFirstPartyBinaries -RepoRoot $RepoRoot)
    $binaries = [Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
    foreach ($entry in $PackageEntries) { if ($entry -match '\.(exe|dll)$') { [void]$binaries.Add($entry) } }
    $files = [Collections.Generic.List[object]]::new()
    $excluded = [Collections.Generic.List[string]]::new()
    foreach ($entry in $binaries) {
        $binaryPath = Join-Path $BuildOutput $entry.Replace('/', '\')
        $pdbEntry = $entry -replace '\.(exe|dll)$', '.pdb'
        $pdbPath = Join-Path $BuildOutput $pdbEntry.Replace('/', '\')
        if (-not (Test-Path -LiteralPath $pdbPath -PathType Leaf)) {
            if ($firstParty -ccontains $entry) {
                throw "No PDB for the first-party binary '$entry': expected $pdbPath. A Release crash dump cannot be symbolized without the exact PDB of every shipped RedXe binary; rebuild Release with debug information (or, for a third-party DLL, list it in PackageVcpkgRuntimeBinaries in Build/Package.psm1)."
            }
            $excluded.Add($entry)
            continue
        }
        if (-not (Test-Path -LiteralPath $binaryPath -PathType Leaf)) {
            throw "The package ships '$entry', but $binaryPath is not in the build output that holds its PDB, so the PDB cannot be matched to the shipped binary."
        }
        $binary = Get-Item -LiteralPath $binaryPath
        $pdb = Get-Item -LiteralPath $pdbPath
        if ($pdb.LastWriteTimeUtc.Add($script:SymbolPdbSkew) -lt $binary.LastWriteTimeUtc) {
            throw "The PDB for '$entry' is older than the binary (PDB $($pdb.LastWriteTimeUtc.ToString('o')), binary $($binary.LastWriteTimeUtc.ToString('o'))): $pdbPath. It was not written by the link that produced the shipped binary; rebuild Release."
        }
        $files.Add([pscustomobject]@{ Entry = $pdbEntry; Source = $pdb.FullName; Binary = $entry })
    }
    [pscustomobject]@{ Files = @($files); Excluded = @($excluded) }
}

<#
.SYNOPSIS
Writes .build/packages/RedXe-<version>-<platform>-Symbols.zip beside the portable ZIP, with its .sha256 sidecar.
.DESCRIPTION
Release binaries are optimized and link-time-code-generated, so a minidump from %LocalAppData%\RedXe\Crashes can only be
symbolized with the exact PDBs. The ZIP holds the PDB of every first-party executable and DLL that the portable package
lists (Get-RedXeSymbolFiles, from the Release output that package was built from), at the package's relative paths, and
nothing else. A missing or stale PDB fails with the binary named. The ZIP is staged under a temporary name, its entry
list and sizes are read back, and only then is it moved into place; a failure leaves neither the ZIP nor a sidecar,
not even one from an earlier run, because that would pair PDBs of one build with the binaries of another.
.OUTPUTS
An object with Version, Platform, ZipPath, Sha256, Sha256Path, Entries (the ZIP's entry names), Binaries (the package
entries they belong to), and Excluded (the shipped binaries that have no PDB).
#>
function New-RedXeSymbolArchive {
    param(
        [Parameter(Mandatory)][string] $RepoRoot,
        [Parameter(Mandatory)][string] $Version,
        [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string] $Platform,
        [Parameter(Mandatory)][AllowEmptyCollection()][string[]] $PackageEntries,
        [string] $BuildOutput,
        [string] $OutputDirectory
    )
    if (-not $BuildOutput) { $BuildOutput = Join-Path $RepoRoot ".build\$Platform\Release" }
    if (-not $OutputDirectory) { $OutputDirectory = Join-Path $RepoRoot '.build\packages' }
    [void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
    $zipPath = Join-Path $OutputDirectory (Get-RedXeSymbolArchiveName -Version $Version -Platform $Platform)
    Remove-Item -LiteralPath $zipPath, "$zipPath.sha256" -Force -ErrorAction SilentlyContinue
    $selection = Get-RedXeSymbolFiles -RepoRoot $RepoRoot -BuildOutput $BuildOutput -PackageEntries $PackageEntries
    if ($selection.Files.Count -eq 0) { throw "The package lists no executable or DLL with a PDB in $BuildOutput; there is nothing to archive." }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $stagedZip = "$zipPath.$([guid]::NewGuid().ToString('N')).tmp"
    try {
        $archive = [IO.Compression.ZipFile]::Open($stagedZip, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($file in $selection.Files) {
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.Source, $file.Entry, [IO.Compression.CompressionLevel]::Optimal)
            }
        } finally { $archive.Dispose() }
        $written = [IO.Compression.ZipFile]::OpenRead($stagedZip)
        try { $actual = @($written.Entries | ForEach-Object { '{0}|{1}' -f $_.FullName, $_.Length }) } finally { $written.Dispose() }
        $expected = @($selection.Files | ForEach-Object { '{0}|{1}' -f $_.Entry, (Get-Item -LiteralPath $_.Source).Length })
        if (($actual -join "`n") -cne ($expected -join "`n")) {
            throw "The staged symbols ZIP does not hold exactly the selected PDBs. Expected: $($expected -join ', '). Found: $($actual -join ', ')."
        }
        Move-Item -LiteralPath $stagedZip -Destination $zipPath -Force
    } finally {
        if (Test-Path -LiteralPath $stagedZip) { Remove-Item -LiteralPath $stagedZip -Force }
    }
    $checksum = Write-RedXePackageChecksum -Path $zipPath
    [pscustomobject]@{
        Version = $Version
        Platform = $Platform
        ZipPath = $zipPath
        Sha256 = $checksum.Sha256
        Sha256Path = $checksum.Sha256Path
        Entries = @($selection.Files | ForEach-Object { $_.Entry })
        Binaries = @($selection.Files | ForEach-Object { $_.Binary })
        Excluded = $selection.Excluded
    }
}

Export-ModuleMember -Function Get-RedXePackageName, Get-RedXeSymbolArchiveName, Write-RedXePackageChecksum, Get-RedXeBundledPluginModules,
    Get-RedXePackageRequiredEntries, Get-RedXePackageForbiddenPatterns, Get-RedXeVcRuntimeDirectory, New-RedXePortablePackage,
    Test-RedXePortablePackage, Assert-RedXePackageEntries, Get-RedXeRedMonitorLicenseNotice, Assert-RedXeRedMonitorLicenseNotice,
    Get-RedXeSymbolFiles, New-RedXeSymbolArchive
