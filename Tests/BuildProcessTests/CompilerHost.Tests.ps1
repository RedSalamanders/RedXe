[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
Import-Module (Join-Path $root 'Build/DxUiRestore.psm1') -Force
if ((Get-RedXeDefaultCompilerHost -OperatingSystemArchitecture Arm64) -cne 'arm64') { throw 'ARM64 OS must use its native compiler host.' }
if ((Get-RedXeDefaultCompilerHost -OperatingSystemArchitecture X64) -cne 'x64') { throw 'x64 OS must use its native compiler host.' }
$msbuild = Find-RedXeMSBuild
$previous = [Environment]::GetEnvironmentVariable('PreferredToolArchitecture','Process')
try {
    [Environment]::SetEnvironmentVariable('PreferredToolArchitecture',$null,'Process')
    $cases = @(
        @{name='native-arm64'; process='ARM64'; wow=''; expected='arm64'},
        @{name='native-x64'; process='AMD64'; wow=''; expected='x64'},
        @{name='x64-emulated-on-arm64'; process='AMD64'; wow='ARM64'; expected='arm64'},
        @{name='x86-on-x64'; process='x86'; wow='AMD64'; expected='x64'},
        @{name='explicit-x64-on-arm64'; process='ARM64'; wow=''; preferred='x64'; expected='x64'},
        @{name='explicit-x86-on-x64'; process='AMD64'; wow=''; preferred='x86'; expected='x86'}
    )
    foreach ($case in $cases) {
        $arguments = @((Join-Path $root 'Directory.Build.props'),'/nologo','/p:Platform=ARM64','/p:Configuration=Debug',
            ('/p:PROCESSOR_ARCHITECTURE='+$case.process),('/p:PROCESSOR_ARCHITEW6432='+$case.wow),'-getProperty:PreferredToolArchitecture')
        if ($case.ContainsKey('preferred')) { $arguments += '/p:PreferredToolArchitecture='+$case.preferred }
        $actual = (@(& $msbuild @arguments) -join [Environment]::NewLine).Trim()
        if ($LASTEXITCODE -ne 0 -or $actual -cne $case.expected) { throw "Compiler host $($case.name): expected $($case.expected), got '$actual'." }
        Write-Host "PASS compiler host $($case.name)"
    }
} finally {
    [Environment]::SetEnvironmentVariable('PreferredToolArchitecture',$previous,'Process')
}
Write-Host 'PASS native OS policy and six effective MSBuild host/override scenarios.'
