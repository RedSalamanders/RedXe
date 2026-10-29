<#
.SYNOPSIS
Exercises offline RedMonitor client verification with isolated immutable-file fixtures.
.DESCRIPTION
Every control runs under PowerShell 7 and under Windows PowerShell 5.1, which the MSBuild gate uses. The MSBuild
controls run the RequirePinnedRedMonitorClient target itself: it passes, skips when nothing changed, and fails on a
tampered client.
#>
[CmdletBinding()]
param([string]$RepositoryRoot)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if(-not $RepositoryRoot){
 $RepositoryRoot=$PSScriptRoot
 while(-not(Test-Path -LiteralPath (Join-Path $RepositoryRoot 'Dependencies/RedMonitor.lock.json'))){
  $parent=Split-Path $RepositoryRoot
  if(-not $parent -or $parent -eq $RepositoryRoot){throw 'Cannot locate consumer root.'}
  $RepositoryRoot=$parent
 }
}
$verify=Join-Path $RepositoryRoot 'Verify-RedMonitorClient.ps1'
if(-not(Test-Path -LiteralPath $verify)){$verify=Join-Path $RepositoryRoot 'Tools/Verify-RedMonitorClient.ps1'}
$packet=Join-Path $RepositoryRoot ('.build/client-sdk-tests/Unicode 日 space/'+[guid]::NewGuid().ToString('N'))
$fixture=Join-Path $packet 'consumer'
[IO.Directory]::CreateDirectory((Join-Path $fixture 'Dependencies'))|Out-Null
[IO.Directory]::CreateDirectory((Join-Path $fixture 'ThirdParty'))|Out-Null
Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'ThirdParty/RedMonitor') -Destination (Join-Path $fixture 'ThirdParty') -Recurse
$lockPath=Join-Path $fixture 'Dependencies/RedMonitor.lock.json'
$baseline=[IO.File]::ReadAllText((Join-Path $RepositoryRoot 'Dependencies/RedMonitor.lock.json'))
[IO.File]::WriteAllText($lockPath,$baseline)
$hosts=[ordered]@{pwsh=(Get-Process -Id $PID).Path;powershell=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'}
$results=[Collections.Generic.List[object]]::new()
function Invoke-Owned([string]$Executable,[string[]]$Arguments,[string]$WorkingDirectory,[string]$Log){
 $start=[Diagnostics.ProcessStartInfo]::new($Executable)
 foreach($arg in $Arguments){$start.ArgumentList.Add($arg)}
 $start.Environment['PATH']=Join-Path $env:SystemRoot 'System32'
 $start.WorkingDirectory=$WorkingDirectory
 $start.UseShellExecute=$false;$start.CreateNoWindow=$true
 $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 $process=[Diagnostics.Process]::new();$process.StartInfo=$start
 try{
  if(-not $process.Start()){throw 'Cannot start verification control.'}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  if(-not $process.WaitForExit(60000)){$process.Kill($true);$process.WaitForExit();throw 'Owned verification control timed out.'}
  $text=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
  [IO.File]::WriteAllText($Log,$text)
  return [pscustomobject]@{Exit=$process.ExitCode;Text=$text}
 }finally{$process.Dispose()}
}
function Invoke-Control([bool]$Pass,[string]$Name,[string]$Root=$fixture,[string]$Location=$packet,[string[]]$Expect=@()){
 foreach($hostName in $hosts.Keys){
  # The location is set inside the host, so a relative -RepositoryRoot must follow it rather than the process folder.
  $command="Set-Location -LiteralPath '$($Location.Replace("'","''"))'; & '$($verify.Replace("'","''"))'"+$(if($Root){" -RepositoryRoot '$($Root.Replace("'","''"))'"}else{''})
  $arguments=@('-NoProfile','-NonInteractive')+$(if($hostName -eq 'powershell'){@('-ExecutionPolicy','Bypass')}else{@()})+@('-Command',$command)
  $result=Invoke-Owned $hosts[$hostName] $arguments $env:SystemRoot (Join-Path $packet "$Name.$hostName.log")
  $results.Add([pscustomobject]@{name=$Name;host=$hostName;expectedSuccess=$Pass;exitCode=$result.Exit})
  if(($result.Exit -eq 0) -ne $Pass){throw "Client control failed under ${hostName}: $Name`n$($result.Text)"}
  foreach($needle in $Expect){if(-not $result.Text.Contains($needle)){throw "Client control $Name under $hostName does not name '$needle':`n$($result.Text)"}}
  if(Test-Path -LiteralPath (Join-Path $fixture '.build')){throw 'Verification created build/cache output.'}
  Write-Host "PASS RedMonitor client $Name ($hostName)"
 }
}
Invoke-Control $true 'offline-exact-bytes'
Invoke-Control $true 'relative-root-follows-location' -Root 'consumer'
# Without -RepositoryRoot the script's own checkout is verified, whatever the caller's location (read-only).
Invoke-Control $true 'default-root-is-script-folder' -Root ''
foreach($relative in @('include/RedMonitor/Client.h','LICENSE.txt')){
 $path=Join-Path $fixture ('ThirdParty/RedMonitor/'+$relative)
 $bytes=[IO.File]::ReadAllBytes($path)
 [IO.File]::AppendAllText($path,'corrupt')
 Invoke-Control $false ('tampered-'+[IO.Path]::GetFileName($path))
 [IO.File]::WriteAllBytes($path,$bytes)
 Move-Item -LiteralPath $path -Destination ($path+'.saved')
 Invoke-Control $false ('missing-'+[IO.Path]::GetFileName($path))
 Move-Item -LiteralPath ($path+'.saved') -Destination $path
}
foreach($field in @('repository','commit','apiRevision')){
 $pin=$baseline|ConvertFrom-Json
 $pin.$field=if($field -eq 'apiRevision'){99}else{'foreign'}
 $pin|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $lockPath
 Invoke-Control $false ('invalid-'+$field)
}
foreach($kind in @('duplicate','traversal','hash')){
 $pin=$baseline|ConvertFrom-Json
 switch($kind){
  duplicate {$pin.files[1]=$pin.files[0]}
  traversal {$pin.files[0].path='../Client.h'}
  hash {$pin.files[0].sha256='invalid'}
 }
 $pin|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $lockPath
 Invoke-Control $false ('invalid-'+$kind)
}
[IO.File]::WriteAllText($lockPath,$baseline)
# A checkout reached through a junction above its root (a link or synced folder the user chose) is accepted.
$holder=Join-Path $packet 'holder'
[IO.Directory]::CreateDirectory($holder)|Out-Null
Copy-Item -LiteralPath $fixture -Destination $holder -Recurse
$linked=Join-Path $packet 'linked'
New-Item -ItemType Junction -Path $linked -Target $holder|Out-Null
Invoke-Control $true 'junction-above-root' -Root (Join-Path $linked 'consumer')
# The MSBuild gate: the real props file and verifier in a consumer-shaped fixture whose project is named RedXe.
Import-Module (Join-Path $RepositoryRoot 'Build/DxUiRestore.psm1') -Force
$msbuild=Find-RedXeMSBuild
$gate=Join-Path $packet 'msbuild'
Copy-Item -LiteralPath $fixture -Destination $gate -Recurse
[IO.Directory]::CreateDirectory((Join-Path $gate 'Build'))|Out-Null
Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'Build/RedMonitor.props') -Destination (Join-Path $gate 'Build')
Copy-Item -LiteralPath $verify -Destination $gate
# As in the product, the project sits in a subfolder, so Exec's working directory is not the checkout root.
[IO.Directory]::CreateDirectory((Join-Path $gate 'RedXe'))|Out-Null
[IO.File]::WriteAllText((Join-Path $gate 'RedXe/RedXe.proj'),'<Project><PropertyGroup><IntDir>obj\</IntDir></PropertyGroup><Import Project="..\Build\RedMonitor.props" /><Target Name="PrepareForBuild" /></Project>')
function Invoke-Gate([bool]$Pass,[string]$Name,[bool]$Skipped=$false){
 $result=Invoke-Owned $msbuild @((Join-Path $gate 'RedXe/RedXe.proj'),'-nologo','-nr:false','-v:normal','-t:PrepareForBuild') (Join-Path $gate 'RedXe') (Join-Path $packet "$Name.msbuild.log")
 $results.Add([pscustomobject]@{name=$Name;host='msbuild';expectedSuccess=$Pass;exitCode=$result.Exit})
 if(($result.Exit -eq 0) -ne $Pass){throw "MSBuild gate control failed: $Name`n$($result.Text)"}
 $ran=$result.Text.Contains('Verified local RedMonitor client')
 if($Pass -and $ran -eq $Skipped){throw "MSBuild gate control $Name expected the verifier to $(if($Skipped){'be skipped'}else{'run'}):`n$($result.Text)"}
 Write-Host "PASS RedMonitor MSBuild gate $Name"
}
Invoke-Gate $true 'gate-verifies-once'
Invoke-Gate $true 'gate-skips-unchanged-inputs' -Skipped $true
$client=Join-Path $gate 'ThirdParty/RedMonitor/include/RedMonitor/Client.h'
$bytes=[IO.File]::ReadAllBytes($client)
[IO.File]::AppendAllText($client,'corrupt')
Invoke-Gate $false 'gate-refuses-tampered-client'
Invoke-Gate $false 'gate-keeps-refusing-without-a-stamp-update'
[IO.File]::WriteAllBytes($client,$bytes)
Invoke-Gate $true 'gate-reverifies-restored-client'
$vendor=Join-Path $fixture 'ThirdParty/RedMonitor'
Move-Item -LiteralPath $vendor -Destination ($vendor+'.saved')
New-Item -ItemType Junction -Path $vendor -Target ($vendor+'.saved')|Out-Null
# The refusal names the linked folder itself (its ASCII tail: the host's redirected output may not carry the fixture's U+65E5).
Invoke-Control $false 'reparse-vendor' -Expect @('Reparse RedMonitor client path:','consumer\ThirdParty\RedMonitor')
# Retain the entire fixture, including its contained reparse controls, for diagnosis.
$results|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $packet 'receipt.json')
Write-Host "PASS $($results.Count) offline client controls; retained $packet"
