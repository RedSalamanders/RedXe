#Requires -Version 7.2
# Real imported compiler/linker profiles, pinned archives, and deliberately broken controls; no compilation or desktop use.
[CmdletBinding()]param([string]$Root='', [switch]$RequirePinnedArchives)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if(-not $Root){$Root=(& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()}
$Root=[IO.Path]::GetFullPath($Root)
if(-not $IsWindows){Write-Host 'SKIP Release build-policy evaluation: Windows MSVC required; native qualification remains open.';return}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=(& $vswhere -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild -property installationPath).Trim()
$msbuild=Join-Path $installation 'MSBuild/Current/Bin/amd64/MSBuild.exe'
if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()-eq 'Arm64'){$msbuild=Join-Path $installation 'MSBuild/Current/Bin/arm64/MSBuild.exe'}
if(-not(Test-Path -LiteralPath $msbuild)){throw 'Install Visual Studio 2026 with MSVC 14.52 C++ tools.'}
$scratch=Join-Path $Root ('.build/ReleaseBuildPolicy/'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($scratch)
$nativePolicyFile=Join-Path $Root 'Build/NativeBuildPolicy.targets'
$dependencySource=''
$pinPath=Join-Path $Root 'Dependencies/DxUi.lock.json'
if(Test-Path -LiteralPath $pinPath){
    $pin=Get-Content -LiteralPath $pinPath -Raw|ConvertFrom-Json
    $candidateSource=Join-Path $Root ".build/dependencies/DxUi/source/$($pin.commit.Substring(0,16))"
    if(Test-Path -LiteralPath (Join-Path $candidateSource 'src/DxUi.vcxproj')){$dependencySource=$candidateSource}
    elseif($RequirePinnedArchives){throw 'Restore the exact DxUi source before required archive-policy evaluation.'}
    else{Write-Host 'Pinned archive evaluation pending: restore the exact DxUi source and rerun with -RequirePinnedArchives.'}
}
$probe=Join-Path $scratch 'Read.targets'
$probeXml='<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><Import Project="{0}" Condition="&apos;$(NativeBuildPolicyImported)&apos;!=&apos;true&apos;"/><ItemGroup><Link Include="ReleasePolicyLinkProbe"/></ItemGroup></Project>'
[IO.File]::WriteAllText($probe,($probeXml -f [Security.SecurityElement]::Escape($nativePolicyFile)))
function Read-Profile([string]$Project,[string]$Configuration,[string]$Platform,[string]$ProbePath=$probe,[string[]]$Extra=@()){
    # Retain the real entrypoint's forced policy import and then add the observer.
    # Extra arguments must not replace the observer or silently skip the binding.
    $forced=@($Extra|Where-Object{$_ -like '/p:ForceImportAfterCppTargets=*'})
    if($forced.Count){
        $binding=$forced[-1].Substring('/p:ForceImportAfterCppTargets='.Length)
        $chainPath=Join-Path $scratch ([guid]::NewGuid().ToString('N')+'.targets')
        $chainXml='<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><Import Project="'+[Security.SecurityElement]::Escape($binding)+'"/><Import Project="'+[Security.SecurityElement]::Escape($ProbePath)+'"/></Project>'
        [IO.File]::WriteAllText($chainPath,$chainXml)
        $ProbePath=$chainPath
        $Extra=@($Extra|Where-Object{$_ -notlike '/p:ForceImportAfterCppTargets=*'})
    }
    $arguments=@($Project,'/nologo',"/p:Configuration=$Configuration","/p:Platform=$Platform","/p:ForceImportAfterCppTargets=$ProbePath",'-getItem:ClCompile,Link,ProjectReference','-getProperty:ConfigurationType,VCToolsVersion,WholeProgramOptimization,LinkIncremental,Platform,PreferredToolArchitecture')+$Extra
    if($dependencySource){$arguments+="/p:DxUiRoot=$dependencySource"}
    $output=@(& $msbuild @arguments 2>&1)
    if($LASTEXITCODE-ne 0){throw "Evaluation failed: $Project $Configuration $Platform`n$($output -join "`n")"}
    return ($output -join "`n")|ConvertFrom-Json -AsHashtable
}
function Assert-Profile($Value,[string]$Configuration,[string]$Label,[switch]$PinnedArchive){
    if($Value.Properties.VCToolsVersion-notmatch '^14[.]52[.]\d+$'){throw "$Label selected unsupported MSVC $($Value.Properties.VCToolsVersion)."}
    foreach($item in $Value.Items.ClCompile){
        if($item['ExcludedFromBuild']-eq 'true'){continue}
        $armLtcgNotice=@($item['DisableSpecificWarnings'] -split '[;, ]+') -contains '4746'
        if($armLtcgNotice -ne ($Value.Properties.Platform -eq 'ARM64' -and $Configuration -eq 'Release')){
            throw "$Label has an incorrectly scoped ARM64 LTCG volatile-model notice exception."
        }
        if($item['WarningLevel']-ne 'EnableAllWarnings' -or $item['TreatWarningAsError']-ne 'false'){throw "$Label overrides /Wall without /WX."}
        if($Configuration-eq 'Debug' -and $item['DebugInformationFormat']-ne 'EditAndContinue'){throw "$Label does not use Debug /ZI."}
        if($Configuration-eq 'ASan Debug' -and $item['DebugInformationFormat']-ne 'ProgramDatabase'){throw "$Label does not use ASan /Zi."}
        if($item['AdditionalOptions']-match '(?i)(?:^|\s)/(?:W[0-4]|WX)(?:\s|$)'){throw "$Label overrides the warning policy."}
        if($Configuration-eq 'Release'){
            $packaged=$item['FunctionLevelLinking']-eq 'true' -or ($PinnedArchive -and $item['FunctionLevelLinking']-ne 'false' -and $item['Optimization']-in @('MaxSpeed','MinSpace'))
            if(-not $packaged){throw "$Label $($item.Identity) disables function-level linking."}
            if($item['WholeProgramOptimization']-ne 'true'){throw "$Label $($item.Identity) disables /GL."}
            if($item['AdditionalOptions']-match '(?i)(?:^|\s)/(?:GL-|Gy-)(?:\s|$)'){throw "$Label overrides the Release compiler policy."}
        }elseif($item['WholeProgramOptimization']-eq 'true'){throw "$Label unexpectedly enables /GL in $Configuration."}
    }
    $link=@($Value.Items.Link|Where-Object Identity -eq 'ReleasePolicyLinkProbe')[0]
    $folding=if($Configuration-eq 'Release'){'true'}else{'false'}
    if($link['EnableCOMDATFolding']-ne $folding){throw "$Label has incorrect COMDAT folding for $Configuration."}
    if(($Configuration-eq 'Release' -and $link['AdditionalOptions']-match '(?i)/OPT:NOICF') -or
       ($Configuration-ne 'Release' -and $link['AdditionalOptions']-match '(?i)/OPT:ICF(?:[,\s]|$)')){throw "$Label overrides COMDAT folding."}
    if($Configuration-eq 'Release' -and $Value.Properties.ConfigurationType-in @('Application','DynamicLibrary')){
        if($link['OptimizeReferences']-ne 'true'){throw "$Label disables /OPT:REF."}
        if($link['LinkTimeCodeGeneration']-ne 'UseLinkTimeCodeGeneration'){throw "$Label does not use full /LTCG."}
        if($Value.Properties.LinkIncremental-eq 'true'){throw "$Label enables incremental linking with full LTCG."}
        if($link['AdditionalOptions']-match '(?i)/(?:OPT:NOREF|LTCG:OFF|INCREMENTAL(?:\s|$))'){throw "$Label overrides the Release linker policy."}
    }
    if($Configuration-ne 'Release' -and $link['LinkTimeCodeGeneration']-notin @($null,'','Default')){throw "$Label unexpectedly enables Release LTCG in $Configuration."}
}
$projects=@(& git -C $Root ls-files -- '*.vcxproj')
if($LASTEXITCODE-ne 0){throw 'Cannot enumerate tracked native projects.'}
$checked=0;$archiveChecked=@{};$first=$null
foreach($relative in $projects){
    $path=Join-Path $Root $relative
    [xml]$xml=Get-Content -LiteralPath $path -Raw
    if(-not $xml.SelectNodes('//*[local-name()="ClCompile"][@Include]').Count){continue}
    foreach($configuration in 'Debug','Release','ASan Debug'){foreach($platform in 'x64','ARM64'){
        $value=Read-Profile $path $configuration $platform
        Assert-Profile $value $configuration "$relative $platform $configuration"
        if($platform-eq 'ARM64'){
            $expectedHost=if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()-eq 'Arm64'){'arm64'}else{'x64'}
            if($value.Properties.PreferredToolArchitecture-ne $expectedHost){throw "$relative uses an unqualified ARM64 compiler host."}
        }
        if($configuration-eq 'Release' -and $platform-eq 'x64' -and -not $first){$first=$path}
        foreach($reference in $value.Items.ProjectReference){
            if($reference.Identity-notmatch '[\\/]src[\\/]DxUi[.]vcxproj$'){continue}
            $archive=[IO.Path]::GetFullPath([IO.Path]::Combine([IO.Path]::GetDirectoryName($path),$reference.Identity))
            if($archive-eq [IO.Path]::GetFullPath((Join-Path $Root 'src/DxUi.vcxproj'))){continue}
            $properties=@{}
            foreach($pair in ([string]$reference['AdditionalProperties']).Split(';',[StringSplitOptions]::RemoveEmptyEntries)){
                $parts=$pair.Split('=',2);if($parts.Count-eq 2){$properties[$parts[0]]=$parts[1]}
            }
            $expected=if($configuration-eq 'Release'){'true'}else{'false'}
            if($properties['ForceImportAfterCppTargets']-ne (Join-Path $Root 'Build/PinnedDxUiPolicy.targets')){throw "Pinned archive is not bound to the native build policy."}
            if($properties['WholeProgramOptimization']-ne $expected -or $properties['VCToolsVersion']-ne $value.Properties.VCToolsVersion){throw "$relative does not forward exact toolset and WPO to pinned DxUi."}
            $key="$archive|$configuration|$platform"
            if(-not $archiveChecked.ContainsKey($key)){
                $extra=@("/p:WholeProgramOptimization=$expected","/p:VCToolsVersion=$($properties['VCToolsVersion'])","/p:ForceImportAfterCppTargets=$($properties['ForceImportAfterCppTargets'])")
                if($platform-eq 'ARM64'){$extra+="/p:PreferredToolArchitecture=$($properties['PreferredToolArchitecture'])"}
                $child=Read-Profile $archive $configuration $platform -Extra $extra
                Assert-Profile $child $configuration "Pinned DxUi $platform $configuration" -PinnedArchive
                $archiveChecked[$key]=$true
            }
        }
        ++$checked
    }}
}
if(-not $first){throw 'No native Release profile was evaluated.'}
$negative=Join-Path $scratch 'Broken.targets'
$negativeXml='<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><Import Project="{0}" Condition="&apos;$(NativeBuildPolicyImported)&apos;!=&apos;true&apos;"/><ItemGroup><ClCompile Update="@(ClCompile)"><WholeProgramOptimization>false</WholeProgramOptimization></ClCompile><Link Include="ReleasePolicyLinkProbe"><OptimizeReferences>false</OptimizeReferences></Link></ItemGroup></Project>'
[IO.File]::WriteAllText($negative,($negativeXml -f [Security.SecurityElement]::Escape($nativePolicyFile)))
$rejected=$false
try{Assert-Profile (Read-Profile $first 'Release' 'x64' $negative) 'Release' 'deliberately broken profile'}catch{if($_.Exception.Message-notmatch 'disables /GL'){throw};$rejected=$true}
if(-not $rejected){throw 'The evaluated compiler-negative control was accepted.'}
$broken=Read-Profile $first 'Release' 'x64'
foreach($item in $broken.Items.ClCompile){$item['WholeProgramOptimization']='true'}
$broken.Properties.ConfigurationType='Application'
foreach($item in $broken.Items.Link){$item['OptimizeReferences']='false'}
$rejected=$false
try{Assert-Profile $broken 'Release' 'broken final link'}catch{if($_.Exception.Message-notmatch 'disables /OPT:REF'){throw};$rejected=$true}
if(-not $rejected){throw 'The linker-negative control was accepted.'}
if($RequirePinnedArchives -and (Test-Path -LiteralPath $pinPath) -and $archiveChecked.Count-ne 6){throw 'Required pinned archive coverage is incomplete.'}
Write-Host "PASS Release policy: $checked native profiles, $($archiveChecked.Count) pinned-archive profiles, compiler/linker negative controls."

# Provisioning must remain read-only on local hosts and refuse a missing required family
# before downloading or starting any installer. The ordinary selected installation also
# has to satisfy the same SDK/target/ASan/LLVM check that CI uses.
$ciSetup=Join-Path $Root '.github/actions/setup-msvc/Setup.ps1'
& pwsh -NoProfile -File $ciSetup -VerifyOnly -Platform x64
if($LASTEXITCODE-ne 0){throw 'Read-only CI toolchain verification failed.'}
$fixture=Join-Path $scratch 'ci-provisioning-refusal'
$fixtureScript=Join-Path $fixture '.github/actions/setup-msvc/Setup.ps1'
[void][IO.Directory]::CreateDirectory((Split-Path $fixtureScript))
Copy-Item -LiteralPath $ciSetup -Destination $fixtureScript
[IO.File]::WriteAllText((Join-Path $fixture 'Directory.Build.props'),'<Project><PropertyGroup><RequiredMsvcToolsVersion>14.99</RequiredMsvcToolsVersion></PropertyGroup></Project>')
foreach($readOnly in @($true,$false)){
    $start=[Diagnostics.ProcessStartInfo]::new('pwsh')
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    foreach($argument in @('-NoProfile','-File',$fixtureScript,'-Platform','x64')){$start.ArgumentList.Add($argument)}
    if($readOnly){$start.ArgumentList.Add('-VerifyOnly')}
    $start.Environment['GITHUB_ACTIONS']='false'
    $process=[Diagnostics.Process]::new();$process.StartInfo=$start
    try{
        [void]$process.Start();$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        if(-not $process.WaitForExit(30000)){$process.Kill($true);throw 'Owned CI refusal control timed out.'}
        $message=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
        $expected=if($readOnly){'discovered installation lacks default MSVC 14.99'}else{'Automatic provisioning is restricted'}
        if($process.ExitCode-eq 0-or $message-notmatch $expected){throw "CI provisioning refusal control failed: $message"}
    }finally{$process.Dispose()}
}
Write-Host 'PASS CI toolchain verification and missing-family/local-install refusal controls.'
# build.ps1 and restore-dxui.ps1 select the compiler host only through the shared pair: the native OS host unless the
# caller chose one, with the caller's value restored after success and failure. Never restores or builds anything.
Import-Module (Join-Path $Root 'Build/DxUiRestore.psm1') -Force
$nativeHost=Get-RedXeDefaultCompilerHost
$original=[Environment]::GetEnvironmentVariable('PreferredToolArchitecture','Process')
# $null alone reaches .NET as an empty value; NullString removes the variable.
function Set-CallerCompilerHost($Value){[Environment]::SetEnvironmentVariable('PreferredToolArchitecture',$(if($null-eq $Value){[Management.Automation.Language.NullString]::Value}else{$Value}),'Process')}
try{
    foreach($case in @(@{caller=$null;fail=$false},@{caller='';fail=$false},@{caller='x64';fail=$false},@{caller='x86';fail=$false},@{caller=$null;fail=$true},@{caller='x86';fail=$true})){
        Set-CallerCompilerHost $case.caller
        $selected=$null;$caught=$false
        try{
            $state=Enter-RedXeCompilerHost
            try{
                $selected=[Environment]::GetEnvironmentVariable('PreferredToolArchitecture','Process')
                if($case.fail){throw 'controlled build failure'}
            }finally{Exit-RedXeCompilerHost -State $state}
        }catch{if($_.Exception.Message-cne 'controlled build failure'){throw};$caught=$true}
        if($caught-ne $case.fail){throw 'A build failure was hidden or invented.'}
        $wanted=if($case.caller){$case.caller}else{$nativeHost}
        if($selected-cne $wanted){throw "Caller '$($case.caller)' built with compiler host '$selected' instead of '$wanted'."}
        $after=[Environment]::GetEnvironmentVariable('PreferredToolArchitecture','Process')
        if(($null-eq $case.caller-and $null-ne $after)-or ($null-ne $case.caller-and $after-cne $case.caller)){throw "The caller compiler-host selection '$($case.caller)' was not restored (now '$after')."}
    }
}finally{Set-CallerCompilerHost $original}
foreach($script in @('build.ps1','restore-dxui.ps1')){
    $scriptTokens=$null;$scriptErrors=$null
    $scriptAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $Root $script),[ref]$scriptTokens,[ref]$scriptErrors)
    if($scriptErrors.Count){throw "$script has syntax errors."}
    $enter=@($scriptAst.FindAll({param($node)$node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Enter-RedXeCompilerHost'},$true))
    # The restore is captured when the host is selected: a module re-importing DxUiRestore with -Force removes the
    # script's copy before its finally block runs.
    $capture=@($scriptAst.FindAll({param($node)$node -is [Management.Automation.Language.AssignmentStatementAst] -and
        $node.Left.Extent.Text -ceq '$exitCompilerHost' -and $node.Right.Extent.Text -ceq 'Get-Command Exit-RedXeCompilerHost'},$true))
    $restore=@($scriptAst.FindAll({param($node)$node -is [Management.Automation.Language.TryStatementAst] -and $node.Finally -and
        $node.Finally.Extent.Text.Contains('& $exitCompilerHost -State $compilerHost')},$true))
    if($enter.Count-ne 1-or $capture.Count-ne 1-or $restore.Count-ne 1){throw "$script must select the compiler host once, capture its restore and run it in one finally block."}
    if($scriptAst.Extent.Text -match "SetEnvironmentVariable\(\s*'PreferredToolArchitecture'|\`$env:PreferredToolArchitecture\s*="){throw "$script sets the compiler host outside the shared pair."}
}
Write-Host 'PASS compiler-host selection and caller restoration on success/failure; build.ps1 and restore-dxui.ps1 use only the shared pair.'
# Every family guard reads the one RequiredMsvcToolsVersion and refuses another compiler family.
if((Get-RedXeRequiredMsvcFamily -RepoRoot $Root)-notmatch '^\d+\.\d+$'){throw 'The required MSVC family is not major.minor.'}
$familyFixture=Join-Path $scratch 'required-family'
[void][IO.Directory]::CreateDirectory($familyFixture)
foreach($bad in @('','14','14.52.1','latest')){
    [IO.File]::WriteAllText((Join-Path $familyFixture 'Directory.Build.props'),"<Project><PropertyGroup><RequiredMsvcToolsVersion>$bad</RequiredMsvcToolsVersion></PropertyGroup></Project>")
    $rejected=$false
    try{[void](Get-RedXeRequiredMsvcFamily -RepoRoot $familyFixture)}catch{if($_.Exception.Message-notmatch 'does not name a required MSVC family'){throw};$rejected=$true}
    if(-not $rejected){throw "A malformed required family '$bad' was accepted."}
}
$required=Get-RedXeRequiredMsvcFamily -RepoRoot $Root
foreach($case in @(@{version="$required.37025";ok=$true},@{version="$required.1.2";ok=$true},@{version="${required}0.1";ok=$false},@{version=($required -replace '\.\d+$','.5')+'.1';ok=$false},@{version=$required;ok=$false},@{version='';ok=$false})){
    if((Test-RedXeMsvcFamily -Required $required -Version $case.version)-ne $case.ok){throw "The shared family check misjudged '$($case.version)' against $required."}
}
# restore-dxui.ps1 and vcpkg-install.ps1 judge the selected compiler with the same policy read and predicate.
foreach($script in @('restore-dxui.ps1','vcpkg-install.ps1')){
    $text=[IO.File]::ReadAllText((Join-Path $Root $script))
    if(([regex]::Matches($text,'Get-RedXeRequiredMsvcFamily')).Count-ne 1-or ([regex]::Matches($text,'Test-RedXeMsvcFamily')).Count-ne 1-or $text.Contains('RequiredMsvcToolsVersion')){
        throw "$script must read the required family and judge the compiler only through the shared module."
    }
}
$wrongFamily=($required -replace '\d+$',{[int]$_.Value-1})+'.37000'
$familyResult=@(& $msbuild $first '/nologo' '/v:minimal' '/t:ValidateMsvcToolsVersion' '/p:Configuration=Release' '/p:Platform=x64' "/p:VCToolsVersion=$wrongFamily" 2>&1)
if($LASTEXITCODE-eq 0-or ($familyResult -join "`n")-notmatch "requires MSVC build tools $([regex]::Escape($required))\.x; selected $([regex]::Escape($wrongFamily))"){
    throw "ValidateMsvcToolsVersion accepted MSVC $wrongFamily`: $($familyResult -join ' ')"
}
$global:LASTEXITCODE=0
$familyResult=@(& $msbuild $first '/nologo' '/v:minimal' '/t:ValidateMsvcToolsVersion' '/p:Configuration=Release' '/p:Platform=x64' "/p:VCToolsVersion=$required.37025" 2>&1)
if($LASTEXITCODE-ne 0){throw "ValidateMsvcToolsVersion refused the required family $required`: $($familyResult -join ' ')"}
Write-Host "PASS required MSVC $required family: malformed policy and MSVC $wrongFamily refused, $required.x accepted by the MSBuild guard."

# Independently broken real metadata must not pass because LTO settings are valid.
foreach($control in @(
    @{name='warning';configuration='Release';metadata='<ClCompile Update="@(ClCompile)"><WarningLevel>Level4</WarningLevel></ClCompile>';message='/Wall without /WX'},
    @{name='debug-symbols';configuration='Debug';metadata='<ClCompile Update="@(ClCompile)"><DebugInformationFormat>ProgramDatabase</DebugInformationFormat></ClCompile>';message='Debug /ZI'},
    @{name='folding';configuration='Release';metadata='<Link Update="ReleasePolicyLinkProbe"><EnableCOMDATFolding>false</EnableCOMDATFolding></Link>';message='COMDAT folding'}
)){
    $brokenPath=Join-Path $scratch ($control.name+'.targets')
    $controlXml='<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><Import Project="'+[Security.SecurityElement]::Escape($nativePolicyFile)+'" Condition="&apos;$(NativeBuildPolicyImported)&apos;!=&apos;true&apos;"/><ItemGroup><Link Include="ReleasePolicyLinkProbe"/>'+$control.metadata+'</ItemGroup></Project>'
    [IO.File]::WriteAllText($brokenPath,$controlXml)
    $rejected=$false
    try{Assert-Profile (Read-Profile $first $control.configuration 'x64' $brokenPath) $control.configuration $control.name}
    catch{if($_.Exception.Message-notlike ('*'+$control.message+'*')){throw};$rejected=$true}
    if(-not $rejected){throw "Broken $($control.name) policy was accepted."}
}
Write-Host 'PASS /Wall without /WX, Debug/ASan symbols, Release-only ICF and independent negative controls.'
