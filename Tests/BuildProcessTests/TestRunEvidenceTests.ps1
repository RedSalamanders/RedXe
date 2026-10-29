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
$evidenceModule = Import-Module $ModulePath -Force -PassThru
$script:count = 0
function Assert-Evidence([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
    $script:count++
}
# The body must fail with the expected error: every message in the exception chain (PowerShell wraps a .NET exception
# thrown by a method call) is matched against $Pattern, so a guard that moved or vanished cannot pass on another failure.
function Reject-Evidence([scriptblock] $Body, [string] $Message, [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string] $Pattern) {
    $thrown = $null
    try { & $Body } catch { $thrown = $_.Exception }
    Assert-Evidence ($null -ne $thrown) $Message
    $chain = @(for ($link = $thrown; $link; $link = $link.InnerException) { $link.Message }) -join "`n"
    Assert-Evidence ($chain -cmatch $Pattern) "$Message Rejected with: $chain"
}
# The reason Windows gives for a code, escaped for a pattern: the same machine and language produce the same text.
function Get-OsReason([int] $Code) { [regex]::Escape([ComponentModel.Win32Exception]::new($Code).Message.TrimEnd('.')) }
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
    Reject-Evidence { Complete-TestRunEvidence $a $true } 'Completed evidence could be overwritten.' 'already been closed'
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
        Reject-Evidence { Publish-TestRunEvidenceReceipt $second '../outside.json' @{} } 'Receipt escaped the owned output.' 'Invalid receipt publication'
    } finally { Complete-TestRunEvidence $second $true }
} finally { Complete-TestRunEvidence $run $true }

$run = Start-TestRunEvidence $root @{scenario='locked-payload'}
$locked = Join-Path $run.Path 'still-writing.log'
$writer = [IO.File]::Open($locked, 'CreateNew', 'ReadWrite', 'Read')
try {
    # ERROR_SHARING_VIOLATION, named with its OS reason and the file.
    Reject-Evidence { Complete-TestRunEvidence $run $true } 'A payload still open for writing was accepted.' ((Get-OsReason 32) + ': .*still-writing\.log')
    Assert-Evidence (-not (Test-Path (Join-Path $run.Directory 'completed.json')) -and (Test-Path $locked)) 'Failed verification lost data or published success.'
} finally { $writer.Dispose() }

$outside = New-Fixture 'outside'
[IO.File]::WriteAllText((Join-Path $outside 'witness.txt'), 'protected')
$rootLink = New-Fixture 'root-link'
[void](New-Item -ItemType Junction -Path (Join-Path $rootLink '.build') -Target $outside)
Reject-Evidence { Start-TestRunEvidence $rootLink @{} } 'A linked output ancestor was accepted.' 'Refusing reparse point'
Assert-Evidence (-not (Test-Path (Join-Path $outside 'evidence'))) 'Refusal wrote through the linked ancestor.'
$run = Start-TestRunEvidence $root @{scenario='payload-link'}
$payloadLink = Join-Path $run.Path 'link'
[void](New-Item -ItemType Junction -Path $payloadLink -Target $outside)
Reject-Evidence { Complete-TestRunEvidence $run $true } 'A payload junction was traversed.' 'Refusing reparse point'
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
# The repository must be fully qualified, local and not a drive root. The guard is a pure function, so the real drive root
# and a UNC path go through it alone: a regression could neither write to the root nor reach the network. The entry point
# then gets names that cannot exist, with the working directory inside the scratch, so a regressed guard would fail at its
# first pin and any output it did create would land in the scratch.
$refusal = 'absolute local repository directory'
$guard = { param($candidate) Resolve-TestRunEvidenceRoot $candidate }
$driveRoot = [IO.Path]::GetPathRoot($root)
foreach ($candidate in @($driveRoot, ($driveRoot -replace '\\', '/'), '\\evidence-server\share\repository')) {
    Reject-Evidence { & $evidenceModule $guard $candidate } "The guard accepted '$candidate'." $refusal
}
Assert-Evidence ((& $evidenceModule $guard ($root + '\')) -ceq [IO.Path]::GetFullPath($root).TrimEnd('\')) 'The guard changed a valid repository.'
$workingDirectory = [Environment]::CurrentDirectory
try {
    [Environment]::CurrentDirectory = $ScratchRoot
    $unique = 'no-such-evidence-' + [guid]::NewGuid().ToString('N')
    $drive = $driveRoot.Substring(0, 2)
    # Rooted without a drive (IsPathRooted accepts it), drive-relative (likewise), and plainly relative.
    foreach ($candidate in @('relative-path', "\$unique", "$drive$unique")) {
        Reject-Evidence { Start-TestRunEvidence $candidate @{} } "Start-TestRunEvidence accepted '$candidate'." $refusal
    }
    Assert-Evidence (-not (Test-Path -LiteralPath (Join-Path $ScratchRoot $unique)) -and -not (Test-Path -LiteralPath "$drive\$unique")) 'A refused output was created anyway.'
} finally { [Environment]::CurrentDirectory = $workingDirectory }

$run = Start-TestRunEvidence $root @{scenario='rename-control'}
try {
    $moved = $false
    try { [IO.Directory]::Move($run.Path, $run.Path+'-moved'); $moved=$true } catch [IO.IOException] { }
    if ($moved) {
        # ReFS permits some open-directory renames. A fresh identity/path check must still refuse completion.
        [void][IO.Directory]::CreateDirectory($run.Path)
        Reject-Evidence { Complete-TestRunEvidence $run $true } 'A replaced evidence directory was accepted.' 'Evidence directory identity changed'
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
    Reject-Evidence { Complete-TestRunEvidence $run $true } 'Another owner could close the run.' 'ownership or path mismatch'
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
        $handle = [TestRunEvidenceV3.Native]::Pin($shared, $false)
        try { $identity = [TestRunEvidenceV3.Native]::Identity($handle) } finally { $handle.Dispose() }
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
    Reject-Evidence { Start-TestRunEvidence $nativeRoot @{} -CaptureNativeArtifacts } 'Concurrent native output lease was accepted.' ((Get-OsReason 32) + ': .*test-artifacts\.lock')
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

# Run ids are formatted with the invariant culture. In th-TH the current culture's Buddhist calendar writes the year as
# 2569, so the first assertion proves the control is live; a host without such a culture skips it rather than passing it.
$culture = [Threading.Thread]::CurrentThread.CurrentCulture
$thai = try { [Globalization.CultureInfo]::GetCultureInfo('th-TH') } catch [Globalization.CultureNotFoundException] { $null }
if ($thai -and $thai.DateTimeFormat.Calendar -is [Globalization.ThaiBuddhistCalendar]) {
    try {
        [Threading.Thread]::CurrentThread.CurrentCulture = $thai
        $yearBefore = [DateTime]::UtcNow.Year
        Assert-Evidence ([DateTime]::UtcNow.ToString('yyyy') -ceq [string]($yearBefore + 543)) 'The Buddhist-calendar negative control did not move the year.'
        $run = Start-TestRunEvidence (New-Fixture 'thai-calendar') @{scenario='th-TH'}
        Complete-TestRunEvidence $run $true
    } finally { [Threading.Thread]::CurrentThread.CurrentCulture = $culture }
    $years = @([string] $yearBefore, [string] [DateTime]::UtcNow.Year)
    Assert-Evidence ($run.Id -cmatch '^[0-9]{8}T[0-9]{9}Z-[0-9]+-[0-9a-f]{32}$' -and $run.Id.Substring(0, 4) -in $years) "A run id followed the current culture's calendar: $($run.Id)"
    foreach ($record in 'started', 'completed') {
        $recordText = Get-Content (Join-Path $run.Directory "$record.json") -Raw
        Assert-Evidence ($recordText -cmatch ('"' + $record + 'Utc":\s*"([0-9]{4})-') -and $Matches[1] -in $years) "The $record time followed the current culture's calendar."
    }
} else { Write-Host 'SKIP invariant run id: this host has no Buddhist-calendar culture.' }

# Inventory and stream order is ordinal (UTF-16 code units), never the current culture's: B (0x42), _ (0x5F), a (0x61), then
# the a-umlaut (0xE4). Culture-aware sorting puts _, a, the a-umlaut and only then B.
$auml = [string] [char] 0xE4
$names = @('a', 'B', '_', $auml)
$run = Start-TestRunEvidence (New-Fixture 'ordinal-order') @{scenario='ordinal-order'}
try {
    $streamFile = Join-Path $run.Path 'streams.txt'
    [IO.File]::WriteAllText($streamFile, 'main')
    foreach ($name in $names) {
        [IO.File]::WriteAllText((Join-Path $run.Path $name), $name)
        [IO.File]::WriteAllText($streamFile + ':' + $name, $name)
    }
    Complete-TestRunEvidence $run $true
} finally { if (-not $run.Closed) { Complete-TestRunEvidence $run $false } }
$ordinal = Get-Content (Join-Path $run.Directory 'completed.json') -Raw | ConvertFrom-Json
$streamRow = @($ordinal.payload | Where-Object path -ceq 'streams.txt')[0]
Assert-Evidence (($ordinal.payload.path -join '|') -ceq ('B|_|a|streams.txt|' + $auml)) "Inventory paths are not in ordinal order: $($ordinal.payload.path -join '|')"
Assert-Evidence (($streamRow.streams.Name -join '|') -ceq ('::$DATA|:B:$DATA|:_:$DATA|:a:$DATA|:' + $auml + ':$DATA')) "Stream names are not in ordinal order: $($streamRow.streams.Name -join '|')"
if ((($names | Sort-Object -CaseSensitive) -join '|') -ceq ('B|_|a|' + $auml)) { Write-Host 'SKIP culture-aware ordering control: this host compares ordinally anyway.' }

# A record field documented as a list is a JSON array with one entry and with none. Enumerating pipelines would write a lone
# entry bare and an empty list as null: ConvertFrom-Json unrolls an array, as does a piped ConvertTo-Json.
$listRoot = New-Fixture 'single-entry-lists'
$listNative = Join-Path $listRoot '.build/test-artifacts'
[void][IO.Directory]::CreateDirectory($listNative)
[IO.File]::WriteAllText((Join-Path $listNative 'only.png'), 'single capture')
$run = Start-TestRunEvidence $listRoot @{scenario='single-entry-native'} -CaptureNativeArtifacts
try {
    [void][IO.Directory]::CreateDirectory($listNative)
    [IO.File]::WriteAllText((Join-Path $listNative 'last.png'), 'single capture at completion')
    Complete-TestRunEvidence $run $true
} finally { if (-not $run.Closed) { Complete-TestRunEvidence $run $false } }
foreach ($label in 'previous-native-artifacts', 'native-artifacts') {
    foreach ($kind in 'intent', 'after') {
        $inventory = (Get-Content (Join-Path $run.Directory "$label.$kind.json") -Raw | ConvertFrom-Json).inventory
        Assert-Evidence ($inventory -is [array] -and $inventory.Length -eq 1 -and $inventory[0].streams -is [array] -and $inventory[0].streams.Length -eq 1) "$label.$kind.json did not keep its one-entry inventory a list."
    }
}
$emptyNative = New-Fixture 'empty-native-list'
[void][IO.Directory]::CreateDirectory((Join-Path $emptyNative '.build/test-artifacts'))
$run = Start-TestRunEvidence $emptyNative @{scenario='empty-native'} -CaptureNativeArtifacts
Complete-TestRunEvidence $run $true
foreach ($kind in 'intent', 'after') {
    $inventory = (Get-Content (Join-Path $run.Directory "previous-native-artifacts.$kind.json") -Raw | ConvertFrom-Json).inventory
    Assert-Evidence ($inventory -is [array] -and $inventory.Length -eq 0) "previous-native-artifacts.$kind.json wrote an empty inventory as null."
}
$single = Start-TestRunEvidence $listRoot @{scenario='one-payload-entry'}
[IO.File]::WriteAllText((Join-Path $single.Path 'only.log'), 'one')
Complete-TestRunEvidence $single $true
$empty = Start-TestRunEvidence $listRoot @{scenario='no-payload-entry'}
Complete-TestRunEvidence $empty $true
$one = Get-Content (Join-Path $single.Directory 'completed.json') -Raw | ConvertFrom-Json
$none = Get-Content (Join-Path $empty.Directory 'completed.json') -Raw | ConvertFrom-Json
Assert-Evidence ($one.payload -is [array] -and $one.payload.Length -eq 1 -and $one.payload[0].streams -is [array] -and $one.payload[0].streams.Length -eq 1) 'completed.json did not keep a one-entry payload a list.'
Assert-Evidence ($none.payload -is [array] -and $none.payload.Length -eq 0) 'completed.json did not keep an empty payload a list.'
$run = Start-TestRunEvidence $listRoot @{scenario='one-element-receipt'}
try { Publish-TestRunEvidenceReceipt $run 'Single.json' @('only') } finally { Complete-TestRunEvidence $run $true }
Assert-Evidence ((Get-Content (Join-Path $run.Path 'Single.json') -Raw).TrimStart().StartsWith('[')) 'A one-element receipt was written as its element.'

# A Windows failure names the OS reason and then the path; Win32Exception(code, path) alone would say only the path.
$absent = Join-Path $ScratchRoot ('no-such-evidence-file-' + [guid]::NewGuid().ToString('N'))
$win32 = $null
try { [void][TestRunEvidenceV3.Native]::Pin($absent, $false) } catch { $win32 = $_.Exception; while ($win32.InnerException) { $win32 = $win32.InnerException } }
Assert-Evidence ($win32 -is [ComponentModel.Win32Exception] -and $win32.NativeErrorCode -eq 2) 'Pin did not fail with ERROR_FILE_NOT_FOUND and its native code.'
Assert-Evidence ($win32.Message -ceq ([ComponentModel.Win32Exception]::new(2).Message.TrimEnd('.') + ': ' + $absent)) "A Win32 failure lost the OS reason or the path: $($win32.Message)"

[ordered]@{passed=$script:count;module=(Get-FileHash -LiteralPath $ModulePath).Hash;scratch=$ScratchRoot} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ScratchRoot 'result.json')
Write-Host "PASS $script:count evidence preservation checks; fixtures retained at $ScratchRoot"
$global:LASTEXITCODE=0
