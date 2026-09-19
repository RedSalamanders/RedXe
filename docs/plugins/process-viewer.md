# Process Viewer

Plugin id: `builtin.process-viewer`

## Screenshot

![Process Viewer](../screenshots/process-viewer.png)

## What it does

Ranks running processes by available CPU, with working set and PID as tie-breakers. PID 0, the **System Idle Process**, is left out by default: its CPU figure is the share of the machine doing nothing, so a high number there is good news. Set `hideIdle` to `false` to list it the way Task Manager does; it then reads in the calm green whatever its percent, never the amber or red of a busy process. Wide tiles split into two columns when the names and stats still fit. If more processes exist than fit, a row of page dots appears at the bottom; tap a dot, swipe, or scroll to see the rest (one wheel notch is one page, and the wheel stays with the tile while it shows page dots). Double-click or double-tap raises it to half the window.

It does not collect command lines, full paths, or user names. Inaccessible values show as muted dashes.

## Parameters

| Parameter | Type | Range | Default | Meaning |
| --- | --- | --- | --- | --- |
| `topN` | integer | 1–32 | `10` | How many rows to keep |
| `hideIdle` | boolean | | `true` | Leave the System Idle Process (PID 0) out of the list |

```json
{ "plugin": "builtin.process-viewer", "topN": 10, "hideIdle": true }
```
