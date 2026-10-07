[CmdletBinding()]
param()

# Restoring the DxUi pin: the lock the product accepts, the sparse long-path checkout restored from a deep root, the repair of an
# unfinished or changed restore, a restore that loses to a concurrent one, the filtered clone, the removal of superseded restores, the
# Visual Studio installation vcpkg builds with, and the wiring that keeps build.ps1's order. Everything runs on fixtures or on the
# files the build already restored; nothing needs the network or a window.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repoRoot 'Build/DxUiRestore.psm1') -Force -ErrorAction Stop

$testParent = [IO.Path]::GetFullPath((Join-Path $repoRoot '.build/BuildProcessTests'))
$expectedPrefix = $testParent.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$testRoot = [IO.Path]::GetFullPath((Join-Path $testParent ('DxUiRestore-' + [guid]::NewGuid().ToString('N'))))
if (-not $testRoot.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to create a DxUi restore fixture outside '$testParent': $testRoot"
}

function Assert-That([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
    Write-Host "PASS $Message"
}

function Assert-Throws([scriptblock] $Action, [string] $Pattern, [string] $Message) {
    $text = $null
    try { & $Action } catch { $text = $_.Exception.Message }
    if ($null -eq $text -or $text -notmatch $Pattern) { throw "FAIL: $Message (got: $text)" }
    Write-Host "PASS $Message"
}

function Invoke-Git {
    # Runs git with Git long paths on (the fixtures are deep) and fails on a nonzero exit.
    $output = & git -c core.longpaths=true @args 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') failed with exit code ${LASTEXITCODE}: $($output -join ' | ')" }
    return $output
}

function Write-FixtureFile([string] $Root, [string] $RelativePath) {
    $path = [IO.Path]::GetFullPath([IO.Path]::Combine($Root, $RelativePath))
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    [IO.File]::WriteAllText($path, "fixture $RelativePath`n")
}

# A DxUi tree in miniature: what the restore and build use, plus the three trees a restore leaves out, and one kept file whose
# path is long enough to pass 259 characters under the deep root below.
$longKept = 'src/Controls/' + ('a-deliberately-long-kept-directory-name/' * 2) + 'Deep.cpp'
$kept = @('capabilities.json', 'vcpkg.json', 'vcpkg-install.ps1', 'validate-build-matrix.ps1', 'Tools/validate_consumer.ps1',
    'Tools/VisualStudio.psm1', 'Build/DxUi.Consumer.props', 'src/DxUi.vcxproj', 'include/DxUi/DxUi.h', 'docs/controls.md',
    'README.md', $longKept)
$leftOut = @('Measurements/GridTextOverflow/2026-09-21/receipt.txt', 'docs/gallery/index.html', 'Specs/Core/Core_Documentation.md')

function New-DxUiFixtureRepository([string] $Path) {
    foreach ($file in @($kept + $leftOut)) { Write-FixtureFile $Path $file }
    Invoke-Git init -q $Path | Out-Null
    Invoke-Git -C $Path add -A | Out-Null
    Invoke-Git -C $Path -c user.name=fixture -c user.email=fixture@example.invalid commit -q -m 'DxUi fixture' | Out-Null
    return ((Invoke-Git -C $Path rev-parse HEAD) -join '').Trim()
}

function New-DxUiLockFile([string] $Path, [hashtable] $Override = @{}) {
    $lock = [ordered]@{ repository = 'https://github.com/RedSalamanders/DxUi'; commit = ('a' * 40); apiRevision = 3; targets = @('DxUi') }
    foreach ($key in $Override.Keys) { $lock[$key] = $Override[$key] }
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
    [IO.File]::WriteAllText($Path, ($lock | ConvertTo-Json))
}

function Assert-CleanRestore([string] $Path, [string] $Commit, [string] $Message) {
    # The restore is the top of its own working tree, at the commit, unchanged, sparse, and no temporary folder is left beside it.
    $location = @(& git -C $Path rev-parse --show-cdup HEAD)
    $changes = @(& git -C $Path status --porcelain --untracked-files=normal)
    Assert-That ($location.Count -eq 2 -and -not $location[0] -and $location[1] -ceq $Commit -and $changes.Count -eq 0 -and
        [IO.File]::Exists((Join-Path $Path 'Tools/VisualStudio.psm1')) -and -not [IO.Directory]::Exists((Join-Path $Path 'Specs'))) $Message
    Assert-That (@(Get-ChildItem -LiteralPath (Split-Path -Parent $Path) -Force | Where-Object Name -like '~*').Count -eq 0) "$Message; no temporary folder is left"
}

# Fixture Git ignores the developer's global and system configuration: commit signing (a key prompt would hang the run), hooks
# (core.hooksPath, or an init template's) and line-ending conversion would otherwise change or block the fixture commits and the
# restores under test. The finally below puts the previous values back.
$savedGitConfigGlobal = $env:GIT_CONFIG_GLOBAL
$savedGitConfigNoSystem = $env:GIT_CONFIG_NOSYSTEM

try {
    [void][IO.Directory]::CreateDirectory($testRoot)
    $emptyGitConfig = Join-Path $testRoot 'empty.gitconfig'
    [IO.File]::WriteAllText($emptyGitConfig, '')
    $env:GIT_CONFIG_GLOBAL = $emptyGitConfig
    $env:GIT_CONFIG_NOSYSTEM = '1'

    # --- The lock ---
    $lockPath = Join-Path $testRoot 'locks/DxUi.lock.json'
    New-DxUiLockFile $lockPath
    Assert-That ((Read-RedXeDxUiLock -LockFile $lockPath).commit -ceq ('a' * 40)) 'a canonical API revision 3 lock is accepted'
    foreach ($case in @(
            @{ Name = 'the previous API revision 2'; Override = @{ apiRevision = 2 } },
            @{ Name = 'a later API revision 4'; Override = @{ apiRevision = 4 } },
            @{ Name = 'another repository'; Override = @{ repository = 'https://example.invalid/DxUi' } },
            @{ Name = 'an abbreviated commit'; Override = @{ commit = 'a0b4934' } },
            @{ Name = 'an uppercase commit'; Override = @{ commit = ('A' * 40) } },
            @{ Name = 'another target'; Override = @{ targets = @('Foundation') } },
            @{ Name = 'two targets'; Override = @{ targets = @('DxUi', 'Foundation') } })) {
        New-DxUiLockFile $lockPath $case.Override
        Assert-Throws { Read-RedXeDxUiLock -LockFile $lockPath } 'API revision 3' "the lock refuses $($case.Name)"
    }

    # --- The Visual Studio installation of the build's MSBuild ---
    $installation = Join-Path $testRoot 'VisualStudio/18/Insiders'
    foreach ($directory in @('MSBuild/Current/Bin/amd64', 'VC/Auxiliary/Build', 'Common7/IDE')) {
        [void][IO.Directory]::CreateDirectory((Join-Path $installation $directory))
    }
    foreach ($relative in @('MSBuild/Current/Bin/MSBuild.exe', 'MSBuild/Current/Bin/amd64/MSBuild.exe')) {
        [IO.File]::WriteAllText((Join-Path $installation $relative), 'fixture')
    }
    foreach ($relative in @('MSBuild/Current/Bin/amd64/MSBuild.exe', 'MSBuild/Current/Bin/MSBuild.exe')) {
        $found = Get-RedXeVisualStudioInstallation -MSBuildPath (Join-Path $installation $relative)
        Assert-That ($found -ieq $installation) "the installation of $relative is the directory with VC\Auxiliary\Build"
    }
    Push-Location (Join-Path $installation 'MSBuild/Current')
    try {
        $relativeFound = Get-RedXeVisualStudioInstallation -MSBuildPath 'Bin/amd64/MSBuild.exe'
        Assert-That ($relativeFound -ieq $installation) 'a relative MSBuild path resolves against the PowerShell location'
    }
    finally { Pop-Location }
    $strayMSBuild = Join-Path $testRoot 'Elsewhere/MSBuild.exe'
    [void][IO.Directory]::CreateDirectory((Join-Path $testRoot 'Elsewhere'))
    [IO.File]::WriteAllText($strayMSBuild, 'fixture')
    Assert-Throws { Get-RedXeVisualStudioInstallation -MSBuildPath $strayMSBuild } 'not inside a Visual Studio installation' `
        'an MSBuild outside any Visual Studio installation is refused'

    # --- The restore from a deep root ---
    $fixture = Join-Path $testRoot 'DxUi-fixture'
    $commit = New-DxUiFixtureRepository $fixture
    # The restored directory fits within 259 characters, as it must for the build's other tools; the files below it do not.
    $deepRoot = Join-Path $testRoot 'deep-roots'
    while ($deepRoot.Length -lt 140) { $deepRoot = Join-Path $deepRoot 'restore-from-a-deep-root' }
    $destination = Join-Path $deepRoot "source/$commit"
    $longKeptPath = [IO.Path]::GetFullPath([IO.Path]::Combine($destination, $longKept))
    Assert-That ($destination.Length -lt 250 -and $longKeptPath.Length -gt 259) `
        "the deep root puts a kept file past the 259-character path limit ($($longKeptPath.Length)) under a restored directory that fits ($($destination.Length))"

    # Without Git long paths the same checkout cannot create that file; with them off the control fails as it does on a CI runner.
    # The control sits exactly as deep as the restore below, so its long file has the same length.
    $control = Join-Path $deepRoot ('source/' + ('c' * 40))
    $controlOutput = & git -c core.longpaths=false clone -q --no-checkout --no-hardlinks $fixture $control 2>&1
    $controlCloned = $LASTEXITCODE -eq 0
    $controlOutput = & git -c core.longpaths=false -C $control checkout -q --detach $commit 2>&1
    $controlCheckedOut = $controlCloned -and $LASTEXITCODE -eq 0
    if ($controlCheckedOut) {
        Write-Host 'NOTE this Git creates paths past 259 characters without core.longpaths, so the control cannot show the setting matters.'
    }
    else {
        Assert-That (-not [IO.File]::Exists([IO.Path]::GetFullPath([IO.Path]::Combine($control, $longKept)))) `
            'without Git long paths the deep checkout fails to create the long file (the control)'
    }

    $restored = Restore-RedXeDxUiSource -Repository 'https://github.com/RedSalamanders/DxUi' -Commit $commit -Destination $destination -CloneFrom $fixture
    Assert-That $restored 'the restore clones the commit into a deep root'
    Assert-That ([IO.File]::Exists($longKeptPath)) 'the long kept file exists under the deep root'
    foreach ($file in $kept) {
        Assert-That ([IO.File]::Exists([IO.Path]::GetFullPath([IO.Path]::Combine($destination, $file)))) "the restore keeps $file"
    }
    foreach ($file in $leftOut) {
        Assert-That (-not [IO.File]::Exists([IO.Path]::GetFullPath([IO.Path]::Combine($destination, $file)))) "the restore leaves out $file"
    }
    foreach ($directory in @('Measurements', 'docs/gallery', 'Specs')) {
        Assert-That (-not [IO.Directory]::Exists([IO.Path]::GetFullPath([IO.Path]::Combine($destination, $directory)))) "the restore writes no $directory directory"
    }
    $status = @(& git -C $destination status --porcelain --untracked-files=normal)
    Assert-That ($LASTEXITCODE -eq 0 -and $status.Count -eq 0) 'the sparse checkout reads as a clean tree, as Tools/validate_consumer.ps1 requires'
    Assert-That (((& git -C $destination rev-parse HEAD) -join '').Trim() -ceq $commit) 'the checkout is the exact commit'
    Assert-That (((& git -C $destination remote get-url origin) -join '').Trim() -ceq 'https://github.com/RedSalamanders/DxUi.git') `
        'origin names the canonical repository, not the copy it was cloned from'
    Assert-That (((& git -C $destination config --local --get core.longpaths) -join '').Trim() -ceq 'true') `
        'Git long paths stay on in the clone, for the git calls that follow'
    Assert-That (-not (Restore-RedXeDxUiSource -Repository 'https://github.com/RedSalamanders/DxUi' -Commit $commit -Destination $destination -CloneFrom $fixture)) `
        'an existing restore is left alone'

    # --- An existing destination counts as restored only when it is the clean checkout of the commit ---
    $repair = Join-Path $testRoot "repairs/source/$commit"
    $brokenStates = [ordered]@{
        # A restore by an older build, interrupted after its clone: no working tree.
        'a clone interrupted before its checkout' = { Invoke-Git clone -q --no-checkout --no-hardlinks $fixture $repair | Out-Null }
        # A deletion that did not finish: Git's pack files are read-only. The folder is no repository, so Git answers for the
        # product checkout around the test root.
        'the read-only pack files of an unfinished deletion' = {
            $pack = Join-Path $repair '.git/objects/pack/pack-remnant.pack'
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($pack))
            [IO.File]::WriteAllText($pack, 'remnant')
            [IO.File]::SetAttributes($pack, [IO.FileAttributes]::ReadOnly)
        }
        'a checkout of another commit' = {
            Invoke-Git clone -q --no-hardlinks $fixture $repair | Out-Null
            [IO.File]::AppendAllText((Join-Path $repair 'README.md'), "another commit`n")
            Invoke-Git -C $repair -c user.name=fixture -c user.email=fixture@example.invalid commit -q -a -m 'another commit' | Out-Null
        }
        'a checkout with an edited file' = {
            [void](Restore-RedXeDxUiSource -Repository 'https://github.com/RedSalamanders/DxUi' -Commit $commit -Destination $repair -CloneFrom $fixture)
            [IO.File]::AppendAllText((Join-Path $repair 'Tools/VisualStudio.psm1'), "edited`n")
        }
    }
    foreach ($state in $brokenStates.Keys) {
        if (Test-Path -LiteralPath $repair) { Remove-Item -LiteralPath $repair -Recurse -Force }
        & $brokenStates[$state]
        $repaired = Restore-RedXeDxUiSource -Repository 'https://github.com/RedSalamanders/DxUi' -Commit $commit -Destination $repair -CloneFrom $fixture 3>$null
        Assert-That ($repaired -is [bool] -and $repaired) "$state is removed and restored again"
        Assert-CleanRestore $repair $commit "the restore that replaced $state is the clean checkout of the commit"
    }

    # --- A failed restore leaves nothing behind ---
    $missingCommit = 'e' * 40
    $failed = Join-Path $testRoot "failures/source/$missingCommit"
    Assert-Throws { Restore-RedXeDxUiSource -Repository 'https://github.com/RedSalamanders/DxUi' -Commit $missingCommit -Destination $failed -CloneFrom $fixture } `
        'could not be checked out' 'a restore of a commit the source lacks fails'
    Assert-That (-not (Test-Path -LiteralPath $failed) -and @(Get-ChildItem -LiteralPath (Split-Path -Parent $failed) -Force).Count -eq 0) `
        'the failed restore leaves neither the destination nor its temporary folder'

    # --- A restore that loses the rename to a concurrent one uses the winner ---
    # The concurrent restore is played deterministically. A stand-in for git inside the module runs each command and, once this
    # restore has recorded origin (its last step before the check and the rename), publishes the same commit at the destination as
    # a concurrent run would, by restoring it.
    $raced = Join-Path $testRoot "races/source/$commit"
    $module = Get-Module DxUiRestore
    & $module {
        param([string] $Winner, [string] $From, [string] $Commit)
        $script:FixtureRaceWinner = $Winner
        $script:FixtureRaceFrom = $From
        $script:FixtureRaceCommit = $Commit
        function script:git {
            if ($MyInvocation.ExpectingInput) { $input | & git.exe @args } else { & git.exe @args }
            $exitCode = $LASTEXITCODE
            if ($script:FixtureRaceWinner -and $args -contains 'set-url') {
                $winner = $script:FixtureRaceWinner
                $script:FixtureRaceWinner = ''
                [void](Restore-RedXeDxUiSource -Repository 'https://github.com/RedSalamanders/DxUi' -Commit $script:FixtureRaceCommit `
                        -Destination $winner -CloneFrom $script:FixtureRaceFrom)
            }
            $global:LASTEXITCODE = $exitCode
        }
    } $raced $fixture $commit
    try {
        $won = Restore-RedXeDxUiSource -Repository 'https://github.com/RedSalamanders/DxUi' -Commit $commit -Destination $raced -CloneFrom $fixture
        $raceStaged = & $module { -not $script:FixtureRaceWinner }
    }
    finally {
        & $module {
            Remove-Item -LiteralPath Function:\git
            Remove-Variable -Name FixtureRaceWinner, FixtureRaceFrom, FixtureRaceCommit -Scope Script
        }
    }
    Assert-That ($raceStaged -and $won -is [bool] -and -not $won) 'a restore whose rename loses to a concurrent restore reports that it restored nothing'
    Assert-CleanRestore $raced $commit 'the concurrent restore''s checkout is used'

    # --- A clone of the repository itself fetches only the kept files' contents ---
    # A bare copy of the fixture stands in for the canonical repository. file:// is Git's network path (a local path is cloned
    # locally, where filters are ignored), and the copy accepts filtered fetches as GitHub does.
    $remote = Join-Path $testRoot 'remote/DxUi.git'
    Invoke-Git clone -q --bare $fixture $remote | Out-Null
    Invoke-Git -C $remote config uploadpack.allowFilter true | Out-Null
    $repository = 'file:///' + (Join-Path $testRoot 'remote/DxUi').Replace('\', '/')
    $filtered = Join-Path $testRoot "filtered/source/$commit"
    $cloned = Restore-RedXeDxUiSource -Repository $repository -Commit $commit -Destination $filtered
    Assert-That ($cloned -is [bool] -and $cloned) 'a restore without a sibling checkout clones the repository'
    Assert-CleanRestore $filtered $commit 'the filtered clone is the clean checkout of the commit'
    $missing = @(& git -C $filtered rev-list --objects --missing=print HEAD | Where-Object { $_.StartsWith('?') } | ForEach-Object { $_.Substring(1) })
    $blobs = { param([string[]] $Paths) @($Paths | ForEach-Object { ((Invoke-Git -C $fixture rev-parse "HEAD:$_") -join '').Trim() }) }
    Assert-That (@(& $blobs $leftOut | Where-Object { $_ -notin $missing }).Count -eq 0 -and @(& $blobs $kept | Where-Object { $_ -in $missing }).Count -eq 0) `
        'the clone fetched the contents of the kept files only, never those the sparse checkout leaves out'
    Assert-That (((& git -C $filtered remote get-url origin) -join '').Trim() -ceq "$repository.git") 'the filtered clone records the repository as origin'

    # --- Superseded restores are removed; what a platform builds with, a restore in progress and other folders stay ---
    $pruneRoot = Join-Path $testRoot 'prune/RedXe'
    $dependencyRoot = Get-RedXeDxUiDependencyRoot -RepoRoot $pruneRoot
    $pinned = '8' * 40
    $old = [DateTime]::UtcNow.AddDays(-2)
    $folders = [ordered]@{
        ('1' * 16) = @{ Age = $old; Kept = 'the x64 properties name it' }
        ('2' * 16) = @{ Age = $old; Kept = 'the ARM64 properties name it' }
        ('3' * 16) = @{ Age = $old; Kept = '' }
        ('4' * 64) = @{ Age = $old; Kept = '' }
        (('5' * 40) + '-api2-v145-sdk26100-md') = @{ Age = $old; Kept = '' }
        ('7' * 16) = @{ Age = [DateTime]::UtcNow; Kept = 'a restore may still be writing it' }
        'notes' = @{ Age = $old; Kept = 'a restore never names a folder so' }
        "source/$pinned" = @{ Age = $old; Kept = 'it is the pinned commit' }
        "source/$('6' * 40)" = @{ Age = $old; Kept = '' }
        'source/~0123abcd' = @{ Age = $old; Kept = '' }
        'source/~89abcdef' = @{ Age = [DateTime]::UtcNow; Kept = 'a restore may still be writing it' }
    }
    foreach ($name in $folders.Keys) {
        $file = Join-Path $dependencyRoot "$name/.git/objects/pack/pack-old.pack"
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file))
        [IO.File]::WriteAllText($file, 'old')
        [IO.File]::SetAttributes($file, [IO.FileAttributes]::ReadOnly)
        [IO.Directory]::SetLastWriteTimeUtc((Join-Path $dependencyRoot $name), $folders[$name].Age)
    }
    foreach ($platform in @(@{ Name = 'x64'; Root = '1' * 16 }, @{ Name = 'ARM64'; Root = '2' * 16 })) {
        [IO.File]::WriteAllText((Join-Path $dependencyRoot "DxUi.resolved.$($platform.Name).props"),
            "<Project><PropertyGroup><DxUiConsumerOutputRoot>$(Join-Path $dependencyRoot $platform.Root)\</DxUiConsumerOutputRoot></PropertyGroup></Project>")
    }
    Remove-RedXeDxUiSupersededRestores -RepoRoot $pruneRoot -Commit $pinned 6>$null
    foreach ($name in $folders.Keys) {
        $exists = [IO.Directory]::Exists((Join-Path $dependencyRoot $name))
        if ($folders[$name].Kept) { Assert-That $exists "$name stays: $($folders[$name].Kept)" }
        else { Assert-That (-not $exists) "the superseded $name is removed, read-only files included" }
    }
    Assert-That (@(Get-ChildItem -LiteralPath $dependencyRoot -File).Count -eq 2) 'the resolved properties stay'

    # --- The pin restore from a sibling checkout ---
    $productRoot = Join-Path $testRoot 'product-root/RedXe'
    $siblingRoot = Join-Path $testRoot 'product-root'
    $siblingCommit = New-DxUiFixtureRepository (Join-Path $siblingRoot 'DxUi')
    New-DxUiLockFile (Join-Path $productRoot 'Dependencies/DxUi.lock.json') @{ commit = $siblingCommit }
    Assert-That ((Get-RedXeDxUiCloneSource -RepoRoot $productRoot -Commit $siblingCommit) -ieq (Join-Path $siblingRoot 'DxUi')) `
        'a sibling checkout that has the commit is where the restore reads from'
    Assert-That ((Get-RedXeDxUiCloneSource -RepoRoot $productRoot -Commit ('b' * 40)) -eq '') 'a sibling without the commit is not used'
    $pinRestore = Restore-RedXeDxUiPin -RepoRoot $productRoot
    Assert-That ($pinRestore.Source -ieq (Get-RedXeDxUiSourcePath -RepoRoot $productRoot -Commit $siblingCommit)) 'the pin restores under .build/dependencies/DxUi/source/<commit>'
    Assert-That ([IO.File]::Exists((Join-Path $pinRestore.Source 'capabilities.json')) -and -not [IO.Directory]::Exists((Join-Path $pinRestore.Source 'Specs'))) `
        'the pin restore is the sparse checkout'
    $siblingStatus = @(& git -C (Join-Path $siblingRoot 'DxUi') status --porcelain --untracked-files=normal)
    Assert-That ($LASTEXITCODE -eq 0 -and $siblingStatus.Count -eq 0 -and
        (((& git -C (Join-Path $siblingRoot 'DxUi') rev-parse HEAD) -join '').Trim() -ceq $siblingCommit)) 'the sibling checkout is read, never changed'
    $again = Restore-RedXeDxUiPin -RepoRoot $productRoot
    Assert-That ($again.Source -ieq $pinRestore.Source) 'a second pin restore finds the first'
    # vcpkg-install.ps1 imports Tools/VisualStudio.psm1 from what the pin restore returns.
    Remove-Item -LiteralPath (Join-Path $pinRestore.Source 'Tools/VisualStudio.psm1')
    $repairedPin = Restore-RedXeDxUiPin -RepoRoot $productRoot 3>$null
    Assert-CleanRestore $repairedPin.Source $siblingCommit 'a pin restore that lost a file is restored again before a caller imports from it'

    # --- What the product consumes from the pinned DxUi: the discovery and the overlay writer ---
    $realPin = Read-RedXeDxUiLock -LockFile (Join-Path $repoRoot 'Dependencies/DxUi.lock.json')
    $realSource = Get-RedXeDxUiSourcePath -RepoRoot $repoRoot -Commit $realPin.commit
    foreach ($module in @('Tools/VisualStudio.psm1', 'Tools/VcpkgTriplet.psm1')) {
        Assert-That ([IO.File]::Exists((Join-Path $realSource $module))) "the pinned DxUi restore holds $module, which vcpkg-install.ps1 imports"
    }
    Import-Module (Join-Path $realSource 'Tools/VisualStudio.psm1') -Force
    Import-Module (Join-Path $realSource 'Tools/VcpkgTriplet.psm1') -Force
    [IO.File]::WriteAllText((Join-Path $installation 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'), "14.51.36231`n")
    $toolset = Get-DxUiDefaultToolset -Installation $installation
    Assert-That ($toolset.MajorMinor -ceq '14.51' -and $toolset.Version -ceq '14.51.36231') 'the default toolset is read from the installation, not from the newest one'
    $stock = Join-Path $testRoot 'vcpkg/triplets/arm64-windows.cmake'
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($stock))
    [IO.File]::WriteAllText($stock, "set(VCPKG_TARGET_ARCHITECTURE arm64)`r`nset(VCPKG_CRT_LINKAGE dynamic)`r`n")
    $overlay = Update-DxUiVcpkgOverlayTriplet -StockTripletPath $stock -Toolset $toolset -OutputDirectory (Join-Path $testRoot 'overlays/ARM64')
    $overlayText = [IO.File]::ReadAllText($overlay.Path)
    Assert-That ($overlay.Changed -and $overlay.Triplet -ceq 'arm64-windows' -and $overlayText.StartsWith("set(VCPKG_TARGET_ARCHITECTURE arm64)`r`n")) `
        'the overlay keeps the stock triplet and its line endings'
    Assert-That ($overlayText.Contains('set(VCPKG_PLATFORM_TOOLSET_VERSION "14.51")') -and $overlayText.Contains('set(VCPKG_VISUAL_STUDIO_PATH "')) `
        'the overlay pins the installation and the default toolset'
    $again = Update-DxUiVcpkgOverlayTriplet -StockTripletPath $stock -Toolset $toolset -OutputDirectory (Join-Path $testRoot 'overlays/ARM64')
    Assert-That (-not $again.Changed) 'an unchanged restore leaves the overlay alone'

    # --- The wiring ---
    $vcpkgInstall = Get-Content -LiteralPath (Join-Path $repoRoot 'vcpkg-install.ps1') -Raw
    foreach ($needle in @('Restore-RedXeDxUiPin', 'Find-RedXeMSBuild', 'Get-RedXeVisualStudioInstallation',
            'Get-DxUiDefaultToolset', 'Update-DxUiVcpkgOverlayTriplet', '--overlay-triplets=')) {
        Assert-That ($vcpkgInstall.Contains($needle)) "vcpkg-install.ps1 uses $needle"
    }
    Assert-That (-not $vcpkgInstall.Contains('Get-DxUiVisualStudioInstallation')) `
        'vcpkg-install.ps1 run on its own builds with the MSBuild build.ps1 would choose, not the newest Visual Studio'
    $overlayCalls = [regex]::Matches($vcpkgInstall, 'Update-DxUiVcpkgOverlayTriplet').Count
    $overlayArguments = [regex]::Matches($vcpkgInstall, '--overlay-triplets=').Count
    Assert-That ($overlayCalls -eq 1 -and $overlayArguments -eq 1 -and $vcpkgInstall.IndexOf('foreach ($targetPlatform') -lt $vcpkgInstall.IndexOf('Update-DxUiVcpkgOverlayTriplet')) `
        'every triplet the script installs is pinned: the overlay is written and passed inside the platform loop'
    $restoreScript = Get-Content -LiteralPath (Join-Path $repoRoot 'restore-dxui.ps1') -Raw
    Assert-That ($restoreScript.Contains('Restore-RedXeDxUiPin') -and $restoreScript -notmatch 'git clone') 'restore-dxui.ps1 restores through the shared module, never its own clone'
    Assert-That ($restoreScript.Contains('Find-RedXeMSBuild') -and $restoreScript -notmatch 'vswhere') 'restore-dxui.ps1 run on its own resolves MSBuild as build.ps1 does'
    $propsWrite = $restoreScript.IndexOf('WriteAllText($propsPath')
    Assert-That ($propsWrite -gt 0 -and $restoreScript.IndexOf('Remove-RedXeDxUiSupersededRestores') -gt $propsWrite) `
        'restore-dxui.ps1 removes superseded restores only after its platform''s properties name the root it builds with'
    $buildScript = Get-Content -LiteralPath (Join-Path $repoRoot 'build.ps1') -Raw
    $installerCall = $buildScript.IndexOf('& $dependencyInstaller')
    $restoreCall = $buildScript.IndexOf("restore-dxui.ps1')")
    Assert-That ($installerCall -gt 0 -and $restoreCall -gt $installerCall) 'build.ps1 still installs the vcpkg dependencies before it restores DxUi'
    Assert-That ($buildScript -match '& \$dependencyInstaller -Platform \$Platform -MSBuildPath \$msbuild') `
        'build.ps1 gives vcpkg-install.ps1 the MSBuild it runs, so vcpkg builds with that installation'
    Assert-That ($buildScript.Contains('$msbuild = Find-RedXeMSBuild') -and $buildScript -notmatch 'function Find-MSBuild') `
        'build.ps1 resolves MSBuild through the shared module'
}
finally {
    $env:GIT_CONFIG_GLOBAL = $savedGitConfigGlobal
    $env:GIT_CONFIG_NOSYSTEM = $savedGitConfigNoSystem
    $path = [IO.Path]::GetFullPath($testRoot)
    if (-not $path.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe DxUi restore fixture cleanup path.' }
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
}

Write-Host 'DxUi restore tests passed.' -ForegroundColor Green
