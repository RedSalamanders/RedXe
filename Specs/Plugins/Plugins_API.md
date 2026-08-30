# RedXe plugin API contract

Status: current normative contract
Last reviewed: 2026-08-30

## Purpose and current scope

This contract owns RedXe native plugin discovery, factory behavior, standard widget instances, and the first bundled
plugin. The current executable milestone supports one trusted in-process widget-provider DLL and one standard drawing
command. It deliberately does not freeze the future data-provider, advanced D3D11, native-window, configuration, or
dashboard-editing interfaces described by the active architecture RFC.

The published ABI lives under `Common/PlugInterfaces/`. Consumers include RedXe, bundled plugins, third-party plugin
projects, and ABI tests. Published interface IIDs and vtables are immutable; a breaking revision receives a new IID.

## Binary discovery and lifetime

- RedXe loads the bundled `Plugins/RotatingTriangle.dll` from the executable directory using its absolute path and
  `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`.
- The current host does not scan the working directory or accept custom plugin paths.
- `RedXeCreate` is required. `RedXeEnumeratePlugins` and `RedXePluginShutdown` are optional in the general ABI; the
  bundled plugin exports enumeration and has no shutdown work.
- Factory, enumeration, widget creation, and frame construction run synchronously on the RedXe UI/render thread and
  are non-reentrant.
- Widgets and providers are released before shutdown. Loaded plugin modules remain mapped until process teardown.
- Exceptions must not cross an export, COM method, callback, `wWinMain`, or Win32 callback boundary.

## Factory contract

`Factory.h` defines `RedXeFactoryOptions`, `RedXePluginMetadata`, capability flags, export names, and function-pointer
types. `FactoryImpl.h` is the shared implementation used by bundled plugins.

The current interface identifiers are:

| Interface | IID |
| --- | --- |
| `IRedXeHost` | `053E6CDF-0238-4B69-BC80-F173D9EB93D1` |
| `IRedXeWidgetTypeSink` | `027D19CE-187E-486C-AEA3-B8B4F75D3F80` |
| `IRedXeFrameBuilder` | `45B1E983-0F49-4F1B-ACC2-90E2FF19C3FE` |
| `IRedXeWidget` | `6E4C2E10-D546-4A1C-A478-068EAB2B02E5` |
| `IRedXeWidgetProvider` | `436A7DF3-DB73-442B-99FE-63837F24BA75` |

Factory behavior is normative:

- a null output pointer returns `E_POINTER`;
- every output is cleared before other validation;
- an unsupported IID returns `E_NOINTERFACE`;
- a null or empty plugin ID is accepted only when the module exposes one logical plugin;
- an unknown non-empty plugin ID returns `HRESULT_FROM_WIN32(ERROR_NOT_FOUND)`;
- an undersized options record returns `E_INVALIDARG`, while larger records are accepted;
- successful creation returns one caller-owned COM reference;
- enumeration returns 1–256 contiguous module-owned metadata records whose strings remain valid while the module is
  mapped.

RedXe requests only IIDs it implements. A future host requests the newest supported IID and retries a named older IID
only when the first call returns `E_NOINTERFACE`; versions and structure sizes never negotiate vtables.

## Standard widget contract

`IRedXeWidgetProvider::EnumerateWidgetTypes` synchronously sends borrowed descriptors to the supplied sink.
`CreateWidget` creates an isolated instance from a stable type ID and non-empty instance ID. Each successful call
returns a distinct object with independent animation state.

`IRedXeWidget::BuildFrame` receives:

- the widget-local physical width and height;
- the destination window DPI;
- process-relative elapsed time and frame delta time;
- a borrowed `IRedXeFrameBuilder` valid only for the synchronous call.

The widget must not retain the frame context or builder. `BuildFrame` must not perform disk, network, display,
hardware-discovery, process-launch, or long-lock work.

The initial command is `RedXeTriangleCommand`. It contains three widget-local normalized vertices. Each position
component is finite and in `[-1, 1]`; each RGBA component is finite and in `[0, 1]`. The host validates the complete
command before updating its dynamic D3D11 vertex buffer. Invalid commands fail that widget frame and do not stop
remaining widget instances.

The standard path never gives a plugin an HWND, D3D device, device context, render target, swap chain, or back buffer.
RedXe owns shaders, buffers, per-widget viewports, resize, device-loss recovery, composition, WARP behavior, and
presentation.

## First bundled plugin

`Plugins/RotatingTriangle` exposes logical plugin ID `builtin.rotating-triangle`, widget type ID
`rotating-triangle`, and the widget-provider capability.

The default embedded settings create four instances. RedXe lays them out in a two-column grid on the 2560×720 design
canvas and scales their viewports to the current DPI-correct client surface. Every instance:

- computes rotation in the plugin from elapsed time;
- has a deterministic speed, direction, phase, and RGB ordering derived from its instance ID;
- compensates for its local canvas aspect ratio so the triangle is not stretched;
- emits one triangle per requested frame;
- declares continuous animation.

The host accepts 2–8 instances from its validated settings, with four as the shipped default.

## Ownership and failure

- `Application` owns the window, display selection, DPI behavior, message loop, `PluginManager`, and `Renderer`.
- `PluginManager` owns module metadata, provider references, widget references, and design-canvas placements.
- `Renderer` owns all Direct3D and DXGI resources and the concrete standard frame builder.
- A failed plugin load or required-provider creation fails startup with diagnostics.
- A failed widget frame is diagnosed and skipped for that frame; other instances and presentation continue.
- Direct3D device-loss results rebuild all host graphics resources. Standard widget objects survive because they own no
  device resources.

## Required validation

Before changing this contract or its implementation:

1. run `./format.ps1` and `./validate-skills.ps1`;
2. run the Debug x64 WARP self-test and confirm the plugin DLL is loaded from the output `Plugins` directory;
3. run the Release x64 WARP self-test;
4. build Release ARM64 and confirm the host and plugin both compile for ARM64;
5. confirm the default host creates four widget instances and a frame from each reaches the standard builder;
6. keep `/W4`, `/permissive-`, SDL checks, and warnings-as-errors green.

Live visual validation on a XENEON EDGE should confirm four independently rotating, aspect-correct triangles within
the DPI-correct 2560×720 canvas.
