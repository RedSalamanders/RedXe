<#!
.SYNOPSIS
Formats the RedXe C++ sources with clang-format.
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSCommandPath
$command = Get-Command 'clang-format.exe' -ErrorAction SilentlyContinue
$clangFormat = if ($command) { $command.Source } else { $null }

if (-not $clangFormat) {
    $candidates = @(
        (Join-Path $env:ProgramFiles 'LLVM\bin\clang-format.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\18\Insiders\VC\Tools\Llvm\x64\bin\clang-format.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-format.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin\clang-format.exe')
    )
    $clangFormat = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}

if (-not $clangFormat) {
    throw 'clang-format was not found. Install the Visual Studio C++ Clang tools component or LLVM.'
}

$sourceFiles = Get-ChildItem -LiteralPath (Join-Path $repoRoot 'src') -Recurse -File |
    Where-Object { $_.Extension -in @('.cpp', '.h') } |
    Sort-Object FullName

if ($sourceFiles.Count -eq 0) {
    throw 'No C++ source files were found.'
}

& $clangFormat -i --style=file @($sourceFiles.FullName)
if ($LASTEXITCODE -ne 0) {
    throw "clang-format failed with exit code $LASTEXITCODE."
}

Write-Host "Formatted $($sourceFiles.Count) files." -ForegroundColor Green
