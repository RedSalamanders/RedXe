# Widget screenshots

Live Debug captures of each settings-visible widget, taken from a one-tile page at a representative size. Replace a file when that widget's appearance or interaction changes.

Captures come from the application, not from a screenshot tool:

```powershell
.\.build\x64\Debug\RedXe.exe --settings <scene.settings.json> --screenshot docs\screenshots\<widget>.png --page <id> --widget 0 --after 6000
```

`--page` names the page in the scene, `--widget` the 0-based position of the tile on it, `--after` the settle time in milliseconds (use a few seconds for widgets that fetch data or wait for a device). The window is captured through Windows.Graphics.Capture without taking focus; `logicon-monitor.png` was taken this way from the Debug template's `logicon` page and scaled to half size. `dock-bottom.png` is a whole-window capture of a `--dock bottom@primary` bar (Desk Clock, Studio Clock, CPU Meter, Memory Meter, System Pulse on a 180-DIP bar at 150 %), scaled to half size; a dock capture takes no `--widget` and shows the entire bar.
