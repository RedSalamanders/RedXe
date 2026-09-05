# Active plan index

Files in this directory are non-normative execution or decision records. Current requirements remain in the owning
domain spec under `Specs/<Domain>/`.

Add every direct WIP plan to this table exactly once. Use `ACTIVE` for executable work, `HOLD` for paused work,
`DECISION` when product intent is unresolved, and `RETIRED` for a bounded tombstone.

| Plan | Status | Owning domain spec | Purpose |
| --- | --- | --- | --- |
| [`LauncherImportAndSettingsFormatting_2026-09-05.md`](LauncherImportAndSettingsFormatting_2026-09-05.md) | `ACTIVE` | `Specs/Core/Core_Settings.md`, `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_PerformanceAndResources.md` | Persist imported taskbar pins and restore readable settings writes. |
| [`WeatherLocationFallbacks_2026-09-05.md`](WeatherLocationFallbacks_2026-09-05.md) | `ACTIVE` | `Specs/Plugins/Plugins_Weather.md`, `Specs/Core/Core_PerformanceAndResources.md` | Add bounded Windows fallbacks to the disposable location helper. |
| [`RFC_Core_DxUiSharedProject.md`](RFC_Core_DxUiSharedProject.md) | `HOLD` | Standalone DxUi domain contracts; `Specs/Plugins/Plugins_API.md`, `Specs/Plugins/Plugins_AVControl.md`, `Specs/UI/UI_Dashboard.md`, `Specs/Core/Core_PerformanceAndResources.md`, and `Specs/Build/Build_Process.md` | User paused RedXe adoption. One library and shared text/UIA implementation exist; final CI and resource acceptance remain open. See the continuation checkpoint; RedSalamander migrates later. |
| [`RFC_Plugins_AVControl.md`](RFC_Plugins_AVControl.md) | `HOLD` | `Specs/Plugins/Plugins_AVControl.md`, `Specs/Plugins/Plugins_API.md`, `Specs/UI/UI_Dashboard.md`, `Specs/Core/Core_Settings.md`, and `Specs/Core/Core_PerformanceAndResources.md` | User paused after UIA integration and camera guidance. Saved pins, evidence and resume steps in docs/AVControl-Continuation.md. Final dependency adoption, real-device usability and resource acceptance remain open. |
| [`PluginDashboardRemainingCloseout_2026-09-02.md`](PluginDashboardRemainingCloseout_2026-09-02.md) | `ACTIVE` | `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_PerformanceAndResources.md`, `Specs/Core/Core_Settings.md`, and `Specs/UI/UI_XeneonDisplayWindowing.md` | Close remaining System Data host validation and leftover plugin-dashboard architecture gates. |
| [`PluginBoundaryHardening_2026-09-03.md`](PluginBoundaryHardening_2026-09-03.md) | `ACTIVE` | `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_PerformanceAndResources.md`, `Specs/UI/UI_Dashboard.md`, and `Specs/Core/Core_Settings.md` | Make the plugin runtime process-scoped, pin and document the ABI records, add missing host services, and isolate per-widget failure. Landed except G2 and H3. |
| [`WeatherPlugin_2026-09-04.md`](WeatherPlugin_2026-09-04.md) | `ACTIVE` | `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_PerformanceAndResources.md`, and `Specs/Core/Core_Settings.md` | Host-owned network lane and bundled Direct3D weather widget (MET Norway, MeteoAlarm, NWS; plugin-owned curl; no keys). |

On completion, merge durable requirements into the owning domain spec, move the plan to `Specs/Plans/Done/`, and remove
its row here.
