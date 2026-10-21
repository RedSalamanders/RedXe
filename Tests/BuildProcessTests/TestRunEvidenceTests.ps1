<# .SYNOPSIS Exercises lossless, isolated run evidence without a native product build or desktop use. #>
[CmdletBinding()]
param([string] $ModulePath = '', [string] $ScratchRoot = '')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $repository 'AGENTS.md'))) { $repository = Split-Path -Parent $repository }
if (-not $ModulePath) {
    $ModulePath = Join-Path $repository 'Tools/TestRunEvidence.psm1'
    if (-not (Test-Path -LiteralPath $ModulePath)) { $ModulePath = Join-Path $repository 'Build/TestRunEvidence.psm1' }
}
if (-not $ScratchRoot) {
    if (-not (Test-Path -LiteralPath (Join-Path $repository 'AGENTS.md'))) { $repository = Split-Path -Parent $PSScriptRoot }
    $ScratchRoot = Join-Path $repository ('.build/evidence-tooling-tests/' + [guid]::NewGuid().ToString('N'))
}
[void][IO.Directory]::CreateDirectory($ScratchRoot)
Import-Module $ModulePath -Force
$script:count = 0
function Assert-Evidence([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
    $script:count++
}
function Reject-Evidence([scriptblock] $Body, [string] $Message) {
    $rejected = $false
    try { & $Body } catch { $rejected = $true }
    Assert-Evidence $rejected $Message
}
function New-Fixture([string] $Name) {
    $path = Join-Path $ScratchRoot $Name
    [void][IO.Directory]::CreateDirectory($path)
    return $path
}

$root = New-Fixture 'ordinary'
$a = Start-TestRunEvidence $root @{scenario='first'}
$b = Start-TestRunEvidence $root @{scenario='concurrent'}
try {
    Assert-Evidence ($a.Path -cne $b.Path) 'Concurrent runs shared a writable output path.'
    $file = Join-Path $a.Path 'evidence-Ł.txt'
    [IO.File]::WriteAllText($file, 'unique evidence')
    [IO.File]::WriteAllText($file + ':provenance', 'original stream')
    [IO.File]::SetAttributes($file, [IO.FileAttributes]::ReadOnly -bor [IO.FileAttributes]::Hidden)
    $before = Get-FileHash -LiteralPath $file
    $attributes = [IO.File]::GetAttributes($file)
    $security = (Get-Acl -LiteralPath $file).Sddl
    Complete-TestRunEvidence $a $true
    $manifest = Get-Content -LiteralPath (Join-Path $a.Directory 'completed.json') -Raw | ConvertFrom-Json
    Assert-Evidence ($manifest.outcome -ceq 'passed') 'Success outcome missing.'
    Assert-Evidence ($before.Hash -ceq (Get-FileHash -LiteralPath $file).Hash) 'Payload bytes changed.'
    Assert-Evidence ($attributes -eq [IO.File]::GetAttributes($file) -and $security -ceq (Get-Acl -LiteralPath $file).Sddl) 'Attributes or ACL changed.'
    Assert-Evidence ($manifest.payload.Count -eq 1 -and $manifest.payload[0].streams.Count -eq 2) 'Hidden file or named stream missing.'
    $ads = @($manifest.payload[0].streams | Where-Object Name -eq ':provenance:$DATA')
    Assert-Evidence ($ads.Count -eq 1 -and $ads[0].Length -eq 15) 'Named-stream evidence was not hashed.'
    Reject-Evidence { Complete-TestRunEvidence $a $true } 'Completed evidence could be overwritten.'
    [IO.File]::WriteAllText((Join-Path $b.Path 'failed.log'), 'original failure')
} finally {
    if (-not $a.Closed) { Complete-TestRunEvidence $a $false }
    if (-not $b.Closed) { Complete-TestRunEvidence $b $false }
}
Assert-Evidence ((Get-Content (Join-Path $b.Directory 'completed.json') -Raw | ConvertFrom-Json).outcome -ceq 'failed-or-interrupted') 'Failure output lost or promoted to success.'

$run = Start-TestRunEvidence $root @{scenario='publication'}
try {
    Publish-TestRunEvidenceReceipt $run 'Suite.json' @{exitCode=0;log='first immutable log'}
    $old = Join-Path $root '.build/reports/Suite.json'
    [IO.File]::WriteAllText($old + ':original', 'retained')
    $hash = (Get-FileHash -LiteralPath $old).Hash
    $second = Start-TestRunEvidence $root @{scenario='next-publication'}
    try {
        Publish-TestRunEvidenceReceipt $second 'Suite.json' @{exitCode=3;log='second immutable log'}
        $previous = @(Get-ChildItem (Join-Path $second.Path 'previous-receipts') -File)
        Assert-Evidence ($previous.Count -eq 1 -and (Get-FileHash -LiteralPath $previous[0].FullName).Hash -ceq $hash) 'Previous compatibility receipt lost.'
        Assert-Evidence ([IO.File]::ReadAllText($previous[0].FullName + ':original') -ceq 'retained') 'Previous receipt named stream lost.'
        Assert-Evidence ((Get-Content -LiteralPath $old -Raw | ConvertFrom-Json).exitCode -eq 3) 'Latest failure receipt was hidden.'
        Reject-Evidence { Publish-TestRunEvidenceReceipt $second '../outside.json' @{} } 'Receipt escaped the owned output.'
    } finally { Complete-TestRunEvidence $second $true }
} finally { Complete-TestRunEvidence $run $true }

$run = Start-TestRunEvidence $root @{scenario='locked-payload'}
$locked = Join-Path $run.Path 'still-writing.log'
$writer = [IO.File]::Open($locked, 'CreateNew', 'ReadWrite', 'Read')
try {
    Reject-Evidence { Complete-TestRunEvidence $run $true } 'A payload still open for writing was accepted.'
    Assert-Evidence (-not (Test-Path (Join-Path $run.Directory 'completed.json')) -and (Test-Path $locked)) 'Failed verification lost data or published success.'
} finally { $writer.Dispose() }

$outside = New-Fixture 'outside'
[IO.File]::WriteAllText((Join-Path $outside 'witness.txt'), 'protected')
$rootLink = New-Fixture 'root-link'
[void](New-Item -ItemType Junction -Path (Join-Path $rootLink '.build') -Target $outside)
Reject-Evidence { Start-TestRunEvidence $rootLink @{} } 'A linked output ancestor was accepted.'
Assert-Evidence (-not (Test-Path (Join-Path $outside 'evidence'))) 'Refusal wrote through the linked ancestor.'
$run = Start-TestRunEvidence $root @{scenario='payload-link'}
$payloadLink = Join-Path $run.Path 'link'
[void](New-Item -ItemType Junction -Path $payloadLink -Target $outside)
Reject-Evidence { Complete-TestRunEvidence $run $true } 'A payload junction was traversed.'
Assert-Evidence ([IO.File]::ReadAllText((Join-Path $outside 'witness.txt')) -ceq 'protected') 'Junction target changed.'
Assert-Evidence (-not (Test-Path (Join-Path $run.Directory 'completed.json'))) 'Unsafe payload received a completion record.'
# These two links were created by this fixture. Unlink just the verified link objects, without recursion;
# their targets and all evidence files remain. Do not leave deliberately unsafe links for a later audit.
foreach ($link in @((Join-Path $rootLink '.build'), $payloadLink)) {
    $item = Get-Item -LiteralPath $link -Force
    if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $item.Target -cne $outside -or
        -not [IO.Path]::GetFullPath($link).StartsWith([IO.Path]::GetFullPath($ScratchRoot).TrimEnd('\')+'\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Synthetic junction changed; refusing to unlink: $link"
    }
    [IO.Directory]::Delete($link)
}
Reject-Evidence { Start-TestRunEvidence ([IO.Path]::GetPathRoot($root)) @{} } 'A drive-root output was authorized implicitly.'
Reject-Evidence { Start-TestRunEvidence 'relative-path' @{} } 'A relative output was accepted.'

$run = Start-TestRunEvidence $root @{scenario='rename-control'}
try {
    $moved = $false
    try { [IO.Directory]::Move($run.Path, $run.Path+'-moved'); $moved=$true } catch [IO.IOException] { }
    if ($moved) {
        # ReFS permits some open-directory renames. A fresh identity/path check must still refuse completion.
        [void][IO.Directory]::CreateDirectory($run.Path)
        Reject-Evidence { Complete-TestRunEvidence $run $true } 'A replaced evidence directory was accepted.'
        Assert-Evidence (-not (Test-Path (Join-Path $run.Directory 'completed.json'))) 'A replaced output received successful evidence.'
    } else { Assert-Evidence $true 'Directory rename was refused by the filesystem.' }
} finally { if (-not $run.Closed) { Complete-TestRunEvidence $run $false } }
$run = Start-TestRunEvidence $root @{scenario='long-path-and-ownership'}
try {
    $long = $run.Path
    foreach ($i in 1..12) { $long = Join-Path $long ('Unicode-Ł-' + ('x'*22) + $i) }
    [void][IO.Directory]::CreateDirectory($long)
    $longFile = Join-Path $long 'original.txt'
    [IO.File]::WriteAllText($longFile, 'long-path evidence')
    [IO.File]::WriteAllText($longFile+':source', 'long-path stream')
    $run.Owner = -1
    Reject-Evidence { Complete-TestRunEvidence $run $true } 'Another owner could close the run.'
    $run.Owner = $PID
    Complete-TestRunEvidence $run $true
    $longManifest = Get-Content (Join-Path $run.Directory 'completed.json') -Raw | ConvertFrom-Json
    $longRow = @($longManifest.payload | Where-Object { -not $_.directory })
    Assert-Evidence ($longRow.Count -eq 1 -and $longRow[0].streams.Count -eq 2) 'Long Unicode file or stream omitted.'
} finally { if (-not $run.Closed) { Complete-TestRunEvidence $run $false } }

$volume = Get-Volume -DriveLetter ([IO.Path]::GetPathRoot($root).Substring(0,1))
if ($volume.FileSystem -eq 'NTFS') {
    $run = Start-TestRunEvidence $root @{scenario='shared-read-only-payload'}
    try {
        $shared = Join-Path $outside 'shared.bin'
        [IO.File]::WriteAllText($shared, 'shared payload')
        $alias = Join-Path $run.Path 'shared.bin'
        [void](New-Item -ItemType HardLink -Path $alias -Target $shared)
        [IO.File]::SetAttributes($shared, [IO.FileAttributes]::ReadOnly)
        $handle = [TestRunEvidenceV2.Native]::Pin($shared, $false)
        try { $identity = [TestRunEvidenceV2.Native]::Identity($handle) } finally { $handle.Dispose() }
        Complete-TestRunEvidence $run $true
        $sharedManifest = Get-Content (Join-Path $run.Directory 'completed.json') -Raw | ConvertFrom-Json
        Assert-Evidence ($sharedManifest.payload[0].identity -ceq $identity -and
            ([IO.File]::GetAttributes($shared) -band [IO.FileAttributes]::ReadOnly) -and
            ([IO.File]::GetAttributes($alias) -band [IO.FileAttributes]::ReadOnly)) 'Shared read-only payload or its identity changed.'
    } finally { if (-not $run.Closed) { Complete-TestRunEvidence $run $false } }
}

# Run in an actual child so a hard process exit bypasses finally, as a terminated runner would.
$child = Join-Path $ScratchRoot 'interrupted.ps1'
$escapedModule = $ModulePath.Replace("'", "''")
$interruptedRoot = New-Fixture 'interrupted'
$escapedRoot = $interruptedRoot.Replace("'", "''")
[IO.File]::WriteAllText($child, "Import-Module '$escapedModule'; `$r=Start-TestRunEvidence '$escapedRoot' @{}; [IO.File]::WriteAllText((Join-Path `$r.Path 'partial.log'),'partial'); [Environment]::Exit(17)")
& (Get-Process -Id $PID).Path -NoProfile -File $child | Out-Host
Assert-Evidence ($LASTEXITCODE -eq 17) 'Interruption child did not exit at its intended boundary.'
$interrupted = @(Get-ChildItem (Join-Path $interruptedRoot '.build/evidence/test-runs') -Directory)
Assert-Evidence ($interrupted.Count -eq 1 -and (Test-Path (Join-Path $interrupted[0].FullName 'payload/partial.log')) -and
    -not (Test-Path (Join-Path $interrupted[0].FullName 'completed.json'))) 'Interrupted evidence was deleted or falsely marked complete.'
$later = Start-TestRunEvidence $interruptedRoot @{scenario='after-interruption'}
Complete-TestRunEvidence $later $true
Assert-Evidence (Test-Path (Join-Path $interrupted[0].FullName 'payload/partial.log')) 'A later run removed interrupted evidence.'

$nativeRoot = New-Fixture 'native-output'
$native = Join-Path $nativeRoot '.build/test-artifacts'
[void][IO.Directory]::CreateDirectory($native)
[IO.File]::WriteAllText((Join-Path $native 'old.png'), 'previous unique capture')
$run = Start-TestRunEvidence $nativeRoot @{scenario='native-output'} -CaptureNativeArtifacts
try {
    Assert-Evidence ((Test-Path (Join-Path $run.Path 'previous-native-artifacts/old.png')) -and -not (Test-Path $native)) 'Previous native captures lost or left in active output.'
    Reject-Evidence { Start-TestRunEvidence $nativeRoot @{} -CaptureNativeArtifacts } 'Concurrent native output lease was accepted.'
    [void][IO.Directory]::CreateDirectory($native)
    [IO.File]::WriteAllText((Join-Path $native 'new.png'), 'new unique capture')
    [IO.File]::WriteAllText((Join-Path $native 'new.png:scenario'), 'original native scenario')
    Complete-TestRunEvidence $run $false
    Assert-Evidence ((Test-Path (Join-Path $run.Path 'native-artifacts/new.png')) -and -not (Test-Path $native)) 'Failed native capture output was not retained.'
    Assert-Evidence ([IO.File]::ReadAllText((Join-Path $run.Path 'native-artifacts/new.png:scenario')) -ceq 'original native scenario') 'Native capture stream changed in rename.'
} finally { if (-not $run.Closed) { Complete-TestRunEvidence $run $false } }

# FindFirstFile metadata can be stale on a busy NTFS host. Keep the stale provider objects and reverse
# their enumeration, while the actual files have newer metadata. A safe rename must still verify exactly.
$cachedRoot = New-Fixture 'cached-native-metadata'
$cachedNative = Join-Path $cachedRoot '.build/test-artifacts'
$nested = Join-Path $cachedNative 'nested'
[void][IO.Directory]::CreateDirectory($nested)
foreach ($relative in @('a.txt','z.txt','nested/inside.txt')) {
    [IO.File]::WriteAllText((Join-Path $cachedNative $relative), 'unique cached-metadata evidence: '+$relative)
}
$cache = @{}
foreach ($folder in @($cachedNative,$nested)) {
    $items = @(Get-ChildItem -LiteralPath $folder -Force)
    foreach ($item in $items) { $item.Refresh(); [void]$item.LastWriteTimeUtc; [void]$item.CreationTimeUtc; [void]$item.Attributes }
    [array]::Reverse($items)
    $cache[[IO.Path]::GetFullPath($folder)] = $items
}
$changed = Join-Path $cachedNative 'a.txt'
[IO.File]::SetCreationTimeUtc($changed,[DateTime]::UtcNow.AddDays(-3))
[IO.File]::SetLastWriteTimeUtc($changed,[DateTime]::UtcNow.AddDays(-2))
[IO.File]::SetAttributes($changed,[IO.FileAttributes]::ReadOnly -bor [IO.FileAttributes]::Hidden)
[IO.Directory]::SetLastWriteTimeUtc($nested,[DateTime]::UtcNow.AddDays(-1))
$expectedCreated = [IO.File]::GetCreationTimeUtc($changed).Ticks
$expectedWritten = [IO.File]::GetLastWriteTimeUtc($changed).Ticks
$expectedDirectoryWritten = [IO.Directory]::GetLastWriteTimeUtc($nested).Ticks
$stale = @($cache[[IO.Path]::GetFullPath($cachedNative)] | Where-Object Name -eq 'a.txt')[0]
Assert-Evidence ($stale.LastWriteTimeUtc.Ticks -ne $expectedWritten) 'The cached-metadata negative control was not stale.'
$evidenceModule = Get-Module | Where-Object Path -eq $ModulePath | Select-Object -First 1
& $evidenceModule {
    param($cache)
    $script:EvidenceTestCache = $cache
    function script:Get-ChildItem {
        param([string]$LiteralPath,[switch]$Force)
        $key = [IO.Path]::GetFullPath($LiteralPath)
        if ($script:EvidenceTestCache.ContainsKey($key)) { return $script:EvidenceTestCache[$key] }
        Microsoft.PowerShell.Management\Get-ChildItem -LiteralPath $LiteralPath -Force:$Force
    }
} $cache
$cachedRun = $null
try {
    $cachedRun = Start-TestRunEvidence $cachedRoot @{scenario='cached-metadata-and-reversed-enumeration'} -CaptureNativeArtifacts
    $intent = Get-Content (Join-Path $cachedRun.Directory 'previous-native-artifacts.intent.json') -Raw | ConvertFrom-Json
    $observed = Get-Content (Join-Path $cachedRun.Directory 'previous-native-artifacts.after.json') -Raw | ConvertFrom-Json
    $entry = @($intent.inventory | Where-Object path -ceq 'a.txt')[0]
    $folder = @($intent.inventory | Where-Object path -ceq 'nested')[0]
    Assert-Evidence ($entry.createdTicks -eq $expectedCreated -and $entry.writtenTicks -eq $expectedWritten -and
        ($entry.attributes -band [int][IO.FileAttributes]::ReadOnly) -and ($entry.attributes -band [int][IO.FileAttributes]::Hidden)) 'Inventory trusted stale provider file metadata.'
    Assert-Evidence ($folder.writtenTicks -eq $expectedDirectoryWritten) 'Inventory trusted stale provider directory metadata.'
    Assert-Evidence (($intent.inventory.path -join '|') -ceq 'a.txt|nested|nested\inside.txt|z.txt') 'Inventory comparison depends on provider enumeration order.'
    Assert-Evidence (($intent.inventory | ConvertTo-Json -Depth 12 -Compress) -ceq
        ($observed.inventory | ConvertTo-Json -Depth 12 -Compress)) 'Observed move evidence differs from its exact intent.'
    Complete-TestRunEvidence $cachedRun $true
} finally {
    & $evidenceModule { Remove-Item Function:Get-ChildItem; Remove-Variable EvidenceTestCache -Scope Script }
    if ($cachedRun -and -not $cachedRun.Closed) { Complete-TestRunEvidence $cachedRun $false }
}

[ordered]@{passed=$script:count;module=(Get-FileHash -LiteralPath $ModulePath).Hash;scratch=$ScratchRoot} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ScratchRoot 'result.json')
Write-Host "PASS $script:count evidence preservation checks; fixtures retained at $ScratchRoot"
$global:LASTEXITCODE=0
