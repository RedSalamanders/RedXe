# Launcher

Plugin id: `builtin.launcher`

## Screenshot

![Launcher](../screenshots/launcher.png)

## What it does

A grid of up to 32 shortcuts. Click an icon to launch it with the Windows default verb (no confirmation UI), or to run any other [action](../actions.md) the shortcut names. Drag a file or a full URL onto the tile to append a shortcut, up to 32.

If there are too many shortcuts for the chosen icon size, Launcher splits them across internal pages. A row of small dots appears at the **bottom of the launcher tile**, not at the dashboard edge. The default `iconSize`, `"automatic"`, balances icon size against page count: it takes the largest icons that put every shortcut on one page, and pays for a second page only when the icons on it are at least twice as big (a third page three times), so eight shortcuts on a wide tile are one page of near-jumbo icons rather than four pages of two, and nine on a narrow strip two pages of six and three rather than nine pages of one. Icons never go below 72 DIP. `"huge"` (192 DIP), `"large"` (144 DIP), `"medium"` (96 DIP), and `"small"` (72 DIP) keep a fixed icon size on a regular grid and send extra shortcuts to the next page. When the tile is taller or wider than the icons need, leftover space becomes even gaps around and between icons instead of a tight cluster in the middle, with at least 8 DIP between icon edges. Swipe horizontally with one finger and the icons follow your finger, then ease to the next page — or tap a dot to slide there. With a mouse, one wheel notch (roll down or tilt right for the next page, up or left for the previous) slides one launcher page. While the launcher shows page dots the wheel stays with it, even at its first or last page; point at a tile without page dots, or use the edge chevrons, to change the RedXe page. The dots stay at the bottom. Two or three fingers still change RedXe dashboard pages.

An empty `shortcuts` list imports up to 32 current-user **taskbar pins** the first time the widget becomes visible, then saves them as this instance's list. After that, the saved list is authoritative and is not reimported. An empty or missing pin folder does not write settings.

Clicking padding (not an icon) can still raise the widget. Icon clicks launch and do not raise. Double-click or double-tap empty space raises it to half the window.

## Parameters

Default is `{ "shortcuts": [], "iconSize": "automatic" }`. Unknown keys, an empty `target` for a launch, an unknown `action`, a non-launch action without an `icon`, duplicate shortcuts, unknown `iconSize` values, or more than 32 items reject the document. A launch `target` that is not an absolute path or a URI is accepted but shows a warning tile and does nothing when clicked.

| Parameter | Type | Limits | Meaning |
| --- | --- | --- | --- |
| `iconSize` | string | `automatic` (default), `small`, `medium`, `large`, or `huge` | Balance icon size against page count (largest icons on one page; another page only for icons twice the size), or uniform 72 / 96 / 144 / 192 DIP icon edges on a regular grid that page when full |
| `shortcuts` | array | 0–32 items | Ordered shortcuts |
| `shortcuts[].action` | string | any [action](../actions.md) | Omitted means `system.launch` |
| `shortcuts[].target` | string | ≤ 512 bytes | For `system.launch`: an absolute Win32 path (`C:\...` or `\\server\share\...`) or a URI with a two-or-more-letter scheme (`https:...`); otherwise the action's argument |
| `shortcuts[].icon` | string | glyph name or `png:<absolute path>` | Optional for a launch (the shell icon is used); required for any other action |

```json
{
  "plugin": "builtin.launcher",
  "shortcuts": [
    { "target": "C:\\Windows\\explorer.exe" },
    { "target": "https://www.corsair.com" },
    { "action": "system.lock", "icon": "Lock" },
    { "action": "keys.media", "target": "play-pause", "icon": "Play" }
  ]
}
```

A tile whose `icon` is a glyph name shows that Segoe Fluent Icons glyph (the same names as [Logicon](logicon.md) keys); `png:C:\...\icon.png` shows that picture. A shortcut whose `target` does not fit its action shows a warning glyph and does nothing when clicked.
