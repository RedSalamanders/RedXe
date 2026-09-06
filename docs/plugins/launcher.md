# Launcher

Plugin id: `builtin.launcher`

## Screenshot

![Launcher](../screenshots/launcher.png)

## What it does

A grid of up to eight shortcuts. Click an icon to launch it with the Windows default verb (no confirmation UI). Drag a file or a full URL onto the tile to append a shortcut, up to eight.

An empty `shortcuts` list imports up to eight current-user **taskbar pins** the first time the widget becomes visible, then saves them as this instance's list. After that, the saved list is authoritative and is not reimported. An empty or missing pin folder does not write settings.

Clicking padding (not an icon) can still raise the widget. Icon clicks launch and do not raise. Double-click or double-tap empty space raises it to half the window.

## Parameters

Default is `{ "shortcuts": [] }`. Unknown keys, relative paths, empty targets, duplicate targets, or more than eight items reject the document.

| Parameter | Type | Limits | Meaning |
| --- | --- | --- | --- |
| `shortcuts` | array | 0–8 items | Ordered shortcuts |
| `shortcuts[].target` | string | 1–512 bytes | Absolute Win32 path (`C:\...` or `\\server\share\...`) or a URI with a two-or-more-letter scheme (`https:...`) |
| `shortcuts[].iconPng` | string | 0–260 bytes | Optional absolute PNG icon path |

```json
{
  "plugin": "builtin.launcher",
  "settings": {
    "shortcuts": [
      { "target": "C:\\Windows\\explorer.exe" },
      { "target": "https://www.corsair.com" }
    ]
  }
}
```
