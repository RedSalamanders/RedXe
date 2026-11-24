#Requires -Version 7.4
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Join-Path (Split-Path $PSScriptRoot -Parent) 'RedXe'
$encoding = [Text.UTF8Encoding]::new($false, $true)

function Read-StringTable([string]$Path, [switch]$Source) {
    $text = [IO.File]::ReadAllText($Path, $encoding)
    if ($text -cnotmatch '(?m)^#pragma[ \t]+code_page\(65001\)[ \t]*\r?$') {
        throw "$Path needs the UTF-8 resource code page."
    }
    $entries = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
    $tables = [regex]::Matches($text, '(?ms)^STRINGTABLE[ \t]*\r?\n[ \t]*BEGIN[ \t]*\r?\n(?<body>.*?)^[ \t]*END[ \t]*\r?$')
    foreach ($table in $tables) {
        foreach ($line in ($table.Groups['body'].Value -split '\r?\n')) {
            $entry = $line.Trim()
            if (-not $entry -or $entry.StartsWith('//')) { continue }
            if ($entry -cnotmatch '^(?<id>IDS_[A-Z0-9_]+)\s+"(?<text>(?:[^"]|"")*)"$') {
                throw "Invalid STRINGTABLE entry in $Path. $entry"
            }
            $id = $Matches.id
            $value = $Matches.text
            if ($value -cmatch '%(?:[-+#0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|I64|I32|I)?[sSdiuxXfcC])') {
                throw "$Path has a printf placeholder in $id."
            }
            $tokens = [Collections.Generic.List[string]]::new()
            $indexes = [Collections.Generic.HashSet[int]]::new()
            foreach ($token in [regex]::Matches($value, '\{\{|\}\}|\{(?<index>0|[1-9][0-9]*)(?::[^{}]+)?\}|[{}]')) {
                if ($token.Value -cin '{{','}}') { continue }
                if (-not $token.Groups['index'].Success) {
                    throw "$Path has an invalid positional placeholder in $id."
                }
                $index = [int]$token.Groups['index'].Value
                if ($Source -and -not $indexes.Contains($index) -and $index -ne $indexes.Count) {
                    throw "$Path introduces $($token.Value) out of argument order in $id."
                }
                [void]$indexes.Add($index)
                $tokens.Add($token.Value)
            }
            if (-not $entries.TryAdd($id, (($tokens | Sort-Object -CaseSensitive) -join '|'))) {
                throw "$Path defines $id twice."
            }
        }
    }
    if (-not $entries.Count) { throw "$Path has no STRINGTABLE entries." }
    return ,$entries
}

$base = Read-StringTable (Join-Path $root 'RedXe.rc') -Source
$ids = [Collections.Generic.Dictionary[string, int]]::new([StringComparer]::Ordinal)
$values = [Collections.Generic.HashSet[int]]::new()
$definitions = [IO.File]::ReadAllText((Join-Path $root 'resource.h'), $encoding)
foreach ($match in [regex]::Matches($definitions, '(?m)^#define\s+(?<id>IDS_[A-Z0-9_]+)\s+(?<value>[0-9]+)[ \t]*\r?$')) {
    $number = [int]$match.Groups['value'].Value
    if ($number -lt 1 -or $number -gt 65535 -or -not $values.Add($number) -or -not $ids.TryAdd($match.Groups['id'].Value, $number)) {
        throw "Invalid or duplicate resource identifier $($match.Value)."
    }
}
foreach ($id in $base.Keys) {
    if (-not $ids.ContainsKey($id)) { throw "$id lacks a resource.h definition." }
}
foreach ($id in $ids.Keys) {
    if (-not $base.ContainsKey($id)) { throw "$id lacks a base string." }
}
foreach ($file in Get-ChildItem -LiteralPath $root -File | Where-Object { $_.Extension -in '.cpp','.h' -and $_.Name -cne 'resource.h' }) {
    $text = [IO.File]::ReadAllText($file.FullName, $encoding)
    foreach ($match in [regex]::Matches($text, '\bIDS_[A-Z0-9_]+\b')) {
        if (-not $base.ContainsKey($match.Value)) { throw "$($file.Name) uses an undefined base string $($match.Value)." }
    }
}
$languages = Join-Path $root 'Lang'
if (Test-Path -LiteralPath $languages -PathType Container) {
    foreach ($file in Get-ChildItem -LiteralPath $languages -Recurse -Filter '*.rc' -File) {
        $translation = Read-StringTable $file.FullName
        foreach ($id in $translation.Keys) {
            if (-not $base.ContainsKey($id) -or $translation[$id] -cne $base[$id]) {
                throw "$($file.Name) has an unknown string or changed placeholder signature for $id."
            }
        }
    }
}
Write-Host "PASS RedXe resource strings ($($base.Count) base entries)."
