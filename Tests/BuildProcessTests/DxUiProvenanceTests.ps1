[CmdletBinding()] param()
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repo 'Build/DxUiProvenance.psm1') -Force
$fixture=Join-Path $repo ('.build/BuildProcessTests/DxUi-'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path (Join-Path $fixture 'Plugins') -Force)
try {
    $lock=Join-Path $fixture 'lock.json'
    @{repository='https://github.com/RedSalamanders/DxUi';commit=('a'*40);apiRevision=2} | ConvertTo-Json | Set-Content -LiteralPath $lock
    $modules=foreach ($name in @('RedXe.exe','Plugins/AVControl.dll','AVControlTests.exe')) {
        $path=Join-Path $fixture $name
        Set-Content -LiteralPath $path -Value "fixture $name"
        @{path=$name;sha256=(Get-FileHash -LiteralPath $path).Hash}
    }
    $record=@{repository='https://github.com/RedSalamanders/DxUi';commit=('a'*40);apiRevision=2;platform='x64';configuration='Debug';modules=@($modules)}
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
    $record.commit='a'*40; $record.apiRevision=1; Write-Record; Reject 'wrong API'
    $record.apiRevision=2; $record.modules=@($modules[0]); Write-Record; Reject 'incomplete module closure'
    $record.modules=@($modules[0],$modules[0],$modules[2]); Write-Record; Reject 'duplicate module'
    $record.modules=@($modules); Write-Record
    Add-Content -LiteralPath (Join-Path $fixture 'Plugins/AVControl.dll') -Value 'replacement'
    Reject 'changed binary'
} finally {
    $path=[IO.Path]::GetFullPath($fixture)
    $parent=[IO.Path]::GetFullPath((Join-Path $repo '.build/BuildProcessTests')).TrimEnd('\')+'\'
    if (-not $path.StartsWith($parent,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe fixture cleanup path.' }
    Remove-Item -LiteralPath $path -Recurse -Force
}
