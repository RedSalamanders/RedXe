#Requires -Version 7.4
<#
.SYNOPSIS
Collects paired Release x64 measurements of RedXe's offscreen WARP AV views.
.DESCRIPTION
Builds the requested revisions in separate worktrees. Reuses measure-av-views.ps1
and retains the build provenance, raw rounds and per-block comparisons.
.PARAMETER BaselineRevision
Git revision of the baseline source.
.PARAMETER CandidateRevision
Git revision of the candidate source. Defaults to HEAD.
.PARAMETER Blocks
Number of four-observation ABBA or BAAB blocks.
.PARAMETER Seed
Reproducible seed for the order. A default of -1 selects and records a seed.
.PARAMETER CalibrationAA
Uses one revision for both labels to measure baseline variation.
.PARAMETER OutputDirectory
Parent for the unique study directory under the project's .build directory.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $BaselineRevision,
    [string] $CandidateRevision = 'HEAD',
    [ValidateRange(5, 20)][int] $Blocks = 5,
    [ValidateRange(-1, [int]::MaxValue)][int] $Seed = -1,
    [switch] $CalibrationAA,
    [string] $OutputDirectory = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (!$IsWindows -or [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'X64' -or
    [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture -ne 'X64') {
    throw 'Use native x64 Windows PowerShell 7 for the offscreen AV comparison.'
}
$root = $PSScriptRoot
$engine = Join-Path $root '.ci-engine'
Import-Module (Join-Path $engine 'Tools/Modules/Ci.Policy.psm1') -ErrorAction Stop
Import-Module (Join-Path $engine 'Tools/Modules/Ci.Project.psm1') -ErrorAction Stop

function Get-CleanCommit([string] $Tree) {
    $commit = Resolve-CiCommit $Tree 'HEAD'
    $changes = @(git -C $Tree status --porcelain --untracked-files=normal)
    if ($LASTEXITCODE -or $changes.Count) { throw "Expected a clean checkout at $Tree" }
    return $commit
}

$driver = Get-CleanCommit $root
$enginePin = Get-CleanCommit $engine
$baseline = Resolve-CiCommit $root $BaselineRevision
$candidate = Resolve-CiCommit $root $CandidateRevision
if (($baseline -ceq $candidate) -ne [bool]$CalibrationAA) {
    throw 'Use distinct revisions for a comparison and one revision for CalibrationAA.'
}
foreach ($revision in @($driver, $baseline, $candidate) | Select-Object -Unique) {
    $policy = git -C $root show "$($revision):.ci/project.json" | ConvertFrom-Json
    if ($LASTEXITCODE -or $policy.repository -cne 'RedSalamanders/RedXe' -or $policy.engine.ref -cne $enginePin) {
        throw "Revision $revision must pin the same reviewed RedXe engine."
    }
}
$inputs = [ordered]@{}
foreach ($path in 'Dependencies/DxUi.lock.json', 'Dependencies/RedMonitor.lock.json', 'vcpkg.json', 'vcpkg-tool.json',
    'measure-av-views.ps1', 'Tests/AVControlTests/AVControlTests.vcxproj',
    'Tests/AVControlTests/AVControl.Tests.Runner.cpp', 'Tests/AVControlTests/AVControl.Tests.NativeView.cpp') {
    $before = git -C $root rev-parse --verify "$($baseline):$path"
    if ($LASTEXITCODE) { throw "Baseline input is missing. $path" }
    $after = git -C $root rev-parse --verify "$($candidate):$path"
    if ($LASTEXITCODE -or $before -cne $after) { throw "Paired input must match. $path" }
    $inputs[$path] = $before
}

$buildRoot = Join-Path $root '.build'
$parent = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory, $root) } else { Join-Path $buildRoot 'paired-av' }
$parent = Assert-CiContainedPath -Root $buildRoot -Path $parent
$runRoot = Assert-CiContainedPath -Root $root -Path (Join-Path $parent ([guid]::NewGuid().ToString('N').Substring(0, 12)))
if (Test-Path -LiteralPath $runRoot) { throw 'Use a fresh study directory.' }
$evidence = [IO.Directory]::CreateDirectory((Join-Path $runRoot 'evidence')).FullName
$runTemp = [IO.Directory]::CreateDirectory((Join-Path $runRoot 'tmp')).FullName

if ($Seed -eq -1) { $Seed = [Security.Cryptography.RandomNumberGenerator]::GetInt32([int]::MaxValue) }
$random = [Random]::new($Seed)
$schedule = @(1..$Blocks | ForEach-Object { if ($random.Next(2) -eq 0) { 'ABBA' } else { 'BAAB' } })
$metrics = @('prepareP95Ms', 'composeCpuP95Ms', 'frameP95Ms', 'completedOffscreenFps', 'privateBytes', 'workingSetBytes')
$report = [ordered]@{
    schemaVersion = 1
    status = 'started'
    scope = 'offscreen AV tile and overlay using WARP'
    driver = $driver
    engine = $enginePin
    baseline = $baseline
    candidate = $candidate
    calibrationAA = [bool]$CalibrationAA
    platform = 'x64'
    configuration = 'Release'
    inputs = $inputs
    environment = $null
    seed = $Seed
    schedule = $schedule
    builds = [ordered]@{}
    observations = [Collections.Generic.List[object]]::new()
    pairedDifferences = [Collections.Generic.List[object]]::new()
}
$study = Join-Path $evidence 'study.json'
$hostPath = (Get-Process -Id $PID).Path

function Get-AvBuild([string] $Tree, [string] $Source) {
    if ((Get-CleanCommit $Tree) -cne $Source) { throw "Build checkout changed. $Source" }
    $profile = Join-Path $Tree '.build/x64/Release'
    $binary = Join-Path $profile 'AVControlTests.exe'
    $provenancePath = Join-Path $profile 'DxUi.provenance.json'
    $provenance = Get-Content -LiteralPath $provenancePath -Raw | ConvertFrom-Json
    $module = @($provenance.modules | Where-Object path -CEQ 'AVControlTests.exe')
    $hash = (Get-FileHash -LiteralPath $binary).Hash.ToLowerInvariant()
    Assert-CiPeArchitecture -Path $binary -Platform x64
    if ($provenance.platform -cne 'x64' -or $provenance.configuration -cne 'Release' -or $provenance.buildRole -cne 'test-enabled' -or
        $provenance.dependencyRecipe.recipe -cne 'PinnedSource' -or $module.Count -ne 1 -or $module[0].sha256.ToLowerInvariant() -cne $hash) {
        throw "Expected a pinned Source Release AV test executable. $Source"
    }
    return [ordered]@{
        tree = $Tree
        source = $Source
        binarySha256 = $hash
        provenance = $provenancePath
        provenanceSha256 = (Get-FileHash -LiteralPath $provenancePath).Hash
        dxUiCommit = $provenance.commit
        archiveSha256 = $provenance.archiveSha256.ToLowerInvariant()
        toolchain = $provenance.buildIdentity.Fingerprint
    }
}

function New-AvBuild([string] $Role, [string] $Source) {
    $tree = Join-Path $runRoot $Role
    git -C $root -c core.longpaths=true worktree add --quiet --detach $tree $Source
    if ($LASTEXITCODE) { throw "Could not check out $Role at $Source" }
    $treeEngine = Join-Path $tree '.ci-engine'
    git -c core.longpaths=true clone --quiet --no-hardlinks --no-checkout -- $engine $treeEngine
    if ($LASTEXITCODE) { throw 'Could not provision the pinned engine.' }
    git -C $treeEngine remote set-url origin 'https://github.com/RedSalamanders/.github.git'
    if ($LASTEXITCODE) { throw 'Could not configure the pinned engine origin.' }
    git -C $treeEngine switch --quiet --detach $enginePin
    if ($LASTEXITCODE -or (Get-CleanCommit $treeEngine) -cne $enginePin) { throw 'Could not select the pinned engine.' }

    $log = Join-Path $evidence "build-$Role.log"
    & $hostPath -NoProfile -NonInteractive -File (Join-Path $tree 'build.ps1') -Platform x64 -Configuration Release -MaxCpuCount 2 *> $log
    if ($LASTEXITCODE -or !(Select-String -LiteralPath $log -Pattern 'Diagnostics: 0 warning\(s\), 0 error\(s\)' -Quiet)) {
        throw "The measured Release build did not pass without warnings. $log"
    }
    $build = Get-AvBuild $tree $Source
    Copy-Item -LiteralPath $build.provenance -Destination (Join-Path $evidence "build-$Role.json")
    return $build
}

function Read-AvObservation([string] $Path, [System.Collections.IDictionary] $Build) {
    $native = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json -AsHashtable
    $metadata = $native.metadata
    if ($native.fixture -cne 'redxe-av-two-views-v1' -or $native.renderer -cne 'WARP' -or $native.views -ne 2 -or
        $native.framesPerRound -ne 600 -or $native.roundCount -ne 5 -or @($native.scenarios).Count -ne 6 -or
        $native.hiddenSurfaceBytes -cne 0 -or $native.hiddenPreparations -cne 0 -or $native.hiddenComposites -cne 0 -or
        $metadata.productCommit -cne $Build.source -or @($metadata.productStatus).Count -or $metadata.platform -cne 'x64' -or
        $metadata.configuration -cne 'Release' -or $metadata.dxUiCommit -cne $Build.dxUiCommit -or
        $metadata.dependencyRecipe.recipe -cne 'PinnedSource' -or
        $metadata.binaryInputs['.build/x64/Release/AVControlTests.exe'] -cne $Build.binarySha256) {
        throw "Wrong source, fixture, or build identity in $Path"
    }
    $archives = @($metadata.binaryInputs.GetEnumerator() | Where-Object Key -CLike '*/DxUi.lib')
    if ($archives.Count -ne 1 -or $archives[0].Value -cne $Build.archiveSha256) { throw "Wrong linked archive in $Path" }

    $values = [ordered]@{}
    foreach ($dpi in 96,144,192) {
        foreach ($mode in 'clean','dirty') {
            $sample = @($native.scenarios | Where-Object { $_.dpi -eq $dpi -and $_.name -ceq $mode })
            if ($sample.Count -ne 1 -or @($sample[0].rounds).Count -ne 5) { throw "Missing $dpi $mode rounds in $Path" }
            $rounds = $sample[0].rounds
            if (@($rounds | Where-Object { $_.surfaceBytes -ne 4608000 -or $_.composites -ne 1200 -or
                ($mode -ceq 'clean' -and $_.preparations -ne 0) }).Count) {
                throw "The fixture's surface or work counts changed in $Path"
            }
            foreach ($metric in $metrics) {
                $sorted = @(foreach ($round in $rounds) {
                    if (!$round.Contains($metric) -or $null -eq $round[$metric]) { throw "Missing $metric in $Path" }
                    $value = $round[$metric]
                    if ($value.GetType().Name -cnotin 'Int32','Int64','Double' -or ![double]::IsFinite($value) -or $value -lt 0) {
                        throw "Invalid $metric in $Path"
                    }
                    $number = [double]$value
                    $number
                })
                $values["$dpi/$mode/$metric"] = ($sorted | Sort-Object)[2]
            }
        }
    }
    return [ordered]@{
        environment = [ordered]@{
            machine = $metadata.machine
            cpu = $metadata.cpu
            operatingSystem = $metadata.operatingSystem
            warpVersion = $metadata.warpVersion
            powerPolicy = $metadata.powerPolicy
            compiler = $native.compiler
            wrapperSha256 = $metadata.wrapperSha256
            fixtureInputs = $metadata.fixtureInputs
        }
        metrics = $values
    }
}

$previousTemp = $env:TEMP
$previousTmp = $env:TMP
$previousBinarySources = $env:VCPKG_BINARY_SOURCES
try {
    $env:TEMP = $env:TMP = $runTemp
    $env:VCPKG_BINARY_SOURCES = 'clear'
    $report.builds.A = New-AvBuild A $baseline
    $report.builds.B = if ($CalibrationAA) { $report.builds.A } else { New-AvBuild B $candidate }
    if ($report.builds.A.toolchain -cne $report.builds.B.toolchain -or $report.builds.A.dxUiCommit -cne $report.builds.B.dxUiCommit) {
        throw 'The toolchain or pinned DxUi source differs across the pair.'
    }

    for ($block = 0; $block -lt $schedule.Count; ++$block) {
        $replicates = @{ A = [Collections.Generic.List[object]]::new(); B = [Collections.Generic.List[object]]::new() }
        for ($position = 0; $position -lt 4; ++$position) {
            $role = [string]$schedule[$block][$position]
            $build = $report.builds[$role]
            $name = "block-$($block + 1)-$($position + 1)-$role"
            $path = Join-Path $evidence "$name.json"
            $arguments = @('-NoProfile', '-NonInteractive', '-File', (Join-Path $build.tree 'measure-av-views.ps1'),
                '-Configuration', 'Release', '-SkipBuild', '-OutputPath', $path)
            $result = Invoke-CiProcess -FilePath $hostPath -Arguments $arguments -WorkingDirectory $build.tree -TimeoutSeconds 1200 -AllowFailure
            [IO.File]::WriteAllText((Join-Path $evidence "$name.stdout.log"), $result.stdout)
            [IO.File]::WriteAllText((Join-Path $evidence "$name.stderr.log"), $result.stderr)
            if ($result.exitCode) { throw "The AV view fixture failed. $name (exit $($result.exitCode))" }
            $observed = Read-AvObservation $path $build
            if ($null -eq $report.environment) { $report.environment = $observed.environment }
            if (($observed.environment | ConvertTo-Json -Compress -Depth 8) -cne ($report.environment | ConvertTo-Json -Compress -Depth 8)) {
                throw "The machine or fixture inputs changed during the study. $name"
            }
            $report.observations.Add([ordered]@{block = $block + 1; position = $position + 1; role = $role; file = "$name.json";
                    sha256 = (Get-FileHash -LiteralPath $path).Hash; metrics = $observed.metrics})
            $replicates[$role].Add($observed.metrics)
        }
        foreach ($metric in $replicates.A[0].Keys) {
            $meanA = ($replicates.A[0][$metric] + $replicates.A[1][$metric]) / 2
            $meanB = ($replicates.B[0][$metric] + $replicates.B[1][$metric]) / 2
            $report.pairedDifferences.Add([ordered]@{
                block = $block + 1
                metric = $metric
                baseline = $meanA
                candidate = $meanB
                candidateMinusBaseline = $meanB - $meanA
            })
        }
    }
    foreach ($build in $report.builds.Values) {
        $after = Get-AvBuild $build.tree $build.source
        if ($after.binarySha256 -cne $build.binarySha256 -or $after.provenanceSha256 -cne $build.provenanceSha256) {
            throw "The measured build changed during the study. $($build.source)"
        }
    }
    $report.status = 'collected'
    Write-Host "Paired offscreen AV evidence written to $study"
} catch {
    $report.status = 'failed'
    $report.error = $_.Exception.Message
    throw
} finally {
    $env:TEMP = $previousTemp
    $env:TMP = $previousTmp
    $env:VCPKG_BINARY_SOURCES = $previousBinarySources
    $report | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $study -Encoding utf8NoBOM
}
