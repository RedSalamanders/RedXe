# The exact DxUi pin and what restoring it takes: the lock and the pinned source under .build. restore-dxui.ps1 and the update
# helper share it, so each fact lives once.
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

function Get-RedXeDxUiSourcePath {
    <# Where the pinned commit's source is restored: .build/dependencies/DxUi/source/<commit> beneath the product root. #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $RepoRoot,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string] $Commit
    )
    return Join-Path $RepoRoot ".build/dependencies/DxUi/source/$Commit"
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

function Restore-RedXeDxUiSource {
    <# Clones Commit of Repository into Destination, unless Destination exists, and returns whether it did.
       - Git long paths are on for the clone and its checkout. `git clone -c` keeps the setting in the clone's own configuration, so
         the later git calls that read it (status, rev-parse) keep it too; no user or global Git setting is touched. Left off, Git
         cannot create a file whose full path passes 259 characters, and the restore then fails as a dirty checkout. (A destination
         that is itself past 259 characters is beyond what the build's other tools handle; git's -C cannot enter it either.)
       - The working tree is sparse (SparseCheckoutPatterns), set before the checkout so the left-out files are never written.
       - CloneFrom, when given, is where the clone reads from (a sibling checkout); origin is set to Repository either way. #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $Repository,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string] $Commit,
        [Parameter(Mandatory)][string] $Destination,
        [string] $CloneFrom = ''
    )

    if (Test-Path -LiteralPath $Destination) { return $false }
    [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination))
    $from = if ($CloneFrom) { $CloneFrom } else { "$Repository.git" }
    # The setting comes before each command, so it is in force for the whole command; the clone option is the one the new
    # clone keeps in its own configuration for the git calls of other scripts (status, rev-parse).
    & git -c core.longpaths=true clone -c core.longpaths=true --no-checkout --no-hardlinks $from $Destination
    if ($LASTEXITCODE -ne 0) { throw 'DxUi source restore failed. Check Git/network access to the public repository and retry; no custom access token is required.' }
    $script:SparseCheckoutPatterns | & git -c core.longpaths=true -C $Destination sparse-checkout set --no-cone --stdin
    if ($LASTEXITCODE -ne 0) { throw 'The DxUi checkout could not be limited to the files the product consumes.' }
    & git -c core.longpaths=true -C $Destination checkout --detach $Commit
    if ($LASTEXITCODE -ne 0) { throw 'The exact DxUi source pin could not be checked out.' }
    & git -c core.longpaths=true -C $Destination remote set-url origin "$Repository.git"
    if ($LASTEXITCODE -ne 0) { throw 'Could not record the canonical DxUi origin.' }
    return $true
}

function Restore-RedXeDxUiPin {
    <# Reads the product's lock and restores the pinned commit under .build/dependencies/DxUi/source/<commit> when that is missing.
       Returns the validated pin, the lock's path and the source path. Validating that the checkout is the clean pin is
       Tools/validate_consumer.ps1's job, which the callers run on it. #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $RepoRoot)

    $lockFile = Join-Path $RepoRoot 'Dependencies/DxUi.lock.json'
    $pin = Read-RedXeDxUiLock -LockFile $lockFile
    $source = Get-RedXeDxUiSourcePath -RepoRoot $RepoRoot -Commit $pin.commit
    if (-not (Test-Path -LiteralPath $source)) {
        $cloneFrom = Get-RedXeDxUiCloneSource -RepoRoot $RepoRoot -Commit $pin.commit
        [void](Restore-RedXeDxUiSource -Repository $pin.repository -Commit $pin.commit -Destination $source -CloneFrom $cloneFrom)
    }
    return [pscustomobject]@{ Pin = $pin; LockFile = $lockFile; Source = $source }
}

Export-ModuleMember -Function Read-RedXeDxUiLock, Get-RedXeDxUiSourcePath, Get-RedXeDxUiCloneSource, Restore-RedXeDxUiSource, Restore-RedXeDxUiPin
