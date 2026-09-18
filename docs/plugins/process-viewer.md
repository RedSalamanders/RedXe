# Process Viewer

Plugin id: `builtin.process-viewer`

## Screenshot

![Process Viewer](../screenshots/process-viewer.png)

## What it does

Ranks running processes by available CPU, with working set and PID as tie-breakers. PID 0 is shown as **System Idle Process**, matching Task Manager. Wide tiles split into two columns when the names and stats still fit. If more processes exist than fit, a row of page dots appears at the bottom; tap a dot, swipe, or scroll to see the rest (one wheel notch is one page, and the wheel stays with the tile while it shows page dots). Double-click or double-tap raises it to half the window.

It does not collect command lines, full paths, or user names. Inaccessible values show as muted dashes.

## Parameters

| Parameter | Type | Range | Default | Meaning |
| --- | --- | --- | --- | --- |
| `topN` | integer | 1–32 | `10` | How many rows to keep |

```json
{ "plugin": "builtin.process-viewer", "topN": 10 }
```
