Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# The zero-warning gate's diagnostic grammar (the build console contract the other repositories share). Without -Force: a
# nested forced import would remove the copy build.ps1 imported for itself.
Import-Module (Join-Path $PSScriptRoot 'BuildConsole.psm1')

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

function Get-RedXeBuildBannerModel {
    $block = [char]0x2588
    $topLeft = [char]0x2554
    $topRight = [char]0x2557
    $bottomLeft = [char]0x255A
    $bottomRight = [char]0x255D
    $bar = [char]0x2550
    $vertical = [char]0x2551
    $space = ' '

    $letterR = @(
        "$block$block$block$block$block$block$topRight$space",
        "$block$block$topLeft$bar$bar$block$block$topRight",
        "$block$block$block$block$block$block$topLeft$bottomRight",
        "$block$block$topLeft$bar$bar$block$block$topRight",
        "$block$block$vertical$space$space$block$block$vertical",
        "$bottomLeft$bar$bottomRight$space$space$bottomLeft$bar$bottomRight"
    )
    $letterE = @(
        "$block$block$block$block$block$block$block$topRight",
        "$block$block$topLeft$bar$bar$bar$bar$bottomRight",
        "$block$block$block$block$block$topRight$space$space",
        "$block$block$topLeft$bar$bar$bottomRight$space$space",
        "$block$block$block$block$block$block$block$topRight",
        "$bottomLeft$bar$bar$bar$bar$bar$bar$bottomRight"
    )
    $letterD = @(
        "$block$block$block$block$block$block$topRight$space",
        "$block$block$topLeft$bar$bar$block$block$topRight",
        "$block$block$vertical$space$space$block$block$vertical",
        "$block$block$vertical$space$space$block$block$vertical",
        "$block$block$block$block$block$block$topLeft$bottomRight",
        "$bottomLeft$bar$bar$bar$bar$bar$bottomRight$space"
    )
    $letterX = @(
        "$block$block$topRight$space$space$block$block$topRight",
        "$bottomLeft$block$block$topRight$block$block$topLeft$bottomRight",
        "$space$bottomLeft$block$block$block$topLeft$bottomRight$space",
        "$space$block$block$topLeft$block$block$topRight$space",
        "$block$block$topLeft$bottomRight$space$block$block$topRight",
        "$bottomLeft$bar$bottomRight$space$space$bottomLeft$bar$bottomRight"
    )

    $redGlyphs = @(for ($index = 0; $index -lt 6; ++$index) {
        $letterR[$index] + $letterE[$index] + $letterD[$index]
    })
    $xeGlyphs = @(for ($index = 0; $index -lt 6; ++$index) {
        $letterX[$index] + $letterE[$index]
    })

    $gap = 2
    $tagline = 'XENEON EDGE // BUILD SIGNAL LOCKED'
    $taglineLabel = " $tagline "
    $artWidth = $redGlyphs[0].Length + $gap + $xeGlyphs[0].Length
    $innerWidth = 58
    $artLeftPad = [int][Math]::Floor(($innerWidth - $artWidth) / 2)
    $artRightPad = $innerWidth - $artWidth - $artLeftPad
    $taglineLeftBar = [int][Math]::Floor(($innerWidth - $taglineLabel.Length) / 2)
    $taglineRightBar = $innerWidth - $taglineLabel.Length - $taglineLeftBar

    return [pscustomobject]@{
        RedGlyphs = @($redGlyphs)
        XeGlyphs = @($xeGlyphs)
        Gap = $gap
        Tagline = $tagline
        TaglineLabel = $taglineLabel
        InnerWidth = $innerWidth
        ArtLeftPad = $artLeftPad
        ArtRightPad = $artRightPad
        TaglineLeftBar = $taglineLeftBar
        TaglineRightBar = $taglineRightBar
        TopLeft = $topLeft
        TopRight = $topRight
        BottomLeft = $bottomLeft
        BottomRight = $bottomRight
        Bar = $bar
        Vertical = $vertical
        Indent = '  '
    }
}

function Write-RedXeBannerChrome {
    param(
        [Parameter(Mandatory)]
        [string] $Text,

        [bool] $UseColor,

        [switch] $NoNewline
    )

    if ($UseColor) {
        if ($NoNewline) {
            Write-Host $Text -ForegroundColor DarkGray -NoNewline
        }
        else {
            Write-Host $Text -ForegroundColor DarkGray
        }
    }
    elseif ($NoNewline) {
        Write-Host $Text -NoNewline
    }
    else {
        Write-Host $Text
    }
}

function Write-RedXeBuildBanner {
    [CmdletBinding()]
    param(
        [bool] $UseColor = $true
    )

    $model = Get-RedXeBuildBannerModel
    $bar = $model.Bar.ToString()

    Write-Host ''
    Write-RedXeBannerChrome -UseColor $UseColor -Text (
        '{0}{1}{2}{3}' -f $model.Indent, $model.TopLeft, ($bar * $model.InnerWidth), $model.TopRight)

    for ($index = 0; $index -lt $model.RedGlyphs.Count; ++$index) {
        $leftPad = ' ' * $model.ArtLeftPad
        $gap = ' ' * $model.Gap
        $rightPad = ' ' * $model.ArtRightPad
        if ($UseColor) {
            Write-RedXeBannerChrome -UseColor $true -NoNewline -Text ($model.Indent + $model.Vertical + $leftPad)
            Write-Host $model.RedGlyphs[$index] -ForegroundColor Red -NoNewline
            Write-Host $gap -NoNewline
            Write-Host $model.XeGlyphs[$index] -ForegroundColor Cyan -NoNewline
            Write-RedXeBannerChrome -UseColor $true -Text ($rightPad + $model.Vertical)
        }
        else {
            Write-Host (
                '{0}{1}{2}{3}{4}{5}{6}{1}' -f $model.Indent, $model.Vertical, $leftPad,
                $model.RedGlyphs[$index], $gap, $model.XeGlyphs[$index], $rightPad)
        }
    }

    Write-RedXeBannerChrome -UseColor $UseColor -Text (
        '{0}{1}{2}{3}{4}{5}' -f $model.Indent, $model.BottomLeft, ($bar * $model.TaglineLeftBar),
        $model.TaglineLabel, ($bar * $model.TaglineRightBar), $model.BottomRight)
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

function Get-RedXeDxUiUpdateNoticeForegroundColor {
    [CmdletBinding()]
    param(
        [AllowNull()]
        [string] $Notice
    )

    if ([string]::IsNullOrWhiteSpace($Notice)) {
        return $null
    }

    if ($Notice -like 'DxUi main * has no successful completed validation;*') {
        return 'Red'
    }
    if ($Notice -like 'DxUi update available:*') {
        return 'Yellow'
    }

    return 'DarkYellow'
}

function Format-RedXeDxUiUpdateNotice {
    [CmdletBinding()]
    param(
        [AllowNull()]
        [string] $Notice
    )

    if ([string]::IsNullOrWhiteSpace($Notice)) {
        return $null
    }

    if ($Notice -like 'DxUi update available:*') {
        $summary = $Notice -replace ' \. Update .* on a branch and run the product regressions\.$', ''
        return "$summary.`nTo upgrade and run local validation: .\Update-DxUi.ps1`nTo upgrade only after equivalent product validation passed elsewhere: .\Update-DxUi.ps1 -UpdateOnly"
    }

    return $Notice
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

function Get-RedXeBuildDiagnosticSummary {
    <# The log's warning and error counts in the zero-warning gate's own grammar (BuildConsole's Get-NativeBuildDiagnostics), so
       the summary line never reports a count the gate then contradicts, such as a warning without a code. #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $LogPath
    )

    return Get-NativeBuildDiagnostics -LogPath $LogPath
}

function Reset-RedXeWarnedProjectTracking {
    <# Deletes the MSBuild tracking logs of every project a diagnostic line of LogPath names ([...\<project>.vcxproj] at its end)
       under IntermediateRoot (.build\Intermediate\<Platform>\<Configuration>), and returns those projects. /WX stays off, so a
       warning still writes its object file and tracking logs, and an unchanged rerun would skip that source file and pass the
       zero-warning gate; without its tracking logs the project compiles and links again, and warns again, until it builds clean. #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $LogPath,

        [Parameter(Mandatory)]
        [string] $IntermediateRoot
    )

    $projects = [Collections.Generic.SortedSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($line in [IO.File]::ReadLines([IO.Path]::GetFullPath($LogPath))) {
        $match = [regex]::Match($line, '(?i)\b(?:warning|error)\b.*\[(?<project>[^\[\]]+\.vcxproj)\]\s*$')
        if ($match.Success) {
            [void]$projects.Add([IO.Path]::GetFileNameWithoutExtension($match.Groups['project'].Value))
        }
    }
    foreach ($project in $projects) {
        foreach ($tracking in @(Get-ChildItem -LiteralPath (Join-Path $IntermediateRoot $project) -Filter '*.tlog' -Directory -ErrorAction SilentlyContinue)) {
            Get-ChildItem -LiteralPath $tracking.FullName -Filter '*.tlog' -File | Remove-Item -Force
        }
    }
    return @($projects)
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
    'Get-RedXeDxUiUpdateNoticeForegroundColor',
    'Format-RedXeDxUiUpdateNotice',
    'Write-RedXeBuildStreamingLine',
    'Get-RedXeBuildDiagnosticSummary',
    'Write-RedXeBuildDiagnosticSummary',
    'Reset-RedXeWarnedProjectTracking',
    'Format-RedXeBuildDuration'
)
