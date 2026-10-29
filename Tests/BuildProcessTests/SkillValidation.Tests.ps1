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
    @{Name='double quoted timestamp'; Text=$prefix.Replace('description: valid','description: "2026-10-28T09:00:00Z"')+'Read.'; Valid=$true},
    @{Name='date-prefixed prose'; Text=$prefix.Replace('description: valid','description: 2026-10-28 migration guidance')+'Read.'; Valid=$true},
    @{Name='timestamp-prefixed prose'; Text=$prefix.Replace('description: valid','description: 2026-10-28T09:00:00Z migration guidance')+'Read.'; Valid=$true},
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
    @{Name='TODO after line-leading inline code'; Text=$prefix+'```literal```'+"`n`n[TODO: finish]"; Valid=$false},
    @{Name='tilde info permits backticks'; Text=$prefix+'~~~info`'+"`n[TODO: example]`n~~~"; Valid=$true},
    @{Name='indented code does not open a fence'; Text=$prefix+'    ```'+"`n`n[TODO: finish]"; Valid=$false},
    @{Name='tab-indented code does not open a fence'; Text=$prefix+"`t"+'```'+"`n`n[TODO: finish]"; Valid=$false},
    @{Name='overindented marker does not close fence'; Text=$prefix+'```text'+"`n    "+'```'+"`n[TODO: example]`n"+'```'; Valid=$true},
    @{Name='tilde list fence'; Text=$prefix+"- ~~~~text`n[TODO: example]`n  ~~~~"; Valid=$true},
    @{Name='short fence stays open'; Text=$prefix+'````text'+"`n"+'```'+"`n[TODO: example]`n"+'````'; Valid=$true},
    @{Name='nested mapping refused'; Text=$prefix.Replace('description: valid',"description: valid`nmetadata:`n  nested:`n    key: value")+'Read.'; Valid=$false},
    @{Name='flow collection refused'; Text=$prefix.Replace('description: valid','description: [one, two]')+'Read.'; Valid=$false},
    @{Name='tag refused'; Text=$prefix.Replace('description: valid','description: !!str unsafe')+'Read.'; Valid=$false},
    @{Name='anchor refused'; Text=$prefix.Replace('description: valid','description: &anchor text')+'Read.'; Valid=$false},
    @{Name='alias refused'; Text=$prefix.Replace('description: valid','description: *anchor')+'Read.'; Valid=$false},
    @{Name='malformed quote'; Text=$prefix.Replace('description: valid',"description: 'unclosed")+'Read.'; Valid=$false},
    @{Name='bad JSON escape'; Text=$prefix.Replace('description: valid','description: "bad\q"')+'Read.'; Valid=$false},
    @{Name='invalid indentation'; Text=$prefix.Replace('description: valid',"description: |`n one space")+'Read.'; Valid=$false},
    # An unclosed fence is a problem that names its line, and the text after it is still checked.
    @{Name='unclosed backtick fence'; Text=$prefix+'```text'+"`nexample"; Valid=$false; Expect='SKILL.md:5: unclosed code fence'},
    @{Name='unclosed tilde fence'; Text=$prefix+"~~~~`nexample"; Valid=$false; Expect='SKILL.md:5: unclosed code fence'},
    @{Name='unclosed fence names its own line'; Text=$prefix+"Intro.`n`n"+'```text'+"`nexample"; Valid=$false; Expect='SKILL.md:7: unclosed code fence'},
    @{Name='unclosed fence line follows multi-line front matter'; Text="---`nname: example-skill`ndescription: >-`n  Folded`n  text.`n---`n"+'```'+"`nexample"; Valid=$false; Expect='SKILL.md:7: unclosed code fence'},
    @{Name='unclosed fence in CRLF text'; Text=($prefix+'```text'+"`nexample").Replace("`n","`r`n"); Valid=$false; Expect='SKILL.md:5: unclosed code fence'},
    @{Name='shorter closing marker leaves a fence unclosed'; Text=$prefix+'````text'+"`nexample`n"+'```'; Valid=$false; Expect='SKILL.md:5: unclosed code fence'},
    @{Name='placeholder after an unclosed fence is still reported'; Text=$prefix+'```text'+"`nexample`n[TODO: finish]"; Valid=$false; Expect=@('SKILL.md:5: unclosed code fence','unfinished instructions')},
    @{Name='fence closed by a longer marker'; Text=$prefix+'```text'+"`n[TODO: example]`n"+'`````'; Valid=$true},
    @{Name='fences in sequence'; Text=$prefix+'```text'+"`nexample`n"+'```'+"`ntext`n~~~`n[TODO: example]`n~~~"; Valid=$true},
    @{Name='line-leading inline code is not an unclosed fence'; Text=$prefix+'```literal```'+"`nRead."; Valid=$true},
    @{Name='indented marker is not an unclosed fence'; Text=$prefix+'    ```'+"`nRead."; Valid=$true}
)
foreach ($typed in @('123','true','yes','null','2026-10-28','2026-10-28T09:00:00Z','2026-10-28 9:00:00 +02:30','2026-10-28t09:00:00.25-08:00','1.5','0x10','.inf','off')) {
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
    foreach ($expected in @(if ($case.ContainsKey('Expect')) { $case.Expect })) {
        if (-not $problem.Contains($expected)) { throw "Skill fixture '$($case.Name)' reported '$problem' instead of '$expected'" }
    }
    $passed++
}
$fixture = Join-Path $repository ('.build/SkillValidation-' + [guid]::NewGuid().ToString('N'))
try {
    [void][IO.Directory]::CreateDirectory((Join-Path $fixture '.agents/skills/example-skill'))
    $path = Join-Path $fixture '.agents/skills/example-skill/SKILL.md'
    [IO.File]::WriteAllText($path,$prefix+'Read.',[Text.UTF8Encoding]::new($false))
    if ((Test-RepositorySkills $fixture) -ne 1) { throw 'Aggregate did not validate the fixture skill' }
    $hidden = Join-Path $fixture '.agents/skills/hidden-skill'
    [void][IO.Directory]::CreateDirectory($hidden)
    [IO.File]::WriteAllText((Join-Path $hidden 'SKILL.md'), 'Malformed hidden skill')
    [IO.File]::SetAttributes($hidden, [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::Directory)
    try {
        $message=''; try { Test-RepositorySkills $fixture | Out-Null } catch [System.Management.Automation.RuntimeException] { $message=$_.Exception.Message }
        if ($message -notlike '*hidden-skill: missing YAML front matter*') { throw 'Hidden package bypassed metadata validation' }
        $passed++
    } finally { [IO.File]::SetAttributes($hidden, [IO.FileAttributes]::Directory); [IO.File]::Delete((Join-Path $hidden 'SKILL.md')); [IO.Directory]::Delete($hidden) }
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
# File-level controls. A module function sees the global error preference, not this script's, so each call runs under
# PowerShell's default one: the corpus recipe in Build_SkillPackages.md does not set Stop either.
$fixtureParent = Join-Path $repository '.build'
$fileFixture = Join-Path $fixtureParent ('SkillValidation-files-' + [guid]::NewGuid().ToString('N'))
$utf8 = [Text.UTF8Encoding]::new($false)
function Write-SkillBytes([string]$Folder, [byte[]]$Bytes) {
    $directory = Join-Path $fileFixture ".agents/skills/$Folder"
    [void][IO.Directory]::CreateDirectory($directory)
    [IO.File]::WriteAllBytes((Join-Path $directory 'SKILL.md'), $Bytes)
}
function Get-SkillFailure([string]$Root) {
    $previous = $global:ErrorActionPreference
    $global:ErrorActionPreference = 'Continue'
    try { [void](Test-RepositorySkills -Root $Root 6>$null); return '' }
    catch [System.Management.Automation.RuntimeException] { return $_.Exception.Message }
    finally { $global:ErrorActionPreference = $previous }
}
try {
    $valid = $utf8.GetBytes($prefix + 'Read.')
    $little = [Text.UnicodeEncoding]::new($false, $true)
    $big = [Text.UnicodeEncoding]::new($true, $true)
    foreach ($encoding in @(
        @{Name='UTF-8 byte order mark'; Bytes=[byte[]](@(0xEF,0xBB,0xBF) + $valid); Expect='expected UTF-8 without a byte order mark'},
        @{Name='UTF-16 little endian'; Bytes=[byte[]]($little.GetPreamble() + $little.GetBytes($prefix + 'Read.')); Expect='UTF-16 or UTF-32 byte order mark'},
        @{Name='UTF-16 big endian'; Bytes=[byte[]]($big.GetPreamble() + $big.GetBytes($prefix + 'Read.')); Expect='UTF-16 or UTF-32 byte order mark'},
        @{Name='UTF-16 without a byte order mark'; Bytes=[Text.UnicodeEncoding]::new($false, $false).GetBytes($prefix + 'Read.'); Expect='NUL bytes'},
        @{Name='invalid UTF-8'; Bytes=[byte[]](@($valid) + @(0xC3, 0x28)); Expect='invalid bytes'}
    )) {
        Write-SkillBytes 'example-skill' $encoding.Bytes
        $message = Get-SkillFailure $fileFixture
        if ($message -notlike '*example-skill: *SKILL.md: *' -or -not $message.Contains($encoding.Expect)) { throw "Aggregate did not reject $($encoding.Name) and name its file: $message" }
        # Positive control: the same skill as plain UTF-8 passes.
        Write-SkillBytes 'example-skill' $valid
        if (Get-SkillFailure $fileFixture) { throw "Aggregate rejected plain UTF-8 after $($encoding.Name)" }
        $passed += 2
    }
    # An unreadable skill after a valid one is a failure that names its file; the valid skill's text is never reused for it.
    Write-SkillBytes 'aaa-valid' ($utf8.GetBytes($prefix.Replace('example-skill', 'aaa-valid') + 'Read.'))
    Write-SkillBytes 'zzz-unreadable' ([byte[]](0xC3, 0x28))
    $message = Get-SkillFailure $fileFixture
    if ($message -notlike '*zzz-unreadable: *zzz-unreadable*SKILL.md: expected UTF-8 but found invalid bytes*' -or $message.Contains('aaa-valid')) { throw "An unreadable skill was not reported by file: $message" }
    Remove-Item -LiteralPath (Join-Path $fileFixture '.agents/skills/zzz-unreadable') -Recurse -Force
    if (Get-SkillFailure $fileFixture) { throw 'The valid skills failed once the unreadable one was removed' }
    $passed += 2
    # A relative root is the caller's PowerShell location; Set-Location leaves the process directory alone.
    $process = [Environment]::CurrentDirectory
    Push-Location $fixtureParent
    try {
        [Environment]::CurrentDirectory = $repository
        if ((Test-RepositorySkills -Root (Split-Path $fileFixture -Leaf) 6>$null) -ne 2) { throw 'A relative root was not resolved against the PowerShell location' }
        # Negative control: the process directory holds a package, the PowerShell location does not.
        [Environment]::CurrentDirectory = $fileFixture
        $message = Get-SkillFailure '.'
        if ($message -notlike '*repository skill directory was not found*') { throw "A relative root followed the process directory: $message" }
    } finally { [Environment]::CurrentDirectory = $process; Pop-Location }
    $passed += 2
} finally {
    $resolved = [IO.Path]::GetFullPath($fileFixture)
    if (-not $resolved.StartsWith([IO.Path]::GetFullPath($fixtureParent) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Refused fixture cleanup outside the repository .build directory' }
    if ([IO.Directory]::Exists($resolved)) { Remove-Item -LiteralPath $resolved -Recurse -Force }
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
