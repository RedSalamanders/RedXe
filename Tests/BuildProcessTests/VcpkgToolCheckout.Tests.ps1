<#
.SYNOPSIS Exercises Build/VcpkgToolCheckout.psm1 against a local file:// fixture: no network, no build.
.DESCRIPTION The pinned DxUi installer reuses a clean checkout of its vcpkg pin under its output root and otherwise makes a
partial clone that vcpkg cannot read. Covers a fresh complete clone, a rerun, completion of a shallow partial clone in place,
refusal of a foreign origin and of an occupied path, and offline extraction of an older tree as vcpkg performs it.
Fixtures live under .build/VcpkgToolCheckoutTests/<guid>; they are removed after a pass and retained after a failure.
#>
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot)
Import-Module (Join-Path $root 'Build/VcpkgToolCheckout.psm1') -Force
$prefix=[IO.Path]::GetFullPath((Join-Path $root '.build/VcpkgToolCheckoutTests'))+[IO.Path]::DirectorySeparatorChar
$scratch=Join-Path $prefix ([guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($scratch)
$oldGlobal=$env:GIT_CONFIG_GLOBAL;$oldSystem=$env:GIT_CONFIG_NOSYSTEM
$emptyConfig=Join-Path $scratch 'empty.gitconfig'
[IO.File]::WriteAllText($emptyConfig,'')
$passed=$false
try {
$env:GIT_CONFIG_GLOBAL=$emptyConfig;$env:GIT_CONFIG_NOSYSTEM='1'
function Require([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Git([string]$Directory,[string[]]$Arguments){
    $text=@(& git.exe -c core.longpaths=true -C $Directory @Arguments 2>&1|ForEach-Object{[string]$_})
    if($LASTEXITCODE -ne 0){throw ($text -join [Environment]::NewLine)}
    return ($text -join [Environment]::NewLine).Trim()
}
function Get-LocalConfig([string]$Checkout,[string]$Name){$value=& git.exe -C $Checkout config --get $Name;$global:LASTEXITCODE=0;return [string]$value}
function Get-MissingObjectCount([string]$Checkout){@(& git.exe -C $Checkout rev-list --objects --missing=print --all|Where-Object{$_.StartsWith('?')}).Count}
# vcpkg's extraction: read-tree and checkout-index in an environment holding only GIT_INDEX_FILE. GIT_NO_LAZY_FETCH stands in for
# the missing network, since file:// needs none.
function Read-TreeOffline([string]$Checkout,[string]$Treeish,[string]$Name){
    $out=Join-Path $scratch ('offline-'+$Name);[void][IO.Directory]::CreateDirectory($out)
    $codes=foreach($arguments in @(@('read-tree',$Treeish),@('--work-tree',$out,'checkout-index','-af',"--prefix=$out/"))){
        $start=[Diagnostics.ProcessStartInfo]::new((Get-Command git.exe).Source)
        foreach($argument in @('--git-dir',(Join-Path $Checkout '.git'))+$arguments){$start.ArgumentList.Add($argument)}
        $start.UseShellExecute=$false;$start.RedirectStandardError=$true;$start.Environment.Clear()
        $start.Environment['GIT_INDEX_FILE']=Join-Path $scratch ($Name+'.index');$start.Environment['GIT_NO_LAZY_FETCH']='1'
        $process=[Diagnostics.Process]::Start($start);$null=$process.StandardError.ReadToEnd();$process.WaitForExit();$process.ExitCode
        $process.Dispose()
    }
    return (@($codes|Where-Object{$_ -ne 0}).Count -eq 0)
}
function Assert-Ready([string]$ToolRoot,[string]$Label){
    Require ((Git $ToolRoot @('remote','get-url','origin')) -ceq $sourceUri) "$Label`: origin is not the pinned repository."
    Require ((Git $ToolRoot @('rev-parse','HEAD')) -ceq $pinned) "$Label`: HEAD is not the pinned commit."
    Require (-not (Git $ToolRoot @('status','--porcelain'))) "$Label`: the checkout is not clean, so the pinned installer would refuse it."
    Require (-not (Get-LocalConfig $ToolRoot 'remote.origin.partialclonefilter')) "$Label`: the checkout is partial."
    Require ((Git $ToolRoot @('rev-parse','--is-shallow-repository')) -ceq 'false') "$Label`: the checkout is shallow."
    Require ((Get-MissingObjectCount $ToolRoot) -eq 0) "$Label`: objects are missing."
    Require (Read-TreeOffline $ToolRoot ($older+'^{tree}') $Label) "$Label`: an older port tree is not readable offline."
}
$source=Join-Path $scratch 'upstream'
[void][IO.Directory]::CreateDirectory($source)
$null=Git $source @('init','--quiet','--initial-branch=master')
foreach($setting in @(@('user.name','Vcpkg Fixture'),@('user.email','fixture@example.invalid'),@('commit.gpgsign','false'),@('core.autocrlf','false'),@('uploadpack.allowFilter','true'),@('uploadpack.allowAnySHA1InWant','true'))){$null=Git $source @('config',$setting[0],$setting[1])}
foreach($index in 1..5){
    [IO.File]::WriteAllText((Join-Path $source "port$index.txt"),"port $index")
    [IO.File]::WriteAllText((Join-Path $source 'shared.txt'),"shared $index")
    $null=Git $source @('add','--all');$null=Git $source @('commit','--quiet','-m',"Catalogue $index")
}
$pinned=Git $source @('rev-parse','HEAD~2')
$older=Git $source @('rev-parse','HEAD~3')
$sourceUri=([Uri]$source).AbsoluteUri
$identityPath=Join-Path $scratch 'vcpkg-tool.json'
[IO.File]::WriteAllText($identityPath,(@{repository=$sourceUri;commit=$pinned}|ConvertTo-Json))

$fresh=Join-Path $scratch 'fresh/vcpkg-tool'
Initialize-VcpkgToolCheckout -ToolIdentityPath $identityPath -ToolRoot $fresh
Assert-Ready $fresh 'fresh'
Initialize-VcpkgToolCheckout -ToolIdentityPath $identityPath -ToolRoot $fresh
Assert-Ready $fresh 'rerun'
Write-Host 'PASS a fresh output root gets a complete, clean checkout at the pin, and a rerun keeps it.'

# What a DxUi installer pinned before the fix leaves: a partial clone, made shallow by its depth-1 fetch of the pin.
$partial=Join-Path $scratch 'partial/vcpkg-tool'
[void][IO.Directory]::CreateDirectory((Split-Path -Parent $partial))
$null=Git $scratch @('clone','-c','core.longpaths=true','--filter=blob:none','--',$sourceUri,$partial)
$null=Git $partial @('fetch','--depth','1','origin',$pinned)
$null=Git $partial @('checkout','--detach',$pinned)
Require ((Git $partial @('rev-parse','--is-shallow-repository')) -ceq 'true') 'The old installer fixture is not shallow.'
Require (-not (Read-TreeOffline $partial ($older+'^{tree}') 'before-completion')) 'The partial fixture does not reproduce the offline failure.'
Initialize-VcpkgToolCheckout -ToolIdentityPath $identityPath -ToolRoot $partial
Assert-Ready $partial 'completed'
Write-Host 'PASS a shallow partial checkout is completed in place, HEAD and tree kept, and becomes readable offline.'

$foreign=Join-Path $scratch 'foreign/vcpkg-tool'
[void][IO.Directory]::CreateDirectory((Split-Path -Parent $foreign))
$null=Git $scratch @('clone','--quiet','--',$sourceUri,$foreign)
$null=Git $foreign @('remote','set-url','origin','https://example.invalid/other')
$refused=$false
try{Initialize-VcpkgToolCheckout -ToolIdentityPath $identityPath -ToolRoot $foreign}catch{$refused=$_.Exception.Message.Contains('does not use the pinned repository')}
Require $refused 'A checkout of another repository was accepted.'
Require ((Git $foreign @('remote','get-url','origin')) -ceq 'https://example.invalid/other') 'The foreign origin was rewritten.'
$occupied=Join-Path $scratch 'occupied/vcpkg-tool'
[void][IO.Directory]::CreateDirectory($occupied);[IO.File]::WriteAllText((Join-Path $occupied 'keep.txt'),'preserve')
$refused=$false
try{Initialize-VcpkgToolCheckout -ToolIdentityPath $identityPath -ToolRoot $occupied}catch{$refused=$_.Exception.Message.Contains('is not a Git checkout')}
Require ($refused -and [IO.File]::ReadAllText((Join-Path $occupied 'keep.txt')) -ceq 'preserve') 'An occupied path was not refused and preserved.'
foreach($bad in @('main',('a'*12))){
    $badIdentity=Join-Path $scratch ('bad-'+[guid]::NewGuid().ToString('N')+'.json')
    [IO.File]::WriteAllText($badIdentity,(@{repository=$sourceUri;commit=$bad}|ConvertTo-Json))
    $target=Join-Path $scratch ('bad-'+[guid]::NewGuid().ToString('N'))
    $refused=$false
    try{Initialize-VcpkgToolCheckout -ToolIdentityPath $badIdentity -ToolRoot $target}catch{$refused=$_.Exception.Message.Contains('full 40-character commit')}
    Require ($refused -and -not (Test-Path -LiteralPath $target)) "A moving or truncated pin ($bad) was not refused before any clone."
}
Write-Host 'PASS foreign origins, occupied paths and moving or truncated pins are refused without changes.'
$passed=$true
} finally {
    $env:GIT_CONFIG_GLOBAL=$oldGlobal;$env:GIT_CONFIG_NOSYSTEM=$oldSystem
    $path=[IO.Path]::GetFullPath($scratch)
    if(-not $path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe vcpkg checkout fixture cleanup path.'}
    if($passed){Remove-Item -LiteralPath $path -Recurse -Force}else{Write-Host "Fixtures retained at $path"}
}
Write-Host 'Vcpkg tool checkout tests passed.' -ForegroundColor Green
