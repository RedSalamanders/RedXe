<#!
.SYNOPSIS
Regenerates the original RedXe 64-glyph signed-distance-field atlas header.

.DESCRIPTION
Each glyph is a small set of vector polylines (round-capped strokes) drawn in a 1 x 2 design cell and rasterized as
an exact signed distance field into a 24 x 36 texel cell, 8 x 8 cells in a 192 x 288 R8_UNORM atlas. The set is the
one the film's code rain is remembered for: 46 half-width-katakana-inspired shapes drawn mirrored, the ten digits,
and a few Latin letters and symbols. Every shape is an original stroke drawing, not font data. Vector strokes keep
diagonals straight and corners crisp at any glyph size; the pixel shader thresholds the field with a screen-space
width, so the glyphs stay smooth from a 12-DIP tile glyph to a raised 48-DIP one.
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$cellWidth = 24
$cellHeight = 36
$atlasColumns = 8
$atlasRows = 8
$atlasWidth = $cellWidth * $atlasColumns
$atlasHeight = $cellHeight * $atlasRows
$unitPixels = [double]$cellWidth          # design unit: the cell is 1 x 1.5 units (drawings are authored 1 x 2 and squeezed)
$designHeightScale = 0.75                 # authored y (0..2) to cell y (0..1.5)
$strokeRadius = 0.13                      # units; 3.1 px in the atlas, about 4.7 px at a 27 px glyph: the film's bold strokes
$encodeScale = 28.0                       # 255 / encodeScale px of spread around the edge (about +-4.5 px)
$atlas = [byte[]]::new($atlasWidth * $atlasHeight)

# Each glyph: a list of polylines, each a flat list of x,y pairs in design units (x 0..1, y 0..2). A single point is
# a dot. `mirror` flips the drawing horizontally, the way the film shows its katakana.
$glyphs = @(
    # 46 katakana-inspired shapes (a i u e o, ka ki ku ke ko, ... wa n), mirrored.
    @{ mirror = $true; strokes = @(@(0.15, 0.38, 0.85, 0.38, 0.66, 0.95), @(0.62, 0.55, 0.20, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.80, 0.25, 0.20, 1.02), @(0.50, 0.75, 0.50, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.50, 0.20, 0.50, 0.48), @(0.15, 0.48, 0.85, 0.48, 0.85, 1.15, 0.30, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.22, 0.38, 0.78, 0.38), @(0.50, 0.38, 0.50, 1.66), @(0.15, 1.66, 0.85, 1.66)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.62, 0.85, 0.62), @(0.60, 0.22, 0.60, 1.78), @(0.60, 0.62, 0.15, 1.50)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.62, 0.80, 0.62, 0.80, 1.50, 0.62, 1.78), @(0.42, 0.22, 0.42, 1.00, 0.15, 1.62)) }
    @{ mirror = $true; strokes = @(@(0.18, 0.62, 0.85, 0.46), @(0.15, 1.16, 0.88, 1.00), @(0.45, 0.22, 0.60, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.36, 0.22, 0.20, 0.78), @(0.36, 0.22, 0.80, 0.22, 0.80, 0.92, 0.25, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.30, 0.22, 0.15, 0.82), @(0.30, 0.46, 0.85, 0.46), @(0.60, 0.46, 0.60, 1.02, 0.20, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.42, 0.85, 0.42, 0.85, 1.60, 0.15, 1.60)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.62, 0.85, 0.62), @(0.32, 0.25, 0.32, 1.10), @(0.68, 0.25, 0.68, 1.10, 0.20, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.20, 0.36, 0.40, 0.60), @(0.20, 0.86, 0.40, 1.10), @(0.15, 1.70, 0.85, 0.76)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.42, 0.85, 0.42, 0.20, 1.78), @(0.50, 1.10, 0.85, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.76, 0.80, 0.56, 0.80, 0.92, 0.60, 1.06), @(0.35, 0.22, 0.35, 1.60, 0.85, 1.60)) }
    @{ mirror = $true; strokes = @(@(0.25, 0.36, 0.40, 0.76), @(0.85, 0.36, 0.20, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.36, 0.22, 0.20, 0.80), @(0.36, 0.22, 0.80, 0.22, 0.80, 0.92, 0.25, 1.78), @(0.36, 0.92, 0.64, 1.26)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.50, 0.75, 0.36), @(0.15, 1.00, 0.85, 1.00), @(0.50, 0.36, 0.50, 1.20, 0.25, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.20, 0.36, 0.30, 0.72), @(0.50, 0.30, 0.60, 0.66), @(0.85, 0.36, 0.25, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.25, 0.36, 0.75, 0.36), @(0.15, 0.76, 0.85, 0.76), @(0.50, 0.76, 0.50, 1.20, 0.25, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.40, 0.22, 0.40, 1.78), @(0.40, 0.82, 0.80, 1.30)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.66, 0.85, 0.66), @(0.55, 0.22, 0.55, 1.10, 0.20, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.25, 0.56, 0.75, 0.56), @(0.15, 1.56, 0.85, 1.56)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.42, 0.85, 0.42, 0.20, 1.78), @(0.35, 1.06, 0.75, 1.50)) }
    @{ mirror = $true; strokes = @(@(0.50, 0.22, 0.50, 0.50), @(0.20, 0.50, 0.80, 0.50, 0.20, 1.30, 0.85, 1.30), @(0.50, 0.90, 0.50, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.80, 0.22, 0.20, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.40, 0.40, 0.15, 1.60), @(0.55, 0.40, 0.85, 1.60)) }
    @{ mirror = $true; strokes = @(@(0.25, 0.22, 0.25, 1.60, 0.85, 1.60), @(0.25, 0.86, 0.80, 0.56)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.42, 0.85, 0.42, 0.85, 0.92, 0.30, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.92, 0.40, 0.50, 0.85, 1.50)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.56, 0.85, 0.56), @(0.50, 0.22, 0.50, 1.78), @(0.30, 1.00, 0.20, 1.50), @(0.70, 1.00, 0.80, 1.50)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.46, 0.85, 0.46, 0.35, 1.50), @(0.40, 1.00, 0.75, 1.40)) }
    @{ mirror = $true; strokes = @(@(0.20, 0.40, 0.80, 0.60), @(0.20, 0.90, 0.80, 1.10), @(0.20, 1.40, 0.80, 1.60)) }
    @{ mirror = $true; strokes = @(@(0.55, 0.22, 0.20, 1.60, 0.85, 1.60), @(0.60, 1.20, 0.85, 1.60)) }
    @{ mirror = $true; strokes = @(@(0.80, 0.30, 0.20, 1.78), @(0.25, 0.90, 0.85, 1.50)) }
    @{ mirror = $true; strokes = @(@(0.20, 0.46, 0.80, 0.46), @(0.15, 0.96, 0.85, 0.96), @(0.45, 0.22, 0.45, 1.56, 0.85, 1.56)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.62, 0.75, 0.40, 0.55, 1.00), @(0.40, 0.22, 0.55, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.20, 0.46, 0.75, 0.46, 0.75, 1.50), @(0.15, 1.50, 0.85, 1.50)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.42, 0.85, 0.42, 0.85, 1.60, 0.15, 1.60), @(0.25, 1.00, 0.85, 1.00)) }
    @{ mirror = $true; strokes = @(@(0.25, 0.36, 0.75, 0.36), @(0.15, 0.80, 0.85, 0.80, 0.85, 1.20, 0.30, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.30, 0.25, 0.30, 1.20), @(0.70, 0.25, 0.70, 1.20, 0.25, 1.78)) }
    @{ mirror = $true; strokes = @(@(0.35, 0.30, 0.30, 1.20, 0.15, 1.60), @(0.65, 0.30, 0.65, 1.50, 0.85, 1.30)) }
    @{ mirror = $true; strokes = @(@(0.30, 0.22, 0.30, 1.60, 0.85, 1.10)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.42, 0.85, 0.42, 0.85, 1.60, 0.15, 1.60, 0.15, 0.42)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.42, 0.85, 0.42, 0.85, 1.00, 0.50, 1.78), @(0.15, 0.42, 0.15, 1.00)) }
    @{ mirror = $true; strokes = @(@(0.20, 0.40, 0.40, 0.70), @(0.15, 1.70, 0.85, 0.76)) }
    @{ mirror = $true; strokes = @(@(0.15, 0.46, 0.85, 0.46), @(0.50, 0.46, 0.50, 1.10, 0.15, 1.78), @(0.50, 1.10, 0.85, 1.78)) }
    # Ten digits.
    @{ mirror = $false; strokes = @(@(0.20, 0.30, 0.80, 0.30, 0.80, 1.70, 0.20, 1.70, 0.20, 0.30)) }
    @{ mirror = $false; strokes = @(@(0.30, 0.50, 0.50, 0.30, 0.50, 1.70), @(0.25, 1.70, 0.75, 1.70)) }
    @{ mirror = $false; strokes = @(@(0.20, 0.30, 0.80, 0.30, 0.80, 1.00, 0.20, 1.00, 0.20, 1.70, 0.80, 1.70)) }
    @{ mirror = $false; strokes = @(@(0.20, 0.30, 0.80, 0.30, 0.80, 1.70, 0.20, 1.70), @(0.35, 1.00, 0.80, 1.00)) }
    @{ mirror = $false; strokes = @(@(0.20, 0.30, 0.20, 1.00, 0.80, 1.00), @(0.80, 0.30, 0.80, 1.70)) }
    @{ mirror = $false; strokes = @(@(0.80, 0.30, 0.20, 0.30, 0.20, 1.00, 0.80, 1.00, 0.80, 1.70, 0.20, 1.70)) }
    @{ mirror = $false; strokes = @(@(0.80, 0.30, 0.20, 0.30, 0.20, 1.70, 0.80, 1.70, 0.80, 1.00, 0.20, 1.00)) }
    @{ mirror = $false; strokes = @(@(0.20, 0.30, 0.80, 0.30, 0.40, 1.70)) }
    @{ mirror = $false; strokes = @(@(0.20, 0.30, 0.80, 0.30, 0.80, 1.70, 0.20, 1.70, 0.20, 0.30), @(0.20, 1.00, 0.80, 1.00)) }
    @{ mirror = $false; strokes = @(@(0.80, 1.00, 0.20, 1.00, 0.20, 0.30, 0.80, 0.30, 0.80, 1.70, 0.20, 1.70)) }
    # Latin letters and symbols the film's rain also shows.
    @{ mirror = $true; strokes = @(@(0.20, 0.30, 0.80, 0.30, 0.20, 1.70, 0.80, 1.70)) }
    @{ mirror = $false; strokes = @(@(0.15, 0.30, 0.85, 0.30), @(0.50, 0.30, 0.50, 1.70)) }
    @{ mirror = $true; strokes = @(@(0.80, 0.30, 0.20, 0.30, 0.20, 1.70, 0.80, 1.70), @(0.20, 1.00, 0.65, 1.00)) }
    @{ mirror = $false; strokes = @(@(0.20, 0.30, 0.80, 1.70), @(0.80, 0.30, 0.20, 1.70)) }
    @{ mirror = $false; strokes = @(@(0.20, 0.80, 0.80, 0.80), @(0.20, 1.20, 0.80, 1.20)) }
    @{ mirror = $false; strokes = @(@(0.50, 0.72), @(0.50, 1.28)) }
    @{ mirror = $false; strokes = @(@(0.50, 0.60, 0.50, 1.40), @(0.18, 0.80, 0.82, 1.20), @(0.82, 0.80, 0.18, 1.20)) }
    @{ mirror = $false; strokes = @(@(0.25, 1.00, 0.75, 1.00)) }
)
if ($glyphs.Count -ne 64) {
    throw "The atlas needs exactly 64 glyphs; the table has $($glyphs.Count)."
}

function Get-SegmentDistance {
    param([double] $X, [double] $Y, [double] $Ax, [double] $Ay, [double] $Bx, [double] $By)
    $abx = $Bx - $Ax; $aby = $By - $Ay
    $apx = $X - $Ax; $apy = $Y - $Ay
    $lengthSquared = $abx * $abx + $aby * $aby
    $t = if ($lengthSquared -gt 0) { [Math]::Clamp(($apx * $abx + $apy * $aby) / $lengthSquared, 0.0, 1.0) } else { 0.0 }
    $dx = $apx - $t * $abx; $dy = $apy - $t * $aby
    return [Math]::Sqrt($dx * $dx + $dy * $dy)
}

for ($glyph = 0; $glyph -lt $glyphs.Count; ++$glyph) {
    $definition = $glyphs[$glyph]
    $mirror = [bool]$definition.mirror
    # Flatten the polylines into segments once per glyph. PowerShell unwraps @(@(...)) to its inner list, so a
    # glyph with a single polyline arrives as a flat number list and is wrapped again here.
    $polylines = $definition.strokes
    if ($polylines.Count -gt 0 -and -not ($polylines[0] -is [array])) {
        $polylines = @(, $polylines)
    }
    $segments = [System.Collections.Generic.List[double[]]]::new()
    foreach ($polyline in $polylines) {
        $points = [double[]]$polyline
        if ($points.Length % 2 -ne 0) { throw "Glyph $glyph has an odd coordinate list." }
        $pointCount = $points.Length / 2
        for ($index = 0; $index -lt [Math]::Max(1, $pointCount - 1); ++$index) {
            $ax = $points[2 * $index]; $ay = $points[2 * $index + 1] * $designHeightScale
            $bx = if ($pointCount -gt 1) { $points[2 * $index + 2] } else { $ax }
            $by = if ($pointCount -gt 1) { $points[2 * $index + 3] * $designHeightScale } else { $ay }
            if ($mirror) { $ax = 1.0 - $ax; $bx = 1.0 - $bx }
            $segments.Add([double[]]@($ax, $ay, $bx, $by))
        }
    }
    $glyphColumn = $glyph % $atlasColumns
    $glyphRow = [int][Math]::Floor($glyph / $atlasColumns)
    for ($y = 0; $y -lt $cellHeight; ++$y) {
        for ($x = 0; $x -lt $cellWidth; ++$x) {
            $unitX = ($x + 0.5) / $unitPixels
            $unitY = ($y + 0.5) / $unitPixels
            $nearest = 1e9
            foreach ($segment in $segments) {
                $distance = Get-SegmentDistance -X $unitX -Y $unitY -Ax $segment[0] -Ay $segment[1] -Bx $segment[2] -By $segment[3]
                if ($distance -lt $nearest) { $nearest = $distance }
            }
            # Positive inside the stroke union, in atlas pixels.
            $signed = ($strokeRadius - $nearest) * $unitPixels
            $encoded = [Math]::Clamp([int][Math]::Round(128.0 + $signed * $encodeScale), 0, 255)
            $atlasX = $glyphColumn * $cellWidth + $x
            $atlasY = $glyphRow * $cellHeight + $y
            $atlas[$atlasY * $atlasWidth + $atlasX] = [byte]$encoded
        }
    }
}

$builder = [Text.StringBuilder]::new()
[void]$builder.AppendLine('#pragma once')
[void]$builder.AppendLine()
[void]$builder.AppendLine('#include <array>')
[void]$builder.AppendLine('#include <cstdint>')
[void]$builder.AppendLine()
[void]$builder.AppendLine('// Generated by GenerateGlyphAtlas.ps1: 64 original stroke glyphs (46 mirrored katakana-inspired shapes, ten digits,')
[void]$builder.AppendLine('// eight Latin letters and symbols) as an exact signed distance field, 24 x 36 texels per glyph in an 8 x 8 grid,')
[void]$builder.AppendLine('// encoded 128 + 28 * distance (positive inside). Do not edit by hand.')
[void]$builder.AppendLine("inline constexpr uint32_t kMatrixRainGlyphAtlasWidth = $atlasWidth;")
[void]$builder.AppendLine("inline constexpr uint32_t kMatrixRainGlyphAtlasHeight = $atlasHeight;")
[void]$builder.AppendLine("inline constexpr uint32_t kMatrixRainGlyphCellWidth = $cellWidth;")
[void]$builder.AppendLine("inline constexpr uint32_t kMatrixRainGlyphCellHeight = $cellHeight;")
[void]$builder.AppendLine("inline constexpr uint32_t kMatrixRainGlyphCount = $($glyphs.Count);")
[void]$builder.AppendLine('// clang-format off')
[void]$builder.AppendLine("inline constexpr std::array<std::uint8_t, $($atlas.Length)> kMatrixRainGlyphAtlas{")
for ($index = 0; $index -lt $atlas.Length; ++$index) {
    if (($index % 24) -eq 0) {
        [void]$builder.Append('    ')
    }
    [void]$builder.Append(('0x{0:X2}' -f $atlas[$index]))
    if ($index + 1 -ne $atlas.Length) {
        [void]$builder.Append(',')
    }
    if (($index % 24) -eq 23) {
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
