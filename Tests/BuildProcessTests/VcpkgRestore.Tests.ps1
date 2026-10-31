[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot)
Import-Module (Join-Path $root 'Build/VcpkgRestore.psm1') -Force
$scratch=Join-Path $root ('.build/VcpkgSourceTests/'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($scratch)
$oldGlobal=$env:GIT_CONFIG_GLOBAL;$oldSystem=$env:GIT_CONFIG_NOSYSTEM
$emptyConfig=Join-Path $scratch 'empty.gitconfig'
[IO.File]::WriteAllText($emptyConfig,'')
try {
$env:GIT_CONFIG_GLOBAL=$emptyConfig;$env:GIT_CONFIG_NOSYSTEM='1'
function Require([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Git([string]$Directory,[string[]]$Arguments){
    $text=@(& git.exe -c core.longpaths=true -C $Directory @Arguments 2>&1|ForEach-Object{[string]$_})
    if($LASTEXITCODE -ne 0){throw ($text -join [Environment]::NewLine)}
    return ($text -join [Environment]::NewLine).Trim()
}
function Refuses([scriptblock]$Body,[string]$Pattern){
    $refused=$false
    try{& $Body|Out-Null}catch{Require ($_.Exception.Message -match $Pattern) $_.Exception.Message;$refused=$true}
    Require $refused "Expected refusal: $Pattern"
}
$source=Join-Path $scratch 'upstream'
[void][IO.Directory]::CreateDirectory($source)
$null=Git $source @('init','--quiet','--initial-branch=main')
foreach($setting in @(@('user.name','Vcpkg Fixture'),@('user.email','fixture@example.invalid'),@('commit.gpgsign','false'),@('core.autocrlf','false'),@('core.longpaths','true'))){$null=Git $source @('config',$setting[0],$setting[1])}
[IO.File]::WriteAllText((Join-Path $source '.gitignore'),"vcpkg.exe`n")
[IO.File]::WriteAllText((Join-Path $source 'unchanged.txt'),'unchanged catalogue input')
[IO.File]::WriteAllText((Join-Path $source 'versions.txt'),'yyjson=0.12;curl=8.22')
$null=Git $source @('add','--all');$null=Git $source @('commit','--quiet','-m','Executable catalogue')
$toolCommit=Git $source @('rev-parse','HEAD')
[IO.File]::WriteAllText((Join-Path $source 'versions.txt'),'yyjson=0.13;curl=8.23')
$longRelative='ports/'+('x'*100)+'/'+('y'*100)+'.txt'
$longSource=Join-Path $source $longRelative
[void][IO.Directory]::CreateDirectory((Split-Path -Parent $longSource))
[IO.File]::WriteAllText($longSource,'long port path')
$null=Git $source @('add','--all');$null=Git $source @('commit','--quiet','-m','Ports catalogue')
$baseline=Git $source @('rev-parse','HEAD')
function Product([string]$Name){
    $path=Join-Path $scratch $Name
    [void][IO.Directory]::CreateDirectory($path)
    [IO.File]::WriteAllText((Join-Path $path 'vcpkg-tool.json'),(@{repository=$source;commit=$toolCommit}|ConvertTo-Json))
    [IO.File]::WriteAllText((Join-Path $path 'vcpkg.json'),(@{name='fixture';'builtin-baseline'=$baseline}|ConvertTo-Json))
    return $path
}
$product=Product 'independent'
$original=@{};foreach($name in @('vcpkg.json','vcpkg-tool.json')){$original[$name]=[IO.File]::ReadAllText((Join-Path $product $name))}
$before=$env:VCPKG_ROOT
try{
    $env:VCPKG_ROOT='fixture-caller-environment'
    $state=Initialize-RedXeVcpkgSources $product
    Require ((Git $state.ToolRoot @('rev-parse','HEAD')) -ceq $toolCommit) 'Executable revision changed.'
    Require ((Git $state.RegistryRoot @('rev-parse','HEAD')) -ceq $baseline) 'Ports revision is wrong.'
    Require ([IO.File]::ReadAllText((Join-Path $state.RegistryRoot 'versions.txt')) -ceq 'yyjson=0.13;curl=8.23') 'Requested versions unavailable.'
    Require ([IO.File]::ReadAllText((Join-Path $state.ToolRoot 'versions.txt')) -ceq 'yyjson=0.12;curl=8.22') 'Tool source silently upgraded.'
    Require ([IO.File]::ReadAllText((Join-Path $state.RegistryRoot 'unchanged.txt')) -ceq 'unchanged catalogue input') 'Unchanged catalogue entries were not materialized.'
    $again=Initialize-RedXeVcpkgSources $product
    Require ($again.RegistryRoot -ceq $state.RegistryRoot) 'Warm restore changed catalogue location.'
    Require ($env:VCPKG_ROOT -ceq 'fixture-caller-environment') 'Caller environment changed.'
    Require ((Git $source @('rev-parse','HEAD')) -ceq $baseline) 'Upstream checkout changed.'
    foreach($checkout in @($state.ToolRoot,$state.RegistryRoot)){
        Require ((Git $checkout @('config','--local','--get','core.longpaths')) -ceq 'true') 'Missing local long-path setting.'
        Require (-not (Test-Path -LiteralPath (Join-Path $checkout '.git/objects/info/alternates'))) 'Borrowed caller Git objects.'
    }
    foreach($name in $original.Keys){Require ([IO.File]::ReadAllText((Join-Path $product $name)) -ceq $original[$name]) 'Pin declaration changed.'}
    Write-Host 'PASS separate executable/catalogue revisions, requested versions, warm restore, pin/source/environment preservation.'
}finally{$env:VCPKG_ROOT=$before}
$null=Git $source @('config','uploadpack.allowFilter','true')
$null=Git $source @('config','uploadpack.allowAnySHA1InWant','true')
$product=Product 'partial-transport'
$sourceUri=([Uri]$source).AbsoluteUri
[IO.File]::WriteAllText((Join-Path $product 'vcpkg-tool.json'),(@{repository=$sourceUri;commit=$toolCommit}|ConvertTo-Json))
$state=Initialize-RedXeVcpkgSources $product
Require ((Git $state.ToolRoot @('config','--get','remote.origin.promisor')) -ceq 'true') 'Tool clone lost filtering configuration.'
Require ((Git $state.RegistryRoot @('config','--get','remote.origin.promisor')) -ceq 'true') 'Catalogue clone lost filtering configuration.'
Require ([IO.File]::ReadAllText((Join-Path $state.RegistryRoot 'versions.txt')) -ceq 'yyjson=0.13;curl=8.23') 'Filtered catalogue cannot retrieve baseline objects.'
Write-Host 'PASS real filtered transport retains promisor metadata and retrieves independent catalogue objects.'
foreach($role in @('ToolRoot','RegistryRoot')){
    $product=Product ('dirty-'+$role);$state=Initialize-RedXeVcpkgSources $product
    $file=Join-Path $state.$role 'versions.txt'
    [IO.File]::WriteAllText($file,'preserve local changes')
    Refuses {Initialize-RedXeVcpkgSources $product} 'local changes'
    Require ([IO.File]::ReadAllText($file) -ceq 'preserve local changes') 'Dirty source overwritten.'
    Write-Host "PASS dirty $role preserved."
}
$product=Product 'foreign';$state=Initialize-RedXeVcpkgSources $product
$null=Git $state.ToolRoot @('remote','set-url','origin','https://example.invalid/foreign')
Refuses {Initialize-RedXeVcpkgSources $product} 'origin differs'
Require ((Git $state.ToolRoot @('remote','get-url','origin')) -ceq 'https://example.invalid/foreign') 'Foreign origin rewritten.'
Write-Host 'PASS foreign origin refused without changing it.'
$product=Product 'occupied';$occupied=Join-Path $product '.build/vcpkg-tool'
[void][IO.Directory]::CreateDirectory($occupied);[IO.File]::WriteAllText((Join-Path $occupied 'keep.txt'),'preserve')
Refuses {Initialize-RedXeVcpkgSources $product} 'not an owned clone'
Require ([IO.File]::ReadAllText((Join-Path $occupied 'keep.txt')) -ceq 'preserve') 'Occupied path changed.'
Write-Host 'PASS occupied path preserved.'
foreach($bad in @('main',('0'*40),('a'*12))){
    $product=Product ('bad-'+[guid]::NewGuid().ToString('N'))
    [IO.File]::WriteAllText((Join-Path $product 'vcpkg.json'),(@{name='fixture';'builtin-baseline'=$bad}|ConvertTo-Json))
    Refuses {Initialize-RedXeVcpkgSources $product} 'full nonzero'
    Require (-not (Test-Path -LiteralPath (Join-Path $product '.build'))) 'Invalid pin created outputs.'
}
Write-Host 'PASS moving, zero and truncated identities rejected before mutation.'
$product=Product 'long-files';$state=Initialize-RedXeVcpkgSources $product
$longRestored=Join-Path $state.RegistryRoot $longRelative
Require ($longRestored.Length -gt 260) 'Long-path fixture is too short.'
Require ([IO.File]::ReadAllText($longRestored) -ceq 'long port path') 'Long-path checkout failed.'
Write-Host "PASS Git restores deep port files beyond MAX_PATH; fixtures retained at $scratch"
$installer=[IO.File]::ReadAllText((Join-Path $root 'vcpkg-install.ps1'))
Require ($installer.Contains('"--vcpkg-root=$registryRoot"')) 'Installer does not select the ports catalogue explicitly.'
Require ($installer.Contains('(Join-Path $registryRoot "triplets/$triplet.cmake")')) 'Overlay triplet comes from the wrong source role.'
Require (-not ($installer -match '\$env:VCPKG_ROOT\s*=')) 'Installer mutates caller root selection.'
Write-Host 'PASS installer binds the explicit catalogue and its triplet without an environment override.'
} finally {$env:GIT_CONFIG_GLOBAL=$oldGlobal;$env:GIT_CONFIG_NOSYSTEM=$oldSystem}
