# Widgets

Each bundled widget has a settings `plugin` id, a screenshot, and a parameter table. None of these pages is a substitute for `Specs/`; they describe what you place and configure.

| Widget | Plugin id | Parameters |
| --- | --- | --- |
| [Matrix Rain](matrix-rain.md) | `builtin.matrix-rain` | Seed, density, speed, colors |
| [Studio Clock](studio-clock.md) | `builtin.studio-clock` | Seconds, date, colors |
| [Desk Clock](desk-clock.md) | `builtin.desk-clock` | Flip duration, colors |
| [Weather](weather.md) | `builtin.weather` | Location, units |
| [Launcher](launcher.md) | `builtin.launcher` | Shortcut list |
| [AV Control](av-control.md) | `builtin.av-control` | Audio/camera profiles |
| [Process Viewer](process-viewer.md) | `builtin.process-viewer` | `topN` |
| [GPU Processes](gpu-processes.md) | `builtin.gpu-processes` | `topN` |
| [Network Meter](network-meter.md) | `builtin.network-meter` | `topN` |
| [System Pulse](system-pulse.md) | `builtin.system-pulse` | None |
| [CPU Meter](cpu-meter.md) | `builtin.cpu-meter` | None |
| [Memory Meter](memory-meter.md) | `builtin.memory-meter` | None |
| [Storage Meter](storage-meter.md) | `builtin.storage-meter` | None |
| [GPU Meter](gpu-meter.md) | `builtin.gpu-meter` | None |
| [Power Meter](power-meter.md) | `builtin.power-meter` | None |
| [Thermal Meter](thermal-meter.md) | `builtin.thermal-meter` | None |
| [Rotating Triangle](rotating-triangle.md) | `builtin.rotating-triangle` | None |
| [GDI Orbit](gdi-orbit.md) | `builtin.gdi-orbit` | None |
| [Logicon](logicon.md) | `builtin.logicon` (service) / `builtin.logicon-monitor` (Debug tile) | Brightness, page buttons, key faces and actions, dialpad bindings, System Data faces |

`builtin.system-data` is the shared metrics source behind the System widgets. It is not a dashboard tile.
`builtin.logicon` is a background service configured under `services`; it drives a Logitech MX Creative Console
keypad and dialpad and has no tile of its own outside developer builds.
