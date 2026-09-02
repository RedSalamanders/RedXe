# RedXe Development Guidelines

## Project overview

RedXe is the native Windows foundation for the XENEON EDGE dashboard, built with Win32, Direct3D 11, DXGI, WIL,
yyjson, and modern C++. WIL and yyjson are pinned through the repository vcpkg manifest.

## Requirements

- Windows 10 or Windows 11
- Visual Studio 2026 (18.x), toolset `v145`, with the Desktop development with C++ workload
- Windows 11 SDK 10.0.26100
- MSVC in `stdcpplatest` mode
- Unicode Win32 APIs
- x64 and ARM64 project configurations
- Git for the pinned vcpkg bootstrap

## Canonical project guidance

- This file is the repository-wide instruction source.
- `Specs/README.md` defines specification authority and the plan lifecycle.
- Normative product behavior lives in domain contracts under `Specs/<Domain>/`; read the owning contract before
  changing durable behavior.
- Repo-scoped skills live in `.agents/skills/` and must pass `.\validate-skills.ps1`, which runs the bundled
  `quick_validate.py` validator.
- Keep reusable build entrypoints at the repository root.
- Generated build products belong under `.build/`; never commit them.

## Mandatory performance and resource policy

- [`Specs/Core/Core_PerformanceAndResources.md`](Specs/Core/Core_PerformanceAndResources.md) applies to every design,
  implementation, review, and validation task.
- Performance and low resource consumption are primary requirements. Among correct designs, prefer the one with the
  lowest steady-state CPU, memory, allocation, copy, synchronization, wake-up, and GPU-submission cost.
- Steady-state hot paths must use bounded reusable storage and avoid heap allocation. Cache derived state and batch
  compatible work.
- Idle, hidden, minimized, suspended, and occluded execution must block on events; busy polling is prohibited.
- Do not trade correctness, security, visual quality, or required behavior for an unmeasured micro-optimization.
- Any intentional resource regression requires measurement and explicit justification in the owning spec or WIP plan.

## Skills

| Skill | Use it for |
| --- | --- |
| [spec-workflow](.agents/skills/spec-workflow/SKILL.md) | Normative specs, reconciliation, active plans, and closeout |
| [build-redxe](.agents/skills/build-redxe/SKILL.md) | Building, cleaning, running, and smoke-testing |
| [direct3d11-rendering](.agents/skills/direct3d11-rendering/SKILL.md) | Device, swap-chain, pipeline, rendering, resize, and device loss |
| [plugin-development](.agents/skills/plugin-development/SKILL.md) | Native plugin ABI, factories, loading, widget instances, and bundled DLLs |
| [performance-resources](.agents/skills/performance-resources/SKILL.md) | CPU, memory, allocation, batching, idle, and resource-budget decisions |
| [win32-windowing](.agents/skills/win32-windowing/SKILL.md) | Window creation, message routing, DPI, and lifetime |
| [modern-cpp-windows](.agents/skills/modern-cpp-windows/SKILL.md) | C++ ownership, HRESULT handling, warnings, and source style |
| [wil-raii](.agents/skills/wil-raii/SKILL.md) | Windows handles, COM interfaces, and unconditional cleanup |
| [yyjson](.agents/skills/yyjson/SKILL.md) | JSON parsing, writing, document lifetime, and string ownership |
| [settings-store](.agents/skills/settings-store/SKILL.md) | Settings paths, schema, recovery, stamps, watching, and live apply |

## Specification workflow

- Start with [`Specs/README.md`](Specs/README.md), then read the owning domain spec. XENEON display, window, fallback,
  fullscreen, and DPI behavior is owned by
  [`Specs/UI/UI_XeneonDisplayWindowing.md`](Specs/UI/UI_XeneonDisplayWindowing.md).
- Native factory, generic widget mechanisms, bundled plugin, and plugin-lifetime behavior is owned by
  [`Specs/Plugins/Plugins_API.md`](Specs/Plugins/Plugins_API.md).
- Adding or removing a settings-visible bundled widget requires one aligned change to `RedXe/BundledPlugins.h`, the
  schema/parser, its plugin contract, and real placed examples in both Debug and Release settings templates.
- Mandatory performance and resource behavior is owned by
  [`Specs/Core/Core_PerformanceAndResources.md`](Specs/Core/Core_PerformanceAndResources.md).
- User settings files, schema, cold recovery, and live reload are owned by
  [`Specs/Core/Core_Settings.md`](Specs/Core/Core_Settings.md).
- Fatal-process capture, local minidumps, prior-crash UI, and the crash harness are owned by
  [`Specs/Core/Core_CrashHandling.md`](Specs/Core/Core_CrashHandling.md).
- Dashboard pages, the placement grid, active-page composition, and widget instance layout are owned by
  [`Specs/UI/UI_Dashboard.md`](Specs/UI/UI_Dashboard.md).
- Domain specs describe current behavior. `Specs/Plans/WIP/` is non-normative active work and
  `Specs/Plans/Done/` is historical context.
- Small settled changes may update the spec, implementation, and validation directly. Multi-step, risky, or undecided
  work requires one indexed WIP plan naming the domain specs it expects to change.
- A change is not complete while durable requirements exist only in code, tests, commentary, or a plan. Merge them
  into the authoritative domain spec during closeout.
- When every plan item is implemented, its tests and required validation pass, and its durable behavior is persisted
  in the normative contracts, the plan MUST be moved from WIP to Done and removed from the active index. A completed
  plan MUST NOT remain under `Specs/Plans/WIP/`.

## Architecture

```text
Common/PlugInterfaces/
  Factory.*        Current factory ABI and shared factory implementation
  Host.h           Host-service COM root
  Widget.h         Complete generic, GPU, scheduled, child-window, and raised-overlay widget ABI
  Data.h           Complete source, provider, snapshot, sink, and subscription ABI
Plugins/
  RotatingTriangle/ First bundled widget-provider DLL
  GdiOrbit/         Double-buffered GDI window-widget DLL
  MatrixRain/       Production low-resource Direct3D digital-rain DLL
  ProcessViewer/    System Data GPU viewers sharing one Direct3D DLL
RedXe/
  Main.cpp          Process setup and command-line modes
  Application.*     Win32 window and message-loop lifetime
  CrashHandler.*    Fatal-process front door, local minidumps/call stacks, and prior-crash notice
  PluginHost.*      Shared module loading and host-managed data providers
  PluginManager.*   Widget providers and instance lifetime
  DashboardHost.*   Widget placement and frame-scheduling policy
  Renderer.*        Direct3D 11 host resources, widget callbacks, and frames
  Settings.*        Typed yyjson persistence, paths, recovery, and file stamps
  SettingsWatcher.* Event-blocked directory notification; posts to the UI thread only
  app.manifest      Per-monitor-v2 DPI and Windows compatibility metadata
Tests/
  PluginContractTests/ Factory, COM identity, and rendering-IID tests
  HostPluginTests/     Hidden WARP production host/plugin integration and soak tests
  SettingsTests/       Settings, schema, stamp, and watcher tests
Settings/
  RedXe-debug.settings.json  Shipped Debug default
  RedXe.settings.json        Shipped Release default
Specs/
  README.md         Specification authority and plan workflow
  Core/             Normative cross-cutting performance and resource behavior
  Plugins/          Normative native plugin and widget behavior
  UI/               Normative display and windowing behavior
  Plans/WIP/        Non-normative active plans
  Plans/Done/       Historical completed plans
```

Keep the boundary explicit:

- `Application` owns the HWND and translates messages into narrow operations.
- `CrashHandler` owns fatal-process registration and best-effort local artifacts; it creates no background work and
  never uploads dumps.
- `SettingsStore` owns typed settings validation, user/deployed paths, cold recovery, and stamp deduplication.
- `SettingsWatcher` owns one event-blocked directory watcher and only posts a coalesced UI message; settings and
  dashboard mutation remain on `Application`'s UI thread.
- `PluginManager` owns plugin modules and provider/widget COM references.
- `DashboardHost` owns design-canvas placements, native child containers, and frame-scheduling policy.
- `Renderer` owns host COM graphics resources, cached viewports, device notifications, and presentation; it has no
  message-dispatch or plugin-specific drawing logic.
- `Widget.h` owns every widget declaration. Widgets expose supported GPU, scheduled, native-window, or raised-overlay
  mechanisms as sibling COM interfaces queried by IID.
- GPU widgets receive the borrowed D3D11 device during setup and immediate context during rendering, but never the
  HWND, swap chain, or back buffer.
- A window widget receives only a host-owned child container, never the top-level HWND, and destroys all plugin-owned
  children before detach returns.
- Device-independent state survives swap-chain recreation; device resources are rebuilt together after device loss.

## Build and validation

```powershell
.\vcpkg-install.ps1 -Platform All
.\build.ps1
.\build.ps1 -Configuration Release
.\build.ps1 -Platform ARM64
.\build.ps1 -Rebuild
.\build.ps1 -Run
.\test.ps1
.\validate-skills.ps1
```

`build.ps1` rejects only a running `RedXe.exe` whose normalized executable path exactly matches the selected
`.build/<Platform>/<Configuration>/RedXe.exe`. It MUST identify that process and MUST NOT terminate it; same-name
processes from other paths do not block the build.

Before declaring a change complete, build the affected configuration, run `test.ps1`, and satisfy the validation
contract in the owning domain spec. Rendering changes must keep the WARP smoke test green so CI and GPU-independent
hosts can validate device creation, embedded shader bytecode, resize, drawing, and presentation.
`test.ps1` also validates crash capture by launching an isolated child process; it requires no desktop automation and
must not write to the user's normal crash directory.

## C++ and Win32 rules

- Use WIL `wil::com_ptr_nothrow<T>` for COM ownership in HRESULT-based code and the matching WIL unique wrapper for
  owned Windows resources.
- Do not manually release or destroy an owned resource; reset its WIL owner and make cleanup idempotent.
- Keep the static window procedure small. Bind the instance at `WM_NCCREATE`, then route to `Application::HandleMessage`.
- Do not allow exceptions to cross `wWinMain` or a Win32 callback boundary.
- Use `HRESULT` for DirectX and Windows failures. Handle expected device-loss codes by rebuilding resources.
- Treat zero-sized `WM_SIZE` as suspension, not an error.
- Follow `UI_XeneonDisplayWindowing.md` for `WM_DPICHANGED`: fullscreen popups use the suggested monitor rectangle;
  titled windows use its destination position and recalculate the outer frame for the exact DPI-adjusted client
  canvas.
- Do not render from `WM_PAINT`; this real-time app renders from the idle side of the message loop.
- Keep `/W4`, `/permissive-`, SDL checks, and warnings-as-errors green for project code.
- Prefer targeted local warning suppression only when an SDK or tool header requires it, with a reason beside it.
- Treat `yyjson_val*` and strings returned by yyjson as borrowed from their document. Use WIL RAII for documents and
  copy dynamic strings into mutable documents.
- Public COM contracts use `interface __declspec(uuid(...)) __declspec(novtable) Name : IUnknown`. Do not declare
  them with `struct`, and do not use interface inheritance to couple generic widget identity to a rendering mechanism.
- Keep `Settings/`, `Specs/Settings.schema.json`, `Settings.*`, `Core_Settings.md`, and `SettingsTests` aligned. A
  settings change is incomplete if any one of these still describes the old document.
