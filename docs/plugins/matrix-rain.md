# Matrix Rain

Plugin id: `builtin.matrix-rain`

## Screenshot

![Matrix Rain](../screenshots/matrix-rain.png)

## What it does

A continuous digital-rain field in the style of the film's code: RedXe's own glyph set of mirrored katakana-like shapes, digits, and a few Latin letters and symbols, drawn in the film's bold close-set style and falling as white-headed streams that light the glyphs in place, two streams to a column, a few bright sharp streams in front of a dimmer field, each trail lit evenly and fading only at its end, with an uneven shimmer and the occasional glyph change. On the Release first page it already fills the canvas, so double-activate does not raise it. On a smaller gallery tile, double-click or double-tap raises it to the full window.

Only one live Matrix Rain widget is allowed per configured provider.

## Parameters

All members are optional in the file; omitted keys take these defaults. Unknown keys reject the document. Colors are `#RRGGBB`. The rain falls over the dashboard `backgroundColor`; to change it for this widget only, put `backgroundColor` on the widget object (see [usage](../usage.md#background-color)).

| Parameter | Type | Range | Default | Meaning |
| --- | --- | --- | --- | --- |
| `seed` | integer | 0–4294967295 | `1999` | Deterministic column layout and mutation |
| `glyphHeightDips` | integer | 12–48 | `18` | Glyph size in DIPs |
| `densityPercent` | integer | 10–100 | `70` | How many columns are active |
| `speedPercent` | integer | 25–300 | `100` | Fall speed |
| `trailLengthGlyphs` | integer | 6–48 | `18` | Trail length. In a tile shorter than the trail (a dock bar, a small tile) the trail shortens to two thirds of the visible rows so the bright heads stay on screen |
| `mutationPerSecond` | integer | 0–30 | `8` | How often glyphs change: each glyph re-rolls every `16 / mutationPerSecond` seconds at its own moment, with a brief flash (0 never changes) |
| `headColor` | string | `#RRGGBB` | `#F6FFF6` | Leading glyph |
| `trailColor` | string | `#RRGGBB` | `#33FF33` | Trail |
| `glowPercent` | integer | 0–100 | `35` | Phosphor bloom: the soft glow around lit glyphs and the bright halo of each stream head. `0` turns the bloom passes off |

```json
{
  "plugin": "builtin.matrix-rain",
  "seed": 4242,
  "densityPercent": 55,
  "speedPercent": 75,
  "headColor": "#B8E8FF",
  "trailColor": "#2388D1"
}
```
