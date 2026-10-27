<#
.SYNOPSIS Verify the required default MSVC tools, provisioning an isolated VS Insiders instance on hosted CI if needed.
.PARAMETER VerifyOnly Read-only local verification; never downloads or installs software.
#>
[CmdletBinding()]
param([ValidateSet('x64','ARM64')][string]$Platform='x64',[switch]$VerifyOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
[xml]$policy=Get-Content -LiteralPath (Join-Path $root 'Directory.Build.props') -Raw
$node=$policy.SelectSingleNode('//RequiredMsvcToolsVersion')
if($null-eq $node){throw 'Missing required MSVC family in Directory.Build.props.'}
$required=[string]$node.InnerText
if($required -notmatch '^14\.\d+$'){throw 'Missing required MSVC family in Directory.Build.props.'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
function Get-SelectedTools {
    $installation=(& $vswhere -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild -property installationPath|Select-Object -First 1)
    if(-not $installation){return $null}
    $default=Join-Path $installation 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'
    if(-not(Test-Path -LiteralPath $default)){return $null}
    $version=([IO.File]::ReadAllText($default)).Trim()
    if($version -notmatch ('^'+[regex]::Escape($required)+'\.\d+(?:\.\d+)?$')){return $null}
    $nativeHost=if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture-eq 'Arm64'){'arm64'}else{'x64'}
    $tools=Join-Path $installation "VC/Tools/MSVC/$version"
    foreach($file in @("bin/Host$nativeHost/$Platform/cl.exe","bin/Host$nativeHost/$Platform/link.exe")){
        if(-not(Test-Path -LiteralPath (Join-Path $tools $file))){return $null}
    }
    $asan=if($Platform-eq 'ARM64'){'clang_rt.asan_dynamic-aarch64.lib'}else{'clang_rt.asan_dynamic-x86_64.lib'}
    if(-not(Test-Path -LiteralPath (Join-Path $tools "lib/$Platform/$asan"))){return $null}
    if(-not(Test-Path -LiteralPath (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Include/10.0.26100.0/um/Windows.h'))){return $null}
    if($Platform-eq 'x64' -and -not(Test-Path -LiteralPath (Join-Path $installation 'VC/Tools/Llvm/x64/bin/llvm-pdbutil.exe'))){return $null}
    return [ordered]@{installation=$installation;version=$version;host=$nativeHost;platform=$Platform}
}
$selected=Get-SelectedTools
if(-not $selected){
    if($VerifyOnly){throw "The discovered installation lacks default MSVC $required.x, target/ASan/SDK/LLVM tools for $Platform. No installation attempted."}
    if($env:GITHUB_ACTIONS-ne 'true'-or $env:RUNNER_ENVIRONMENT-ne 'github-hosted'-or -not $env:RUNNER_TEMP){throw 'Automatic provisioning is restricted to GitHub-hosted runners. Install the required tools locally, then use -VerifyOnly.'}
    $scratch=Join-Path $env:RUNNER_TEMP ('msvc-'+$required+'-'+[guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($scratch)
    if($env:GITHUB_OUTPUT){
        Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "installer_logs=$scratch"
        Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value ('artifact_id='+[IO.Path]::GetFileName($scratch))
    }
    $installer=Join-Path $scratch 'vs_BuildTools.exe'
    Invoke-WebRequest 'https://aka.ms/vs/18/insiders/vs_buildtools.exe' -OutFile $installer
    $signature=Get-AuthenticodeSignature -LiteralPath $installer
    if($signature.Status-ne 'Valid'-or $signature.SignerCertificate.Subject-notmatch 'O=Microsoft Corporation(?:,|$)'){throw 'The Visual Studio bootstrapper does not have a valid Microsoft signature.'}
    $installRoot=Join-Path $env:RUNNER_TEMP ('VS-Msvc-'+$required)
    $arguments=@('--quiet','--wait','--norestart','--nocache','--installPath',('"'+$installRoot+'"'),
        '--add','Microsoft.VisualStudio.Workload.VCTools','--includeRecommended',
        '--add','Microsoft.VisualStudio.Component.VC.Tools.ARM64',
        '--add','Microsoft.VisualStudio.Component.VC.Preview.Tools.x86.x64',
        '--add','Microsoft.VisualStudio.Component.VC.Preview.Tools.ARM64',
        '--add','Microsoft.VisualStudio.Component.VC.ASAN',
        '--add','Microsoft.VisualStudio.Component.VC.Llvm.Clang',
        '--add','Microsoft.VisualStudio.Component.Windows11SDK.26100')
    $stdout=Join-Path $scratch 'bootstrapper.stdout.log'
    $stderr=Join-Path $scratch 'bootstrapper.stderr.log'
    Write-Host "Provisioning required MSVC $required at '$installRoot'; bootstrapper output: $scratch"
    # Windows GUI applications launched with & can return before completion and leave
    # LASTEXITCODE unset/stale. Both the bootstrapper's --wait and PowerShell's -Wait
    # are required; use the owned process result, never ambient native-command state.
    $installerStarted=Get-Date
    $process=Start-Process -FilePath $installer -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    try {$installerExit=$process.ExitCode}finally{$process.Dispose()}
    Write-Host "Visual Studio bootstrapper exit code: $installerExit"
    foreach($log in @($stdout,$stderr)){
        if(Test-Path -LiteralPath $log){Get-Content -LiteralPath $log -Tail 40 | Write-Host}
    }
    # VS writes detailed dd_* logs to TEMP, which need not equal RUNNER_TEMP.
    foreach($directory in @($env:TEMP,$env:RUNNER_TEMP)|Where-Object {$_}|Select-Object -Unique){
        Get-ChildItem -LiteralPath $directory -File -Filter 'dd_*.log' -ErrorAction SilentlyContinue |
            Where-Object LastWriteTime -ge $installerStarted |
            ForEach-Object {
                Write-Host "Visual Studio installer log: $($_.FullName)"
                try {Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $scratch $_.Name)}
                catch {Write-Warning "Cannot archive installer log '$($_.TargetObject)': $($_.Exception.Message)"}
            }
    }
    if($installerExit-notin @(0,3010)){throw "VS provisioning failed: $installerExit. Bootstrapper output remains at '$scratch'; detailed installer logs are listed above."}
    $selected=Get-SelectedTools
    if(-not $selected){throw "Insiders did not provide default MSVC $required.x and every required target tool. Review channel/component availability; another compiler is never accepted."}
}
Write-Host ('Verified required toolchain: '+($selected|ConvertTo-Json -Compress))
$global:LASTEXITCODE=0
