# Execute actual routing/publication statements with recording boundaries; no native launch or SDK acceptance.
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$file=Join-Path $root 'test.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($file,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Runtime command syntax is invalid.'}
$commands=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -ceq 'Open-CiLocalSdkApplicationRuntimeInput'},$true))
if($commands.Count -ne 1){throw 'Expected one original shared runtime admission.'}
function Find-RedXeMSBuild {'C:/synthetic-not-launched/MSBuild.exe'}
function Open-CiLocalSdkApplicationRuntimeInput {
    param($ProjectRoot,$BuildReceipt,$ExpectedBuildSha256,$MSBuildPath)
    $script:received=$PSBoundParameters
    [pscustomobject]@{Build=@{executable='C:/synthetic-not-launched/RedXe.exe'}}
}
$repoRoot=$root;$LocalSdkBuildReceipt='C:/synthetic-not-read/record.json';$LocalSdkBuildSha256='a'*64
$result=& ([scriptblock]::Create($commands[0].Extent.Text))
if($received.ProjectRoot -cne $root -or $received.BuildReceipt -cne $LocalSdkBuildReceipt -or $received.ExpectedBuildSha256 -cne $LocalSdkBuildSha256 -or $result.Build.executable -cne 'C:/synthetic-not-launched/RedXe.exe'){throw 'Runtime routing changed original record/root/digest.'}
$saved=$env:CI_NATIVE_BUILDS_DISABLED
try{
    $env:CI_NATIVE_BUILDS_DISABLED='true';$refused=$false
    try{& $file -Configuration Release -SkipBuild -Suites HostSmoke -LocalSdkBuildReceipt $LocalSdkBuildReceipt -LocalSdkBuildSha256 $LocalSdkBuildSha256}
    catch{if($_.Exception.Message -notmatch 'requires local Windows native execution'){throw};$refused=$true}
    if(-not $refused){throw 'Hosted/compile-free runtime reached discovery.'}
}finally{$env:CI_NATIVE_BUILDS_DISABLED=$saved}
$scopeGuards=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.Contains("throw 'Local SDK runtime requires the original build record/digest")},$true))
if($scopeGuards.Count -ne 1){throw 'Expected one original runtime scope admission.'}
# Evaluate the actual pre-discovery scope guard in every host, including hosted
# compiler-free validation. No CI markers are cleared and no discovery can run.
foreach($arguments in @(@{Suites=@('Settings')},@{Suites=@('HostSmoke');Full=$true},@{Suites=@('HostSmoke');SkipBuild=$false},@{Suites=@('HostSmoke');Configuration='Debug'})){
    $localSdkMode=$true;$Configuration='Release';$SkipBuild=$true;$Suites=@('HostSmoke');$Full=$false;$Rebuild=$false
    foreach($key in $arguments.Keys){Set-Variable -Name $key -Value $arguments[$key]}
    $refused=$false
    try{. ([scriptblock]::Create($scopeGuards[0].Extent.Text))}catch{if($_.Exception.Message -notmatch 'requires the original build record/digest'){throw};$refused=$true}
    if(-not $refused){throw 'Unsupported runtime scope reached discovery.'}
}
Write-Host 'PASS actual runtime admission binding and early hosted/unsupported-scope refusals; no native execution.'
$global:LASTEXITCODE=0
