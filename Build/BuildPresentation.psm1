Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-RedXeEnvironmentMap {
    $environment = @{}
    foreach ($entry in Get-ChildItem Env:) {
        $environment[$entry.Name] = $entry.Value
    }
    return $environment
}

function Test-RedXeInteractiveTerminal {
    [CmdletBinding()]
    param(
        [bool] $IsOutputRedirected = [Console]::IsOutputRedirected,
        [bool] $IsErrorRedirected = [Console]::IsErrorRedirected,
        [bool] $HasRawUi = ($null -ne $Host -and $null -ne $Host.UI -and $null -ne $Host.UI.RawUI),
        [bool] $CanReadWindowTitle = $true,
        [hashtable] $Environment = @{}
    )

    if ($Environment.Count -eq 0) {
        $Environment = Get-RedXeEnvironmentMap
    }
    if (-not $HasRawUi -or -not $CanReadWindowTitle) {
        return $false
    }

    if (-not $IsOutputRedirected -and -not $IsErrorRedirected) {
        return $true
    }

    foreach ($marker in @('CODEX_SHELL', 'WT_SESSION', 'TERM_PROGRAM', 'VSCODE_PID', 'ConEmuPID', 'ANSICON')) {
        if ($Environment.ContainsKey($marker) -and
            -not [string]::IsNullOrWhiteSpace([string] $Environment[$marker])) {
            return $true
        }
    }

    return $false
}

function Test-RedXeDirectConsoleSupportedHost {
    param(
        [hashtable] $Environment = @{}
    )

    if ($Environment.Count -eq 0) {
        $Environment = Get-RedXeEnvironmentMap
    }

    # These hosts display redirected line replay reliably, while a directly attached
    # native child can lose progress or color when the host captures its output.
    foreach ($marker in @('CODEX_SHELL', 'WT_SESSION')) {
        if ($Environment.ContainsKey($marker) -and
            -not [string]::IsNullOrWhiteSpace([string] $Environment[$marker])) {
            return $false
        }
    }

    return $true
}

function New-RedXeBuildLogPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $RepoRoot,

        [ValidateSet('Build', 'Clean', 'Rebuild')]
        [string] $Target = 'Build'
    )

    $logDirectory = Join-Path $RepoRoot '.build\logs'
    [void](New-Item -ItemType Directory -Path $logDirectory -Force)
    $timestamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
    $suffix = [guid]::NewGuid().ToString('N').Substring(0, 8)
    return Join-Path $logDirectory ("redxe-{0}-{1}-pid{2}-{3}.log" -f $Target.ToLowerInvariant(), $timestamp, $PID, $suffix)
}

function Get-RedXeBuildFileLoggerArguments {
    param(
        [Parameter(Mandatory)]
        [string] $LogPath
    )

    $resolvedLogPath = [IO.Path]::GetFullPath($LogPath)
    return @(
        '/fl',
        "/flp:Verbosity=minimal;LogFile=$resolvedLogPath;Encoding=UTF-8"
    )
}

function Get-RedXeBuildInvocationPlan {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [bool] $UseInteractiveTerminal,

        [Parameter(Mandatory)]
        [string] $LogPath,

        [hashtable] $Environment = @{}
    )

    $useDirectConsole = $UseInteractiveTerminal -and
        (Test-RedXeDirectConsoleSupportedHost -Environment $Environment)

    return [pscustomobject]@{
        UseDirectConsole = $useDirectConsole
        AdditionalArguments = if ($useDirectConsole) {
            Get-RedXeBuildFileLoggerArguments -LogPath $LogPath
        }
        else {
            @()
        }
    }
}

function Write-RedXeBuildBanner {
    [CmdletBinding()]
    param(
        [bool] $UseColor = $true
    )

    $redGlyphs = @(
        'RRRR   EEEEE  DDDD   ',
        'R   R  E      D   D  ',
        'RRRR   EEEE   D   D  ',
        'R  R   E      D   D  ',
        'R   R  EEEEE  DDDD   '
    )
    $xeGlyphs = @(
        'X   X  EEEEE',
        ' X X   E    ',
        '  X    EEEE ',
        ' X X   E    ',
        'X   X  EEEEE'
    )

    Write-Host ''
    for ($index = 0; $index -lt $redGlyphs.Count; ++$index) {
        if ($UseColor) {
            Write-Host '  ' -NoNewline
            Write-Host $redGlyphs[$index] -ForegroundColor Red -NoNewline
            Write-Host $xeGlyphs[$index] -ForegroundColor Cyan
        }
        else {
            Write-Host ("  {0}{1}" -f $redGlyphs[$index], $xeGlyphs[$index])
        }
    }

    if ($UseColor) {
        Write-Host '          XENEON EDGE // BUILD SIGNAL LOCKED' -ForegroundColor DarkGray
    }
    else {
        Write-Host '          XENEON EDGE // BUILD SIGNAL LOCKED'
    }
    Write-Host ''
}

function Get-RedXeBuildLineForegroundColor {
    [CmdletBinding()]
    param(
        [AllowNull()]
        [string] $Line,

        [bool] $IsError = $false
    )

    if ($IsError) {
        return 'Red'
    }
    if ([string]::IsNullOrWhiteSpace($Line)) {
        return $null
    }
    if ($Line -match '(?i)(^|\s)(build\s+failed|fatal\s+error|error\s+([A-Z]+)?\d+|MSBUILD\s*:\s*error)\b') {
        return 'Red'
    }
    if ($Line -match '(?i)(^|\s)warning\s+([A-Z]+)?\d+\b') {
        return 'Yellow'
    }
    if ($Line -match '(?i)\.(vcxproj|vcproj|sln)\s+->\s+') {
        return 'Green'
    }
    if ($Line -match '(?i)^\s*build\s+succeeded\.?\s*$') {
        return 'Green'
    }

    return $null
}

function Write-RedXeBuildStreamingLine {
    [CmdletBinding()]
    param(
        [AllowNull()]
        [string] $Line,

        [bool] $IsError = $false
    )

    $foregroundColor = Get-RedXeBuildLineForegroundColor -Line $Line -IsError $IsError
    if ([string]::IsNullOrWhiteSpace($foregroundColor)) {
        Write-Host $Line
    }
    else {
        Write-Host $Line -ForegroundColor $foregroundColor
    }
}

function Test-RedXeBuildDiagnosticLine {
    param(
        [Parameter(Mandatory)]
        [AllowEmptyString()]
        [string] $Line,

        [Parameter(Mandatory)]
        [ValidateSet('Warning', 'Error')]
        [string] $Kind
    )

    $diagnosticCode = '[A-Z]+[A-Z0-9]*\d[A-Z0-9]*'
    if ($Kind -eq 'Warning') {
        return $Line -match "(?i):\s+warning\s+$diagnosticCode\s*:"
    }
    return $Line -match "(?i):\s+(?:fatal\s+)?error\s+$diagnosticCode\s*:"
}

function Get-RedXeBuildDiagnosticSummary {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $LogPath
    )

    $resolvedLogPath = [IO.Path]::GetFullPath($LogPath)
    $warningCount = 0
    $errorCount = 0
    foreach ($line in [IO.File]::ReadLines($resolvedLogPath)) {
        if (Test-RedXeBuildDiagnosticLine -Line $line -Kind Warning) {
            ++$warningCount
        }
        if (Test-RedXeBuildDiagnosticLine -Line $line -Kind Error) {
            ++$errorCount
        }
    }

    return [pscustomobject]@{
        LogPath = $resolvedLogPath
        WarningCount = $warningCount
        ErrorCount = $errorCount
    }
}

function Write-RedXeBuildDiagnosticSummary {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $LogPath
    )

    if (-not (Test-Path -LiteralPath $LogPath -PathType Leaf)) {
        Write-Host 'Diagnostics: unavailable (build log was not created)' -ForegroundColor DarkYellow
        return
    }

    $summary = Get-RedXeBuildDiagnosticSummary -LogPath $LogPath
    $foregroundColor = if ($summary.ErrorCount -gt 0) {
        'Red'
    }
    elseif ($summary.WarningCount -gt 0) {
        'Yellow'
    }
    else {
        'Green'
    }
    Write-Host ("Diagnostics: {0} warning(s), {1} error(s)" -f $summary.WarningCount, $summary.ErrorCount) `
        -ForegroundColor $foregroundColor
}

function ConvertTo-RedXeQuotedProcessArgument {
    param(
        [AllowNull()]
        [string] $Argument
    )

    if ($null -eq $Argument -or $Argument.Length -eq 0) {
        return '""'
    }
    if ($Argument -notmatch '[\s"]') {
        return $Argument
    }

    $builder = [Text.StringBuilder]::new()
    [void] $builder.Append('"')
    $backslashCount = 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq '\') {
            ++$backslashCount
            continue
        }
        if ($character -eq '"') {
            if ($backslashCount -gt 0) {
                [void] $builder.Append(('\' * ($backslashCount * 2)))
                $backslashCount = 0
            }
            [void] $builder.Append('\"')
            continue
        }
        if ($backslashCount -gt 0) {
            [void] $builder.Append(('\' * $backslashCount))
            $backslashCount = 0
        }
        [void] $builder.Append($character)
    }
    if ($backslashCount -gt 0) {
        [void] $builder.Append(('\' * ($backslashCount * 2)))
    }
    [void] $builder.Append('"')
    return $builder.ToString()
}

function Set-RedXeProcessArguments {
    param(
        [Parameter(Mandatory)]
        [Diagnostics.ProcessStartInfo] $ProcessStartInfo,

        [string[]] $Arguments = @()
    )

    $argumentListProperty = $ProcessStartInfo.PSObject.Properties['ArgumentList']
    if ($argumentListProperty -and $null -ne $ProcessStartInfo.ArgumentList) {
        foreach ($argument in $Arguments) {
            [void] $ProcessStartInfo.ArgumentList.Add($argument)
        }
        return
    }

    $ProcessStartInfo.Arguments = (($Arguments | ForEach-Object {
        ConvertTo-RedXeQuotedProcessArgument -Argument $_
    }) -join ' ')
}

function Invoke-RedXeStreamingProcess {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $FilePath,

        [string[]] $Arguments = @(),

        [string] $WorkingDirectory = (Get-Location).Path,

        [Parameter(Mandatory)]
        [string] $LogPath,

        [scriptblock] $OutputLineCallback
    )

    $resolvedLogPath = [IO.Path]::GetFullPath($LogPath)
    $logDirectory = Split-Path -Parent $resolvedLogPath
    if (-not [string]::IsNullOrWhiteSpace($logDirectory)) {
        [void](New-Item -ItemType Directory -Path $logDirectory -Force)
    }

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    Set-RedXeProcessArguments -ProcessStartInfo $startInfo -Arguments $Arguments
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $logWriter = $null
    try {
        $encoding = [Text.UTF8Encoding]::new($false)
        $logWriter = [IO.StreamWriter]::new($resolvedLogPath, $false, $encoding)

        if (-not $process.Start()) {
            throw "Unable to start '$FilePath'."
        }

        $standardOutputOpen = $true
        $standardErrorOpen = $true
        $standardOutputTask = $process.StandardOutput.ReadLineAsync()
        $standardErrorTask = $process.StandardError.ReadLineAsync()

        while ($standardOutputOpen -or $standardErrorOpen) {
            $pendingTasks = [Collections.Generic.List[Threading.Tasks.Task[string]]]::new()
            if ($standardOutputOpen) {
                [void] $pendingTasks.Add($standardOutputTask)
            }
            if ($standardErrorOpen) {
                [void] $pendingTasks.Add($standardErrorTask)
            }

            $completedIndex = [Threading.Tasks.Task]::WaitAny($pendingTasks.ToArray())
            $completedTask = $pendingTasks[$completedIndex]
            $isError = $standardErrorOpen -and
                [object]::ReferenceEquals($completedTask, $standardErrorTask)
            $line = $completedTask.GetAwaiter().GetResult()

            if ($null -eq $line) {
                if ($isError) {
                    $standardErrorOpen = $false
                }
                else {
                    $standardOutputOpen = $false
                }
                continue
            }

            $logWriter.WriteLine($line)
            if ($OutputLineCallback) {
                & $OutputLineCallback $line $isError
            }
            else {
                Write-Host $line
            }

            if ($isError) {
                $standardErrorTask = $process.StandardError.ReadLineAsync()
            }
            else {
                $standardOutputTask = $process.StandardOutput.ReadLineAsync()
            }
        }

        $process.WaitForExit()
        $exitCode = [int] $process.ExitCode
        $global:LASTEXITCODE = $exitCode
        return $exitCode
    }
    finally {
        if ($logWriter) {
            $logWriter.Dispose()
        }
        $process.Dispose()
    }
}

function Format-RedXeBuildDuration {
    param(
        [Parameter(Mandatory)]
        [TimeSpan] $Duration
    )

    return $Duration.ToString('hh\:mm\:ss\.fff')
}

Export-ModuleMember -Function @(
    'Test-RedXeInteractiveTerminal',
    'New-RedXeBuildLogPath',
    'Get-RedXeBuildInvocationPlan',
    'Write-RedXeBuildBanner',
    'Get-RedXeBuildLineForegroundColor',
    'Write-RedXeBuildStreamingLine',
    'Get-RedXeBuildDiagnosticSummary',
    'Write-RedXeBuildDiagnosticSummary',
    'Invoke-RedXeStreamingProcess',
    'Format-RedXeBuildDuration'
)
