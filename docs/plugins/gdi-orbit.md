# GDI Orbit

Plugin id: `builtin.gdi-orbit`

## Screenshot

![GDI Orbit](../screenshots/gdi-orbit.png)

## What it does

A native child-window widget: a double-buffered GDI xenon orbit at 30 FPS while visible. It is the shipped example of a window widget (not Direct3D). Double-click or double-tap raises it to a quarter of the window.

Drops intended for a GPU tile (for example Launcher) are not stolen by this child.

## Parameters

This widget has no parameters. `settings`, if present, must be `{}`.

```json
{ "plugin": "builtin.gdi-orbit" }
```
