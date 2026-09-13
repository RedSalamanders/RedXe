<#!
.SYNOPSIS
Builds RedXe and runs its automated plugin and host tests.

.DESCRIPTION
The test validates settings templates, schema, strict parsing, file stamps, and event-driven live-reload notification,
then validates the factory, generic widget, GPU, and native-window ABI, including Matrix Rain configuration rejection,
deterministic WARP pixel readback, device recreation, and child-HWND teardown. It finally creates a hidden Win32
window and drives the production PluginManager, DashboardHost, and Renderer through Release and Debug plugin
compositions, transactional Matrix reconfiguration, suspend/restore, lifetime teardown, WARP rendering, and frame
scheduling decisions. The final application smoke test also creates a hidden flip-model WARP swap chain and presents
frames without touching the user's settings. A final isolated child-process crash validates the production SEH
boundary, minidump, call-stack report, and marker for both an application exception and a real stack overflow without
touching the user's crash directory. GPU plugins must not load the runtime shader compiler.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'ASan Debug')]
    [string] $Configuration = 'Debug',

    [ValidateSet('x64', 'ARM64')]
    [string] $Platform = 'x64',

    [switch] $Rebuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSCommandPath
$nativeArchitecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
if ($Platform -eq 'ARM64' -and $nativeArchitecture -ne 'Arm64') {
    throw 'ARM64 runtime qualification requires a native ARM64 host; use build.ps1 for cross-compilation.'
}
$buildArguments = @{
    Configuration = $Configuration
    Platform = $Platform
}
if ($Rebuild) {
    $buildArguments.Rebuild = $true
}

& (Join-Path $repoRoot 'build.ps1') @buildArguments
if ($LASTEXITCODE -ne 0) {
    throw "Build entrypoint failed with exit code $LASTEXITCODE."
}
Import-Module (Join-Path $repoRoot 'Build/DxUiProvenance.psm1') -Force
Import-Module (Join-Path $repoRoot 'Build/BuildPresentation.psm1') -Force
Assert-RedXeDxUiProvenance -OutputRoot (Join-Path $repoRoot ".build/$Platform/$Configuration") -LockFile (Join-Path $repoRoot 'Dependencies/DxUi.lock.json') -Platform $Platform -Configuration $Configuration

$executable = Join-Path $repoRoot ".build\$Platform\$Configuration\RedXe.exe"
if ($Platform -ne 'x64' -and $env:PROCESSOR_ARCHITECTURE -eq 'AMD64') {
    throw 'The ARM64 smoke test must run on ARM64 Windows. The build itself completed successfully.'
}

$executableVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($executable)
if ($executableVersion.FileDescription -ne 'RedXe XENEON dashboard' -or
    $executableVersion.OriginalFilename -ne 'RedXe.exe' -or
    $executableVersion.ProductName -ne 'RedXe') {
    throw 'RedXe.exe is missing its stable Windows executable version identity.'
}

Write-Host 'Running exact build-output process preflight tests...' -ForegroundColor Cyan
& (Join-Path $repoRoot 'Tests\BuildProcessTests\BuildProcessTests.ps1')
& (Join-Path $repoRoot 'Tests\BuildProcessTests\DxUiProvenanceTests.ps1')
& (Join-Path $repoRoot 'Tests\BuildProcessTests\DxUiUpdateTests.ps1')

$contractTests = Join-Path $repoRoot ".build\$Platform\$Configuration\PluginContractTests.exe"
if ($Configuration -eq 'ASan Debug') {
    $probeLog = Join-Path $repoRoot ".build\logs\I19-ASan-probe-$Platform-$([guid]::NewGuid().ToString('N')).log"
    $previousOptions=$env:ASAN_OPTIONS
    try {
        $env:ASAN_OPTIONS='halt_on_error=1:abort_on_error=0:detect_leaks=0'
        & $contractTests --asan-probe *> $probeLog
        $probeExit=$LASTEXITCODE
    } finally { $env:ASAN_OPTIONS=$previousOptions }
    if ($probeExit -eq 0 -or -not (Select-String -LiteralPath $probeLog -SimpleMatch 'AddressSanitizer: heap-use-after-free')) {
        throw "ASAN failed to diagnose the isolated deliberate defect: $probeLog"
    }
    Write-Host "PASS AddressSanitizer detection probe: $probeLog"
}
Write-Host 'Running plugin ABI and rendering-interface contract tests...' -ForegroundColor Cyan
$contractProcess = Invoke-RedXeStreamingProcess -FilePath $contractTests -WorkingDirectory $repoRoot -LogPath ($contractTests + '.log')
if ($contractProcess -ne 0) {
    throw "Plugin contract tests failed with exit code $($contractProcess)."
}

$avControlTests = Join-Path $repoRoot ".build\$Platform\$Configuration\AVControlTests.exe"
Write-Host 'Running AV Control model, input and layout tests...' -ForegroundColor Cyan
$avControlProcess = Invoke-RedXeStreamingProcess -FilePath $avControlTests -WorkingDirectory $repoRoot -LogPath ($avControlTests + '.log')
if ($avControlProcess -ne 0) {
    throw "AV Control tests failed with exit code $($avControlProcess)."
}
& (Join-Path $repoRoot 'Tests/AVControlTests/CameraPackageTests.ps1') -Configuration $Configuration -Platform $Platform

$systemDataTests = Join-Path $repoRoot ".build\$Platform\$Configuration\SystemDataTests.exe"
Write-Host 'Running local system-data provider contract tests...' -ForegroundColor Cyan
$systemDataProcess = Invoke-RedXeStreamingProcess -FilePath $systemDataTests -WorkingDirectory $repoRoot -LogPath ($systemDataTests + '.log')
if ($systemDataProcess -ne 0) {
    throw "System-data provider tests failed with exit code $($systemDataProcess)."
}
if ($Configuration -eq 'Release' -and $Platform -eq 'x64') {
    Write-Host 'Running Release system-data row-cap resource measurement...' -ForegroundColor Cyan
    $systemDataBenchmark = Invoke-RedXeStreamingProcess -FilePath $systemDataTests -WorkingDirectory $repoRoot -Arguments @('--benchmark') -LogPath ($systemDataTests + '-benchmark.log')
    if ($systemDataBenchmark -ne 0) {
        throw "System-data row-cap measurement failed with exit code $($systemDataBenchmark)."
    }
    Write-Host 'Running Release system-data per-domain measurement...' -ForegroundColor Cyan
    $systemDataDomains = Invoke-RedXeStreamingProcess -FilePath $systemDataTests -WorkingDirectory $repoRoot -Arguments @('--domains') -LogPath ($systemDataTests + '-domains.log')
    if ($systemDataDomains -ne 0) {
        throw "System-data per-domain measurement failed with exit code $($systemDataDomains)."
    }
}

$systemDataPhase0 = Join-Path $repoRoot ".build\$Platform\$Configuration\SystemDataPhase0.exe"
Write-Host 'Running system-data Phase 0 acquisition spikes...' -ForegroundColor Cyan
$systemDataPhase0Process = Invoke-RedXeStreamingProcess -FilePath $systemDataPhase0 -WorkingDirectory $repoRoot -LogPath ($systemDataPhase0 + '.log')
if ($systemDataPhase0Process -ne 0) {
    throw "System-data Phase 0 spikes failed with exit code $($systemDataPhase0Process)."
}

$studioClockTests = Join-Path $repoRoot ".build\$Platform\$Configuration\StudioClockTests.exe"
Write-Host 'Running Studio Clock contract, scheduling, WARP, and resource tests...' -ForegroundColor Cyan
$studioClockProcess = Invoke-RedXeStreamingProcess -FilePath $studioClockTests -WorkingDirectory $repoRoot -LogPath ($studioClockTests + '.log')
if ($studioClockProcess -ne 0) {
    throw "Studio Clock tests failed with exit code $($studioClockProcess)."
}

$deskClockTests = Join-Path $repoRoot ".build\$Platform\$Configuration\DeskClockTests.exe"
Write-Host 'Running Desk Clock contract, scheduling, WARP, and resource tests...' -ForegroundColor Cyan
$deskClockProcess = Invoke-RedXeStreamingProcess -FilePath $deskClockTests -WorkingDirectory $repoRoot -LogPath ($deskClockTests + '.log')
if ($deskClockProcess -ne 0) {
    throw "Desk Clock tests failed with exit code $($deskClockProcess)."
}

$launcherTests = Join-Path $repoRoot ".build\$Platform\$Configuration\LauncherTests.exe"
Write-Host 'Running Launcher factory, pin fallback, WARP, launch, and drop tests...' -ForegroundColor Cyan
$launcherProcess = Invoke-RedXeStreamingProcess -FilePath $launcherTests -WorkingDirectory $repoRoot -LogPath ($launcherTests + '.log')
if ($launcherProcess -ne 0) {
    throw "Launcher tests failed with exit code $($launcherProcess)."
}

$weatherTests = Join-Path $repoRoot ".build\$Platform\$Configuration\WeatherTests.exe"
Write-Host 'Running Weather HTTP, unit, and label format tests...' -ForegroundColor Cyan
$weatherProcess = Invoke-RedXeStreamingProcess -FilePath $weatherTests -WorkingDirectory $repoRoot -LogPath ($weatherTests + '.log')
if ($weatherProcess -ne 0) {
    throw "Weather tests failed with exit code $($weatherProcess)."
}

$settingsTests = Join-Path $repoRoot ".build\$Platform\$Configuration\SettingsTests.exe"
Write-Host 'Running settings, schema, stamp, and watcher contract tests...' -ForegroundColor Cyan
$settingsProcess = Invoke-RedXeStreamingProcess -FilePath $settingsTests -WorkingDirectory $repoRoot -LogPath ($settingsTests + '.log')
if ($settingsProcess -ne 0) {
    throw "Settings tests failed with exit code $($settingsProcess)."
}

$hostPluginTests = Join-Path $repoRoot ".build\$Platform\$Configuration\HostPluginTests.exe"
Write-Host 'Running production host and plugin integration tests...' -ForegroundColor Cyan
$hostPluginLog = Join-Path $repoRoot ".build\$Platform\$Configuration\HostPluginTests.log"
$hostPluginErrors = Join-Path $repoRoot ".build\$Platform\$Configuration\HostPluginTests.stderr.log"
$hostPluginProcess = Start-Process -WindowStyle Hidden -FilePath $hostPluginTests -Wait -PassThru `
    -RedirectStandardOutput $hostPluginLog -RedirectStandardError $hostPluginErrors
if ($hostPluginProcess.ExitCode -ne 0) {
    Get-Content -LiteralPath $hostPluginLog -Tail 80
    Get-Content -LiteralPath $hostPluginErrors -Tail 40
    throw "Host/plugin integration tests failed with exit code $($hostPluginProcess.ExitCode)."
}
Write-Host "Host integration log: $hostPluginLog" -ForegroundColor DarkGray

Write-Host 'Running hidden Direct3D 11 WARP smoke test...' -ForegroundColor Cyan
$process = Start-Process -WindowStyle Hidden -FilePath $executable -ArgumentList @('--self-test', '--warp') -Wait -PassThru
if ($process.ExitCode -ne 0) {
    throw "Smoke test failed with exit code $($process.ExitCode)."
}

function Invoke-RedXeCrashTest {
    param(
        [Parameter(Mandatory)]
        [string] $CrashArgument,

        [Parameter(Mandatory)]
        [uint32] $ExpectedExceptionCode,

        [Parameter(Mandatory)]
        [string] $Label
    )

    $buildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '.build'))
    $crashTestDirectory = [IO.Path]::GetFullPath(
        (Join-Path $buildRoot (Join-Path 'CrashTests' ([guid]::NewGuid().ToString('N')))))
    $expectedPrefix = $buildRoot.TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $crashTestDirectory.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to use a crash-test directory outside the build root: $crashTestDirectory"
    }

    Write-Host "Running isolated $Label crash diagnostics test..." -ForegroundColor Cyan
    [void](New-Item -ItemType Directory -Path $crashTestDirectory -Force)
    try {
        $quotedDirectoryArgument = '"--crash-test-directory={0}"' -f $crashTestDirectory
        $crashProcess = Start-Process -WindowStyle Hidden -FilePath $executable `
            -ArgumentList @($CrashArgument, $quotedDirectoryArgument) -Wait -PassThru
    if ($crashProcess.ExitCode -ne 127) {
        throw "Crash harness returned exit code $($crashProcess.ExitCode); expected 127."
    }

    $dumps = @(Get-ChildItem -LiteralPath $crashTestDirectory -Filter '*.dmp' -File)
    if ($dumps.Count -ne 1) {
        throw "Crash harness created $($dumps.Count) minidumps; expected exactly one."
    }

    $dumpBytes = [IO.File]::ReadAllBytes($dumps[0].FullName)
    if ($dumpBytes.Length -lt 4 -or $dumpBytes[0] -ne 0x4D -or $dumpBytes[1] -ne 0x44 -or
        $dumpBytes[2] -ne 0x4D -or $dumpBytes[3] -ne 0x50) {
        throw 'Crash harness output does not have the MDMP minidump signature.'
    }
    if ($dumpBytes.Length -lt 32) {
        throw 'Crash harness output is shorter than a minidump header.'
    }
    $streamCount = [BitConverter]::ToUInt32($dumpBytes, 8)
    $streamDirectoryRva = [BitConverter]::ToUInt32($dumpBytes, 12)
    $streamDirectoryEnd = [uint64]$streamDirectoryRva + ([uint64]$streamCount * 12)
    if ($streamCount -eq 0 -or $streamDirectoryRva -lt 32 -or $streamDirectoryEnd -gt $dumpBytes.LongLength) {
        throw 'Crash harness output has an invalid or empty minidump stream directory.'
    }

    $reports = @(Get-ChildItem -LiteralPath $crashTestDirectory -Filter 'RedXe-*.txt' -File)
    if ($reports.Count -ne 1) {
        throw "Crash harness created $($reports.Count) call-stack reports; expected exactly one."
    }
    $expectedReportPath = [IO.Path]::ChangeExtension($dumps[0].FullName, '.txt')
    if (-not [string]::Equals($reports[0].FullName, $expectedReportPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Call-stack report is not beside the minidump with the same basename: $($reports[0].FullName)"
    }
    $reportBytes = [IO.File]::ReadAllBytes($reports[0].FullName)
    if ($reportBytes.Length -lt 2 -or $reportBytes[0] -ne 0xFF -or $reportBytes[1] -ne 0xFE) {
        throw 'Call-stack report is not UTF-16 with a byte-order mark.'
    }
    $reportText = [IO.File]::ReadAllText($reports[0].FullName)
    $exceptionPattern = '(?m)^ExceptionCode=0x{0:X8}\r?$' -f $ExpectedExceptionCode
    if ($reportText -notmatch $exceptionPattern -or
        $reportText -notmatch "(?m)^ProcessId=$($crashProcess.Id)\r?$" -or
        $reportText -notmatch '(?m)^ThreadId=[1-9][0-9]*\r?$' -or
        $reportText -notmatch '(?m)^Callstack:\r?$' -or
        $reportText -notmatch '(?m)^00 0x[0-9A-F]{16} ' -or
        $reportText -notmatch '(?m)^FrameCount=([1-9]|[1-5][0-9]|6[0-4])\r?$') {
        throw 'Call-stack report is missing required crash metadata or bounded address frames.'
    }

    $markerPath = Join-Path $crashTestDirectory 'last_crash.txt'
    if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf)) {
        throw 'Crash harness did not create last_crash.txt.'
    }
    $markedDumpPath = [IO.File]::ReadAllText($markerPath).Trim()
    if (-not [string]::Equals([IO.Path]::GetFullPath($markedDumpPath), $dumps[0].FullName, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Crash marker does not identify the generated minidump: $markedDumpPath"
    }
    }
    finally {
        $resolvedCleanupTarget = [IO.Path]::GetFullPath($crashTestDirectory)
        if (-not $resolvedCleanupTarget.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove a crash-test directory outside the build root: $resolvedCleanupTarget"
        }
        if (Test-Path -LiteralPath $resolvedCleanupTarget) {
            Remove-Item -LiteralPath $resolvedCleanupTarget -Recurse -Force
        }
    }
}

$invalidOverrideProcess = Start-Process -WindowStyle Hidden -FilePath $executable `
    -ArgumentList @('--crash-test', '--crash-test-directory=relative-path-is-invalid') -Wait -PassThru
if ($invalidOverrideProcess.ExitCode -ne 2) {
    throw "Invalid crash-directory override returned exit code $($invalidOverrideProcess.ExitCode); expected 2."
}

Invoke-RedXeCrashTest -CrashArgument '--crash-test' -ExpectedExceptionCode 0xE000CAFEl `
    -Label 'fatal-process'
Invoke-RedXeCrashTest -CrashArgument '--crash-test-stack-overflow' -ExpectedExceptionCode 0xC00000FDl `
    -Label 'stack-overflow'

Write-Host 'All tests passed.' -ForegroundColor Green
exit 0
