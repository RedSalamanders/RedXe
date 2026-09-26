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
- Debug, Release and ASan Debug project configurations on x64 and ARM64, including real sanitizer instrumentation
- Git for the pinned vcpkg bootstrap

## Canonical project guidance

- This file is the repository-wide instruction source.
- `Specs/README.md` defines specification authority and the plan lifecycle.
- Normative product behavior lives in domain contracts under `Specs/<Domain>/`; read the owning contract before
  changing durable behavior.
- Repo-scoped skills live in `.agents/skills/` and must pass `.\validate-skills.ps1`, which runs the repository-owned
  `Build/validate_skills.py` validator. Install `Build/requirements-validation.txt` on a clean Python environment.
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
  schema/parser, its plugin contract, real placed examples in both Debug and Release settings templates, and the
  matching user-guide page under `docs/plugins/`. The only exception is a widget listed in
  `kRedXeOptInBundledWidgetIds` (today the GdiOrbit native-window example): it stays catalogued, schema-accepted,
  documented, and covered by `HostPluginTests`, but no shipped page places it because its child HWND forces composed
  presentation for the whole window. A widget listed in `kRedXeDebugOnlyBundledWidgetIds` (today the Logicon
  monitor) is placed only by the Debug template and constructed only by Debug builds; Release refuses the instance.
  Adding a service plugin requires `kRedXeBundledServices`, the `services` parser and schema, both templates, its
  plugin contract, and the `docs/plugins/` page in the same change.
- The command line is declared once in `RedXe/CommandLine.h`, the catalog that `--help` (`-h`, `/?`) prints and that
  `RedXe/Main.cpp` parses through. Adding, renaming, or removing a switch MUST update that catalog, the "Command
  line" section of `docs/usage.md`, and the owning row of the mode table in `Specs/UI/UI_XeneonDisplayWindowing.md`
  in the same change; `SettingsTests` and the `--help` step of `test.ps1` fail otherwise. Never parse a switch from
  a string literal outside the catalog.
- Screenshots for `docs/`, specs, and UI reviews MUST be generated by the application itself:
  `RedXe.exe --screenshot <png> --page <id> --widget <ordinal> [--after <ms>]` on a Debug build with a portable
  `--settings` scene (Windows.Graphics.Capture of its own window, no activation, no cursor). Never use Computer Use or
  a desktop screenshot tool, and never substitute a mockup for a live capture. The helper is `Common/WindowCapture.cpp`;
  tests capture their own windows through it.
- Mandatory performance and resource behavior is owned by
  [`Specs/Core/Core_PerformanceAndResources.md`](Specs/Core/Core_PerformanceAndResources.md).
- User settings files, schema, cold recovery, and live reload are owned by
  [`Specs/Core/Core_Settings.md`](Specs/Core/Core_Settings.md).
- Fatal-process capture, local minidumps, prior-crash UI, and the crash harness are owned by
  [`Specs/Core/Core_CrashHandling.md`](Specs/Core/Core_CrashHandling.md).
- Pinned DxUi restore, host/plugin adapters, and the COM/POD boundary are owned by
  [`Specs/Core/Core_DxUiIntegration.md`](Specs/Core/Core_DxUiIntegration.md).
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
- When that finished spec change impacts an end-user scenario, update `docs/` in the same closeout. `docs/` is the
  user guide (global usage plus one page per settings-visible widget). It is not a product-behavior authority.

## Architecture

```text
Common/PlugInterfaces/
  Factory.*        Current factory ABI, shared factory implementation, and the RedXeComObject mixin
  Host.h           Host-service COM root: data providers, frame requests, widget status, settings persist, JSONL log, and named actions (request, execute, validate)
  Widget.h         Complete generic, GPU, scheduled, child-window, raised-overlay, interactive, and network widget ABI
  Data.h           Complete source, provider, snapshot, sink, and subscription ABI
  Service.h        Headless service ABI: start/apply/host-state/stop and the host-owned device lane worker
  Action.h         Action publication ABI: descriptors, namespaces, RedXeGetActionContract, IRedXeActionPack, name grammar
Common/Actions/
  ActionTargets.*  Shared target grammars (paths, chords, points, monitors, windows, meetings) compiled into the host and every publisher
  WindowSelector.* Top-level window selection and foregrounding
  FluentGlyphNames.h, GlyphIcon.*  Segoe Fluent Icons name table and DirectWrite glyph rasterization shared by Logicon faces and Launcher tiles
Plugins/
  RotatingTriangle/ First bundled widget-provider DLL
  GdiOrbit/         Double-buffered GDI window-widget DLL
  MatrixRain/       Production low-resource Direct3D digital-rain DLL
  5H4D3R5/          Build-time HLSL shader and demo catalog (twelve Shadertoy ports, the RedXe cosmic orb, Hillaire's sky atmosphere): single, random, or fading slideshow; ShadersSettings.h is the shared catalog/settings model; LICENSES.md the notices
  ProcessViewer/    System Data GPU viewers sharing one Direct3D DLL
  Launcher/         GPU shortcut launcher with jumbo icons, glyph tiles, taskbar-pin fallback, and action bindings
  Weather/          GPU weather widget with host-owned network lane
  AVControl/        DxUi retained controls, isolated audio/camera helper, profiles and virtual-camera source
  Logicon/          Headless MX Creative Console keypad and dialpad service (HID++ and Raw Input over the device lane, key faces, the `logicon` action namespace) plus the Debug monitor tile and the Probe tool
  Actions/Zoom/     zoom.action.dll: browser-only service publishing `zoom.open` and `zoom.join`; no Zoom installation, SDK, OAuth, or Marketplace application registration
RedXeLauncher/
  Main.cpp          Dependency-free shim behind the winget `RedXe` alias: resolves its final path, starts the package-root RedXe.exe
Installer/
  Install-RedXe.ps1 In-package installer (copy, Start Menu, Apps entry, start at sign-in, remove); install.cmd / uninstall.cmd wrap it
  winget/templates/ RedSalamanders.RedXe manifest templates (schema 1.12.0, zip + nested portable launcher)
RedXe/
  Main.cpp          Process setup and command-line modes
  CommandLine.h     Command-line switch catalog: --help text, names Main.cpp parses through, unknown-token scanner
  Application.*     Win32 window and message-loop lifetime
  CrashHandler.*    Fatal-process front door, local minidumps/call stacks, and prior-crash notice
  PluginHost.*      Process plugin runtime: module store, data providers, workers, JSONL log, and settings persist
  PluginManager.*   Widget providers and instance lifetime
  HostActionCatalog.* Hardcoded default action namespaces (page, widget, redxe, system, keys, mouse) and name checks
  HostActions.*     Win32 execution of the system, keys, and mouse namespaces; counters for automated hosts
  BundledPlugins.h  Compile-time module, widget, service, and action-namespace catalogs
  DashboardHost.*   Widget placement, frame-scheduling policy, and collect-on-exit
  Renderer.*        Direct3D 11 host resources, widget callbacks, placeholder tiles, and frames
  Settings.*        Typed yyjson persistence, paths, recovery, stamps, and persist merge
  SettingsWatcher.* Event-blocked directory notification; posts to the UI thread only
  FluentIcons.h     Segoe Fluent Icons glyphs and font selection for all host chrome
  app.manifest      Per-monitor-v2 DPI and Windows compatibility metadata
Tests/
  PluginContractTests/ Factory, COM identity, and rendering-IID tests
  HostPluginTests/     Hidden WARP production host/plugin integration and soak tests; embeds a Windows 8+ compatibility manifest so layered native containers are legal
  SettingsTests/       Settings, schema, stamp, watcher, and log-retention tests
  LauncherTests/       Launcher factory, pin fallback, WARP, launch, and drop tests
  WeatherTests/        Weather HTTP heap-body and small-stack overflow regression
  AVControlTests/      Synthetic AV/IPC/MF faults, native controls, camera packaging and bounded control work
  LogiconTests/        HID++ framing, image stream, settings model, faces, synthetic keypad and dialpad sessions, raw-input helpers, and the shipped service DLL
  ZoomTests/           Browser URL validation, empty settings model, and the shipped Zoom service/action DLL
  BuildProcessTests/   Build preflight, DxUi provenance/update, and packaging (version, ZIP rules, winget manifest, in-package installer round-trip)
Build/
  Versioning.psm1   major.minor from Common/Version.h plus the caller's build number
  Package.psm1      Portable ZIP staging rules, CRT bundling, and the clean-extraction smoke
  Winget.psm1       Manifest generation from Installer/winget/templates and `winget validate`
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
docs/
  README.md         End-user guide index
  usage.md          Global usage
  plugins/          One page per settings-visible bundled widget
```

Keep the boundary explicit:

- `Application` owns the HWND and translates messages into narrow operations.
- `CrashHandler` owns fatal-process registration and best-effort local artifacts; it creates no background work and
  never uploads dumps.
- `SettingsStore` owns typed settings validation, user/deployed paths, the sibling Logs directory path, cold recovery,
  and stamp deduplication.
- `SettingsWatcher` owns one event-blocked directory watcher and only posts a coalesced UI message; settings and
  dashboard mutation remain on `Application`'s UI thread.
- `PluginHost` is process scoped. One instance owns every mapped module, every data source, the single acquisition
  worker, the optional network worker, the started services with their device-lane threads, the host action ring,
  and the JSONL log writer for the whole application, including the dashboard page staged during a swipe. Optional
  `RedXePluginShutdown` runs once per module at process teardown.
- A service (`Service.h`) runs without a placed widget: `Application` starts it after the first page is live, feeds it
  host state, drains its host actions on the UI thread, and stops it before the process runtime shuts down. A
  service's device I/O runs only on its host-owned lane, never on a plugin-created thread.
- `PluginManager` borrows that runtime and owns only provider/widget COM references for one page. A per-instance
  construction failure becomes a host-drawn placeholder tile; it does not fail the page.
- `DashboardHost` owns design-canvas placements, native child containers, and frame-scheduling policy.
- `Renderer` owns host COM graphics resources, cached viewports, device notifications, and presentation; it has no
  message-dispatch or plugin-specific drawing logic.
- The ABI headers carry the consumer-facing contract on the declarations: thread affinity, reentrancy limits, borrow
  lifetime, and the GPU pipeline-state guarantee. Every public record is pinned by `sizeof` and, when it carries a
  pointer, by `offsetof` assertions.
- `Widget.h` owns every widget declaration. Widgets expose supported GPU, scheduled, native-window, or raised-overlay
  mechanisms as sibling COM interfaces queried by IID.
- GPU widgets receive the borrowed D3D11 device during setup and immediate context during rendering, but never the
  HWND, swap chain, or back buffer. The host binds only render target and viewport before each callback, so a widget
  binds every other state it depends on, including scissor state.
- A GPU widget rebuilds resolution-dependent resources in `OnTargetSizeChanged`, which the host calls only when the
  largest viewport it will draw that widget at actually changes. That is the one GPU callback allowed to rasterize,
  create textures, or allocate; `Render` stays allocation-free.
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
.\package.ps1 -Platform x64                     # portable ZIP stamped 1.0.<commit count>, smoke-tested from a clean extraction
.\winget-manifest.ps1 -Version 1.0.<n>          # winget manifest from both platform packages
```

The version is `major.minor` from `Common/Version.h` plus the build number: `-BuildNumber` when given,
otherwise the commit count of HEAD, which the release workflow stamps too (one commit, one version). Packaging, the in-package installer, the `RedXe` alias launcher, and the
release and winget workflows are owned by [`Specs/Build/Build_Packaging.md`](Specs/Build/Build_Packaging.md).

`build.ps1` rejects only a running `RedXe.exe` whose normalized executable path exactly matches the selected
`.build/<Platform>/<Configuration>/RedXe.exe`. It MUST identify that process and MUST NOT terminate it; same-name
processes from other paths do not block the build.

Before declaring a change complete, build the affected configuration, run `test.ps1`, and satisfy the validation
contract in the owning domain spec. Rendering changes must keep the WARP smoke test green so CI and GPU-independent
hosts can validate device creation, embedded shader bytecode, resize, drawing, and presentation.
`test.ps1` also validates crash capture by launching an isolated child process; it requires no desktop automation and
must not write to the user's normal crash directory.

## Host chrome iconography

- Every icon in host-drawn chrome comes from `RedXe/FluentIcons.h`. Do not hand-draw arrows, glyphs, or symbols with
  quads or path geometry: font glyphs are hinted, scale correctly with DPI, and match the Windows 11 shell, which
  hand-drawn shapes do not.
- The font order is Segoe Fluent Icons, then Segoe MDL2 Assets for older builds, then a standard Unicode stand-in in
  the normal UI font. Every glyph constant MUST have a Unicode fallback so chrome never renders a missing-glyph box.
  `FluentIcons::ResolveIconFamily` selects the family from the DirectWrite system font collection and reports which
  glyph set applies; `FluentIcons::SelectGlyph` picks the matching code point.
- Add new glyphs to `FluentIcons.h` and to the `HostChromeGlyph` atlas slots in `RedXe/HostChrome.h` rather than
  inline in a draw routine, so the icon set stays reviewable in one place.
- Host chrome is Direct3D: `RedXe/HostChrome.*` rasterizes the glyph atlas with DirectWrite once per DPI and draws
  every band, dim, shadow, and close quad into the swap chain. There is no chrome HWND and no GDI in the host;
  [`Specs/Core/Core_PerformanceAndResources.md`](Specs/Core/Core_PerformanceAndResources.md) owns the budget.

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
