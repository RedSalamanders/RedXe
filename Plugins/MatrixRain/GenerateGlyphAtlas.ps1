<#!
.SYNOPSIS
Regenerates the original RedXe 64-glyph signed-distance-field atlas header.
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$glyphCount = 64
$cellSize = 16
$atlasColumns = 8
$atlasSize = $cellSize * $atlasColumns
$atlas = [byte[]]::new($atlasSize * $atlasSize)

function Test-GlyphPixel {
    param([int] $Glyph, [int] $X, [int] $Y)

    $top = (($Glyph -band 0x01) -ne 0) -and $Y -eq 1 -and $X -ge 1 -and $X -le 6
    $middle = (($Glyph -band 0x02) -ne 0) -and ($Y -eq 3 -or $Y -eq 4) -and $X -ge 1 -and $X -le 6
    $bottom = (($Glyph -band 0x04) -ne 0) -and $Y -eq 6 -and $X -ge 1 -and $X -le 6
    $left = (($Glyph -band 0x08) -ne 0) -and $X -eq 1 -and $Y -ge 1 -and $Y -le 6
    $right = (($Glyph -band 0x10) -ne 0) -and $X -eq 6 -and $Y -ge 1 -and $Y -le 6
    $forward = (($Glyph -band 0x20) -ne 0) -and ($X + $Y -eq 7 -or $X + $Y -eq 8) -and $X -ge 1 -and $X -le 6
    $backward = (($Glyph * 13 + 7) -band 0x08) -ne 0 -and ($X -eq $Y -or $X + 1 -eq $Y) -and $X -ge 1 -and $X -le 6
    $spine = (($Glyph * 29 + 3) -band 0x10) -ne 0 -and ($X -eq 3 -or $X -eq 4) -and $Y -ge 1 -and $Y -le 6
    return $top -or $middle -or $bottom -or $left -or $right -or $forward -or $backward -or $spine
}

for ($glyph = 0; $glyph -lt $glyphCount; ++$glyph) {
    $inside = [bool[]]::new($cellSize * $cellSize)
    for ($y = 0; $y -lt $cellSize; ++$y) {
        for ($x = 0; $x -lt $cellSize; ++$x) {
            $inside[$y * $cellSize + $x] = Test-GlyphPixel -Glyph ($glyph + 1) `
                -X ([int][Math]::Floor($x / 2)) -Y ([int][Math]::Floor($y / 2))
        }
    }

    $glyphColumn = $glyph % $atlasColumns
    $glyphRow = [int][Math]::Floor($glyph / $atlasColumns)
    for ($y = 0; $y -lt $cellSize; ++$y) {
        for ($x = 0; $x -lt $cellSize; ++$x) {
            $isInside = $inside[$y * $cellSize + $x]
            $minimumSquared = 512.0
            for ($otherY = 0; $otherY -lt $cellSize; ++$otherY) {
                for ($otherX = 0; $otherX -lt $cellSize; ++$otherX) {
                    if ($inside[$otherY * $cellSize + $otherX] -eq $isInside) {
                        continue
                    }
                    $deltaX = $x - $otherX
                    $deltaY = $y - $otherY
                    $squared = [double]($deltaX * $deltaX + $deltaY * $deltaY)
                    if ($squared -lt $minimumSquared) {
                        $minimumSquared = $squared
                    }
                }
            }
            $distance = [Math]::Sqrt($minimumSquared)
            $signed = if ($isInside) { $distance } else { -$distance }
            $encoded = [Math]::Clamp([int][Math]::Round(128.0 + $signed * 28.0), 0, 255)
            $atlasX = $glyphColumn * $cellSize + $x
            $atlasY = $glyphRow * $cellSize + $y
            $atlas[$atlasY * $atlasSize + $atlasX] = [byte]$encoded
        }
    }
}

$builder = [Text.StringBuilder]::new()
[void]$builder.AppendLine('#pragma once')
[void]$builder.AppendLine()
[void]$builder.AppendLine('#include <array>')
[void]$builder.AppendLine('#include <cstdint>')
[void]$builder.AppendLine()
[void]$builder.AppendLine('inline constexpr uint32_t kMatrixRainGlyphAtlasSize = 128;')
[void]$builder.AppendLine('inline constexpr uint32_t kMatrixRainGlyphCount = 64;')
[void]$builder.AppendLine('// clang-format off')
[void]$builder.AppendLine('inline constexpr std::array<std::uint8_t, 16384> kMatrixRainGlyphAtlas{')
for ($index = 0; $index -lt $atlas.Length; ++$index) {
    if (($index % 16) -eq 0) {
        [void]$builder.Append('    ')
    }
    [void]$builder.Append(('0x{0:X2}' -f $atlas[$index]))
    if ($index + 1 -ne $atlas.Length) {
        [void]$builder.Append(',')
    }
    if (($index % 16) -eq 15) {
        [void]$builder.AppendLine()
    }
    else {
        [void]$builder.Append(' ')
    }
}
[void]$builder.AppendLine('};')
[void]$builder.AppendLine('// clang-format on')

$output = Join-Path $PSScriptRoot 'MatrixRainGlyphAtlas.h'
[IO.File]::WriteAllText($output, $builder.ToString(), [Text.UTF8Encoding]::new($false))
Write-Host "Generated $output ($($atlas.Length) bytes)." -ForegroundColor Green
