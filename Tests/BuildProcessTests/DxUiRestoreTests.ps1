[CmdletBinding()]
param()

# Restoring the DxUi pin: the lock the product accepts and the sparse long-path checkout restored from a deep root. Everything runs
# on fixtures; nothing needs the network or a window.
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

try {
    [void][IO.Directory]::CreateDirectory($testRoot)

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

}
finally {
    $path = [IO.Path]::GetFullPath($testRoot)
    if (-not $path.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe DxUi restore fixture cleanup path.' }
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
}

Write-Host 'DxUi restore tests passed.' -ForegroundColor Green
