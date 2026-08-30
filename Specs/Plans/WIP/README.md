# Active plan index

Files in this directory are non-normative execution or decision records. Current requirements remain in the owning
domain spec under `Specs/<Domain>/`.

Add every direct WIP plan to this table exactly once. Use `ACTIVE` for executable work, `HOLD` for paused work,
`DECISION` when product intent is unresolved, and `RETIRED` for a bounded tombstone.

| Plan | Status | Owning domain spec | Purpose |
| --- | --- | --- | --- |
| [`RFC_Plugins_XeneonDashboardArchitecture.md`](RFC_Plugins_XeneonDashboardArchitecture.md) | `DECISION` | Future plugin API, settings, dashboard, and validation contracts | Define a normalized native plugin architecture and settings-driven XENEON dashboard composition model. |

On completion, merge durable requirements into the owning domain spec, move the plan to `Specs/Plans/Done/`, and remove
its row here.
