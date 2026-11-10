<# .SYNOPSIS Executes the actual adapter build/package branches with controlled SDK availability; no native work. #>
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'Tools/Invoke-ProjectCi.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Adapter syntax failed.'}
$build=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.StartsWith("if (`$Phase -in @('ReleaseTestBuild','Qualification','Production'))")},$true))
$package=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.StartsWith("if (`$Phase -eq 'Production')") -and $n.Extent.Text.Contains("Run 'package.ps1'")},$true))
if($build.Count -ne 1 -or $package.Count -ne 1){throw 'Expected one actual build and production package branch.'}
$fixture=Join-Path $repo ('.build/hosted-sdk-controls/'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory((Join-Path $fixture 'Dependencies'))
[void][IO.Directory]::CreateDirectory((Join-Path $fixture '.build/packages'))
Copy-Item (Join-Path $repo 'Dependencies/DxUi.lock.json') (Join-Path $fixture 'Dependencies/DxUi.lock.json')
$stage=[scriptblock]::Create(@'
param($Phase,$Configuration,$Platform,$Available,$root,$Probe)
$profile=@('-Platform',$Platform,'-Configuration',$Configuration);$number='123';$EngineRoot='controlled-engine';$context=@{fixture=$true}
function Import-Module {param($Name) if($Name.EndsWith('DxUiRestore.psm1')){return};if(-not $Name.EndsWith('Ci.SdkPreparation.psm1')){throw 'Unexpected SDK module.'};$Probe.imports++}
function Initialize-CiPinnedSdkInput {
    param($Context,$SourceCommit,$ApiRevision)
    $Probe.prepares++;$Probe.source=$SourceCommit;$Probe.api=$ApiRevision
    if($Available){
        $descriptor=Join-Path $root '.build/descriptor.json'
        [IO.File]::WriteAllText($descriptor,(@{sourceRoot=(Join-Path $root '.build/sdk-prepared/source')}|ConvertTo-Json))
        return $descriptor
    }
}
function Read-RedXeDxUiLock {param($LockFile) return Get-Content $LockFile -Raw|ConvertFrom-Json}
function Get-RedXeDxUiSourcePath {
    param($RepoRoot,$Commit)
    if($Available -and $Configuration -ceq 'Release' -and $Phase -cin @('ReleaseTestBuild','Production')){throw 'An SDK hit must not require a separately restored source checkout.'}
    return Join-Path $RepoRoot '.build/pinned-source'
}
function Run {
    param($Script,$CommandArguments)
    if($Script.EndsWith('validate-build-matrix.ps1')){
        $expected=if($Available -and $Configuration -ceq 'Release' -and $Phase -cin @('ReleaseTestBuild','Production')){'.build/sdk-prepared/source/validate-build-matrix.ps1'}else{'.build/pinned-source/validate-build-matrix.ps1'}
        if($Script.Replace('\','/') -cne $expected){throw 'Build-matrix validation did not use the actual admitted SDK/source checkout.'}
    }
    $Probe.commands.Add(@{script=$Script;arguments=@($CommandArguments)})
}
function Write-CiProductionReceipt {param($Context,$Archives,$TestModeDisabled,$SmokePassed) $Probe.receipts++}
'@ + $build[0].Extent.Text + "`n" + $package[0].Extent.Text)
$count=0
foreach($phase in @('Static','ReleaseTestBuild','ReleaseTest','Production','Qualification')){
    foreach($configuration in @('Debug','Release','ASan Debug')){foreach($platform in @('x64','ARM64')){foreach($available in @($true,$false)){
        $probe=@{imports=0;prepares=0;source='';api=0;commands=[Collections.Generic.List[object]]::new();receipts=0}
        & $stage $phase $configuration $platform $available $fixture $probe
        $eligible=$configuration -ceq 'Release' -and $phase -cin @('ReleaseTestBuild','Production')
        if($probe.prepares -ne [int]$eligible -or $probe.imports -ne [int]$eligible){throw "Wrong SDK preparation role/profile: $phase/$configuration/$platform"}
        if($eligible -and ($probe.source -cne 'f2e328a0a26a18ca66bf4b2aea77bf010c9515e1' -or $probe.api -ne 4)){throw 'Adapter changed its actual lock.'}
        $expectedCommands=if($phase -ceq 'Production'){3}elseif($phase -cin @('ReleaseTestBuild','Qualification')){2}else{0}
        if($probe.commands.Count -ne $expectedCommands){throw 'Build/package command coverage changed.'}
        foreach($command in @($probe.commands|Where-Object{$_.script -cin @('build.ps1','package.ps1')})){
            $hasDescriptor='-DxUiSdkInputDescriptor' -cin $command.arguments
            if($hasDescriptor -ne ($eligible -and $available)){throw 'SDK hit/miss was not preserved through actual command arguments.'}
            if($hasDescriptor -and (Join-Path $fixture '.build/descriptor.json') -cnotin $command.arguments){throw 'SDK descriptor argument was split or substituted.'}
        }
        $count++
    }}}
}
Write-Host "PASS hosted SDK adapter: $count actual role/profile/architecture/availability combinations; no native qualification."
