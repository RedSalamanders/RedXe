Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

<#
.SYNOPSIS
Reads the human-maintained major.minor from Common/Version.h and combines it with a build number.
.DESCRIPTION
Returns Major, Minor, Build, Version ("1.0.183": the package, tag, and winget version) and FileVersion
("1.0.183.0": what the compiled version resources report). The header is the single source of major.minor;
the build number comes from the caller (GITHUB_RUN_NUMBER in the release workflow, 0 for a plain local build).
#>
function Get-RedXeVersion {
    param(
        [Parameter(Mandatory)][string] $RepoRoot,
        [ValidateRange(0, 65535)][int] $BuildNumber = 0
    )
    $headerPath = Join-Path $RepoRoot 'Common\Version.h'
    if (-not (Test-Path -LiteralPath $headerPath -PathType Leaf)) { throw "Version header not found: $headerPath" }
    $content = Get-Content -LiteralPath $headerPath -Raw
    $read = {
        param([string] $Name)
        $match = [regex]::Match($content, "(?m)^#define\s+$([regex]::Escape($Name))\s+(\d{1,5})\s*$")
        if (-not $match.Success) { throw "$headerPath does not define $Name as a single integer literal." }
        return [int] $match.Groups[1].Value
    }
    $major = & $read 'REDXE_VERSION_MAJOR'
    $minor = & $read 'REDXE_VERSION_MINOR'
    if ($major -gt 65535 -or $minor -gt 65535) { throw 'Version components must fit a 16-bit resource field.' }
    [pscustomobject]@{
        Major = $major
        Minor = $minor
        Build = $BuildNumber
        Version = "$major.$minor.$BuildNumber"
        FileVersion = "$major.$minor.$BuildNumber.0"
    }
}

<#
.SYNOPSIS
Parses a three-part package version ("1.0.183") and rejects anything else.
#>
function ConvertTo-RedXePackageVersion {
    param([Parameter(Mandatory)][string] $Version)
    $match = [regex]::Match($Version.Trim(), '^(\d{1,5})\.(\d{1,5})\.(\d{1,5})$')
    if (-not $match.Success) { throw "A package version has exactly three numeric components: '$Version'" }
    $parts = 1..3 | ForEach-Object { [int] $match.Groups[$_].Value }
    foreach ($part in $parts) { if ($part -gt 65535) { throw "Version components must be 0..65535: '$Version'" } }
    [pscustomobject]@{
        Major = $parts[0]
        Minor = $parts[1]
        Build = $parts[2]
        Version = "$($parts[0]).$($parts[1]).$($parts[2])"
        FileVersion = "$($parts[0]).$($parts[1]).$($parts[2]).0"
    }
}

Export-ModuleMember -Function Get-RedXeVersion, ConvertTo-RedXePackageVersion
