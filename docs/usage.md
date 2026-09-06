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

Swipe **left** (touch or pen) to go forward, **right** to go back. Navigation is always horizontal, including in portrait.

With a **mouse**, hover the left or right edge of the window. A chevron appears when that direction has a neighbor. Click it to change page.

By default you stop at the first and last page. Set `"wrapPages": true` in settings to wrap from either end.

Double-click or double-tap a tile that does not already fill the window to **raise** it. Close, Escape, or a second double-activate restores it. You cannot swipe pages while a widget is raised.

A tile that failed to load stays in place as a host placeholder. Other tiles on the page keep working.

## Settings file

Without `--settings`, RedXe uses one editable file:

| Build | Path |
| --- | --- |
| Debug | `%LocalAppData%\RedXe\Settings\RedXe-debug.settings.json` |
| Release | `%LocalAppData%\RedXe\Settings\RedXe.settings.json` |

Save the file to apply it. RedXe watches that path; you do not restart. A valid document keeps the page you were on when that page still exists. An invalid save leaves the last good dashboard running and shows one error dialog. Dismissing the dialog suppresses only that failed save; a later distinct invalid save can prompt again.

If the default file is missing, RedXe installs the shipped template and continues. If it is invalid, RedXe copies the bytes beside it as `<stem>.invalid-YYYY-MM-DD_HH-MM-SSZ.json`, installs a fresh template, and tells you where the backup went.

`--settings <path>` uses one portable file instead. A missing or invalid portable file is not rewritten; RedXe reports the problem and runs the shipped default in memory.

Host fields you typically edit:

| Field | Default | Meaning |
| --- | --- | --- |
| `wrapPages` | `false` | Wrap page navigation at the ends |
| `logRetentionDays` | `15` | UTC days of JSONL logs to keep (1–365) under `%LocalAppData%\RedXe\Logs\` |
| `declare` | shipped names | Reusable widget definitions (`plugin` plus optional `settings`) |
| `pages` | 1–16 | Ordered pages. Optional `name` is the label; omitted names display as `Page N` |

A page `layout` is `arrangeAlong` (`long-side` or `short-side`) plus `areas`. Each area has `sizeRatio` (1–1000) and either a `widget` or nested `areas`. `long-side` is horizontal in landscape and vertical in portrait.

Widgets are named in `declare` and referenced by that name, written inline, or reused with `{ "use": "<name>", "override": { ... } }`. Overrides merge objects, replace scalars and arrays, and may replace the plugin.

Every settings-visible widget is documented under [plugins](plugins/README.md).
