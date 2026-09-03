# Plugin boundary and host runtime hardening

Status: `ACTIVE` — workstreams A through F, G1, G3, H1 and H2 landed 2026-09-03; G2 and H3 remain
Implemented: 2026-09-03
Created: 2026-09-03
Owner: plugin ABI surface, plugin host lifetime, host frame policy, and application decomposition

## Goal

Close the defects and contract gaps found while reviewing the main application, the bundled plugins, and the public
plugin interfaces. The review found the boundary design sound: a flat C ABI with `sizeBytes`-guarded POD records,
rendering mechanisms discovered as sibling IIDs rather than an inheritance chain, host-owned pull scheduling for data,
and event-blocked idle everywhere. The problems are concentrated in three places:

1. The plugin runtime is instantiated twice during a page swipe, which duplicates modules, sources, and the
   acquisition worker, and makes `RedXePluginShutdown` fire against a module another host still uses.
2. Almost all of the normative contract lives in `Plugins_API.md` and not in the headers a plugin author consumes.
3. `IRedXeHost` cannot do the one thing a widget most needs — ask for a frame.

Owning contracts: [`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md),
[`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md),
[`../../UI/UI_Dashboard.md`](../../UI/UI_Dashboard.md), and
[`../../Core/Core_Settings.md`](../../Core/Core_Settings.md).

Historical context:

- [`../Done/RFC_Plugins_XeneonDashboardArchitecture.md`](../Done/RFC_Plugins_XeneonDashboardArchitecture.md) — settled
  factory, widget, and local-pull decisions this plan does not reopen.
- [`../Done/PluginInterfaceConsolidation_2026-09-01.md`](../Done/PluginInterfaceConsolidation_2026-09-01.md) — the
  sibling-interface consolidation whose invariants item B3 makes structural.
- [`PluginDashboardRemainingCloseout_2026-09-02.md`](PluginDashboardRemainingCloseout_2026-09-02.md) — open
  architecture gates. Gate 1 (data services beyond local pull) and gate 3 (missing-plugin placeholders) overlap with
  workstreams A and F below; neither is resolved here.

## Scope

### Included

- Making the module store and data service process-scoped instead of per-`PluginManager`.
- Pinning every ABI record and moving the consumer-facing contract into the headers.
- One host service addition (`RequestFrame`) and one plugin diagnostics channel.
- Removing hand-rolled COM plumbing from every bundled plugin.
- Per-widget creation failure isolation.
- Compile-time bounds, `Application` decomposition, and removing self-test scaffolding from the production entry point.
- Plugin-side hygiene: `ViewerSink` back-pointer, shipped test exports, and decomposing the two oversized plugin
  translation units.

### Excluded

- Out-of-process or sandboxed plugin isolation. Widgets keep sharing the host device and immediate context.
- Push providers, network providers, or schema-based source selection. Local pull stays the only data mechanism.
- Any change to shipped dataset IDs, column freezes, row caps, the 32-subscription cap, or the WMI prohibition.
- Third-party plugin distribution, signing, or a versioned public SDK. This plan makes the headers honest; it does not
  ship them as a product.
- Reopening the raised-overlay contract, page-swipe gesture semantics, or version-4 settings syntax.

## Workstream A — process-scoped plugin host

`PluginHost` is a member of `PluginManager`. `Application::StageTransitionPage` builds a second `PluginManager` for the
adjacent page, so a swipe creates a second `PluginHost` that performs its own `LoadLibraryExW` per module, its own
`RedXeCreate` per provider, its own `IRedXeDataSource` instances, and — once a data widget subscribes — its own
acquisition `std::jthread`. Two pipelines then query the same counters, and every swipe creates and destroys one
worker thread and a set of native query objects.

| ID | Item | Pass condition |
| --- | --- | --- |
| A1 | Extract a process-scoped `PluginRuntime` owning module slots, data providers, sources, subscriptions, and the single acquisition worker. `PluginManager` holds a borrowed reference. | Staging an adjacent page creates no second `HMODULE` map, no second `IRedXeDataSource` per provider ID, and no second worker thread. |
| A2 | Both `PluginManager` instances share one `IRedXeHost`, and provider identity stays stable across a promote. | A widget on the staged page that subscribes to an already-active dataset joins the existing collection instead of starting a second one. |
| A3 | `RedXePluginShutdown` runs exactly once per module, at process teardown, after every widget, provider, and source is released. | A swipe that promotes and retires a page does not call `RedXePluginShutdown` on any module. Today the retiring host calls it on every module the surviving host still uses; it is benign only because `Plugins/StudioClock/StudioClock.cpp` shutdown clears a test-only global. |
| A4 | Module mapping is owned by the process-scoped store, so the deliberate `HMODULE` leak in `ShutdownModules` becomes one intentional process-lifetime hold instead of a per-swipe loader-refcount increment. | Loader reference count for each plugin module is independent of swipe count. |
| A5 | Measure steady-state and mid-swipe cost before and after against `Core_PerformanceAndResources.md`. | Recorded thread count, private bytes, and mid-swipe collect cadence for the `System` page improve or are unchanged; any regression is justified in the owning spec. |

`Plugins_API.md` currently states that `PluginHost` owns modules and providers "for one `PluginManager`" and that
optional shutdown "runs only on the owning slot". Both sentences change with A1 and A3.

## Workstream B — ABI header contract

| ID | Item | Pass condition |
| --- | --- | --- |
| B1 | Add a `static_assert` on `sizeof` for every public record in `Widget.h`, `Data.h`, and `Factory.h`, plus `offsetof` asserts for records containing pointers. Today only `RedXeFactoryOptions`, `RedXeDataCollectRequest`, and `RedXeDataCollectResult` are pinned; `Widget.h` has none, so a field reorder that preserves size passes the `sizeBytes` check on both sides. | Every record carrying `sizeBytes` has a matching size assert. A deliberate reorder in a scratch branch fails to compile. |
| B2 | Move the consumer-facing rules onto the declarations as doc comments: thread affinity, non-reentrancy, borrow lifetime, that a sink MUST NOT activate/deactivate/release a subscription inside `OnDataSnapshot`, and that a window widget MUST destroy its children before `Detach` returns. | A plugin author reading only `Common/PlugInterfaces/*.h` can implement a correct widget and a correct sink. `Plugins_API.md` stays authoritative and is cross-referenced, not duplicated wholesale. |
| B3 | State the GPU pipeline-state guarantee precisely in `Widget.h`. `Renderer::Render` binds only `OMSetRenderTargets` and `RSSetViewports` per callback ([`RedXe/Renderer.cpp:555`](../../../RedXe/Renderer.cpp)); blend, depth, rasterizer, **scissor rect and `ScissorEnable`**, input layout, topology, shaders, SRVs, samplers, and constant buffers all leak from one widget to the next. | The header names exactly what the host binds and states that a widget MUST bind every other state it depends on, including scissor, and MUST NOT rely on inherited state. |
| B4 | **Decision required.** Choose between (a) documenting the leak per B3 plus a Debug-only inter-widget state check, and (b) restoring state between widgets. | One option is selected with a measurement. (b) costs a per-widget rebind on the hot path and must be justified against `Core_PerformanceAndResources.md`; (a) is the presumed default and needs the Debug check to be actionable. |
| B5 | **Decision required.** `sizeBytes` is documented as an exact-match stale-binary guard, not versioning. Decide whether to keep it, adopt real forward compatibility (accept `>= sizeof(vN)` and read only the vN prefix), or replace it with one module-level `RedXeAbiVersion` export. | The chosen policy is recorded in `Plugins_API.md` and the redundant per-record cost is either justified or removed. |

## Workstream C — host services

`IRedXeHost` has exactly one method, `GetDataProvider`.

| ID | Item | Pass condition |
| --- | --- | --- |
| C1 | Add `IRedXeHost::RequestFrame()`: thread-safe, coalescing, and a no-op while the widget's page is hidden, occluded, or inactive. `PluginHost::RequestUiInvalidate` already implements exactly this behavior and is currently private and reachable only from data delivery. | A widget can request a redraw from its own state change without declaring `RedXeWidgetFlagContinuousAnimation` and without being polled through `IRedXeScheduledWidget`. Idle wake-ups do not increase for a settled page. |
| C2 | Add a bounded plugin diagnostics channel so a widget can report degradation with a reason. Today a plugin failure produces one `OutputDebugStringW` and is otherwise invisible. `RedXeDataQuality` already models availability on the data side; the widget side has no equivalent. | A widget that cannot render its content surfaces a host-rendered state instead of a silently blank or stale tile. Bounded storage; no allocation on the reporting path. |
| C3 | Record in `Plugins_API.md` that `RedXeWidgetFlagContinuousAnimation` is a property of the widget *type*, not of an instance's current state, and that C1 is the per-instance mechanism. | The two frame-request mechanisms are distinguishable from the contract alone. |

## Workstream D — plugin SDK ergonomics

Six bundled DLLs hand-roll fourteen `QueryInterface`/`AddRef`/`Release` implementations; the host adds two more.
`FactoryImpl.h` supplies factory helpers but no COM object base. The "same controlling `IUnknown`" rule is exactly what
a hand-written multi-interface `QueryInterface` gets wrong — `ViewerWidget`'s is correct only because `IRedXeWidget`
happens to be its first base — and `PluginContractTests` exists to catch that.

| ID | Item | Pass condition |
| --- | --- | --- |
| D1 | Add a `RedXeComObject<Interfaces...>` mixin to `FactoryImpl.h` providing atomic refcounting and an IID-table `QueryInterface` that always returns the same controlling `IUnknown`. | The mixin is header-only, allocation-free beyond the object itself, and adds no vtable beyond the declared interfaces. |
| D2 | Convert every bundled plugin object and both host COM objects to the mixin. | Zero hand-written `AddRef`/`Release` remain under `Plugins/`. `PluginContractTests` still passes unchanged and now guards a structural property rather than a repeated hand-written one. |

## Workstream E — settings contract honesty

`PluginManager::ValidateValueAgainstPublishedSchema` supports `object`, `integer`, `string`, and `boolean` with
`minimum`, `maximum`, and `enum`, plus exactly one `pattern` — the literal `"^#[0-9A-Fa-f]{6}$"`, matched by string
comparison. There is no `number`, no `array`, and no `required`. `Plugins_API.md` calls the schema "Draft 2020-12".

| ID | Item | Pass condition |
| --- | --- | --- |
| E1 | **Decision required.** Either narrow `Plugins_API.md` to name the supported subset exactly, or extend the validator to the subset a real plugin catalog needs. | The spec and the validator agree. A plugin publishing a construct the host cannot validate is rejected with a contract-defined result, not a bare `ERROR_INVALID_DATA`. |
| E2 | Add `required` support, or state that defaults are the only completeness guarantee. | A plugin's declared defaults cannot silently omit a field the widget then reads as absent. |
| E3 | Cache the parsed schema per plugin ID for one `StageActivePage` pass. It currently re-parses each plugin's schema once per widget instance on every page, on every settings apply. | Settings apply performs one schema parse per referenced plugin, not one per widget appearance. |

## Workstream F — per-widget failure isolation

`PluginManager::StageActivePage` is all-or-nothing: any provider-creation, type-lookup, or schema failure aborts the
whole page, and an `Initialize` failure aborts startup. Because static discovery validates *every* page, one broken
widget on page 3 prevents page 1 from starting. Render failures are already isolated per widget
([`RedXe/Renderer.cpp:564`](../../../RedXe/Renderer.cpp)); creation is not.

| ID | Item | Pass condition |
| --- | --- | --- |
| F1 | Isolate per-widget creation failure behind a host-rendered placeholder tile carrying the failing plugin ID and `HRESULT`. | A page with one unconstructable widget still composes every other widget. Startup is not aborted. |
| F2 | Keep document-level validation failures fatal, and distinguish them from per-instance failures in `Core_Settings.md` and `Plugins_API.md`. | A malformed document still fails closed with the existing preserve-and-reset behavior; a single failing instance does not. |
| F3 | Coordinate with gate 3 of [`PluginDashboardRemainingCloseout_2026-09-02.md`](PluginDashboardRemainingCloseout_2026-09-02.md), which owns missing-plugin migration policy. | The placeholder introduced here does not pre-empt that gate's decision on configured-but-unavailable plugins. |

## Workstream G — host bounds and application decomposition

| ID | Item | Pass condition |
| --- | --- | --- |
| G1 | Add `static_assert(PluginManager::kMaximumWidgetInstances <= Renderer::kMaximumWidgetViewports)`. Both are 32 today by coincidence, not by construction. `UpdateCachedViewports` fails safe if they diverge, but `Render` indexes `_widgetViewports` with a `widgetCount`-bounded loop. | Raising `kMaximumWidgetsPerPage` alone fails to compile. |
| G2 | Move page-swipe gesture state and raise state out of `Application` into `PageSwipeGesture` and `WidgetRaiseState`, beside the policy functions already extracted into `PageNavigation.h` and `WidgetRaise.h`. | `Application` owns the HWND, message routing, and runtime lifetime, as `AGENTS.md` states. Member count and file size drop materially from the current 2044 lines and ~30 members. |
| G3 | Move `Application::Run`'s `selfTest` assertions and its `#if defined(_DEBUG)` disabled-plugin probe into `HostPluginTests`, which already exercises the production host/plugin stack. | `Run` contains window creation, the message loop, and frame policy only. |
| G4 | Remove the Debug/Release behavioral divergence in `Run` — Debug never enters fullscreen and never shows the XENEON-missing prompt, so the Release startup path is the least exercised one. | Both configurations take the same startup path; any remaining difference is a diagnostic, not a behavior. Reconcile with `UI_XeneonDisplayWindowing.md`. |

## Workstream H — plugin-side hygiene

| ID | Item | Pass condition |
| --- | --- | --- |
| H1 | `ViewerSink` holds a raw `ViewerWidget*` that is never cleared. It is safe today only because `_sinks[2]` is declared before `_subscriptions[2]`, so reverse-order destruction releases subscriptions first, *and* because `RemoveSubscription` genuinely drains an in-flight `OnDataSnapshot` through its exclusive SRW acquisition. Make the invariant explicit: clear the back-pointer before releasing sinks, or state the ordering requirement at the declarations. | Neither correctness nor review depends on unstated member declaration order. |
| H2 | Gate test-only exports on a build flag. `RedXeStudioClockSetTestTime` and `RedXeStudioClockGetTestDiagnostics` are unguarded `__declspec(dllexport)` in Release, and the first overrides the displayed clock time. `ProcessViewer` and `MatrixRain` route theirs through a `REDXE_*_TEST_API` macro keyed on `REDXE_PLUGIN_EXPORTS`, which is also defined in Release. | Either the exports are absent from Release binaries, or they are specified in `Plugins_API.md` as part of the shipped ABI. |
| H3 | Decompose `Plugins/ProcessViewer/ProcessViewer.cpp` (3455 lines, 10 of the 16 catalog entries behind a `ViewerKind` switch) and `Plugins/SystemData/SystemData.cpp` (3019 lines) into per-family translation units behind the existing plugin boundary. | No plugin translation unit exceeds roughly 1200 lines. Behavior, dataset IDs, and shipped extents are unchanged; existing plugin tests pass without modification. |

## Outcome as of 2026-09-03

Landed and validated on Debug and Release x64: **A1–A5**, **B1–B5**, **C1–C3**, **D1–D2**, **E1–E3**, **F1–F3**,
**G1**, **G3**, **H1**, **H2**. Durable behavior is merged into `Plugins_API.md`, `UI_Dashboard.md`,
`Core_PerformanceAndResources.md`, and `AGENTS.md`.

Three items changed shape once the code was in front of us, and the reasons matter more than the items:

- **G4 is withdrawn as an incorrect finding.** The review called the Debug/Release divergence in `Run` accidental. It
  is not: `UI_XeneonDisplayWindowing.md` specifies it row by row — Debug never forces fullscreen and never prompts,
  Release does both. Nothing was changed. That spec also already requires the Release missing-display prompt to be
  checked manually, so the "least exercised path" concern is covered by the existing validation contract.
- **G3 kept the self-test inside `Application`.** The plan said move it into `HostPluginTests`. In practice the
  self-test drives `RegisterWindowClass`, `CreateMainWindow`, `InitializeDashboardRuntime`, and `ApplySettings`, all
  private, plus four file-local helpers. Moving it out would mean exporting Application's startup internals or
  duplicating them in the test, both worse than the problem. Instead `Run` lost its `selfTest` parameter and every
  test branch, and the validation now lives in a separate, clearly-named `Application::RunSelfTest`. `Main.cpp`
  chooses between them. The pass condition — `Run` contains window creation, the message loop, and frame policy only
  — is met.
- **F1's creation-failure branch is defensive and cannot be reached from a valid document.** Writing the test for it
  surfaced that `ValidateAppSettings` already rejects, at the document level, the cases that would make a bundled
  plugin refuse an instance (for example more than one Matrix Rain per page). The branch still earns its place for
  runtime exhaustion — memory, subscription slots — but the reachable behavior, and what
  `TestHostOwnedPlaceholderTiles` covers, is the status-driven placeholder from C2. This is recorded in
  `Plugins_API.md` rather than left as a surprise for the next reader.

Also fixed in passing: `test.ps1` could not run at all. It had three PowerShell literal errors — `12u`, and two hex
constants that PowerShell parses as signed `Int32` and then fails to convert to `[uint32]`. The whole gate was red
before this work started.

## Sequencing

1. **G1, B1** first. Compile-time guards are cheap, land independently, and protect every later change.
2. **A1–A5** next. It is the only workstream that fixes a latent defect and a measured resource cost together, and D
   and C are easier once there is one host object.
3. **D1–D2**, then **C1–C3**. The mixin reduces the diff for the host-service change across every plugin.
4. **B2–B5, E, F, G2–G4** in any order.
5. **H** last; H3 is mechanical but large and should not sit under the other diffs.

Workstreams B4, B5, and E1 are decisions. Do not implement past them without recording the choice in the owning
contract.

## Remaining

- **G2 — move page-swipe and raise state out of `Application`.** Deferred. It is a mechanical move of roughly twenty
  members across fifteen methods, with no behavioral change and no test that would catch a mistake: the pure policy
  functions are already extracted and covered, but `Application`'s state machine is not. It should be done against a
  green gate as its own change, not folded in behind eleven other workstreams.
- **H3 — split `ProcessViewer.cpp` and `SystemData.cpp`.** Deferred for the same reason at larger scale: about 6,500
  lines moving with no behavior change. Worth doing, worth doing alone.

Both keep their original pass conditions. Nothing else in this plan depends on them.

## Checklist

- [x] Land G1 and B1 as compile-time guards before any behavioral change.
- [x] Complete workstream A. `TestSharedPluginRuntime` proves that staging an adjacent page adds no second
      acquisition thread and that repeated provider lookup returns one shared identity;
      `Core_PerformanceAndResources.md` now states the process-scoped rule.
- [x] Resolve decisions B4, B5, and E1 in `Plugins_API.md`. B4: document the leak and add a Debug-only check rather
      than reset state per widget. B5: keep `sizeBytes` as an exact stale-binary guard, add compile-time size and
      offset pinning. E1: name the supported schema subset exactly, and extend the validator with `number` and
      `required` so the spec and the code agree.
- [x] Reconcile `Plugins_API.md` module/provider ownership and `RedXePluginShutdown` wording with A1 and A3.
- [x] Reconcile `UI_Dashboard.md` with the frame-request mechanism from C1 and the placeholder tile from F1.
- [x] Extend `HostPluginTests` for single-worker/single-source behavior across a staged swipe, for the new host
      services, and for host-owned placeholder tiles.
- [x] Build Debug and Release x64; `.\test.ps1` passes for both.
- [ ] Build ARM64. Not verified: `vcpkg-install.ps1` cannot install the `arm64-windows` triplet in the current
      environment. The pinned record sizes assume LLP64 with eight-byte pointers, which ARM64 Windows satisfies, so
      the asserts are expected to hold — but they are unproven there.
- [ ] `.\validate-skills.ps1`. Not run: its bundled `quick_validate.py` cannot `import yaml` in this environment.
      No skill was modified by this plan.
- [ ] Complete G2 and H3, or split them into their own dated plan.
- [ ] Move this plan to `Specs/Plans/Done/` and remove its WIP index row.

## Exit criteria

This plan is complete when one process-scoped plugin runtime owns modules, sources, subscriptions, and the acquisition
worker; `RedXePluginShutdown` runs once per module at teardown; every ABI record is size-pinned and every
consumer-facing rule is stated on its declaration; a widget can request a frame and report degradation through
`IRedXeHost`; no bundled plugin hand-writes COM plumbing; a single failing widget instance no longer fails its page;
`Application` no longer carries gesture state or self-test scaffolding; decisions B4, B5, and E1 are recorded in the
owning contract; and no requirement introduced here exists only in this file.
