---
name: plugin-development
description: Implement or revise RedXe native plugin interfaces, factory exports, PluginManager loading, bundled plugin DLLs, or generic widget instances. Do not use for renderer-only D3D pipeline work.
---

# RedXe plugin development

Start with [the plugin API contract](../../../Specs/Plugins/Plugins_API.md). Read the owning contract for the work:

- [Weather](../../../Specs/Plugins/Plugins_Weather.md): network lane, curl and forecast behavior.
- [Logicon](../../../Specs/Plugins/Plugins_Logicon.md): headless service and device lane.
- [Actions](../../../Specs/Plugins/Plugins_Actions.md): action publication, execution and validation.
- [Dashboard](../../../Specs/UI/UI_Dashboard.md): placement, pointer routing, raised widgets and internal paging.
- [Performance and resources](../../../Specs/Core/Core_PerformanceAndResources.md): every ABI or hot-path change.

Use [the active plan index](../../../Specs/Plans/WIP/README.md) for unfinished work; delivery history and old
closeout snapshots do not define today's ABI or support status. Apply `spec-workflow` when behavior or ABI changes.

## Boundaries

- Keep public ABI declarations under `Common/PlugInterfaces/`, host-only code under `RedXe/`, and bundled DLL
  projects under `Plugins/`. Match the current ABI in place before an explicit production freeze; rebuild every
  source-coordinated consumer. Do not invent legacy-IID or vtable-prefix compatibility.
- Expose rendering/work mechanisms as sibling `IUnknown` interfaces with one controlling identity. Keep widget
  declarations in `Widget.h` and data declarations in `Data.h`; plugin geometry and drawing commands stay out of
  the generic root. Public records use the contract's exact size checks and expose no owning C++/yyjson types.
- Use `FactoryImpl.h`, clear outputs before validation, return the specified HRESULT and one caller-owned reference.
  Load only absolute DLL paths with restricted flags. `PluginHost` maps shared DLLs once; release objects before
  optional shutdown and retain modules until process teardown. Do not add hot unload.
- Determine template coverage from `RedXe/BundledPlugins.h` and
  [the settings template contract](../../../Specs/Core/Core_Settings.md#selection-storage-and-deployed-defaults):
  ordinary widgets have effective examples in both templates, opt-in widgets in neither, and Debug-only widgets
  only in Debug. Services have entries in both templates without requiring a placed widget. Update the catalog,
  schema/parser, owning contract and `docs/plugins/` together; validate category-aware coverage.
- A catalogued DLL that cannot map becomes an instance placeholder; an unknown plugin ID rejects the document.
  Preserve per-instance state even when a shared module serves multiple plugin IDs.
- Deliver copied sink references after releasing subscription locks; coalesce UI invalidation. A failed snapshot
  batch must not commit rate-history mutation.
- GPU widgets borrow the device/context, never the HWND, swap chain or back buffer. Share compatible immutable
  resources. Preserve full-size, potentially negative swipe viewports; Direct3D performs clipping.
- Query `IRedXeRaisedWidget::GetRaisedExtent` and follow its notification lifecycle instead of guessing dimensions.
  Pointer cancellation, wheel-sequence ownership, paging geometry, density and typography belong to the linked
  dashboard/plugin contracts. Reuse `Common/WheelDetent.h` and `Common/PageIndicator.h`; test boundary paging
  and cancellation when changing interaction rather than copying their metrics.
- Native-window widgets own their children/resources, quiesce visibility-dependent work and destroy children before
  detach returns. GPU Render, paint and visibility callbacks remain allocation-free and nonblocking; prepare changed
  glyphs/textures on the owner before rendering and reuse bounded size-dependent resources.
- Use sparse `IRedXeHost::Log` failure/lifetime records; no per-frame success, Render or paint logging. Consult the
  performance contract for current capacities and worker stack limits instead of duplicating them here.
- Persist through host services, never by writing the settings file in a plugin. Follow the API's permitted callback
  set, queued-worker delivery and collect-on-exit fallback. No exception crosses exports, COM or Win32 callbacks.

## Build and verification

New plugin projects follow the supported matrix in the solution/project files and
[the build contract](../../../Specs/Build/Build_Process.md#output-and-validation). Place their DLLs under the host's
`Plugins` output with build-only project references. Preserve each plugin's declared runtime DLL/font dependencies
and the package smoke contract; do not substitute a guessed dependency filename or a developer-global DLL.

Iterate with `Test-Changes.ps1 -Explain` and affected coverage. For qualification, use PrePush/Full accounting and
the owning domain's build/runtime gates; an unchanged CI-covered obligation need not run twice. Run
`validate-skills.ps1` for guidance changes. Focus-taking acceptance requires agreement to the time.
