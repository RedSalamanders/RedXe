# Package checks complement SkillValidation.psm1's metadata grammar. They are read-only
# and repository-local so a consumer never depends on a sibling checkout to validate skills.
#Requires -Version 7.0
Set-StrictMode -Version Latest

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

function Get-PackageMarkdownLink([string] $Text) {
    # Bounded Markdown parsing: hide fenced/code-span content, then read inline/reference
    # destinations with balanced parentheses and Markdown punctuation escapes. Never evaluate text.
    $prose = [Text.StringBuilder]::new()
    $fence = $null
    foreach ($line in $Text.Replace("`r`n", "`n") -split "`n") {
        $marker = [regex]::Match($line, '^\s*(?:(?:[-+*]|\d+[.)])\s+)?(?<fence>`{3,}|~{3,})(?<rest>.*)$')
        $hidden = $null -ne $fence
        if ($marker.Success) {
            $token = $marker.Groups['fence'].Value
            if ($null -eq $fence) { $fence = $token }
            elseif ($token[0] -ceq $fence[0] -and $token.Length -ge $fence.Length -and -not $marker.Groups['rest'].Value.Trim()) { $fence = $null }
            $hidden = $true
        }
        [void]$prose.Append($(if ($hidden) { ' ' * $line.Length } else { $line })).Append("`n")
    }
    $characters = $prose.ToString().ToCharArray()
    for ($i = 0; $i -lt $characters.Length; $i++) {
        if ($characters[$i] -ne '`') { continue }
        $slashes = 0
        for ($j = $i - 1; $j -ge 0 -and $characters[$j] -eq '\'; $j--) { $slashes++ }
        if ($slashes % 2) { continue }
        $length = 1
        while ($i + $length -lt $characters.Length -and $characters[$i + $length] -eq '`') { $length++ }
        $closing = -1
        for ($j = $i + $length; $j -lt $characters.Length; $j++) {
            if ($characters[$j] -ne '`') { continue }
            $run = 1
            while ($j + $run -lt $characters.Length -and $characters[$j + $run] -eq '`') { $run++ }
            if ($run -eq $length) { $closing = $j + $run; break }
            $j += $run - 1
        }
        if ($closing -ge 0) {
            for ($j = $i; $j -lt $closing; $j++) { if ($characters[$j] -ne "`n") { $characters[$j] = ' ' } }
            $i = $closing - 1
        } else { $i += $length - 1 }
    }
    $textProse = [string]::new($characters)
    $lineNumber = 0
    foreach ($line in $textProse -split "`n") {
        $lineNumber++
        $definition = [regex]::Match($line, '^\s{0,3}\[[^\]]+\]:\s*')
        if ($definition.Success) {
            $destination = Read-PackageMarkdownDestination $line $definition.Length
            if ($null -ne $destination -and $destination.Target) { [pscustomobject]@{ Target = $destination.Target; Line = $lineNumber } }
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
        if ($end -lt $textProse.Length -and $textProse[$end] -in @('"',"'")) {
            $quote = $textProse[$end]; $end++
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

    $rootPath = [IO.Path]::GetFullPath($Root)
    $problems = [Collections.Generic.List[string]]::new()
    $markdown = [Collections.Generic.List[string]]::new()
    $skillRoot = Join-Path $rootPath '.agents/skills'
    $skills = 0
    $references = 0
    if (-not [IO.Directory]::Exists($skillRoot)) {
        $problems.Add('Missing .agents/skills directory')
    } elseif (@((Join-Path $rootPath '.agents'), $skillRoot) | Where-Object { [IO.File]::GetAttributes($_) -band [IO.FileAttributes]::ReparsePoint }) {
        $problems.Add('Skill package ancestry contains a reparse point')
    } else {
        $pending = [Collections.Generic.Stack[string]]::new()
        $pending.Push($skillRoot)
        while ($pending.Count) {
            $directory = $pending.Pop()
            foreach ($entry in Get-ChildItem -LiteralPath $directory -Force) {
                if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    $problems.Add("Skill package contains a reparse point: $($entry.FullName)")
                } elseif ($entry.PSIsContainer) {
                    $pending.Push($entry.FullName)
                } elseif ($entry.Extension -ieq '.md') {
                    $markdown.Add($entry.FullName)
                    if ($entry.Name -ceq 'SKILL.md') { $skills++ }
                } elseif ($entry.Name -ceq 'openai.yaml' -and -not [IO.File]::ReadAllText($entry.FullName).Trim()) {
                    $problems.Add("Empty optional skill UI metadata: $($entry.FullName)")
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
        $reparse = Get-PackageReparseProblem $path $rootPath
        if ($reparse) { $problems.Add($reparse); continue }
        $text = [IO.File]::ReadAllText($path, [Text.UTF8Encoding]::new($false, $true))
        if (-not $text.Trim()) { $problems.Add("Empty guidance: $([IO.Path]::GetRelativePath($rootPath, $path))") }
        foreach ($link in Get-PackageMarkdownLink $text) {
            $target = $link.Target
            $lineNumber = $link.Line
            if ($target.StartsWith('#') -or $target.StartsWith('//')) { continue }
            if ($target -match '^[A-Za-z][A-Za-z0-9+.-]*:' -and -not [IO.Path]::IsPathRooted($target)) {
                if ($target.StartsWith('file:', [StringComparison]::OrdinalIgnoreCase)) { $target = ([Uri]$target).LocalPath }
                else { continue }
            }
            $target = [Uri]::UnescapeDataString($target.Split('#', 2)[0])
            if (-not $target) { continue }
            $references++
            $resolved = [IO.Path]::GetFullPath([IO.Path]::Combine([IO.Path]::GetDirectoryName($path), $target))
            $relative = [IO.Path]::GetRelativePath($rootPath, $resolved)
            if ([IO.Path]::IsPathRooted($relative) -or $relative -eq '..' -or $relative.StartsWith('../') -or $relative.StartsWith('..\')) {
                $problems.Add("$([IO.Path]::GetRelativePath($rootPath, $path)):${lineNumber}: reference escapes this repository: $target")
            } elseif (($reparse = Get-PackageReparseProblem $resolved $rootPath)) {
                $problems.Add($reparse)
            } elseif (-not (Test-Path -LiteralPath $resolved)) {
                $problems.Add("$([IO.Path]::GetRelativePath($rootPath, $path)):${lineNumber}: missing local reference: $target")
            }
        }
    }
    return [pscustomobject]@{ Problems = $problems.ToArray(); Skills = $skills; References = $references }
}

Export-ModuleMember -Function Get-SkillPackageIntegrity
