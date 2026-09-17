# Global usage

## What you see

RedXe fills a XENEON EDGE display with ordered **pages**. Each page is a tree of tiles. Sibling tiles share space by ratio, not by pixel coordinates, so the same layout works in landscape and portrait.

Shipped layouts:

| Build | First page | Other pages |
| --- | --- | --- |
| Release | Full-canvas Matrix Rain | Plugin Gallery, then System |
| Debug | Development mix | Plugin Gallery, then System |

The first page is selected on every launch. RedXe does not remember which page you were on.

## Window

| Build | With a XENEON (or CORSAIR) display | Without that display |
| --- | --- | --- |
| Release | Borderless fullscreen on that monitor | Yes/No prompt. Yes opens a normal titled window; No exits. |
| Debug | Titled window, 2560×720 design canvas, placed at the XENEON origin | Same titled window with normal Windows placement. No prompt. |

**Escape** or closing the window exits RedXe.

If RedXe stopped after a crash, the next normal launch may offer to open the local crash folder. Dumps stay on this PC; nothing is uploaded.

## Pages

Swipe **left** with **two or three fingers** to go forward, **right** to go back. Navigation is always horizontal, including in portrait. One finger stays with the tile (for example AV Control sliders, or Launcher's own pages). A second finger does not steal a slider until the two-finger swipe actually starts (moves sideways). A pen does not change dashboard pages.

With a **mouse**, hover the left or right edge of the window. A chevron appears when that direction has a neighbor. Click it to change page.

By default you stop at the first and last page. Set `"wrapPages": true` in settings to wrap from either end.

Double-click or double-tap a tile that does not already fill the window to **raise** it. Close, Escape, or a second double-activate restores it. You cannot swipe dashboard pages while a widget is raised.

A tile that failed to load stays in place as a host placeholder. Other tiles on the page keep working.

## Settings file

Without `--settings`, RedXe uses one editable file:

| Build | Path |
| --- | --- |
| Debug | `%LocalAppData%\RedXe\Settings\RedXe-debug.settings.json` |
| Release | `%LocalAppData%\RedXe\Settings\RedXe.settings.json` |

Save the file to apply it. RedXe watches that path; you do not restart. A valid document keeps the page you were on when that page still exists. An invalid save leaves the last good dashboard running and shows one error dialog with the JSON path and the reason (and line and column when those are known). Dismissing the dialog suppresses only that failed save; a later distinct invalid save can prompt again.

If the default file is missing, RedXe installs the shipped template and continues. If it is invalid, RedXe copies the bytes beside it as `<stem>.invalid-YYYY-MM-DD_HH-MM-SSZ.json`, installs a fresh template, and tells you where the backup went.

`--settings <path>` uses one portable file instead. A missing or invalid portable file is not rewritten; RedXe reports the problem and runs the shipped default in memory.

Host fields you typically edit:

| Field | Default | Meaning |
| --- | --- | --- |
| `wrapPages` | `false` | Wrap page navigation at the ends |
| `logRetentionDays` | `15` | UTC days of JSONL logs to keep (1–365) under `%LocalAppData%\RedXe\Logs\` |
| `backgroundColor` | `#000000` | Background of the whole dashboard: the canvas and every widget tile (`#RRGGBB`) |
| `declare` | shipped names | Reusable widget definitions (`plugin` plus flattened keys) |
| `services` | `Logicon` | Background services that run with the dashboard, such as the [Logicon](plugins/logicon.md) keypad service |
| `pages` | 1–16 | Ordered pages. Optional `name` is the label; omitted names display as `Page N` |

This build reads `"version": { "major": 5 }` only (minor `1` adds `services`; minor `0` files still load). A leftover version 4 file is invalid: the default path is backed up and replaced with the shipped template; `--settings` leaves the portable file alone.

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
