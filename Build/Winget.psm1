Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Winget manifest generation and validation. See Specs/Build/Build_Packaging.md.

$script:WingetManifestSuffixes = @('.yaml', '.installer.yaml', '.locale.en-US.yaml')

function Get-RedXeWingetPackageIdentifier {
    $manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../.ci/project.json') -Raw | ConvertFrom-Json
    return $manifest.winget.identifier
}

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
Writes the three-file WinGet manifest from the project configuration and the two portable ZIPs.
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

    $x64 = Resolve-RedXeWingetZip -Path $X64ZipPath -ExpectedName (Get-RedXePackageName -Version $Version -Platform x64) -Label 'x64'
    $arm64 = Resolve-RedXeWingetZip -Path $Arm64ZipPath -ExpectedName (Get-RedXePackageName -Version $Version -Platform ARM64) -Label 'ARM64'
    if (-not $OutputDirectory) { $OutputDirectory = Join-Path $RepoRoot ".build\packages\winget-manifest\$Version" }
    Import-Module (Join-Path $RepoRoot '.ci-engine/Tools/Modules/Ci.EngineBinding.psm1') -Scope Local -ErrorAction Stop
    $publicationModule = Resolve-CiPinnedEngineModule -ProjectRoot $RepoRoot -Name 'Publication.psm1'
    Import-Module $publicationModule -Scope Local -ErrorAction Stop
    Import-Module (Join-Path (Split-Path -Parent $publicationModule) 'Ci.Policy.psm1') -Scope Local -ErrorAction Stop
    $project = Read-CiProject $RepoRoot
    if ($project.repository -cne 'RedSalamanders/RedXe') { throw 'The manifest generator requires the RedXe project configuration.' }
    $OutputDirectory = Assert-CiContainedPath -Root $RepoRoot -Path $OutputDirectory
    $identifier = $project.winget.identifier
    $manifestFiles = @($script:WingetManifestSuffixes | ForEach-Object {
        Assert-CiContainedPath -Root $RepoRoot -Path (Join-Path $OutputDirectory "$identifier$_")
    })
    $x64Hash = (Get-FileHash -LiteralPath $x64 -Algorithm SHA256).Hash
    $arm64Hash = (Get-FileHash -LiteralPath $arm64 -Algorithm SHA256).Hash
    $plan = @{
        project = $project
        batch = @{ version = $Version }
        tag = "v$Version"
        assets = @(
            @{ platform = 'x64'; file = [IO.Path]::GetFileName($x64); path = $x64; sha256 = $x64Hash }
            @{ platform = 'ARM64'; file = [IO.Path]::GetFileName($arm64); path = $arm64; sha256 = $arm64Hash }
        )
    }
    $writtenDirectory = New-CiWingetManifest -Plan $plan -Directory $OutputDirectory -ReleaseDate $ReleaseDate
    [pscustomobject]@{
        PackageIdentifier = $identifier
        Version = $Version
        ReleaseDate = $ReleaseDate
        OutputDirectory = $writtenDirectory
        Files = $manifestFiles
        X64Sha256 = $x64Hash
        Arm64Sha256 = $arm64Hash
    }
}

<#
.SYNOPSIS
Runs `winget validate --manifest` and returns its output; throws on a nonzero exit.
#>
function Test-RedXeWingetManifest {
    param([Parameter(Mandatory)][string] $ManifestDirectory, [string] $WingetCommand = 'winget')
    if (-not (Test-Path -LiteralPath $ManifestDirectory -PathType Container)) { throw "Manifest directory not found: $ManifestDirectory" }
    $identifier = Get-RedXeWingetPackageIdentifier
    foreach ($suffix in $script:WingetManifestSuffixes) {
        $name = "$identifier$suffix"
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
