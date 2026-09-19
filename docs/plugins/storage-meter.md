# Storage Meter

Plugin id: `builtin.storage-meter`

## Screenshot

![Storage Meter](../screenshots/storage-meter.png)

## What it does

Volumes with a drive letter or mount folder, in drive-letter order and named the way Explorer names them ("Data (E:)": the volume label, or "Local Disk", "USB Drive", "CD Drive", "Network Drive" when it has none, then the letter), as cards with used percent, used/total bytes, and an OK / HIGH / FULL band. When volumes do not fit, a row of page dots appears above the disk-activity line; tap a dot, swipe, or scroll to page them. Disk activity is shown when the volume data allows. Double-click or double-tap raises it to a third of the window.

## Parameters

This widget has no parameters. `settings`, if present, must be `{}`.

```json
{ "plugin": "builtin.storage-meter" }
```
