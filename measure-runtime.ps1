<#
.SYNOPSIS
Collect an opt-in Release x64 resource baseline from owned, non-activating production windows.
.DESCRIPTION
Uses a portable copy of the Release template with services and tray disabled. Shows a normal full-height XENEON
window without activation, samples fresh pages, visits every page and returns, then hides/minimizes that owned
window. Does not send physical input or touch normal user settings. Retains scene, raw logs, source/binary hashes
and phase timings under .build/receipts. PresentMon/input latency and real AV/Logicon acceptance are separate work.
#>
[CmdletBinding()]
param(
    [ValidateRange(20, 600)][int] $Seconds = 65,
    [ValidateRange(1, 5)][int] $IntervalSeconds = 5,
    [ValidateSet('A-matrix', 'B-gallery', 'C-system', 'D-cycle')]
    [string[]] $Scenarios = @('A-matrix', 'B-gallery', 'C-system', 'D-cycle'),
    [switch] $InactiveOnly,
    [string] $OutputRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot
if ($InactiveOnly) {
    if (-not $PSBoundParameters.ContainsKey('Scenarios')) { $Scenarios = @('A-matrix', 'B-gallery', 'C-system') }
    if ('D-cycle' -in $Scenarios) { throw 'Inactive-only measurement cannot navigate a suspended presentation.' }
}
$executable = Join-Path $repoRoot '.build/x64/Release/RedXe.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build x64 Release before measuring.' }
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repoRoot ('.build/receipts/runtime-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw 'Use a fresh receipt directory to preserve previous runs.' }
[void](New-Item -ItemType Directory -Path $OutputRoot)
$utf8 = [Text.UTF8Encoding]::new($false)
if (-not ('RedXeRuntimeMeasurement' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class RedXeRuntimeMeasurement {
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr w, int command);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr w, uint message, UIntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr w, out Rect rect);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr w);
    [DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint flags);
    [DllImport("kernel32.dll")] public static extern bool GetSystemPowerStatus(out Power power);
    [StructLayout(LayoutKind.Sequential)] public struct Power {
        public byte acLineStatus, batteryFlag, batteryPercent, reserved;
        public uint batteryLifeSeconds, batteryFullLifeSeconds;
    }
    delegate bool Callback(IntPtr hwnd, IntPtr lparam);
    [DllImport("user32.dll")] static extern bool EnumWindows(Callback callback, IntPtr state);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr hwnd, System.Text.StringBuilder name, int size);
    public static IntPtr Find(uint pid) {
        IntPtr found=IntPtr.Zero;
        EnumWindows((w,p)=>{ uint owner; GetWindowThreadProcessId(w,out owner); if(owner==pid) { var name=new System.Text.StringBuilder(256); GetClassNameW(w,name,name.Capacity); if(name.ToString()=="RedXe.Window") { found=w; return false; } } return true; },IntPtr.Zero);
        return found;
    }
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left, top, right, bottom; }
    public static bool Wheel(IntPtr w, bool forward) {
        Rect rect; if (!GetWindowRect(w, out rect)) return false;
        // All shipped first tiles decline horizontal wheel; use its interior, never a host edge band.
        int x=rect.left+64, y=rect.top+64;
        uint wp=unchecked((uint)((forward ? 120 : -120) << 16));
        return PostMessageW(w, 0x20e, new UIntPtr(wp), new IntPtr(unchecked((int)(((uint)y << 16) | ((uint)x & 65535)))));
    }
}
'@
}
$oldDpi = [RedXeRuntimeMeasurement]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))
$oldExecutionState = if ($InactiveOnly) { 0 } else { [RedXeRuntimeMeasurement]::SetThreadExecutionState([uint32]2147483651) }
if (-not $InactiveOnly -and $oldExecutionState -eq 0) {
    [void][RedXeRuntimeMeasurement]::SetThreadDpiAwarenessContext($oldDpi)
    throw 'Could not hold system/display awake for hardware measurement.'
}
$powerStatus = [RedXeRuntimeMeasurement+Power]::new()
if (-not [RedXeRuntimeMeasurement]::GetSystemPowerStatus([ref]$powerStatus) -or $powerStatus.acLineStatus -ne 1) {
    if ($oldExecutionState -ne 0) { [void][RedXeRuntimeMeasurement]::SetThreadExecutionState($oldExecutionState) }
    [void][RedXeRuntimeMeasurement]::SetThreadDpiAwarenessContext($oldDpi)
    throw 'Hardware baseline requires AC power.'
}
$runs = [Collections.Generic.List[object]]::new()
$identityBefore = @()
try {
function Write-Json([string] $Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 100), $utf8)
}
function Hash-Inputs {
    $paths = @(& git -C $repoRoot ls-files --cached --others --exclude-standard) |
        Where-Object { $_ -match '\.(cpp|h|hlsl|vcxproj|props|targets|json|ps1|psm1)$' -and
            $_ -notmatch '^(Measurements|Mockups|docs)/' } | Sort-Object -Unique
    foreach ($path in $paths) {
        $absolute = Join-Path $repoRoot $path
        if (Test-Path -LiteralPath $absolute -PathType Leaf) {
            [pscustomobject]@{ path = $path; sha256 = (Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash }
        }
    }
    foreach ($file in @(Get-Item -LiteralPath $executable) +
        @(Get-ChildItem -LiteralPath (Split-Path -Parent $executable) -File -Filter '*.dll') +
        @(Get-ChildItem -LiteralPath (Join-Path $repoRoot '.build/x64/Release/Plugins') -File -Recurse |
            Where-Object { $_.Extension -in '.exe', '.dll' }) +
        @(Get-Item -LiteralPath (Join-Path $repoRoot '.build/x64/Release/DxUi.provenance.json'))) {
        [pscustomobject]@{ path = $file.FullName; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
    }
}
$identityBefore = @(Hash-Inputs)
Write-Json (Join-Path $OutputRoot 'identity-before.json') $identityBefore
$head = & git -C $repoRoot rev-parse HEAD
$status = @(& git -C $repoRoot status --short)
$os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber
$cpu = Get-CimInstance Win32_Processor | Select-Object Name, NumberOfLogicalProcessors
Write-Json (Join-Path $OutputRoot 'context.json') ([ordered]@{
    head = $head; workingTree = $status; version = (Get-Item $executable).VersionInfo.FileVersion;
    profile = 'x64 Release'; monitor = 'xeneon'; seconds = $Seconds; intervalSeconds = $IntervalSeconds; scenarios = $Scenarios;
    os = $os; cpu = $cpu; startedUtc = [DateTime]::UtcNow.ToString('o');
    power = $powerStatus; inactiveOnly = [bool]$InactiveOnly;
    displayAwake = if ($InactiveOnly) { 'No power request; owned windows remain hidden.' }
        else { 'temporary ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED; previous request restored' };
    variant = 'Release template; services {}, tray false; normal XENEON popup; no activation or physical input';
    excludes = 'helper-process resources, PresentMode, presented-frame/input latency, mid-swipe peak, real-device acceptance'
})
$source = [IO.File]::ReadAllText((Join-Path $repoRoot 'Settings/RedXe.settings.json'))
$template = $source | ConvertFrom-Json -AsHashtable
$pages = @($template.pages)
Write-Json (Join-Path $OutputRoot 'page-map.json') @($pages | ForEach-Object { $_.name })
function Phase($Process, [Collections.Generic.List[object]] $Phases, [string] $Name, [int] $HoldSeconds) {
    Write-Host "Measuring $Name ($HoldSeconds s)..."
    $Phases.Add([pscustomobject]@{ name = $Name; utc = [DateTime]::UtcNow.ToString('o'); durationSeconds = $HoldSeconds })
    if ($Process.WaitForExit($HoldSeconds * 1000)) { throw "RedXe ended during $Name (exit $($Process.ExitCode))." }
}
function Collect-Run([string] $Name, [int] $FirstPage, [bool] $Cycle) {
    $root = Join-Path $OutputRoot $Name
    [void](New-Item -ItemType Directory -Path (Join-Path $root 'Settings'))
    $scene = $source | ConvertFrom-Json -AsHashtable
    $scene.services = @{}
    $scene.trayIcon = $false
    $scene.pages = @($pages[$FirstPage]) + @(for ($i = 0; $i -lt $pages.Count; ++$i) {
        if ($i -ne $FirstPage) { $pages[$i] }
    })
    $settings = Join-Path $root 'Settings/baseline.settings.json'
    Write-Json $settings $scene
    Write-Json (Join-Path $root 'initial.settings.json') $scene
    $start = [Diagnostics.ProcessStartInfo]::new($executable)
    $start.WorkingDirectory = $repoRoot
    $start.UseShellExecute = $false
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    foreach ($arg in @("--probe=$IntervalSeconds", '--settings', $settings)) { $start.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($start)
    $phases = [Collections.Generic.List[object]]::new()
    $window = [IntPtr]::Zero
    try {
        # Start/show only the owned popup without activation. Refuse a different-size/DPI topology.
        if ($process.WaitForExit(3000)) { throw "RedXe did not start (exit $($process.ExitCode))." }
        $process.Refresh()
        $window = $process.MainWindowHandle
        if ($window -eq [IntPtr]::Zero) { $window = [RedXeRuntimeMeasurement]::Find($process.Id) }
        if ($window -eq [IntPtr]::Zero) { throw 'The owned measurement window was not found.' }
        if (-not $InactiveOnly) { [void][RedXeRuntimeMeasurement]::ShowWindowAsync($window, 4) }
        $rect = [RedXeRuntimeMeasurement+Rect]::new()
        [void][RedXeRuntimeMeasurement]::GetWindowRect($window, [ref]$rect)
        if (($rect.right - $rect.left) -ne 2560 -or ($rect.bottom - $rect.top) -ne 720 -or
            [RedXeRuntimeMeasurement]::GetDpiForWindow($window) -ne 96) { throw 'Requires a real 2560x720, 96-DPI XENEON window.' }
        $runs.Add([ordered]@{ name = $Name; pid = $process.Id; dpi = [RedXeRuntimeMeasurement]::GetDpiForWindow($window);
            rectangle = $rect; pages = @($scene.pages | ForEach-Object { $_.name }); phases = $phases })
        Phase $process $phases 'cold-settled' $Seconds
        $latest = Get-ChildItem -LiteralPath (Join-Path $root 'Logs') -File -Filter '*.jsonl' |
            Sort-Object LastWriteTime | Select-Object -Last 1 |
            ForEach-Object { Get-Content -LiteralPath $_.FullName } |
            Where-Object { $_ -match '"event":"probe"' } | Select-Object -Last 1 | ConvertFrom-Json
        if ($null -eq $latest -or ($InactiveOnly -and ($latest.state -band 2) -ne 0) -or
            (-not $InactiveOnly -and ($latest.state -band 254) -ne 6)) {
            throw 'Visible/display-on/unoccluded state is unavailable; retain this inactive receipt and retry when ready.'
        }
        if ($Cycle) {
            for ($i = 1; $i -lt $pages.Count; ++$i) {
                if (-not [RedXeRuntimeMeasurement]::Wheel($window, $true)) { throw 'Owned-window navigation post failed.' }
                Phase $process $phases ('visit-' + $i) 15
            }
            for ($i = 1; $i -lt $pages.Count; ++$i) {
                if (-not [RedXeRuntimeMeasurement]::Wheel($window, $false)) { throw 'Owned-window return post failed.' }
                Phase $process $phases ('return-' + $i) 10
            }
            Phase $process $phases 'returned-settled' $Seconds
            [void][RedXeRuntimeMeasurement]::ShowWindowAsync($window, 0)
            Phase $process $phases 'hidden' 25
            [void][RedXeRuntimeMeasurement]::ShowWindowAsync($window, 4)
            Phase $process $phases 'restored' 10
            [void][RedXeRuntimeMeasurement]::ShowWindowAsync($window, 7)
            Phase $process $phases 'minimized' 25
        }
    }
    finally {
        if (-not $process.HasExited) {
            if ($window -eq [IntPtr]::Zero) { $process.Refresh(); $window = $process.MainWindowHandle }
            if ($window -eq [IntPtr]::Zero -or -not
                [RedXeRuntimeMeasurement]::PostMessageW($window, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero)) {
                throw 'Owned RedXe could not be asked to close; it remains running.'
            }
            if (-not $process.WaitForExit(10000)) { throw 'Owned RedXe did not close within 10 seconds; it remains running.' }
        }
        $exitCode = $process.ExitCode
        $process.Dispose()
        Write-Json (Join-Path $root 'phases.json') $phases.ToArray()
    }
    if ($exitCode -ne 0) { throw "Measurement run exited $exitCode." }
}
    if ('A-matrix' -in $Scenarios) { Collect-Run 'A-matrix' 0 $false }
    if ('B-gallery' -in $Scenarios) { Collect-Run 'B-gallery' 1 $false }
    if ('C-system' -in $Scenarios) { Collect-Run 'C-system' 2 $false }
    if ('D-cycle' -in $Scenarios) { Collect-Run 'D-cycle' 0 $true }
    Write-Json (Join-Path $OutputRoot 'runs.json') $runs.ToArray()
    $identityAfter = @(Hash-Inputs)
    Write-Json (Join-Path $OutputRoot 'identity-after.json') $identityAfter
    if (Compare-Object ($identityBefore | ConvertTo-Json -Depth 5) ($identityAfter | ConvertTo-Json -Depth 5)) {
        throw 'Source/binary inputs changed during the run; retain receipts but do not compare them.'
    }
    $records = [Collections.Generic.List[object]]::new()
    $summaries = [Collections.Generic.List[object]]::new()
    function Percentile($Values, [double] $Fraction) {
        $sorted = @($Values | Sort-Object)
        if ($sorted.Count -eq 0) { return $null }
        return $sorted[[Math]::Max(0, [Math]::Ceiling($Fraction * $sorted.Count) - 1)]
    }
    foreach ($run in $runs) {
        $logs = @(Get-ChildItem -LiteralPath (Join-Path $OutputRoot ($run.name + '/Logs')) -File -Filter '*.jsonl' |
            ForEach-Object { Get-Content -LiteralPath $_.FullName | ForEach-Object { $_ | ConvertFrom-Json -AsHashtable } })
        Write-Json (Join-Path $OutputRoot ($run.name + '/devices.json')) @($logs | Where-Object { $_.event -eq 'device-created' })
        if (@($logs | Where-Object { $_.event -eq 'probe-failed' -or $_.event -eq 'failure-exit' }).Count -ne 0) {
            throw "Probe/runtime failure in $($run.name); inspect raw logs."
        }
        $samples = @($logs | Where-Object { $_.event -eq 'probe' } | Sort-Object tick)
        if ($samples.Count -lt 2) { throw "Too few probe records in $($run.name)." }
        for ($i = 1; $i -lt $samples.Count; ++$i) {
            if ($samples[$i].tick - $samples[$i - 1].tick -ne $samples[$i].ms) {
                throw 'A probe record is missing; interval/CPU averages would be ambiguous.'
            }
        }
        foreach ($sample in $samples) {
            if ($sample.v -ne 1 -or $sample.process.Count -ne 11 -or $sample.present.Count -ne 11 -or
                $sample.local.Count -ne 5 -or $sample.nonlocal.Count -ne 5 -or $sample.av.Count -ne 9 -or
                $sample.pid -ne $run.pid -or $sample.process[0] -ne 127) { throw 'Unexpected probe format/identity/validity.' }
            $sample['run'] = $run.name
            $records.Add($sample)
        }
        foreach ($phase in $run.phases) {
            $startUtc = ([DateTime]$phase.utc).ToUniversalTime()
            $endUtc = $startUtc.AddSeconds($phase.durationSeconds)
            $phaseSamples = @($samples | Where-Object {
                $time = ([DateTime]$_.ts).ToUniversalTime()
                $time -ge $startUtc -and $time -lt $endUtc
            })
            # Discard intervals that straddle a phase boundary; first samples/resets carry no present delta.
            $intervals = @($phaseSamples | Where-Object {
                ([DateTime]$_.ts).ToUniversalTime().AddMilliseconds(-[double]$_.ms) -ge $startUtc -and
                ($_.present[3] -band 1) -ne 0 -and ($_.process[0] -band 3) -eq 3
            })
            $expectedPage = 0
            if ($phase.name -match '^visit-(\d+)$') { $expectedPage = [int]$Matches[1] }
            elseif ($phase.name -match '^return-(\d+)$') { $expectedPage = $pages.Count - 1 - [int]$Matches[1] }
            if ($phaseSamples.Count -eq 0 -or $phaseSamples[-1].page -ne $expectedPage) {
                throw "Navigation/phase was not observed: $($run.name) $($phase.name)."
            }
            $inactivePhase = $InactiveOnly -or $phase.name -in 'hidden', 'minimized'
            if (-not $inactivePhase -and ($phaseSamples[-1].state -band 254) -ne 6) {
                throw "Visible/display-on/unoccluded/settled phase was not observed: $($run.name) $($phase.name)."
            }
            if ($phase.name -in 'cold-settled', 'returned-settled' -and -not $InactiveOnly -and
                @($phaseSamples | Where-Object { ($_.state -band 254) -ne 6 }).Count -ne 0) {
                throw 'The settled baseline contains inactive samples; repeat with the display continuously on.'
            }
            if ($phase.name -eq 'minimized') {
                # WM_SHOWWINDOW visibility remains true for an iconic window; WM_SIZE suspends its renderer.
                if (@($phaseSamples | Where-Object { ($_.state -band 8) -eq 0 }).Count -ne 0) {
                    throw 'The minimized measurement window was not suspended; repeat the run.'
                }
            }
            elseif ($inactivePhase -and @($phaseSamples | Where-Object { ($_.state -band 2) -ne 0 }).Count -ne 0) {
                throw 'An inactive measurement window became visible; repeat the run.'
            }
            $cpu = 0.0; $milliseconds = 0.0; $presents = 0.0
            foreach ($sample in $intervals) {
                $previous = $samples | Where-Object { $_.tick -lt $sample.tick } | Select-Object -Last 1
                if ($null -eq $previous -or ($previous.process[0] -band 2) -eq 0) { continue }
                if ($sample.page -ne $expectedPage -or $previous.page -ne $expectedPage -or
                    (-not $inactivePhase -and (($sample.state -band 254) -ne 6 -or ($previous.state -band 254) -ne 6))) { continue }
                $cpu += [double]$sample.process[5] - [double]$previous.process[5]
                $milliseconds += [double]$sample.ms
                $presents += [double]$sample.present[4]
            }
            $summary = [ordered]@{
                run = $run.name; phase = $phase.name; page = $run.pages[$expectedPage]; inactive = [bool]$inactivePhase;
                samples = $phaseSamples.Count; intervalMilliseconds = $milliseconds;
                privateMiB = [Math]::Round((Percentile ($phaseSamples | ForEach-Object { $_.process[1] }) 0.5) / 1MB, 2);
                workingSetMiB = [Math]::Round((Percentile ($phaseSamples | ForEach-Object { $_.process[2] }) 0.5) / 1MB, 2);
                peakCommitMiB = [Math]::Round(($phaseSamples | ForEach-Object { $_.process[3] } | Measure-Object -Maximum).Maximum / 1MB, 2);
                threads = Percentile ($phaseSamples | ForEach-Object { $_.process[7] }) 0.5;
                handles = Percentile ($phaseSamples | ForEach-Object { $_.process[6] }) 0.5;
                modules = Percentile ($phaseSamples | ForEach-Object { $_.process[10] }) 0.5;
                coreCpuPercent = if ($milliseconds -gt 0) { [Math]::Round($cpu / ($milliseconds * 100), 3) } else { $null };
                presentsPerSecond = if ($milliseconds -gt 0) { [Math]::Round($presents * 1000 / $milliseconds, 3) } else { $null };
                queryMicrosecondsP95 = Percentile ($phaseSamples | ForEach-Object { $_.sample_us }) 0.95;
                avSurfaceBytes = Percentile ($phaseSamples | Where-Object { $_.av[0] -eq 0 } | ForEach-Object { $_.av[4] }) 0.5;
                localUsageMiB = if (@($phaseSamples | Where-Object { $_.local[0] -eq 0 }).Count -gt 0) {
                    [Math]::Round((Percentile ($phaseSamples | Where-Object { $_.local[0] -eq 0 } | ForEach-Object { $_.local[2] }) 0.5) / 1MB, 2)
                } else { $null }
            }
            if ($milliseconds -eq 0 -or ($inactivePhase -and $presents -ne 0)) {
                throw "Inactive probe presented or lacks evidence: $($phase.name)."
            }
            $summaries.Add($summary)
        }
    }
    Write-Json (Join-Path $OutputRoot 'samples.json') $records.ToArray()
    Write-Json (Join-Path $OutputRoot 'summary.json') $summaries.ToArray()
    Write-Json (Join-Path $OutputRoot 'outcome.json') @{ accepted = $true; reason = 'All requested phases and identities passed.' }
    # Raw logs remain the authority; JSON summary is a convenience, never a replacement for validity bits.
    $summaries | ForEach-Object { [pscustomobject]$_ } | Format-Table run, phase, page, privateMiB, threads, coreCpuPercent, presentsPerSecond
    Write-Host "Receipts retained in $OutputRoot"
}
catch {
    $failure = $_
    Write-Json (Join-Path $OutputRoot 'runs.json') $runs.ToArray()
    if ($identityBefore.Count -gt 0) {
        Write-Json (Join-Path $OutputRoot 'identity-after.json') @(Hash-Inputs)
    }
    Write-Json (Join-Path $OutputRoot 'outcome.json') @{ accepted = $false; reason = $failure.Exception.Message }
    throw $failure
}
finally {
    if ($oldExecutionState -ne 0) { [void][RedXeRuntimeMeasurement]::SetThreadExecutionState($oldExecutionState) }
    [void][RedXeRuntimeMeasurement]::SetThreadDpiAwarenessContext($oldDpi)
}
