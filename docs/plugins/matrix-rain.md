# Matrix Rain

Plugin id: `builtin.matrix-rain`

## Screenshot

![Matrix Rain](../screenshots/matrix-rain.png)

## What it does

A continuous digital-rain field using RedXe's own glyph set. On the Release first page it already fills the canvas, so double-activate does not raise it. On a smaller gallery tile, double-click or double-tap raises it to the full window.

Only one live Matrix Rain widget is allowed per configured provider.

## Parameters

All members are optional in the file; omitted keys take these defaults. Unknown keys reject the document. Colors are `#RRGGBB`.

| Parameter | Type | Range | Default | Meaning |
| --- | --- | --- | --- | --- |
| `seed` | integer | 0–4294967295 | `1999` | Deterministic column layout and mutation |
| `glyphHeightDips` | integer | 12–48 | `18` | Glyph size in DIPs |
| `densityPercent` | integer | 10–100 | `70` | How many columns are active |
| `speedPercent` | integer | 25–300 | `100` | Fall speed |
| `trailLengthGlyphs` | integer | 6–48 | `18` | Trail length |
| `mutationPerSecond` | integer | 0–30 | `8` | Glyph changes per second |
| `headColor` | string | `#RRGGBB` | `#D8FFE5` | Leading glyph |
| `trailColor` | string | `#RRGGBB` | `#00E65C` | Trail |
| `backgroundColor` | string | `#RRGGBB` | `#010502` | Opaque background |
| `glowPercent` | integer | 0–100 | `35` | Head glow |

```json
{
  "plugin": "builtin.matrix-rain",
  "settings": {
    "seed": 4242,
    "densityPercent": 55,
    "speedPercent": 75,
    "headColor": "#B8E8FF",
    "trailColor": "#2388D1"
  }
}
```
