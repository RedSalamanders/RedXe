# RedXe plugin API contract

Status: current normative contract
Last reviewed: 2026-08-31

## Purpose and scope

This contract owns RedXe native plugin discovery, generic widget creation, rendering-interface negotiation, GPU and
native-window widget lifetimes, and the bundled demonstration plugins.

Public ABI headers live under `Common/PlugInterfaces/`. `Widget.h` is deliberately rendering-neutral: it contains no
shape, shader, drawing command, HWND, or device-specific implementation. A created widget exposes the mechanisms it
supports through `QueryInterface`. New mechanisms and breaking revisions use new IIDs rather than expanding the
generic widget root.

The current executable host renders `IRedXeGpuWidget` and hosts `IRedXeWindowWidget` in host-owned child containers.
The native-window path supports GDI, native controls, media hosts, and future WebView implementations without exposing
the RedXe top-level window.

The mandatory requirements in `Specs/Core/Core_PerformanceAndResources.md` apply to every plugin and host path.

## Public interfaces

| Header | Interface | IID | Purpose |
| --- | --- | --- | --- |
| `Host.h` | `IRedXeHost` | `053E6CDF-0238-4B69-BC80-F173D9EB93D1` | Empty host-service query root |
| `Widget.h` | `IRedXeWidget` | `2C66DE33-08D1-4A0C-890C-38521F142AA0` | Generic widget identity and lifetime |
| `Widget.h` | `IRedXeWidgetProvider` | `231AC0E8-1204-4BFF-BCEA-7CACF11F439D` | Type enumeration and instance creation |
| `GpuWidget.h` | `IRedXeGpuWidget` | `DBEED29C-63EB-409E-816B-F4BDC5EF7AA9` | Direct3D 11 rendering mechanism |
| `WindowWidget.h` | `IRedXeWindowWidget` | `D324CF49-A71A-4AE9-AC25-D2988F302625` | Host-owned child-container mechanism |

`GpuWidget.h` and `WindowWidget.h` are independent mechanisms. The v1 host selects GPU when an instance exposes both;
otherwise it uses the one supported mechanism. An instance exposing neither is rejected with
`HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)`.

Every public COM declaration MUST use the MSVC
`interface __declspec(uuid("...")) __declspec(novtable) Name : IUnknown` form. Declaring a COM interface with the C++
`struct` keyword is forbidden. `IRedXeWidget`, `IRedXeWidgetProvider`, `IRedXeGpuWidget`, and
`IRedXeWindowWidget` are independent direct children of `IUnknown`; rendering mechanisms MUST NOT inherit from
`IRedXeWidget` or from one another.

`IRedXeWidget` adds no methods to `IUnknown`. A widget implementation exposes that identity interface and each
supported rendering mechanism as sibling COM interfaces on one object. `QueryInterface(IID_IUnknown)` from every
interface MUST return the same controlling `IUnknown` pointer. This preserves COM identity without creating a C++
inheritance relationship or an accidental vtable dependency between the generic widget and a rendering mechanism.

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
- Factory, enumeration, widget creation, device notification, GPU rendering, and native-window lifecycle calls are
  synchronous and non-reentrant in v1. Native-window calls run on the RedXe UI thread.
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
  Exactly that prefix and larger records are accepted. A consumer reads an appended field only when `sizeBytes`
  contains that field's complete named prefix; unknown future tail bytes are ignored.
- `kRedXeFactoryOptionsV2Size` is 20 bytes on x64 and ARM64 and appends a borrowed `configurationJsonUtf8` pointer
  plus `configurationBytes`. Structure size is 24 bytes because of pointer alignment; the V1 prefix remains eight
  bytes. Configuration length excludes a terminator and MUST NOT exceed 4096 bytes.
- A null configuration pointer with zero bytes selects plugin defaults. Every other null/length mismatch returns
  `E_INVALIDARG`. Plugins that consume the tail MUST parse and copy it synchronously before `RedXeCreate` returns;
  no plugin retains the borrowed pointer.
- Host-created providers receive the compact normalized object `{"plugin":<object>,"instance":<object>}`. A bundled
  plugin with no current private fields MUST validate the exact empty-object envelope when configuration is present;
  the V1/no-configuration path remains accepted for factory compatibility. Malformed normalized configuration fails
  before a provider is published.
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

A provider MAY publish a lower bounded instance limit for a production type. Matrix Rain permits one live widget per
provider and returns `HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES)` for a second live instance.

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

## Native-window widget contract

`WindowWidget.h` is the general child-HWND path. The host owns one `WS_CHILD` container for each selected native-window
widget and passes that borrowed container to `Attach`. The plugin may paint with GDI or create child controls, media
hosts, or a WebView controller inside it.

- `Attach`, `Resize`, `SetVisible`, and `Detach` run synchronously on the RedXe UI thread.
- The plugin may retain the container HWND only from successful `Attach` until `Detach` begins. It MUST NOT subclass,
  destroy, reparent, or change the host container, and it never receives the top-level HWND.
- The plugin owns every HWND, timer, controller, COM object, GDI object, and other resource it creates. Before
  `Detach` returns it MUST stop callbacks and timers, destroy its child HWNDs, and release those resources.
- `Attach` creates plugin content for the supplied physical pixel size and DPI. A failed call leaves the widget
  detached. Repeated `Detach` is safe.
- `Resize` receives the new physical container size and destination-monitor DPI after the host has repositioned the
  container. It performs no continuous work when size and DPI are unchanged.
- `SetVisible(FALSE)` quiesces animation and background activity before returning. `SetVisible(TRUE)` may resume only
  the work required for visible content.
- Native children are composed above the parent's D3D surface. They can be ordered relative to other child windows,
  but cannot be interleaved or alpha-composited with GPU widgets. Effects requiring D3D-layer composition use the GPU
  mechanism.
- Ordinary child-window pointer, keyboard, focus, accessibility, and IME behavior remains inside the widget bounds.
  Global commands and page policy remain host-owned. Interactive WebView security and navigation policy require a
  separate normative contract before a bundled web widget ships.

## Host rendering and resources

- `PluginManager` owns modules, providers, generic widgets, and queried rendering-interface references.
- Module-slot capacity MUST equal the normalized settings registry cap rather than today's bundled-DLL count. The
  canonical bundled-plugin table MUST be compile-time checked against that capacity so adding another supported DLL
  does not require finding an unrelated hard-coded module limit.
- `PluginManager` creates only the configured active page and groups widgets by normalized plugin/instance factory
  configuration when provider sharing is valid.
- `DashboardHost` owns the configured grid-to-design-canvas transform, cached placements, host child containers,
  native-window lifecycle, and frame-scheduling policy.
- `Renderer` owns D3D11/DXGI resources, cached viewports, device notifications, rendering callbacks, recovery, and
  presentation. It contains no bundled-plugin shader, vertex type, or geometry.
- The host creates one D3D11 device, immediate context, swap chain, and back-buffer render target.
- DPI and viewport transforms are recomputed only on initialization, resize, or DPI change.
- Visible continuous animation is paced by `Present(1)` with maximum frame latency one. Hidden, minimized, suspended,
  and display-off execution blocks on messages. Occluded execution builds no frames, waits for the DXGI factory's
  occlusion-status notification, and uses `DXGI_PRESENT_TEST` before resuming.
- One widget failure does not prevent later widgets from rendering.
- Native-window containers use the same cached design-canvas placements as GPU viewports. Resize and Per-Monitor-V2
  DPI changes reposition the container and notify the plugin without per-frame layout work.
- Hidden, minimized, display-off, and DXGI-occluded states call `SetVisible(FALSE)` so native-widget timers and
  animation stop. Recovery calls `SetVisible(TRUE)` only after visible rendering resumes.

## Bundled plugins

`Plugins/RotatingTriangle` is an implementation of the generic contracts, not part of them. It exposes plugin ID
`builtin.rotating-triangle`, type ID `rotating-triangle`, and `IRedXeGpuWidget` on each created widget.

The DLL owns its triangle geometry, build-time HLSL source and embedded shader bytecode, immutable vertex buffer,
shared constant buffer, animation, aspect correction, and color selection. It does not link or load the runtime shader
compiler. Shared device resources live once per provider rather than once per widget instance. The Debug template
uses two independent instances; Release disables this fixture and MUST NOT load its DLL.

`Plugins/GdiOrbit` exposes plugin ID `builtin.gdi-orbit`, type ID `gdi-orbit`, and `IRedXeWindowWidget`. The host creates
one instance beside the GPU examples. The plugin creates one child window, caches a resize-owned 32-bit DIB and GDI
objects, paints a double-buffered Xenon orbit at 30 FPS while visible, and kills its timer while hidden or detached.
Painting performs no heap allocation and creates no GDI objects.

`Plugins/MatrixRain` is the production-oriented bundled GPU plugin. It exposes plugin ID `builtin.matrix-rain`, type
ID `matrix-rain`, and one continuous-animation widget per configured provider. The Release active page names its
instance `matrix-rain.1`; its alternate page uses a different document-unique instance ID. `DashboardHost` gives the
active Release widget the complete 2560×720 design canvas. Debug places Matrix Rain beside the two triangle and one
GDI fixtures on its active page.

The host passes every bundled provider the same normalized V2 factory envelope with `plugin` and `instance` private
objects. Matrix Rain consumes the instance object, strictly rejects malformed, missing, duplicate, unknown, and
out-of-range members, copies normalized numeric settings during creation, and retains no borrowed JSON. It continues
to accept the previously shipped direct Matrix settings object for factory compatibility. A live page or private
settings change creates the required providers and widgets transactionally; successfully mapped modules remain loaded.

The plugin owns an original generated 128×128 `R8_UNORM` signed-distance-field atlas, four build-time Shader Model 5.0
shader blobs, one dynamic 128-byte constant buffer, and its immutable pipeline states. It loads no font, DirectWrite,
WIC, runtime HLSL compiler, loose glyph asset, timer, worker, or HWND. Grid values are cached until viewport or DPI
changes, and configured colors are converted to float vectors once during provider construction rather than on every
frame. Stream phase, speed, trail length, column permutation, and mutation are deterministic functions of the
configured seed and frame time.

Each non-zero visible Matrix frame performs one map/unmap, one opaque background draw, and one alpha-blended instanced
glyph draw. The CPU performs no per-column or per-glyph simulation. Submitted glyphs are bounded at 65,536; density
reduces both visible columns and submitted instances. The callback performs no heap allocation, synchronization, I/O,
texture upload, or shader/font work. `OnDeviceCreated` builds the complete provider resource set transactionally and
`OnDeviceLost` releases it idempotently.

## Required validation

1. Run `./format.ps1` and `./validate-skills.ps1`.
2. Run Debug and Release x64 `test.ps1`; both must pass the plugin contract executable, the production host/plugin
   harness, and the hidden application WARP smoke frame.
3. Build Release ARM64 and confirm the host, plugins, contract tests, and host/plugin harness compile.
4. Verify factory null outputs, unsupported IIDs, IDs, exact V1/V2 option prefixes, future oversized records, every
   configuration pointer/length mismatch, the 4096-byte cap, synchronous configuration copying, and borrowed array
   stability.
5. Verify generic widget/rendering-interface negotiation and controlling-IUnknown identity. The GPU-only widget rejects
   the window IID and the window-only widget rejects the GPU IID.
   Compile-time contract checks MUST also prove that every public COM interface derives directly from `IUnknown` and
   that neither rendering interface derives from `IRedXeWidget`.
6. Verify all configured GPU-widget instances receive device creation, render successfully, receive device loss, and
   survive WARP rendering without a hardware GPU. Matrix readback MUST contain configured background and glyph pixels;
   identical inputs MUST reproduce identical pixels, while time and seed changes MUST change the result.
7. Verify native-window attach validation, duplicate-attach rejection, physical resize/DPI notification, visibility
   transitions, idempotent detach, and destruction of every plugin child before the host container.
8. Verify Matrix minimum/maximum settings, device recreation, the 65,536-instance bound, one-upload/two-draw structure,
   resource-size static assertions, and absence of `d3dcompiler_47.dll`, DirectWrite, and WIC loading.
9. Run the Release `PluginContractTests.exe --matrix-benchmark` path at 2560×720 and record disabled-versus-enabled
   CPU submission time, D3D timestamp time, private bytes, working set, and steady heap block/byte deltas.
10. Run Release `HostPluginTests.exe --matrix-soak` for five minutes. The production host stack MUST render throughout,
    retain exactly one provider, widget, and device-resource set, report its memory deltas, and release the device set
    on shutdown.
11. Verify through `HostPluginTests` that Release loads only Matrix Rain, gives it the full design canvas, survives
    suspend/restore and transactional reconfiguration, preserves the active widget after a rejected dashboard, and
    leaves no Matrix objects after host teardown. The `RedXeMatrixRainGetTestDiagnostics` export is a read-only test
    seam, not a published plugin ABI or host dependency.
12. Verify normalized factory envelopes, case-insensitive plugin-ID uniqueness, active-page-only provider creation,
    provider sharing for identical private objects, isolation for distinct private objects, and configured grid order.
13. Compile-time checks MUST keep the canonical bundled module table within the settings-registry-sized module slots.
14. Keep `/W4`, `/permissive-`, SDL checks, and warnings-as-errors green.

The automated Debug host composition must contain two GPU fixtures, the GDI fixture, and Matrix Rain. The automated
Release composition must contain one full-canvas Matrix widget. Scheduler tests must prove that hidden, minimized,
suspended, display-off, and occluded states select an event-blocked action. Contract tests must confirm that loading
the GPU plugins does not load `d3dcompiler_47.dll`.
