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

function Invoke-RedXeVcpkgGit {
    param([string]$Directory,[string[]]$Arguments,[switch]$AllowFailure)
    $output = @(& git.exe -c core.longpaths=true -C $Directory @Arguments 2>&1 | ForEach-Object { [string]$_ })
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0 -and -not $AllowFailure) {
        $diagnostic = $output -join [Environment]::NewLine
        if ($diagnostic.Length -gt 4000) { $diagnostic = $diagnostic.Substring(0,4000) + [Environment]::NewLine + '[remaining Git diagnostics omitted]' }
        throw "Managed vcpkg Git operation failed ($exitCode): $diagnostic"
    }
    [pscustomobject]@{ExitCode=$exitCode;Text=($output -join [Environment]::NewLine).Trim()}
}

function Initialize-RedXeVcpkgCheckout {
    param([string]$RepoRoot,[string]$Path,[string]$Repository,[string]$Commit)
    $created = $false
    $prefix = [IO.Path]::GetFullPath((Join-Path $RepoRoot '.build')).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $Path = [IO.Path]::GetFullPath($Path)
    if (-not $Path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Managed vcpkg checkout must be beneath the product .build directory.' }
    for ($cursor=$Path; $cursor.Length -ge $prefix.TrimEnd('\','/').Length; $cursor=Split-Path -Parent $cursor) {
        if ((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Managed vcpkg path is a reparse point: $cursor" }
        if ($cursor -ceq $prefix.TrimEnd('\','/')) { break }
    }
    if (-not (Test-Path -LiteralPath $Path)) {
        [void][IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
        # Clone from the declared repository so Git retains the promisor configuration
        # needed for filtered objects. Copying a partial clone locally loses that contract.
        $null = Invoke-RedXeVcpkgGit $RepoRoot @('clone','--filter=blob:none','--no-hardlinks','--no-checkout','--',$Repository,$Path)
        $created = $true
    }
    if (-not (Test-Path -LiteralPath (Join-Path $Path '.git') -PathType Container)) { throw "Managed vcpkg path is not an owned clone: $Path" }
    $top = [IO.Path]::GetFullPath((Invoke-RedXeVcpkgGit $Path @('rev-parse','--show-toplevel')).Text)
    if (-not $top.Equals($Path,[StringComparison]::OrdinalIgnoreCase)) { throw "Managed vcpkg path names another checkout: $Path" }
    $origin = (Invoke-RedXeVcpkgGit $Path @('remote','get-url','origin')).Text
    if ($origin.TrimEnd('/') -cne $Repository.TrimEnd('/')) { throw "Managed vcpkg origin differs from the declared repository: $Path" }
    if (-not $created -and (Invoke-RedXeVcpkgGit $Path @('status','--porcelain')).Text) { throw "Managed vcpkg checkout contains local changes: $Path" }
    $null = Invoke-RedXeVcpkgGit $Path @('config','core.longpaths','true')
    $available = Invoke-RedXeVcpkgGit $Path @('cat-file','-e',($Commit+'^{commit}')) -AllowFailure
    if ($available.ExitCode -ne 0) { $null = Invoke-RedXeVcpkgGit $Path @('fetch','--depth','1','origin',$Commit) }
    $head = (Invoke-RedXeVcpkgGit $Path @('rev-parse','HEAD')).Text
    if ($created) {
        # Only this invocation's new --no-checkout clone has no caller work to preserve.
        # Materialize the complete tree, including entries unchanged from clone HEAD.
        $null = Invoke-RedXeVcpkgGit $Path @('checkout','--force','--detach',$Commit)
    } elseif ($head -cne $Commit) {
        $null = Invoke-RedXeVcpkgGit $Path @('checkout','--detach',$Commit)
    }
    if ((Invoke-RedXeVcpkgGit $Path @('rev-parse','HEAD')).Text -cne $Commit -or
        (Invoke-RedXeVcpkgGit $Path @('status','--porcelain')).Text) { throw "Pinned vcpkg source verification failed: $Path" }
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
    Initialize-RedXeVcpkgCheckout -RepoRoot $RepoRoot -Path $toolRoot -Repository $identity.Repository -Commit $identity.ToolCommit
    Initialize-RedXeVcpkgCheckout -RepoRoot $RepoRoot -Path $registryRoot -Repository $identity.Repository -Commit $identity.Baseline
    [pscustomobject]@{ToolRoot=$toolRoot;RegistryRoot=$registryRoot;Executable=(Join-Path $toolRoot 'vcpkg.exe');ToolCommit=$identity.ToolCommit;Baseline=$identity.Baseline}
}

Export-ModuleMember -Function Get-RedXeVcpkgIdentity, Initialize-RedXeVcpkgSources
