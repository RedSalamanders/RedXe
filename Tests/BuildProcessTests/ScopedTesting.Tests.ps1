# Real Git/content/binary fixtures; does not build or take desktop focus.
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repository=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repository 'Build/ScopedTesting.psm1') -Force
$passed=0
function Assert-Scope([bool]$Condition,[string]$Message) {if(-not $Condition){throw $Message}}
function Run-Case([string]$Name,[scriptblock]$Action) {& $Action;$script:passed++;Write-Host "PASS $Name"}
function Write-Fixture([string]$Path,[string]$Content) {[void](New-Item -ItemType Directory -Path (Split-Path $Path) -Force);[IO.File]::WriteAllText($Path,$Content,[Text.UTF8Encoding]::new($false))}
function Invoke-FixtureGit([string[]]$Arguments) {& git -C $fixture @Arguments *> $null;if($LASTEXITCODE){throw "Fixture git failed: $Arguments"}}
$fixture=Join-Path $repository ('.build/ToolTests/ScopedTesting-'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixture -Force)
try {
    Invoke-FixtureGit @('init','-q')
    Invoke-FixtureGit @('config','user.name','Scoped testing fixture')
    Invoke-FixtureGit @('config','user.email','fixture@example.invalid')
    foreach($file in @('code.cpp','staged.cpp','deleted.cpp','renamed.cpp')) {Write-Fixture (Join-Path $fixture $file) 'initial'}
    Invoke-FixtureGit @('add','-A');Invoke-FixtureGit @('commit','-q','-m','fixture baseline')
    $baseline=(& git -C $fixture rev-parse HEAD).Trim()
    Write-Fixture (Join-Path $fixture 'committed.cpp') 'committed'
    Invoke-FixtureGit @('add','-A');Invoke-FixtureGit @('commit','-q','-m','fixture candidate')
    Write-Fixture (Join-Path $fixture 'staged.cpp') 'staged'
    Invoke-FixtureGit @('add','staged.cpp')
    # A working copy matching HEAD must not hide a different staged blob.
    Write-Fixture (Join-Path $fixture 'staged.cpp') 'initial'
    Write-Fixture (Join-Path $fixture 'code.cpp') 'working'
    Remove-Item -LiteralPath (Join-Path $fixture 'deleted.cpp')
    Move-Item -LiteralPath (Join-Path $fixture 'renamed.cpp') -Destination (Join-Path $fixture 'renamed-new.cpp')
    Write-Fixture (Join-Path $fixture 'untracked file.cpp') 'untracked'
    $manifest=Read-ScopedTestManifest $repository
    Run-Case 'all active native tests satisfy naming and inventory' {Assert-Scope ((Assert-ScopedTestNames $repository) -gt 0) 'Empty native inventory'}
    Run-Case 'active names require exact Tests spelling and reject historical inventory membership' {
        $naming=Join-Path $fixture 'naming'
        foreach($invalid in @('Tests/Scope.tests.Case.cpp','Specs/TestRuns/Scope.Tests.Case.cpp')) {
            Write-Fixture (Join-Path $naming $invalid) 'invalid declared source'
            Write-Fixture (Join-Path $naming 'Tests/native-test-files.json') (ConvertTo-Json -InputObject @($invalid) -Compress)
            $message=''
            try {Assert-ScopedTestNames $naming | Out-Null} catch [System.Management.Automation.RuntimeException] {$message=$_.Exception.Message}
            Assert-Scope ($message -match 'Invalid/missing native test source:|Historical/external source cannot enter') "Invalid inventory accepted: $invalid ($message)"
        }
    }
    Run-Case 'committed, staged, working, deleted, both rename sides and spaced untracked paths count' {
        $paths=@(Get-ScopedChangedPaths $fixture $baseline)
        foreach($path in @('committed.cpp','staged.cpp','code.cpp','deleted.cpp','renamed.cpp','renamed-new.cpp','untracked file.cpp')) {Assert-Scope ($path -in $paths) "Missing impact: $path"}
    }
    Run-Case 'unmapped executable input widens to every scope' {$plan=Get-ScopedTestPlan $manifest @('Unknown/new.cpp');Assert-Scope $plan.full 'Fallback was narrowed'}
    Run-Case 'executable schema changes are never mistaken for prose' {$plan=Get-ScopedTestPlan $manifest @('Specs/Unmapped.schema.json');Assert-Scope $plan.full 'Schema was ignored'}
    Run-Case 'prose selects only independent validators, and empty changes select nothing' {
        foreach($path in @('Specs/Testing/explanation.md','Plugins/Weather/README.md','.agents/skills/example/SKILL.md')) {
            $plan=Get-ScopedTestPlan $manifest @($path)
            Assert-Scope ($plan.scopes.Count -gt 0) 'Documentation validation omitted'
            Assert-Scope (@($manifest.scopes | Where-Object {$_.native -and $_.name -in $plan.scopes}).Count -eq 0) 'Prose unnecessarily selected native tests'
        }
        Assert-Scope ((Get-ScopedTestPlan $manifest @()).scopes.Count -eq 0) 'Empty changes selected tests'
    }
    Run-Case 'invalid explicit scope and nonrelative paths fail before execution' {
        foreach($action in @({Get-ScopedTestPlan -Manifest $manifest -ChangedPaths @() -Scopes @('Typo')},{Get-ScopedTestPlan -Manifest $manifest -ChangedPaths @('../code.cpp')})) {
            $threw=$false;try{&$action|Out-Null}catch [System.Management.Automation.RuntimeException]{$threw=$true};Assert-Scope $threw 'Invalid selector accepted'
        }
    }
    Run-Case 'selection explains every changed path and accumulates integration consumers' {
        $path=if($manifest.repository -like '*/DxUi'){'src/Controls/DxUi.Tree.cpp'}else{'Plugins/Weather/Weather.cpp'}
        $plan=Get-ScopedTestPlan $manifest @($path)
        Assert-Scope ($plan.scopes.Count -gt 1 -and -not $plan.full) 'No focused integration fan-out'
        Assert-Scope ($plan.reasons[0].path -eq $path) 'Missing selection explanation'
        if($manifest.repository -like '*/DxUi') {
            $grid=Get-ScopedTestPlan $manifest @('src/Controls/DxUi.Grid.cpp')
            foreach($consumer in @('Grid','Tree','Theme','Animation','EditorControls','Tooltip','Embedded','WindowHost')) {Assert-Scope ($consumer -in $grid.scopes) "Grid caller omitted: $consumer"}
        } else {
            $systemData=Get-ScopedTestPlan $manifest @('Plugins/SystemData/SystemData.cpp')
            Assert-Scope ('SystemDataPhase0' -in $systemData.scopes) 'Referenced SystemData module did not select its phase-zero consumer'
            foreach($inputPath in @('Plugins/AVControl/AVControlModel.cpp','Plugins/Launcher/LauncherPaging.h','Plugins/Logicon/LogiconSettings.cpp','Plugins/Actions/Zoom/ZoomSettings.cpp')) {
                Assert-Scope ('Settings' -in (Get-ScopedTestPlan $manifest @($inputPath)).scopes) "Shared settings consumer omitted: $inputPath"
            }
        }
    }
    Run-Case 'only intact identical success can be reused' {
        $path=Join-Path $fixture 'evidence/pass.json';$key=Get-ScopedDigest 'identity'
        Assert-Scope (-not(Test-ScopedReceipt $path $key)) 'Missing receipt reused'
        Write-ScopedReceipt $path $key
        Assert-Scope (Test-ScopedReceipt $path $key) 'Identical success not reusable'
        Assert-Scope (-not(Test-ScopedReceipt $path (Get-ScopedDigest 'other'))) 'Changed identity reused'
        Write-Fixture $path '{broken'
        Assert-Scope (-not(Test-ScopedReceipt $path $key)) 'Corrupt receipt reused'
        Write-ScopedReceipt $path $key
        $content=Get-Content $path -Raw | ConvertFrom-Json;$content.outcome='FAILED';Write-Fixture $path ($content|ConvertTo-Json)
        Assert-Scope (-not(Test-ScopedReceipt $path $key)) 'Failed result reused'
    }
    $runtimeInputs=@('Settings/RedXe-debug.settings.json','Settings/RedXe.settings.json','Settings/RedXe.settings.schema.json','DxUi.provenance.json')
    foreach($profile in @('Debug','Release')) {
        Write-Fixture (Join-Path $fixture ".build/x64/$profile/suite.exe") 'binary'
        Write-Fixture (Join-Path $fixture ".build/x64/$profile/Plugins/runtime.dll") 'dependency'
        foreach($relative in $runtimeInputs) {Write-Fixture (Join-Path $fixture ".build/x64/$profile/$relative") 'original deployed input'}
    }
    Run-Case 'tooling identities include documentation, skills and source-origin mappings' {
        $compiled=Get-ScopedSourceIdentity $fixture -CompiledOnly
        $native=Get-ScopedRunIdentity $fixture x64 Debug Example
        foreach($path in @('README.md','.agents/skills/example/SKILL.md','Specs/Done/SourceImport/test-port.json')) {
            $before=Get-ScopedSourceIdentity $fixture
            Write-Fixture (Join-Path $fixture $path) 'changed validator input'
            Assert-Scope ((Get-ScopedSourceIdentity $fixture) -cne $before) "Validator input omitted: $path"
        }
        Assert-Scope ((Get-ScopedSourceIdentity $fixture -CompiledOnly) -ceq $compiled) 'Prose invalidated build attestation'
        Assert-Scope ((Get-ScopedRunIdentity $fixture x64 Debug Example) -ceq $native) 'Validator prose unnecessarily invalidated native success'
        Write-Fixture (Join-Path $fixture 'code.cpp') 'new implementation'
        Assert-Scope ((Get-ScopedSourceIdentity $fixture -CompiledOnly) -cne $compiled) 'Source mutation did not invalidate build attestation'
    }
    Run-Case 'uncommon executable input extensions invalidate build attestation' {
        $before=Get-ScopedSourceIdentity $fixture -CompiledOnly
        Write-Fixture (Join-Path $fixture 'include/Fixture.inl') 'changed inline implementation'
        Assert-Scope ((Get-ScopedSourceIdentity $fixture -CompiledOnly) -cne $before) 'Inline implementation was absent from build identity'
    }
    Run-Case 'runtime DLL mutation invalidates even with unchanged executable' {
        $before=Get-ScopedArtifactIdentity $fixture x64 Debug
        Write-Fixture (Join-Path $fixture '.build/x64/Debug/Plugins/runtime.dll') 'newdependency'
        Assert-Scope ((Get-ScopedArtifactIdentity $fixture x64 Debug) -cne $before) 'DLL mutation reused'
    }
    Run-Case 'deployed settings and provenance invalidate receipts while generated reports do not' {
        foreach($name in $runtimeInputs) {
            $path=Join-Path $fixture ('.build/x64/Debug/'+$name)
            Write-Fixture $path 'original deployed input'
            $key=Get-ScopedRunIdentity $fixture x64 Debug Example
            $receipt=Join-Path $fixture '.build/reports/deployed.json'
            Write-ScopedReceipt $receipt $key
            Write-Fixture $path 'changed deployed input'
            Assert-Scope (-not(Test-ScopedReceipt $receipt (Get-ScopedRunIdentity $fixture x64 Debug Example))) "Changed deployed input reused: $name"
            Remove-Item -LiteralPath $path
            $message='';try {Get-ScopedRunIdentity $fixture x64 Debug Example | Out-Null} catch [System.Management.Automation.RuntimeException] {$message=$_.Exception.Message}
            Assert-Scope ($message -eq "Missing deployed runtime input: $name") "Missing deployed input accepted: $name ($message)"
            Write-Fixture $path 'original deployed input'
            Assert-Scope (Test-ScopedReceipt $receipt (Get-ScopedRunIdentity $fixture x64 Debug Example)) "Identical deployed input not reusable: $name"
        }
        $before=Get-ScopedArtifactIdentity $fixture x64 Debug
        Write-Fixture (Join-Path $fixture '.build/x64/Debug/logs/generated-report.json') 'new report'
        Assert-Scope ((Get-ScopedArtifactIdentity $fixture x64 Debug) -ceq $before) 'Generated reports invalidated native evidence'
    }
    Run-Case 'incomplete profiles cannot establish an initial artifact identity' {
        $incomplete=Join-Path $fixture 'incomplete'
        $profile=Join-Path $incomplete '.build/x64/Debug'
        Write-Fixture (Join-Path $profile 'suite.exe') 'binary'
        $message='';try {Get-ScopedArtifactIdentity $incomplete x64 Debug | Out-Null} catch [System.Management.Automation.RuntimeException] {$message=$_.Exception.Message}
        Assert-Scope ($message -like 'Missing deployed runtime input:*') "Missing initial closure accepted: $message"
        foreach($relative in $runtimeInputs) {Write-Fixture (Join-Path $profile $relative) 'original deployed input'}
        Assert-Scope (-not [string]::IsNullOrWhiteSpace((Get-ScopedArtifactIdentity $incomplete x64 Debug))) 'Complete initial closure rejected'
        Remove-Item -LiteralPath (Join-Path $profile 'suite.exe')
        $message='';try {Get-ScopedArtifactIdentity $incomplete x64 Debug | Out-Null} catch [System.Management.Automation.RuntimeException] {$message=$_.Exception.Message}
        Assert-Scope ($message -eq 'No executable build artifacts were found.') "JSON-only profile accepted: $message"
    }
    Run-Case 'configuration, scope and sanitizer environment differ' {
        $before=Get-ScopedRunIdentity $fixture x64 Debug Example
        Assert-Scope ((Get-ScopedRunIdentity $fixture x64 Release Example) -cne $before) 'Configuration ignored'
        Assert-Scope ((Get-ScopedRunIdentity $fixture x64 Debug Other) -cne $before) 'Scope ignored'
        $options=$env:ASAN_OPTIONS
        try {$env:ASAN_OPTIONS='halt_on_error=1:fixture=changed';Assert-Scope ((Get-ScopedRunIdentity $fixture x64 Debug Example) -cne $before) 'Environment ignored'}
        finally {$env:ASAN_OPTIONS=$options}
    }
    Run-Case 'skip-build attestation rejects changed source and executable bytes' {
        $path=Join-Path $fixture 'evidence/build.json'
        $source=Get-ScopedSourceIdentity $fixture -CompiledOnly;$binary=Get-ScopedArtifactIdentity $fixture x64 Debug
        Write-ScopedReceipt $path (Get-ScopedDigest ($source+"`n"+$binary))
        Write-Fixture (Join-Path $fixture '.build/x64/Debug/suite.exe') 'differentbinary'
        Assert-Scope (-not(Test-ScopedReceipt $path (Get-ScopedDigest ($source+"`n"+(Get-ScopedArtifactIdentity $fixture x64 Debug))))) 'Stale executable accepted'
        Write-Fixture (Join-Path $fixture 'code.cpp') 'another implementation'
        Assert-Scope (-not(Test-ScopedReceipt $path (Get-ScopedDigest ((Get-ScopedSourceIdentity $fixture -CompiledOnly)+"`n"+$binary)))) 'Stale source accepted'
    }
    Run-Case 'PR delegation binds clean committed bytes and rejects concurrent changes' {
        $delegate=Join-Path $fixture 'delegation'
        function Invoke-DelegateGit([string[]]$Arguments) {& git -C $delegate @Arguments *> $null;if($LASTEXITCODE){throw "Delegation fixture git failed: $Arguments"}}
        [void](New-Item -ItemType Directory -Path $delegate -Force)
        $workflow="name: fixture`non:`n  pull_request:`n"
        [void](New-Item -ItemType Directory -Path (Join-Path $delegate '.github/workflows') -Force)
        [IO.File]::WriteAllText((Join-Path $delegate '.github/workflows/ci.yml'),$workflow)
        [IO.File]::WriteAllText((Join-Path $delegate 'code.cpp'),'initial')
        Invoke-DelegateGit @('init','-q')
        Invoke-DelegateGit @('config','user.name','Delegation fixture')
        Invoke-DelegateGit @('config','user.email','fixture@example.invalid')
        Invoke-DelegateGit @('add','-A');Invoke-DelegateGit @('commit','-q','-m','baseline')
        Invoke-DelegateGit @('update-ref','refs/remotes/origin/main','HEAD')
        $coverage=[pscustomobject]@{repository='fixture/example';defaultBranch='main';prWorkflowDigest=(Get-ScopedDigest $workflow);prCoverage=@([pscustomobject]@{platform='x64';configuration='Release';scopes=@('Example')})}
        $previousGh=Get-Item Function:\global:gh -ErrorAction SilentlyContinue
        $global:ScopedFixtureGhCalls=0;$global:ScopedFixtureGhMutation=''
        $global:ScopedFixtureGhRoot=$delegate
        function global:gh {
            param([Parameter(ValueFromRemainingArguments=$true)][string[]]$FixtureGhArguments)
            $global:ScopedFixtureGhCalls++;$global:LASTEXITCODE=0
            if($global:ScopedFixtureGhMutation -eq 'untracked') {[IO.File]::WriteAllText((Join-Path $global:ScopedFixtureGhRoot 'during-api.cpp'),'changed')}
            if($global:ScopedFixtureGhMutation -eq 'commit') {
                [IO.File]::WriteAllText((Join-Path $global:ScopedFixtureGhRoot 'code.cpp'),'concurrent committed change')
                & git -C $global:ScopedFixtureGhRoot add code.cpp *> $null
                & git -C $global:ScopedFixtureGhRoot commit -q -m 'concurrent change' *> $null
            }
            '{"state":"active"}'
        }
        try {
            Assert-Scope (@(Get-ScopedPrCoverage $delegate $coverage x64 Release) -contains 'Example') 'Clean committed candidate was not delegated'
            foreach($state in @('untracked','unstaged','staged')) {
                $path=Join-Path $delegate $(if($state -eq 'untracked'){'pending.cpp'}else{'code.cpp'})
                [IO.File]::WriteAllText($path,'pending')
                if($state -eq 'staged'){Invoke-DelegateGit @('add','code.cpp')}
                $calls=$global:ScopedFixtureGhCalls
                Assert-Scope (@(Get-ScopedPrCoverage $delegate $coverage x64 Release).Count -eq 0) "Dirty candidate delegated: $state"
                Assert-Scope ($global:ScopedFixtureGhCalls -eq $calls) 'Dirty candidate consulted CI before retaining obligations locally'
                if($state -eq 'untracked'){Remove-Item -LiteralPath $path}else{Invoke-DelegateGit @('restore','--staged','--worktree','--','code.cpp')}
            }
            $global:ScopedFixtureGhMutation='untracked'
            Assert-Scope (@(Get-ScopedPrCoverage $delegate $coverage x64 Release).Count -eq 0) 'Mutation during API lookup was delegated'
            Remove-Item -LiteralPath (Join-Path $delegate 'during-api.cpp')
            $global:ScopedFixtureGhMutation='commit'
            Assert-Scope (@(Get-ScopedPrCoverage $delegate $coverage x64 Release).Count -eq 0) 'Concurrent committed candidate change was delegated'
        } finally {
            if($previousGh){Set-Item Function:\global:gh -Value $previousGh.ScriptBlock}else{Remove-Item Function:\global:gh}
            Remove-Variable -Name ScopedFixtureGhCalls,ScopedFixtureGhMutation,ScopedFixtureGhRoot -Scope Global
        }
    }

    Run-Case 'a profile outside actual PR coverage cannot be delegated' {Assert-Scope (@(Get-ScopedPrCoverage $repository $manifest Unknown Unknown).Count -eq 0) 'Unknown PR profile delegated'}
    Run-Case 'PR coverage accounts for conditional native jobs without an API dependency' {
        $profile=$manifest.prCoverage[0]
        $documentation=@(Get-ScopedPrCandidateScopes $repository $manifest $profile.platform $profile.configuration @('README.md'))
        $code=@(Get-ScopedPrCandidateScopes $repository $manifest $profile.platform $profile.configuration @('src/Controls/DxUi.Grid.cpp'))
        if($manifest.repository -like '*/DxUi') {
            Assert-Scope ($documentation.Count -eq 1 -and $documentation[0] -eq 'Tooling') 'Skipped native PR jobs were counted as coverage'
            Assert-Scope ($code.Count -eq $manifest.scopes.Count) 'Native PR coverage was narrowed for code edits'
        } else {
            Assert-Scope ($documentation.Count -eq $manifest.scopes.Count -and $code.Count -eq $manifest.scopes.Count) 'Unconditional RedXe PR gate was incorrectly narrowed'
        }
    }
    Run-Case 'reviewed CI preserves full native coverage and separates independent tooling' {
        $workflow=[IO.File]::ReadAllText((Join-Path $repository '.github/workflows/ci.yml')) -replace "`r`n","`n"
        Assert-Scope ((Get-ScopedDigest $workflow) -ceq $manifest.prWorkflowDigest) 'CI contract digest is stale'
        Assert-Scope ($workflow -match 'test.ps1 -Full -SkipTooling') 'CI accidentally uses affected iteration or repeats independent tooling'
        $errors=$null;$tokens=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repository 'test.ps1'),[ref]$tokens,[ref]$errors)
        Assert-Scope (@($errors).Count -eq 0) 'Native entrypoint has parse errors'
        $defaults=($ast.ParamBlock.Parameters | Where-Object {$_.Name.VariablePath.UserPath -eq 'Suites'}).DefaultValue.Extent.Text
        foreach($scope in $manifest.scopes){
            Assert-Scope ($defaults.Contains("'$($scope.name)'") -or -not $scope.native) "CI native default misses $($scope.name)"
        }
        foreach($profile in $manifest.prCoverage){foreach($scope in $profile.scopes){Assert-Scope ($scope -in $manifest.scopes.name) "PR profile declares nonexistent scope $scope"}}
        if($manifest.repository -like '*/DxUi') {
            Assert-Scope ($manifest.prCoverage.Count -eq 6 -and $workflow.Contains('MenuResourceScaling,MenuTextLayoutResources')) 'DxUi profile/resource coverage was narrowed'
            Assert-Scope ($workflow.Contains('run: ./validate.ps1')) 'Portable tooling gate missing'
        } else {
            Assert-Scope ($manifest.prCoverage.Count -eq 1 -and $manifest.prCoverage[0].configuration -eq 'Release' -and $manifest.prCoverage[0].platform -eq 'x64') 'RedXe PR coverage overclaimed'
            Assert-Scope ($workflow.Contains('./Tests/BuildProcessTests/Invoke-ToolingTests.ps1')) 'Independent tooling job missing'
        }
        $digest=$manifest.prWorkflowDigest
        try {$manifest.prWorkflowDigest='stale';$profile=$manifest.prCoverage[0];Assert-Scope (@(Get-ScopedPrCoverage $repository $manifest $profile.platform $profile.configuration).Count -eq 0) 'Modified workflow was delegated'}
        finally {$manifest.prWorkflowDigest=$digest}
    }
    Run-Case 'public runner executes once, reuses success and never retains failed or mutated runs' {
        $sandbox=Join-Path $fixture 'runner'
        Write-Fixture (Join-Path $sandbox '.gitignore') '.build/'
        Copy-Item -LiteralPath (Join-Path $repository 'Test-Changes.ps1') -Destination (Join-Path $sandbox 'Test-Changes.ps1')
        $moduleRelative=if ($manifest.repository -like '*/DxUi') {'Tools/ScopedTesting.psm1'} else {'Build/ScopedTesting.psm1'}
        Write-Fixture (Join-Path $sandbox $moduleRelative) ([IO.File]::ReadAllText((Join-Path $repository $moduleRelative)))
        Write-Fixture (Join-Path $sandbox 'Tests/Example.Tests.Case.cpp') 'int example;'
        Write-Fixture (Join-Path $sandbox 'Specs/TestRuns/history/SelfTest/Old.SelfTest.cpp') 'immutable historical source;'
        Write-Fixture (Join-Path $sandbox 'Tests/native-test-files.json') '["Tests/Example.Tests.Case.cpp"]'
        Write-Fixture (Join-Path $sandbox 'Tests/test-scopes.json') '{"version":1,"defaultBranch":"main","repository":"fixture/example","scopes":[{"name":"Example","native":true,"reuse":true}],"rules":[],"toolingCommands":[],"prCoverage":[],"prWorkflowDigest":"none"}'
        Write-Fixture (Join-Path $sandbox 'build.ps1') @'
param($Platform,$Configuration)
$output=Join-Path $PSScriptRoot ".build/$Platform/$Configuration"
New-Item -ItemType Directory -Path $output -Force|Out-Null
[IO.File]::WriteAllText((Join-Path $output 'example.exe'),'fixture binary')
foreach($relative in @('Settings/RedXe-debug.settings.json','Settings/RedXe.settings.json','Settings/RedXe.settings.schema.json','DxUi.provenance.json')) {
    $path=Join-Path $output $relative
    [void](New-Item -ItemType Directory -Path (Split-Path $path) -Force)
    [IO.File]::WriteAllText($path,'fixture deployed input')
}
exit 0
'@
        Write-Fixture (Join-Path $sandbox 'test.ps1') @'
param($Platform,$Configuration,[switch]$SkipBuild,[string[]]$Suites,[switch]$SkipTooling)
[IO.File]::AppendAllText((Join-Path $PSScriptRoot '.build/calls.txt'),"execute`n")
if(Test-Path (Join-Path $PSScriptRoot '.build/fail')){exit 1}
if(Test-Path (Join-Path $PSScriptRoot '.build/mutate')){[IO.File]::AppendAllText((Join-Path $PSScriptRoot 'Tests/Example.Tests.Case.cpp'),'changed')}
exit 0
'@
        & git -C $sandbox init -q
        if ($LASTEXITCODE) {throw 'Runner fixture git init failed'}
        $fixturePlatform=if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -eq 'Arm64') {'ARM64'} else {'x64'}
        function Invoke-PublicRunner([string[]]$Arguments) {
            $start=[Diagnostics.ProcessStartInfo]::new((Join-Path $PSHOME 'pwsh.exe'))
            if (-not(Test-Path $start.FileName)) {$start.FileName=Join-Path $PSHOME 'pwsh'}
            $start.UseShellExecute=$false;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
            foreach($argument in @('-NoProfile','-File',(Join-Path $sandbox 'Test-Changes.ps1'),'-Scopes','Example','-Platform',$fixturePlatform)+$Arguments){$start.ArgumentList.Add($argument)}
            $process=[Diagnostics.Process]::Start($start)
            try {$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$process.WaitForExit();return [pscustomobject]@{Exit=$process.ExitCode;Text=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()}}
            finally {$process.Dispose()}
        }
        $run=Invoke-PublicRunner @();Assert-Scope ($run.Exit -eq 0) "Initial execution failed: $($run.Text)"
        $run=Invoke-PublicRunner @('-SkipBuild');Assert-Scope ($run.Exit -eq 0 -and $run.Text.Contains('REUSED Example')) "Identical success was executed again: $($run.Text)"
        Assert-Scope (@(Get-Content (Join-Path $sandbox '.build/calls.txt')).Count -eq 1) 'Cached call executed runtime'
        $receipt=Join-Path $sandbox ".build/reports/scoped-tests/$fixturePlatform-Debug/Example.json"
        Write-Fixture (Join-Path $sandbox '.build/fail') 'fail'
        $run=Invoke-PublicRunner @('-Force','-SkipBuild');Assert-Scope ($run.Exit -ne 0 -and -not(Test-Path $receipt)) 'Failure retained earlier reusable success'
        Remove-Item -LiteralPath (Join-Path $sandbox '.build/fail')
        $run=Invoke-PublicRunner @('-SkipBuild');Assert-Scope ($run.Exit -eq 0 -and -not $run.Text.Contains('REUSED Example')) 'Failed run was reused'
        Write-Fixture (Join-Path $sandbox '.build/mutate') 'mutate'
        $run=Invoke-PublicRunner @('-Force','-SkipBuild');Assert-Scope ($run.Exit -ne 0 -and -not(Test-Path $receipt)) 'Post-run mutation published success'
        Remove-Item -LiteralPath (Join-Path $sandbox '.build/mutate')
        $run=Invoke-PublicRunner @('-SkipBuild');Assert-Scope ($run.Exit -ne 0 -and $run.Text.Contains('SkipBuild refused')) 'Stale compiled input accepted'
    }
    Write-Host "Scoped testing: $passed cases passed."
} finally {
    $resolved=[IO.Path]::GetFullPath($fixture);$allowed=[IO.Path]::GetFullPath((Join-Path $repository '.build/ToolTests'))+[IO.Path]::DirectorySeparatorChar
    if(-not $resolved.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Fixture cleanup escaped its owned root.'}
    if(Test-Path -LiteralPath $resolved){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
exit 0
