[CmdletBinding()] param()
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repo 'Build/DxUiProvenance.psm1') -Force
& {
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'package.ps1'),[ref]$tokens,[ref]$errors)
    if($errors.Count){throw 'Package entrypoint syntax is invalid.'}
    $capture=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq '$findPackageMSBuild'},$true))
    $open=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq '$sdk' -and $n.Right.Extent.Text.Contains('Open-CiApplicationSdkInput')},$true))
    $imports=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -ceq 'Import-Module' -and $n.Extent.Text.Contains('DxUiProvenance.psm1')},$true))
    if($capture.Count -ne 1 -or $open.Count -ne 1 -or $imports.Count -ne 1 -or $capture[0].Extent.EndOffset -ge $imports[0].Extent.StartOffset){throw 'The packaging tool getter must be retained before provenance import can replace its exports.'}
    function Find-RedXeMSBuild {'C:/synthetic-selected-vs/MSBuild.exe'}
    . ([scriptblock]::Create($capture[0].Extent.Text))
    # Model the observed nested forced import removing the outer function export.
    Remove-Item function:Find-RedXeMSBuild
    function Open-CiApplicationSdkInput {
        param($ProjectRoot,$Descriptor,$Platform,$Configuration,$MSBuildPath)
        if($MSBuildPath -cne 'C:/synthetic-selected-vs/MSBuild.exe' -or $Platform -cne 'ARM64' -or $Configuration -cne 'Release' -or $Descriptor -cne 'synthetic original descriptor'){throw 'Packaging lost the original tool getter or SDK arguments after export replacement.'}
        [pscustomobject]@{Evidence=@{recipe='original reader result'}}
    }
    $repoRoot=$repo;$Platform='ARM64';$DxUiSdkInputDescriptor='synthetic original descriptor'
    . ([scriptblock]::Create($open[0].Extent.Text))
    if($sdk.Evidence.recipe -cne 'original reader result'){throw 'Packaging replaced the original SDK result.'}
    Write-Host 'PASS actual package SDK branch retains its selected tool getter after nested export removal'
}
$fixture=Join-Path $repo ('.build/BuildProcessTests/DxUi-'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path (Join-Path $fixture 'Plugins') -Force)
try {
    $lock=Join-Path $fixture 'lock.json'
    function Write-Lock([int]$ApiRevision) { @{repository='https://github.com/RedSalamanders/DxUi';commit=('a'*40);apiRevision=$ApiRevision;targets=@('DxUi')} | ConvertTo-Json | Set-Content -LiteralPath $lock }
    Write-Lock 4
    $modules=foreach ($name in @('RedXe.exe','Plugins/AVControl.dll','AVControlTests.exe')) {
        $path=Join-Path $fixture $name
        Set-Content -LiteralPath $path -Value "fixture $name"
        @{path=$name;sha256=(Get-FileHash -LiteralPath $path).Hash}
    }
    $record=@{repository='https://github.com/RedSalamanders/DxUi';commit=('a'*40);apiRevision=4;platform='x64';configuration='Debug';buildRole='test-enabled';modules=@($modules)}
    function Write-Record { $record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $fixture 'DxUi.provenance.json') }
    function Reject([string]$Scenario) {
        $rejected=$false
        try { Assert-RedXeDxUiProvenance -OutputRoot $fixture -LockFile $lock -Platform x64 -Configuration Debug } catch { $rejected=$true }
        if (-not $rejected) { throw "Accepted stale provenance: $Scenario" }
        Write-Host "PASS provenance rejects $Scenario"
    }
    Write-Record
    Assert-RedXeDxUiProvenance -OutputRoot $fixture -LockFile $lock -Platform x64 -Configuration Debug
    $record.configuration='Release';$record.buildRole='production-only';$record.modules=@($modules[0],$modules[1]);Write-Record
    Assert-RedXeDxUiProvenance -OutputRoot $fixture -LockFile $lock -Platform x64 -Configuration Release -ProductionOnly
    Write-Host 'PASS production provenance accepts the two shipping modules without an unbuilt test executable'
    $message=''
    try{Assert-RedXeDxUiProvenance -OutputRoot $fixture -LockFile $lock -Platform x64 -Configuration Release}catch{$message=$_.Exception.Message}
    if($message -notlike '*build role*'){throw 'A production record qualified a test-enabled request.'}
    Write-Host 'PASS production provenance cannot qualify test-enabled validation'
    $record.buildRole='test-enabled';$record.modules=@($modules);Write-Record
    $message=''
    try{Assert-RedXeDxUiProvenance -OutputRoot $fixture -LockFile $lock -Platform x64 -Configuration Release -ProductionOnly}catch{$message=$_.Exception.Message}
    if($message -notlike '*build role*'){throw 'A test-enabled record qualified production packaging.'}
    Write-Host 'PASS test-enabled provenance cannot qualify production packaging'
    $record.configuration='Debug';Write-Record
    $record.repository='https://example.invalid/fork'; Write-Record; Reject 'wrong repository'
    $record.repository='https://github.com/RedSalamanders/DxUi'
    $record.platform='ARM64'; Write-Record; Reject 'wrong platform'
    $record.platform='x64'; $record.configuration='ASan Debug'; Write-Record; Reject 'wrong configuration'
    $record.configuration='Debug'
    $record.commit='b'*40; Write-Record; Reject 'wrong pin'
    $record.commit='a'*40; $record.apiRevision=3; Write-Record; Reject 'wrong API'
    $record.apiRevision=4; $record.modules=@($modules[0]); Write-Record; Reject 'incomplete module closure'
    $record.modules=@($modules[0],$modules[0],$modules[2]); Write-Record; Reject 'duplicate module'
    $record.modules=@($modules); Write-Record
    # The check reads the lock as the restore does, so a lock the build refuses fails it too, even with a record that matches it.
    Write-Lock 3; $record.apiRevision=3; Write-Record; Reject 'a lock the restore refuses, even with a matching record'
    Write-Lock 4; $record.apiRevision=4; Write-Record
    # build.ps1 -ProductionOnly (the CI production role) builds no Tests project, so it must ask the writer for that role's
    # record, or the writer reads the link log of an AVControlTests.exe that was never built.
    $buildAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'build.ps1'),[ref]$null,[ref]$null)
    $writes=@($buildAst.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -eq 'Write-RedXeDxUiProvenance' }, $true))
    if ($writes.Count -ne 1 -or @($writes[0].CommandElements | Where-Object { $_ -is [Management.Automation.Language.CommandParameterAst] -and $_.ParameterName -eq 'ProductionOnly' }).Count -ne 1) {
        throw 'build.ps1 must tell the provenance writer whether the build is production-only.'
    }
    Write-Host 'PASS build.ps1 records a production-only build as one'
    Add-Content -LiteralPath (Join-Path $fixture 'Plugins/AVControl.dll') -Value 'replacement'
    Reject 'changed binary'

    # The writer records the restored build identity beside the archive's hash only when the output root MSBuild was given is the
    # one the restore named after that identity's fingerprint.
    $product=Join-Path $fixture 'product'
    $dependencyRoot=Join-Path $product '.build/dependencies/DxUi'
    [void](New-Item -ItemType Directory -Path (Join-Path $product 'Dependencies'),$dependencyRoot -Force)
    @{repository='https://github.com/RedSalamanders/DxUi';commit=('a'*40);apiRevision=4;targets=@('DxUi')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $product 'Dependencies/DxUi.lock.json')
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

    # Exercise the real measurement gate with a recording SDK transport boundary.
    # These are data/lease controls, not native ABI or performance qualification.
    $output=Join-Path $product '.build/x64/Release'
    [void][IO.Directory]::CreateDirectory((Join-Path $output 'Plugins'))
    $measurementModules=foreach($name in @('RedXe.exe','Plugins/AVControl.dll','AVControlTests.exe')){
        $path=Join-Path $output $name
        [IO.File]::WriteAllText($path,"measurement fixture $name")
        @{path=$name;sha256=(Get-FileHash -LiteralPath $path).Hash}
    }
    $descriptor=Join-Path $product 'descriptor.json'
    [IO.File]::WriteAllText($descriptor,'{"recordingTransportOnly":true}')
    $sdkRoot=Join-Path $product 'sdk'
    $archive=Join-Path $sdkRoot 'lib/x64/Release/DxUi.lib'
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $archive))
    [IO.File]::WriteAllText($archive,'recording SDK archive')
    $transportPath=Join-Path $product '.ci-engine/Tools/Modules/Ci.SdkConsumer.psm1'
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $transportPath))
    [IO.File]::WriteAllText($transportPath,@'
function Open-CiApplicationSdkInput {
    param($ProjectRoot,$Descriptor,$Platform,$Configuration,$MSBuildPath)
    $script:opens++
    if($MSBuildPath -cne 'recording-msbuild'){throw 'Unexpected compiler discovery boundary.'}
    return $script:reader
}
function Close-CiPinnedSdkInput {
    param($Reader)
    $script:closes++
    $Reader.Closed=$true
    if($script:closeFails){throw 'recording close failure'}
}
Export-ModuleMember -Function Open-CiApplicationSdkInput,Close-CiPinnedSdkInput
'@)
    $transport=Import-Module $transportPath -PassThru
    $provenanceModule=Get-Module DxUiProvenance
    & $provenanceModule { function script:Find-RedXeMSBuild { 'recording-msbuild' } }
    try{
        function Reset-MeasurementFixture([string]$Recipe='PinnedQualifiedReleaseSdkV2'){
            $script:measurementRecord=@{repository='https://github.com/RedSalamanders/DxUi';commit=('a'*40);apiRevision=4;
                platform='x64';configuration='Release';buildRole='test-enabled';modules=@($measurementModules|ConvertTo-Json -Depth 5|ConvertFrom-Json);
                dependencyRecipe=@{recipe=$Recipe;inputKey=('1'*64);manifestSha256=('2'*64)};
                archiveSha256=(Get-FileHash -LiteralPath $archive).Hash;
                sdkInputDescriptor=@{path=$descriptor;sha256=(Get-FileHash -LiteralPath $descriptor).Hash.ToLowerInvariant()}}
            $reader=[pscustomobject]@{Closed=$false;SdkRoot=$sdkRoot;Descriptor=[pscustomobject]@{Path=$descriptor};
                Evidence=[pscustomobject]@{recipe=$Recipe;inputKey=('1'*64);manifestSha256=('2'*64)}}
            & $transport {param($Reader)$script:reader=$Reader;$script:opens=0;$script:closes=0;$script:closeFails=$false} $reader
        }
        function Save-MeasurementFixture {
            $script:measurementRecord|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $output 'DxUi.provenance.json')
        }
        function Assert-MeasurementCounts([int]$Opens,[int]$Closes){
            $counts=& $transport {@($script:opens,$script:closes)}
            if($counts[0] -ne $Opens -or $counts[1] -ne $Closes){throw "Unexpected SDK lease counts: $counts; expected $Opens/$Closes"}
        }
        function Refuse-Measurement([string]$Pattern,[int]$Opens=0,[int]$Closes=0){
            Save-MeasurementFixture
            $message=''
            try{Open-RedXeDxUiMeasurementSdkInput -RepoRoot $product -Platform x64 -Configuration Release|Out-Null}catch{$message=$_.Exception.Message}
            if($message -notlike $Pattern){throw "Measurement refusal differed: $message; expected $Pattern"}
            Assert-MeasurementCounts $Opens $Closes
        }
        if($null -ne (Open-RedXeDxUiMeasurementSdkInput -RepoRoot $product -Platform x64 -Configuration Release)){throw 'Historical Source unexpectedly acquired SDK inputs.'}
        foreach($recipe in @('PinnedReleaseSdkV1','PinnedQualifiedReleaseSdkV2')){
            Reset-MeasurementFixture $recipe;Save-MeasurementFixture
            $reader=Open-RedXeDxUiMeasurementSdkInput -RepoRoot $product -Platform x64 -Configuration Release
            if($null -eq $reader -or $reader.Closed){throw 'Measurement did not retain its live SDK reader.'}
            Assert-MeasurementCounts 1 0
            & $transport {param($Reader)Close-CiPinnedSdkInput $Reader} $reader
            Assert-MeasurementCounts 1 1
        }
        Reset-MeasurementFixture 'PinnedSource';Save-MeasurementFixture
        if($null -ne (Open-RedXeDxUiMeasurementSdkInput -RepoRoot $product -Platform x64 -Configuration Release)){throw 'Source unexpectedly entered SDK transport.'}
        Assert-MeasurementCounts 0 0
        $measurementRecord.Remove('dependencyRecipe');$measurementRecord.Remove('buildRole');Save-MeasurementFixture
        if($null -ne (Open-RedXeDxUiMeasurementSdkInput -RepoRoot $product -Platform x64 -Configuration Release)){throw 'Legacy Source unexpectedly entered SDK transport.'}
        Reset-MeasurementFixture 'UnreviewedSdk';Refuse-Measurement '*Unsupported measurement dependency recipe*'
        Reset-MeasurementFixture;$measurementRecord.buildRole='production-only';Refuse-Measurement '*build role*'
        Reset-MeasurementFixture;$measurementRecord.Remove('sdkInputDescriptor');Refuse-Measurement '*original descriptor*'
        Reset-MeasurementFixture;$measurementRecord.sdkInputDescriptor.sha256='3'*64;Refuse-Measurement '*descriptor changed*'
        Reset-MeasurementFixture;$measurementRecord.commit='b'*40;Refuse-Measurement '*Stale DxUi product provenance*'
        Reset-MeasurementFixture;$measurementRecord.modules[0].sha256='4'*64;Refuse-Measurement '*Stale or missing DxUi consumer module*'
        foreach($field in @('recipe','inputKey','manifestSha256')){
            Reset-MeasurementFixture
            & $transport {param($Field)$script:reader.Evidence.$Field='different'} $field
            Refuse-Measurement '*does not match the actual test build inputs*' 1 1
        }
        Reset-MeasurementFixture;$measurementRecord.archiveSha256='5'*64;Refuse-Measurement '*archive differs from the linked build input*' 1 1
        Reset-MeasurementFixture;$measurementRecord.archiveSha256='5'*64
        & $transport {$script:closeFails=$true}
        Refuse-Measurement '*archive differs from the linked build input*' 1 1
        Reset-MeasurementFixture;$measurementRecord.Remove('sdkInputDescriptor');Save-MeasurementFixture
        $reader=Open-RedXeDxUiMeasurementSdkInput -RepoRoot $product -Platform x64 -Configuration Release -Descriptor $descriptor
        Assert-MeasurementCounts 1 0
        & $transport {param($Reader)Close-CiPinnedSdkInput $Reader} $reader
        Assert-MeasurementCounts 1 1
        Write-Host 'PASS measurement preserves Source and SDK recipes, original descriptor/archive/module checks, and live lease ownership'
        $wrapper=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'measure-av-views.ps1'),[ref]$null,[ref]$null)
        $scope=@($wrapper.FindAll({param($node)$node -is [Management.Automation.Language.TryStatementAst] -and $node.Body.Extent.Text.Contains('$provenancePath=')},$true))
        $writes=@($wrapper.FindAll({param($node)$node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Set-Content' -and $node.Extent.Text.Contains('$OutputPath')},$true))
        $closes=@($wrapper.FindAll({param($node)$node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Close-CiPinnedSdkInput'},$true))
        if($scope.Count -ne 1 -or $writes.Count -ne 1 -or $closes.Count -ne 1 -or
            $writes[0].Extent.StartOffset -le $scope[0].Extent.EndOffset -or
            $closes[0].Extent.StartOffset -le $scope[0].Finally.Extent.StartOffset -or $closes[0].Extent.EndOffset -ge $scope[0].Finally.Extent.EndOffset){
            throw 'Measurement provenance must be written only after the enclosing input lease finally completed.'
        }
        Write-Host 'PASS measurement publishes wrapper provenance only after successful final input verification'
        $hashFunction=@($wrapper.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Get-InputHashes'},$true))
        $snapshots=@($wrapper.EndBlock.Statements|Where-Object {$_.Extent.Text.StartsWith('$toolingPaths=') -or $_.Extent.Text.StartsWith('$toolingInputs=')})
        $guards=@($wrapper.FindAll({param($node)$node -is [Management.Automation.Language.ForEachStatementAst] -and $node.Variable.Extent.Text -ceq '$inputSet'},$true))
        $imports=@($wrapper.FindAll({param($node)$node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Import-Module'},$true))
        $localImports=@($imports|Where-Object {$_.Extent.Text.Contains("Build/DxUiProvenance.psm1") -or $_.Extent.Text.Contains("Build/DxUiRestore.psm1")})
        if($localImports.Count -ne 2){throw 'Expected both canonical measurement modules.'}
        $coldImports=Join-Path $fixture 'cold-measurement-imports.ps1'
        $coldText='param($RepoRoot)'+"`n"+($localImports.Extent.Text -join "`n")+@'

$ErrorActionPreference='Stop'
$pin=Read-RedXeDxUiLock -LockFile (Join-Path $RepoRoot 'Dependencies/DxUi.lock.json')
if(-not $pin.commit){throw 'Cold measurement cannot read its actual pin.'}
$dependency=Get-RedXeDxUiDependencyRoot -RepoRoot $RepoRoot
if(-not $dependency){throw 'Cold Source measurement lost its restore helper.'}
if(-not (Get-Command Open-RedXeDxUiMeasurementSdkInput -ErrorAction Stop)){throw 'Cold SDK measurement lost its provenance helper.'}
'@
        [IO.File]::WriteAllText($coldImports,$coldText)
        & (Join-Path $PSHOME 'pwsh.exe') -NoProfile -File $coldImports -RepoRoot $repo
        if($LASTEXITCODE){throw 'Actual cold measurement imports lost Source or SDK helper visibility.'}
        Write-Host 'PASS actual cold measurement module imports preserve Source and SDK helper bindings'
        if($hashFunction.Count -ne 1 -or $snapshots.Count -ne 2 -or $guards.Count -ne 1 -or
            $snapshots[-1].Extent.EndOffset -ge ($imports.Extent.StartOffset|Measure-Object -Minimum).Minimum -or
            $guards[0].Extent.StartOffset -le $scope[0].Extent.EndOffset -or
            $guards[0].Extent.EndOffset -ge $writes[0].Extent.StartOffset){
            throw 'Measurement tooling must freeze before imports and be checked after finalization before publication.'
        }
        $guardRoot=Join-Path $fixture 'wrapper-input-guard'
        [void][IO.Directory]::CreateDirectory((Join-Path $guardRoot 'Build'))
        $guardPaths=@('measure-av-views.ps1','build.ps1','Build/DxUiProvenance.psm1','Build/DxUiRestore.psm1','Build/DxUiPaths.psm1')
        $guardSetup=@'
$exe=Join-Path $RepoRoot 'fixture.exe';$archive=Join-Path $RepoRoot 'archive.lib'
$binaryInputs=Get-InputHashes @($exe,$archive)
$fixturePaths=@();$productionPaths=@();$dependencyPaths=@()
$fixtureInputs=@{};$productionInputs=@{};$dependencyInputs=@{}
if($ChangedTool -cne 'Unchanged'){[IO.File]::AppendAllText((Join-Path $RepoRoot $ChangedTool),'changed during finalization')}
'@
        $guardScript='param($RepoRoot,$ChangedTool)'+"`n"+$hashFunction[0].Extent.Text+"`n"+($snapshots.Extent.Text -join "`n")+"`n"+$guardSetup+"`n"+$guards[0].Extent.Text
        foreach($changedTool in @('Unchanged')+$guardPaths){
            foreach($relative in $guardPaths+@('fixture.exe','archive.lib')){[IO.File]::WriteAllText((Join-Path $guardRoot $relative),'original owned fixture')}
            # A real script file supplies its actual automatic PSCommandPath;
            # evaluating the fragment in a dynamic block would make that empty.
            [IO.File]::WriteAllText((Join-Path $guardRoot 'measure-av-views.ps1'),$guardScript)
            $refused=$false
            try{
                & (Join-Path $guardRoot 'measure-av-views.ps1') -RepoRoot $guardRoot -ChangedTool $changedTool
            }catch{
                if($_.Exception.Message -notlike '*Measurement inputs changed during execution or finalization*'){throw}
                $refused=$true
            }
            if($refused -ne ($changedTool -cne 'Unchanged')){throw "Actual wrapper input guard gave the wrong verdict for $changedTool."}
        }
        Write-Host 'PASS actual final measurement guard accepts unchanged tooling and rejects each wrapper/build/provenance/restore/path-helper mutation'
    }finally{
        & $provenanceModule {Remove-Item function:script:Find-RedXeMSBuild}
        Remove-Module $transport
    }
} finally {
    $path=[IO.Path]::GetFullPath($fixture)
    $parent=[IO.Path]::GetFullPath((Join-Path $repo '.build/BuildProcessTests')).TrimEnd('\')+'\'
    if (-not $path.StartsWith($parent,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe fixture cleanup path.' }
    Remove-Item -LiteralPath $path -Recurse -Force
}
