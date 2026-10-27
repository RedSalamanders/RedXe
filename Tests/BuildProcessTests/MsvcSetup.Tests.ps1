#Requires -Version 7.2
# The delayed GUI fixture replaces download/signature/discovery boundaries; no VS installer runs.
[CmdletBinding()]param([string]$SetupScriptPath='')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$script:setupCases=0
function Invoke-TestCase([string]$Name,[scriptblock]$Body){
    & $Body
    ++$script:setupCases
    Write-Host "PASS $Name"
}
function Assert-True([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Assert-Equal($Expected,$Actual,[string]$Message){if($Expected -cne $Actual){throw "$Message; expected '$Expected', actual '$Actual'."}}
function Assert-Contains($Values,$Expected,[string]$Message){if($Expected -cnotin $Values){throw $Message}}
$repository=(& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
if(-not $SetupScriptPath){$SetupScriptPath=Join-Path $repository '.github/actions/setup-msvc/Setup.ps1'}
if(-not $IsWindows){Write-Host 'SKIP hosted MSVC GUI bootstrapper fixtures: Windows process semantics required.';return}
# Retain all attempts as setup evidence.
$packet=Join-Path $repository ('.build/msvc-setup-fixtures/'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($packet)
$source=Join-Path $packet 'FakeInstaller.cs'
[IO.File]::WriteAllText($source,@'
using System;
using System.IO;
using System.Threading;
class FakeInstaller {
    static int Main(string[] args) {
        string root=Environment.GetEnvironmentVariable("DXUI_SETUP_FIXTURE");
        if (args.Length>0 && args[0]=="-latest") {
            Console.WriteLine(Path.Combine(root,"VS instance"));
            return 0;
        }
        File.WriteAllLines(Path.Combine(root,"arguments.txt"),args);
        Thread.Sleep(400);
        string version=Environment.GetEnvironmentVariable("DXUI_FIXTURE_VERSION");
        if (!String.IsNullOrEmpty(version))
            File.WriteAllText(Path.Combine(root,"VS instance","VC","Auxiliary","Build","Microsoft.VCToolsVersion.default.txt"),version);
        File.WriteAllText(Path.Combine(root,"completed.txt"),"completed");
        File.WriteAllText(Path.Combine(Environment.GetEnvironmentVariable("TEMP"),"dd_fixture.log"),"fixture installer detail");
        Console.WriteLine("fixture bootstrapper stdout");
        Console.Error.WriteLine("fixture bootstrapper stderr");
        return Int32.Parse(Environment.GetEnvironmentVariable("DXUI_FIXTURE_EXIT"));
    }
}
'@)
$fake=Join-Path $packet 'FakeInstaller.exe'
& (Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe') /nologo /target:winexe /out:$fake $source
if($LASTEXITCODE-ne 0){throw 'Cannot compile the fake GUI bootstrapper.'}
$fakeDiscoveryExecutable=Join-Path $packet 'FakeDiscovery.exe'
& (Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe') /nologo /target:exe /out:$fakeDiscoveryExecutable $source
if($LASTEXITCODE-ne 0){throw 'Cannot compile the console discovery fixture.'}
function Invoke-SetupFixture([string]$Name,[int]$ExitCode=0,[string]$Version='14.52.37025',[switch]$Ready,[switch]$VerifyOnly,[switch]$Local,[switch]$BadSignature,[string]$Missing=''){
    $fixture=Join-Path $packet $Name
    $script=Join-Path $fixture '.github/actions/setup-msvc/Setup.ps1'
    [void][IO.Directory]::CreateDirectory((Split-Path $script))
    Copy-Item $SetupScriptPath $script
    $discovery=Join-Path $fixture 'Program Files/Microsoft Visual Studio/Installer/vswhere.exe'
    [void][IO.Directory]::CreateDirectory((Split-Path $discovery))
    Copy-Item -LiteralPath $fakeDiscoveryExecutable -Destination $discovery
    [IO.File]::WriteAllText((Join-Path $fixture 'Directory.Build.props'),'<Project><PropertyGroup><RequiredMsvcToolsVersion>14.52</RequiredMsvcToolsVersion></PropertyGroup></Project>')
    $installation=Join-Path $fixture 'VS instance'
    $hostArch=if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()-eq 'Arm64'){'arm64'}else{'x64'}
    $files=@("VC/Tools/MSVC/14.52.37025/bin/Host$hostArch/x64/cl.exe","VC/Tools/MSVC/14.52.37025/bin/Host$hostArch/x64/link.exe",'VC/Tools/MSVC/14.52.37025/lib/x64/clang_rt.asan_dynamic-x86_64.lib','VC/Tools/Llvm/x64/bin/llvm-pdbutil.exe','VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt')
    if($Missing-ne 'MSBuild'){$files+="MSBuild/Current/Bin/$(if($hostArch-eq 'arm64'){'arm64'}else{'amd64'})/MSBuild.exe"}
    foreach($file in $files){$path=Join-Path $installation $file;[void][IO.Directory]::CreateDirectory((Split-Path $path));if($file-ne $Missing){[IO.File]::WriteAllText($path,'')}}
    $sdk=Join-Path $fixture 'Program Files/Windows Kits/10/Include/10.0.26100.0/um/Windows.h'
    [void][IO.Directory]::CreateDirectory((Split-Path $sdk))
    if($Missing-ne 'SDK'){[IO.File]::WriteAllText($sdk,'')}
    if($Ready){[IO.File]::WriteAllText((Join-Path $installation 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'),'14.52.37025')}
    $wrapper=Join-Path $fixture 'Invoke.ps1'
    [IO.File]::WriteAllText($wrapper,@'
param([switch]$VerifyOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
function Invoke-WebRequest {param($Uri,$OutFile) Copy-Item $env:DXUI_FAKE_INSTALLER $OutFile;[IO.File]::WriteAllText((Join-Path $env:DXUI_SETUP_FIXTURE 'downloaded.txt'),$Uri)}
function Get-AuthenticodeSignature {param($LiteralPath) [pscustomobject]@{Status=$env:DXUI_FIXTURE_SIGNATURE;SignerCertificate=[pscustomobject]@{Subject='CN=Microsoft Corporation, O=Microsoft Corporation, C=US'}}}
# Success starts with LASTEXITCODE unset, as on hosted CI. Failure has stale success.
if($env:DXUI_FIXTURE_EXIT-ne '0'){$global:LASTEXITCODE=0}
& (Join-Path $PSScriptRoot '.github/actions/setup-msvc/Setup.ps1') -Platform x64 -VerifyOnly:$VerifyOnly
'@)
    $start=[Diagnostics.ProcessStartInfo]::new('pwsh')
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    foreach($argument in @('-NoProfile','-File',$wrapper)){$start.ArgumentList.Add($argument)}
    if($VerifyOnly){$start.ArgumentList.Add('-VerifyOnly')}
    $start.Environment['DXUI_SETUP_FIXTURE']=$fixture;$start.Environment['DXUI_FAKE_INSTALLER']=$fake
    $start.Environment['DXUI_FIXTURE_VERSION']=$Version;$start.Environment['DXUI_FIXTURE_EXIT']=[string]$ExitCode
    $start.Environment['DXUI_FIXTURE_SIGNATURE']=if($BadSignature){'NotSigned'}else{'Valid'}
    $start.Environment['ProgramFiles(x86)']=Join-Path $fixture 'Program Files'
    $start.Environment['GITHUB_ACTIONS']=if($Local){'false'}else{'true'}
    $start.Environment['RUNNER_ENVIRONMENT']='github-hosted';$start.Environment['RUNNER_TEMP']=Join-Path $fixture 'runner temp with spaces'
    $start.Environment['TEMP']=$start.Environment['RUNNER_TEMP']
    $start.Environment['GITHUB_OUTPUT']=Join-Path $fixture 'github-output.txt'
    $start.Environment['GITHUB_ENV']=Join-Path $fixture 'github-env.txt'
    # Model the older installation that wins ambient discovery on hosted images.
    $oldMSBuild=Join-Path $fixture 'old installation/MSBuild/Current/Bin/amd64/MSBuild.exe'
    [void][IO.Directory]::CreateDirectory((Split-Path $oldMSBuild))
    [IO.File]::WriteAllText($oldMSBuild,'old MSBuild fixture')
    $start.Environment['MSBUILD_EXE_PATH']=$oldMSBuild
    [void][IO.Directory]::CreateDirectory($start.Environment['RUNNER_TEMP'])
    $process=[Diagnostics.Process]::new();$process.StartInfo=$start
    try{
        [void]$process.Start();$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        if(-not $process.WaitForExit(30000)){$process.Kill($true);throw 'Owned fake setup timed out.'}
        $message=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $fixture 'result.log'),$message)
        return [pscustomobject]@{Exit=$process.ExitCode;Text=$message;Root=$fixture}
    }finally{$process.Dispose()}
}
Invoke-TestCase 'delayed GUI success waits and preserves install path with spaces' {
    $result=Invoke-SetupFixture 'success'
    Assert-Equal 0 $result.Exit $result.Text
    Assert-True (Test-Path (Join-Path $result.Root 'completed.txt')) 'GUI completed before verification'
    $arguments=[IO.File]::ReadAllLines((Join-Path $result.Root 'arguments.txt'))
    $index=[Array]::IndexOf($arguments,'--installPath')
    Assert-Equal (Join-Path $result.Root 'runner temp with spaces/VS-Msvc-14.52') $arguments[$index+1] 'install path remains one argument'
    Assert-Contains $arguments '--wait' 'bootstrapper waits for its installer'
}
Invoke-TestCase 'reboot-required success is accepted only after exact tools verify' {
    $result=Invoke-SetupFixture 'reboot' -ExitCode 3010
    Assert-Equal 0 $result.Exit $result.Text
    Assert-True ($result.Text-match 'exit code: 3010') 'explicit GUI exit reported'
}
Invoke-TestCase 'verified installation reaches actual build discovery ahead of an older ambient MSBuild' {
    $result=Invoke-SetupFixture 'build-context'
    Assert-Equal 0 $result.Exit $result.Text
    $environment=Join-Path $result.Root 'github-env.txt'
    Assert-True (Test-Path -LiteralPath $environment) 'No verified MSBuild handoff was published.'
    $exports=@([IO.File]::ReadAllLines($environment)|Where-Object {$_-like 'MSBUILD_EXE_PATH=*'})
    Assert-Equal 1 $exports.Count 'one exact MSBuild context is exported'
    $selected=$exports[0].Substring('MSBUILD_EXE_PATH='.Length)
    $hostArch=if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()-eq 'Arm64'){'arm64'}else{'amd64'}
    Assert-Equal (Join-Path $result.Root "VS instance/MSBuild/Current/Bin/$hostArch/MSBuild.exe") $selected 'MSBuild belongs to the verified installation and native host'
    $previous=[Environment]::GetEnvironmentVariable('MSBUILD_EXE_PATH','Process')
    try{
        $project=Split-Path $repository -Leaf
        switch($project){
            'RedXe' {Import-Module (Join-Path $repository 'Build/DxUiRestore.psm1') -Force;$discover='Find-RedXeMSBuild'}
            'RedPrism' {Import-Module (Join-Path $repository 'Build/VisualStudioDiscovery.psm1') -Force;$discover='Find-RedPrismMSBuild'}
            default {throw "Missing discovery owner for $project"}
        }
        $older=Join-Path $result.Root 'old installation/MSBuild/Current/Bin/amd64/MSBuild.exe'
        [Environment]::SetEnvironmentVariable('MSBUILD_EXE_PATH',$older,'Process')
        Assert-Equal $older (& $discover) 'the control reproduces older ambient discovery'
        [Environment]::SetEnvironmentVariable('MSBUILD_EXE_PATH',$selected,'Process')
        Assert-Equal $selected (& $discover) 'the actual product lookup consumes the setup handoff'
    }finally{
        if($null-eq $previous){[Environment]::SetEnvironmentVariable('MSBUILD_EXE_PATH',[Management.Automation.Language.NullString]::Value,'Process')}
        else{[Environment]::SetEnvironmentVariable('MSBUILD_EXE_PATH',$previous,'Process')}
    }
    Assert-True ([object]::Equals($previous,[Environment]::GetEnvironmentVariable('MSBUILD_EXE_PATH','Process'))) 'discovery control restores the caller build selection'
}
Invoke-TestCase 'GUI failure overrides stale success and retains output diagnostics' {
    $result=Invoke-SetupFixture 'failed' -ExitCode 1603
    Assert-True ($result.Exit-ne 0 -and $result.Text-match 'VS provisioning failed: 1603') $result.Text
    Assert-True ($result.Text-match 'Bootstrapper output remains') 'retained diagnostics named'
    $output=[IO.File]::ReadAllLines((Join-Path $result.Root 'github-output.txt'))
    $logs=($output|Where-Object {$_-like 'installer_logs=*'}).Substring('installer_logs='.Length)
    Assert-True (Test-Path (Join-Path $logs 'dd_fixture.log')) 'detailed installer log archived'
    Assert-True ($result.Text-match 'fixture bootstrapper stdout' -and $result.Text-match 'fixture bootstrapper stderr') 'both output tails reported'
}
Invoke-TestCase 'successful installer cannot admit another compiler family' {
    $result=Invoke-SetupFixture 'wrong-family' -Version '14.51.36231'
    Assert-True ($result.Exit-ne 0 -and $result.Text-match 'Insiders did not provide default MSVC 14.52') $result.Text
}
foreach($missing in @("VC/Tools/MSVC/14.52.37025/bin/Host$(if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()-eq 'Arm64'){'arm64'}else{'x64'})/x64/cl.exe",'VC/Tools/MSVC/14.52.37025/lib/x64/clang_rt.asan_dynamic-x86_64.lib','VC/Tools/Llvm/x64/bin/llvm-pdbutil.exe','SDK')){
    Invoke-TestCase "successful installer cannot admit missing $missing" {
        $result=Invoke-SetupFixture ('missing-'+[guid]::NewGuid().ToString('N')) -Missing $missing
        Assert-True ($result.Exit-ne 0 -and $result.Text-match 'Insiders did not provide') $result.Text
    }
}
Invoke-TestCase 'verified existing tools never download or install' {
    $result=Invoke-SetupFixture 'ready' -Ready -VerifyOnly
    Assert-Equal 0 $result.Exit $result.Text
    Assert-True (-not(Test-Path (Join-Path $result.Root 'downloaded.txt'))) 'no download'
    Assert-True (-not(Test-Path (Join-Path $result.Root 'github-env.txt'))) 'VerifyOnly does not publish or change build selection'
}
foreach($mode in @('VerifyOnly','Local','BadSignature')){
    Invoke-TestCase "$mode refuses before installer launch" {
        $options=@{};$options[$mode]=$true
        $result=Invoke-SetupFixture $mode @options
        Assert-True ($result.Exit-ne 0) $result.Text
        Assert-True (-not(Test-Path (Join-Path $result.Root 'completed.txt'))) 'installer never launched'
        if($mode-ne 'BadSignature'){Assert-True (-not(Test-Path (Join-Path $result.Root 'downloaded.txt'))) 'no download'}
    }
}
Invoke-TestCase 'successful installer cannot admit missing MSBuild' {
    $result=Invoke-SetupFixture 'missing-msbuild' -Missing MSBuild
    Assert-True ($result.Exit-ne 0 -and $result.Text-match 'Insiders did not provide') $result.Text
}
Write-Host "Retained MSVC setup fixtures: $packet"
Write-Host "PASS hosted MSVC setup: $script:setupCases behavioral cases. No real installer ran."
$global:LASTEXITCODE=0
