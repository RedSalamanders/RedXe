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
function Invoke-FixtureGit([string[]]$Arguments) {$output=& git -C $fixture @Arguments 2>&1;if($LASTEXITCODE){throw "Fixture git failed: $Arguments`n$($output -join "`n")"}}
# A fixture repository ignores the developer's Git settings that change or block a commit: signing (a key prompt would hang the
# run), hooks (core.hooksPath, or an init template's) and line-ending conversion. Local settings override global and system ones.
function Initialize-FixtureRepository([string]$Root) {
    foreach($arguments in @(@('init','-q'),@('config','commit.gpgsign','false'),@('config','core.hooksPath',(Join-Path $Root '.git/no-hooks')),@('config','core.autocrlf','false'))) {
        $output=& git -C $Root @arguments 2>&1;if($LASTEXITCODE){throw "Fixture git failed: $arguments`n$($output -join "`n")"}
    }
    [void](New-Item -ItemType Directory -Path (Join-Path $Root '.git/no-hooks') -Force)
}
# test.ps1 runs a suite only inside its `if ('<Scope>' -in $Suites)` block.
function Get-TestSuiteBlocks([string]$Path) {
    $errors=$null;$tokens=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$errors)
    if(@($errors).Count){throw "Native entrypoint has parse errors: $Path"}
    foreach($statement in $ast.FindAll({param($node) $node -is [Management.Automation.Language.IfStatementAst]},$true)) {
        foreach($clause in $statement.Clauses) {if($clause.Item1.Extent.Text -match '^''(\w+)'' -in \$Suites$'){[pscustomobject]@{Name=$Matches[1];Body=$clause.Item2}}}
    }
}
# Each workflow test.ps1 call, by job. A call runs the Python tooling suite unless it passes -SkipTooling, so it is
# provisioned only then or when its job installs Build/requirements-validation.txt. Comment lines are not calls.
function Get-WorkflowTestCalls([string]$Directory) {
    foreach($file in @(Get-ChildItem -LiteralPath $Directory -File | Where-Object {$_.Extension -in '.yml','.yaml'})) {
        $jobs=[ordered]@{};$job=$null;$indent=-1;$inJobs=$false
        foreach($line in @([IO.File]::ReadAllLines($file.FullName) | Where-Object {$_ -notmatch '^\s*#'})) {
            if($line -match '^\S') {$inJobs=$line -match '^jobs:\s*$';$job=$null;continue}
            if(-not $inJobs) {continue}
            if($line -match '^(\s+)([\w-]+):\s*$' -and ($indent -lt 0 -or $Matches[1].Length -eq $indent)) {$indent=$Matches[1].Length;$job=$Matches[2];$jobs[$job]=@();continue}
            if($job) {$jobs[$job]+=$line}
        }
        foreach($name in $jobs.Keys) {
            $installs=@($jobs[$name] -match 'pip\s+install\s.*-r\s+[''"]?Build[\\/]requirements-validation\.txt').Count -gt 0
            foreach($call in @($jobs[$name] -match '(?<![\w./\\-])(?:\.[\\/])?test\.ps1(?![\w.])')) {
                [pscustomobject]@{Workflow=$file.Name;Job=$name;Call=$call.Trim();Provisioned=$installs -or $call -match '(?<!\S)-SkipTooling(?!\S)'}
            }
        }
    }
}
$fixture=Join-Path $repository ('.build/ToolTests/ScopedTesting-'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixture -Force)
try {
    Initialize-FixtureRepository $fixture
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
            Assert-Scope ($message -match 'Invalid/missing native test source:|Historical source cannot enter') "Invalid inventory accepted: $invalid ($message)"
        }
    }
    Run-Case 'test sources are found by name component, not by the letters test, mock or fake inside a word' {
        $detector=Join-Path $fixture 'detector'
        Write-Fixture (Join-Path $detector 'Tests/Scope.Tests.Case.cpp') 'declared test source'
        Write-Fixture (Join-Path $detector 'Tests/native-test-files.json') '["Tests/Scope.Tests.Case.cpp"]'
        foreach($product in @('Common/Attestation.h','RedXe/LatestRelease.cpp','Plugins/Weather/ContestedLock.h','Plugins/Weather/Mockingbird.h','Plugins/Weather/Fakery.cpp')) {Write-Fixture (Join-Path $detector $product) 'product source'}
        Assert-Scope ((Assert-ScopedTestNames $detector) -eq 1) 'Product names were taken for test sources'
        foreach($test in @('Plugins/Weather/FakeClock.h','Plugins/Weather/MockHost.cpp','Plugins/Weather/WeatherTest.cpp','Plugins/Weather/Weather.tests.Contract.h')) {
            Write-Fixture (Join-Path $detector $test) 'undeclared test source'
            $message='';try {Assert-ScopedTestNames $detector | Out-Null} catch [System.Management.Automation.RuntimeException] {$message=$_.Exception.Message}
            Assert-Scope ($message -eq "Native test source is absent from Tests/native-test-files.json: $test") "An undeclared test source was not found: $test ($message)"
            Remove-Item -LiteralPath (Join-Path $detector $test)
        }
    }
    Run-Case 'committed, staged, working, deleted, both rename sides and spaced untracked paths count' {
        $paths=@(Get-ScopedChangedPaths $fixture $baseline)
        foreach($path in @('committed.cpp','staged.cpp','code.cpp','deleted.cpp','renamed.cpp','renamed-new.cpp','untracked file.cpp')) {Assert-Scope ($path -in $paths) "Missing impact: $path"}
    }
    Run-Case 'a missing comparison base or one without shared history names the failing command and the full gate' {
        Assert-Scope ((Get-ScopedComparisonProblem $fixture $baseline) -eq '') 'A resolvable base was reported unusable'
        $missing=Get-ScopedComparisonProblem $fixture 'refs/remotes/origin/no-such-fixture-branch'
        Assert-Scope ($missing -like "*'refs/remotes/origin/no-such-fixture-branch', which does not resolve here*./test.ps1 -Full*git rev-parse --verify --quiet*exited with code*") "A missing base was not explained: $missing"
        # git merge-base exits 1 without a message when the histories share no commit, as with a parentless commit.
        $unrelated=(& git -C $fixture commit-tree (& git -C $fixture rev-parse 'HEAD^{tree}').Trim() -m 'unrelated root').Trim()
        Assert-Scope ($LASTEXITCODE -eq 0) "Fixture git failed to create an unrelated commit: $unrelated"
        $message='';try {Get-ScopedChangedPaths $fixture $unrelated | Out-Null} catch [System.Management.Automation.RuntimeException] {$message=$_.Exception.Message}
        Assert-Scope ($message -like "HEAD shares no history with '$unrelated'*git fetch --unshallow*./test.ps1 -Full*git merge-base $unrelated HEAD exited with code 1: (no message)") "A base without shared history was not explained: $message"
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
        # Nothing builds, tests or validates the HTML mockups.
        Assert-Scope ((Get-ScopedTestPlan $manifest @('Mockups/av-control.html')).scopes.Count -eq 0) 'A mockup selected tests'
    }
    Run-Case 'invalid explicit scope and nonrelative paths fail before execution' {
        foreach($action in @({Get-ScopedTestPlan -Manifest $manifest -ChangedPaths @() -Scopes @('Typo')},{Get-ScopedTestPlan -Manifest $manifest -ChangedPaths @('../code.cpp')})) {
            $threw=$false;try{&$action|Out-Null}catch [System.Management.Automation.RuntimeException]{$threw=$true};Assert-Scope $threw 'Invalid selector accepted'
        }
    }
    Run-Case 'Test-Changes.ps1 help examples name only manifest scopes' {
        $errors=$null;$tokens=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repository 'Test-Changes.ps1'),[ref]$tokens,[ref]$errors)
        $examples=@($ast.GetHelpContent().Examples)
        Assert-Scope ($examples.Count -gt 0) 'Test-Changes.ps1 has no help examples'
        foreach($match in @($examples | ForEach-Object {[regex]::Matches($_,'-Scopes\s+(\S+)')})) {
            foreach($scope in $match.Groups[1].Value -split ',') {Assert-Scope ($scope -cin $manifest.scopes.name) "A Test-Changes.ps1 help example names the unknown scope '$scope'"}
        }
    }
    Run-Case 'selection explains every changed path and accumulates integration consumers' {
        $path='Plugins/Weather/Weather.cpp'
        $plan=Get-ScopedTestPlan $manifest @($path)
        Assert-Scope ($plan.scopes.Count -gt 1 -and -not $plan.full) 'No focused integration fan-out'
        Assert-Scope ($plan.reasons[0].path -eq $path) 'Missing selection explanation'
        $systemData=Get-ScopedTestPlan $manifest @('Plugins/SystemData/SystemData.cpp')
        Assert-Scope ('SystemDataPhase0' -in $systemData.scopes) 'Referenced SystemData module did not select its phase-zero consumer'
        foreach($inputPath in @('Plugins/AVControl/AVControlModel.cpp','Plugins/Launcher/LauncherPaging.h','Plugins/Logicon/LogiconSettings.cpp','Plugins/Actions/Zoom/ZoomSettings.cpp')) {
            Assert-Scope ('Settings' -in (Get-ScopedTestPlan $manifest @($inputPath)).scopes) "Shared settings consumer omitted: $inputPath"
        }
    }
    Run-Case 'test project files reach the shared-output consumers, and skill tooling selects only the tooling scope' {
        $project=Get-ScopedTestPlan $manifest @('Tests/SettingsTests/SettingsTests.vcxproj')
        foreach($consumer in @('Settings','PluginContract','HostPlugin','HostSmoke','Packaging')) {Assert-Scope ($consumer -in $project.scopes) "A test project edit omitted $consumer"}
        Assert-Scope (-not $project.full) 'A test project edit fell back to every scope'
        Assert-Scope (((Get-ScopedTestPlan $manifest @('Tests/SettingsTests/SettingsTests.vcxproj.filters')).scopes -join ',') -eq 'Settings') 'An IDE-only filters edit left its own suite'
        foreach($path in @('.agents/skills/yyjson/agents/openai.yaml','validate-skills.ps1','Build/validate_skills.py','Build/requirements-validation.txt')) {
            $plan=Get-ScopedTestPlan $manifest @($path)
            Assert-Scope (($plan.scopes -join ',') -eq 'BuildProcess') "Skill tooling input $path selected $($plan.scopes -join ', ')"
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
    Run-Case 'tooling identities include documentation, skills, plan records and mockups, which build attestation leaves out' {
        $compiled=Get-ScopedSourceIdentity $fixture -CompiledOnly
        $native=Get-ScopedRunIdentity $fixture x64 Debug Example
        foreach($path in @('README.md','.agents/skills/example/SKILL.md','Specs/Plans/Done/Example/receipt.json','Mockups/example.html')) {
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
    Run-Case 'non-ASCII paths stay in the change set and the source identity under a legacy console code page' {
        $name="M$([char]0xE9)t$([char]0xE9)o.cpp"
        $path=Join-Path $fixture $name
        Write-Fixture $path 'initial'
        # Git writes UTF-8. A decoder that follows the console code page turns this name into one that does not exist.
        $previous=[Console]::OutputEncoding;$legacy=$false
        try {
            try {[Console]::OutputEncoding=[Text.Encoding]::GetEncoding(850);$legacy=$true} catch [System.Management.Automation.RuntimeException] {}
            $listed=@(Get-ScopedChangedPaths $fixture $baseline) -ccontains $name
            $before=Get-ScopedSourceIdentity $fixture -CompiledOnly
            Write-Fixture $path 'edited'
            $after=Get-ScopedSourceIdentity $fixture -CompiledOnly
        } finally {if($legacy){[Console]::OutputEncoding=$previous}}
        Assert-Scope $listed 'A non-ASCII path was missing from the change set'
        Assert-Scope ($after -cne $before) 'Editing a non-ASCII path left the source identity unchanged'
    }
    Run-Case 'only paths Git reports deleted may leave the source identity' {
        $absent=Join-Path $fixture 'absent'
        foreach($file in @('kept.cpp','deleted.cpp','unresolvable.cpp')) {Write-Fixture (Join-Path $absent $file) 'initial'}
        Initialize-FixtureRepository $absent
        # A skip-worktree entry stands in for a name that does not resolve: Git lists it, it is not on disk, and Git does
        # not report it deleted.
        foreach($arguments in @(@('config','user.name','Absent fixture'),@('config','user.email','fixture@example.invalid'),@('add','-A'),@('commit','-q','-m','baseline'),@('update-index','--skip-worktree','unresolvable.cpp'))) {
            $output=& git -C $absent @arguments 2>&1;if($LASTEXITCODE){throw "Fixture git failed: $arguments`n$($output -join "`n")"}
        }
        $before=Get-ScopedSourceIdentity $absent
        Remove-Item -LiteralPath (Join-Path $absent 'deleted.cpp')
        Assert-Scope ((Get-ScopedSourceIdentity $absent) -cne $before) 'A deleted file did not leave the source identity'
        Remove-Item -LiteralPath (Join-Path $absent 'unresolvable.cpp')
        $message='';try {Get-ScopedSourceIdentity $absent | Out-Null} catch [System.Management.Automation.RuntimeException] {$message=$_.Exception.Message}
        Assert-Scope ($message -like "Source identity input 'unresolvable.cpp' is listed by Git but is neither on disk nor deleted.*") "An unresolvable path silently left the source identity: $message"
    }
    Run-Case 'scoped builds are stamped with the merge base commit count, which a commit leaves unchanged' {
        Assert-Scope ((Get-ScopedBuildNumber $fixture $baseline) -eq 1) 'The scoped build number followed HEAD instead of the merge base'
        Import-Module (Join-Path $repository 'Build/Versioning.psm1') -Force
        $default=Resolve-RedXeBuildNumber -RepoRoot $repository -WarningAction SilentlyContinue
        Assert-Scope ((Get-ScopedBuildNumber $repository 'refs/remotes/origin/no-such-fixture-branch') -eq $default) 'A missing base did not fall back to the default build number'
    }
    Run-Case 'environment identity covers OS servicing, graphics and imaging runtimes, Git and the tooling interpreter' {
        $inputs=@(Get-ScopedEnvironmentInputs)
        foreach($label in @('os-build=','d2d1.dll=','dxgi.dll=','windowscodecs.dll=','git=')) {
            Assert-Scope (@($inputs | Where-Object {"$_".StartsWith($label)}).Count -eq 1) "Environment identity omits $label"
        }
        $tooling=@(Get-ScopedEnvironmentInputs -Tooling)
        Assert-Scope (@($tooling | Where-Object {"$_".StartsWith('python=')}).Count -eq 1 -and @($inputs | Where-Object {"$_".StartsWith('python=')}).Count -eq 0) 'The Python identity is not limited to tooling evidence'
        Assert-Scope ((Get-ScopedEnvironmentIdentity -Tooling) -cne (Get-ScopedEnvironmentIdentity)) 'Tooling evidence ignores its interpreter'
    }
    Run-Case 'runtime platform rule matches test.ps1 on x64 and ARM64 hosts' {
        foreach($pair in @(@('x64','X64'),@('x64','Arm64'),@('ARM64','Arm64'))) {Assert-ScopedRuntimePlatform $pair[0] $pair[1]}
        $message='';try {Assert-ScopedRuntimePlatform ARM64 X64} catch [System.Management.Automation.RuntimeException] {$message=$_.Exception.Message}
        Assert-Scope ($message -and [IO.File]::ReadAllText((Join-Path $repository 'test.ps1')).Contains("throw '$message'")) "ARM64 on an x64 host is not refused as test.ps1 refuses it: $message"
    }
    Run-Case 'PR delegation binds clean committed bytes, the reviewed runner and required checks, and names each refusal' {
        $delegate=Join-Path $fixture 'delegation'
        function Invoke-DelegateGit([string[]]$Arguments) {$output=& git -C $delegate @Arguments 2>&1;if($LASTEXITCODE){throw "Delegation fixture git failed: $Arguments`n$($output -join "`n")"}}
        [void](New-Item -ItemType Directory -Path $delegate -Force)
        $workflow="name: fixture`non:`n  pull_request:`n"
        [void](New-Item -ItemType Directory -Path (Join-Path $delegate '.github/workflows') -Force)
        [IO.File]::WriteAllText((Join-Path $delegate '.github/workflows/ci.yml'),$workflow)
        [IO.File]::WriteAllText((Join-Path $delegate 'code.cpp'),'initial')
        Initialize-FixtureRepository $delegate
        Invoke-DelegateGit @('config','user.name','Delegation fixture')
        Invoke-DelegateGit @('config','user.email','fixture@example.invalid')
        Invoke-DelegateGit @('add','-A');Invoke-DelegateGit @('commit','-q','-m','baseline')
        Invoke-DelegateGit @('update-ref','refs/remotes/origin/main','HEAD')
        # One check runs the profile-independent tooling for every profile, the other one native profile.
        $coverage=[pscustomobject]@{repository='fixture/example';defaultBranch='main';prWorkflowDigest=(Get-ScopedDigest $workflow);toolingCommands=@('tooling.ps1')
            prCoverage=@([pscustomobject]@{check='tooling';scopes=@('Docs')},[pscustomobject]@{check='native (x64, Release)';platform='x64';configuration='Release';scopes=@('Example')})}
        $required='[{"type":"pull_request"},{"type":"required_status_checks","parameters":{"required_status_checks":[{"context":"tooling"},{"context":"native (x64, Release)"}]}}]'
        $previousGh=Get-Item Function:\global:gh -ErrorAction SilentlyContinue
        $global:ScopedFixtureGhCalls=0;$global:ScopedFixtureGhMutation='';$global:ScopedFixtureGhRules=$required;$global:ScopedFixtureGhExit=0
        $global:ScopedFixtureGhRoot=$delegate
        function global:gh {
            param([Parameter(ValueFromRemainingArguments=$true)][string[]]$FixtureGhArguments)
            $global:ScopedFixtureGhCalls++;$global:LASTEXITCODE=$global:ScopedFixtureGhExit
            if($global:ScopedFixtureGhExit) {return}
            if($global:ScopedFixtureGhMutation -eq 'untracked') {[IO.File]::WriteAllText((Join-Path $global:ScopedFixtureGhRoot 'during-api.cpp'),'changed')}
            if($global:ScopedFixtureGhMutation -eq 'commit') {
                [IO.File]::WriteAllText((Join-Path $global:ScopedFixtureGhRoot 'code.cpp'),'concurrent committed change')
                & git -C $global:ScopedFixtureGhRoot add code.cpp *> $null
                & git -C $global:ScopedFixtureGhRoot commit -q -m 'concurrent change' *> $null
            }
            if(($FixtureGhArguments -join ' ') -like '*/rules/branches/main*') {$global:ScopedFixtureGhRules} else {'{"state":"active"}'}
        }
        function Get-Delegation([string]$Platform='x64',[string]$Configuration='Release',[object]$Manifest=$coverage) {
            $refusal='';$scopes=@(Get-ScopedPrCoverage $delegate $Manifest $Platform $Configuration -Refusal ([ref]$refusal))
            [pscustomobject]@{Scopes=$scopes;Refusal=$refusal}
        }
        try {
            $result=Get-Delegation
            Assert-Scope ($result.Scopes -contains 'Example' -and $result.Scopes -contains 'Docs' -and $result.Refusal -eq '') "Clean committed candidate was not delegated: $($result.Refusal)"
            # The tooling check runs whatever the profile; a profile no native check runs keeps its native work local.
            foreach($profile in @(@('x64','Debug'),@('ARM64','ASan Debug'),@('Unknown','Unknown'))) {
                $result=Get-Delegation $profile[0] $profile[1]
                Assert-Scope ($result.Scopes.Count -eq 1 -and $result.Scopes[0] -eq 'Docs' -and $result.Refusal -eq '') "Only the profile-independent tooling may be delegated for $profile"
            }
            # A check the default branch does not require is no gate, since a failing or pending run can still merge.
            $global:ScopedFixtureGhRules='[{"type":"required_status_checks","parameters":{"required_status_checks":[{"context":"tooling"}]}}]'
            $result=Get-Delegation
            Assert-Scope ($result.Scopes.Count -eq 1 -and $result.Scopes[0] -eq 'Docs' -and $result.Refusal -like "main does not require the PR check 'native (x64, Release)'*") "An unrequired native check was trusted: $($result.Refusal)"
            $global:ScopedFixtureGhRules='[{"type":"pull_request"}]'
            $result=Get-Delegation
            Assert-Scope ($result.Scopes.Count -eq 0 -and $result.Refusal -like "main does not require the PR check 'tooling', 'native (x64, Release)'*") "Unrequired checks were trusted: $($result.Refusal)"
            $global:ScopedFixtureGhRules=$required
            $global:ScopedFixtureGhExit=4
            $result=Get-Delegation
            Assert-Scope ($result.Scopes.Count -eq 0 -and $result.Refusal -like 'gh api repos/fixture/example/actions/workflows/ci.yml exited with code 4*') "A gh failure was not reported: $($result.Refusal)"
            $global:ScopedFixtureGhExit=0
            foreach($state in @('untracked','unstaged','staged')) {
                $path=Join-Path $delegate $(if($state -eq 'untracked'){'pending.cpp'}else{'code.cpp'})
                [IO.File]::WriteAllText($path,'pending')
                if($state -eq 'staged'){Invoke-DelegateGit @('add','code.cpp')}
                $calls=$global:ScopedFixtureGhCalls
                $result=Get-Delegation
                Assert-Scope ($result.Scopes.Count -eq 0 -and $result.Refusal -like '*uncommitted or untracked changes*') "Dirty candidate delegated: $state ($($result.Refusal))"
                Assert-Scope ($global:ScopedFixtureGhCalls -eq $calls) 'Dirty candidate consulted CI before retaining obligations locally'
                if($state -eq 'untracked'){Remove-Item -LiteralPath $path}else{Invoke-DelegateGit @('restore','--staged','--worktree','--','code.cpp')}
            }
            # An unreviewed workflow, or one no PR runs, is refused before GitHub is asked. Only a fixture shows it: CI's tooling
            # job has no gh credentials, so there every lookup fails anyway.
            $calls=$global:ScopedFixtureGhCalls
            $stale=$coverage.PSObject.Copy();$stale.prWorkflowDigest='stale'
            $result=Get-Delegation -Manifest $stale
            Assert-Scope ($result.Scopes.Count -eq 0 -and $result.Refusal -like '*is not the workflow reviewed in Tests/test-scopes.json (prWorkflowDigest)') "Modified workflow was delegated: $($result.Refusal)"
            $pushOnly="name: fixture`non:`n  push:`n"
            [IO.File]::WriteAllText((Join-Path $delegate '.github/workflows/ci.yml'),$pushOnly)
            Invoke-DelegateGit @('commit','-q','-am','push-only workflow')
            $untriggered=$coverage.PSObject.Copy();$untriggered.prWorkflowDigest=Get-ScopedDigest $pushOnly
            $result=Get-Delegation -Manifest $untriggered
            Assert-Scope ($result.Scopes.Count -eq 0 -and $result.Refusal -like '*has no pull_request trigger') "A workflow without a pull_request trigger was delegated: $($result.Refusal)"
            Invoke-DelegateGit @('reset','-q','--hard','HEAD~1')
            # The PR runs the workflow and runner of its merge with main, so one main changed after the branch point is not
            # the one reviewed here.
            [IO.File]::WriteAllText((Join-Path $delegate '.github/workflows/ci.yml'),"$workflow# changed on main`n")
            Invoke-DelegateGit @('commit','-q','-am','main changes the workflow')
            Invoke-DelegateGit @('update-ref','refs/remotes/origin/main','HEAD')
            Invoke-DelegateGit @('reset','-q','--hard','HEAD~1')
            $result=Get-Delegation
            Assert-Scope ($result.Scopes.Count -eq 0 -and $result.Refusal -like 'origin/main changed .github/workflows/ci.yml after this branch left it*') "A runner changed on main was delegated: $($result.Refusal)"
            Assert-Scope ($global:ScopedFixtureGhCalls -eq $calls) 'An unreviewed runner consulted CI before retaining obligations locally'
            Invoke-DelegateGit @('update-ref','refs/remotes/origin/main','HEAD')
            Assert-Scope ((Get-Delegation).Scopes.Count -eq 2) 'The restored candidate was not delegated'
            $global:ScopedFixtureGhMutation='untracked'
            $result=Get-Delegation
            Assert-Scope ($result.Scopes.Count -eq 0 -and $result.Refusal -eq 'HEAD or the working tree changed while delegation was checked') "Mutation during API lookup was delegated: $($result.Refusal)"
            Remove-Item -LiteralPath (Join-Path $delegate 'during-api.cpp')
            $global:ScopedFixtureGhMutation='commit'
            Assert-Scope ((Get-Delegation).Scopes.Count -eq 0) 'Concurrent committed candidate change was delegated'
        } finally {
            if($previousGh){Set-Item Function:\global:gh -Value $previousGh.ScriptBlock}else{Remove-Item Function:\global:gh}
            Remove-Variable -Name ScopedFixtureGhCalls,ScopedFixtureGhMutation,ScopedFixtureGhRules,ScopedFixtureGhExit,ScopedFixtureGhRoot -Scope Global
        }
    }

    Run-Case 'PR coverage of a profile is every scope its unconditional checks run, without an API dependency' {
        $profile=@($manifest.prCoverage | Where-Object {$_.PSObject.Properties['platform']})[0]
        $native=@(Get-ScopedPrCandidateScopes $manifest $profile.platform $profile.configuration)
        Assert-Scope ($native.Count -eq $manifest.scopes.Count) 'Unconditional RedXe PR gate was incorrectly narrowed'
        $debug=@(Get-ScopedPrCandidateScopes $manifest x64 Debug)
        Assert-Scope ($debug.Count -eq 1 -and $debug[0] -eq 'BuildProcess') 'The tooling job was tied to the native PR profile'
    }
    Run-Case 'reviewed CI preserves full native coverage and separates independent tooling' {
        $workflow=[IO.File]::ReadAllText((Join-Path $repository '.github/workflows/ci.yml')) -replace "`r`n","`n"
        Assert-Scope ((Get-ScopedDigest $workflow) -ceq $manifest.prWorkflowDigest) 'CI contract digest is stale'
        Assert-Scope ($workflow -match 'test.ps1 -Full -SkipTooling') 'CI accidentally uses affected iteration or repeats independent tooling'
        # test.ps1 takes its suite names from the manifest and runs each in exactly one block: a scope without a block would
        # pass without running anything, and a block outside the manifest would never be selected.
        $blocks=@(Get-TestSuiteBlocks (Join-Path $repository 'test.ps1') | ForEach-Object Name)
        foreach($scope in $manifest.scopes){Assert-Scope (@($blocks | Where-Object {$_ -ceq $scope.name}).Count -eq 1) "test.ps1 has no single block for scope $($scope.name)"}
        foreach($block in $blocks){Assert-Scope ($block -cin $manifest.scopes.name) "test.ps1 runs a suite the manifest does not name: $block"}
        $errors=$null;$tokens=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repository 'test.ps1'),[ref]$tokens,[ref]$errors)
        Assert-Scope ($null -eq ($ast.ParamBlock.Parameters | Where-Object {$_.Name.VariablePath.UserPath -eq 'Suites'}).DefaultValue) 'test.ps1 keeps its own copy of the suite list'
        foreach($profile in $manifest.prCoverage){foreach($scope in $profile.scopes){Assert-Scope ($scope -in $manifest.scopes.name) "PR profile declares nonexistent scope $scope"}}
        $native=@($manifest.prCoverage | Where-Object {$_.PSObject.Properties['platform']})
        Assert-Scope ($native.Count -eq 1 -and $native[0].configuration -eq 'Release' -and $native[0].platform -eq 'x64' -and $native[0].check -ceq 'native (x64, Release)') 'RedXe PR coverage overclaimed'
        Assert-Scope (-not (Compare-Object @($native[0].scopes) @($manifest.scopes | Where-Object {$_.native} | ForEach-Object name))) 'The x64 Release PR check does not cover exactly the native scopes'
        $tooling=@($manifest.prCoverage | Where-Object {-not $_.PSObject.Properties['platform']})
        Assert-Scope ($tooling.Count -eq 1 -and $tooling[0].check -ceq 'tooling' -and -not (Compare-Object @($tooling[0].scopes) @('BuildProcess'))) 'The tooling PR check does not cover exactly the tooling scope'
        # A check's name is its job's: `native (x64, Release)` is the native job's matrix leg.
        foreach($job in $manifest.prCoverage){Assert-Scope ($workflow -match ('(?m)^  '+[regex]::Escape(($job.check -split ' ')[0])+':')) "PR check $($job.check) is not a job of ci.yml"}
        Assert-Scope ($workflow.Contains('./Tests/BuildProcessTests/Invoke-ToolingTests.ps1')) 'Independent tooling job missing'
        # The release workflow runs test.ps1 too: no workflow may run the tooling suite without its Python packages.
        $calls=@(Get-WorkflowTestCalls (Join-Path $repository '.github/workflows'))
        foreach($name in @('ci.yml','release.yml')) {Assert-Scope (@($calls | Where-Object Workflow -eq $name).Count -gt 0) "No test.ps1 call found in $name"}
        foreach($call in $calls) {Assert-Scope $call.Provisioned "$($call.Workflow) job $($call.Job) runs the tooling suite without Build/requirements-validation.txt: $($call.Call)"}
        Write-Fixture (Join-Path $fixture 'workflows/fixture.yml') @'
jobs:
  bare:
    steps:
      # ./test.ps1 in a comment is not a call
      - run: ./test.ps1 -Configuration Release -Platform 'x64' -BuildNumber 7
  installs:
    steps:
      - run: |
          python -m pip install -r Build/requirements-validation.txt
          ./test.ps1 -Full
  skips:
    steps:
      - run: ./test.ps1 -Full -SkipTooling -Configuration Release
'@
        $calls=@(Get-WorkflowTestCalls (Join-Path $fixture 'workflows'))
        Assert-Scope ($calls.Count -eq 3 -and (@($calls | Where-Object {-not $_.Provisioned} | ForEach-Object Job) -join ',') -eq 'bare') 'Workflow tooling provisioning was misread'
    }
    Run-Case 'every input of a project a suite runs selects that suite, and every rule matches a tracked path' {
        # A matched rule replaces the full fallback, so it must name every suite that builds its paths. Each suite's
        # executables lead to their projects' items, project references and quoted #include closure.
        $module=Get-Module ScopedTesting
        $listed=@(& $module {param($Root) Get-ScopedTrackedPaths $Root} $repository)
        $tracked=@{};foreach($path in $listed){$tracked[[IO.Path]::GetFullPath((Join-Path $repository $path))]=$path}
        foreach($rule in $manifest.rules) {
            $matched=& $module {param($Paths,$Pattern) foreach($path in $Paths){if(Test-ScopedPattern $path $Pattern){return $true}};$false} $listed $rule.pattern
            Assert-Scope $matched "Rule matches no tracked path: $($rule.pattern)"
        }
        $executables=@{}
        foreach($project in @($listed | Where-Object {$_ -match '\.vcxproj$'})) {
            $full=[IO.Path]::GetFullPath((Join-Path $repository $project))
            $target=@(([xml][IO.File]::ReadAllText($full)).SelectNodes("//*[local-name()='TargetName']") | ForEach-Object InnerText | Select-Object -Unique)
            $executables[$(if($target.Count -eq 1 -and $target[0] -notmatch '\$\('){$target[0]}else{[IO.Path]::GetFileNameWithoutExtension($full)})+'.exe']=$full
        }
        function Add-InputClosure([string]$Path,[string[]]$Directories,[hashtable]$Seen) {
            if($Seen.ContainsKey($Path)){return};$Seen[$Path]=$true
            if($Path -notmatch '\.(c|cpp|h|hpp|inl|hlsl|hlsli)$'){return}
            foreach($match in [regex]::Matches([IO.File]::ReadAllText($Path),'(?m)^[ \t]*#[ \t]*include[ \t]*"([^"]+)"')) {
                foreach($directory in @(Split-Path $Path)+$Directories) {
                    $candidate=[IO.Path]::GetFullPath([IO.Path]::Combine($directory,$match.Groups[1].Value))
                    if($tracked.ContainsKey($candidate)){Add-InputClosure $candidate $Directories $Seen;break}
                }
            }
        }
        function Add-ProjectClosure([string]$Project,[hashtable]$Seen) {
            if($Seen.ContainsKey($Project) -or -not $tracked.ContainsKey($Project)){return};$Seen[$Project]=$true
            $directory=Split-Path $Project;$xml=[xml][IO.File]::ReadAllText($Project)
            # A quoted include resolves beside its file, then through the project's directories and Common (Directory.Build.props).
            $directories=@($xml.SelectNodes("//*[local-name()='AdditionalIncludeDirectories']") | ForEach-Object {$_.InnerText -split ';'} |
                Where-Object {$_ -and $_ -notmatch '[$%]\('} | ForEach-Object {[IO.Path]::GetFullPath([IO.Path]::Combine($directory,$_))})+@(Join-Path $repository 'Common')
            foreach($item in $xml.SelectNodes("//*[local-name()='ItemGroup']/*[@Include]")) {
                $include=$item.GetAttribute('Include')
                if($include -match '[|*?;]|[$%@]\('){continue}
                $path=[IO.Path]::GetFullPath([IO.Path]::Combine($directory,$include))
                if($item.LocalName -eq 'ProjectReference'){Add-ProjectClosure $path $Seen}elseif($tracked.ContainsKey($path)){Add-InputClosure $path $directories $Seen}
            }
        }
        $failures=[Collections.Generic.List[string]]::new();$plans=@{};$roots=@{}
        foreach($block in @(Get-TestSuiteBlocks (Join-Path $repository 'test.ps1'))) {
            $strings=$block.Body.FindAll({param($node) $node -is [Management.Automation.Language.StringConstantExpressionAst] -or $node -is [Management.Automation.Language.ExpandableStringExpressionAst]},$true)
            foreach($name in @($strings | ForEach-Object {[regex]::Matches($_.Value,'\w+\.exe\b')} | ForEach-Object Value | Sort-Object -Unique | Where-Object {$executables.ContainsKey($_)})) {
                $roots[$executables[$name]]=$true;$seen=@{};Add-ProjectClosure $executables[$name] $seen
                foreach($path in @($seen.Keys | ForEach-Object {$tracked[$_]} | Where-Object {$_ -notmatch '\.md$'})) {
                    if(-not $plans.ContainsKey($path)){$plans[$path]=Get-ScopedTestPlan $manifest @($path)}
                    if(-not $plans[$path].full -and $block.Name -notin $plans[$path].scopes){$failures.Add("$path (built into $name) does not select $($block.Name)")}
                }
            }
        }
        foreach($project in @($listed | Where-Object {$_ -match '^Tests/[^/]+/[^/]+\.vcxproj$'})){Assert-Scope $roots.ContainsKey([IO.Path]::GetFullPath((Join-Path $repository $project))) "No suite runs the executable of $project"}
        Assert-Scope ($failures.Count -eq 0) "Scope rules omit suites that build their paths:`n$($failures -join "`n")"
    }
    Run-Case 'public runner executes once, reuses success across commits and never records failed or mutated runs' {
        $sandbox=Join-Path $fixture 'runner'
        Write-Fixture (Join-Path $sandbox '.gitignore') '.build/'
        Copy-Item -LiteralPath (Join-Path $repository 'Test-Changes.ps1') -Destination (Join-Path $sandbox 'Test-Changes.ps1')
        Write-Fixture (Join-Path $sandbox 'Build/ScopedTesting.psm1') ([IO.File]::ReadAllText((Join-Path $repository 'Build/ScopedTesting.psm1')))
        Write-Fixture (Join-Path $sandbox 'Tests/Example.Tests.Case.cpp') 'int example;'
        Write-Fixture (Join-Path $sandbox 'Specs/TestRuns/history/SelfTest/Old.SelfTest.cpp') 'immutable historical source;'
        Write-Fixture (Join-Path $sandbox 'README.md') 'fixture guide'
        Write-Fixture (Join-Path $sandbox 'Tests/native-test-files.json') '["Tests/Example.Tests.Case.cpp"]'
        $fixturePlatform=if([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -eq 'Arm64') {'ARM64'} else {'x64'}
        $workflow="name: fixture`non:`n  pull_request:`n"
        Write-Fixture (Join-Path $sandbox '.github/workflows/ci.yml') $workflow
        Write-Fixture (Join-Path $sandbox 'Tests/test-scopes.json') ('{"version":1,"defaultBranch":"main","repository":"fixture/example","scopes":[{"name":"Example","native":true,"reuse":true},{"name":"Docs","native":false,"reuse":true}],"rules":[],"toolingCommands":["tooling.ps1"],"prCoverage":[{"check":"tooling","scopes":["Docs"]},{"check":"native","platform":"'+$fixturePlatform+'","configuration":"Release","scopes":["Example"]}],"prWorkflowDigest":"'+(Get-ScopedDigest $workflow)+'"}')
        # Like the real entrypoints, the fixture build stamps -BuildNumber (by default the commit count of HEAD) into its
        # binary, and the fixture test requires the stamp of its own -BuildNumber.
        Write-Fixture (Join-Path $sandbox 'build.ps1') @'
param($Platform,$Configuration,[int]$BuildNumber)
if(-not $BuildNumber){$BuildNumber=[int](git -C $PSScriptRoot rev-list --count HEAD)}
$output=Join-Path $PSScriptRoot ".build/$Platform/$Configuration"
New-Item -ItemType Directory -Path $output -Force|Out-Null
[IO.File]::WriteAllText((Join-Path $output 'example.exe'),"fixture binary $BuildNumber")
foreach($relative in @('Settings/RedXe-debug.settings.json','Settings/RedXe.settings.json','Settings/RedXe.settings.schema.json','DxUi.provenance.json')) {
    $path=Join-Path $output $relative
    [void](New-Item -ItemType Directory -Path (Split-Path $path) -Force)
    [IO.File]::WriteAllText($path,'fixture deployed input')
}
exit 0
'@
        Write-Fixture (Join-Path $sandbox 'test.ps1') @'
param($Platform,$Configuration,[switch]$SkipBuild,[string[]]$Suites,[switch]$SkipTooling,[int]$BuildNumber)
if(-not $BuildNumber){$BuildNumber=[int](git -C $PSScriptRoot rev-list --count HEAD)}
[IO.File]::AppendAllText((Join-Path $PSScriptRoot '.build/calls.txt'),"execute`n")
if([IO.File]::ReadAllText((Join-Path $PSScriptRoot ".build/$Platform/$Configuration/example.exe")) -cne "fixture binary $BuildNumber"){Write-Host "Version identity mismatch: expected $BuildNumber";exit 5}
if(Test-Path (Join-Path $PSScriptRoot '.build/fail')){exit 1}
if(Test-Path (Join-Path $PSScriptRoot '.build/mutate')){[IO.File]::AppendAllText((Join-Path $PSScriptRoot 'Tests/Example.Tests.Case.cpp'),'changed')}
exit 0
'@
        Write-Fixture (Join-Path $sandbox 'tooling.ps1') @'
[IO.File]::AppendAllText((Join-Path $PSScriptRoot '.build/calls.txt'),"tooling`n")
if(Test-Path (Join-Path $PSScriptRoot '.build/mutate-prose')){[IO.File]::AppendAllText((Join-Path $PSScriptRoot 'README.md'),' changed')}
exit 0
'@
        Initialize-FixtureRepository $sandbox
        function Invoke-SandboxGit([string[]]$Arguments) {$output=& git -C $sandbox @Arguments 2>&1;if($LASTEXITCODE){throw "Runner fixture git failed: $Arguments`n$($output -join "`n")"}}
        Invoke-SandboxGit @('config','user.name','Runner fixture')
        Invoke-SandboxGit @('config','user.email','fixture@example.invalid')
        Invoke-SandboxGit @('add','-A');Invoke-SandboxGit @('commit','-q','-m','baseline')
        Invoke-SandboxGit @('update-ref','refs/remotes/origin/main','HEAD')
        function Invoke-PublicRunner([string[]]$Arguments,[string]$Scopes='Example',[hashtable]$Environment=@{}) {
            $start=[Diagnostics.ProcessStartInfo]::new((Join-Path $PSHOME 'pwsh.exe'))
            if (-not(Test-Path $start.FileName)) {$start.FileName=Join-Path $PSHOME 'pwsh'}
            $start.UseShellExecute=$false;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
            foreach($name in $Environment.Keys){$start.Environment[$name]=$Environment[$name]}
            foreach($argument in @('-NoProfile','-File',(Join-Path $sandbox 'Test-Changes.ps1'))+@(if($Scopes){'-Scopes',$Scopes})+@('-Platform',$fixturePlatform)+$Arguments){$start.ArgumentList.Add($argument)}
            $process=[Diagnostics.Process]::Start($start)
            try {$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$process.WaitForExit();return [pscustomobject]@{Exit=$process.ExitCode;Text=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()}}
            finally {$process.Dispose()}
        }
        function Get-RuntimeCalls {@(Get-Content (Join-Path $sandbox '.build/calls.txt') | Where-Object {$_ -eq 'execute'}).Count}
        $run=Invoke-PublicRunner @();Assert-Scope ($run.Exit -eq 0 -and $run.Text.Contains('Build number: 1')) "Initial execution failed: $($run.Text)"
        $run=Invoke-PublicRunner @('-SkipBuild');Assert-Scope ($run.Exit -eq 0 -and $run.Text.Contains('REUSED Example')) "Identical success was executed again: $($run.Text)"
        Assert-Scope ((Get-RuntimeCalls) -eq 1) 'Cached call executed runtime'
        # A commit leaves the merge base, so the version stamp, the binaries and their evidence, unchanged.
        Invoke-SandboxGit @('commit','-q','--allow-empty','-m','candidate')
        $run=Invoke-PublicRunner @();Assert-Scope ($run.Exit -eq 0 -and $run.Text.Contains('REUSED Example')) "A commit invalidated identical evidence: $($run.Text)"
        $run=Invoke-PublicRunner @('-Force','-SkipBuild');Assert-Scope ($run.Exit -eq 0 -and (Get-RuntimeCalls) -eq 2) "SkipBuild after a commit did not test the attested stamp: $($run.Text)"
        # A newer base changes the stamp: SkipBuild refuses before any test runs, and a build restamps.
        Invoke-SandboxGit @('update-ref','refs/remotes/origin/main','HEAD')
        $run=Invoke-PublicRunner @('-SkipBuild');Assert-Scope ($run.Exit -ne 0 -and $run.Text.Contains('SkipBuild refused') -and (Get-RuntimeCalls) -eq 2) "A changed build number passed the attestation: $($run.Text)"
        $run=Invoke-PublicRunner @();Assert-Scope ($run.Exit -eq 0 -and $run.Text.Contains('Build number: 2') -and (Get-RuntimeCalls) -eq 3) "A changed build number did not restamp and rerun: $($run.Text)"
        # Every exit ends with a coverage label: an empty plan evaluated nothing, and delegated work is pending.
        $run=Invoke-PublicRunner @() ''
        Assert-Scope ($run.Exit -eq 0 -and $run.Text.Contains('NOTHING_SELECTED; repository NOT_EVALUATED (no test input changed since the merge base with origin/main')) "An empty plan did not say it evaluated nothing: $($run.Text)"
        # gh answers from outside the candidate's tree, which must stay clean to be delegated.
        $gh=Join-Path $fixture 'gh-bin'
        Write-Fixture (Join-Path $gh 'gh.ps1') "if((`$args -join ' ') -like '*/rules/branches/main*'){`$env:SCOPED_FIXTURE_RULES}else{'{`"state`":`"active`"}'}`nexit 0`n"
        $environment=@{PATH=$gh+[IO.Path]::PathSeparator+$env:PATH;SCOPED_FIXTURE_RULES='[{"type":"required_status_checks","parameters":{"required_status_checks":[{"context":"tooling"},{"context":"native"}]}}]'}
        $calls=Get-RuntimeCalls
        $run=Invoke-PublicRunner @('-Mode','PrePush','-Configuration','Release') '' $environment
        Assert-Scope ($run.Exit -eq 0 -and $run.Text.Contains('DEFERRED_CI (forthcoming PR, pending success): Docs, Example') -and $run.Text.Contains('LOCAL_OBLIGATIONS_NONE; CI_PENDING') -and (Get-RuntimeCalls) -eq $calls) "A fully delegated PrePush did not end with CI_PENDING: $($run.Text)"
        $run=Invoke-PublicRunner @('-Mode','PrePush') '' $environment
        Assert-Scope ($run.Exit -eq 0 -and $run.Text.Contains('DEFERRED_CI (forthcoming PR, pending success): Docs') -and $run.Text.Contains('LOCAL_OBLIGATIONS_PASSED; CI_PENDING')) "The tooling check was not delegated for a profile no native check runs: $($run.Text)"
        $environment.SCOPED_FIXTURE_RULES='[]'
        $run=Invoke-PublicRunner @('-Mode','PrePush','-Configuration','Release','-Explain') '' $environment
        Assert-Scope ($run.Exit -eq 0 -and -not $run.Text.Contains('DEFERRED_CI') -and $run.Text.Contains("PR delegation not used: main does not require the PR check 'tooling', 'native', so a failing run could still merge")) "A refused delegation gave no reason: $($run.Text)"
        $receipt=Join-Path $sandbox ".build/reports/scoped-tests/$fixturePlatform-Debug/Example.json"
        Write-Fixture (Join-Path $sandbox '.build/fail') 'fail'
        $run=Invoke-PublicRunner @('-Force','-SkipBuild');Assert-Scope ($run.Exit -ne 0 -and -not(Test-Path $receipt)) 'Failure retained earlier reusable success'
        Remove-Item -LiteralPath (Join-Path $sandbox '.build/fail')
        $run=Invoke-PublicRunner @('-SkipBuild');Assert-Scope ($run.Exit -eq 0 -and -not $run.Text.Contains('REUSED Example')) 'Failed run was reused'
        # A comma list under pwsh -File selects both scopes. Prose edited while they run changes only the tooling scope's
        # inputs: the run passes unrecorded for Docs and keeps the native receipt.
        $docs=Join-Path $sandbox '.build/reports/scoped-tests/independent/Docs.json'
        Write-Fixture (Join-Path $sandbox '.build/mutate-prose') 'mutate'
        $run=Invoke-PublicRunner @('-Force','-SkipBuild') 'Example,Docs'
        Assert-Scope ($run.Exit -eq 0 -and $run.Text.Contains('SELECTED_PASSED; NOT_RECORDED') -and (Test-Path $receipt) -and -not(Test-Path $docs)) "Prose edited during the run failed it or recorded the changed scope: $($run.Text)"
        Remove-Item -LiteralPath (Join-Path $sandbox '.build/mutate-prose')
        Write-Fixture (Join-Path $sandbox '.build/mutate') 'mutate'
        $run=Invoke-PublicRunner @('-Force','-SkipBuild');Assert-Scope ($run.Exit -eq 0 -and $run.Text.Contains('SELECTED_PASSED; NOT_RECORDED') -and -not(Test-Path $receipt)) "Source edited during the run failed it or published success: $($run.Text)"
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
