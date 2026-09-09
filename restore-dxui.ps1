<# .SYNOPSIS Restore the exact DxUi source pin and isolated build dependencies without modifying a sibling checkout. #>
[CmdletBinding()]
param([ValidateSet('x64','ARM64')][string] $Platform = 'x64', [string] $MSBuildPath = '', [switch] $CheckUpdates)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$pinPath = Join-Path $PSScriptRoot 'Dependencies/DxUi.lock.json'
$pin = Get-Content -LiteralPath $pinPath -Raw | ConvertFrom-Json
if ($pin.repository -ne 'https://github.com/RedSalamanders/DxUi' -or $pin.commit -notmatch '^[0-9a-f]{40}$' -or $pin.apiRevision -ne 2 -or @($pin.targets).Count -ne 1 -or $pin.targets[0] -ne 'DxUi') {
    throw 'DxUi.lock.json must identify the canonical repository, exact commit, API revision 2 and single DxUi target.'
}
$dependencyRoot = Join-Path $PSScriptRoot '.build/dependencies/DxUi'
$source = Join-Path $dependencyRoot "source/$($pin.commit)"
if (-not (Test-Path -LiteralPath $source)) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $source) | Out-Null
    $sibling = Join-Path (Split-Path -Parent $PSScriptRoot) 'DxUi'
    $cloneFrom = "$($pin.repository).git"
    if (Test-Path -LiteralPath (Join-Path $sibling '.git')) {
        & git -C $sibling cat-file -e "$($pin.commit)^{commit}" 2>$null
        if ($LASTEXITCODE -eq 0) { $cloneFrom = $sibling }
    }
    & git clone --no-checkout --no-hardlinks $cloneFrom $source
    if ($LASTEXITCODE -ne 0) { throw 'DxUi source restore failed. Check Git/network access to the public repository and retry; no custom access token is required.' }
    & git -C $source checkout --detach $pin.commit
    if ($LASTEXITCODE -ne 0) { throw 'The exact DxUi source pin could not be checked out.' }
    & git -C $source remote set-url origin "$($pin.repository).git"
    if ($LASTEXITCODE -ne 0) { throw 'Could not record the canonical DxUi origin.' }
}
& (Join-Path $source 'Tools/validate_consumer.ps1') -DxUiRoot $source -LockFile $pinPath
if (-not $MSBuildPath) {
    if ($env:MSBUILD_EXE_PATH) { $MSBuildPath=$env:MSBUILD_EXE_PATH }
    else {
        $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $installation=& $vswhere -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild -property installationPath
        $MSBuildPath=Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
    }
}
Import-Module (Join-Path $source 'Tools/ConsumerBuild.psm1') -Force
$buildIdentity=Get-DxUiConsumerBuildIdentity -DxUiRoot $source -MSBuildPath $MSBuildPath -Platform $Platform
$output = (Join-Path $dependencyRoot $buildIdentity.Fingerprint) + [IO.Path]::DirectorySeparatorChar
& (Join-Path $source 'vcpkg-install.ps1') -Platform $Platform -OutputRoot $output
$escape = { param([string] $value) [System.Security.SecurityElement]::Escape($value) }
$sourceXml = & $escape $source
$outputXml = & $escape $output
$pinXml = & $escape $pinPath
$identityProperties = '<DxUiConsumerToolset>' + (& $escape $buildIdentity.Identity.toolset) + '</DxUiConsumerToolset>' +
    '<DxUiConsumerVCToolsVersion>' + (& $escape $buildIdentity.Identity.vcToolsVersion) + '</DxUiConsumerVCToolsVersion>' +
    '<DxUiConsumerSdkVersion>' + (& $escape $buildIdentity.Identity.windowsSdkVersion) + '</DxUiConsumerSdkVersion>' +
    '<DxUiConsumerPreferredToolArchitecture>' + (& $escape $buildIdentity.Identity.preferredToolArchitecture) + '</DxUiConsumerPreferredToolArchitecture>'
$props = "<Project><PropertyGroup>$identityProperties<DxUiRoot>$sourceXml</DxUiRoot><DxUiConsumerOutputRoot>$outputXml</DxUiConsumerOutputRoot><DxUiConsumerLockFile>$pinXml</DxUiConsumerLockFile></PropertyGroup></Project>"
$propsPath = Join-Path $dependencyRoot "DxUi.resolved.$Platform.props"
if (-not (Test-Path -LiteralPath $propsPath) -or [IO.File]::ReadAllText($propsPath) -ne $props) {
    [IO.File]::WriteAllText($propsPath, $props, [Text.UTF8Encoding]::new($false))
}
$identityPath=Join-Path $dependencyRoot "DxUi.identity.$Platform.json"
$identityJson=$buildIdentity | ConvertTo-Json -Depth 5
if (-not (Test-Path -LiteralPath $identityPath) -or [IO.File]::ReadAllText($identityPath) -ne $identityJson) {
    [IO.File]::WriteAllText($identityPath,$identityJson,[Text.UTF8Encoding]::new($false))
}
if ($CheckUpdates) {
    Import-Module (Join-Path $source 'Tools/ConsumerUpdate.psm1') -Force
    Show-DxUiUpdateNotice -LockFile $pinPath
}
Write-Host "DxUi restored at exact pin $($pin.commit); sibling checkout unchanged."
