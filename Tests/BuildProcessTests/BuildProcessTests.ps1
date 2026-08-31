[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$modulePath = Join-Path $repoRoot 'Build\BuildOutputProcess.psm1'
Import-Module $modulePath -Force -ErrorAction Stop

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
    if (Test-Path -LiteralPath $resolvedCleanupTarget) {
        Remove-Item -LiteralPath $resolvedCleanupTarget -Recurse -Force
    }
}

Write-Host 'Build-output process preflight tests passed.' -ForegroundColor Green
