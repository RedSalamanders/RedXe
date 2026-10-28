# Behavioral fixtures run in memory; aggregate fixtures use this repository's .build.
#Requires -Version 7.0
[CmdletBinding()]param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$skillModule = Import-Module (Join-Path $repository 'Build/SkillValidation.psm1') -Force -PassThru
$prefix = "---`nname: example-skill`ndescription: valid`n---`n"
$cases = @(
    @{Name='plain'; Text=$prefix+'Read the contract.'; Valid=$true},
    @{Name='CRLF'; Text=($prefix+'Read.').Replace("`n","`r`n"); Valid=$true},
    @{Name='literal Unicode'; Text="---`nname: example-skill`ndescription: |`n  Validate français`n  and 日本語.`n---`nRead."; Valid=$true},
    @{Name='folded description'; Text="---`nname: example-skill`ndescription: >-`n  Validate français`n  and 日本語.`n---`nRead."; Valid=$true},
    @{Name='optional metadata'; Text="---`nname: example-skill`ndescription: valid`nlicense: MIT`nallowed-tools: Read Write`nmetadata:`n  version: '1.0'`n  author: Example`n---`nRead."; Valid=$true},
    @{Name='single quoted apostrophe'; Text="---`nname: example-skill`ndescription: 'It''s valid: yes' # comment`n---`nRead."; Valid=$true},
    @{Name='JSON quoted escape'; Text='---'+"`nname: example-skill`ndescription: "+'"Line\nDeux \u65e5"'+"`n---`nRead."; Valid=$true},
    @{Name='plain comment'; Text="---`n# comment`nname: example-skill`ndescription: valid # comment`n---`nRead."; Valid=$true},
    @{Name='quoted typed string'; Text="---`nname: example-skill`ndescription: '123'`n---`nRead."; Valid=$true},
    @{Name='missing file'; Text=$null; Valid=$false},
    @{Name='missing delimiters'; Text='name: example-skill'+"`ndescription: valid`nRead."; Valid=$false},
    @{Name='malformed YAML'; Text="---`n[`n---`nRead."; Valid=$false},
    @{Name='sequence mapping'; Text="---`n- list`n---`nRead."; Valid=$false},
    @{Name='wrong name'; Text=$prefix.Replace('example-skill','wrong-name')+'Read.'; Valid=$false},
    @{Name='wrong name case'; Text=$prefix.Replace('example-skill','Example-Skill')+'Read.'; Valid=$false},
    @{Name='duplicate name'; Text=$prefix.Replace('description: valid',"name: wrong`ndescription: valid")+'Read.'; Valid=$false},
    @{Name='unsupported key'; Text=$prefix.Replace('description: valid',"description: valid`nunexpected: value")+'Read.'; Valid=$false},
    @{Name='empty description'; Text=$prefix.Replace('description: valid',"description: ''")+'Read.'; Valid=$false},
    @{Name='angle brackets'; Text=$prefix.Replace('description: valid','description: <unsafe>')+'Read.'; Valid=$false},
    @{Name='description too long'; Text=$prefix.Replace('description: valid',('description: '+('x'*1025)))+'Read.'; Valid=$false},
    @{Name='description at limit'; Text=$prefix.Replace('description: valid',('description: '+('x'*1024)))+'Read.'; Valid=$true},
    @{Name='empty instructions'; Text=$prefix; Valid=$false},
    @{Name='unfinished description'; Text=$prefix.Replace('description: valid',"description: '[TODO: finish]'")+'Read.'; Valid=$false},
    @{Name='unfinished body'; Text=$prefix+"[TODO: finish]`n"; Valid=$false},
    @{Name='TODO example'; Text=$prefix+'```text'+"`n[TODO: example]`n"+'```'; Valid=$true},
    @{Name='TODO after example'; Text=$prefix+'```text'+"`nexample`n"+'```'+"`n[TODO: finish]"; Valid=$false},
    @{Name='tilde list fence'; Text=$prefix+"- ~~~~text`n[TODO: example]`n  ~~~~"; Valid=$true},
    @{Name='short fence stays open'; Text=$prefix+'````text'+"`n"+'```'+"`n[TODO: example]`n"+'````'; Valid=$true},
    @{Name='nested mapping refused'; Text=$prefix.Replace('description: valid',"description: valid`nmetadata:`n  nested:`n    key: value")+'Read.'; Valid=$false},
    @{Name='flow collection refused'; Text=$prefix.Replace('description: valid','description: [one, two]')+'Read.'; Valid=$false},
    @{Name='tag refused'; Text=$prefix.Replace('description: valid','description: !!str unsafe')+'Read.'; Valid=$false},
    @{Name='anchor refused'; Text=$prefix.Replace('description: valid','description: &anchor text')+'Read.'; Valid=$false},
    @{Name='alias refused'; Text=$prefix.Replace('description: valid','description: *anchor')+'Read.'; Valid=$false},
    @{Name='malformed quote'; Text=$prefix.Replace('description: valid',"description: 'unclosed")+'Read.'; Valid=$false},
    @{Name='bad JSON escape'; Text=$prefix.Replace('description: valid','description: "bad\q"')+'Read.'; Valid=$false},
    @{Name='invalid indentation'; Text=$prefix.Replace('description: valid',"description: |`n one space")+'Read.'; Valid=$false}
)
foreach ($typed in @('123','true','yes','null','2026-10-28','1.5','0x10','.inf','off')) {
    $cases += @{Name="typed $typed"; Text=$prefix.Replace('description: valid',"description: $typed")+'Read.'; Valid=$false}
}
$unicodeScalar = [char]::ConvertFromUtf32(0x1f4d8)
$cases += @{Name='Unicode code-point limit'; Text=$prefix.Replace('description: valid',('description: '+($unicodeScalar*1024)))+'Read.'; Valid=$true}
$cases += @{Name='Unicode code-point overflow'; Text=$prefix.Replace('description: valid',('description: '+($unicodeScalar*1025)))+'Read.'; Valid=$false}
$passed = 0
foreach ($block in @(
    @{Text="description: |`n  first`n  second"; Expected="first`nsecond`n"},
    @{Text="description: |-`n  first`n  second"; Expected="first`nsecond"},
    @{Text="description: >-`n  first`n  second`n`n  third"; Expected="first second`nthird"},
    @{Text="description: >-`n  first`n`n    indented`n  last"; Expected="first`n`n  indented`nlast"},
    @{Text="description: |+`n  first`n`n"; Expected="first`n`n`n"}
)) {
    $metadata = & $skillModule {param($Text) ConvertFrom-SkillMetadata $Text} $block.Text
    if ($metadata.description -cne $block.Expected) {throw 'Multiline scalar differs from its independent expected text'}
    $passed++
}
foreach ($case in $cases) {
    $problem = Get-SkillValidationProblem -FolderName 'example-skill' -Text $case.Text
    if ((-not $problem) -ne $case.Valid) { throw "Skill fixture '$($case.Name)' expected valid=$($case.Valid): $problem" }
    $passed++
}
$fixture = Join-Path $repository ('.build/SkillValidation-' + [guid]::NewGuid().ToString('N'))
try {
    [void][IO.Directory]::CreateDirectory((Join-Path $fixture '.agents/skills/example-skill'))
    $path = Join-Path $fixture '.agents/skills/example-skill/SKILL.md'
    [IO.File]::WriteAllText($path,$prefix+'Read.',[Text.UTF8Encoding]::new($false))
    if ((Test-RepositorySkills $fixture) -ne 1) { throw 'Aggregate did not validate the fixture skill' }
    [IO.File]::WriteAllText($path,$prefix,[Text.UTF8Encoding]::new($false))
    $message='';try { Test-RepositorySkills $fixture | Out-Null } catch [System.Management.Automation.RuntimeException] {$message=$_.Exception.Message}
    if ($message -notlike '*example-skill: empty skill instructions*') { throw "Aggregate admitted invalid instructions: $message" }
    [IO.File]::Delete($path)
    $message='';try { Test-RepositorySkills $fixture | Out-Null } catch [System.Management.Automation.RuntimeException] {$message=$_.Exception.Message}
    if ($message -notlike '*example-skill: missing SKILL.md*') { throw "Aggregate admitted a missing file: $message" }
    [IO.Directory]::Delete((Split-Path $path))
    $message='';try { Test-RepositorySkills $fixture | Out-Null } catch [System.Management.Automation.RuntimeException] {$message=$_.Exception.Message}
    if ($message -notlike '*No repository skills found*') { throw "Aggregate admitted an empty skill root: $message" }
    $passed += 4
} finally {
    if ([IO.File]::Exists($path)) { [IO.File]::Delete($path) }
    foreach($relative in @('.agents/skills/example-skill','.agents/skills','.agents','')) {
        $directory=Join-Path $fixture $relative
        if ([IO.Directory]::Exists($directory)) {[IO.Directory]::Delete($directory)}
    }
}
& {
    function Get-Command {
        [CmdletBinding()]param([string]$Name, [string]$CommandType)
        if ($Name -match '^(python|py)(\.exe)?$') { throw 'Validation must not discover Python' }
        Microsoft.PowerShell.Core\Get-Command @PSBoundParameters
    }
    function python { throw 'Validation must not invoke Python' }
    function py { throw 'Validation must not invoke the Python launcher' }
    & (Join-Path $repository 'validate-skills.ps1')
}
$scopedModule = Import-Module (Join-Path $repository 'Build/ScopedTesting.psm1') -Force -PassThru
$inputs = @(& $scopedModule {
    function Get-Command {
        [CmdletBinding()]param([string]$Name, [string]$CommandType)
        if ($Name -match '^(python|py)(\.exe)?$') { throw 'Receipt identity must not discover Python' }
        Microsoft.PowerShell.Core\Get-Command @PSBoundParameters
    }
    Get-ScopedEnvironmentInputs -Tooling
})
if (@($inputs | Where-Object {"$_" -match '^python='}).Count -or 'tooling-runtime=PowerShell' -cnotin $inputs) {
    throw 'Tooling receipt identity does not describe PowerShell-only validation'
}
$passed += 2
Write-Host "Skill validation: $passed behavioral cases passed."
