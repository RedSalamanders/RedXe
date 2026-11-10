<#
.SYNOPSIS
Records the six offscreen AV view scenarios with five rounds each.
.DESCRIPTION
Builds the native profile unless skipped. Checks linked dependency and module
provenance, retains an SDK reader throughout measurement, and hashes inputs
before and after execution. Creates a new JSON receipt without taking focus.
.PARAMETER RepoRoot
Product checkout containing the measured build and fixture sources.
.PARAMETER Configuration
Native Debug or Release profile; SDK inputs require Release.
.PARAMETER OutputPath
New receipt path. Existing measurements are never overwritten.
.PARAMETER SkipBuild
Measure an existing test-enabled profile after checking its provenance.
.PARAMETER DxUiSdkInputDescriptor
Original SDK descriptor. If omitted, an SDK build's recorded descriptor is used.
Missing or mismatched SDK inputs fail rather than falling back to Source.
.OUTPUTS
Measurement JSON and recorded source, binary, dependency and machine metadata.
#>
[CmdletBinding()]
param(
    [string] $RepoRoot = $PSScriptRoot,
    [ValidateSet('Debug', 'Release')][string] $Configuration = 'Release',
    [Parameter(Mandatory)][string] $OutputPath,
    [switch] $SkipBuild,
    [string] $DxUiSdkInputDescriptor = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $OutputPath) { throw "Keep prior measurements; output already exists: $OutputPath" }
$platform = switch ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()) {
    'X64' { 'x64' }
    'Arm64' { 'ARM64' }
    default { throw 'Run measurements on native x64 or ARM64 Windows.' }
}
if (-not $SkipBuild) {
    & (Join-Path $RepoRoot 'build.ps1') -Platform $platform -Configuration $Configuration -DxUiSdkInputDescriptor $DxUiSdkInputDescriptor
    if ($LASTEXITCODE -ne 0) { throw "Measurement build failed: $LASTEXITCODE" }
}
# The pin and where its restore lives are the build scripts' (Build/DxUiRestore.psm1), so this reads them as they do.
Import-Module (Join-Path $RepoRoot 'Build/DxUiRestore.psm1') -Force -ErrorAction Stop
$lockPath = Join-Path $RepoRoot 'Dependencies/DxUi.lock.json'
$pin = Read-RedXeDxUiLock -LockFile $lockPath
Import-Module (Join-Path $RepoRoot 'Build/DxUiProvenance.psm1') -ErrorAction Stop
$sdk=$null;$compilerHost=$null;$exitCompilerHost=$null;$measurementFailure=$null
try{
$provenancePath=Join-Path $RepoRoot ".build/$platform/$Configuration/DxUi.provenance.json"
$provenance=if(Test-Path -LiteralPath $provenancePath){Get-Content -LiteralPath $provenancePath -Raw|ConvertFrom-Json}else{$null}
if($null -ne $provenance -and $provenance.PSObject.Properties['dependencyRecipe'] -and
    $provenance.dependencyRecipe.recipe -cin @('PinnedReleaseSdkV1','PinnedQualifiedReleaseSdkV2')){
    $compilerHost=Enter-RedXeCompilerHost
    $exitCompilerHost=Get-Command Exit-RedXeCompilerHost
    Import-Module (Join-Path $RepoRoot '.ci-engine/Tools/Modules/Ci.SdkConsumer.psm1') -ErrorAction Stop
}
$sdk=Open-RedXeDxUiMeasurementSdkInput -RepoRoot $RepoRoot -Platform $platform -Configuration $Configuration -Descriptor $DxUiSdkInputDescriptor
if($null -ne $sdk){
    $dxUiRoot=$sdk.Descriptor.Value.sourceRoot
    $archive=Join-Path $sdk.SdkRoot "lib/$platform/Release/DxUi.lib"
}else{
$dependencyRoot = Get-RedXeDxUiDependencyRoot -RepoRoot $RepoRoot
$resolved = Join-Path $dependencyRoot "DxUi.resolved.$platform.props"
if (-not (Test-Path -LiteralPath $resolved)) {
    # The original adoption baseline predates per-platform resolved properties.
    $resolved = Join-Path $dependencyRoot 'DxUi.resolved.props'
}
[xml] $props = Get-Content -LiteralPath $resolved -Raw
$dxUiRoot = [string] $props.Project.PropertyGroup.DxUiRoot
$archive = Join-Path ([string] $props.Project.PropertyGroup.DxUiConsumerOutputRoot) "$platform/$Configuration/DxUi.lib"
}
& (Join-Path $dxUiRoot 'Tools/validate_consumer.ps1') -DxUiRoot $dxUiRoot -LockFile $lockPath
$exe = Join-Path $RepoRoot ".build/$platform/$Configuration/AVControlTests.exe"
$linkLog = Join-Path $RepoRoot ".build/Intermediate/$platform/$Configuration/AVControlTests/AVControlTests.tlog/link.command.1.tlog"
$linkText = (Get-Content -LiteralPath $linkLog -Raw).Replace('/', '\')
if ($linkText.IndexOf($archive.Replace('/', '\'), [StringComparison]::OrdinalIgnoreCase) -lt 0) {
    throw 'The measured executable linker record does not name the selected DxUi archive.'
}
function Get-InputHashes([string[]] $Paths) {
    $result = [ordered] @{}
    foreach ($path in $Paths) {
        $relative = [IO.Path]::GetRelativePath($RepoRoot, $path).Replace('\', '/')
        $result[$relative] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    return $result
}
$fixturePaths = @('Tests/AVControlTests/AVControl.Tests.Runner.cpp', 'Tests/AVControlTests/AVControl.Tests.NativeView.cpp') |
    ForEach-Object { Join-Path $RepoRoot $_ }
$productionPaths = Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'Plugins/AVControl') -Recurse -File |
    Where-Object Extension -In '.cpp', '.h' | Sort-Object FullName | Select-Object -ExpandProperty FullName
$fixtureInputs = Get-InputHashes $fixturePaths
$productionInputs = Get-InputHashes $productionPaths
$binaryInputs = Get-InputHashes @($exe, $archive)
$dependencyPaths=@()
if(Test-Path -LiteralPath $provenancePath){$dependencyPaths+=,$provenancePath}
if($null -ne $sdk){$dependencyPaths+=,$sdk.Descriptor.Path}
$dependencyInputs=Get-InputHashes $dependencyPaths
$productCommit = & git -C $RepoRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the product checkout.' }
$productStatus = @(& git -C $RepoRoot status --porcelain --untracked-files=normal)
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify product modifications.' }
$cpu = Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors
$os = Get-CimInstance Win32_OperatingSystem | Select-Object Version, BuildNumber
$powerPolicy = (& powercfg /getactivescheme | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot record the active power policy.' }
$warp = (Get-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/d3d10warp.dll')).VersionInfo.FileVersion
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($OutputPath)) | Out-Null
$started = [DateTime]::UtcNow.ToString('o')
Push-Location $RepoRoot
try {
    & $exe --measure-native-views $OutputPath
    if ($LASTEXITCODE -ne 0) { throw "AV resource fixture failed: $LASTEXITCODE" }
} finally { Pop-Location }
$completed = [DateTime]::UtcNow.ToString('o')
foreach ($paths in @(@($exe, $archive), $fixturePaths, $productionPaths, $dependencyPaths)) {
    if(-not $paths.Count){continue}
    $before = if ($paths[0] -eq $exe) { $binaryInputs } elseif ($paths[0] -eq $fixturePaths[0]) { $fixtureInputs } elseif ($paths[0] -eq $productionPaths[0]) {$productionInputs} else {$dependencyInputs}
    $after = Get-InputHashes $paths
    if (($before | ConvertTo-Json -Compress) -cne ($after | ConvertTo-Json -Compress)) {
        throw 'Measurement inputs changed during execution; retain this incomplete run for diagnosis.'
    }
}
$receipt = Get-Content -LiteralPath $OutputPath -Raw | ConvertFrom-Json
if ($receipt.fixture -ne 'redxe-av-two-views-v1' -or $receipt.scenarios.Count -ne 6 -or
    @($receipt.scenarios | Where-Object { $_.rounds.Count -ne 5 }).Count) {
    throw 'Incomplete AV resource measurement.'
}
$receipt | Add-Member -NotePropertyName metadata -NotePropertyValue ([ordered] @{
    productCommit = $productCommit.Trim(); productStatus = $productStatus; dxUiCommit = $pin.commit
    platform = $platform; configuration = $Configuration; machine = $env:COMPUTERNAME
    cpu = $cpu; operatingSystem = $os; warpVersion = $warp; powerPolicy = $powerPolicy
    fixtureInputs = $fixtureInputs; productionInputs = $productionInputs; binaryInputs = $binaryInputs
    dependencyInputs=$dependencyInputs;dependencyRecipe=$(if($null -ne $sdk){$sdk.Evidence}else{@{recipe='PinnedSource'}})
    wrapperSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
    startedUtc = $started; completedUtc = $completed
})
}catch{$measurementFailure=$_;throw}
finally{
    try{if($null -ne $sdk){try{Close-CiPinnedSdkInput $sdk}catch{if($null -eq $measurementFailure){throw}else{Write-Warning ('SDK measurement close also failed: '+$_.Exception.Message)}}}}
    finally{if($null -ne $compilerHost){& $exitCompilerHost -State $compilerHost}}
}
# Keep the native fixture's raw result on failure. Publish wrapper provenance only
# after the SDK close verified the original inputs and compiler state was restored.
$receipt | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $OutputPath -Encoding utf8NoBOM
Write-Host "Recorded AV views: $OutputPath. Compare matched runs; this receipt alone is not acceptance."
