# Actual command routing at a recording boundary; no tool discovery, compilation or input.
[CmdletBinding()]param([string]$NativeMsBuildPath='')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$file=Join-Path $root 'Tools/Build-LocalSdkCandidate.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($file,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Local SDK command syntax is invalid.'}
$contracts=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq '$buildContract'},$true))
$calls=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq '$result'},$true))
if($contracts.Count -ne 1 -or $calls.Count -ne 1){throw 'Expected one literal application contract and broker invocation.'}
function Find-RedXeMSBuild {'C:/synthetic-not-launched/MSBuild.exe'}
function Invoke-CiLocalSdkApplicationBuild {
    param($ProjectRoot,$Descriptor,$ExpectedDescriptorSha256,$Platform,$MSBuildPath,$BuildContract,$BuildNumber)
    $script:received=$PSBoundParameters
    [pscustomobject]@{Role='LocalUnpinnedApplicationBuild';Sentinel='original broker result'}
}
$Descriptor='C:/synthetic-not-read/local snapshot.json';$DescriptorSha256='a'*64;$Platform='x64';$BuildNumber=123
. ([scriptblock]::Create($contracts[0].Extent.Text))
. ([scriptblock]::Create($calls[0].Extent.Text))
if($received.ProjectRoot -cne $root -or $received.Descriptor -cne $Descriptor -or $received.ExpectedDescriptorSha256 -cne $DescriptorSha256 -or $received.Platform -cne 'x64' -or $received.BuildNumber -ne 123 -or $result.Sentinel -cne 'original broker result'){throw 'Actual local SDK routing changed source, descriptor, profile, version or result.'}
if($received.BuildContract.Count -ne 9 -or $received.BuildContract.sdkProperty -cne 'RedXeDxUiSdkRoot' -or $received.BuildContract.outputProperty -cne 'RedXeOutputRoot' -or $received.BuildContract.metadataProject -cne 'RedXe/RedXe.vcxproj' -or $received.BuildContract.target -cne 'RedXe' -or $received.BuildContract.testsProperty -cne ''){throw 'Actual application SDK/owned-output binding changed.'}
$saved=$env:CI_NATIVE_BUILDS_DISABLED
try{
    $env:CI_NATIVE_BUILDS_DISABLED='true';$refused=$false
    try{& $file -Descriptor $Descriptor -DescriptorSha256 $DescriptorSha256}catch{if($_.Exception.Message -notmatch 'require local Windows native execution'){throw};$refused=$true}
    if(-not $refused){throw 'Compiler-free local SDK command reached discovery.'}
}finally{$env:CI_NATIVE_BUILDS_DISABLED=$saved}
$global:LASTEXITCODE=0
Write-Host 'PASS actual local SDK command binding and compiler-free early refusal; no native execution.'
if($NativeMsBuildPath){
    if(-not $IsWindows -or $env:GITHUB_ACTIONS -ceq 'true' -or $env:CI_NATIVE_BUILDS_DISABLED -ceq 'true'){throw 'Explicit MSBuild target controls require local Windows native execution.'}
    $fixture=Join-Path $root ('.build/local-sdk-output-guard/'+[guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($fixture)
    $source=[xml](Get-Content (Join-Path $root 'Directory.Build.targets') -Raw)
    $guard=@($source.Project.Target|Where-Object Name -ceq 'ValidateRedXeOutputRoot')
    if($guard.Count -ne 1){throw 'Expected the actual output-root guard.'}
    $project=Join-Path $fixture 'OutputGuard.proj'
    [IO.File]::WriteAllText($project,('<Project>'+$guard[0].OuterXml+'<Target Name="RedXeRequireLocalSdkOutputLease"><Error Text="ACTUAL_LOCAL_LEASE_GUARD_REACHED" /></Target></Project>'))
    function Invoke-OutputGuard([string]$OutputRoot,[string]$Contract,[bool]$LocalSdk){
        $arguments=@($project,'/nologo','/v:quiet','/t:ValidateRedXeOutputRoot',('/p:RedXeOutputRoot='+$OutputRoot),('/p:CiSdkContract='+$Contract))
        if($LocalSdk){$arguments+=@('/p:RedXeDxUiSdkRoot=synthetic-sdk','/p:RedXeCiEngineRoot=synthetic-engine')}
        $text=(& $NativeMsBuildPath @arguments 2>&1|Out-String)
        return @{exit=$LASTEXITCODE;text=$text}
    }
    $canonical=Invoke-OutputGuard (Join-Path $fixture '.build/') '' $false
    if($canonical.exit -ne 0){throw 'Canonical outputs unexpectedly entered the local-only guard.'}
    foreach($contract in @('','QualifiedAbiV2')){
        $refusal=Invoke-OutputGuard (Join-Path $fixture 'other/') $contract $true
        if($refusal.exit -eq 0 -or $refusal.text -notmatch 'Separate RedXe output roots require'){throw 'A canonical or downgraded build reached separate outputs.'}
    }
    $local=Invoke-OutputGuard (Join-Path $fixture 'other/') 'LocalUnpinnedAbiV1' $true
    if($local.exit -eq 0 -or $local.text -notmatch 'ACTUAL_LOCAL_LEASE_GUARD_REACHED'){throw 'Actual alternate output target did not delegate to the existing live SDK lease guard.'}
    if(Test-Path (Join-Path $fixture 'other')){throw 'Read-only output target controls created artifacts.'}
    Write-Host 'PASS actual MSBuild output-root guard preserves canonical paths and delegates local ownership before writes.'
    # A solution's plugin projects can override the root default independently.
    # Evaluate every owned native project, without invoking a target or a compiler.
    $sdk=Join-Path $fixture 'sdk'
    [void][IO.Directory]::CreateDirectory($sdk)
    [IO.File]::WriteAllText((Join-Path $sdk 'DxUi.Sdk.props'),'<Project><PropertyGroup><DxUiSdkRoot>$(MSBuildThisFileDirectory)</DxUiSdkRoot></PropertyGroup></Project>')
    $isolated=Join-Path $fixture 'candidate'
    $projects=@(& git -C $root ls-files -- '*.vcxproj')
    if($LASTEXITCODE -or -not $projects.Count){throw 'Cannot enumerate owned native output consumers.'}
    foreach($relative in $projects){
        $metadata=& $NativeMsBuildPath (Join-Path $root $relative) '/nologo' '/v:quiet' '/p:Platform=x64' '/p:Configuration=Release' ('/p:RedXeOutputRoot='+$isolated+[IO.Path]::DirectorySeparatorChar) ('/p:RedXeDxUiSdkRoot='+$sdk) ('/p:RedXeCiEngineRoot='+(Join-Path $root '.ci-engine')) '-getProperty:OutDir,IntDir'
        if($LASTEXITCODE){throw ('Native output evaluation failed: '+$relative)}
        $paths=($metadata -join "`n"|ConvertFrom-Json).Properties
        foreach($name in @('OutDir','IntDir')){
            if(-not [IO.Path]::GetFullPath($paths.$name).StartsWith(($isolated+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw ('A native project redirects local candidate outputs: '+$relative+'/'+$name)}
        }
    }
    if(Test-Path -LiteralPath $isolated){throw 'Metadata-only output inventory created candidate artifacts.'}
    Write-Host ('PASS actual isolated output metadata for all '+$projects.Count+' owned native projects; no compilation or ABI acceptance.')
}
$global:LASTEXITCODE=0
