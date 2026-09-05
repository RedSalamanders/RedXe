---
name: plugin-development
description: Implement or revise RedXe native plugin interfaces, factory exports, PluginManager loading, bundled plugin DLLs, or generic widget instances. Do not use for renderer-only D3D pipeline work.
---

# RedXe plugin development

Read `Specs/Plugins/Plugins_API.md` before changing shipped plugin behavior. Read
[`Specs/Plans/WIP/WeatherPlugin_2026-09-04.md`](../../Specs/Plans/WIP/WeatherPlugin_2026-09-04.md) when work concerns
the host network lane, plugin-owned curl, or the weather widget. Read
[`Specs/Plans/WIP/PluginDashboardRemainingCloseout_2026-09-02.md`](../../Specs/Plans/WIP/PluginDashboardRemainingCloseout_2026-09-02.md)
only when work concerns remaining closeout: push providers, network `IRedXeDataSource` datasets, host-owned primitive
batching, settings migration beyond version 4 reset, interactive WebView policy, or System Data follow-on hosts. Read
`Specs/Core/Core_PerformanceAndResources.md` for every plugin ABI or hot path change. Apply `spec-workflow` when
behavior or ABI changes. Launcher GPU tiles, `IRedXeInteractiveWidget`, jumbo extraction, host drop, and shortcut
persist are owned by `Specs/Plugins/Plugins_API.md` and `Specs/UI/UI_Dashboard.md`.

Preserve these boundaries:

- Keep public ABI declarations under `Common/PlugInterfaces/`; host-only code belongs under `RedXe/`, and
  bundled DLL projects belong under `Plugins/`.
- RedXe is pre-production: change the one current ABI in place and rebuild every source-coordinated host/plugin
  consumer together. Do not add old-IID, prefix, tail, major/minor, or legacy-module fallback before an explicit
  production ABI freeze. `IRedXeHost` and `IRedXeWidget` vtables MAY grow. Do not add a sibling IID only to freeze a
  vtable. Rendering and work mechanisms stay sibling `IUnknown` children. Persist is
  `IRedXeHost::PersistWidgetSettings` (complete object or a mergeable subset); collect-on-exit is
  `IRedXeWidget::CollectPersistentSettings`. Diagnostics are `IRedXeHost::Log` with `RedXeLogRecord`; there is no
  sibling log IID. There is no `IRedXeSettingsEditor`. Default collect is
  `RedXeCollectNoPersistentSettings` (`S_FALSE`, nothing to save). Plugins MUST NOT write the settings file.
- Public ABI records use a leading `sizeBytes`, require exact equality with the current `sizeof`, and do not expose STL
  containers, exceptions, WIL owners, allocators, or yyjson document pointers.
- Declare public COM contracts with the MSVC `interface __declspec(uuid(...)) __declspec(novtable) Name : IUnknown`
  form. Do not use `struct` for an interface, and do not derive rendering or service interfaces from
  `IRedXeWidget`; implementations expose sibling interfaces through `QueryInterface` with one controlling identity.
- Use `FactoryImpl.h` for bundled plugin factory behavior. Clear outputs before validation, return the contract's
  exact HRESULTs, and return one caller-owned reference on success.
- Resolve exports from an absolute DLL path with restricted `LoadLibraryExW` flags. Do not search the working
  directory or hot-unload a module.
- Release widget, host-provider, and plugin-source COM objects before optional shutdown. Keep the module mapped until
  process teardown.
- When adding or removing a settings-visible bundled widget, update `RedXe/BundledPlugins.h`, schema/parser support,
  and real placed examples in both shipped settings templates in the same change. Template validation iterates this
  catalog and must fail when either default omits the plugin. Catalog plugin IDs and type IDs stay unique; module names
  MAY repeat. `PluginHost` maps a shared DLL once and copies exports to sibling slots; optional shutdown runs only on
  the owning slot. A catalogued DLL that `LoadLibraryExW` cannot map is a per-instance placeholder, not a startup abort.
  Unknown plugin IDs remain a fatal document. Do not put a catalogued widget on the Debug or Release first page until
  its DLL is in the `Plugins` output.
- After delivering a snapshot to an active GPU data sink, `PluginHost` copies sink pointers, releases the subscription
  lock, then invokes `OnDataSnapshot`, and coalesces one UI-thread frame invalidation so scheduled GPU widgets can
  start an ease without a child HWND. A failed `CollectSnapshots` batch MUST NOT keep rate-history mutations.
- Keep every widget declaration in `Widget.h` and every data declaration in `Data.h`. Generic widgets expose GPU,
  scheduled, native-window, and raised-overlay mechanisms through sibling IIDs; never add a plugin's geometry, shader, or
  drawing commands to the generic root. The host MUST query `IRedXeRaisedWidget::GetRaisedExtent` before raising a
  non-full tile; it MUST NOT guess 1/4, 1/3, 1/2, or 1/1. Those fractions are of **client width** as a full-height
  slice over the original column. Dimmed siblings keep drawing. The content rectangle MUST NOT shrink the tile.
  `SetRaised` is UI-thread, idempotent, and allocation-free. The host sends `TRUE` when raise starts and `FALSE` when
  dismiss settle completes. `OnTargetSizeChanged` reports the final overlay size at raise start, not each interpolated
  frame.
- GPU widgets receive a borrowed D3D11 device during setup and immediate context during rendering. They never receive
  the HWND, swap chain, or back buffer. Share immutable device resources across compatible instances. A swipe
  viewport keeps the widget's full size and may have a negative origin; `Render` must still draw, and must not treat
  that origin as invalid. Direct3D clips to the target. System Data GPU
  viewers pick a density rung from the widget rectangle, grow type with leftover height among visible rows, split wide
  lists into two columns, omit content that does not fit below type floors of 16 / 18 / 30 / 48 px and a 26 px title
  floor, inset panel chrome 4 px with at least 12 px interior padding, color utilization and temperature values by
  healthy/warn/hot intent, hide network interfaces that stay at 0 B/s for eight samples, display Celsius as `°C`,
  format byte and rate values as one-decimal 1000-based KB/MB/GB/TB (or `/s`), show process working set rather than
  unlabeled PID, keep storage used/total above a used-percent-sorted capacity track, draw progress troughs near the
  panel with fill matching KPI intent color, sit network and memory bars under their text, keep thermal names above
  the level track, and join `gpu.process` names from `process.list` without adding a new dataset ID. While
  `SetRaised(TRUE)`, System Data viewers use Standard density so overlay content can show every row that `topN` allows.
  Raised System Pulse also fills leftover height with a physical-memory bar and CPU history. Weather follows
  `Specs/Plugins/Plugins_Weather.md`: configured city wins; empty city resolves once in a disposable helper and the
  host queues a UI-thread save. Location owns a line, Today appears once, and fitting hourly columns, rain/snow
  notices and future-day rows use remaining space. Fit complete glyph ink, preserve accents/icons, and ellipsize
  labels without overflow. Type remains light Segoe UI.
- Window widgets receive only a borrowed host-owned child container and own all children, timers, controllers, and GDI
  resources they create. Every widget quiesces visibility-dependent work in `IRedXeWidget::SetVisible(FALSE)`;
  window widgets destroy their children before `Detach` returns.
- Keep GPU `Render`, GDI paint, and visibility callbacks allocation-free and non-blocking. Native-window resize may
  rebuild bounded size-dependent buffers and handles, then must reuse them until size or DPI changes again.
  Process Viewer rasterizes new atlas glyphs from snapshot delivery or device creation, never from `Render`. Sample-
  driven eases call `IRedXeHost::RequestFrame` after each presented frame instead of returning a 1 ms scheduled delay.
- Use `IRedXeHost::Log` for lifetime and failure diagnostics, never for per-frame success. The call is any-thread,
  allocation-free, and forbidden from `Render` and GDI paint. `OnDataSnapshot` and `RunNetworkWork` MAY log. Factory
  create, placeholders, attach, device-create, and render-failure (once per HRESULT) are host-covered; plugins add
  their own sparse event ids such as weather `forecast-ok` / `forecast-failed`. Weather HTTP response bodies are 256 KiB
  heap buffers; do not put them on the network-worker stack.
- Keep each `CreateWidget` result isolated so multiple instance IDs can animate and configure independently.
- Every `IRedXeWidget` implements `CollectPersistentSettings`. The host calls it after `SetVisible(FALSE)` and before
  `Detach`. `S_FALSE` is the correct answer when there is nothing to save. A widget MAY call
  `IRedXeHost::PersistWidgetSettings` from `OnPointer` (committed click) or `OnDrop` with all of its instance settings
  or a part of them. The host always merges, then validates the complete object. Do not persist from `Render`,
  device, size, visibility, raise, collect, `OnDataSnapshot`, or `RunNetworkWork`.
  Worker-discovered settings may instead use the optional `IRedXeSettingsQueue`; it copies a bounded merge and posts
  UI-thread persistence outside plugin callbacks. Retain collect fallback for failed or unavailable queued delivery.
- Do not allow exceptions across exports, COM methods, callbacks, `wWinMain`, or Win32 procedures.

When a plugin project is added, include all four solution configurations, place its DLL under the host output
`Plugins` directory, and add a build-only project reference from RedXe without static import-library linkage.
`Weather.dll` load flags search only the plugin directory and System32: copy every vcpkg runtime DLL it and `libcurl`
import (`libcurl*.dll`, `yyjson.dll`, and zlib as `z.dll`/`zd.dll` or `zlib1.dll`) into `Plugins\`. Do not assume the
classic `zlib1.dll` name. Also copy `Plugins/Weather/Fonts/weathericons-regular-webfont.ttf` (Erik Flowers Weather
Icons, SIL OFL 1.1) beside `Weather.dll` so the tile can atlas-rasterize condition glyphs. `Launcher.dll` also needs
`yyjson.dll` beside it.

Run `./format.ps1`, `./test.ps1 -Configuration Debug -Platform x64 -Rebuild`,
`./test.ps1 -Configuration Release -Platform x64 -Rebuild`, an affected ARM64 build, and `./validate-skills.ps1`.
