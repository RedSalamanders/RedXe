# Active plan index

Files in this directory are non-normative execution or decision records. Current requirements remain in the owning
domain spec under `Specs/<Domain>/`.

Add every direct WIP plan to this table exactly once. Use `ACTIVE` for executable work, `HOLD` for paused work,
`DECISION` when product intent is unresolved, and `RETIRED` for a bounded tombstone.

| Plan | Status | Owning domain spec | Purpose |
| --- | --- | --- | --- |
| [`RFC_Plugins_AVControl.md`](RFC_Plugins_AVControl.md) | `HOLD` | `Specs/Plugins/Plugins_AVControl.md`, `Specs/Plugins/Plugins_API.md`, `Specs/UI/UI_Dashboard.md`, `Specs/Core/Core_Settings.md`, `Specs/Core/Core_PerformanceAndResources.md`, and `Specs/Core/Core_DxUiIntegration.md` | AV release gates: real IME/touch/screen-reader, matched text/UIA performance, G1 audio policy, G2 camera. Library extraction and the synthetic DxUi pin/adapters are Done. |
| [`RFC_Plugins_Actions.md`](RFC_Plugins_Actions.md) | `ACTIVE` | `Specs/Plugins/Plugins_Actions.md`, `Specs/Plugins/Plugins_Zoom.md`, `Specs/Plugins/Plugins_API.md`, `Specs/Plugins/Plugins_Logicon.md`, and `Specs/Core/Core_Settings.md` | Phase 1 (binding shape, default namespaces, publication ABI, registry and collisions, host runtime, Logicon and Launcher owners, `zoom.action.dll` over the synthetic session) is implemented and specified. Remaining: pin the Zoom SDK adapter against the imported package and validate against Zoom Workplace (phase 2), then the later `<namespace>.action.dll` publishers (`screen`, `window`, `navigate`, `audio`, `clipboard`) and Zoom state faces (phases 3–4). |
| [`PluginDashboardRemainingCloseout_2026-09-02.md`](PluginDashboardRemainingCloseout_2026-09-02.md) | `ACTIVE` | `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_PerformanceAndResources.md`, `Specs/Core/Core_Settings.md`, and `Specs/UI/UI_XeneonDisplayWindowing.md` | Close remaining System Data host validation and leftover plugin-dashboard architecture gates. |
| [`PluginBoundaryHardening_2026-09-03.md`](PluginBoundaryHardening_2026-09-03.md) | `ACTIVE` | `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_PerformanceAndResources.md`, `Specs/UI/UI_Dashboard.md`, and `Specs/Core/Core_Settings.md` | Make the plugin runtime process-scoped, pin and document the ABI records, add missing host services, and isolate per-widget failure. Landed except G2 and H3. |
| [`HostChromeWithoutGdi_2026-09-07.md`](HostChromeWithoutGdi_2026-09-07.md) | `ACTIVE` | `Specs/UI/UI_Dashboard.md`, `Specs/Core/Core_PerformanceAndResources.md`, `Specs/UI/UI_XeneonDisplayWindowing.md`, `Specs/Plugins/Plugins_API.md`, and `Specs/Core/Core_Settings.md` | Direct3D edge bands and raise chrome, no chrome HWNDs, GdiOrbit off the shipped templates, layered native containers, `WS_EX_NOREDIRECTIONBITMAP`. |
| [`RuntimeMemoryAndLatency_2026-09-07.md`](RuntimeMemoryAndLatency_2026-09-07.md) | `ACTIVE` | `Specs/Core/Core_PerformanceAndResources.md`, `Specs/UI/UI_XeneonDisplayWindowing.md`, `Specs/Plugins/Plugins_API.md`, `Specs/UI/UI_Dashboard.md`, `Specs/Core/Core_Settings.md`, `Specs/Core/Core_DxUiIntegration.md`, and `Specs/Plugins/Plugins_AVControl.md` | Measure and cut whole-process memory and lag: adapter-of-output device that follows the window, waitable swap chain, idle System page without per-process handles, fluid swipes with a settle sweep and `RedXePluginTrim`, arena-backed System Data, lazy Launcher/AV/Weather retention, DxUi consumer accounting. |

On completion, merge durable requirements into the owning domain spec, move the plan to `Specs/Plans/Done/`, and remove
its row here.

[DxUi adoption is complete](../Done/DxUiAdoption_2026-09-09.md) for the accepted local scope.
Further ARM64 and ASan qualification is user-deferred and routed through RedSalamander's
`DxUi_DeferredPlatformQualification_2026-09-13.md`; AV release gates retain their owner above.
