Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-BuildExecutablePath {
    param(
        [Parameter(Mandatory)]
        [string] $Path
    )

    try {
        return [IO.Path]::GetFullPath($Path)
    }
    catch {
        throw "Build canceled because target-output process safety could not normalize executable path '$Path'. No process was terminated. $($_.Exception.Message)"
    }
}

function Assert-BuildOutputProcessNotRunning {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $ProcessName,

        [Parameter(Mandatory)]
        [string] $ExpectedExecutablePath
    )

    $expectedFullPath = Resolve-BuildExecutablePath -Path $ExpectedExecutablePath
    if (-not [string]::Equals(
            [IO.Path]::GetFileName($expectedFullPath),
            $ProcessName,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Build canceled because process name '$ProcessName' does not match target executable '$expectedFullPath'. No process was terminated."
    }

    $escapedProcessName = $ProcessName.Replace("'", "''")
    try {
        $namedProcesses = @(Get-CimInstance Win32_Process -Filter "Name='$escapedProcessName'" -ErrorAction Stop)
    }
    catch {
        throw ("Build canceled because target-output process safety could not be verified for '$expectedFullPath': " +
               "unable to enumerate '$ProcessName' process metadata. No process was terminated. " +
               $_.Exception.Message)
    }

    $matchingProcesses = @()
    foreach ($candidate in $namedProcesses) {
        if (-not $candidate.ExecutablePath) {
            continue
        }

        try {
            $candidatePath = [IO.Path]::GetFullPath([string] $candidate.ExecutablePath)
        }
        catch {
            continue
        }
        if ([string]::Equals($candidatePath, $expectedFullPath, [StringComparison]::OrdinalIgnoreCase)) {
            $matchingProcesses += [pscustomobject]@{
                ProcessId = [uint32] $candidate.ProcessId
                ExecutablePath = $candidatePath
                CommandLine = $candidate.CommandLine
            }
        }
    }

    if ($matchingProcesses.Count -gt 0) {
        $diagnostics = @($matchingProcesses | ForEach-Object {
            $commandLine = if ([string]::IsNullOrWhiteSpace($_.CommandLine)) {
                '<unavailable>'
            }
            else {
                $_.CommandLine
            }
            "  PID=$($_.ProcessId); Path='$($_.ExecutablePath)'; CommandLine='$commandLine'"
        })
        throw ("Build canceled because an independently launched process is using the exact target output. " +
               "It was not terminated because it is not proven to belong to this build operation; close it and retry.`n" +
               ($diagnostics -join "`n"))
    }
}

Export-ModuleMember -Function Assert-BuildOutputProcessNotRunning
