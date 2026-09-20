[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'ASan Debug')][string] $Configuration = 'Debug',
    [ValidateSet('x64', 'ARM64')][string] $Platform = 'x64'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repo 'Build\Versioning.psm1') -Force
Import-Module (Join-Path $repo 'Build\Package.psm1') -Force
Import-Module (Join-Path $repo 'Build\Winget.psm1') -Force

$fixtureParent = [IO.Path]::GetFullPath((Join-Path $repo '.build\BuildProcessTests'))
$fixture = Join-Path $fixtureParent ('Packaging-' + [guid]::NewGuid().ToString('N'))
$registryRoot = "HKCU:\Software\RedSalamanders\RedXePackagingTests-$([guid]::NewGuid().ToString('N'))"
[void](New-Item -ItemType Directory -Path $fixture -Force)

function Get-RunValue([string] $Root) {
    $item = Get-ItemProperty -LiteralPath (Join-Path $Root 'Run') -ErrorAction SilentlyContinue
    if ($item -and $item.PSObject.Properties["RedXe"]) { return [string] $item.RedXe }
    return $null
}

function Reject([string] $Scenario, [scriptblock] $Action) {
    $rejected = $false
    try { & $Action } catch { $rejected = $true }
    if (-not $rejected) { throw "Accepted: $Scenario" }
    Write-Host "PASS rejects $Scenario"
}

try {
    # Versioning: the header is the single source of major.minor; callers supply the build number.
    $version = Get-RedXeVersion -RepoRoot $repo -BuildNumber 183
    if ($version.Version -ne "$($version.Major).$($version.Minor).183" -or $version.FileVersion -ne "$($version.Version).0") { throw 'Get-RedXeVersion shape' }
    if ((Get-RedXeVersion -RepoRoot $repo).Build -ne 0) { throw 'Default build number must be 0.' }
    $parsed = ConvertTo-RedXePackageVersion -Version ' 1.0.42 '
    if ($parsed.Version -ne '1.0.42' -or $parsed.Build -ne 42) { throw 'ConvertTo-RedXePackageVersion' }
    Reject 'a four-part version' { ConvertTo-RedXePackageVersion -Version '1.0.42.0' }
    Reject 'a v-prefixed version' { ConvertTo-RedXePackageVersion -Version 'v1.0.42' }
    Write-Host 'PASS versioning'

    # Package layout: every catalogued plugin ships; build artifacts, tests, and the Zoom SDK never do.
    $modules = Get-RedXeBundledPluginModules -RepoRoot $repo
    if ($modules -notcontains 'MatrixRain.dll' -or $modules -notcontains 'zoom.action.dll') { throw 'Bundled plugin catalog was not read.' }
    $required = Get-RedXePackageRequiredEntries -RepoRoot $repo
    foreach ($entry in @('RedXe.exe', 'RedXeLauncher.exe', 'Install-RedXe.ps1', 'Settings/RedXe.settings.json', 'Plugins/MatrixRain.dll', 'msvcp140.dll', 'Plugins/vcruntime140.dll')) {
        if ($required -cnotcontains $entry) { throw "Required entry list lacks $entry" }
    }
    if ($required | Where-Object { $_.Contains('\') }) { throw 'Required entries must use forward slashes.' }
    Assert-RedXePackageEntries -RepoRoot $repo -Entries $required
    Reject 'a package missing a plugin' { Assert-RedXePackageEntries -RepoRoot $repo -Entries ($required | Where-Object { $_ -ne 'Plugins/MatrixRain.dll' }) }
    Reject 'a package with a PDB' { Assert-RedXePackageEntries -RepoRoot $repo -Entries ($required + 'RedXe.pdb') }
    Reject 'a package with the Zoom SDK' { Assert-RedXePackageEntries -RepoRoot $repo -Entries ($required + 'Plugins/ZoomSdk/tp.dll') }
    Reject 'a package with a test executable' { Assert-RedXePackageEntries -RepoRoot $repo -Entries ($required + 'SettingsTests.exe') }
    Write-Host 'PASS package entry policy'

    # CRT discovery picks the newest redistributable for the architecture and requires the complete DLL set.
    $redist = Join-Path $fixture 'VS'
    foreach ($spec in @(@{ v = '14.40.1'; a = 'x64' }, @{ v = '14.51.2'; a = 'x64' }, @{ v = '14.51.2'; a = 'arm64' })) {
        $dir = Join-Path $redist "VC\Redist\MSVC\$($spec.v)\$($spec.a)\Microsoft.VC145.CRT"
        [void](New-Item -ItemType Directory -Path $dir -Force)
        foreach ($name in @('msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) { Set-Content -LiteralPath (Join-Path $dir $name) -Value $spec.a }
    }
    if ((Get-RedXeVcRuntimeDirectory -Platform x64 -SearchRoots @($redist)) -notmatch '\\14\.51\.2\\x64\\') { throw 'Newest x64 CRT not selected.' }
    if ((Get-RedXeVcRuntimeDirectory -Platform ARM64 -SearchRoots @($redist)) -notmatch '\\14\.51\.2\\arm64\\') { throw 'ARM64 CRT not selected.' }
    Remove-Item -LiteralPath (Join-Path $redist 'VC\Redist\MSVC\14.51.2\arm64\Microsoft.VC145.CRT\vcruntime140_1.dll')
    Reject 'an incomplete CRT directory' { Get-RedXeVcRuntimeDirectory -Platform ARM64 -SearchRoots @($redist) }
    Reject 'a host without the redistributable' { Get-RedXeVcRuntimeDirectory -Platform x64 -SearchRoots @((Join-Path $fixture 'nothing')) }
    Write-Host 'PASS CRT discovery'

    # Winget manifest: exact asset names, both architectures, deterministic ReleaseDate, no placeholder left.
    $packages = Join-Path $fixture 'packages'
    [void](New-Item -ItemType Directory -Path $packages)
    $x64Zip = Join-Path $packages (Get-RedXePackageName -Version '1.0.42' -Platform x64)
    $arm64Zip = Join-Path $packages (Get-RedXePackageName -Version '1.0.42' -Platform ARM64)
    Set-Content -LiteralPath $x64Zip -Value 'x64 bytes'
    Set-Content -LiteralPath $arm64Zip -Value 'arm64 bytes'
    $manifest = New-RedXeWingetManifest -RepoRoot $repo -Version '1.0.42' -X64ZipPath $x64Zip -Arm64ZipPath $arm64Zip -OutputDirectory (Join-Path $fixture 'manifest') -ReleaseDate '2026-09-19'
    if ($manifest.Files.Count -ne 3) { throw 'Three manifest files expected.' }
    $installer = Get-Content -LiteralPath (Join-Path $manifest.OutputDirectory 'RedSalamanders.RedXe.installer.yaml') -Raw
    foreach ($needle in @('PackageVersion: 1.0.42', 'ReleaseDate: 2026-09-19', "InstallerSha256: $((Get-FileHash $x64Zip).Hash)", "InstallerSha256: $((Get-FileHash $arm64Zip).Hash)",
            'releases/download/v1.0.42/RedXe-1.0.42-x64-Portable.zip', 'releases/download/v1.0.42/RedXe-1.0.42-ARM64-Portable.zip', 'PortableCommandAlias: RedXe', 'RelativeFilePath: RedXeLauncher.exe')) {
        if (-not $installer.Contains($needle)) { throw "Installer manifest lacks '$needle'." }
    }
    foreach ($file in $manifest.Files) {
        $text = Get-Content -LiteralPath $file -Raw
        if ($text -match '\{[A-Z0-9_]+\}') { throw "Unresolved placeholder in $file" }
        if ($text.Contains("`r")) { throw "Manifest must use LF line endings: $file" }
    }
    Reject 'a zero build number' { New-RedXeWingetManifest -RepoRoot $repo -Version '1.0.0' -X64ZipPath $x64Zip -Arm64ZipPath $arm64Zip -OutputDirectory (Join-Path $fixture 'manifest0') }
    Reject 'a mismatched package name' { New-RedXeWingetManifest -RepoRoot $repo -Version '1.0.43' -X64ZipPath $x64Zip -Arm64ZipPath $arm64Zip -OutputDirectory (Join-Path $fixture 'manifest1') }
    Reject 'a malformed release date' { New-RedXeWingetManifest -RepoRoot $repo -Version '1.0.42' -X64ZipPath $x64Zip -Arm64ZipPath $arm64Zip -OutputDirectory (Join-Path $fixture 'manifest2') -ReleaseDate '19/09/2026' }
    Write-Host 'PASS winget manifest generation'

    # In-package installer: a synthetic package built from this profile's binaries, installed and removed in
    # isolation (scratch destination, Start Menu folder, and registry root) with the stock Windows PowerShell
    # that install.cmd uses. Nothing touches the user's real Start Menu or registry.
    $output = Join-Path $repo ".build\$Platform\$Configuration"
    $package = Join-Path $fixture 'package'
    [void](New-Item -ItemType Directory -Path (Join-Path $package 'Plugins') -Force)
    [void](New-Item -ItemType Directory -Path (Join-Path $package 'Settings') -Force)
    foreach ($name in @('RedXe.exe', 'RedXeLauncher.exe')) { Copy-Item -LiteralPath (Join-Path $output $name) -Destination (Join-Path $package $name) }
    Copy-Item -LiteralPath (Join-Path $output 'Settings\RedXe.settings.json') -Destination (Join-Path $package 'Settings\RedXe.settings.json')
    Set-Content -LiteralPath (Join-Path $package 'Plugins\Fixture.dll') -Value 'fixture'
    foreach ($name in @('Install-RedXe.ps1', 'install.cmd', 'uninstall.cmd')) { Copy-Item -LiteralPath (Join-Path $repo "Installer\$name") -Destination (Join-Path $package $name) }
    $script = Join-Path $package 'Install-RedXe.ps1'
    $destination = Join-Path $fixture 'installed'
    $startMenu = Join-Path $fixture 'start-menu'
    $hosts = @(@{ Name = 'pwsh'; Path = (Get-Process -Id $PID).Path })
    $windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    if (Test-Path -LiteralPath $windowsPowerShell) { $hosts += @{ Name = 'Windows PowerShell'; Path = $windowsPowerShell } }
    foreach ($shell in $hosts) {
        $common = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $script, '-StartMenuDirectory', $startMenu, '-RegistryRoot', $registryRoot)
        $whatIf = & $shell.Path @common -Destination $destination -WhatIf 2>&1 | ForEach-Object { $_.ToString() }
        if ($LASTEXITCODE -ne 0 -or (Test-Path -LiteralPath $destination) -or ($whatIf -match 'Set Alias')) { throw "$($shell.Name): -WhatIf changed state or printed module noise: $($whatIf -join ' | ')" }
        & $shell.Path @common -Destination $destination -StartAtSignIn *> $null
        if ($LASTEXITCODE -ne 0) { throw "$($shell.Name): install failed." }
        $expectedExecutable = Join-Path $destination 'RedXe.exe'
        if (-not (Test-Path -LiteralPath $expectedExecutable) -or -not (Test-Path -LiteralPath (Join-Path $destination 'Plugins\Fixture.dll'))) { throw "$($shell.Name): files were not copied." }
        $shortcut = Join-Path $startMenu 'RedXe.lnk'
        if (-not (Test-Path -LiteralPath $shortcut)) { throw "$($shell.Name): shortcut missing." }
        $shellObject = New-Object -ComObject WScript.Shell
        try { if ($shellObject.CreateShortcut($shortcut).TargetPath -ne $expectedExecutable) { throw "$($shell.Name): shortcut target." } }
        finally { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shellObject) }
        if ((Get-RunValue $registryRoot) -ne ('"{0}"' -f $expectedExecutable)) { throw "$($shell.Name): Run entry." }
        $apps = Get-ItemProperty -LiteralPath "$registryRoot\Uninstall\RedXe"
        if ($apps.DisplayName -ne 'RedXe' -or $apps.Publisher -ne 'RedSalamanders' -or $apps.InstallLocation -ne $destination -or $apps.UninstallString -notmatch 'Install-RedXe\.ps1" -Action Remove$') { throw "$($shell.Name): Apps entry." }
        $manifestPath = Join-Path $destination 'redxe-install.json'
        $installManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        if (@($installManifest.files).Count -ne @(Get-ChildItem -LiteralPath $package -File -Recurse).Count) { throw "$($shell.Name): manifest file list." }
        # A stale file from an earlier version disappears on reinstall; a foreign file is never deleted.
        Set-Content -LiteralPath (Join-Path $destination 'Plugins\Stale.dll') -Value 'stale'
        $installManifest.files += 'Plugins\Stale.dll'
        $installManifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $manifestPath
        & $shell.Path @common -Destination $destination *> $null
        if ($LASTEXITCODE -ne 0 -or (Test-Path -LiteralPath (Join-Path $destination 'Plugins\Stale.dll'))) { throw "$($shell.Name): reinstall did not replace the previous copy." }
        if (-not (Get-RunValue $registryRoot)) { throw "$($shell.Name): reinstall dropped the sign-in entry." }
        # Remove through the exact command Settings > Apps would run.
        $uninstall = [regex]::Match($apps.UninstallString, '^powershell\.exe (.+)$').Groups[1].Value
        $removal = Start-Process -FilePath $shell.Path -ArgumentList $uninstall -Wait -PassThru -WindowStyle Hidden
        if ($removal.ExitCode -ne 0) { throw "$($shell.Name): removal exit $($removal.ExitCode)." }
        if ((Test-Path -LiteralPath $destination) -or (Test-Path -LiteralPath $shortcut) -or (Test-Path -LiteralPath "$registryRoot\Uninstall\RedXe") -or
            (Get-RunValue $registryRoot)) { throw "$($shell.Name): removal left state behind." }
        Write-Host "PASS installer round-trip ($($shell.Name))"
    }
    $foreign = Join-Path $fixture 'foreign'
    [void](New-Item -ItemType Directory -Path $foreign)
    Set-Content -LiteralPath (Join-Path $foreign 'keep.txt') -Value 'keep'
    & $hosts[0].Path -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $script -Destination $foreign -StartMenuDirectory $startMenu -RegistryRoot $registryRoot *> $null
    if ($LASTEXITCODE -eq 0 -or -not (Test-Path -LiteralPath (Join-Path $foreign 'keep.txt')) -or (Test-Path -LiteralPath (Join-Path $foreign 'RedXe.exe'))) { throw 'A non-empty foreign destination was not refused.' }
    Write-Host 'PASS installer refuses a foreign destination'
    Write-Host 'Packaging tests passed.' -ForegroundColor Green
}
finally {
    $path = [IO.Path]::GetFullPath($fixture)
    if (-not $path.StartsWith($fixtureParent.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe fixture cleanup path.' }
    Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction SilentlyContinue
    if ($registryRoot -like 'HKCU:\Software\RedSalamanders\RedXePackagingTests-*') { Remove-Item -LiteralPath $registryRoot -Recurse -Force -ErrorAction SilentlyContinue }
}

# The last native command above is the expected refusal (exit 1); report the script result explicitly.
exit 0
