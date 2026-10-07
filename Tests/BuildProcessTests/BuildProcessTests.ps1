[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$modulePath = Join-Path $repoRoot 'Build\BuildOutputProcess.psm1'
$presentationModulePath = Join-Path $repoRoot 'Build\BuildPresentation.psm1'
Import-Module $modulePath -Force -ErrorAction Stop
Import-Module $presentationModulePath -Force -ErrorAction Stop

$buildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '.build'))
$testParent = [IO.Path]::GetFullPath((Join-Path $buildRoot 'BuildProcessTests'))
$testRoot = [IO.Path]::GetFullPath((Join-Path $testParent ([guid]::NewGuid().ToString('N'))))
$expectedPrefix = $testParent.TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $testRoot.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to create a build-process test directory outside '$testParent': $testRoot"
}

$targetProcess = $null
$foreignProcess = $null
try {
    $targetDirectory = Join-Path $testRoot 'Target'
    $foreignDirectory = Join-Path $testRoot 'Foreign'
    [void](New-Item -ItemType Directory -Path $targetDirectory -Force)
    [void](New-Item -ItemType Directory -Path $foreignDirectory -Force)

    $targetExecutable = Join-Path $targetDirectory 'RedXe.exe'
    $foreignExecutable = Join-Path $foreignDirectory 'RedXe.exe'
    Copy-Item -LiteralPath $env:ComSpec -Destination $targetExecutable
    Copy-Item -LiteralPath $env:ComSpec -Destination $foreignExecutable

    $arguments = @('/d', '/c', 'ping.exe -n 30 127.0.0.1 > nul')
    $targetProcess = Start-Process -FilePath $targetExecutable -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $foreignProcess = Start-Process -FilePath $foreignExecutable -ArgumentList $arguments -WindowStyle Hidden -PassThru

    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $visible = @(Get-CimInstance Win32_Process -Filter "Name='RedXe.exe'" -ErrorAction Stop)
        $visibleIds = @($visible | ForEach-Object { [uint32] $_.ProcessId })
        if ($visibleIds -contains [uint32] $targetProcess.Id -and
            $visibleIds -contains [uint32] $foreignProcess.Id) {
            break
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)

    if ($visibleIds -notcontains [uint32] $targetProcess.Id -or
        $visibleIds -notcontains [uint32] $foreignProcess.Id) {
        throw 'The build-process fixture executables did not become observable through Win32_Process.'
    }

    $errorMessage = $null
    try {
        Assert-BuildOutputProcessNotRunning `
            -ProcessName 'RedXe.exe' `
            -ExpectedExecutablePath $targetExecutable
    }
    catch {
        $errorMessage = $_.Exception.Message
    }
    if (-not $errorMessage) {
        throw 'The exact target-output fixture process did not block the build preflight.'
    }
    if ($errorMessage -notmatch [regex]::Escape("PID=$($targetProcess.Id)")) {
        throw "The build preflight diagnostic omitted the exact target PID: $errorMessage"
    }
    if ($errorMessage -notmatch [regex]::Escape([IO.Path]::GetFullPath($targetExecutable))) {
        throw "The build preflight diagnostic omitted the exact target path: $errorMessage"
    }
    if ($errorMessage -match [regex]::Escape("PID=$($foreignProcess.Id)")) {
        throw "The build preflight diagnostic incorrectly included the foreign PID: $errorMessage"
    }
    if ($errorMessage -notmatch "CommandLine='" -or
        $errorMessage -notmatch 'was not terminated because it is not proven to belong') {
        throw "The build preflight diagnostic did not explain the preserved process: $errorMessage"
    }
    if (-not (Get-Process -Id $targetProcess.Id -ErrorAction SilentlyContinue)) {
        throw 'The exact target-output fixture process was incorrectly stopped.'
    }
    if (-not (Get-Process -Id $foreignProcess.Id -ErrorAction SilentlyContinue)) {
        throw 'The foreign same-name fixture process was incorrectly stopped.'
    }
}
finally {
    foreach ($ownedProcess in @($targetProcess, $foreignProcess)) {
        if ($ownedProcess -and (Get-Process -Id $ownedProcess.Id -ErrorAction SilentlyContinue)) {
            Stop-Process -Id $ownedProcess.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $ownedProcess.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
    }

    $resolvedCleanupTarget = [IO.Path]::GetFullPath($testRoot)
    if (-not $resolvedCleanupTarget.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a build-process test directory outside '$testParent': $resolvedCleanupTarget"
    }
    for ($attempt = 0; $attempt -lt 20 -and (Test-Path -LiteralPath $resolvedCleanupTarget); ++$attempt) {
        try {
            Remove-Item -LiteralPath $resolvedCleanupTarget -Recurse -Force -ErrorAction Stop
        }
        catch {
            if ($attempt -eq 19) {
                throw
            }
            Start-Sleep -Milliseconds 100
        }
    }
}

Write-Host 'Build-output process preflight tests passed.' -ForegroundColor Green

$presentationTestRoot = [IO.Path]::GetFullPath((Join-Path $testParent ([guid]::NewGuid().ToString('N'))))
if (-not $presentationTestRoot.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to create a build-presentation test directory outside '$testParent': $presentationTestRoot"
}

# The processes of a fixture run: a cmd.exe whose command line carries one of the markers, and every descendant of one
# (ping.exe carries none). With -WaitSeconds the scan is repeated until they are gone, for a termination nothing waited on.
function Get-FixtureSurvivors {
    param(
        [Parameter(Mandatory)]
        [string[]] $Markers,

        [int] $WaitSeconds = 0
    )

    $scanClock = [Diagnostics.Stopwatch]::StartNew()
    while ($true) {
        $candidates = @(Get-CimInstance Win32_Process -Filter "Name='cmd.exe' OR Name='ping.exe'" -ErrorAction Stop)
        $runProcessIds = [Collections.Generic.HashSet[uint32]]::new()
        foreach ($candidate in $candidates) {
            foreach ($marker in $Markers) {
                if ($candidate.CommandLine -and $candidate.CommandLine.Contains($marker)) {
                    [void] $runProcessIds.Add($candidate.ProcessId)
                }
            }
        }
        do {
            $foundDescendant = $false
            foreach ($candidate in $candidates) {
                if ($runProcessIds.Contains($candidate.ParentProcessId) -and $runProcessIds.Add($candidate.ProcessId)) {
                    $foundDescendant = $true
                }
            }
        } while ($foundDescendant)
        $survivors = @($candidates | Where-Object { $runProcessIds.Contains($_.ProcessId) })
        if ($survivors.Count -eq 0 -or $scanClock.Elapsed.TotalSeconds -ge $WaitSeconds) {
            return $survivors
        }
        Start-Sleep -Milliseconds 200
    }
}

try {
    [void](New-Item -ItemType Directory -Path $presentationTestRoot -Force)

    $bannerText = @(& { Write-RedXeBuildBanner -UseColor $false } 6>&1 | ForEach-Object { $_.ToString() }) -join "`n"
    $fullBlock = [char]0x2588
    $boxTopLeft = [char]0x2554
    $blockCount = @($bannerText.ToCharArray() | Where-Object { $_ -eq $fullBlock }).Count
    if ($blockCount -lt 50 -or
        $bannerText.IndexOf($boxTopLeft) -lt 0 -or
        $bannerText -notmatch 'XENEON EDGE // BUILD SIGNAL LOCKED') {
        throw "The RedXe banner lost its framed product mark or build-signal signature: $bannerText"
    }

    $redirectedInteractive = Test-RedXeInteractiveTerminal `
        -IsOutputRedirected $true `
        -IsErrorRedirected $true `
        -HasRawUi $true `
        -CanReadWindowTitle $true `
        -Environment @{ CI = 'true' }
    if ($redirectedInteractive) {
        throw 'A CI-style redirected host was incorrectly treated as an interactive terminal.'
    }

    $planLogPath = Join-Path $presentationTestRoot 'direct-msbuild.log'
    $codexPlan = Get-RedXeBuildInvocationPlan `
        -UseInteractiveTerminal $true `
        -LogPath $planLogPath `
        -Environment @{ CODEX_SHELL = '1' }
    if ($codexPlan.UseDirectConsole -or @($codexPlan.AdditionalArguments).Count -ne 0) {
        throw 'Codex must use replay streaming without adding the MSBuild file logger.'
    }

    $windowsTerminalPlan = Get-RedXeBuildInvocationPlan `
        -UseInteractiveTerminal $true `
        -LogPath $planLogPath `
        -Environment @{ WT_SESSION = '1' }
    if ($windowsTerminalPlan.UseDirectConsole) {
        throw 'Windows Terminal must use replay streaming so captured progress remains visible.'
    }

    $plainConsolePlan = Get-RedXeBuildInvocationPlan `
        -UseInteractiveTerminal $true `
        -LogPath $planLogPath `
        -Environment @{ PLAIN_CONSOLE = '1' }
    $expectedLoggerArgument = "/flp:Verbosity=minimal;LogFile=$([IO.Path]::GetFullPath($planLogPath));Encoding=UTF-8"
    if (-not $plainConsolePlan.UseDirectConsole -or
        @($plainConsolePlan.AdditionalArguments).Count -ne 2 -or
        $plainConsolePlan.AdditionalArguments[0] -ne '/fl' -or
        $plainConsolePlan.AdditionalArguments[1] -ne $expectedLoggerArgument) {
        throw 'A plain interactive console must retain direct MSBuild output plus the UTF-8 file logger.'
    }

    $colorCases = @(
        @{ Line = 'MSBUILD : error MSB1009: Project file does not exist.'; IsError = $false; Expected = 'Red' },
        @{ Line = 'file.cpp(10,5): warning C4100: unreferenced parameter'; IsError = $false; Expected = 'Yellow' },
        @{ Line = 'tool wrote to stderr'; IsError = $true; Expected = 'Red' },
        @{ Line = '  RedXe.vcxproj -> Z:\src\RedXe\.build\x64\Debug\RedXe.exe'; IsError = $false; Expected = 'Green' },
        @{ Line = 'Build succeeded.'; IsError = $false; Expected = 'Green' },
        @{ Line = '  Renderer.cpp'; IsError = $false; Expected = $null }
    )
    foreach ($case in $colorCases) {
        $actual = Get-RedXeBuildLineForegroundColor -Line $case.Line -IsError $case.IsError
        if ($actual -ne $case.Expected) {
            throw "Unexpected color '$actual' for '$($case.Line)'; expected '$($case.Expected)'."
        }
    }

    $dxUiAdvisoryColorCases = @(
        @{ Notice = 'DxUi main b129956d01b7 has no successful completed validation; keep pinned 13788e95f1c9.'; Expected = 'Red' },
        @{ Notice = 'DxUi update available: pinned 13788e95f1c9, available b129956d01b7.'; Expected = 'Yellow' },
        @{ Notice = 'DxUi update check unavailable: GitHub API rate limit exceeded.'; Expected = 'DarkYellow' },
        @{ Notice = ''; Expected = $null }
    )
    foreach ($case in $dxUiAdvisoryColorCases) {
        $actual = Get-RedXeDxUiUpdateNoticeForegroundColor -Notice $case.Notice
        if ($actual -ne $case.Expected) {
            throw "Unexpected DxUi advisory color '$actual' for '$($case.Notice)'; expected '$($case.Expected)'."
        }
    }

    $formattedDxUiAdvisory = Format-RedXeDxUiUpdateNotice -Notice (
        'DxUi update available: pinned 13788e95f1c9, available b129956d01b7. ' +
        'https://github.com/RedSalamanders/DxUi/compare/test . Update Z:\fixture\Dependencies\DxUi.lock.json ' +
        'on a branch and run the product regressions.')
    if ($formattedDxUiAdvisory -notmatch [regex]::Escape("`nTo upgrade and run local validation: .\Update-DxUi.ps1") -or
        $formattedDxUiAdvisory -notmatch [regex]::Escape('To upgrade only after equivalent product validation passed elsewhere: .\Update-DxUi.ps1 -UpdateOnly')) {
        throw "DxUi update advisory omitted a RedXe upgrade instruction: $formattedDxUiAdvisory"
    }

    $diagnosticLogPath = Join-Path $presentationTestRoot 'diagnostics.log'
    @'
Z:\src\RedXe\Renderer.cpp(10,5): warning C4100: unreferenced parameter [Z:\src\RedXe\RedXe\RedXe.vcxproj]
MSBUILD : error MSB1009: Project file does not exist.
link : fatal error LNK1120: 1 unresolved externals
  0 Warning(s)
  0 Error(s)
'@ | Set-Content -LiteralPath $diagnosticLogPath -Encoding UTF8
    $diagnosticSummary = Get-RedXeBuildDiagnosticSummary -LogPath $diagnosticLogPath
    if ($diagnosticSummary.WarningCount -ne 1 -or $diagnosticSummary.ErrorCount -ne 2) {
        throw ("Diagnostic summary mismatch: expected 1 warning and 2 errors; got {0} and {1}." -f `
            $diagnosticSummary.WarningCount, $diagnosticSummary.ErrorCount)
    }

    $emitterPath = Join-Path $presentationTestRoot 'Emit Build Output.ps1'
    @'
param([string] $Message)
Write-Output "stdout:$Message"
Write-Output ('stdout:caf' + [char] 0xE9)
[Console]::Error.WriteLine("stderr:$Message")
Write-Output "pid:$PID"
exit 17
'@ | Set-Content -LiteralPath $emitterPath -Encoding UTF8
    $powershellPath = (Get-Process -Id $PID -ErrorAction Stop).Path
    # An unbounded run uses Process.Start and a bounded one the job's own start, so both must quote arguments, keep
    # stream identity, propagate the exit code, report the child's process identifier, and decode the same bytes into
    # the same text.
    $decodedOutput = @{}
    foreach ($streamBudget in @(0, 120)) {
        $streamLogPath = Join-Path $presentationTestRoot "streamed-$streamBudget.log"
        $receivedLines = [Collections.Generic.List[psobject]]::new()
        $streamProcessId = 0
        $streamExitCode = Invoke-RedXeStreamingProcess `
            -FilePath $powershellPath `
            -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $emitterPath, 'argument with spaces') `
            -WorkingDirectory $presentationTestRoot `
            -LogPath $streamLogPath `
            -TimeoutSeconds $streamBudget `
            -ProcessId ([ref] $streamProcessId) `
            -OutputLineCallback {
            param([string] $Line, [bool] $IsError)
            [void] $receivedLines.Add([pscustomobject]@{ Line = $Line; IsError = $IsError })
        }

        if ($streamExitCode -ne 17 -or $LASTEXITCODE -ne 17) {
            throw "Streaming with a $streamBudget s budget did not propagate the child exit code; expected 17, got $streamExitCode."
        }
        if ($streamProcessId -le 0 -or -not ($receivedLines | Where-Object { $_.Line -eq "pid:$streamProcessId" })) {
            throw "Streaming with a $streamBudget s budget did not report the child's process identifier (got $streamProcessId): $($receivedLines | Out-String)"
        }
        $stdoutRecord = $receivedLines | Where-Object { $_.Line -eq 'stdout:argument with spaces' -and -not $_.IsError }
        $stderrRecord = $receivedLines | Where-Object { $_.Line -eq 'stderr:argument with spaces' -and $_.IsError }
        if (-not $stdoutRecord -or -not $stderrRecord) {
            throw "Streaming with a $streamBudget s budget did not preserve argument quoting and stream identity: $($receivedLines | Out-String)"
        }
        $streamLogText = Get-Content -LiteralPath $streamLogPath -Raw
        if ($streamLogText -notmatch 'stdout:argument with spaces' -or
            $streamLogText -notmatch 'stderr:argument with spaces' -or
            $streamLogText.Contains([char] 0x1b)) {
            throw "The captured streaming log with a $streamBudget s budget omitted output or contained terminal control sequences."
        }
        # Each run has its own process identifier, so that line is left out of the comparison.
        $decodedOutput[$streamBudget] = @($receivedLines | Where-Object { -not $_.Line.StartsWith('pid:') } |
            ForEach-Object { '{0}|{1}' -f [int] $_.IsError, $_.Line } | Sort-Object) -join "`n"
    }
    if ($decodedOutput[0] -cne $decodedOutput[120]) {
        throw "A bounded run decoded the child's output differently from Process.Start: '$($decodedOutput[120])' instead of '$($decodedOutput[0])'."
    }

    # A bounded child's job ends a process on an unhandled exception instead of leaving it in a Windows Error Reporting
    # dialog until the budget: the child reads the limit flags of its own job (JOBOBJECT_BASIC_LIMIT_INFORMATION's
    # LimitFlags sits at byte 16 on 64-bit Windows) and must find JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION.
    $jobProbePath = Join-Path $presentationTestRoot 'job-probe.ps1'
    @'
Add-Type -Namespace RedXeJobProbe -Name Native -MemberDefinition '[DllImport("kernel32.dll")] public static extern bool QueryInformationJobObject(IntPtr job, int infoClass, byte[] info, int size, IntPtr returnLength);'
$limits = [byte[]]::new(64)
$queried = [RedXeJobProbe.Native]::QueryInformationJobObject([IntPtr]::Zero, 2, $limits, $limits.Length, [IntPtr]::Zero)
'job:{0}:{1}' -f $queried, [BitConverter]::ToUInt32($limits, 16)
'@ | Set-Content -LiteralPath $jobProbePath -Encoding UTF8
    $jobProbeLines = [Collections.Generic.List[string]]::new()
    [void](Invoke-RedXeStreamingProcess -FilePath $powershellPath `
        -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $jobProbePath) `
        -WorkingDirectory $presentationTestRoot -LogPath (Join-Path $presentationTestRoot 'job-probe.log') -TimeoutSeconds 120 `
        -OutputLineCallback { param([string] $Line, [bool] $IsError) [void] $jobProbeLines.Add($Line) })
    $jobProbeRecord = @($jobProbeLines | Where-Object { $_ -match '^job:True:(\d+)$' })
    if ($jobProbeRecord.Count -ne 1 -or -not ([uint32] ($jobProbeRecord[0] -replace '^job:True:', '') -band 0x400)) {
        throw "A bounded child's job does not end a process on an unhandled exception: $($jobProbeLines -join ' | ')"
    }

    # A session cannot unload a compiled type, so the launcher's type follows its source: an edited copy of the module,
    # imported after the bounded run above compiled the original, runs its own code. A runspace of its own keeps the
    # edited functions out of this one; compiled types are shared by the whole process.
    $editedModulePath = Join-Path $presentationTestRoot 'BuildPresentation.psm1'
    $presentationSource = Get-Content -LiteralPath $presentationModulePath -Raw
    $editedSource = $presentationSource.Replace('"Unable to start ''" + fileName', '"Edited launcher could not start ''" + fileName')
    if ($editedSource -ceq $presentationSource) {
        throw 'The launcher type check found no start failure message to edit.'
    }
    Set-Content -LiteralPath $editedModulePath -Value $editedSource -Encoding UTF8 -NoNewline
    $editedShell = [powershell]::Create()
    try {
        [void] $editedShell.AddScript({
            param([string] $ModulePath, [string] $MissingPath, [string] $LogPath)
            Import-Module $ModulePath -Force
            try {
                [void](Invoke-RedXeStreamingProcess -FilePath $MissingPath -LogPath $LogPath -TimeoutSeconds 30)
                'The missing executable started.'
            }
            catch {
                $_.Exception.Message
            }
        }.ToString()).AddArgument($editedModulePath).AddArgument((Join-Path $presentationTestRoot 'missing.exe')).AddArgument(
            (Join-Path $presentationTestRoot 'edited.log'))
        $editedMessage = -join @($editedShell.Invoke())
    }
    finally {
        $editedShell.Dispose()
    }
    if ($editedMessage -notmatch 'Edited launcher could not start') {
        throw "A session that had compiled the launcher ran its earlier type for an edited definition: $editedMessage"
    }

    # A bounded child that never stops writing is still ended at its budget: the deadline is checked on every line,
    # so reads that always have a line ready cannot keep the run going. The child keeps the pipe full of short lines;
    # a wait that ended only when no line was ready would let it write for minutes.
    $spewerPath = Join-Path $presentationTestRoot 'spewer.cmd'
    Set-Content -LiteralPath (Join-Path $presentationTestRoot 'spewer.txt') -Value (@('x') * 10000) -Encoding ASCII
    @'
@echo off
for /l %%i in (1,1,120) do type "%~dp0spewer.txt"
'@ | Set-Content -LiteralPath $spewerPath -Encoding ASCII
    $spewLogPath = Join-Path $presentationTestRoot 'spewed.log'
    $spewBudget = 3
    $spewMessage = $null
    $spewStopwatch = [Diagnostics.Stopwatch]::StartNew()
    try {
        [void](Invoke-RedXeStreamingProcess `
            -FilePath $env:ComSpec `
            -Arguments @('/d', '/c', $spewerPath) `
            -WorkingDirectory $presentationTestRoot `
            -LogPath $spewLogPath `
            -TimeoutSeconds $spewBudget `
            -OutputLineCallback { param([string] $Line, [bool] $IsError) })
    }
    catch {
        $spewMessage = $_.Exception.Message
    }
    $spewStopwatch.Stop()
    if (-not $spewMessage -or $spewMessage -notmatch "did not finish within $spewBudget s and was terminated" -or
        $spewMessage -notmatch [regex]::Escape($spewLogPath)) {
        throw "A bounded child that kept writing was not terminated at its budget: '$spewMessage' after $($spewStopwatch.Elapsed.TotalSeconds) s."
    }
    if ($spewStopwatch.Elapsed.TotalSeconds -gt $spewBudget + 7) {
        throw "Terminating a bounded child that kept writing took $($spewStopwatch.Elapsed.TotalSeconds) s."
    }
    $spewLogText = Get-Content -LiteralPath $spewLogPath -Raw
    if (-not $spewLogText.StartsWith("x`r`nx`r`n") -or $spewLogText -notmatch 'TIMEOUT:') {
        throw 'The log of a bounded child that kept writing lacks its output or the timeout record.'
    }

    # Ctrl+C stops a run within a wait slice while the child is silent, bounded or not, and the stop ends the child's
    # whole tree: an unbounded child (build.ps1's MSBuild) as well as a contained one. PowerShell.Stop() requests the
    # same pipeline stop as Ctrl+C. Nothing waits on these terminations, so the survivor check gives them a moment.
    $holderPath = Join-Path $presentationTestRoot 'holder.cmd'
    $holderReadyPath = Join-Path $presentationTestRoot 'holder.ready'
    @'
@echo off
echo holder:started
type nul > "%~dp0holder.ready"
ping.exe -n 120 127.0.0.1 > nul
'@ | Set-Content -LiteralPath $holderPath -Encoding ASCII
    foreach ($holderBudget in @(0, 120)) {
        Remove-Item -LiteralPath $holderReadyPath -Force -ErrorAction SilentlyContinue
        $holderShell = [powershell]::Create()
        try {
            [void] $holderShell.AddScript({
                param([string] $ModulePath, [string] $HolderPath, [string] $LogPath, [int] $TimeoutSeconds)
                Import-Module $ModulePath -Force
                Invoke-RedXeStreamingProcess -FilePath $env:ComSpec -Arguments @('/d', '/c', $HolderPath) `
                    -LogPath $LogPath -TimeoutSeconds $TimeoutSeconds -OutputLineCallback { param([string] $Line, [bool] $IsError) }
            }.ToString()).AddArgument($presentationModulePath).AddArgument($holderPath).AddArgument(
                (Join-Path $presentationTestRoot "holder-$holderBudget.log")).AddArgument($holderBudget)
            $holderRun = $holderShell.BeginInvoke()
            $readyClock = [Diagnostics.Stopwatch]::StartNew()
            while (-not (Test-Path -LiteralPath $holderReadyPath) -and -not $holderRun.IsCompleted -and
                $readyClock.Elapsed.TotalSeconds -lt 30) {
                Start-Sleep -Milliseconds 50
            }
            if (-not (Test-Path -LiteralPath $holderReadyPath)) {
                throw "The stop fixture's child did not start within 30 s (budget $holderBudget s): $($holderShell.Streams.Error)"
            }
            $stopping = $holderShell.BeginStop($null, $null)
            if (-not $stopping.AsyncWaitHandle.WaitOne(15000)) {
                throw "Stopping a run whose child was silent (budget $holderBudget s) took longer than 15 s."
            }
            $holderShell.EndStop($stopping)
            $survivors = @(Get-FixtureSurvivors -Markers @($holderPath) -WaitSeconds 5)
            if ($survivors.Count -ne 0) {
                throw "A process of a stopped run (budget $holderBudget s) survived the stop: $(($survivors | ForEach-Object { "$($_.Name) $($_.ProcessId)" }) -join ', ')"
            }
        }
        finally {
            # Should the stop have failed, ending the tree ends the run too, so the shell can be disposed.
            foreach ($survivor in @(Get-FixtureSurvivors -Markers @($holderPath))) {
                Stop-Process -Id $survivor.ProcessId -Force -ErrorAction SilentlyContinue
            }
            $holderShell.Dispose()
        }
    }

    # A bounded child that exits while a process it started still holds its output open is ended after a short grace,
    # not at its budget, and the call reports that the child exited and something it started held the output. The
    # hardest shape is exercised: the grandchild inherits the redirected pipe (`start /b` keeps the standard handles)
    # and outlives its parent, so the pipe never reaches end of file and only job containment can reach it. It is
    # recognized by a marker in its command line, and the call returns only once it is gone. The child's last output
    # is a line without a newline, which reaches the log when the pipes are drained after the termination.
    #
    # The budget leaves the child ample time to get that far even on a loaded machine, where starting cmd.exe alone has
    # taken over a second; the child writes staller.ready once it has, so a missing line is output the helper lost.
    $stallMarker = 'RedXeStallFixture-' + [guid]::NewGuid().ToString('N')
    $stallerPath = Join-Path $presentationTestRoot 'staller.cmd'
    $stallReadyPath = Join-Path $presentationTestRoot 'staller.ready'
    @"
@echo off
echo staller:started
start /b "" cmd.exe /d /c "ping.exe -n 120 127.0.0.1 > nul & rem $stallMarker"
echo staller:grandchild
<nul set /p "=staller:partial"
type nul > "%~dp0staller.ready"
exit /b 0
"@ | Set-Content -LiteralPath $stallerPath -Encoding ASCII
    $stallLogPath = Join-Path $presentationTestRoot 'stalled.log'
    $stallBudget = 60
    $timeoutStopwatch = [Diagnostics.Stopwatch]::StartNew()
    $timeoutMessage = $null
    try {
        [void](Invoke-RedXeStreamingProcess `
            -FilePath $env:ComSpec `
            -Arguments @('/d', '/c', $stallerPath) `
            -WorkingDirectory $presentationTestRoot `
            -LogPath $stallLogPath `
            -TimeoutSeconds $stallBudget `
            -OutputLineCallback { param([string] $Line, [bool] $IsError) })
    }
    catch {
        $timeoutMessage = $_.Exception.Message
    }
    $timeoutStopwatch.Stop()
    $stallLogText = if (Test-Path -LiteralPath $stallLogPath) { Get-Content -LiteralPath $stallLogPath -Raw } else { '' }
    # The child's command line names this run's staller and the grandchild's carries the marker.
    $survivors = @(Get-FixtureSurvivors -Markers @($stallMarker, $stallerPath))
    if ($survivors.Count -ne 0) {
        foreach ($survivor in $survivors) { Stop-Process -Id $survivor.ProcessId -Force -ErrorAction SilentlyContinue }
        throw "A process of the stalled run survived its termination: $(($survivors | ForEach-Object { "$($_.Name) $($_.ProcessId)" }) -join ', ')"
    }
    if (-not (Test-Path -LiteralPath $stallReadyPath)) {
        throw "The stall fixture did not reach its grandchild within its $stallBudget s budget: $stallLogText"
    }
    if (-not $timeoutMessage -or
        $timeoutMessage -notmatch 'exited with code 0, but a process it started kept its output open and was terminated' -or
        $timeoutMessage -notmatch [regex]::Escape($stallLogPath)) {
        throw "A child whose output a process it started held open was not reported as such: '$timeoutMessage' (log: $stallLogText)"
    }
    # The grandchild pings for two minutes, so returning this soon means the grace, not the budget, ended the run.
    if ($timeoutStopwatch.Elapsed.TotalSeconds -ge $stallBudget - 10) {
        throw "Ending the run whose child had exited took $($timeoutStopwatch.Elapsed.TotalSeconds) s, nearly its whole budget."
    }
    if ($stallLogText -notmatch 'TIMEOUT:') {
        throw "The stalled run log lacks the timeout record: $stallLogText"
    }
    if ($stallLogText -notmatch 'staller:started' -or $stallLogText -notmatch 'staller:grandchild' -or
        $stallLogText -notmatch 'staller:partial') {
        throw "The stalled run log lacks output the child wrote before it was terminated: $stallLogText"
    }

    $formattedDuration = Format-RedXeBuildDuration -Duration ([TimeSpan]::FromMilliseconds(3723004))
    if ($formattedDuration -ne '01:02:03.004') {
        throw "Unexpected elapsed-time format: $formattedDuration"
    }
}
finally {
    $resolvedPresentationCleanupTarget = [IO.Path]::GetFullPath($presentationTestRoot)
    if (-not $resolvedPresentationCleanupTarget.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a build-presentation test directory outside '$testParent': $resolvedPresentationCleanupTarget"
    }
    for ($attempt = 0; $attempt -lt 20 -and (Test-Path -LiteralPath $resolvedPresentationCleanupTarget); ++$attempt) {
        try {
            Remove-Item -LiteralPath $resolvedPresentationCleanupTarget -Recurse -Force -ErrorAction Stop
        }
        catch {
            if ($attempt -eq 19) {
                throw
            }
            Start-Sleep -Milliseconds 100
        }
    }
}

Write-Host 'Build presentation and streaming tests passed.' -ForegroundColor Green

# The last native command above is a fixture that exits nonzero by design; report the script result explicitly.
exit 0
