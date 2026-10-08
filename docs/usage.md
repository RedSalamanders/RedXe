# Global usage

## Install

RedXe ships as a portable ZIP per CPU architecture (`RedXe-<version>-x64-Portable.zip`,
`RedXe-<version>-ARM64-Portable.zip`) on the [releases page](https://github.com/RedSalamanders/RedXe/releases). It
needs Windows 10 version 2004 or later; the Visual C++ runtime is inside the package.

**From the ZIP.** Extract it anywhere and run `RedXe.exe` — that is a complete installation. To get a Start Menu
shortcut, an entry under Settings > Apps, and (optionally) RedXe starting when you sign in, run `install.cmd` from
the extracted folder. It copies the package to `%LocalAppData%\Programs\RedXe`; nothing needs administrator rights.

```powershell
.\install.cmd                              # copy, Start Menu shortcut, Apps entry
.\install.cmd -StartAtSignIn -Launch       # also start at sign-in, then start now
.\Install-RedXe.ps1 -Destination D:\Apps\RedXe
.\Install-RedXe.ps1 -InPlace               # register this folder without copying it
.\uninstall.cmd                            # remove the shortcut, sign-in entry, Apps entry, and the copy
.\uninstall.cmd -PurgeUserData             # also delete %LocalAppData%\RedXe (settings, logs, crash dumps)
```

Running `install.cmd` again with a newer package upgrades the copy in place; your settings under
`%LocalAppData%\RedXe` are never touched by install or uninstall (unless you ask with `-PurgeUserData`). Close RedXe
before installing or removing: a running copy is reported, never killed.

**With winget.**

```powershell
winget install RedSalamanders.RedXe
```

This installs the same package and adds a `RedXe` command to new terminals (`RedXe`, `RedXe --dock bottom`,
`RedXe --help`). winget does not create Start Menu shortcuts for portable packages; to add one, or to start at
sign-in, run `install.cmd` inside the package folder,
`%LocalAppData%\Microsoft\WinGet\Packages\RedSalamanders.RedXe_Microsoft.Winget.Source_8wekyb3d8bbwe` — inside a
winget package the installer registers that folder in place. Upgrade with `winget upgrade RedSalamanders.RedXe`
and remove with `winget uninstall RedSalamanders.RedXe`.

The `RedXe` command is a small launcher (`RedXeLauncher.exe`) that starts `RedXe.exe` from the package folder;
double-clicking `RedXe.exe` itself does the same thing. The command returns at once when it starts the dashboard; for
`--help`, `--self-test`, `--screenshot`, the crash tests, and a mistyped switch it waits, so you see the output and
get RedXe's exit code.

## What you see

RedXe fills a XENEON EDGE display with ordered **pages**. Each page is a tree of tiles. Sibling tiles share space by ratio, not by pixel coordinates, so the same layout works in landscape and portrait.

Shipped layouts:

| Build | First page | Other pages |
| --- | --- | --- |
| Release | Full-canvas Matrix Rain | Plugin Gallery, System, then a full-canvas 5H4D3R5 shader slideshow |
| Debug | Development mix | Logicon, Plugin Gallery, System, then a full-canvas 5H4D3R5 shader slideshow |

The first page is selected on every launch. RedXe does not remember which page you were on.

## Window

| Build | With a XENEON (or CORSAIR) display | Without that display |
| --- | --- | --- |
| Release | Borderless fullscreen on that monitor | Yes/No prompt. Yes opens a normal titled window; No exits. |
| Debug | Titled window, 2560×720 design canvas, placed at the XENEON origin | Same titled window with normal Windows placement. No prompt. |

With a `dock` configured (or `--dock` on the command line) both builds run as a bar on a screen edge instead; see [Dock](#dock).

On the very first start without a XENEON (no settings file yet), RedXe skips the prompt and the plain window: it writes an auto-hiding bar into the new settings file, on your second screen when you have more than one and on its bottom edge unless that screen's own taskbar is there, and starts as that bar. See [First start without a XENEON](#first-start-without-a-xeneon).

**Escape**, closing the window, or **Exit** in the [notification-area icon](#notification-area-icon)'s menu exits RedXe. Signing out, restarting, or shutting down Windows closes it the same way, so widget settings are saved and a [Logicon keypad](plugins/logicon.md) gets its own buttons back.

If RedXe stopped after a crash, the next normal launch may offer to open the local crash folder. Dumps stay on this PC; nothing is uploaded. When RedXe runs as a [bar](#dock), that question comes before the bar appears.

## Notification-area icon

The Release build puts the RedXe icon in the notification area of the taskbar (the system tray). Windows may first put it among the hidden icons behind the **^** arrow; drag it onto the taskbar, or turn it on under **Settings > Personalization > Taskbar**, to keep it in view. The Debug build shows it only when you ask for it.

- **Double-click** the icon (or select it with the keyboard and press **Enter**) to open the settings file RedXe is using in your default editor for `.json` files. If no app is associated with `.json` yet, Windows asks which one to use; the dashboard keeps running while it asks. It works while the settings-error dialog is open, which is when you most need the file.
- **Right-click** it (or press **Shift+F10** on it) for the menu: **Edit settings** does the same as a double-click; **Exit** closes RedXe.

Turn it on or off with `trayIcon` in the settings file; the change applies when you save:

```json
"trayIcon": false
```

Without `trayIcon`, Release shows the icon and Debug does not. The shipped files write it out: `true` in the Release file, `false` in the Debug file.

If Windows is still busy when RedXe starts (right after you sign in), the icon can take a few seconds to appear. It also comes back by itself when Windows Explorer restarts.

## Dock

Instead of the window above, RedXe can run as a **bar along one edge of a monitor** — any monitor, not only the XENEON — like a second taskbar. Put a `dock` object in the settings file, or try it for one run from the command line:

![A bottom dock: Desk Clock, Studio Clock, CPU Meter, Memory Meter, and System Pulse in a 180-DIP bar](screenshots/dock-bottom.png)

```powershell
RedXe.exe --dock bottom@primary
RedXe.exe --dock left@2 --dock-thickness 240 --dock-reserve off
RedXe.exe --dock top@name:DELL --dock-mode autohide --dock-peek 6
RedXe.exe --settings C:\Dash\bar.settings.json --dock none
```

| Setting (`dock`) | Switch | Default | Meaning |
| --- | --- | --- | --- |
| `edge` | `--dock <edge>` | `none` | `top`, `bottom`, `left`, or `right` of the monitor. `none` is the normal window. |
| `monitor` | `--dock <edge>@<monitor>` | `primary` | `primary`, `secondary` (your second screen: the first display that is not the main one, skipping a XENEON unless it is the only other one), `xeneon`, a display number (`2`), or `name:<part of the display name>` (`name:DELL`, `name:DISPLAY2`). A display that is not connected falls back to the primary. |
| `thickness` | `--dock-thickness` | `180` | How deep the bar is, in DIPs (scaled with the monitor's display scaling; 180 is 270 px at 150 %). 32–1080, at most half the monitor. |
| `mode` | `--dock-mode` | `fixed` | `fixed` keeps the bar on screen. `autohide` collapses it to a few pixels until you point at them; maximized windows stop just inside those pixels. |
| `reserveWorkArea` | `--dock-reserve on\|off` | `true` | `fixed` only. `true`: maximized windows stop at the bar. `false`: the bar floats over the maximized area. An `autohide` bar always keeps its thin strip clear of maximized windows. |
| `peek` | `--dock-peek` | `4` | `autohide` only: how many pixels stay visible while the bar is collapsed (1–64); maximized windows stop just inside them. |
| `revealDelayMilliseconds` | — | `150` | `autohide` only: how long the pointer must rest on the strip before the bar comes back (0–2000; 0 is immediate). |
| `hideDelayMilliseconds` | — | `800` | `autohide` only: how long after the pointer leaves the bar collapses again (0–10000). |
| `animationMilliseconds` | — | `200` | `autohide` only: how long the bar takes to slide out of the edge when it appears, and back when it collapses (0–1000; 0 shows and hides it at once). |

A switch overrides that one setting for the run, even when you edit the file while RedXe is running. Everything else about the dashboard is unchanged: your pages, swipes, edge chevrons, wheel, raise, and services work in the bar; a left or right bar is simply portrait. Author the pages for the bar's shape — a 180-DIP strip holds a Launcher row, a clock, and a meter comfortably; the shipped XENEON pages are too tall for it:

```json
{
  "version": { "major": 5, "minor": 2 },
  "dock": { "edge": "bottom", "monitor": "primary", "mode": "autohide" },
  "declare": {
    "Launcher": { "plugin": "builtin.launcher", "shortcuts": [], "iconSize": "medium" },
    "Clock": { "plugin": "builtin.desk-clock" }
  },
  "pages": [
    { "columns": [ { "weight": 5, "widget": "Launcher" }, { "weight": 2, "widget": "Clock" } ] }
  ]
}
```

Good to know:

- The bar has no taskbar button and does not take the focus when it starts. To quit, choose **Exit** in the [notification-area icon](#notification-area-icon)'s menu, click or tap the bar and press **Escape**, or bind `redxe.quit`.
- **Resize by dragging**: point at the bar's inner edge (the side facing the desktop; the cursor becomes a resize arrow), press, and drag. The bar follows the mouse; when you release, a `fixed` bar that reserves its space reserves the new size (an `autohide` bar keeps reserving only its thin strip) and the new `thickness` is written to the settings file, so it survives the next start. Only that value changes: your comments and layout stay, and a file without a `dock` gains a `"dock": { "thickness": ... }` line right after `version`. A click on the edge that leaves the size as it was writes nothing. Works in `fixed` and `autohide` (the bar stays open while you drag); a `--dock-thickness` switch is replaced by the dragged value for that run.
- **Autohide**: rest the mouse on the thin strip at the screen edge and the bar slides out of the edge (it takes `animationMilliseconds`, after the `revealDelayMilliseconds` wait); it collapses again shortly after the pointer leaves and nothing else holds it (a raised widget, a swipe, a text field with the focus, the settings-error dialog). A click or a touch on the strip reveals at once, and a Logicon key or a Launcher tile bound to `redxe.dock.show`, `hide`, or `toggle` does too; one bound to a `page.*` or `widget.*` [action](actions.md) brings the bar out first so you see the result. A click or a tap while the bar is still sliding out of the edge goes to what you saw under the pointer, and the mouse wheel over the thin strip does nothing. If the taskbar sits on the same edge, the strip is just above the taskbar, so aim for that line or use another edge.
- **Maximized windows and an auto-hiding bar**: Windows keeps the thin strip free, so a maximized window on that screen stops just inside it: with a bar at the top, its title-bar buttons and tabs sit just below the strip instead of under it; at the bottom or a side, its status bar or scrollbar stays uncovered. When the bar slides out, it covers part of the maximized window until it collapses again; the window does not move. When RedXe exits, maximized windows grow back to the screen edge.
- The bar never covers the taskbar: with `reserveWorkArea` off it hugs the edge of the free area, and an auto-hiding bar's strip sits next to a taskbar on the same edge.
- A full-screen game or video on that monitor pushes the bar beneath it; it returns when you leave full screen. A full-screen window on another display (a XENEON dashboard, a video on a second screen), or a maximized window with a title bar, leaves the bar on top.
- Saving the file applies every change while RedXe runs: `thickness`, `edge`, `monitor`, `mode`, `reserveWorkArea`, `peek`, the delays, and `animationMilliseconds` re-place the bar, and turning the dock on or off (`edge` between `none` and an edge) turns the running window into the bar, or the bar back into the normal window (fullscreen or titled, as in [Window](#window), without the XENEON prompt). The switch never takes the focus from the editor you saved in. If the switch cannot be completed, RedXe keeps the window it had and shows the settings error dialog instead of closing. A `--dock` switch keeps its edge for that run.
- `--screenshot` works for a dock too; an auto-hiding bar is held open for the capture.
- If Windows Explorer restarts while the bar runs, the bar registers with the new taskbar by itself: the reserved space, auto-hiding, and stepping beneath full-screen windows come back without restarting RedXe.
- If RedXe crashes while it reserves space (a `fixed` bar with `reserveWorkArea`, or an auto-hiding bar's thin strip), Windows may keep that space reserved until RedXe runs again or you sign out.

### First start without a XENEON

When RedXe starts with no settings file yet and no XENEON display is connected, the settings file it creates already contains a bar, with a comment saying why. This `dock` line is the one to edit: the commented-out `dock` example the shipped template carries further down is left out of this file, so there is no second one to uncomment.

```jsonc
  // No XENEON display was found when RedXe installed this file, so this dock runs it as a bar on a screen edge;
  // set "edge" to "none" to use the standard window instead. See docs/usage.md "Dock" for the other members.
  "dock": { "edge": "bottom", "monitor": "secondary", "mode": "autohide", "thickness": 720 },
```

Only a first start does this. When RedXe replaces an invalid settings file, the new file is the plain shipped one, without a bar, even if no XENEON is connected. A first start in a Remote Desktop session also writes the plain file, because RedXe sees only the remote screens there.

Where the bar goes is decided once, when the file is written:

- **Which screen**: with more than one display, your second screen (`"secondary"`: the first display that is not the main one, skipping a XENEON unless it is the only other one); with a single display, that display (`"primary"`). Windows reports a TV that is in standby but still connected, a dummy display plug, and a virtual display (one a streaming or screen-sharing app adds) like any other screen, so the bar can go there; set `monitor` to a display number or to `name:` and part of the display's name to choose another.
- **Which edge**: the **bottom**, away from the title-bar buttons and tabs at the top of maximized windows, unless that screen's own taskbar is at the bottom; then the top, so the thin strip sits at the screen edge rather than next to the taskbar (maximized windows then stop just below the strip). An edge where another display sits right above or below is used only when both are like that, so moving the mouse from one screen to the other does not cross the strip.

The bar collapses to a thin line until you rest the pointer on it. Its `thickness` gives it the XENEON's 32:9 shape across that display, so the shipped pages look as they do on a XENEON: on a 16:9 display that is half the screen height (720 on a 4K display at 150 %, 540 on a 1920×1080 display at 100 %). Drag its inner edge or edit `thickness` to make it smaller, and edit `edge` or `monitor` to move it. If you connect a XENEON later, the bar stays on your second screen (`"secondary"` skips the XENEON) until you set `"edge": "none"`, which switches to the XENEON window at once. A settings file written by an earlier RedXe keeps its bar where it is. The log of that first start (a `dock-first-run` entry) records how many displays RedXe saw and the size of the one it chose.

## Command line

`RedXe.exe --help` (also `-h`, `/?`, `-?`) prints every switch, grouped, with the exit codes, to the console you ran it from (or to a message box when there is none, unless `--self-test` or `--screenshot` is also on the line, since those runs never wait on a box). Every switch overrides the settings file for that run only; an unknown switch is an error (exit code 2) rather than silently ignored.

| Switch | Meaning |
| --- | --- |
| `--settings <path>` | Use one portable settings file instead of the one under `%LocalAppData%\RedXe\Settings`; its `Logs` folder sits beside it. |
| `--warp` | Render on the Microsoft Basic Render Driver (WARP) instead of the GPU. |
| `--dock <edge>[@<monitor>]` | Run as a bar on that screen edge; see [Dock](#dock) for `--dock-mode`, `--dock-thickness`, `--dock-reserve`, and `--dock-peek`. |
| `--screenshot <png>` | Start normally, wait, save the window as PNG, and exit; see [Screenshots](#screenshots) for `--page`, `--widget`, and `--after`. |
| `--self-test` | Hidden startup validation with the deployed template; exits 0 when the host works and 6 when a check fails, which it names on stderr. |
| `--crash-test`, `--crash-test-stack-overflow`, `--crash-test-directory=<dir>` | Raise a test crash and choose where its dump goes (used by `test.ps1`). |

Exit codes: 0 ok, 1 settings, 2 command line or window, 3 plugins, 5 graphics, 7 settings watcher, 8 screenshot capture. With `--self-test`: 6 a failed check, 3 a failed runtime check of a Debug build, 4 any other `abort()`, each reported on stderr. The self-test keeps no log file, so it writes the warnings and errors RedXe would log to stderr too, one JSON line each: a failed check then comes with what explains it, such as a plugin DLL that could not be loaded.

## Screenshots

`RedXe.exe --screenshot <file.png> [--page <id>] [--widget <ordinal>] [--after <milliseconds>]` starts the dashboard as usual, jumps to that page (default: the start page), waits for the delay (default 3000 ms, so widgets and devices have settled), saves its own window — or only the widget at that 0-based position on the page — as a PNG through Windows.Graphics.Capture, and exits. It never takes the focus or moves the mouse. Combine it with `--settings` for a repeatable scene; the exit code is 0 when the file was written and 8 when it was not, including when RedXe was closed, or failed to start or draw, before the capture finished (once the log is open, it has a `screenshot-failed` entry; a failure before the settings load reaches only the debugger output). A capture never waits for an answer: it shows no message box, so a failure is its exit code plus a `failure-exit` entry in the log, a mistyped switch prints its usage to the console or redirected output (exit code 2), a settings problem is a `settings-fallback-notice` entry, and a Release build without a XENEON captures the standard window without asking. `--self-test` follows the same rule. The pictures under `docs/screenshots/` are produced this way. The [`redxe.screenshot`](actions.md#redxe) action saves the same picture from a key or tile and leaves RedXe running.

## Pages

Swipe **left** with **two or three fingers** to go forward, **right** to go back. Navigation is always horizontal, including in portrait. One finger stays with the tile (for example AV Control sliders, or Launcher's own pages). A second finger does not steal a slider until the two-finger swipe actually starts (moves sideways). A pen does not change dashboard pages.

With a **mouse**, hover the left or right edge of the window. A chevron appears when that direction has a neighbor. Click it to change page.

The **mouse wheel** changes pages too: roll **down** (or tilt the wheel **right**, or two-finger swipe right on a precision touchpad) to go forward, **up** or **left** to go back. One notch is one page. A tile that shows page dots keeps the wheel for itself — Launcher pages its icons, Process Viewer and the other System Data meters page their extra rows, Weather pages its forecast hours or days — and at its last page a further notch simply does nothing; the dashboard never changes page under it. To change the dashboard page with the wheel, point at a tile without page dots (a clock, Matrix Rain, a list that fits) or use the edge chevrons.

Every tile with pages of its own shows the same **page control**: a row of dots, one per page, with the current page brighter and larger. Tap a dot to jump to that page, or swipe with one finger, or use the wheel. A `+N` mark, where one still appears, counts items that are not on any page (idle network adapters, sensors that do not fit a very small tile).

By default you stop at the first and last page. Set `"wrapPages": true` in settings to wrap from either end.

Double-click or double-tap a tile that does not already fill the window to **raise** it. Close, Escape, or a second double-activate restores it. You cannot swipe dashboard pages while a widget is raised.

A tile that failed to load stays in place as a host placeholder. Other tiles on the page keep working.

## Settings file

Without `--settings`, RedXe uses one editable file:

| Build | Path |
| --- | --- |
| Debug | `%LocalAppData%\RedXe\Settings\RedXe-debug.settings.json` |
| Release | `%LocalAppData%\RedXe\Settings\RedXe.settings.json` |

The file is UTF-8 JSON; `//` and `/* */` comments and trailing commas are allowed, and a file saved with a byte order mark ("UTF-8 with BOM", as Windows PowerShell 5.1 `Set-Content -Encoding UTF8` writes it) works too and keeps its mark when RedXe writes to it. Save the file to apply it. RedXe watches that path; you do not restart. When the [notification-area icon](#notification-area-icon) is shown, double-clicking it is the quickest way to open the file. A valid document keeps the page you were on when that page still exists. A save that changes what the current page shows (its tiles, grid, or background) while the RedXe window is minimized takes effect when you restore the window, and a save made while you drag or resize the window takes effect when you let go. An invalid save leaves the last good dashboard running and shows one error dialog with the JSON path and the reason (and line and column when those are known). Dismissing the dialog suppresses only that failed save; a later distinct invalid save can prompt again.

RedXe writes the file itself only when a widget's own settings actually change (a Launcher importing your taskbar pins, for example) or when you drag the bar's edge, and only while the file on disk is the one it last loaded. While your last save is invalid, the file was deleted, or a `--settings` file could not be loaded, such a change stays in memory and your file is left as it is (the log records `settings-persist-deferred`); your next valid save takes over from it.

If the default file is missing, RedXe installs the shipped template and continues. If it is invalid, RedXe copies the bytes beside it as `<stem>.invalid-YYYY-MM-DD_HH-MM-SSZ.json`, installs a fresh template, and tells you where the backup went. Without a XENEON connected, a template installed because the file was missing also gets an auto-hiding bar; one that replaces an invalid file does not. See [First start without a XENEON](#first-start-without-a-xeneon).

`--settings <path>` uses one portable file instead. A missing or invalid portable file is not rewritten; RedXe reports the problem and runs the shipped default in memory.

Host fields you typically edit:

| Field | Default | Meaning |
| --- | --- | --- |
| `wrapPages` | `false` | Wrap page navigation at the ends |
| `logRetentionDays` | `15` | UTC days of JSONL logs to keep (1–365) under `%LocalAppData%\RedXe\Logs\` |
| `backgroundColor` | `#000000` | Background of the whole dashboard: the canvas and every widget tile (`#RRGGBB`) |
| `declare` | shipped names | Reusable widget definitions (`plugin` plus flattened keys) |
| `services` | `Logicon` | Background services that run with the dashboard, such as the [Logicon](plugins/logicon.md) keypad service |
| `dock` | off | Run RedXe as a bar on a screen edge instead of a window; see [Dock](#dock) |
| `trayIcon` | `true` in Release, `false` in Debug | Show RedXe's icon in the notification area; see [Notification-area icon](#notification-area-icon) |
| `pages` | 1–16 | Ordered pages. Optional `name` is the label; omitted names display as `Page N` |

This build reads `"version": { "major": 5 }` only (minor `1` adds `services`, minor `2` adds `dock`, minor `3` adds `trayIcon`, the `secondary` monitor, and `dock.animationMilliseconds`; older minors still load). A leftover version 4 file is invalid: the default path is backed up and replaced with the shipped template; `--settings` leaves the portable file alone.

A settings file from an earlier release, back to RedXe 1.0.102, keeps working after an upgrade. Settings that a newer release retired, such as the old `services` entry for Zoom (`builtin.zoom`, with or without its `clientId`, `redirectPort`, and `autoConnect`), are ignored, and the log notes them once per load; you can delete them. Zoom actions need no `services` entry ([Zoom](plugins/zoom.md)). A Logicon key, dialpad button, or turn bound to `keys.down` or `mouse.down` also keeps loading, but it shows a red `!` and does nothing ([Logicon](plugins/logicon.md)). Going back to an older release is not supported: it may reject settings it does not know, such as the Studio Clock `glowPercent`, and then saves your file as `RedXe.settings.invalid-<date>.json` beside its own fresh template.

A page uses exactly one of `widgets`, `columns`, or `rows` (or none, for a blank page). `columns` split along the long side of the window, `rows` along the short side. Omitted `weight` is 1. `widgets` is an equal-share list (omitted `along` is `long-side`). Nested `rows` inside `columns` stack tiles in a column.

```json
{
  "version": { "major": 5 },
  "declare": {
    "Matrix": { "plugin": "builtin.matrix-rain" },
    "Launcher": { "plugin": "builtin.launcher", "shortcuts": [] }
  },
  "pages": [
    { "name": "Main", "widgets": ["Matrix"] },
    {
      "name": "Mix",
      "columns": [
        { "weight": 2, "rows": ["Launcher", "Matrix"] },
        { "weight": 5, "widget": { "use": "Matrix", "seed": 4242 } }
      ]
    }
  ]
}
```

Widgets are named in `declare` and referenced by that name, written as a `builtin.*` plugin id, written inline as `{ "plugin": "...", ...keys }`, or reused with `{ "use": "<name>", ...keys }`. Extra keys on a use-object merge: objects merge, scalars and arrays replace. Do not nest a `settings` object and do not write `layout` / `areas`.

## Background color

Every tile shares the document `backgroundColor` (default `#000000`), so a page reads as one surface. Change it at the root of the file to recolor the whole dashboard:

```json
{
  "version": { "major": 5 },
  "backgroundColor": "#101418",
  "pages": [{ "widgets": ["Matrix"] }]
}
```

To give one widget its own background, put `backgroundColor` on that widget object. It works for every widget, in `declare`, inline, or on a use-object (`null` on a use-object goes back to the document color):

```json
{
  "declare": { "Clock": { "plugin": "builtin.studio-clock", "backgroundColor": "#111111" } },
  "pages": [
    { "widgets": ["Clock", { "use": "Clock", "backgroundColor": null }, { "plugin": "builtin.weather", "backgroundColor": "#0A0A0A" }] }
  ]
}
```

Every settings-visible widget is documented under [plugins](plugins/README.md).
