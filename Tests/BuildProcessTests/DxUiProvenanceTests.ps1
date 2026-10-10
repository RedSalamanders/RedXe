[CmdletBinding()] param()
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repo 'Build/DxUiProvenance.psm1') -Force
$fixture=Join-Path $repo ('.build/BuildProcessTests/DxUi-'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path (Join-Path $fixture 'Plugins') -Force)
try {
    $lock=Join-Path $fixture 'lock.json'
    function Write-Lock([int]$ApiRevision) { @{repository='https://github.com/RedSalamanders/DxUi';commit=('a'*40);apiRevision=$ApiRevision;targets=@('DxUi')} | ConvertTo-Json | Set-Content -LiteralPath $lock }
    Write-Lock 3
    $modules=foreach ($name in @('RedXe.exe','Plugins/AVControl.dll','AVControlTests.exe')) {
        $path=Join-Path $fixture $name
        Set-Content -LiteralPath $path -Value "fixture $name"
        @{path=$name;sha256=(Get-FileHash -LiteralPath $path).Hash}
    }
    $record=@{repository='https://github.com/RedSalamanders/DxUi';commit=('a'*40);apiRevision=3;platform='x64';configuration='Debug';modules=@($modules)}
    function Write-Record { $record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $fixture 'DxUi.provenance.json') }
    function Reject([string]$Scenario) {
        $rejected=$false
        try { Assert-RedXeDxUiProvenance -OutputRoot $fixture -LockFile $lock -Platform x64 -Configuration Debug } catch { $rejected=$true }
        if (-not $rejected) { throw "Accepted stale provenance: $Scenario" }
        Write-Host "PASS provenance rejects $Scenario"
    }
    Write-Record
    Assert-RedXeDxUiProvenance -OutputRoot $fixture -LockFile $lock -Platform x64 -Configuration Debug
    $record.repository='https://example.invalid/fork'; Write-Record; Reject 'wrong repository'
    $record.repository='https://github.com/RedSalamanders/DxUi'
    $record.platform='ARM64'; Write-Record; Reject 'wrong platform'
    $record.platform='x64'; $record.configuration='ASan Debug'; Write-Record; Reject 'wrong configuration'
    $record.configuration='Debug'
    $record.commit='b'*40; Write-Record; Reject 'wrong pin'
    $record.commit='a'*40; $record.apiRevision=2; Write-Record; Reject 'wrong API'
    $record.apiRevision=3; $record.modules=@($modules[0]); Write-Record; Reject 'incomplete module closure'
    $record.modules=@($modules[0],$modules[0],$modules[2]); Write-Record; Reject 'duplicate module'
    $record.modules=@($modules); Write-Record
    # The check reads the lock as the restore does, so a lock the build refuses fails it too, even with a record that matches it.
    Write-Lock 2; $record.apiRevision=2; Write-Record; Reject 'a lock the restore refuses, even with a matching record'
    Write-Lock 3; $record.apiRevision=3; Write-Record
    Add-Content -LiteralPath (Join-Path $fixture 'Plugins/AVControl.dll') -Value 'replacement'
    Reject 'changed binary'

    # The writer records the restored build identity beside the archive's hash only when the output root MSBuild was given is the
    # one the restore named after that identity's fingerprint.
    $product=Join-Path $fixture 'product'
    $dependencyRoot=Join-Path $product '.build/dependencies/DxUi'
    [void](New-Item -ItemType Directory -Path (Join-Path $product 'Dependencies'),$dependencyRoot -Force)
    @{repository='https://github.com/RedSalamanders/DxUi';commit=('a'*40);apiRevision=3;targets=@('DxUi')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $product 'Dependencies/DxUi.lock.json')
    @{Fingerprint=('c'*64);Identity=@{commit=('a'*40);platform='x64'}} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dependencyRoot 'DxUi.identity.x64.json')
    function Write-OutputRoot([string]$Root) {
        Set-Content -LiteralPath (Join-Path $dependencyRoot 'DxUi.resolved.x64.props') -Value "<Project><PropertyGroup><DxUiConsumerOutputRoot>$Root</DxUiConsumerOutputRoot></PropertyGroup></Project>"
        $message=''
        try { Write-RedXeDxUiProvenance -RepoRoot $product -Platform x64 -Configuration Debug | Out-Null } catch { $message=$_.Exception.Message }
        return $message
    }
    if ((Write-OutputRoot "$(Join-Path $dependencyRoot ('d'*16))\") -notlike 'Restored DxUi identity does not match the resolved output root*') { throw 'Provenance paired an identity with another restore''s archive.' }
    Write-Host 'PASS provenance writer rejects an output root another identity named'
    # A directory elsewhere whose name is the identity's 16 digits is still another restore's root.
    $elsewhere=Join-Path $fixture "elsewhere/.build/dependencies/DxUi/$('c'*16)"
    [void](New-Item -ItemType Directory -Path $elsewhere -Force)
    if ((Write-OutputRoot "$elsewhere\") -notlike 'Restored DxUi identity does not match the resolved output root*') { throw 'Provenance paired an identity with the archive of another directory ending in its fingerprint.' }
    Write-Host 'PASS provenance writer rejects another directory ending in the identity''s fingerprint'
    if ((Write-OutputRoot '') -notlike 'Restored DxUi identity does not match the resolved output root*') { throw 'Provenance accepted properties that name no output root.' }
    Write-Host 'PASS provenance writer rejects properties that name no output root'
    # The same directory spelled another way (case, separators, a redundant segment, no trailing separator) is the same root.
    foreach ($spelling in @("$(Join-Path $dependencyRoot ('c'*16))\", (Join-Path $dependencyRoot "x/../$('C'*16)").Replace('\','/'), (Join-Path $dependencyRoot ('c'*16)).ToUpperInvariant())) {
        if ((Write-OutputRoot $spelling) -like '*does not match the resolved output root*') { throw "Provenance rejected the output root its identity named, spelled $spelling." }
    }
    Write-Host 'PASS provenance writer accepts the output root its identity named, however it is spelled'
} finally {
    $path=[IO.Path]::GetFullPath($fixture)
    $parent=[IO.Path]::GetFullPath((Join-Path $repo '.build/BuildProcessTests')).TrimEnd('\')+'\'
    if (-not $path.StartsWith($parent,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe fixture cleanup path.' }
    Remove-Item -LiteralPath $path -Recurse -Force
}
