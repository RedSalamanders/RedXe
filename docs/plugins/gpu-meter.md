# GPU Meter

Plugin id: `builtin.gpu-meter`

## Screenshot

![GPU Meter](../screenshots/gpu-meter.png)

## What it does

Graphics adapters (not NPUs) with utilization, memory, and temperature when Windows publishes them. Missing sensors show muted dashes, not zeros. Extra adapters show as `+N` at the bottom right; swipe or scroll to page them. Double-click or double-tap raises it to a third of the window.

## Parameters

This widget has no parameters. `settings`, if present, must be `{}`.

```json
{ "plugin": "builtin.gpu-meter" }
```
