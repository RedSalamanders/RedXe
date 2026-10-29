# Skill package text handling shared by SkillValidation.psm1 and SkillPackageIntegrity.psm1: strict UTF-8 reads and the one
# fence and code-span grammar that separates prose from code. Read-only and repository-local; it never evaluates text.
#Requires -Version 7.0
Set-StrictMode -Version Latest
# A module function sees the global preference, not its caller's, so a read failure must be terminating here.
$ErrorActionPreference = 'Stop'

function Get-FailureDetail([System.Management.Automation.ErrorRecord] $ErrorRecord) {
    # PowerShell wraps a .NET method failure; the inner exception holds the useful sentence.
    $inner = $ErrorRecord.Exception.InnerException
    if ($inner) { return $inner.Message }
    return $ErrorRecord.Exception.Message
}

function Read-SkillText {
    # Strict UTF-8 text of one skill package file. A failure throws a message that reads after "<file>:"; the caller names
    # the file. UTF-16/32 text and NUL bytes are always refused, because .NET would otherwise decode a UTF-16 file without a
    # word. A UTF-8 byte order mark is dropped unless RejectByteOrderMark is set: SKILL.md front matter must start at byte 0.
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $Path, [switch] $RejectByteOrderMark)
    try { $bytes = [IO.File]::ReadAllBytes($Path) }
    catch [System.Management.Automation.RuntimeException] { throw "cannot be read: $(Get-FailureDetail $_)" }
    $head = [BitConverter]::ToString($bytes, 0, [Math]::Min(4, $bytes.Length))
    if ($head -match '^(?:FF-FE|FE-FF|00-00-FE-FF)') { throw 'expected UTF-8 but found a UTF-16 or UTF-32 byte order mark; save the file as UTF-8' }
    $start = 0
    if ($head.StartsWith('EF-BB-BF', [StringComparison]::Ordinal)) {
        if ($RejectByteOrderMark) { throw 'expected UTF-8 without a byte order mark; remove the UTF-8 byte order mark' }
        $start = 3
    }
    if ([Array]::IndexOf($bytes, [byte]0) -ge 0) { throw 'expected UTF-8 text but found NUL bytes (UTF-16 without a byte order mark, or binary data)' }
    try { return [Text.UTF8Encoding]::new($false, $true).GetString($bytes, $start, $bytes.Length - $start) }
    catch [System.Management.Automation.RuntimeException] { throw "expected UTF-8 but found invalid bytes: $(Get-FailureDetail $_)" }
}

function Get-MarkdownFence {
    # The fenced-code map of Markdown text: Lines, Hidden (a fence marker or fenced content) and Unclosed (one-based Line and
    # Token of each opening fence no marker closes). A fence closes at a marker of the same character, at least as long and
    # with nothing after it. Markers use zero to three leading spaces (optionally after a list marker), so indented code
    # never opens or closes one, and a backtick fence's info string holds no backtick, so a line-leading code span is prose.
    # An unclosed fence hides only its own line: the text after it stays prose and keeps being checked.
    [CmdletBinding()]
    param([Parameter(Mandatory)][AllowEmptyString()][string] $Text)
    $lines = $Text.Replace("`r`n", "`n") -split "`n"
    $markers = @(foreach ($line in $lines) { [regex]::Match($line, '^ {0,3}(?:(?:[-+*]|\d+[.)])[ \t]+)?(?<fence>`{3,}|~{3,})(?<rest>.*)$') })
    $hidden = [bool[]]::new($lines.Length)
    $unclosed = [Collections.Generic.List[object]]::new()
    for ($i = 0; $i -lt $lines.Length; $i++) {
        $opening = $markers[$i]
        if (-not $opening.Success) { continue }
        $token = $opening.Groups['fence'].Value
        if ($token[0] -ceq '`' -and $opening.Groups['rest'].Value.Contains('`')) { continue }
        $hidden[$i] = $true
        $closing = -1
        for ($j = $i + 1; $j -lt $lines.Length -and $closing -lt 0; $j++) {
            $candidate = $markers[$j]
            if ($candidate.Success -and $candidate.Groups['fence'].Value[0] -ceq $token[0] -and
                $candidate.Groups['fence'].Value.Length -ge $token.Length -and -not $candidate.Groups['rest'].Value.Trim()) { $closing = $j }
        }
        if ($closing -lt 0) { $unclosed.Add([pscustomobject]@{ Line = $i + 1; Token = $token }); continue }
        for ($j = $i; $j -le $closing; $j++) { $hidden[$j] = $true }
        $i = $closing
    }
    return [pscustomobject]@{ Lines = $lines; Hidden = $hidden; Unclosed = $unclosed.ToArray() }
}

function Hide-MarkdownCodeSpan([string] $Block) {
    # Blank every inline code span of one block, keeping each newline. A backtick run opens a span that the next run of the
    # same length closes; an odd number of backslashes before a backtick escapes it, and an unmatched run is prose.
    $characters = $Block.ToCharArray()
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
    return [string]::new($characters)
}

function Get-MarkdownProse {
    # The text with fenced code and inline code spans replaced by spaces, so every line keeps its number and length, plus the
    # Unclosed fences of Get-MarkdownFence. A code span pairs only inside one block: a blank line, list item, heading,
    # setext underline, thematic break, table row or block-quote edge ends the search, so a stray backtick cannot hide the
    # links after it. This is a bounded block scan, not a Markdown evaluator.
    [CmdletBinding()]
    param([Parameter(Mandatory)][AllowEmptyString()][string] $Text)
    $fence = Get-MarkdownFence $Text
    $lines = $fence.Lines
    for ($i = 0; $i -lt $lines.Length; $i++) { if ($fence.Hidden[$i]) { $lines[$i] = ' ' * $lines[$i].Length } }
    $blockStart = -1
    $afterSingleLine = $false
    $afterQuote = $false
    for ($i = 0; $i -le $lines.Length; $i++) {
        $line = if ($i -lt $lines.Length) { $lines[$i] } else { '' }
        $boundary = -not $line.Trim()
        $singleLine = $false
        $quote = $false
        if (-not $boundary) {
            # One-line blocks: ATX heading, setext underline, thematic break and table row. A list item starts a block too.
            $singleLine = $line -cmatch '^ {0,3}(?:#{1,6}(?:[ \t]|$)|=+[ \t]*$|(?:\*[ \t]*){3,}$|(?:-[ \t]*){3,}$|(?:_[ \t]*){3,}$)|^[ \t]*\|'
            $quote = $line -cmatch '^ {0,3}>'
            $boundary = $singleLine -or $afterSingleLine -or $quote -ne $afterQuote -or $line -cmatch '^[ \t]*(?:[-+*]|\d{1,9}[.)])(?:[ \t]|$)'
        }
        if ($boundary -and $blockStart -ge 0) {
            $block = $lines[$blockStart..($i - 1)] -join "`n"
            if ($block.Contains('`')) {
                $masked = (Hide-MarkdownCodeSpan $block) -split "`n"
                for ($j = 0; $j -lt $masked.Length; $j++) { $lines[$blockStart + $j] = $masked[$j] }
            }
            $blockStart = -1
        }
        if ($line.Trim() -and $blockStart -lt 0) { $blockStart = $i }
        $afterSingleLine = $singleLine
        $afterQuote = $quote
    }
    return [pscustomobject]@{ Text = $lines -join "`n"; Unclosed = $fence.Unclosed }
}

Export-ModuleMember -Function Read-SkillText, Get-MarkdownFence, Get-MarkdownProse
