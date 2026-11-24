# RedXe's product scope routing and report adapter. Common runner behavior is tested by the pinned engine.
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$engine = Join-Path $repository '.ci-engine'
$engineScopedModule = Join-Path $engine 'Tools/Modules/Ci.ScopedTesting.psm1'
Import-Module (Join-Path $repository 'Build/ScopedTesting.psm1') -Scope Local
$manifest = Read-ScopedTestManifest -Root $repository
$passed = 0

function Assert-Scope([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Run-Case([string]$Name, [scriptblock]$Action) {
    & $Action
    $script:passed++
    Write-Host "PASS $Name"
}
function Write-Fixture([string]$Path, [string]$Content) {
    [void][IO.Directory]::CreateDirectory((Split-Path $Path -Parent))
    [IO.File]::WriteAllText($Path, $Content, [Text.UTF8Encoding]::new($false))
}
function Get-TestSuiteBlocks([string]$Path) {
    $errors = $null
    $tokens = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$errors)
    if (@($errors).Count) { throw "Native entry point has parse errors: $Path" }
    foreach ($call in $ast.FindAll({
        param($node)
        $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Invoke-RedXeNativeSuite'
    }, $true)) {
        $arguments = $call.CommandElements
        if ($arguments.Count -ne 3 -or $arguments[1] -isnot [Management.Automation.Language.StringConstantExpressionAst] -or
            $arguments[2] -isnot [Management.Automation.Language.ScriptBlockExpressionAst]) {
            throw "Suite name and body must be declared in test.ps1: $($call.Extent.Text)"
        }
        [pscustomobject]@{ Name = $arguments[1].Value; Body = $arguments[2].ScriptBlock }
    }
}

$fixture = Join-Path $repository ('.build/ToolTests/ScopedTesting-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixture)
try {
    Run-Case 'active native inventory and profile policy match RedXe' {
        Assert-Scope ((Assert-ScopedTestNames -Root $repository -Manifest $manifest) -gt 0) 'Empty native inventory.'
        Assert-Scope ($manifest.buildNumber -ceq 'mergeBaseCount') 'Scoped builds lost the stable merge-base stamp.'
        $profiles = @($manifest.prePushProfiles | ForEach-Object { "$($_.platform)/$($_.configuration)/$($_.buildOnly)" })
        Assert-Scope (($profiles -join ',') -ceq 'x64/Debug/False,x64/Release/False,x64/ASan Debug/False,ARM64/Debug/True,ARM64/Release/True,ARM64/ASan Debug/True') 'PrePush lost a runtime or cross-build profile.'
        $format = $manifest.scopes | Where-Object name -CEQ 'Format'
        Assert-Scope (-not $format.native -and $format.commands[0].path -ceq 'format.ps1' -and '-Check' -cin $format.commands[0].arguments) 'Formatting is not a read-only independent scope.'
    }

    Run-Case 'unmapped executable input widens to every scope' {$plan=Get-ScopedTestPlan $manifest @('Unknown/new.cpp');Assert-Scope $plan.full 'Fallback was narrowed'}

    Run-Case 'isolated skill package checks select only tooling without narrowing shared build policy' {
        foreach($path in @('validate-skills.ps1','Tools/Test-AgentGuidance.ps1')) {
            $plan=Get-ScopedTestPlan $manifest @($path)
            Assert-Scope (($plan.scopes -join ',') -ceq 'BuildProcess') "Independent package input widened coverage: $path"
            Assert-Scope (@($manifest.rules | Where-Object {$_.pattern -ceq $path -and $_.PSObject.Properties['exclusive'] -and $_.exclusive}).Count -eq 1) "The exception for $path is not one exclusive manifest rule"
        }
        # The exception is the rule's exact path, not its directory: a sibling still gets the broader build/process rule.
        $sibling=Get-ScopedTestPlan $manifest @('Tests/BuildProcessTests/UnlistedFixture.Tests.ps1')
        Assert-Scope ('Packaging' -in $sibling.scopes -and 'BuildProcess' -in $sibling.scopes -and -not $sibling.full) "A sibling of the isolated fixtures lost the broader rule: $($sibling.scopes -join ',')"
        foreach($path in @('Build/ScopedTesting.psm1','Build/UnknownNewBuildPolicy.psm1')) {
            Assert-Scope ((Get-ScopedTestPlan $manifest @($path)).full) "Shared/unknown Build policy lost conservative coverage: $path"
        }
    }

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
        foreach($inputPath in @('Plugins/AVControl/AVControlModel.cpp','Plugins/Launcher/LauncherPaging.h','Plugins/Logicon/LogiconSettings.cpp','Plugins/Actions/Zoom/ZoomSettings.cpp','Plugins/StudioClock/StudioClockSettings.h')) {
            Assert-Scope ('Settings' -in (Get-ScopedTestPlan $manifest @($inputPath)).scopes) "Shared settings consumer omitted: $inputPath"
        }
    }

    Run-Case 'test project files reach the shared-output consumers, and skill tooling selects only the tooling scope' {
        $project=Get-ScopedTestPlan $manifest @('Tests/SettingsTests/SettingsTests.vcxproj')
        foreach($consumer in @('Settings','PluginContract','HostPlugin','HostSmoke','Packaging')) {Assert-Scope ($consumer -in $project.scopes) "A test project edit omitted $consumer"}
        Assert-Scope (-not $project.full) 'A test project edit fell back to every scope'
        Assert-Scope (((Get-ScopedTestPlan $manifest @('Tests/SettingsTests/SettingsTests.vcxproj.filters')).scopes -join ',') -eq 'Settings') 'An IDE-only filters edit left its own suite'
        foreach($path in @('.agents/skills/yyjson/agents/openai.yaml','validate-skills.ps1','Tools/Test-AgentGuidance.ps1')) {
            $plan=Get-ScopedTestPlan $manifest @($path)
            Assert-Scope (($plan.scopes -join ',') -eq 'BuildProcess') "Skill tooling input $path selected $($plan.scopes -join ', ')"
        }
    }

    Run-Case 'unknown and fixture Markdown retain native ownership' {
        foreach ($path in 'Tests/Fixtures/README.md', 'Common/res/README.md', 'Unknown/input.md', 'Plugins/5H4D3R5/LICENSES.md') {
            $plan = Get-ScopedTestPlan -Manifest $manifest -ChangedPaths @($path)
            Assert-Scope (@($manifest.scopes | Where-Object { $_.native -and $_.name -in $plan.scopes }).Count -gt 0) "Runtime input lost native coverage: $path"
        }
    }

    Run-Case 'deployed settings and provenance are required and affect the artifact identity' {
        $expectedFiles = @('Settings/RedXe-debug.settings.json', 'Settings/RedXe.settings.json',
            'Settings/SettingsStore.schema.json', 'DxUi.provenance.json')
        Assert-Scope ((@($manifest.runtimeFiles) -join ',') -ceq ($expectedFiles -join ',')) 'The deployed runtime inventory changed.'
        $profile = Join-Path $fixture '.build/x64/Debug'
        Write-Fixture (Join-Path $profile 'suite.exe') 'fixture bytes'
        foreach ($relative in $expectedFiles) { Write-Fixture (Join-Path $profile $relative) 'original deployed input' }
        $initial = Get-ScopedArtifactIdentity -Root $fixture -Platform x64 -Configuration Debug -Manifest $manifest
        foreach ($relative in $expectedFiles) {
            $path = Join-Path $profile $relative
            Write-Fixture $path 'changed deployed input'
            $changed = Get-ScopedArtifactIdentity -Root $fixture -Platform x64 -Configuration Debug -Manifest $manifest
            Assert-Scope ($initial -cne $changed) "Mutation failed to invalidate the runtime closure: $relative"
            [IO.File]::Delete($path)
            $message = ''
            try { [void](Get-ScopedArtifactIdentity -Root $fixture -Platform x64 -Configuration Debug -Manifest $manifest) }
            catch { $message = $_.Exception.Message }
            Assert-Scope ($message -ceq "Missing deployed runtime input: $relative") "Missing deployed input was accepted: $relative."
            Write-Fixture $path 'original deployed input'
        }
        Write-Fixture (Join-Path $profile 'logs/generated.json') 'test-owned report'
        Assert-Scope ((Get-ScopedArtifactIdentity -Root $fixture -Platform x64 -Configuration Debug -Manifest $manifest) -ceq $initial) 'Generated output changed the runtime closure.'
    }

    Run-Case 'native adapter records success, capability skips and failure with isolated local SDK evidence' {
        $file = Join-Path $repository 'test.ps1'
        $errors = $null
        $tokens = $null
        $ast = [Management.Automation.Language.Parser]::ParseFile($file, [ref]$tokens, [ref]$errors)
        Assert-Scope (-not @($errors).Count) 'Native test syntax is invalid.'
        $wrapper = $ast.Find({
            param($node)
            $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Invoke-RedXeNativeSuite'
        }, $true)
        Assert-Scope ($null -ne $wrapper) 'Native test report wrapper is missing.'
        . ([scriptblock]::Create($wrapper.Extent.Text))
        $received = [Collections.Generic.List[object]]::new()
        function Publish-TestRunEvidenceReceipt($Run, [string]$Name, $Receipt) {
            $received.Add([pscustomobject]@{ Name = $Name; Receipt = $Receipt })
        }
        $Platform = 'x64'
        $Configuration = 'Debug'
        $Suites = @('HostSmoke')
        $localSdkMode = $false
        $reportSuffix = ''
        $evidence = @{}
        Invoke-RedXeNativeSuite 'Settings' { throw 'Unselected suite ran.' }
        Invoke-RedXeNativeSuite 'HostSmoke' { param($Skips) $Skips.Add('viewer is present') }
        Assert-Scope ($received.Count -eq 1 -and $received[0].Name -ceq 'HostSmoke-x64-Debug.json' -and
            $received[0].Receipt.exitCode -eq 0 -and $received[0].Receipt.skips.Count -eq 1 -and $received[0].Receipt.skips[0] -ceq 'viewer is present') 'The capability skip was lost.'
        $reportSuffix = '.instrumented'
        $failure = ''
        try { Invoke-RedXeNativeSuite 'HostSmoke' { throw 'Deliberate suite failure' } }
        catch { $failure = $_.Exception.Message }
        Assert-Scope ($failure -ceq 'Deliberate suite failure' -and $received.Count -eq 2 -and
            $received[1].Name -ceq 'HostSmoke-x64-Debug.instrumented.json' -and $received[1].Receipt.exitCode -ne 0 -and
            $received[1].Receipt.skips.Count -eq 0) 'A failed suite lost its diagnostic or wrote a successful report.'
        $localSdkMode = $true
        Invoke-RedXeNativeSuite 'HostSmoke' {}
        Assert-Scope ($received.Count -eq 2) 'A local SDK probe published an ordinary reusable suite report.'
    }

    Run-Case 'reviewed CI retains normal native execution in its main adapter' {
        $engine=Join-Path $repository '.ci-engine'
        Import-Module (Join-Path $engine 'Tools/Modules/Ci.Project.psm1')
        Import-Module (Join-Path $engine 'Tools/Modules/Ci.Policy.psm1')
        $project=Read-CiProject $repository
        Assert-Scope ('**/*.md' -notin $project.exclude) 'Hosted CI again excludes unknown runtime Markdown by extension'
        foreach($path in @('Tests/Fixtures/README.md','Tests/Data/case.md','Common/res/README.md','Unknown/input.md','Plugins/5H4D3R5/LICENSES.md')) {
            Assert-Scope (Get-CiPathDisposition $path $project).native "Hosted identity excludes runtime/unknown Markdown: $path"
        }
        foreach($path in @('README.md','Specs/Testing/explanation.md','Plugins/Weather/README.md','Changes/2026-11-09-example.md')) {
            Assert-Scope (-not (Get-CiPathDisposition $path $project).native) "Hosted prose unexpectedly requires native work: $path"
        }
        Test-CiProjectConfiguration @{root=$repository;engineRoot=$engine;project=$project;development=$false}
        $adapter=[IO.File]::ReadAllText((Join-Path $repository 'Tools/Invoke-ProjectCi.ps1'))
        Assert-Scope ($adapter.Contains("Run 'test.ps1'") -and $adapter.Contains("'-Full','-SkipTooling','-SkipBuild'")) 'The normal batch lost Full native execution'
        Assert-Scope ($adapter.Contains('Invoke-ToolingTests.ps1')) 'Compiler-dependent tooling lost its main owner'
        Assert-Scope ($adapter.Contains('Get-CiSuiteEvidence')) 'Native suite execution must produce canonical fresh evidence'
        $blocks=@(Get-TestSuiteBlocks (Join-Path $repository 'test.ps1') | ForEach-Object Name)
        foreach($scope in $manifest.scopes | Where-Object native){Assert-Scope (@($blocks|Where-Object {$_ -ceq $scope.name}).Count -eq 1) "The normal native runner lost $($scope.name)"}
        foreach($block in $blocks){Assert-Scope ($block -cin $manifest.scopes.name) "Unregistered native block $block"}
    }

    Run-Case 'every input of a project a suite runs selects that suite, and every rule matches a tracked path' {
        # A matched rule replaces the full fallback, so it must name every suite that builds its paths. Each suite's
        # executables lead to their projects' items, project references and quoted #include closure.
        $module=Import-Module $engineScopedModule -Scope Local -PassThru
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

    Write-Host "RedXe scoped testing: $passed cases passed."
} finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    $allowed = [IO.Path]::GetFullPath((Join-Path $repository '.build/ToolTests')) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture cleanup escaped its owned root.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
exit 0
