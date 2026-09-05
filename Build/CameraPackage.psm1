Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-CameraBinaryMachine {
    param([Parameter(Mandatory)][string] $Path)
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) { throw 'Not a PE file.' }
        $stream.Position = 60
        $offset = $reader.ReadUInt32()
        if ($offset -lt 64 -or $offset -gt $stream.Length - 26) { throw 'Invalid PE header offset.' }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw 'Invalid PE signature.' }
        $machine = $reader.ReadUInt16()
        $stream.Position = $offset + 24
        if ($reader.ReadUInt16() -ne 0x20b) { throw 'Camera package requires PE32+.' }
        switch ($machine) {
            0x8664 { return 'x64' }
            0xaa64 { return 'ARM64' }
            default { throw 'Camera package requires native x64 or ARM64 binaries.' }
        }
    } finally { $reader.Dispose() }
}

function Assert-CameraPlainPath {
    param([Parameter(Mandatory)][string] $Path)
    $absolute = [IO.Path]::GetFullPath($Path)
    $cursor = $absolute
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse paths are not camera package locations: $cursor" }
        }
        $parent = [IO.Path]::GetDirectoryName($cursor)
        if ($parent -eq $cursor) { break }
        $cursor = $parent
    }
    return $absolute
}

function Get-CameraPackageFiles { return @('AVControlCamera.dll', 'AVControlCameraSetup.exe') }

function Test-CameraBinary {
    param([Parameter(Mandatory)][string] $Path, [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string] $Platform)
    $absolute = Assert-CameraPlainPath $Path
    if (-not [IO.File]::Exists($absolute)) { throw "Missing camera binary: $absolute" }
    if ((Get-CameraBinaryMachine $absolute) -ne $Platform) { throw "Camera binary architecture differs from $Platform." }
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($absolute)
    if ($version.IsDebug -or $version.ProductName -ne 'RedXe Camera' -or
        $version.OriginalFilename -cne [IO.Path]::GetFileName($absolute) -or $version.FileVersion -ne '0.1.0.0') {
        throw 'Only identified Release RedXe Camera binaries are installable.'
    }
    return (Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash
}

function Test-CameraPackage {
    param([Parameter(Mandatory)][string] $Path, [Parameter(Mandatory)][ValidateSet('x64', 'ARM64')][string] $Platform)
    $absolute = Assert-CameraPlainPath $Path
    $manifestPath = Join-Path $absolute 'camera-package.json'
    if (-not [IO.File]::Exists($manifestPath) -or (Get-Item -LiteralPath $manifestPath).Length -gt 4096) { throw 'Missing or oversized camera manifest.' }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -AsHashtable
    if ($manifest.Count -ne 5 -or $manifest.schema -ne 1 -or $manifest.product -cne 'RedXe Camera' -or
        $manifest.platform -cne $Platform -or $manifest.sourceClsid -cne '{10F8F1A2-4A82-4D82-9C0E-E253B151BDCB}' -or
        $manifest.files -isnot [Collections.IDictionary] -or $manifest.files.Count -ne 2) { throw 'Invalid camera manifest identity or shape.' }
    foreach ($name in Get-CameraPackageFiles) {
        if (-not $manifest.files.Contains($name) -or $manifest.files[$name] -cnotmatch '^[0-9A-F]{64}$') { throw 'Invalid camera manifest file hash.' }
        if ((Test-CameraBinary (Join-Path $absolute $name) $Platform) -cne $manifest.files[$name]) { throw "Camera package hash mismatch: $name" }
    }
    $expected = @('camera-package.json') + @(Get-CameraPackageFiles)
    foreach ($item in Get-ChildItem -LiteralPath $absolute -Force) {
        if ($item.PSIsContainer -or $item.Name -cnotin $expected) { throw "Unexpected camera package entry: $($item.Name)" }
    }
    return $manifest
}

Export-ModuleMember -Function Get-CameraBinaryMachine, Assert-CameraPlainPath, Get-CameraPackageFiles, Test-CameraBinary, Test-CameraPackage
