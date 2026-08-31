# Active plan index

Files in this directory are non-normative execution or decision records. Current requirements remain in the owning
domain spec under `Specs/<Domain>/`.

Add every direct WIP plan to this table exactly once. Use `ACTIVE` for executable work, `HOLD` for paused work,
`DECISION` when product intent is unresolved, and `RETIRED` for a bounded tombstone.

| Plan | Status | Owning domain spec | Purpose |
| --- | --- | --- | --- |
| [`RFC_Core_SettingsSchema.md`](RFC_Core_SettingsSchema.md) | `DECISION` | `Specs/Core/Core_Settings.md` with dashboard and plugin contracts | Design a human-first JSON settings model that can later drive the RedXe settings UI. |
| [`RFC_Plugins_XeneonDashboardArchitecture.md`](RFC_Plugins_XeneonDashboardArchitecture.md) | `DECISION` | Future plugin service, performance, settings-UI, and interactive-window contracts | Resolve only the unbuilt data-provider, host batching, configuration UI, migration, and interactive-content slices. |

On completion, merge durable requirements into the owning domain spec, move the plan to `Specs/Plans/Done/`, and remove
its row here.
