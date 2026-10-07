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
.PARAMETER Suites
Selects complete component suites, by default every scope Tests/test-scopes.json names. Unknown names fail; explicit
scopes are partial coverage.
.PARAMETER SkipBuild
Uses existing pinned profile artifacts. Test-Changes.ps1 additionally verifies source/build attestation.
.PARAMETER Full
Runs the complete noninteractive gate; ordinary calls use Test-Changes affected iteration, or the full gate when there is
no Git work tree or comparison base to select from.
.PARAMETER SkipTooling
Leaves profile-independent tooling to the CI tooling job or selected BuildProcess scope.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'ASan Debug')]
    [string] $Configuration = 'Debug',

    [ValidateSet('x64', 'ARM64')]
    [string] $Platform = 'x64',

    [switch] $Rebuild,

    [switch] $SkipBuild,
    [switch] $Full,
    [switch] $SkipTooling,

    [string[]] $Suites,

    [ValidateRange(0, 65535)]
    [int] $BuildNumber = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSCommandPath
Import-Module (Join-Path $repoRoot 'Build/ScopedTesting.psm1') -Force
$manifest = Read-ScopedTestManifest $repoRoot
# Ordinary iteration uses source impact. Explicit suites and Full retain the lower-level runner surface.
if (-not $Full -and -not $PSBoundParameters.ContainsKey('Suites') -and
    @($PSBoundParameters.Keys | Where-Object { $_ -notin @('Platform','Configuration','SkipBuild') }).Count -eq 0) {
    $unselectable = Get-ScopedComparisonProblem $repoRoot ('origin/' + $manifest.defaultBranch)
    if (-not $unselectable) {
        & (Join-Path $PSScriptRoot 'Test-Changes.ps1') -Configuration $Configuration -Platform $Platform -SkipBuild:$SkipBuild
        exit $LASTEXITCODE
    }
    # Without a change set (a source archive, no origin, a shallow clone) nothing can be left out: run the full gate.
    Write-Host "Affected selection is unavailable, so every suite runs as with -Full. $unselectable" -ForegroundColor Yellow
}

# Tests/test-scopes.json names the suites; each runs only through its one block below.
$knownSuites = @($manifest.scopes | ForEach-Object { $_.name })
if (-not $PSBoundParameters.ContainsKey('Suites')) { $Suites = $knownSuites }
$Suites = @($Suites | ForEach-Object { $_ -split ',' })
foreach ($suite in $Suites) { if ($suite -notin $knownSuites) { throw "Unknown test suite '$suite'." } }
if ($SkipTooling) {$Suites=@($Suites | Where-Object {$_ -ne 'BuildProcess'})}

$nativeArchitecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
if ($Platform -eq 'ARM64' -and $nativeArchitecture -ne 'Arm64') {
    throw 'ARM64 runtime qualification requires a native ARM64 host; use build.ps1 for cross-compilation.'
}
# Resolve the build number once (explicit, else the commit count) so the build and the identity checks agree.
Import-Module (Join-Path $repoRoot 'Build/Versioning.psm1') -Force
$BuildNumber = Resolve-RedXeBuildNumber -RepoRoot $repoRoot -Requested $BuildNumber
$buildArguments = @{
    Configuration = $Configuration
    Platform = $Platform
    BuildNumber = $BuildNumber
}
if ($Rebuild) {
    $buildArguments.Rebuild = $true
}

if (-not $SkipBuild) {
    & (Join-Path $repoRoot 'build.ps1') @buildArguments
    if ($LASTEXITCODE -ne 0) { throw "Build entrypoint failed with exit code $LASTEXITCODE." }
}
Import-Module (Join-Path $repoRoot 'Build/DxUiProvenance.psm1') -Force
Import-Module (Join-Path $repoRoot 'Build/StreamingProcess.psm1') -Force
Assert-RedXeDxUiProvenance -OutputRoot (Join-Path $repoRoot ".build/$Platform/$Configuration") -LockFile (Join-Path $repoRoot 'Dependencies/DxUi.lock.json') -Platform $Platform -Configuration $Configuration

$executable = Join-Path $repoRoot ".build\$Platform\$Configuration\RedXe.exe"
if ($Platform -ne 'x64' -and $env:PROCESSOR_ARCHITECTURE -eq 'AMD64') {
    throw 'The ARM64 smoke test must run on ARM64 Windows. The build itself completed successfully.'
}

$expectedFileVersion = (Get-RedXeVersion -RepoRoot $repoRoot -BuildNumber $BuildNumber).FileVersion
$executableVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($executable)
if ($executableVersion.FileDescription -ne 'RedXe XENEON dashboard' -or
    $executableVersion.OriginalFilename -ne 'RedXe.exe' -or
    $executableVersion.ProductName -ne 'RedXe' -or
    $executableVersion.FileVersion -ne $expectedFileVersion) {
    throw "RedXe.exe is missing its stable Windows executable version identity (expected $expectedFileVersion, found $($executableVersion.FileVersion)). Rebuild, or pass the -BuildNumber it was built with."
}

# Every standalone test executable gets a wall-clock budget: a hung test then fails in minutes with its log,
# instead of the CI job's timeout cancelling the whole leg without a diagnosis. The longest suite finishes in
# a small fraction of this on the slowest CI runner.
$testTimeoutSeconds = 900
# Every process this script runs through Invoke-RedXeStreamingProcess is RedXe's own, and RedXe writes a redirected
# stderr as UTF-8: a test executable's or RedXe.exe --self-test's failure report and a failed self-test check
# (Common/FailureReports.h). Each run names that encoding for stderr; the runner's default, the console output code
# page, would garble non-ASCII text under a legacy one (Build_Process.md).
$utf8 = [Text.UTF8Encoding]::new($false)

if ('LauncherAlias' -in $Suites) {
# The winget command alias targets RedXeLauncher.exe: it must carry the same version stamp, need nothing but
# system DLLs, and hand --help through to RedXe.exe with its output and exit code.
$launcher = Join-Path $repoRoot ".build\$Platform\$Configuration\RedXeLauncher.exe"
$launcherVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($launcher)
if ($launcherVersion.OriginalFilename -ne 'RedXeLauncher.exe' -or $launcherVersion.ProductName -ne 'RedXe' -or
    $launcherVersion.FileVersion -ne $expectedFileVersion) {
    throw "RedXeLauncher.exe is missing its stable Windows executable version identity (expected $expectedFileVersion)."
}
Write-Host 'Running command alias launcher check...' -ForegroundColor Cyan
# Bounded and logged like every other test process; the job that bounds the launcher holds the RedXe.exe it starts too.
$launcherHelpLog = Join-Path $repoRoot ".build\$Platform\$Configuration\RedXeLauncher.help.log"
$launcherHelpExit = Invoke-RedXeStreamingProcess -FilePath $launcher -Arguments @('--help') -WorkingDirectory $repoRoot `
    -TimeoutSeconds 120 -LogPath $launcherHelpLog -StandardErrorEncoding $utf8 -OutputLineCallback { param([string] $Line, [bool] $IsError) }
if ($launcherHelpExit -ne 0 -or (Get-Content -LiteralPath $launcherHelpLog -Raw) -notmatch '--self-test') {
    throw "RedXeLauncher.exe --help exited with code $launcherHelpExit or did not relay the RedXe help text: $launcherHelpLog"
}
# An unknown switch alone, with no awaited mode beside it, is awaited too: its error and exit code 2 reach the caller.
$launcherUnknownLog = Join-Path $repoRoot ".build\$Platform\$Configuration\RedXeLauncher.unknown.log"
$launcherUnknownExit = Invoke-RedXeStreamingProcess -FilePath $launcher -Arguments @('--no-such-switch') -WorkingDirectory $repoRoot `
    -TimeoutSeconds 120 -LogPath $launcherUnknownLog -StandardErrorEncoding $utf8 -OutputLineCallback { param([string] $Line, [bool] $IsError) }
if ($launcherUnknownExit -ne 2 -or (Get-Content -LiteralPath $launcherUnknownLog -Raw) -notmatch 'Unknown argument "--no-such-switch"') {
    throw "RedXeLauncher.exe did not await and propagate the unknown-switch exit code 2 (got $launcherUnknownExit): $launcherUnknownLog"
}


}

if ('BuildProcess' -in $Suites) {
    & (Join-Path $repoRoot 'Tests/BuildProcessTests/Invoke-ToolingTests.ps1')
    if ($LASTEXITCODE) {throw 'Independent tooling tests failed.'}
}
if ('Packaging' -in $Suites) {
Write-Host 'Running packaging, versioning, winget manifest, and in-package installer tests...' -ForegroundColor Cyan
& (Join-Path $repoRoot 'Tests\BuildProcessTests\PackagingTests.ps1') -Configuration $Configuration -Platform $Platform
if ($LASTEXITCODE -ne 0) {
    throw "Packaging tests failed with exit code $LASTEXITCODE."
}


}

if ('PluginContract' -in $Suites) {
$contractTests = Join-Path $repoRoot ".build\$Platform\$Configuration\PluginContractTests.exe"
if ($Configuration -eq 'ASan Debug') {
    $probeLog = Join-Path $repoRoot ".build\logs\I19-ASan-probe-$Platform-$([guid]::NewGuid().ToString('N')).log"
    $previousOptions=$env:ASAN_OPTIONS
    try {
        $env:ASAN_OPTIONS='halt_on_error=1:abort_on_error=0:detect_leaks=0'
        # Bounded like every other test process; the sanitizer report is captured in the log for the check below.
        $probeExit = Invoke-RedXeStreamingProcess -FilePath $contractTests -Arguments @('--asan-probe') -WorkingDirectory $repoRoot `
            -TimeoutSeconds 120 -LogPath $probeLog -StandardErrorEncoding $utf8 -OutputLineCallback { param([string] $Line, [bool] $IsError) }
    } finally { $env:ASAN_OPTIONS=$previousOptions }
    if ($probeExit -eq 0 -or -not (Select-String -LiteralPath $probeLog -SimpleMatch 'AddressSanitizer: heap-use-after-free')) {
        throw "ASAN failed to diagnose the isolated deliberate defect: $probeLog"
    }
    Write-Host "PASS AddressSanitizer detection probe: $probeLog"
}
# A test process never waits on a dialog: a failed runtime check ends it with its report and exit code 3
# (Common/FailureReports.h). Debug and ASan Debug have such checks; Release has none and exits 0. Bounded like
# every other test process, so a routing that stopped working fails here, in at most two minutes, not in the suites.
# The report carries U+0141, as a source path under such a user name would, which must reach the log intact, and the
# report must go on past it. The run takes place in a background job, whose hidden console is its own, under console
# output code page 437, which has no such character: only the UTF-8 the run names for stderr keeps it there.
Write-Host 'Running test failure-report routing check...' -ForegroundColor Cyan
$failureReportLog = Join-Path $repoRoot ".build\logs\failure-report-$Platform-$($Configuration -replace ' ', '')-$([guid]::NewGuid().ToString('N')).log"
$failureReportJob = Start-Job -ScriptBlock {
    param([string] $ModulePath, [string] $FilePath, [string] $WorkingDirectory, [string] $LogPath)
    $ErrorActionPreference = 'Stop'
    Add-Type -Namespace RedXeRoutingCheck -Name LegacyConsole -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern uint GetConsoleProcessList(uint[] list, uint count);
[DllImport("kernel32.dll")] public static extern bool SetConsoleOutputCP(uint codePage);
'@
    # A console another process shares is never changed.
    if ([RedXeRoutingCheck.LegacyConsole]::GetConsoleProcessList([uint32[]]::new(2), 2) -ne 1 -or
        -not [RedXeRoutingCheck.LegacyConsole]::SetConsoleOutputCP(437)) {
        throw 'The failure-report routing check has no console of its own to set to code page 437.'
    }
    Import-Module $ModulePath -Force
    Invoke-RedXeStreamingProcess -FilePath $FilePath -Arguments @('--failure-report-self-test') -WorkingDirectory $WorkingDirectory `
        -TimeoutSeconds 120 -LogPath $LogPath -StandardErrorEncoding ([Text.UTF8Encoding]::new($false)) `
        -OutputLineCallback { param([string] $Line, [bool] $IsError) }
} -ArgumentList (Join-Path $repoRoot 'Build/StreamingProcess.psm1'), $contractTests, $repoRoot, $failureReportLog
# A stop (Ctrl+C) ends the job too, and with it the fixture's containment job and the fixture.
try { $failureReportExit = $failureReportJob | Receive-Job -Wait }
finally { $failureReportJob | Remove-Job -Force }
$failureReportText = [string] (Get-Content -LiteralPath $failureReportLog -Raw -Encoding UTF8)
if ($Configuration -eq 'Release') {
    if ($failureReportExit -ne 0) {
        throw "The Release failure-report self-test must exit 0, not $failureReportExit`: $failureReportLog"
    }
}
elseif ($failureReportExit -ne 3 -or
    -not $failureReportText.Contains("fails this check on purpose ($([char] 0x141)), and its report goes on") -or
    $failureReportText -match 'returned instead of ending') {
    throw "A failed runtime check must end the run with its whole report, U+0141 included, and exit code 3 (it exited $failureReportExit): $failureReportLog"
}
Write-Host "PASS test failure-report routing (exit $failureReportExit): $failureReportLog"
# An abort() that no failed check reported (an assert(), std::terminate after an unhandled exception) says so and ends
# the run with exit code 4 in every configuration, so exit code 3 keeps meaning a failed check. The fixture reports as
# the GUI-subsystem RedXe.exe does: a Debug assert() must reach the log first, not a message box.
$abortLog = Join-Path $repoRoot ".build\logs\abort-$Platform-$($Configuration -replace ' ', '')-$([guid]::NewGuid().ToString('N')).log"
$abortExit = Invoke-RedXeStreamingProcess -FilePath $contractTests -Arguments @('--abort-self-test') `
    -WorkingDirectory $repoRoot -TimeoutSeconds 120 -LogPath $abortLog -StandardErrorEncoding $utf8 -OutputLineCallback { param([string] $Line, [bool] $IsError) }
$abortText = Get-Content -LiteralPath $abortLog -Raw
if ($abortExit -ne 4 -or $abortText -notmatch 'abort\(\) was called' -or
    ($Configuration -ne 'Release' -and $abortText -notmatch 'fails this assert\(\) on purpose')) {
    throw "An abort() must say so and end the run with exit code 4 (it exited $abortExit): $abortLog"
}
Write-Host "PASS test abort routing (exit $abortExit): $abortLog"
# The same header leaves stdout unbuffered: a line written just before the process is terminated, as a budget ends a
# hung run, must be in the log, so the log names the case that hung.
$unbufferedLog = Join-Path $repoRoot ".build\logs\unbuffered-output-$Platform-$($Configuration -replace ' ', '')-$([guid]::NewGuid().ToString('N')).log"
$unbufferedExit = Invoke-RedXeStreamingProcess -FilePath $contractTests -Arguments @('--unbuffered-output-self-test') `
    -WorkingDirectory $repoRoot -TimeoutSeconds 120 -LogPath $unbufferedLog -StandardErrorEncoding $utf8 -OutputLineCallback { param([string] $Line, [bool] $IsError) }
if ($unbufferedExit -ne 0 -or (Get-Content -LiteralPath $unbufferedLog -Raw) -notmatch 'Unbuffered output reaches the log') {
    throw "A line written before the process was terminated is missing from its log (exit $unbufferedExit): $unbufferedLog"
}
Write-Host "PASS unbuffered test output: $unbufferedLog"
Write-Host 'Running plugin ABI and rendering-interface contract tests...' -ForegroundColor Cyan
$contractProcess = Invoke-RedXeStreamingProcess -FilePath $contractTests -WorkingDirectory $repoRoot -TimeoutSeconds $testTimeoutSeconds -LogPath ($contractTests + '.log') -StandardErrorEncoding $utf8
if ($contractProcess -ne 0) {
    throw "Plugin contract tests failed with exit code $($contractProcess)."
}


}

if ('AVControl' -in $Suites) {
$avControlTests = Join-Path $repoRoot ".build\$Platform\$Configuration\AVControlTests.exe"
Write-Host 'Running AV Control model, input and layout tests...' -ForegroundColor Cyan
$avControlProcess = Invoke-RedXeStreamingProcess -FilePath $avControlTests -WorkingDirectory $repoRoot -TimeoutSeconds $testTimeoutSeconds -LogPath ($avControlTests + '.log') -StandardErrorEncoding $utf8
if ($avControlProcess -ne 0) {
    throw "AV Control tests failed with exit code $($avControlProcess)."
}
# The suite's own stage watchdog must turn a stage that never returns into exit code 10 that names the stage; 3 and 4
# stay a failed runtime check and an abort (Common/FailureReports.h).
Write-Host 'Running AV Control stage-watchdog check...' -ForegroundColor Cyan
$watchdogLog = Join-Path $repoRoot ".build\$Platform\$Configuration\AVControlTests.watchdog.log"
# Bounded itself: if the watchdog ever failed to fire, this check must report that, not hang in its place.
$watchdogExit = Invoke-RedXeStreamingProcess -FilePath $avControlTests -Arguments @('--watchdog-fixture', '500') -WorkingDirectory $repoRoot `
    -TimeoutSeconds 60 -LogPath $watchdogLog -StandardErrorEncoding $utf8 -OutputLineCallback { param([string] $Line, [bool] $IsError) }
$watchdogText = Get-Content -LiteralPath $watchdogLog -Raw
if ($watchdogExit -ne 10 -or $watchdogText -notmatch "stage 'watchdog fixture' did not finish within") {
    throw "The AV Control stage watchdog did not end a hung stage (exit $watchdogExit): $watchdogText"
}
& (Join-Path $repoRoot 'Tests/AVControlTests/CameraPackageTests.ps1') -Configuration $Configuration -Platform $Platform


}

if ('SystemData' -in $Suites) {
$systemDataTests = Join-Path $repoRoot ".build\$Platform\$Configuration\SystemDataTests.exe"
Write-Host 'Running local system-data provider contract tests...' -ForegroundColor Cyan
$systemDataProcess = Invoke-RedXeStreamingProcess -FilePath $systemDataTests -WorkingDirectory $repoRoot -TimeoutSeconds $testTimeoutSeconds -LogPath ($systemDataTests + '.log') -StandardErrorEncoding $utf8
if ($systemDataProcess -ne 0) {
    throw "System-data provider tests failed with exit code $($systemDataProcess)."
}
if ($Configuration -eq 'Release' -and $Platform -eq 'x64') {
    Write-Host 'Running Release system-data row-cap resource measurement...' -ForegroundColor Cyan
    $systemDataBenchmark = Invoke-RedXeStreamingProcess -FilePath $systemDataTests -WorkingDirectory $repoRoot -Arguments @('--benchmark') -TimeoutSeconds $testTimeoutSeconds -LogPath ($systemDataTests + '-benchmark.log') -StandardErrorEncoding $utf8
    if ($systemDataBenchmark -ne 0) {
        throw "System-data row-cap measurement failed with exit code $($systemDataBenchmark)."
    }
    Write-Host 'Running Release system-data per-domain measurement...' -ForegroundColor Cyan
    $systemDataDomains = Invoke-RedXeStreamingProcess -FilePath $systemDataTests -WorkingDirectory $repoRoot -Arguments @('--domains') -TimeoutSeconds $testTimeoutSeconds -LogPath ($systemDataTests + '-domains.log') -StandardErrorEncoding $utf8
    if ($systemDataDomains -ne 0) {
        throw "System-data per-domain measurement failed with exit code $($systemDataDomains)."
    }
}


}

if ('SystemDataPhase0' -in $Suites) {
$systemDataPhase0 = Join-Path $repoRoot ".build\$Platform\$Configuration\SystemDataPhase0.exe"
Write-Host 'Running system-data Phase 0 acquisition spikes...' -ForegroundColor Cyan
$systemDataPhase0Process = Invoke-RedXeStreamingProcess -FilePath $systemDataPhase0 -WorkingDirectory $repoRoot -TimeoutSeconds $testTimeoutSeconds -LogPath ($systemDataPhase0 + '.log') -StandardErrorEncoding $utf8
if ($systemDataPhase0Process -ne 0) {
    throw "System-data Phase 0 spikes failed with exit code $($systemDataPhase0Process)."
}


}

if ('StudioClock' -in $Suites) {
$studioClockTests = Join-Path $repoRoot ".build\$Platform\$Configuration\StudioClockTests.exe"
Write-Host 'Running Studio Clock contract, scheduling, WARP, and resource tests...' -ForegroundColor Cyan
$studioClockProcess = Invoke-RedXeStreamingProcess -FilePath $studioClockTests -WorkingDirectory $repoRoot -TimeoutSeconds $testTimeoutSeconds -LogPath ($studioClockTests + '.log') -StandardErrorEncoding $utf8
if ($studioClockProcess -ne 0) {
    throw "Studio Clock tests failed with exit code $($studioClockProcess)."
}


}

if ('DeskClock' -in $Suites) {
$deskClockTests = Join-Path $repoRoot ".build\$Platform\$Configuration\DeskClockTests.exe"
Write-Host 'Running Desk Clock contract, scheduling, WARP, and resource tests...' -ForegroundColor Cyan
$deskClockProcess = Invoke-RedXeStreamingProcess -FilePath $deskClockTests -WorkingDirectory $repoRoot -TimeoutSeconds $testTimeoutSeconds -LogPath ($deskClockTests + '.log') -StandardErrorEncoding $utf8
if ($deskClockProcess -ne 0) {
    throw "Desk Clock tests failed with exit code $($deskClockProcess)."
}


}

if ('Launcher' -in $Suites) {
$launcherTests = Join-Path $repoRoot ".build\$Platform\$Configuration\LauncherTests.exe"
Write-Host 'Running Launcher factory, pin fallback, WARP, launch, and drop tests...' -ForegroundColor Cyan
$launcherProcess = Invoke-RedXeStreamingProcess -FilePath $launcherTests -WorkingDirectory $repoRoot -TimeoutSeconds $testTimeoutSeconds -LogPath ($launcherTests + '.log') -StandardErrorEncoding $utf8
if ($launcherProcess -ne 0) {
    throw "Launcher tests failed with exit code $($launcherProcess)."
}


}

if ('Weather' -in $Suites) {
$weatherTests = Join-Path $repoRoot ".build\$Platform\$Configuration\WeatherTests.exe"
Write-Host 'Running Weather HTTP, unit, and label format tests...' -ForegroundColor Cyan
$weatherProcess = Invoke-RedXeStreamingProcess -FilePath $weatherTests -WorkingDirectory $repoRoot -TimeoutSeconds $testTimeoutSeconds -LogPath ($weatherTests + '.log') -StandardErrorEncoding $utf8
if ($weatherProcess -ne 0) {
    throw "Weather tests failed with exit code $($weatherProcess)."
}


}

if ('Logicon' -in $Suites) {
$logiconTests = Join-Path $repoRoot ".build\$Platform\$Configuration\LogiconTests.exe"
Write-Host 'Running Logicon protocol, settings, face, device, and module tests...' -ForegroundColor Cyan
$logiconProcess = Invoke-RedXeStreamingProcess -FilePath $logiconTests -WorkingDirectory $repoRoot -TimeoutSeconds $testTimeoutSeconds -LogPath ($logiconTests + '.log') -StandardErrorEncoding $utf8
if ($logiconProcess -ne 0) {
    throw "Logicon tests failed with exit code $($logiconProcess)."
}


}

if ('Zoom' -in $Suites) {
$zoomTests = Join-Path $repoRoot ".build\$Platform\$Configuration\ZoomTests.exe"
Write-Host 'Running Zoom browser action, meeting URL, and module tests...' -ForegroundColor Cyan
$zoomProcess = Invoke-RedXeStreamingProcess -FilePath $zoomTests -WorkingDirectory $repoRoot -TimeoutSeconds $testTimeoutSeconds -LogPath ($zoomTests + '.log') -StandardErrorEncoding $utf8
if ($zoomProcess -ne 0) {
    throw "Zoom tests failed with exit code $($zoomProcess)."
}


}

if ('Settings' -in $Suites) {
$settingsTests = Join-Path $repoRoot ".build\$Platform\$Configuration\SettingsTests.exe"
Write-Host 'Running settings, schema, stamp, and watcher contract tests...' -ForegroundColor Cyan
$settingsProcess = Invoke-RedXeStreamingProcess -FilePath $settingsTests -WorkingDirectory $repoRoot -TimeoutSeconds $testTimeoutSeconds -LogPath ($settingsTests + '.log') -StandardErrorEncoding $utf8
if ($settingsProcess -ne 0) {
    throw "Settings tests failed with exit code $($settingsProcess)."
}


}

if ('HostPlugin' -in $Suites) {
$hostPluginTests = Join-Path $repoRoot ".build\$Platform\$Configuration\HostPluginTests.exe"
Write-Host 'Running production host and plugin integration tests...' -ForegroundColor Cyan
$hostPluginLog = Join-Path $repoRoot ".build\$Platform\$Configuration\HostPluginTests.log"
$hostPluginErrors = Join-Path $repoRoot ".build\$Platform\$Configuration\HostPluginTests.stderr.log"
# Both streams are captured (stderr also on its own, for the failure summary) under the same budget as the others.
# Nothing streams to the console, so the tails are shown for a run ended at its budget as well as for a failing one,
# once the stderr writer is closed.
$hostPluginErrorWriter = [IO.StreamWriter]::new($hostPluginErrors, $false, [Text.UTF8Encoding]::new($false))
$hostPluginFailure = $null
try {
    $hostPluginExit = Invoke-RedXeStreamingProcess -FilePath $hostPluginTests -WorkingDirectory $repoRoot -TimeoutSeconds $testTimeoutSeconds `
        -LogPath $hostPluginLog -StandardErrorEncoding $utf8 -OutputLineCallback {
            param([string] $Line, [bool] $IsError)
            if ($IsError) { $hostPluginErrorWriter.WriteLine($Line) }
        }
}
catch { $hostPluginFailure = $_ }
finally { $hostPluginErrorWriter.Dispose() }
if ($hostPluginFailure -or $hostPluginExit -ne 0) {
    Get-Content -LiteralPath $hostPluginLog -Tail 80
    Get-Content -LiteralPath $hostPluginErrors -Tail 40
    if ($hostPluginFailure) { throw $hostPluginFailure }
    throw "Host/plugin integration tests failed with exit code $hostPluginExit."
}
Write-Host "Host integration log: $hostPluginLog" -ForegroundColor DarkGray


}

if ('HostSmoke' -in $Suites) {
Write-Host 'Running hidden Direct3D 11 WARP smoke test...' -ForegroundColor Cyan
# Bounded and logged like every other test process. A failed self-test check names itself on stderr, so in this log,
# and exits 6. RedXe.exe routes a --self-test run through Common/FailureReports.h, so a failed Debug runtime check ends
# it with its report here and exit code 3, and another abort() with exit code 4, never a dialog.
# The self-test's window stays hidden: its first ShowWindow is SW_HIDE, whatever the start information says.
$smokeLog = Join-Path $repoRoot ".build\$Platform\$Configuration\RedXe.self-test.log"
# Nothing streams to the console, so the tail is shown for a run ended at its budget as well as for a failing one.
$smokeFailure = $null
try {
    $smokeExit = Invoke-RedXeStreamingProcess -FilePath $executable -Arguments @('--self-test', '--warp') -WorkingDirectory $repoRoot `
        -TimeoutSeconds $testTimeoutSeconds -LogPath $smokeLog -StandardErrorEncoding $utf8 -OutputLineCallback { param([string] $Line, [bool] $IsError) }
}
catch { $smokeFailure = $_ }
if ($smokeFailure -or $smokeExit -ne 0) {
    Get-Content -LiteralPath $smokeLog -Tail 40
    if ($smokeFailure) { throw $smokeFailure }
    throw "Smoke test failed with exit code $smokeExit`: $smokeLog"
}
# Every failing check reports through that one path: each return of Application::RunSelfTest other than its success
# returns FailSelfTest, and none writes only to the debugger output, which an unattended run never sees.
$applicationSource = [IO.File]::ReadAllText((Join-Path $repoRoot 'RedXe\Application.cpp'))
$selfTestStart = $applicationSource.IndexOf('int Application::RunSelfTest(')
$selfTestBody = ''
if ($selfTestStart -ge 0) {
    $depth = 0
    for ($index = $applicationSource.IndexOf('{', $selfTestStart); $index -ge 0 -and $index -lt $applicationSource.Length; $index++) {
        if ($applicationSource[$index] -eq '{') { $depth++ }
        elseif ($applicationSource[$index] -eq '}') {
            $depth--
            if ($depth -eq 0) {
                $selfTestBody = $applicationSource.Substring($selfTestStart, $index - $selfTestStart + 1)
                break
            }
        }
    }
}
$bypassingReturns = @([regex]::Matches($selfTestBody, '\breturn\b\s*([^;]*);') |
    Where-Object { $_.Groups[1].Value -cne '0' -and -not $_.Groups[1].Value.StartsWith('FailSelfTest(') })
if (-not $selfTestBody -or $bypassingReturns.Count -ne 0 -or $selfTestBody.Contains('OutputDebugStringW')) {
    throw "Every failing check of Application::RunSelfTest must report through FailSelfTest: $(($bypassingReturns | ForEach-Object Value) -join ' | ')"
}
# The same diagnosis for a failing check, proven in the product executable: a copy of RedXe.exe with no Settings folder
# beside it fails its settings check, which must be named in its log, with exit code 6.
$selfTestFailureDirectory = Join-Path $repoRoot ".build\SelfTestFailure\$([guid]::NewGuid().ToString('N'))"
$selfTestFailureLog = Join-Path $repoRoot ".build\$Platform\$Configuration\RedXe.self-test-failure.log"
[void](New-Item -ItemType Directory -Path $selfTestFailureDirectory -Force)
$selfTestFailureRun = $null
try {
    Copy-Item -LiteralPath $executable -Destination $selfTestFailureDirectory
    Get-ChildItem -LiteralPath (Split-Path -Parent $executable) -Filter '*.dll' -File |
        Copy-Item -Destination $selfTestFailureDirectory
    # As for the smoke run, the tail is shown for a run ended at its budget as well as for a wrong result.
    try {
        $selfTestFailureExit = Invoke-RedXeStreamingProcess -FilePath (Join-Path $selfTestFailureDirectory 'RedXe.exe') `
            -Arguments @('--self-test', '--warp') -WorkingDirectory $selfTestFailureDirectory -TimeoutSeconds 120 `
            -LogPath $selfTestFailureLog -StandardErrorEncoding $utf8 -OutputLineCallback { param([string] $Line, [bool] $IsError) }
    }
    catch { $selfTestFailureRun = $_ }
}
finally {
    Remove-Item -LiteralPath $selfTestFailureDirectory -Recurse -Force -ErrorAction SilentlyContinue
}
if ($selfTestFailureRun -or $selfTestFailureExit -ne 6 -or
    (Get-Content -LiteralPath $selfTestFailureLog -Raw) -notmatch 'Settings initialization or validation failed\. HRESULT 0x8') {
    Get-Content -LiteralPath $selfTestFailureLog -Tail 40
    if ($selfTestFailureRun) { throw $selfTestFailureRun }
    throw "A failed self-test check must name itself in the log and exit with code 6 (it exited $selfTestFailureExit): $selfTestFailureLog"
}

# `--help` writes the RedXe/CommandLine.h catalog to a redirected stdout and exits 0; every switch the catalog
# declares must appear, so a switch added without a catalog entry fails here as well as in SettingsTests. Each run of
# RedXe.exe below is bounded and logged like every other test process.
Write-Host 'Running command-line help check...' -ForegroundColor Cyan
$helpLog = Join-Path $repoRoot ".build\$Platform\$Configuration\RedXe.help.log"
$helpExit = Invoke-RedXeStreamingProcess -FilePath $executable -Arguments @('--help') -WorkingDirectory $repoRoot `
    -TimeoutSeconds 120 -LogPath $helpLog -StandardErrorEncoding $utf8 -OutputLineCallback { param([string] $Line, [bool] $IsError) }
if ($helpExit -ne 0) {
    throw "RedXe.exe --help exited with code $helpExit`: $helpLog"
}
$helpText = Get-Content -LiteralPath $helpLog -Raw -Encoding UTF8
foreach ($switch in @('--help', '--settings', '--warp', '--dock', '--dock-mode', '--dock-thickness', '--dock-reserve',
        '--dock-peek', '--screenshot', '--page', '--widget', '--after', '--self-test', '--crash-test',
        '--crash-test-stack-overflow', '--crash-test-directory', 'Exit codes:')) {
    if ($helpText -notmatch [regex]::Escape($switch)) {
        throw "RedXe.exe --help does not mention $switch."
    }
}
$unknownLog = Join-Path $repoRoot ".build\$Platform\$Configuration\RedXe.unknown-switch.log"
$unknownExit = Invoke-RedXeStreamingProcess -FilePath $executable -Arguments @('--self-test', '--warp', '--no-such-switch') `
    -WorkingDirectory $repoRoot -TimeoutSeconds 120 -LogPath $unknownLog -StandardErrorEncoding $utf8 -OutputLineCallback { param([string] $Line, [bool] $IsError) }
if ($unknownExit -ne 2 -or (Get-Content -LiteralPath $unknownLog -Raw) -notmatch 'Unknown argument "--no-such-switch"') {
    throw "An unknown switch exited with code $unknownExit instead of 2, or its log does not name it: $unknownLog"
}
# An unattended run (--self-test, --screenshot) never waits on a message box: a switch with a missing or invalid value
# exits 2 with its usage on the redirected output before any window exists. Bounded, so a box fails here, never hangs.
Write-Host 'Running unattended command-line error check...' -ForegroundColor Cyan
$unattendedLog = Join-Path $repoRoot ".build\$Platform\$Configuration\RedXe.unattended-error.log"
$unattendedPng = Join-Path $repoRoot ".build\$Platform\$Configuration\RedXe.unattended-error.png"
foreach ($case in @(
        @{ Arguments = @('--self-test', '--warp', '--settings'); Usage = 'Use --settings followed by exactly one settings file path.' },
        @{ Arguments = @('--screenshot', $unattendedPng, '--after', '0'); Usage = '--after takes a delay of 1 through 120000 milliseconds.' })) {
    $unattendedExit = Invoke-RedXeStreamingProcess -FilePath $executable -Arguments $case.Arguments -WorkingDirectory $repoRoot `
        -TimeoutSeconds 60 -LogPath $unattendedLog -OutputLineCallback { param([string] $Line, [bool] $IsError) }
    if ($unattendedExit -ne 2 -or -not (Select-String -LiteralPath $unattendedLog -SimpleMatch $case.Usage)) {
        throw "RedXe.exe $($case.Arguments -join ' ') exited with code $unattendedExit without its usage on the redirected output: $unattendedLog"
    }
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
        # Bounded and logged like every other test process. The bounding job also keeps Windows Error Reporting's
        # dialog away, so a crash path that stopped handling its exception ends the run instead of holding it.
        $crashLog = Join-Path $repoRoot ".build\$Platform\$Configuration\RedXe.crash-test-$Label.log"
        $crashProcessId = 0
        $crashExit = Invoke-RedXeStreamingProcess -FilePath $executable `
            -Arguments @($CrashArgument, "--crash-test-directory=$crashTestDirectory") -WorkingDirectory $repoRoot `
            -TimeoutSeconds $testTimeoutSeconds -LogPath $crashLog -StandardErrorEncoding $utf8 -ProcessId ([ref] $crashProcessId) `
            -OutputLineCallback { param([string] $Line, [bool] $IsError) }
    if ($crashExit -ne 127) {
        throw "Crash harness returned exit code $crashExit; expected 127: $crashLog"
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
        $reportText -notmatch "(?m)^ProcessId=$crashProcessId\r?$" -or
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

$invalidOverrideLog = Join-Path $repoRoot ".build\$Platform\$Configuration\RedXe.crash-test-invalid-directory.log"
$invalidOverrideExit = Invoke-RedXeStreamingProcess -FilePath $executable `
    -Arguments @('--crash-test', '--crash-test-directory=relative-path-is-invalid') -WorkingDirectory $repoRoot `
    -TimeoutSeconds 120 -LogPath $invalidOverrideLog -StandardErrorEncoding $utf8 -OutputLineCallback { param([string] $Line, [bool] $IsError) }
if ($invalidOverrideExit -ne 2) {
    throw "Invalid crash-directory override returned exit code $invalidOverrideExit; expected 2: $invalidOverrideLog"
}

Invoke-RedXeCrashTest -CrashArgument '--crash-test' -ExpectedExceptionCode 0xE000CAFEl `
    -Label 'fatal-process'
Invoke-RedXeCrashTest -CrashArgument '--crash-test-stack-overflow' -ExpectedExceptionCode 0xC00000FDl `
    -Label 'stack-overflow'


}
Write-Host "All $($Suites.Count) requested scopes passed." -ForegroundColor Green
exit 0
