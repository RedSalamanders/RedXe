# Package checks complement SkillValidation.psm1's metadata grammar. They are read-only
# and repository-local so a consumer never depends on a sibling checkout to validate skills.
#Requires -Version 7.0
Set-StrictMode -Version Latest
# A module function sees the global preference, not its caller's, so a failure must be terminating here.
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'SkillText.psm1') -Force

function Get-PackageReparseProblem([string] $Path, [string] $Root) {
    # Inspect ancestry from the owned root outward before opening a package input or target.
    $rootPath = [IO.Path]::GetFullPath($Root)
    $relative = [IO.Path]::GetRelativePath($rootPath, [IO.Path]::GetFullPath($Path))
    if ([IO.Path]::IsPathRooted($relative) -or $relative -eq '..' -or $relative.StartsWith('../') -or $relative.StartsWith('..\')) { return "Non-repository package input: $Path" }
    $component = $rootPath
    foreach ($segment in @('.') + @($relative -split '[\\/]' | Where-Object { $_ -and $_ -ne '.' })) {
        if ($segment -ne '.') { $component = [IO.Path]::Combine($component, $segment) }
        if (-not [IO.File]::Exists($component) -and -not [IO.Directory]::Exists($component)) { return $null }
        if ([IO.File]::GetAttributes($component) -band [IO.FileAttributes]::ReparsePoint) { return "Reparse package path: $component" }
    }
    return $null
}

function Get-PackageCaseMismatch([string] $Resolved, [string] $Root, [hashtable] $Listings) {
    # A case-insensitive host opens a link that GitHub cannot. Compare each existing component below the root, ordinally,
    # with the spelling on disk: the on-disk relative path when one differs, otherwise $null. Listings caches directories.
    $current = $Root
    $actual = [Collections.Generic.List[string]]::new()
    $differs = $false
    foreach ($segment in @([IO.Path]::GetRelativePath($Root, $Resolved) -split '[\\/]' | Where-Object { $_ -and $_ -ne '.' })) {
        if (-not $Listings.ContainsKey($current)) {
            $names = [string[]]@([IO.Directory]::EnumerateFileSystemEntries($current) | ForEach-Object { [IO.Path]::GetFileName($_) })
            $Listings[$current] = [Collections.Generic.HashSet[string]]::new($names, [StringComparer]::Ordinal)
        }
        $name = $segment
        if (-not $Listings[$current].Contains($segment)) {
            $name = $Listings[$current] | Where-Object { $_ -ieq $segment } | Select-Object -First 1
            if (-not $name) { return $null }
            $differs = $true
        }
        $actual.Add($name)
        $current = [IO.Path]::Combine($current, $name)
    }
    if ($differs) { return $actual -join '/' }
    return $null
}

function Read-PackageMarkdownDestination([string] $Text, [int] $Start) {
    $position = $Start
    while ($position -lt $Text.Length -and [char]::IsWhiteSpace($Text[$position])) { $position++ }
    $angle = $position -lt $Text.Length -and $Text[$position] -eq '<'
    if ($angle) { $position++ }
    $value = [Text.StringBuilder]::new()
    $depth = 0
    while ($position -lt $Text.Length) {
        $character = $Text[$position]
        if ($character -eq '\' -and $position + 1 -lt $Text.Length -and $Text[$position + 1] -in @('\','(',')','[',']','<','>')) {
            [void]$value.Append($Text[$position + 1]); $position += 2; continue
        }
        if ($angle) {
            if ($character -eq '>') { return [pscustomobject]@{ Target = $value.ToString(); End = $position + 1 } }
            if ($character -eq "`n") { return $null }
        } else {
            if ([char]::IsWhiteSpace($character) -or ($character -eq ')' -and $depth -eq 0)) { break }
            if ($character -eq '(') { $depth++ }
            elseif ($character -eq ')') { $depth-- }
        }
        [void]$value.Append($character); $position++
    }
    if ($angle -or $depth -ne 0) { return $null }
    return [pscustomobject]@{ Target = $value.ToString(); End = $position }
}

function Get-PackageMarkdownLink([string] $TextProse) {
    # Bounded Markdown parsing of text that Get-MarkdownProse already stripped of fenced and code-span content: read inline
    # and reference destinations with balanced parentheses and Markdown punctuation escapes. Never evaluate text.
    $lines = $TextProse -split "`n"
    for ($lineIndex = 0; $lineIndex -lt $lines.Count; $lineIndex++) {
        $line = $lines[$lineIndex]
        # A footnote definition ([^1]: note) is text, not a link reference definition.
        $definition = [regex]::Match($line, '^[ \t]{0,3}\[(?!\^)[^\]]+\]:[ \t]*')
        if ($definition.Success) {
            $destination = Read-PackageMarkdownDestination $line $definition.Length
            if (($null -eq $destination -or -not $destination.Target) -and $lineIndex + 1 -lt $lines.Count -and $lines[$lineIndex + 1].Trim()) {
                $destination = Read-PackageMarkdownDestination $lines[$lineIndex + 1] 0
            }
            if ($null -ne $destination -and $destination.Target) { [pscustomobject]@{ Target = $destination.Target; Line = $lineIndex + 1 } }
        }
    }
    $brackets = [Collections.Generic.Stack[int]]::new()
    for ($i = 0; $i -lt $textProse.Length; $i++) {
        $character = $textProse[$i]
        if ($character -eq '\' -and $i + 1 -lt $textProse.Length -and $textProse[$i + 1] -in @('\','[',']')) { $i++; continue }
        if ($character -eq '[') { $brackets.Push($i); continue }
        if ($character -ne ']' -or -not $brackets.Count) { continue }
        $start = $brackets.Pop()
        if ($i + 1 -ge $textProse.Length -or $textProse[$i + 1] -ne '(') { continue }
        $destination = Read-PackageMarkdownDestination $textProse ($i + 2)
        if ($null -eq $destination -or -not $destination.Target) { continue }
        $end = $destination.End
        while ($end -lt $textProse.Length -and [char]::IsWhiteSpace($textProse[$end])) { $end++ }
        if ($end -lt $textProse.Length -and $textProse[$end] -in @('"',"'",'(')) {
            $quote = if ($textProse[$end] -eq '(') { ')' } else { $textProse[$end] }; $end++
            while ($end -lt $textProse.Length -and $textProse[$end] -ne $quote) { if ($textProse[$end] -eq '\') { $end++ }; $end++ }
            $end++
            while ($end -lt $textProse.Length -and [char]::IsWhiteSpace($textProse[$end])) { $end++ }
        }
        if ($end -lt $textProse.Length -and $textProse[$end] -eq ')') {
            [pscustomobject]@{ Target = $destination.Target; Line = ($textProse.Substring(0, $start) -split "`n").Length }
            $i = $end
        }
    }
}

function Get-SkillPackageIntegrity {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $Root)

    # A relative root means the caller's PowerShell location, which .NET file APIs do not follow.
    $rootPath = [IO.Path]::GetFullPath($PSCmdlet.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Root))
    $problems = [Collections.Generic.List[string]]::new()
    $markdown = [Collections.Generic.List[string]]::new()
    $listings = @{}
    $skillRoot = Join-Path $rootPath '.agents/skills'
    $skills = 0
    $references = 0
    $rootProblem = Get-PackageReparseProblem $rootPath $rootPath
    if ($rootProblem) { return [pscustomobject]@{ Problems = @($rootProblem); Skills = 0; References = 0 } }
    if (-not [IO.Directory]::Exists($skillRoot)) {
        $problems.Add('Missing .agents/skills directory')
    } elseif (($reparse = Get-PackageReparseProblem $skillRoot $rootPath)) {
        # The traversal below must not list through a linked .agents or .agents/skills.
        $problems.Add($reparse)
    } else {
        $pending = [Collections.Generic.Stack[string]]::new()
        $pending.Push($skillRoot)
        while ($pending.Count) {
            $directory = $pending.Pop()
            try { $entries = @(Get-ChildItem -LiteralPath $directory -Force) }
            catch [System.Management.Automation.RuntimeException] {
                $problems.Add("Cannot list skill package directory ${directory}: $($_.Exception.Message)")
                continue
            }
            foreach ($entry in $entries) {
                if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    $problems.Add("Skill package contains a reparse point: $($entry.FullName)")
                } elseif ($entry.PSIsContainer) {
                    $pending.Push($entry.FullName)
                } elseif ($entry.Extension -ieq '.md') {
                    $markdown.Add($entry.FullName)
                    if ($entry.Name -ceq 'SKILL.md') { $skills++ }
                } elseif ($entry.Name -ceq 'openai.yaml') {
                    try { $metadata = [IO.File]::ReadAllText($entry.FullName) }
                    catch [System.Management.Automation.RuntimeException] {
                        $problems.Add("Cannot read optional skill UI metadata $($entry.FullName): $($_.Exception.Message)")
                        continue
                    }
                    if (-not $metadata.Trim()) { $problems.Add("Empty optional skill UI metadata: $($entry.FullName)") }
                }
            }
        }
    }
    if (-not $skills) { $problems.Add('No SKILL.md packages found') }
    foreach ($relative in @('AGENTS.md', 'CLAUDE.md', '.github/copilot-instructions.md')) {
        $path = Join-Path $rootPath $relative
        if ([IO.File]::Exists($path)) { $markdown.Add($path) }
        elseif ($relative -ceq 'AGENTS.md') { $problems.Add('Missing canonical AGENTS.md') }
    }
    foreach ($path in $markdown) {
        $label = [IO.Path]::GetRelativePath($rootPath, $path)
        $reparse = Get-PackageReparseProblem $path $rootPath
        if ($reparse) { $problems.Add($reparse); continue }
        # A file that cannot be read is a problem that names it, never the previous file's text. SKILL.md front matter
        # must start at byte 0, so only SKILL.md refuses a UTF-8 byte order mark.
        try { $text = Read-SkillText $path -RejectByteOrderMark:([IO.Path]::GetFileName($path) -ceq 'SKILL.md') }
        catch [System.Management.Automation.RuntimeException] { $problems.Add("${label}: $($_.Exception.Message)"); continue }
        if (-not $text.Trim()) { $problems.Add("Empty guidance: $label") }
        $prose = Get-MarkdownProse $text
        foreach ($fence in $prose.Unclosed) { $problems.Add("${label}:$($fence.Line): unclosed code fence (opened with $($fence.Token))") }
        foreach ($link in Get-PackageMarkdownLink $prose.Text) {
            $target = $link.Target
            $lineNumber = $link.Line
            if ($target.StartsWith('#') -or $target.StartsWith('//')) { continue }
            $fileUri = $false
            if ($target -match '^[A-Za-z][A-Za-z0-9+.-]*:' -and -not [IO.Path]::IsPathRooted($target)) {
                if ($target.StartsWith('file:', [StringComparison]::OrdinalIgnoreCase)) {
                    try { $target = ([Uri]$target).LocalPath }
                    catch [System.Management.Automation.RuntimeException] { $problems.Add("${label}:${lineNumber}: invalid file URI: $target"); continue }
                }
                else { continue }
                $fileUri = $true
            }
            # A fragment and then a query end the path (GitHub's ?plain=1); a file URI's LocalPath already omits both.
            if (-not $fileUri) { $target = [Uri]::UnescapeDataString($target.Split('#', 2)[0].Split('?', 2)[0]) }
            if (-not $target) { continue }
            $references++
            $resolved = [IO.Path]::GetFullPath([IO.Path]::Combine([IO.Path]::GetDirectoryName($path), $target))
            $relative = [IO.Path]::GetRelativePath($rootPath, $resolved)
            if ([IO.Path]::IsPathRooted($relative) -or $relative -eq '..' -or $relative.StartsWith('../') -or $relative.StartsWith('..\')) {
                $problems.Add("${label}:${lineNumber}: reference escapes this repository: $target")
            } elseif (($reparse = Get-PackageReparseProblem $resolved $rootPath)) {
                $problems.Add($reparse)
            } elseif (-not (Test-Path -LiteralPath $resolved)) {
                $problems.Add("${label}:${lineNumber}: missing local reference: $target")
            } elseif (($onDisk = Get-PackageCaseMismatch $resolved $rootPath $listings)) {
                $problems.Add("${label}:${lineNumber}: local reference differs in case from the file on disk: $($relative.Replace('\', '/')) (on disk: $onDisk)")
            }
        }
    }
    return [pscustomobject]@{ Problems = $problems.ToArray(); Skills = $skills; References = $references }
}

Export-ModuleMember -Function Get-SkillPackageIntegrity
