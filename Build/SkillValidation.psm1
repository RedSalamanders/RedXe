# Repository-owned skill grammar. Unlike DxUi's single-line subset, this module also
# supports literal/folded descriptions and a one-level metadata mapping. It never
# evaluates YAML tags, aliases or expressions, and rejects unsupported syntax.
# Kept local so validation does not depend on another checkout or an unpinned module.
#Requires -Version 7.0
Set-StrictMode -Version Latest

function ConvertFrom-SkillScalar {
    param([AllowEmptyString()][string]$Value)
    if (-not $Value) { return $null }
    if ($Value.StartsWith("'")) {
        $match = [regex]::Match($Value, "^'(?<text>(?:[^']|'')*)'(?: +#.*)?$")
        if (-not $match.Success) { throw 'invalid single-quoted scalar' }
        return $match.Groups['text'].Value.Replace("''", "'")
    }
    if ($Value.StartsWith('"')) {
        $match = [regex]::Match($Value, '^(?<quoted>"(?:[^"\\]|\\.)*")(?: +#.*)?$')
        if (-not $match.Success) { throw 'invalid double-quoted scalar' }
        return ConvertFrom-Json -InputObject $match.Groups['quoted'].Value -ErrorAction Stop
    }
    $plain = [regex]::Replace($Value, ' +#.*$', '')
    if ($plain -match '^[-?:,\[\]{}#&*!|>''"%@`]' -or $plain.Contains(': ') -or $plain.EndsWith(':') -or $plain -match '[\x00-\x1f]') {
        throw 'unsupported plain scalar; quote the value'
    }
    # Preserve the old YAML 1.1 type checks for required string fields.
    if ($plain -cmatch '^(?:~|null|Null|NULL|yes|Yes|YES|no|No|NO|true|True|TRUE|false|False|FALSE|on|On|ON|off|Off|OFF|=|<<)$' -or
        $plain -cmatch '^[-+]?(?:[0-9][0-9_:]*(?:\.[0-9_]*)?(?:[eE][-+]?[0-9]+)?|\.[0-9_]+(?:[eE][-+]?[0-9]+)?|0b[01_]+|0x[0-9a-fA-F_]+|\.(?:inf|Inf|INF|nan|NaN|NAN))$' -or
        $plain -cmatch '^[0-9]{4}-[0-9]{1,2}-[0-9]{1,2}') {
        return [pscustomobject]@{ NonStringScalar = $plain }
    }
    return $plain
}

function ConvertFrom-SkillMetadata {
    param([string]$Text)
    $values = [ordered]@{}
    $lines = $Text -split "`n"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if (-not $line.Trim() -or $line.TrimStart().StartsWith('#')) { continue }
        $match = [regex]::Match($line, '^(?<key>[a-z][a-z-]*):(?: +(?<value>.*?))? *$')
        if (-not $match.Success) { throw "unsupported mapping at line $($i + 1)" }
        $key = $match.Groups['key'].Value
        if ($key -cnotin @('name', 'description', 'license', 'allowed-tools', 'metadata')) { throw "unsupported front matter key '$key'" }
        if ($values.Contains($key)) { throw "duplicate front matter key '$key'" }
        $raw = $match.Groups['value'].Value
        if ($key -ceq 'metadata') {
            if ($raw -and -not $raw.StartsWith('#')) { throw 'metadata requires an indented scalar mapping' }
            $nested = [ordered]@{}
            while ($i + 1 -lt $lines.Count -and ($lines[$i + 1] -match '^ ' -or -not $lines[$i + 1].Trim())) {
                $i++
                if (-not $lines[$i].Trim() -or $lines[$i].TrimStart().StartsWith('#')) { continue }
                $item = [regex]::Match($lines[$i], '^  (?<key>[A-Za-z_][A-Za-z0-9_-]*): +(?<value>.*?) *$')
                if (-not $item.Success) { throw 'metadata supports two-space-indented scalar values only' }
                $nestedKey = $item.Groups['key'].Value
                if ($nested.Contains($nestedKey)) { throw "duplicate metadata key '$nestedKey'" }
                $nested[$nestedKey] = ConvertFrom-SkillScalar $item.Groups['value'].Value
            }
            $values[$key] = $nested
        } elseif ($raw -match '^(?<style>[|>])(?<chomp>[-+]?)(?: +#.*)?$') {
            $style = $Matches.style
            $chomp = $Matches.chomp
            $block = [Collections.Generic.List[string]]::new()
            while ($i + 1 -lt $lines.Count -and ($lines[$i + 1] -match '^ ' -or -not $lines[$i + 1].Trim())) {
                $i++
                if ($lines[$i] -match '^  (?<content>.*)$') { $block.Add($Matches.content) }
                elseif (-not $lines[$i].Trim()) { $block.Add('') }
                else { throw 'block scalars require two-space indentation' }
            }
            # Fold only adjacent ordinary lines; blank and more-indented lines retain their breaks.
            $builder = [Text.StringBuilder]::new()
            for ($j = 0; $j -lt $block.Count; $j++) {
                [void]$builder.Append($block[$j])
                $fold = $style -ceq '>' -and $j + 1 -lt $block.Count -and $block[$j] -and $block[$j + 1] -and
                    -not $block[$j].StartsWith(' ') -and -not $block[$j + 1].StartsWith(' ')
                # One empty line between ordinary paragraphs represents one newline.
                # Breaks around more-indented text and leading/trailing blanks are kept.
                $paragraphBreak = $false
                if ($style -ceq '>' -and $block[$j] -and -not $block[$j].StartsWith(' ') -and $j + 1 -lt $block.Count -and -not $block[$j + 1]) {
                    $next = $j + 1
                    while ($next -lt $block.Count -and -not $block[$next]) { $next++ }
                    $paragraphBreak = $next -lt $block.Count -and -not $block[$next].StartsWith(' ')
                }
                [void]$builder.Append($(if ($fold) { ' ' } elseif ($paragraphBreak) { '' } else { "`n" }))
            }
            $value = $builder.ToString()
            if ($chomp -cne '+') { $value = $value.TrimEnd("`n") + $(if ($chomp -ceq '-' -or -not $value) { '' } else { "`n" }) }
            $values[$key] = $value
        } else {
            $values[$key] = ConvertFrom-SkillScalar $raw
        }
    }
    return $values
}

function Get-SkillValidationProblem {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$FolderName, [AllowNull()][AllowEmptyString()][string]$Text)
    if ($null -eq $Text -or -not $Text) { return 'missing SKILL.md or empty file' }
    $normalized = $Text.Replace("`r`n", "`n")
    $match = [regex]::Match($normalized, '\A---\n(?<metadata>.*?)\n---(?:\n|$)(?<body>[\s\S]*)\z', [Text.RegularExpressions.RegexOptions]::Singleline)
    if (-not $match.Success) { return 'missing YAML front matter' }
    try { $metadata = ConvertFrom-SkillMetadata $match.Groups['metadata'].Value }
    catch [System.Management.Automation.RuntimeException] { return "invalid YAML subset: $($_.Exception.Message)" }
    catch [System.ArgumentException] { return "invalid YAML subset: $($_.Exception.Message)" }
    $name = if ($metadata.Contains('name')) { $metadata['name'] } else { $null }
    if ($name -isnot [string] -or $name -cne $FolderName -or $name.Length -gt 64 -or $name -cnotmatch '\A[a-z0-9]+(?:-[a-z0-9]+)*\z') {
        return 'name must match the folder and use lowercase hyphen-separated words'
    }
    $description = if ($metadata.Contains('description')) { $metadata['description'] } else { $null }
    if ($description -isnot [string] -or -not $description.Trim() -or $description.Contains('<') -or $description.Contains('>')) { return 'invalid description' }
    # The previous validator counted Unicode code points, not UTF-16 code units.
    $descriptionLength = 0
    foreach ($rune in $description.EnumerateRunes()) { $descriptionLength++ }
    if ($descriptionLength -gt 1024) { return 'invalid description' }
    if ($description.TrimStart().StartsWith('[TODO:', [StringComparison]::Ordinal)) { return 'unfinished description' }
    $body = $match.Groups['body'].Value
    if (-not $body.Trim()) { return 'empty skill instructions' }
    $fence = $null
    foreach ($line in $body -split "`n") {
        $marker = [regex]::Match($line, '^[ \t]*(?:(?:[-+*]|\d+[.)])[ \t]+)?(?<fence>`{3,}|~{3,})(?<rest>.*)$')
        if ($marker.Success) {
            $token = $marker.Groups['fence'].Value
            if ($null -eq $fence) { $fence = $token }
            elseif ($token[0] -ceq $fence[0] -and $token.Length -ge $fence.Length -and -not $marker.Groups['rest'].Value.Trim()) { $fence = $null }
        } elseif ($null -eq $fence -and $line -cmatch '\A[ ]{0,3}\[TODO:[^\n]*\][ \t]*\z') { return 'unfinished instructions' }
    }
    return $null
}

function Test-RepositorySkills {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Root)
    $skillRoot = Join-Path $Root '.agents/skills'
    if (-not [IO.Directory]::Exists($skillRoot)) { throw "The repository skill directory was not found: $skillRoot" }
    $folders = @(Get-ChildItem -LiteralPath $skillRoot -Directory | Sort-Object Name)
    if (-not $folders.Count) { throw 'No repository skills found' }
    $failures = [Collections.Generic.List[string]]::new()
    foreach ($folder in $folders) {
        $path = Join-Path $folder.FullName 'SKILL.md'
        $text = if ([IO.File]::Exists($path)) { [IO.File]::ReadAllText($path, [Text.UTF8Encoding]::new($false, $true)) } else { $null }
        $problem = Get-SkillValidationProblem -FolderName $folder.Name -Text $text
        if ($problem) { $failures.Add("$($folder.Name): $problem") }
        else { Write-Host "Validated $($folder.Name)" }
    }
    if ($failures.Count) { throw ($failures -join "`n") }
    return $folders.Count
}

Export-ModuleMember -Function Get-SkillValidationProblem, Test-RepositorySkills
