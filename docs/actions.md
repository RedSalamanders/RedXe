# Actions

Anything you bind to a physical control or a launcher tile is an **action**: one closed shape everywhere.

```jsonc
{ "action": "system.launch", "target": "C:\\Tools\\Code.exe" }
```

| Member | Meaning |
| --- | --- |
| `action` | `<namespace>.<verb>` such as `page.next`, `keys.media`, or `zoom.open`. The namespaces below are the only ones that exist; an unknown one rejects the settings file. |
| `target` | The action's argument, at most 512 bytes. Its grammar depends on the action (table below). A target that does not fit its action is not an error: the key shows a red `!` (Logicon) or a warning tile (Launcher) and the press does nothing. |

Where bindings live: Logicon `keys[]`, `dialpad.buttons[]`, and `dialpad.turns[]` ([Logicon](plugins/logicon.md)); Launcher
`shortcuts[]` ([Launcher](plugins/launcher.md)).

Actions that could cost you something (`system.shutdown`, `system.restart`, …) need a confirming `target` such as `"now"`; there
is never a dialog, because a keypad key is a deliberate control.

## Built into RedXe

### `page.*` and `widget.*`

| `action` | `target` | Effect |
| --- | --- | --- |
| `page.next`, `page.previous` | none | Slide to the next / previous dashboard page |
| `page.first`, `page.last` | none | Jump to the first / last page |
| `page.goto` | a page `id`, or a 0-based page index | Jump to that page |
| `widget.raise` | `"<ordinal>"` or `"<pageId>/<ordinal>"` (0-based position on the current page) | Raise that widget |
| `widget.toggle` | same | Raise it, or close it when it is the one raised |
| `widget.dismiss` | none | Close the raised widget |
| `widget.next`, `widget.previous` | none | Raise the next / previous widget in page order |

On an auto-hiding [dock](usage.md#dock) that is collapsed, these actions bring the bar out first so you see the page change or the raised widget; it collapses again shortly afterwards unless a raised widget keeps it open.

### `redxe.*`

| `action` | `target` | Effect |
| --- | --- | --- |
| `redxe.settings.reload` | none | Re-read the settings file now |
| `redxe.settings.edit` | none | Open the settings file in its default editor |
| `redxe.logs.open` | none | Open the `Logs` folder |
| `redxe.screenshot` | `<png path>[@<pageId>[/<ordinal>]]` | Save a screenshot of a page (or one widget) like `--screenshot`, without its delay; RedXe keeps running. A press while a capture is still in progress, or a page or widget that does not exist, is refused and captures nothing; a failed capture is noted in the log as `screenshot-failed` |
| `redxe.quit` | `now` | Quit RedXe |
| `redxe.dock.show` | none | Reveal an auto-hiding [dock](usage.md#dock); it stays until you visit it with the mouse and leave, or click it and click elsewhere |
| `redxe.dock.hide` | none | Collapse the dock to its peek strip (does nothing while a widget is raised or being dragged) |
| `redxe.dock.toggle` | none | Reveal the dock when it is collapsed, otherwise collapse it |

### `system.*`

| `action` | `target` | Effect |
| --- | --- | --- |
| `system.launch`, `system.open` | `C:\...`, `\\server\share\...`, or `https://...` | Open it with the shell; a file starts in its own folder |
| `system.run` | `"C:\path\tool.exe" arguments` | Start a process directly, without a shell |
| `system.lock` | none | Lock the workstation |
| `system.sleep`, `system.hibernate` | `now` | Sleep / hibernate |
| `system.logoff` | `now` | Sign out |
| `system.shutdown`, `system.restart` | `now`, or seconds `0`–`3600` | Shut down / restart, with the Windows countdown when delayed |
| `system.shutdown.cancel` | none | Cancel a delayed shutdown |
| `system.process.close` | `foreground`, `exe:<name.exe>`, `class:<class>`, or `title:<text>` | Ask that application to close (never kills it) |
| `system.power.plan` | `balanced`, `highPerformance`, `powerSaver`, or a plan GUID | Switch the active power plan |
| `system.theme` | `light`, `dark`, or `toggle` | Switch Windows between light and dark |
| `system.taskManager` | none | Open Task Manager |

### `keys.*`

| `action` | `target` | Effect |
| --- | --- | --- |
| `keys.press` | one or more chords: `Ctrl+Shift+Esc`, `Win+D`, `Ctrl+K,Ctrl+S` | Press and release them in order |
| `keys.down`, `keys.up` | one chord | Hold / release it (anything still held is released after 2 s, and its later `up` then does nothing). Not on a Logicon control: `keys.down` there shows a red `!` and does nothing ([why](#when-something-is-wrong)) |
| `keys.type` | text | Type it |
| `keys.media` | `play-pause`, `stop`, `next-track`, `previous-track`, `volume-up`, `volume-down`, `mute` | Send that media key |
| `keys.lock` | `caps`, `num`, `scroll` | Toggle that lock key |
| `keys.layout` | `en-US`, `fr-FR`, … or an 8-digit keyboard layout id | Switch the foreground window's keyboard layout |

Chord modifiers are `Ctrl`, `Shift`, `Alt`, `Win`; keys are letters, digits, `F1`–`F24`, `Enter`, `Esc`, `Tab`, `Space`, `Backspace`,
`Delete`, `Insert`, `Home`, `End`, `PageUp`, `PageDown`, `Left`, `Right`, `Up`, `Down`, `PrintScreen`, `Pause`, `CapsLock`, `NumLock`,
`ScrollLock`, `Apps`, `Num0`–`Num9`, `NumAdd`, `NumSub`, `NumMul`, `NumDiv`, `NumDot`, `Plus`, `Minus`, `Comma`, `Period`,
`Semicolon`, `Quote`, `Slash`, `Backslash`, `LBracket`, `RBracket`, `Grave`, or `VK:<hex>`. Windows does not deliver injected
input to a window that runs elevated (as administrator); RedXe never runs elevated, so such a window ignores these actions.
While a UAC prompt or the lock screen is up, Windows refuses injected input altogether: a held key or button that cannot be
released then is retried every quarter second for about 10 s, so a modifier is not left pressed after the prompt closes.

### `mouse.*`

| `action` | `target` | Effect |
| --- | --- | --- |
| `mouse.move` | `x,y` (screen pixels), `+dx,+dy` (relative), or `center`, each optionally `@<monitor>` | Move the pointer |
| `mouse.click`, `mouse.doubleClick` | `left`, `right`, `middle`, `x1`, `x2` | Click where the pointer is |
| `mouse.down`, `mouse.up` | same | Hold / release a button (released after 2 s like a held chord). Not on a Logicon control: `mouse.down` there shows a red `!` and does nothing ([why](#when-something-is-wrong)) |
| `mouse.scroll`, `mouse.scroll.horizontal` | `+n` or `-n` notches | Scroll |
| `mouse.speed` | `1`–`20` | Pointer speed |

A `@<monitor>` is `primary`, `secondary` (your second screen: the first monitor that is not the primary, skipping a XENEON unless it is the only other one), `xeneon` (the monitor RedXe sits on), a 1-based monitor number, or `name:<part of the device name>`.

## Published by plugins

### `logicon.*` — the Logicon service

| `action` | `target` | Effect |
| --- | --- | --- |
| `logicon.keyPage.next`, `logicon.keyPage.previous` | none | Switch to the next / previous key page |
| `logicon.keyPage.goto` | `0`–`3` | Select a key page |
| `logicon.brightness` | `1`–`100`, `+n`, or `-n` | Keypad brightness until the next settings change |

### `zoom.*` — the Zoom actions

See [Zoom](plugins/zoom.md). These actions open the Zoom web app in the default browser; RedXe needs no `services`
entry, Zoom Workplace installation, or Marketplace application registration.

| `action` | `target` | Effect |
| --- | --- | --- |
| `zoom.open` | none | Open the Zoom web join page |
| `zoom.join` | an HTTPS `zoom.us` or `*.zoom.us` meeting link: an invite (`/j/<meeting id>`) or a browser-join link (`/wc/<meeting id>/join`, `/wc/join/<meeting id>`) | Open that link in the browser, preserving its passcode |

## When something is wrong

- A misspelled or unknown `action` rejects the settings file; the message names the entry.
- A `target` that does not fit is shown on the control (red `!` / warning tile) and never runs.
- Launches (`system.launch`, `system.open`, `system.run`, `system.taskManager`, and the Zoom actions) start in the
  background, so the dashboard never freezes, even for a file on a share that does not answer. A launch whose file is
  missing, has no app for it, or cannot be reached does nothing visible; the log (`redxe.logs.open` opens its folder)
  records `launch-failed`.
- A key or mouse action that waited more than a second because RedXe was busy is skipped rather than typed into
  whatever window you switched to meanwhile; the log records `action-expired`.
- `keys.down` and `mouse.down` hold a key or a button until a release, and a Logicon key, dialpad button, or turn only
  ever sends a press. Bound there, they keep the settings file valid but show a red `!` (the dialpad has no face) and
  never run; the log names the control. Use `keys.press` or `mouse.click` instead. A Launcher tile still accepts
  them: each tap holds the key or button until a tile bound to `keys.up` / `mouse.up` with the same chord or button,
  another hold, or the 2 s release (an `up` naming a different chord or button sends its own release and leaves the
  hold in place).
- If two plugin DLLs beside `RedXe.exe` claim the same action namespace, or one that RedXe does not know, a notice
  names both files and the affected bindings are disabled until the deployment is repaired. The dashboard keeps
  running.
