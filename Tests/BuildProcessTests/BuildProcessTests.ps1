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
[Console]::Error.WriteLine("stderr:$Message")
exit 17
'@ | Set-Content -LiteralPath $emitterPath -Encoding UTF8
    $streamLogPath = Join-Path $presentationTestRoot 'streamed.log'
    $receivedLines = [Collections.Generic.List[psobject]]::new()
    $powershellPath = (Get-Process -Id $PID -ErrorAction Stop).Path
    $streamExitCode = Invoke-RedXeStreamingProcess `
        -FilePath $powershellPath `
        -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $emitterPath, 'argument with spaces') `
        -WorkingDirectory $presentationTestRoot `
        -LogPath $streamLogPath `
        -OutputLineCallback {
        param([string] $Line, [bool] $IsError)
        [void] $receivedLines.Add([pscustomobject]@{ Line = $Line; IsError = $IsError })
    }

    if ($streamExitCode -ne 17) {
        throw "Streaming did not propagate the child exit code; expected 17, got $streamExitCode."
    }
    $stdoutRecord = $receivedLines | Where-Object { $_.Line -eq 'stdout:argument with spaces' -and -not $_.IsError }
    $stderrRecord = $receivedLines | Where-Object { $_.Line -eq 'stderr:argument with spaces' -and $_.IsError }
    if (-not $stdoutRecord -or -not $stderrRecord) {
        throw "Streaming did not preserve argument quoting and stream identity: $($receivedLines | Out-String)"
    }
    $streamLogText = Get-Content -LiteralPath $streamLogPath -Raw
    if ($streamLogText -notmatch 'stdout:argument with spaces' -or
        $streamLogText -notmatch 'stderr:argument with spaces' -or
        $streamLogText.Contains([char] 0x1b)) {
        throw 'The captured streaming log omitted output or contained terminal control sequences.'
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
