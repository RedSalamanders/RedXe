# Actions

Anything you bind to a physical control or a launcher tile is an **action**: one closed shape everywhere.

```jsonc
{ "action": "system.launch", "target": "C:\\Tools\\Code.exe" }
```

| Member | Meaning |
| --- | --- |
| `action` | `<namespace>.<verb>` such as `page.next`, `keys.media`, or `zoom.mute`. The namespaces below are the only ones that exist; an unknown one rejects the settings file. |
| `target` | The action's argument, at most 512 bytes. Its grammar depends on the action (table below). A target that does not fit its action is not an error: the key shows a red `!` (Logicon) or a warning tile (Launcher) and the press does nothing. |

Where bindings live: Logicon `keys[]`, `dialpad.buttons[]`, and `dialpad.turns[]` ([Logicon](plugins/logicon.md)); Launcher
`shortcuts[]` ([Launcher](plugins/launcher.md)).

Actions that could cost you something (`system.shutdown`, `zoom.leave`, …) need a confirming `target` such as `"now"`; there
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

### `redxe.*`

| `action` | `target` | Effect |
| --- | --- | --- |
| `redxe.settings.reload` | none | Re-read the settings file now |
| `redxe.settings.edit` | none | Open the settings file in its default editor |
| `redxe.logs.open` | none | Open the `Logs` folder |
| `redxe.screenshot` | `<png path>[@<pageId>[/<ordinal>]]` | Save a screenshot of a page (or one widget) exactly like `--screenshot` |
| `redxe.quit` | `now` | Quit RedXe |

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
| `keys.down`, `keys.up` | one chord | Hold / release it (anything still held is released after 2 s) |
| `keys.type` | text | Type it |
| `keys.media` | `play-pause`, `stop`, `next-track`, `previous-track`, `volume-up`, `volume-down`, `mute` | Send that media key |
| `keys.lock` | `caps`, `num`, `scroll` | Toggle that lock key |
| `keys.layout` | `en-US`, `fr-FR`, … or an 8-digit keyboard layout id | Switch the foreground window's keyboard layout |

Chord modifiers are `Ctrl`, `Shift`, `Alt`, `Win`; keys are letters, digits, `F1`–`F24`, `Enter`, `Esc`, `Tab`, `Space`, `Backspace`,
`Delete`, `Insert`, `Home`, `End`, `PageUp`, `PageDown`, `Left`, `Right`, `Up`, `Down`, `PrintScreen`, `Pause`, `CapsLock`, `NumLock`,
`ScrollLock`, `Apps`, `Num0`–`Num9`, `NumAdd`, `NumSub`, `NumMul`, `NumDiv`, `NumDot`, `Plus`, `Minus`, `Comma`, `Period`,
`Semicolon`, `Quote`, `Slash`, `Backslash`, `LBracket`, `RBracket`, `Grave`, or `VK:<hex>`. Windows does not deliver injected
input to a window that runs elevated (as administrator); RedXe never runs elevated, so such a window ignores these actions.

### `mouse.*`

| `action` | `target` | Effect |
| --- | --- | --- |
| `mouse.move` | `x,y` (screen pixels), `+dx,+dy` (relative), or `center`, each optionally `@<monitor>` | Move the pointer |
| `mouse.click`, `mouse.doubleClick` | `left`, `right`, `middle`, `x1`, `x2` | Click where the pointer is |
| `mouse.down`, `mouse.up` | same | Hold / release a button |
| `mouse.scroll`, `mouse.scroll.horizontal` | `+n` or `-n` notches | Scroll |
| `mouse.speed` | `1`–`20` | Pointer speed |

A `@<monitor>` is `primary`, `xeneon` (the monitor RedXe sits on), a 1-based monitor number, or `name:<part of the device name>`.

## Published by plugins

### `logicon.*` — the Logicon service

| `action` | `target` | Effect |
| --- | --- | --- |
| `logicon.keyPage.next`, `logicon.keyPage.previous` | none | Switch to the next / previous key page |
| `logicon.keyPage.goto` | `0`–`3` | Select a key page |
| `logicon.brightness` | `1`–`100`, `+n`, or `-n` | Keypad brightness until the next settings change |

### `zoom.*` — the Zoom service

See [Zoom](plugins/zoom.md) for setup. Every `zoom.*` action needs the Zoom service configured and a completed `zoom.signIn`.

| `action` | `target` | Effect |
| --- | --- | --- |
| `zoom.signIn` | none | Open the browser to sign in to Zoom and remember the sign-in |
| `zoom.signOut` | `now` | Forget the sign-in |
| `zoom.join` | a meeting link, or `<meeting id>[:<passcode>]` | Join |
| `zoom.start` | your PMI, or empty | Start a meeting |
| `zoom.leave`, `zoom.end` | `now` | Leave, or end for everyone (host) |
| `zoom.audio` | `join`, `leave` | Join / leave computer audio |
| `zoom.mute`, `zoom.video`, `zoom.raiseHand` | `on`, `off`, `toggle` | Your microphone, camera, and hand |
| `zoom.share` | `monitor`, `monitor@<monitor>`, `app@exe:<name.exe>`, `pause`, `resume`, `stop` | Screen sharing |
| `zoom.record` | `local.start`, `local.stop`, `cloud.start`, `cloud.stop`, `pause`, `resume` | Recording |
| `zoom.reaction` | `thumbsUp`, `clap`, `heart`, `joy`, `openMouth`, `tada` | Send a reaction |
| `zoom.chat.send` | text | Send a chat message to everyone |
| `zoom.captions` | `on`, `off` | Captions |
| `zoom.participants.muteAll`, `zoom.participants.admitAll` | none | Host controls |
| `zoom.focus` | none | Bring the Zoom window to the front |

## When something is wrong

- A misspelled or unknown `action` rejects the settings file; the message names the entry.
- A `target` that does not fit is shown on the control (red `!` / warning tile) and never runs.
- If two plugin DLLs beside `RedXe.exe` claim the same action namespace, or one that RedXe does not know, a dialog
  names both files and the affected bindings are disabled until the deployment is repaired. The dashboard keeps
  running.
