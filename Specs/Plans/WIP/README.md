# Active plan index

Files in this directory are non-normative execution or decision records. Current requirements remain in the owning
domain spec under `Specs/<Domain>/`.

Add every direct WIP plan to this table exactly once. Use `ACTIVE` for executable work, `HOLD` for paused work,
`DECISION` when product intent is unresolved, and `RETIRED` for a bounded tombstone.

| Plan | Status | Owning domain spec | Purpose |
| --- | --- | --- | --- |
| [`RFC_Plugins_AVControl.md`](RFC_Plugins_AVControl.md) | `HOLD` | `Specs/Plugins/Plugins_AVControl.md`, `Specs/Plugins/Plugins_API.md`, `Specs/UI/UI_Dashboard.md`, `Specs/Core/Core_Settings.md`, `Specs/Core/Core_PerformanceAndResources.md`, and `Specs/Core/Core_DxUiIntegration.md` | AV release gates: real IME/touch/screen-reader, matched text/UIA performance, G1 audio policy, G2 camera. Library extraction and the synthetic DxUi pin/adapters are Done. |
| [`PluginDashboardRemainingCloseout_2026-09-02.md`](PluginDashboardRemainingCloseout_2026-09-02.md) | `ACTIVE` | `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_PerformanceAndResources.md`, `Specs/Core/Core_Settings.md`, and `Specs/UI/UI_XeneonDisplayWindowing.md` | Close remaining System Data host validation and leftover plugin-dashboard architecture gates. |
| [`PluginBoundaryHardening_2026-09-03.md`](PluginBoundaryHardening_2026-09-03.md) | `ACTIVE` | `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_PerformanceAndResources.md`, `Specs/UI/UI_Dashboard.md`, and `Specs/Core/Core_Settings.md` | Make the plugin runtime process-scoped, pin and document the ABI records, add missing host services, and isolate per-widget failure. Landed except G2 and H3. |
| [`WeatherPlugin_2026-09-04.md`](WeatherPlugin_2026-09-04.md) | `ACTIVE` | `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_PerformanceAndResources.md`, and `Specs/Core/Core_Settings.md` | Host-owned network lane and bundled Direct3D weather widget (MET Norway, MeteoAlarm, NWS; plugin-owned curl; no keys). |

On completion, merge durable requirements into the owning domain spec, move the plan to `Specs/Plans/Done/`, and remove
its row here.
