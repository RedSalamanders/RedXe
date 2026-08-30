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

## Skills

| Skill | Use it for |
| --- | --- |
| [spec-workflow](.agents/skills/spec-workflow/SKILL.md) | Normative specs, reconciliation, active plans, and closeout |
| [build-redxe](.agents/skills/build-redxe/SKILL.md) | Building, cleaning, running, and smoke-testing |
| [direct3d11-rendering](.agents/skills/direct3d11-rendering/SKILL.md) | Device, swap-chain, pipeline, rendering, resize, and device loss |
| [plugin-development](.agents/skills/plugin-development/SKILL.md) | Native plugin ABI, factories, loading, widget instances, and bundled DLLs |
| [win32-windowing](.agents/skills/win32-windowing/SKILL.md) | Window creation, message routing, DPI, and lifetime |
| [modern-cpp-windows](.agents/skills/modern-cpp-windows/SKILL.md) | C++ ownership, HRESULT handling, warnings, and source style |
| [wil-raii](.agents/skills/wil-raii/SKILL.md) | Windows handles, COM interfaces, and unconditional cleanup |
| [yyjson](.agents/skills/yyjson/SKILL.md) | JSON parsing, writing, document lifetime, and string ownership |

## Specification workflow

- Start with [`Specs/README.md`](Specs/README.md), then read the owning domain spec. XENEON display, window, fallback,
  fullscreen, and DPI behavior is owned by
  [`Specs/UI/UI_XeneonDisplayWindowing.md`](Specs/UI/UI_XeneonDisplayWindowing.md).
- Native factory, standard widget, bundled plugin, and plugin-lifetime behavior is owned by
  [`Specs/Plugins/Plugins_API.md`](Specs/Plugins/Plugins_API.md).
- Domain specs describe current behavior. `Specs/Plans/WIP/` is non-normative active work and
  `Specs/Plans/Done/` is historical context.
- Small settled changes may update the spec, implementation, and validation directly. Multi-step, risky, or undecided
  work requires one indexed WIP plan naming the domain specs it expects to change.
- A change is not complete while durable requirements exist only in code, tests, commentary, or a plan. Merge them
  into the authoritative domain spec during closeout.
- When a plan completes, move it from WIP to Done and remove it from the active index only after required validation
  passes and the normative contract is current.

## Architecture

```text
Common/PlugInterfaces/
  Factory.*        Stable factory ABI and shared factory implementation
  Host.h           Host-service COM root
  Widget.h         Standard widget ABI and frame commands
Plugins/
  RotatingTriangle/ First bundled widget-provider DLL
src/RedXe/
  Main.cpp          Process setup and command-line modes
  Application.*     Win32 window and message-loop lifetime
  PluginManager.*   Plugin loading, providers, instances, and placements
  Renderer.*        Direct3D 11 device, swap chain, pipeline, and frames
  Settings.*        yyjson-backed application settings
  app.manifest      Per-monitor-v2 DPI and Windows compatibility metadata
Specs/
  README.md         Specification authority and plan workflow
  Plugins/          Normative native plugin and widget behavior
  UI/               Normative display and windowing behavior
  Plans/WIP/        Non-normative active plans
  Plans/Done/       Historical completed plans
```

Keep the boundary explicit:

- `Application` owns the HWND and translates messages into narrow operations.
- `PluginManager` owns plugin modules, provider/widget COM references, and design-canvas placements.
- `Renderer` owns all COM graphics resources and has no message-dispatch logic.
- Standard plugins emit validated commands and never receive the HWND, D3D device/context, swap chain, or back buffer.
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

Before declaring a change complete, build the affected configuration, run `test.ps1`, and satisfy the validation
contract in the owning domain spec. Rendering changes must keep the WARP smoke test green so CI and GPU-independent
hosts can validate device creation, shader compilation, resize, drawing, and presentation.

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
