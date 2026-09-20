Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Winget manifest generation and validation. See Specs/Build/Build_Packaging.md.

$script:WingetPackageIdentifier = 'RedSalamanders.RedXe'
$script:WingetTemplateNames = @(
    'RedSalamanders.RedXe.yaml',
    'RedSalamanders.RedXe.installer.yaml',
    'RedSalamanders.RedXe.locale.en-US.yaml'
)

function Get-RedXeWingetPackageIdentifier { $script:WingetPackageIdentifier }

function Resolve-RedXeWingetZip {
    param([Parameter(Mandatory)][string] $Path, [Parameter(Mandatory)][string] $ExpectedName, [Parameter(Mandatory)][string] $Label)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Label package not found: $Path" }
    $item = Get-Item -LiteralPath $Path
    if ($item.Name -cne $ExpectedName) { throw "$Label package must be named exactly '$ExpectedName': $($item.Name)" }
    if ($item.Length -eq 0) { throw "$Label package is empty: $Path" }
    return $item.FullName
}

<#
.SYNOPSIS
Writes the three-file winget manifest for one release from the repository templates.
.DESCRIPTION
The x64 and ARM64 portable ZIPs are both required: winget selects the native package per machine. Their names must
match the release asset names exactly, because the InstallerUrl is derived from the version. ReleaseDate defaults to
today; the release workflow passes the GitHub release's published date so a rerun writes an identical manifest.
#>
function New-RedXeWingetManifest {
    param(
        [Parameter(Mandatory)][string] $RepoRoot,
        [Parameter(Mandatory)][string] $Version,
        [Parameter(Mandatory)][string] $X64ZipPath,
        [Parameter(Mandatory)][string] $Arm64ZipPath,
        [string] $OutputDirectory,
        [string] $ReleaseDate
    )
    Import-Module (Join-Path $RepoRoot 'Build\Versioning.psm1') -Force
    Import-Module (Join-Path $RepoRoot 'Build\Package.psm1') -Force
    $parsed = ConvertTo-RedXePackageVersion -Version $Version
    if ($parsed.Build -le 0) { throw "A published version needs a positive build number (got $($parsed.Version)); pass -BuildNumber to build.ps1/package.ps1." }
    $Version = $parsed.Version
    if (-not $ReleaseDate) { $ReleaseDate = (Get-Date).ToUniversalTime().ToString('yyyy-MM-dd') }
    $date = [datetime]::MinValue
    if ($ReleaseDate -notmatch '^\d{4}-\d{2}-\d{2}$' -or
        -not [datetime]::TryParseExact($ReleaseDate, 'yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::None, [ref] $date)) {
        throw "ReleaseDate must be yyyy-MM-dd: '$ReleaseDate'"
    }

    $x64 = Resolve-RedXeWingetZip -Path $X64ZipPath -ExpectedName (Get-RedXePackageName -Version $Version -Platform x64) -Label 'x64'
    $arm64 = Resolve-RedXeWingetZip -Path $Arm64ZipPath -ExpectedName (Get-RedXePackageName -Version $Version -Platform ARM64) -Label 'ARM64'
    if (-not $OutputDirectory) { $OutputDirectory = Join-Path $RepoRoot ".build\packages\winget-manifest\$Version" }
    [void](New-Item -ItemType Directory -Path $OutputDirectory -Force)

    $replacements = [ordered]@{
        '{VERSION}' = $Version
        '{RELEASE_DATE}' = $ReleaseDate
        '{X64_SHA256}' = (Get-FileHash -LiteralPath $x64 -Algorithm SHA256).Hash.ToUpperInvariant()
        '{ARM64_SHA256}' = (Get-FileHash -LiteralPath $arm64 -Algorithm SHA256).Hash.ToUpperInvariant()
    }
    $templateDirectory = Join-Path $RepoRoot 'Installer\winget\templates'
    $written = foreach ($name in $script:WingetTemplateNames) {
        $content = Get-Content -LiteralPath (Join-Path $templateDirectory $name) -Raw
        foreach ($key in $replacements.Keys) { $content = $content.Replace($key, [string] $replacements[$key]) }
        $unresolved = [regex]::Matches($content, '\{[A-Z0-9_]+\}') | ForEach-Object { $_.Value } | Sort-Object -Unique
        if ($unresolved) { throw "Template $name has unresolved placeholders: $($unresolved -join ', ')" }
        $path = Join-Path $OutputDirectory $name
        [IO.File]::WriteAllText($path, $content.Replace("`r`n", "`n"), [Text.UTF8Encoding]::new($false))
        $path
    }
    [pscustomobject]@{
        PackageIdentifier = $script:WingetPackageIdentifier
        Version = $Version
        ReleaseDate = $ReleaseDate
        OutputDirectory = $OutputDirectory
        Files = @($written)
        X64Sha256 = $replacements['{X64_SHA256}']
        Arm64Sha256 = $replacements['{ARM64_SHA256}']
    }
}

<#
.SYNOPSIS
Runs `winget validate --manifest` and returns its output; throws on a nonzero exit.
#>
function Test-RedXeWingetManifest {
    param([Parameter(Mandatory)][string] $ManifestDirectory, [string] $WingetCommand = 'winget')
    if (-not (Test-Path -LiteralPath $ManifestDirectory -PathType Container)) { throw "Manifest directory not found: $ManifestDirectory" }
    foreach ($name in $script:WingetTemplateNames) {
        if (-not (Test-Path -LiteralPath (Join-Path $ManifestDirectory $name) -PathType Leaf)) { throw "Manifest file missing: $name" }
    }
    if (-not (Get-Command $WingetCommand -ErrorAction SilentlyContinue)) { throw "winget was not found on this machine ($WingetCommand)." }
    $output = @(& $WingetCommand validate --manifest $ManifestDirectory 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    # Older winget clients exit nonzero for "succeeded with warnings" (for example the 1.12.0 schema-header notice
    # on a 1.11 client); a manifest that validated is still accepted, and the warnings are returned to the caller.
    $succeededWithWarnings = $output | Where-Object { $_ -match '^Manifest validation succeeded with warnings\.?$' } | Select-Object -First 1
    if ($exitCode -ne 0 -and -not $succeededWithWarnings) { throw "winget validate failed with exit code ${exitCode}:`n$($output -join "`n")" }
    return $output
}

Export-ModuleMember -Function Get-RedXeWingetPackageIdentifier, New-RedXeWingetManifest, Test-RedXeWingetManifest
