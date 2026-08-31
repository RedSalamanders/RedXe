# Active plan index

Files in this directory are non-normative execution or decision records. Current requirements remain in the owning
domain spec under `Specs/<Domain>/`.

Add every direct WIP plan to this table exactly once. Use `ACTIVE` for executable work, `HOLD` for paused work,
`DECISION` when product intent is unresolved, and `RETIRED` for a bounded tombstone.

| Plan | Status | Owning domain spec | Purpose |
| --- | --- | --- | --- |
| [`DeskClock_Direct3DPlugin_2026-08-31.md`](DeskClock_Direct3DPlugin_2026-08-31.md) | `ACTIVE` | `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_Settings.md`, and `Specs/Core/Core_PerformanceAndResources.md` | Add a low-resource Direct3D split-flap desk clock with smooth digit transitions and a local date line. |
| [`StudioClock_Direct3DPlugin_2026-08-31.md`](StudioClock_Direct3DPlugin_2026-08-31.md) | `ACTIVE` | `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_Settings.md`, and `Specs/Core/Core_PerformanceAndResources.md` | Add a low-wake Direct3D studio clock with configurable seconds progress, seconds color, and date display. |
| [`RFC_Plugins_XeneonDashboardArchitecture.md`](RFC_Plugins_XeneonDashboardArchitecture.md) | `DECISION` | Future plugin service, performance, migration, and interactive-window contracts | Resolve the remaining data-broker, host batching, advanced migration, and interactive-content slices. |
| [`SystemDataPlugin_2026-08-31.md`](SystemDataPlugin_2026-08-31.md) | `ACTIVE` | `Specs/Plugins/Plugins_API.md` and `Specs/Core/Core_PerformanceAndResources.md` | Implement bounded local machine and process snapshots, then add the host data broker and consumers. |

On completion, merge durable requirements into the owning domain spec, move the plan to `Specs/Plans/Done/`, and remove
its row here.
