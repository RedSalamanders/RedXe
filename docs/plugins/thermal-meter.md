# Thermal Meter

Plugin id: `builtin.thermal-meter`

## Screenshot

![Thermal Meter](../screenshots/thermal-meter.png)

## What it does

Temperature and fan cards. Labels are COOL / OK / WARM / HOT from 25 / 70 / 85 °C. Numerals include `°C`. When sensors do not fit, a row of page dots appears at the bottom; tap a dot, swipe, or scroll to page them (a very small tile shows one sensor and counts the rest as `+N`). Double-click or double-tap raises it to half the window.

## Parameters

This widget has no parameters. `settings`, if present, must be `{}`.

```json
{ "plugin": "builtin.thermal-meter" }
```
