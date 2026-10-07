# The exact DxUi pin and what restoring it takes: the lock, the pinned source and the output roots under .build, the MSBuild the
# build runs and the Visual Studio installation that vcpkg must build with. build.ps1, restore-dxui.ps1, vcpkg-install.ps1 and the
# update and provenance helpers share it, so each fact lives once.
Set-StrictMode -Version Latest

# The DxUi API revision this product is adapted to. The pinned source names its own revision in capabilities.json and
# Tools/validate_consumer.ps1 rejects a lock that names another, so moving to a new revision is one reviewed change: this number,
# the lock's apiRevision and the adapters it needs.
$script:SupportedApiRevision = 3
$script:CanonicalRepository = 'https://github.com/RedSalamanders/DxUi'

# What a restore leaves out of the working tree: the measurement archives, the published gallery images and the specifications are
# neither built nor consumed. Everything else stays, including all the restore, the build and DxUi's consumer interface use
# (capabilities.json, Tools, Build, src, include, the vcpkg files and the root scripts). Non-cone patterns, so the root stays whole.
$script:SparseCheckoutPatterns = @('/*', '!/Measurements/', '!/docs/gallery/', '!/Specs/')

function Read-RedXeDxUiLock {
    <# The lock at LockFile, once it names the canonical repository, one exact commit, the API revision this product is adapted to
       and the single DxUi target. #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $LockFile)

    $pin = Get-Content -LiteralPath $LockFile -Raw | ConvertFrom-Json
    if ($pin.repository -cne $script:CanonicalRepository -or $pin.commit -cnotmatch '^[0-9a-f]{40}$' -or
        $pin.apiRevision -ne $script:SupportedApiRevision -or @($pin.targets).Count -ne 1 -or $pin.targets[0] -cne 'DxUi') {
        throw "DxUi lock must identify the canonical repository, one exact commit, API revision $($script:SupportedApiRevision) and the single DxUi target: $LockFile"
    }
    return $pin
}

function Get-RedXeDxUiSupportedApiRevision {
    <# The DxUi API revision this product is adapted to, the only one a lock may name. #>
    [CmdletBinding()]
    param()
    return $script:SupportedApiRevision
}

function Get-RedXeDxUiDependencyRoot {
    <# Where every DxUi restore lives: .build/dependencies/DxUi beneath the product root. #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $RepoRoot)
    return Join-Path $RepoRoot '.build/dependencies/DxUi'
}

function Get-RedXeDxUiSourcePath {
    <# Where the pinned commit's source is restored: .build/dependencies/DxUi/source/<commit> beneath the product root. #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $RepoRoot,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string] $Commit
    )
    return Join-Path (Get-RedXeDxUiDependencyRoot -RepoRoot $RepoRoot) "source/$Commit"
}

function Get-RedXeDxUiOutputRoot {
    <# Where restore-dxui.ps1 isolates the vcpkg and library outputs of one DxUi build identity, with the trailing separator MSBuild
       expects: .build/dependencies/DxUi/<the first 16 hex digits of its fingerprint>. Under the full 64 digits, vcpkg's deepest tool
       files pass MAX_PATH; the full fingerprint stays in DxUi.identity.<Platform>.json and the product provenance. #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $RepoRoot,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string] $Fingerprint
    )
    return (Join-Path (Get-RedXeDxUiDependencyRoot -RepoRoot $RepoRoot) $Fingerprint.Substring(0, 16)) + [IO.Path]::DirectorySeparatorChar
}

function Get-RedXeDxUiCloneSource {
    <# The sibling DxUi checkout beside the product when it holds Commit (a local clone is fast and works offline), else an empty
       string, so the canonical repository is cloned. It only reads the sibling: it is never checked out, reset or edited. #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $RepoRoot,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string] $Commit
    )
    $sibling = Join-Path (Split-Path -Parent $RepoRoot) 'DxUi'
    if (Test-Path -LiteralPath (Join-Path $sibling '.git')) {
        & git -C $sibling cat-file -e "$Commit^{commit}" 2>$null
        if ($LASTEXITCODE -eq 0) { return $sibling }
    }
    return ''
}

function Test-RedXeDxUiSourceCheckout {
    <# Whether Path holds a finished restore of Commit: the top of its own Git working tree, at Commit, with nothing changed or
       added. The top level is checked first (--show-cdup prints an empty line only there): a folder whose .git is incomplete is no
       repository, and Git would answer for the product checkout around it. #>
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $Commit
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) { return $false }
    $location = @(& git -C $Path rev-parse --show-cdup HEAD 2>$null)
    if ($LASTEXITCODE -ne 0 -or $location.Count -ne 2 -or $location[0] -or $location[1] -cne $Commit) { return $false }
    # No optional locks: a concurrent restore or build may be reading the same checkout.
    $changes = @(& git -C $Path --no-optional-locks status --porcelain --untracked-files=normal 2>$null)
    return $LASTEXITCODE -eq 0 -and $changes.Count -eq 0
}

function Restore-RedXeDxUiSource {
    <# Restores Commit of Repository at Destination and returns whether it did. A Destination that already is the clean checkout of
       Commit is left alone; any other one (a restore that an older build left unfinished, a deletion that did not finish, another
       commit, edited files) is removed and restored again.
       - A restore is published whole or not at all. The clone, the sparse checkout, the detached checkout and origin are made in a
         temporary sibling, which is checked as Destination would be and then renamed into place. A failure or an interruption
         removes the temporary folder (with -Force, for Git's read-only pack files), so no run ever finds half a restore at
         Destination. When a concurrent restore publishes first, its checkout of the same commit is used and this one discarded.
       - The temporary name (~ and up to eight hex digits) is never longer than Destination's leaf, so every path the checkout
         writes there fits wherever it fits at Destination.
       - Git long paths are on for the clone and its checkout. `git clone -c` keeps the setting in the clone's own configuration, so
         the later git calls that read it (status, rev-parse) keep it too; no user or global Git setting is touched. Left off, Git
         cannot create a file whose full path passes 259 characters, and the restore then fails as a dirty checkout. (A destination
         that is itself past 259 characters is beyond what the build's other tools handle; git's -C cannot enter it either.)
       - The working tree is sparse (SparseCheckoutPatterns), set before the checkout so the left-out files are never written.
       - A clone of Repository fetches no file contents up front (--filter=blob:none): the checkout then fetches the kept files'
         only. Every commit and tree is still fetched, so HEAD:include, status and Tools/validate_consumer.ps1 work as on a full
         clone. A clone from a sibling checkout (CloneFrom) is local, where Git ignores filters, and copies its objects.
       - origin is set to Repository either way. #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $Repository,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string] $Commit,
        [Parameter(Mandatory)][string] $Destination,
        [string] $CloneFrom = ''
    )

    # A relative path means the caller's PowerShell location, which the .NET rename below does not know.
    $Destination = $PSCmdlet.GetUnresolvedProviderPathFromPSPath($Destination)
    if (Test-RedXeDxUiSourceCheckout -Path $Destination -Commit $Commit) { return $false }
    $parent = Split-Path -Parent $Destination
    [void](New-Item -ItemType Directory -Force -Path $parent -ErrorAction Stop)
    if (Test-Path -LiteralPath $Destination) {
        Write-Warning "The DxUi source at '$Destination' is not a clean checkout of $Commit; it is removed and restored again."
        try { Remove-Item -LiteralPath $Destination -Recurse -Force -ErrorAction Stop }
        catch {
            # Gone anyway when a concurrent restore removed it first.
            if (Test-Path -LiteralPath $Destination) {
                throw "The unfinished DxUi source at '$Destination' could not be removed ($($_.Exception.Message)). Close what holds it open, delete the folder and retry."
            }
        }
    }

    $leaf = Split-Path -Leaf $Destination
    $temporary = Join-Path $parent ('~' + [guid]::NewGuid().ToString('N').Substring(0, [Math]::Max(1, [Math]::Min(8, $leaf.Length - 1))))
    $from = if ($CloneFrom) { $CloneFrom } else { "$Repository.git" }
    $filter = @(if (-not $CloneFrom) { '--filter=blob:none' })
    try {
        # The setting comes before each command, so it is in force for the whole command; the clone option is the one the new
        # clone keeps in its own configuration for the git calls of other scripts (status, rev-parse).
        & git -c core.longpaths=true clone -c core.longpaths=true --no-checkout --no-hardlinks @filter $from $temporary
        if ($LASTEXITCODE -ne 0) { throw 'DxUi source restore failed. Check Git/network access to the public repository and retry; no custom access token is required.' }
        $script:SparseCheckoutPatterns | & git -c core.longpaths=true -C $temporary sparse-checkout set --no-cone --stdin
        if ($LASTEXITCODE -ne 0) { throw 'The DxUi checkout could not be limited to the files the product consumes.' }
        & git -c core.longpaths=true -C $temporary checkout --detach $Commit
        if ($LASTEXITCODE -ne 0) { throw 'The exact DxUi source pin could not be checked out.' }
        & git -c core.longpaths=true -C $temporary remote set-url origin "$Repository.git"
        if ($LASTEXITCODE -ne 0) { throw 'Could not record the canonical DxUi origin.' }
        if (-not (Test-RedXeDxUiSourceCheckout -Path $temporary -Commit $Commit)) { throw "The DxUi restore is not a clean checkout of $Commit." }

        # Antivirus or the indexer can hold a file just written for a moment, which fails the rename; a short bounded retry covers it.
        for ($attempt = 1; ; $attempt++) {
            try {
                [IO.Directory]::Move($temporary, $Destination)
                return $true
            }
            catch {
                if (Test-RedXeDxUiSourceCheckout -Path $Destination -Commit $Commit) {
                    Write-Verbose "A concurrent restore published the DxUi source at '$Destination' first; using it."
                    return $false
                }
                if ($attempt -ge 10 -or (Test-Path -LiteralPath $Destination)) {
                    throw "The DxUi restore could not be moved into place at '$Destination': $($_.Exception.Message)"
                }
                Start-Sleep -Milliseconds 200
            }
        }
    }
    finally {
        # After a successful rename the temporary folder is gone; otherwise this restore is discarded.
        if (Test-Path -LiteralPath $temporary) {
            try { Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction Stop }
            catch { Write-Warning "Could not remove the temporary DxUi restore '$temporary' ($($_.Exception.Message)); a later restore removes it." }
        }
    }
}

function Restore-RedXeDxUiPin {
    <# Reads the product's lock and returns the validated pin, the lock's path and the source path, once that source is the clean
       checkout of the pinned commit: an existing restore is verified (Test-RedXeDxUiSourceCheckout), and a missing, unfinished or
       changed one is restored again. Callers therefore import and run only the pinned commit's own files. The API revision of that
       checkout is Tools/validate_consumer.ps1's check, which restore-dxui.ps1 and the provenance writer run on it. #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $RepoRoot)

    $lockFile = Join-Path $RepoRoot 'Dependencies/DxUi.lock.json'
    $pin = Read-RedXeDxUiLock -LockFile $lockFile
    $source = Get-RedXeDxUiSourcePath -RepoRoot $RepoRoot -Commit $pin.commit
    if (-not (Test-RedXeDxUiSourceCheckout -Path $source -Commit $pin.commit)) {
        $cloneFrom = Get-RedXeDxUiCloneSource -RepoRoot $RepoRoot -Commit $pin.commit
        [void](Restore-RedXeDxUiSource -Repository $pin.repository -Commit $pin.commit -Destination $source -CloneFrom $cloneFrom)
    }
    return [pscustomobject]@{ Pin = $pin; LockFile = $lockFile; Source = $source }
}

function Remove-RedXeDxUiSupersededRestores {
    <# Removes what earlier restores left under .build/dependencies/DxUi and nothing uses any more: output roots that no resolved
       properties file names (a pin bump, a toolset or SDK update, or an older folder naming leaves the previous root behind),
       source clones of other commits, and temporary restore folders. restore-dxui.ps1 runs it after it writes its platform's
       properties, so the root each platform builds with is kept.
       - Only folders named the way RedXe's restores name them are candidates: 16- or 64-digit fingerprint roots, the older
         <commit>-api<n>-... roots, source/<commit> and source/~<hex>. Anything else in the folder is left alone.
       - A candidate written to within MinimumAge is kept, so a restore that another session is still running (the other platform's
         root before its properties exist, a temporary clone) is never removed under it.
       - It is best effort: a folder that cannot be removed now (a file still open) is reported and left to a later restore. #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $RepoRoot,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string] $Commit,
        [TimeSpan] $MinimumAge = [TimeSpan]::FromHours(1)
    )

    $dependencyRoot = Get-RedXeDxUiDependencyRoot -RepoRoot $RepoRoot
    $named = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($propsFile in @(Get-ChildItem -LiteralPath $dependencyRoot -Filter 'DxUi.resolved*.props' -File -ErrorAction SilentlyContinue)) {
        try { [xml] $props = [IO.File]::ReadAllText($propsFile.FullName) }
        catch {
            Write-Warning "Cannot read '$($propsFile.FullName)' ($($_.Exception.Message)); no superseded DxUi restore is removed."
            return
        }
        foreach ($root in @($props.SelectNodes('/Project/PropertyGroup/DxUiConsumerOutputRoot'))) {
            [void]$named.Add([IO.Path]::GetFileName($root.InnerText.TrimEnd('\', '/')))
        }
    }
    $cutoff = [DateTime]::UtcNow - $MinimumAge
    $superseded = @(
        Get-ChildItem -LiteralPath $dependencyRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -cmatch '^([0-9a-f]{16}|[0-9a-f]{64}|[0-9a-f]{40}-api[0-9]+-.+)$' -and -not $named.Contains($_.Name) }
        Get-ChildItem -LiteralPath (Join-Path $dependencyRoot 'source') -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -cmatch '^([0-9a-f]{40}|~[0-9a-f]+)$' -and $_.Name -cne $Commit }
    ) | Where-Object { $_.LastWriteTimeUtc -lt $cutoff }
    foreach ($folder in $superseded) {
        try {
            Remove-Item -LiteralPath $folder.FullName -Recurse -Force -ErrorAction Stop
            Write-Host "Removed the superseded DxUi restore $($folder.FullName)" -ForegroundColor DarkGray
        }
        catch { Write-Warning "Could not remove the superseded DxUi restore '$($folder.FullName)' ($($_.Exception.Message)); a later restore tries again." }
    }
}

function Find-RedXeMSBuild {
    <# The MSBuild the build runs: MSBUILD_EXE_PATH when that file exists, then msbuild.exe on PATH (a Developer PowerShell's), then
       the first x64 MSBuild of an installation vswhere reports, then a scan of the Visual Studio folders. build.ps1 hands its
       choice to vcpkg-install.ps1 and restore-dxui.ps1; run on their own, they find the same one here, so the overlay triplets and
       the DxUi identity files do not change with whichever script ran last. #>
    [CmdletBinding()]
    param()

    if ($env:MSBUILD_EXE_PATH -and (Test-Path -LiteralPath $env:MSBUILD_EXE_PATH -PathType Leaf)) {
        return $env:MSBUILD_EXE_PATH
    }

    $pathCommand = Get-Command 'msbuild.exe' -ErrorAction SilentlyContinue
    if ($pathCommand) {
        return $pathCommand.Source
    }

    $vswhereCandidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }

    foreach ($vswhere in $vswhereCandidates) {
        $found = & $vswhere -all -prerelease -products '*' -requires Microsoft.Component.MSBuild `
            -find 'MSBuild\**\Bin\amd64\MSBuild.exe' 2>$null
        $candidate = $found | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
        if ($candidate) {
            return $candidate
        }
    }

    $roots = @(
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) }

    foreach ($root in $roots) {
        $candidate = Get-ChildItem -LiteralPath $root -Filter 'MSBuild.exe' -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\MSBuild\\Current\\Bin\\(amd64\\)?MSBuild\.exe$' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }

    throw 'MSBuild was not found. Install the Visual Studio Desktop development with C++ workload from .vsconfig.'
}

function Get-RedXeVisualStudioInstallation {
    <# The Visual Studio installation that holds the MSBuild the build runs: the nearest directory above MSBuildPath with
       VC\Auxiliary\Build, as MSBuild compiles with that installation's default MSVC toolset. #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $MSBuildPath)

    # A relative path means the caller's PowerShell location, which .NET file calls do not know.
    $path = $PSCmdlet.GetUnresolvedProviderPathFromPSPath($MSBuildPath)
    $directory = [IO.Path]::GetDirectoryName($path)
    while ($directory) {
        if ([IO.Directory]::Exists([IO.Path]::Combine($directory, 'VC', 'Auxiliary', 'Build'))) { return $directory }
        $directory = [IO.Path]::GetDirectoryName($directory)
    }
    throw "The MSBuild at '$path' is not inside a Visual Studio installation with the MSVC build tools (there is no VC\Auxiliary\Build above it). Install the Desktop development with C++ workload, or put a Visual Studio MSBuild on the build's path."
}

Export-ModuleMember -Function Read-RedXeDxUiLock, Get-RedXeDxUiSupportedApiRevision, Get-RedXeDxUiDependencyRoot, Get-RedXeDxUiSourcePath,
    Get-RedXeDxUiOutputRoot, Get-RedXeDxUiCloneSource, Restore-RedXeDxUiSource, Restore-RedXeDxUiPin, Remove-RedXeDxUiSupersededRestores,
    Find-RedXeMSBuild, Get-RedXeVisualStudioInstallation
