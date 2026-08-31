# RedXe plugin API contract

Status: current normative contract
Last reviewed: 2026-08-31

## Purpose and scope

This contract owns RedXe native plugin discovery, generic widget creation, rendering-interface negotiation, GPU-widget
lifetime, and the first bundled plugin.

Public ABI headers live under `Common/PlugInterfaces/`. `Widget.h` is deliberately rendering-neutral: it contains no
shape, shader, drawing command, HWND, or device-specific implementation. A created widget exposes the mechanisms it
supports through `QueryInterface`. New mechanisms and breaking revisions use new IIDs rather than expanding the
generic widget root.

The current executable host renders `IRedXeGpuWidget`. `WindowWidget.h` is an experimental prototype for GDI, native
controls, media hosts, and WebView implementations. Its IID and vtable are not frozen until host-owned child
containers are implemented and promoted from the active dashboard RFC.

The mandatory requirements in `Specs/Core/Core_PerformanceAndResources.md` apply to every plugin and host path.

## Public interfaces

| Header | Interface | IID | Purpose |
| --- | --- | --- | --- |
| `Host.h` | `IRedXeHost` | `053E6CDF-0238-4B69-BC80-F173D9EB93D1` | Empty host-service query root |
| `Widget.h` | `IRedXeWidget` | `2C66DE33-08D1-4A0C-890C-38521F142AA0` | Generic widget identity and lifetime |
| `Widget.h` | `IRedXeWidgetProvider` | `231AC0E8-1204-4BFF-BCEA-7CACF11F439D` | Type enumeration and instance creation |
| `GpuWidget.h` | `IRedXeGpuWidget` | `DBEED29C-63EB-409E-816B-F4BDC5EF7AA9` | Direct3D 11 rendering mechanism |

`WindowWidget.h` is present for the active RFC only. Third-party plugins MUST NOT treat it as a stable ABI, and the
current executable rejects window-only instances with `HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)`.

`IRedXeWidget` adds no methods to `IUnknown`. Rendering-mechanism interfaces derive from that root, so a plugin that
supports one mechanism implements one straightforward COM interface. Its value is one stable identity from which a
host asks for the newest rendering or service IID it understands. An object implementing several mechanisms must
return the same controlling `IUnknown` identity from each one.

A published IID has an immutable vtable and semantics. Compatibility negotiation is newest-IID first and falls back
only after `E_NOINTERFACE`. Structure size does not negotiate a vtable.

## Identifiers and strings

- Plugin IDs, widget type IDs, and widget instance IDs are stable UTF-8 ASCII strings.
- IDs contain 1–128 characters, start with an ASCII alphanumeric character, and otherwise use only
  `[A-Za-z0-9_.-]`.
- IDs compare case-insensitively using ASCII ordinal rules. The host persists canonical plugin spelling.
- Localized display names and descriptions are borrowed UTF-16 Windows strings.
- Borrowed module strings and descriptor arrays remain valid while the module is mapped.

## Binary discovery and lifetime

- RedXe loads DLLs by absolute path using
  `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`; it never searches the working directory.
- `RedXeCreate` is required. `RedXeEnumeratePlugins` and `RedXePluginShutdown` are optional.
- A module without `RedXeEnumeratePlugins` is treated as one logical plugin and receives a null plugin ID during
  creation.
- Factory, enumeration, widget creation, device notification, and GPU rendering are synchronous and non-reentrant in
  v1.
- Rendering interfaces and generic widgets are released before providers, optional shutdown, and process teardown.
- Modules remain mapped until process teardown. Exceptions must not cross ABI or Win32 boundaries.

## Factory contract

`Factory.h` declares export signatures, aliases, metadata, helpers, and names. `FactoryImpl.h` supplies shared bundled
plugin factory behavior.

- A null output returns `E_POINTER`; a present output is cleared before other validation.
- Unsupported IIDs return `E_NOINTERFACE`.
- A null or empty plugin ID is accepted only for a single-plugin module.
- Unknown non-empty IDs return `HRESULT_FROM_WIN32(ERROR_NOT_FOUND)`.
- Factory options smaller than the immutable `kRedXeFactoryOptionsV1Size` eight-byte prefix return `E_INVALIDARG`.
  Exactly that prefix and larger records are accepted without reading their tail.
- Successful creation returns one caller-owned COM reference.
- Enumeration returns 1–256 contiguous module-owned metadata records.
- Malformed metadata, duplicate IDs, or missing required capability fail module discovery safely.

The metadata capability surface advertises logical plugin services, currently only
`RedXePluginCapabilityWidgetProvider`. Rendering mechanisms are discovered on widget instances by IID, not by a
capability or rendering-path enum.

## Generic widget provider

`IRedXeWidgetProvider::GetWidgetTypes` returns a module-owned immutable descriptor array and count. Both outputs are
cleared before validation. A descriptor contains stable identity, localized labels, design-canvas size hints, and
generic scheduling flags only.

`CreateWidget` takes a type ID and non-empty instance ID and returns `IRedXeWidget`. Every successful call creates a
distinct stateful instance. The host then queries that object for supported rendering interfaces. A widget may expose
more than one mechanism; host policy chooses one without changing `Widget.h`.

## GPU widget contract

`GpuWidget.h` is the general Direct3D 11 path. It contains no plugin-specific geometry or commands.

- `OnDeviceCreated` receives the borrowed host device, target format, and selected feature level. A widget may create
  and retain its own device resources and may share immutable resources across instances.
- `OnDeviceLost` is idempotent and releases all plugin-owned device resources before the host releases its device.
- `Render` receives generic widget dimensions/timing, the borrowed immediate context, and the widget viewport.
- The host binds its render target and viewport before every callback. A widget binds every pipeline state it depends
  on and may issue arbitrary D3D11 work within its viewport.
- A widget must not retain the immediate context or frame records, present, resize the swap chain, or access the
  top-level HWND. It never receives the swap chain or back buffer.
- If isolation, deferred command lists, or offscreen composition later require a different contract, that contract
  receives a new IID. It does not change `IRedXeGpuWidget` or `Widget.h`.

Device resources belong to the plugin implementation. The host owns the device, immediate context, swap chain,
render target, viewport placement, device-loss sequence, and presentation.

## Experimental native-window prototype

The proposed container ownership, UI-thread callbacks, GDI/native-control/WebView behavior, focus rules, and final
interface shape remain owned by `Specs/Plans/WIP/RFC_Plugins_XeneonDashboardArchitecture.md`. The prototype is not a
compatibility promise. Promotion requires an implemented host container, a fixture plugin, HWND lifetime tests, DPI
tests, and a simultaneous update that freezes the chosen IID and semantics here.

## Host rendering and resources

- `PluginManager` owns modules, providers, generic widgets, and queried rendering-interface references.
- `DashboardHost` owns design-canvas placement and frame-scheduling policy.
- `Renderer` owns D3D11/DXGI resources, cached viewports, device notifications, rendering callbacks, recovery, and
  presentation. It contains no bundled-plugin shader, vertex type, or geometry.
- The host creates one D3D11 device, immediate context, swap chain, and back-buffer render target.
- DPI and viewport transforms are recomputed only on initialization, resize, or DPI change.
- Visible continuous animation is paced by `Present(1)` with maximum frame latency one. Hidden, minimized, suspended,
  and display-off execution blocks on messages. Occluded execution builds no frames, waits for the DXGI factory's
  occlusion-status notification, and uses `DXGI_PRESENT_TEST` before resuming.
- One widget failure does not prevent later widgets from rendering.

## First bundled plugin

`Plugins/RotatingTriangle` is an implementation of the generic contracts, not part of them. It exposes plugin ID
`builtin.rotating-triangle`, type ID `rotating-triangle`, and `IRedXeGpuWidget` on each created widget.

The DLL owns its triangle geometry, build-time HLSL source and embedded shader bytecode, immutable vertex buffer,
shared constant buffer, animation, aspect correction, and color selection. It does not link or load the runtime shader
compiler. Shared device resources live once per provider rather than once per widget instance. RedXe knows only that
four independent GPU widgets render successfully.

## Required validation

1. Run `./format.ps1` and `./validate-skills.ps1`.
2. Run Debug and Release x64 `test.ps1`; both must pass the plugin contract executable and hidden WARP smoke frame.
3. Build Release ARM64 and confirm host, plugin, and contract tests compile.
4. Verify factory null outputs, unsupported IIDs, IDs, the exact V1 options prefix, oversized records, and borrowed
   array stability.
5. Verify generic widget/GPU interface negotiation, controlling-IUnknown identity, and rejection of the unsupported
   window IID by the bundled GPU-only widget.
6. Verify all configured GPU-widget instances receive device creation, render successfully, receive device loss, and
   survive WARP rendering without a hardware GPU.
7. Keep `/W4`, `/permissive-`, SDL checks, and warnings-as-errors green.

Live XENEON EDGE validation must confirm four independently rotating aspect-correct widgets and no CPU spin while the
window is hidden, minimized, suspended, display-off, or occluded. Contract tests must confirm that loading the bundled
plugin does not load `d3dcompiler_47.dll`.
