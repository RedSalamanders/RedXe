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

# -Naming lists text the failure message must contain (the binary or file a failure has to name).
function Reject([string] $Scenario, [scriptblock] $Action, [string[]] $Naming = @()) {
    $message = $null
    try { & $Action } catch { $message = $_.Exception.Message }
    if ($null -eq $message) { throw "Accepted: $Scenario" }
    foreach ($fragment in $Naming) {
        if (-not $message.Contains($fragment)) { throw "Rejected $Scenario without naming '$fragment': $message" }
    }
    Write-Host "PASS rejects $Scenario"
}

try {
    # Versioning: the header is the single source of major.minor; callers supply the build number.
    $version = Get-RedXeVersion -RepoRoot $repo -BuildNumber 183
    if ($version.Version -ne "$($version.Major).$($version.Minor).183" -or $version.FileVersion -ne "$($version.Version).0") { throw 'Get-RedXeVersion shape' }
    if ((Get-RedXeVersion -RepoRoot $repo).Build -ne 0) { throw 'Get-RedXeVersion without a build number must report 0.' }
    # The default build number is the commit count of HEAD (0 when this checkout itself is shallow); an explicit
    # number always wins.
    $commitCount = if ((git -C $repo rev-parse --is-shallow-repository) -eq 'true') { 0 } else { [int] (git -C $repo rev-list --count HEAD) }
    if ((Get-RedXeDefaultBuildNumber -RepoRoot $repo -WarningAction SilentlyContinue) -ne $commitCount) { throw 'Default build number is not the commit count.' }
    if ((Resolve-RedXeBuildNumber -RepoRoot $repo -WarningAction SilentlyContinue) -ne $commitCount -or (Resolve-RedXeBuildNumber -RepoRoot $repo -Requested 7) -ne 7) { throw 'Resolve-RedXeBuildNumber' }
    # A source archive extracted inside another checkout (this fixture sits inside the repository's .build) must not
    # borrow the enclosing repository's count.
    $nested = Join-Path $fixture 'extracted-archive'
    [void](New-Item -ItemType Directory -Path $nested)
    $nestedWarnings = @()
    if ((Get-RedXeDefaultBuildNumber -RepoRoot $nested -WarningVariable nestedWarnings -WarningAction SilentlyContinue) -ne 0 -or $nestedWarnings.Count -ne 1) {
        throw 'A directory that is not the root of a checkout must yield 0 with a warning.'
    }
    # A shallow clone would count 1: it must also yield 0 rather than a truncated version.
    $shallow = Join-Path $fixture 'shallow-clone'
    git clone --quiet --depth 1 ("file:///" + $repo.Replace('\', '/')) $shallow 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0 -or (git -C $shallow rev-parse --is-shallow-repository) -ne 'true') { throw 'Could not create the shallow-clone fixture.' }
    $shallowWarnings = @()
    if ((Get-RedXeDefaultBuildNumber -RepoRoot $shallow -WarningVariable shallowWarnings -WarningAction SilentlyContinue) -ne 0 -or $shallowWarnings.Count -ne 1 -or $shallowWarnings[0] -notmatch 'Shallow') {
        throw 'A shallow clone must yield 0 with a shallow-clone warning.'
    }
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

    # RedMonitor notice: the vendored licence is the whole RedSalamander licence file, hash-pinned (read here, never
    # written). The package carries only its MIT section, found exactly once.
    $redMonitorLicense = Join-Path $repo 'ThirdParty\RedMonitor\LICENSE.txt'
    $redMonitorText = [IO.File]::ReadAllText($redMonitorLicense).Replace("`r`n", "`n")
    $notice = Get-RedXeRedMonitorLicenseNotice -LicensePath $redMonitorLicense
    if (-not $notice.StartsWith("MIT License`n`nCopyright (c) ") -or -not $notice.EndsWith("SOFTWARE.`n") -or $notice.Contains("`r")) { throw 'The RedMonitor notice has the wrong shape.' }
    foreach ($needle in @('Permission is hereby granted, free of charge', 'The above copyright notice and this permission notice shall be included', 'THE SOFTWARE IS PROVIDED "AS IS"')) {
        if (-not $notice.Contains($needle)) { throw "The RedMonitor notice lacks '$needle'." }
    }
    if ([regex]::Matches($notice, '(?m)^Copyright \(c\) ').Count -ne 1) { throw 'The RedMonitor notice must hold exactly one copyright line.' }
    foreach ($foreign in @('Third-Party', '7-Zip', 'unRAR', 'LibRaw', 'FTXUI', 'pe-parse', 'OpenSSL', 'Dracula')) {
        if ($notice.Contains($foreign)) { throw "The RedMonitor notice carries the notice of a component RedXe does not ship: $foreign" }
    }
    if (-not $redMonitorText.Contains($notice)) { throw 'The RedMonitor notice is not a verbatim section of the vendored licence.' }
    $noticeFixtures = Join-Path $fixture 'redmonitor-license'
    [void](New-Item -ItemType Directory -Path $noticeFixtures)
    $crlfLicense = Join-Path $noticeFixtures 'crlf-with-bom.txt'
    [IO.File]::WriteAllText($crlfLicense, $redMonitorText.Replace("`n", "`r`n"), [Text.UTF8Encoding]::new($true))
    if ((Get-RedXeRedMonitorLicenseNotice -LicensePath $crlfLicense) -cne $notice) { throw 'The RedMonitor notice depends on the line endings or a byte order mark.' }
    $lackingLicense = Join-Path $noticeFixtures 'lacking.txt'
    [IO.File]::WriteAllText($lackingLicense, "### curl`n`nPermission to use, copy, modify, and distribute this software for any purpose`nwith or without fee is hereby granted.`n")
    Reject 'a vendored licence without the MIT section' { Get-RedXeRedMonitorLicenseNotice -LicensePath $lackingLicense } -Naming @('exactly once', 'occurs 0 times', $lackingLicense)
    $truncatedLicense = Join-Path $noticeFixtures 'truncated.txt'
    [IO.File]::WriteAllText($truncatedLicense, $notice.Substring(0, $notice.IndexOf('THE SOFTWARE IS PROVIDED')) + "`n## Third-Party Licenses`n")
    Reject 'a vendored licence whose MIT section lost its warranty paragraph' { Get-RedXeRedMonitorLicenseNotice -LicensePath $truncatedLicense } -Naming @('exactly once', 'occurs 0 times')
    $twiceLicense = Join-Path $noticeFixtures 'twice.txt'
    [IO.File]::WriteAllText($twiceLicense, $notice + "`n" + $redMonitorText)
    Reject 'a vendored licence with two MIT sections' { Get-RedXeRedMonitorLicenseNotice -LicensePath $twiceLicense } -Naming @('exactly once', 'occurs 2 times')
    Reject 'a missing vendored licence' { Get-RedXeRedMonitorLicenseNotice -LicensePath (Join-Path $noticeFixtures 'absent.txt') } -Naming @('missing', 'absent.txt')
    # The smoke check of a built package: the shipped file is the MIT section and nothing else.
    $packagedNotice = Join-Path $noticeFixtures 'RedMonitor-LICENSE.txt'
    [IO.File]::WriteAllText($packagedNotice, $notice, [Text.UTF8Encoding]::new($false))
    Assert-RedXeRedMonitorLicenseNotice -Path $packagedNotice
    Reject 'the whole vendored licence as the packaged RedMonitor notice' { Assert-RedXeRedMonitorLicenseNotice -Path $redMonitorLicense } -Naming @('only the MIT licence section')
    Write-Host 'PASS RedMonitor notice extraction'

    # Symbols ZIP: a fake Release output (tiny stand-in .exe/.dll/.pdb files), a fake package listing, and a fake
    # bundled-plugin catalog. No real build, PDB, or package is involved.
    $symbolRepo = Join-Path $fixture 'symbol-repo'
    [void](New-Item -ItemType Directory -Path (Join-Path $symbolRepo 'RedXe') -Force)
    Set-Content -LiteralPath (Join-Path $symbolRepo 'RedXe\BundledPlugins.h') -Value @(
        'RedXeBundledPluginSpec{"builtin.alpha", L"Alpha.dll"},',
        'RedXeBundledPluginSpec{"builtin.beta", L"Beta.dll"},')
    $linked = [datetime]::new(2026, 10, 29, 12, 0, 0, [DateTimeKind]::Utc)
    $runtimeDlls = @('msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
    # First-party: the executables, the catalogued plugins, the helpers, and one extra shipped tool (LogiconProbe.exe).
    $firstPartyBinaries = @('RedXe.exe', 'RedXeLauncher.exe', 'Plugins/Alpha.dll', 'Plugins/Beta.dll', 'Plugins/AVControlBroker.exe', 'Plugins/AVControlCamera.dll',
        'Plugins/AVControlCameraSetup.exe', 'Plugins/WeatherLocation.exe', 'Plugins/LogiconProbe.exe')
    # Built elsewhere: vcpkg DLLs copied into the output (DLLs only), shipped with no PDB.
    $thirdPartyBinaries = @('yyjson.dll', 'Plugins/yyjson.dll', 'Plugins/libcurl.dll', 'Plugins/z.dll')
    $expectedPdbs = @('Plugins/AVControlBroker.pdb', 'Plugins/AVControlCamera.pdb', 'Plugins/AVControlCameraSetup.pdb', 'Plugins/Alpha.pdb', 'Plugins/Beta.pdb',
        'Plugins/LogiconProbe.pdb', 'Plugins/WeatherLocation.pdb', 'RedXe.pdb', 'RedXeLauncher.pdb')
    $symbolListing = $firstPartyBinaries + $thirdPartyBinaries + $runtimeDlls + @($runtimeDlls | ForEach-Object { "Plugins/$_" }) +
        @('LICENSE.txt', 'README.txt', 'Settings/RedXe.settings.json', 'Plugins/weathericons-regular-webfont.ttf', 'Plugins/OFL.txt')
    function New-FakeFile([string] $Path, [string] $Content, [datetime] $WhenUtc) {
        [void](New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force)
        [IO.File]::WriteAllText($Path, $Content)
        [IO.File]::SetLastWriteTimeUtc($Path, $WhenUtc)
    }
    # The PDB lands a few milliseconds after its image, as the linker's do. The Visual C++ runtime is not in the
    # build output at all; it is copied into the package from the redistributable.
    function New-SymbolOutput([string] $Name) {
        $output = Join-Path $fixture $Name
        foreach ($entry in $firstPartyBinaries) {
            $path = Join-Path $output $entry.Replace('/', '\')
            New-FakeFile $path "binary $entry" $linked
            New-FakeFile ([IO.Path]::ChangeExtension($path, '.pdb')) "pdb $entry" $linked.AddMilliseconds(4)
        }
        foreach ($entry in $thirdPartyBinaries) { New-FakeFile (Join-Path $output $entry.Replace('/', '\')) "third-party $entry" $linked.AddDays(-30) }
        # Build products no package ships: a test executable with its PDB, and a PDB whose binary is not shipped.
        New-FakeFile (Join-Path $output 'SettingsTests.exe') 'test binary' $linked
        New-FakeFile (Join-Path $output 'SettingsTests.pdb') 'test pdb' $linked
        New-FakeFile (Join-Path $output 'Plugins\Unshipped.pdb') 'unshipped pdb' $linked
        return $output
    }
    $symbolOutput = New-SymbolOutput 'symbol-output'
    $symbolPackages = Join-Path $fixture 'symbol-packages'
    $symbols = New-RedXeSymbolArchive -RepoRoot $symbolRepo -Version '1.0.42' -Platform x64 -PackageEntries $symbolListing -BuildOutput $symbolOutput -OutputDirectory $symbolPackages
    if ((Get-RedXeSymbolArchiveName -Version '1.0.42' -Platform ARM64) -cne 'RedXe-1.0.42-ARM64-Symbols.zip') { throw 'Symbols ZIP name (ARM64).' }
    if ($symbols.ZipPath -cne (Join-Path $symbolPackages 'RedXe-1.0.42-x64-Symbols.zip') -or -not (Test-Path -LiteralPath $symbols.ZipPath -PathType Leaf)) { throw "Symbols ZIP name or location: $($symbols.ZipPath)" }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $symbolZip = [IO.Compression.ZipFile]::OpenRead($symbols.ZipPath)
    try {
        $zipNames = @($symbolZip.Entries | ForEach-Object { $_.FullName })
        if ((@($zipNames | Sort-Object) -join '|') -cne (@($expectedPdbs | Sort-Object) -join '|')) { throw "The symbols ZIP must hold exactly the shipped first-party PDBs, but holds: $($zipNames -join ', ')" }
        if ((@($symbols.Entries | Sort-Object) -join '|') -cne (@($expectedPdbs | Sort-Object) -join '|')) { throw "The symbols result lists the wrong entries: $($symbols.Entries -join ', ')" }
        foreach ($zipEntry in $symbolZip.Entries) {
            $reader = [IO.StreamReader]::new($zipEntry.Open())
            try { $pdbContent = $reader.ReadToEnd() } finally { $reader.Dispose() }
            $owner = @($firstPartyBinaries | Where-Object { ($_ -replace '\.(exe|dll)$', '.pdb') -ceq $zipEntry.FullName })
            if ($owner.Count -ne 1 -or $pdbContent -cne "pdb $($owner[0])") { throw "Symbols ZIP entry $($zipEntry.FullName) is not the PDB of its binary." }
        }
    } finally { $symbolZip.Dispose() }
    Write-Host 'PASS symbols ZIP holds exactly the first-party PDBs at the package paths'
    # The Visual C++ runtime and the vcpkg DLLs ship without a PDB: they are left out, not an error.
    $expectedExcluded = $thirdPartyBinaries + $runtimeDlls + @($runtimeDlls | ForEach-Object { "Plugins/$_" })
    if ((@($symbols.Excluded | Sort-Object) -join '|') -cne (@($expectedExcluded | Sort-Object) -join '|')) { throw "Unexpected binaries without a PDB: $($symbols.Excluded -join ', ')" }
    Write-Host 'PASS runtime and third-party DLLs are excluded'

    # The sidecar is the portable ZIP's, byte for byte in form: "<lowercase sha256> *<name>", LF, no byte order mark.
    $portableStandIn = Join-Path $symbolPackages (Get-RedXePackageName -Version '1.0.42' -Platform x64)
    [IO.File]::WriteAllText($portableStandIn, 'stand-in for the portable ZIP')
    $portableChecksum = Write-RedXePackageChecksum -Path $portableStandIn
    foreach ($sidecar in @(@{ Zip = $portableStandIn; Sidecar = $portableChecksum.Sha256Path }, @{ Zip = $symbols.ZipPath; Sidecar = $symbols.Sha256Path })) {
        $zipLeaf = Split-Path -Leaf $sidecar.Zip
        $sidecarHash = (Get-FileHash -LiteralPath $sidecar.Zip -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($sidecar.Sidecar -cne "$($sidecar.Zip).sha256") { throw "Sidecar path for ${zipLeaf}: $($sidecar.Sidecar)" }
        $sidecarText = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($sidecar.Sidecar))
        if ($sidecarText -cne "$sidecarHash *$zipLeaf`n" -or $sidecarText -cnotmatch '^[0-9a-f]{64} \*RedXe-1\.0\.42-x64-(Portable|Symbols)\.zip\n$') { throw "Sidecar form for ${zipLeaf}: '$sidecarText'" }
        # The check release.yml applies to every released ZIP.
        if ((Get-Content -LiteralPath $sidecar.Sidecar -Raw).Trim() -ne "$sidecarHash *$zipLeaf") { throw "release.yml would reject the sidecar of $zipLeaf." }
    }
    if ($symbols.Sha256 -cne (Get-FileHash -LiteralPath $symbols.ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()) { throw 'The symbols result carries the wrong hash.' }
    Write-Host 'PASS symbols sidecar has the portable sidecar form'

    # A first-party binary without its PDB fails, naming the binary, and leaves no ZIP or sidecar behind, not even the
    # leftovers of an earlier run (they would pair PDBs of one build with the binaries of another).
    foreach ($victim in @('Plugins/Beta.dll', 'RedXe.exe')) {
        $tag = $victim -replace '[^A-Za-z]', ''
        $brokenOutput = New-SymbolOutput "symbol-output-missing-$tag"
        Remove-Item -LiteralPath ([IO.Path]::ChangeExtension((Join-Path $brokenOutput $victim.Replace('/', '\')), '.pdb'))
        $failedPackages = Join-Path $fixture "symbol-packages-missing-$tag"
        [void](New-Item -ItemType Directory -Path $failedPackages)
        $earlierZip = Join-Path $failedPackages 'RedXe-1.0.42-x64-Symbols.zip'
        [IO.File]::WriteAllText($earlierZip, 'left by an earlier run')
        [IO.File]::WriteAllText("$earlierZip.sha256", 'left by an earlier run')
        Reject "a missing PDB for $victim" {
            New-RedXeSymbolArchive -RepoRoot $symbolRepo -Version '1.0.42' -Platform x64 -PackageEntries $symbolListing -BuildOutput $brokenOutput -OutputDirectory $failedPackages
        } -Naming @("'$victim'", 'No PDB for the first-party binary')
        if (@(Get-ChildItem -LiteralPath $failedPackages -Force).Count -ne 0) { throw "A failed symbols run left files behind: $((Get-ChildItem -LiteralPath $failedPackages -Force).Name -join ', ')" }
    }
    # A shipped tool that neither the plugin catalog nor the helper list requires (LogiconProbe.exe) is archived when it
    # has a PDB and, like any other binary without one, left out when it has none.
    $probeOutput = New-SymbolOutput 'symbol-output-probe'
    Remove-Item -LiteralPath (Join-Path $probeOutput 'Plugins\LogiconProbe.pdb')
    $withoutProbe = New-RedXeSymbolArchive -RepoRoot $symbolRepo -Version '1.0.42' -Platform x64 -PackageEntries $symbolListing -BuildOutput $probeOutput -OutputDirectory (Join-Path $fixture 'symbol-packages-probe')
    if ($withoutProbe.Entries -ccontains 'Plugins/LogiconProbe.pdb' -or $withoutProbe.Excluded -cnotcontains 'Plugins/LogiconProbe.exe' -or $withoutProbe.Entries.Count -ne $expectedPdbs.Count - 1) { throw 'An unrequired binary without a PDB must be left out.' }
    $noBinary = New-SymbolOutput 'symbol-output-binary-missing'
    Remove-Item -LiteralPath (Join-Path $noBinary 'Plugins\Alpha.dll')
    Reject 'a PDB whose shipped binary is not in the output' {
        New-RedXeSymbolArchive -RepoRoot $symbolRepo -Version '1.0.42' -Platform x64 -PackageEntries $symbolListing -BuildOutput $noBinary -OutputDirectory (Join-Path $fixture 'symbol-packages-binary-missing')
    } -Naming @('Plugins/Alpha.dll', 'cannot be matched')
    Reject 'a package that lists no binary with a PDB' {
        New-RedXeSymbolArchive -RepoRoot $symbolRepo -Version '1.0.42' -Platform x64 -PackageEntries @('LICENSE.txt', 'msvcp140.dll') -BuildOutput $symbolOutput -OutputDirectory (Join-Path $fixture 'symbol-packages-empty')
    } -Naming @('nothing to archive')

    # A PDB older than its binary is not the PDB of that link; one within the linker's own skew is.
    $staleOutput = New-SymbolOutput 'symbol-output-stale'
    $stalePdb = Join-Path $staleOutput 'Plugins\Alpha.pdb'
    [IO.File]::SetLastWriteTimeUtc($stalePdb, $linked.AddMinutes(-10))
    Reject 'a PDB older than its binary' {
        New-RedXeSymbolArchive -RepoRoot $symbolRepo -Version '1.0.42' -Platform x64 -PackageEntries $symbolListing -BuildOutput $staleOutput -OutputDirectory (Join-Path $fixture 'symbol-packages-stale')
    } -Naming @("'Plugins/Alpha.dll'", 'older than the binary')
    [IO.File]::SetLastWriteTimeUtc($stalePdb, $linked.AddSeconds(-1))
    $skewed = New-RedXeSymbolArchive -RepoRoot $symbolRepo -Version '1.0.42' -Platform x64 -PackageEntries $symbolListing -BuildOutput $staleOutput -OutputDirectory (Join-Path $fixture 'symbol-packages-skew')
    if ($skewed.Entries -cnotcontains 'Plugins/Alpha.pdb') { throw 'A PDB within the linker skew of its binary must be accepted.' }
    Write-Host 'PASS missing and stale PDBs fail naming the binary'

    # The real catalog and required-entry rules: a package listing of every required entry, with a PDB for each
    # first-party binary (and none for the CRT and vcpkg DLLs), archives exactly those PDBs.
    $realRequired = Get-RedXePackageRequiredEntries -RepoRoot $repo
    $realOutput = Join-Path $fixture 'symbol-output-real'
    $notBuiltHere = $runtimeDlls + @('libcurl.dll', 'yyjson.dll')
    $realExpected = [Collections.Generic.List[string]]::new()
    foreach ($entry in @($realRequired | Where-Object { $_ -match '\.(exe|dll)$' })) {
        $leaf = Split-Path -Leaf $entry
        if ($runtimeDlls -notcontains $leaf) { New-FakeFile (Join-Path $realOutput $entry.Replace('/', '\')) "binary $entry" $linked }
        if ($notBuiltHere -notcontains $leaf) {
            $pdbEntry = $entry -replace '\.(exe|dll)$', '.pdb'
            New-FakeFile (Join-Path $realOutput $pdbEntry.Replace('/', '\')) "pdb $entry" $linked
            $realExpected.Add($pdbEntry)
        }
    }
    $realSymbols = New-RedXeSymbolArchive -RepoRoot $repo -Version '1.0.42' -Platform x64 -PackageEntries $realRequired -BuildOutput $realOutput -OutputDirectory (Join-Path $fixture 'symbol-packages-real')
    if ((@($realSymbols.Entries | Sort-Object) -join '|') -cne (@($realExpected | Sort-Object) -join '|')) { throw "The real required set archived: $($realSymbols.Entries -join ', ')" }
    foreach ($pdb in @('RedXe.pdb', 'RedXeLauncher.pdb', 'Plugins/MatrixRain.pdb', 'Plugins/zoom.action.pdb', 'Plugins/AVControlCamera.pdb', 'Plugins/WeatherLocation.pdb')) {
        if ($realSymbols.Entries -cnotcontains $pdb) { throw "The real required set lacks $pdb." }
    }
    Write-Host 'PASS symbols ZIP covers the real required first-party set'

    # What a built package cannot show here, because no real build exists in this suite: package.ps1 writes the ZIP,
    # both ZIPs write their sidecar through one writer, and the package carries and re-checks the RedMonitor notice.
    $wiring = @(
        @{ Function = 'New-RedXePortablePackage'; Calls = 'Write-RedXePackageChecksum' },
        @{ Function = 'New-RedXeSymbolArchive'; Calls = 'Write-RedXePackageChecksum' },
        @{ Function = 'New-RedXePortablePackage'; Calls = 'Get-RedXeRedMonitorLicenseNotice' },
        @{ Function = 'Test-RedXePortablePackage'; Calls = 'Assert-RedXeRedMonitorLicenseNotice' })
    foreach ($check in $wiring) {
        $calls = @((Get-Command $check.Function).ScriptBlock.Ast.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -eq $check.Calls }, $true))
        if ($calls.Count -ne 1) { throw "$($check.Function) must call $($check.Calls) exactly once (found $($calls.Count))." }
    }
    $parseTokens = $null
    $parseErrors = $null
    $packageScript = [Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'package.ps1'), [ref] $parseTokens, [ref] $parseErrors)
    $symbolCalls = @($packageScript.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -eq 'New-RedXeSymbolArchive' }, $true))
    if ($parseErrors.Count -ne 0 -or $symbolCalls.Count -ne 1) { throw 'package.ps1 must write the symbols ZIP through New-RedXeSymbolArchive.' }
    Write-Host 'PASS package.ps1 and the package smoke are wired to the symbols ZIP and the RedMonitor notice'

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
