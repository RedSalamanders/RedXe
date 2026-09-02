# Active plan index

Files in this directory are non-normative execution or decision records. Current requirements remain in the owning
domain spec under `Specs/<Domain>/`.

Add every direct WIP plan to this table exactly once. Use `ACTIVE` for executable work, `HOLD` for paused work,
`DECISION` when product intent is unresolved, and `RETIRED` for a bounded tombstone.

| Plan | Status | Owning domain spec | Purpose |
| --- | --- | --- | --- |
| [`PluginDashboardRemainingCloseout_2026-09-02.md`](PluginDashboardRemainingCloseout_2026-09-02.md) | `ACTIVE` | `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_PerformanceAndResources.md`, `Specs/Core/Core_Settings.md`, and `Specs/UI/UI_XeneonDisplayWindowing.md` | Close remaining System Data host validation and leftover plugin-dashboard architecture gates. |

On completion, merge durable requirements into the owning domain spec, move the plan to `Specs/Plans/Done/`, and remove
its row here.
