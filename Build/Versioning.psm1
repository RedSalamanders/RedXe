Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

<#
.SYNOPSIS
Reads the human-maintained major.minor from Common/Version.h and combines it with a build number.
.DESCRIPTION
Returns Major, Minor, Build, Version ("1.0.183": the package, tag, and winget version) and FileVersion
("1.0.183.0": what the compiled version resources report). The header is the single source of major.minor;
the build number comes from the caller; Resolve-RedXeBuildNumber supplies the default (the commit count of HEAD).
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

<#
.SYNOPSIS
The default build number: the number of commits reachable from HEAD.
.DESCRIPTION
The same formula gives the same number locally and in CI, it only grows on a branch that forbids force pushes
(main), and one commit always maps to one version. A checkout without a complete history (a source archive, or a
shallow clone whose count would be truncated) yields 0 with a warning so nothing pretends to be a release.
#>
function Get-RedXeDefaultBuildNumber {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $RepoRoot)
    $git = Get-Command git -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $git) { Write-Warning 'git is not available; the build number is 0.'; return 0 }
    $count = & $git.Source -C $RepoRoot rev-list --count HEAD 2>$null
    if ($LASTEXITCODE -ne 0 -or $count -notmatch '^\d+$') { Write-Warning "$RepoRoot is not a git checkout; the build number is 0."; return 0 }
    $shallow = (& $git.Source -C $RepoRoot rev-parse --is-shallow-repository 2>$null)
    if ($shallow -eq 'true') { Write-Warning 'Shallow clone: the commit count would be truncated, so the build number is 0. Fetch the full history (git fetch --unshallow).'; return 0 }
    if ([int] $count -gt 65535) { throw "The commit count $count exceeds the 16-bit version field; pass -BuildNumber explicitly." }
    return [int] $count
}

<#
.SYNOPSIS
An explicit positive build number, or the default commit count when none was requested.
#>
function Resolve-RedXeBuildNumber {
    param([Parameter(Mandatory)][string] $RepoRoot, [ValidateRange(0, 65535)][int] $Requested = 0)
    if ($Requested -gt 0) { return $Requested }
    return Get-RedXeDefaultBuildNumber -RepoRoot $RepoRoot
}

Export-ModuleMember -Function Get-RedXeVersion, ConvertTo-RedXePackageVersion, Get-RedXeDefaultBuildNumber, Resolve-RedXeBuildNumber
