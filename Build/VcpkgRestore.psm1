Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-RedXeVcpkgIdentity {
    <#
    .SYNOPSIS Reads the independent executable and ports-catalogue identities.
    .PARAMETER RepoRoot Product root containing vcpkg-tool.json and vcpkg.json.
    .OUTPUTS Repository, ToolCommit and Baseline. Reads only; no environment changes.
    .NOTES Requires full nonzero Git revisions. The two revisions need not match.
    .EXAMPLE Get-RedXeVcpkgIdentity -RepoRoot .
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$RepoRoot)
    $tool = Get-Content -LiteralPath (Join-Path $RepoRoot 'vcpkg-tool.json') -Raw | ConvertFrom-Json
    $manifest = Get-Content -LiteralPath (Join-Path $RepoRoot 'vcpkg.json') -Raw | ConvertFrom-Json
    $pattern = '^(?!0{40}$)[a-fA-F0-9]{40}$'
    if ([string]::IsNullOrWhiteSpace([string]$tool.repository) -or
        [string]$tool.commit -cnotmatch $pattern -or [string]$manifest.'builtin-baseline' -cnotmatch $pattern) {
        throw 'vcpkg executable and ports baseline require a repository and full nonzero Git commits.'
    }
    [pscustomobject]@{Repository=[string]$tool.repository;ToolCommit=([string]$tool.commit).ToLowerInvariant();Baseline=([string]$manifest.'builtin-baseline').ToLowerInvariant()}
}

function Initialize-RedXeVcpkgSources {
    <#
    .SYNOPSIS Restores distinct pinned vcpkg executable and ports sources.
    .PARAMETER RepoRoot Product root containing the two immutable pin declarations.
    .OUTPUTS ToolRoot, RegistryRoot, Executable, ToolCommit and Baseline.
    .NOTES Creates managed clones only beneath .build, uses Git/network as needed and
    refuses dirty, foreign or reparse paths. Does not bootstrap, compile, change pins,
    set process/global environment, or mutate a caller-owned checkout.
    .EXAMPLE Initialize-RedXeVcpkgSources -RepoRoot .
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$RepoRoot)
    $RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
    $identity = Get-RedXeVcpkgIdentity $RepoRoot
    $toolRoot = Join-Path $RepoRoot '.build/vcpkg-tool'
    $registryRoot = Join-Path $RepoRoot '.build/vcpkg-registry'

    $project = Split-Path -Parent $PSScriptRoot
    Import-Module (Join-Path $project '.ci-engine/Tools/Modules/Ci.EngineBinding.psm1') -ErrorAction Stop
    Import-Module (Resolve-CiPinnedEngineModule -ProjectRoot $project -Name VcpkgCheckout.psm1) -Scope Local -ErrorAction Stop
    Initialize-CiVcpkgCheckout -ProjectRoot $RepoRoot -Path $toolRoot -Repository $identity.Repository -Commit $identity.ToolCommit
    Initialize-CiVcpkgCheckout -ProjectRoot $RepoRoot -Path $registryRoot -Repository $identity.Repository -Commit $identity.Baseline
    [pscustomobject]@{ToolRoot=$toolRoot;RegistryRoot=$registryRoot;Executable=(Join-Path $toolRoot 'vcpkg.exe');ToolCommit=$identity.ToolCommit;Baseline=$identity.Baseline}
}

Export-ModuleMember -Function Get-RedXeVcpkgIdentity, Initialize-RedXeVcpkgSources
