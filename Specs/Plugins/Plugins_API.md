# RedXe plugin API contract

Status: current normative contract
Last reviewed: 2026-09-02

## Purpose and scope

This contract owns RedXe native plugin discovery, generic widget creation, rendering-interface negotiation, GPU and
native-window widget lifetimes, and the bundled demonstration plugins.

Public ABI headers live under `Common/PlugInterfaces/`. `Widget.h` contains the complete generic, GPU, scheduled, and
native-window widget surface. `Data.h` contains the complete source, provider, snapshot, sink, and subscription
surface. A created widget exposes the mechanisms it supports through `QueryInterface`.

RedXe is pre-production and rebuilds the host and every plugin from one source tree. The repository therefore defines
one current native contract: it does not load older layouts, accept newer record tails, negotiate interface versions,
or preserve a superseded IID/vtable. Interface evolution changes the current headers, host, plugins, tests, and this
contract together. A production ABI freeze requires an explicit future policy change.

The current executable host renders `IRedXeGpuWidget` and hosts `IRedXeWindowWidget` in host-owned child containers.
The native-window path supports GDI, native controls, media hosts, and future WebView implementations without exposing
the RedXe top-level window.

The mandatory requirements in `Specs/Core/Core_PerformanceAndResources.md` apply to every plugin and host path.

## Public interfaces

| Header | Interface | IID | Purpose |
| --- | --- | --- | --- |
| `Host.h` | `IRedXeHost` | `D00BE2C2-10C4-4D8D-8097-4C26DFC29A19` | Host services, beginning with data-provider lookup |
| `Widget.h` | `IRedXeWidget` | `62DB9FB4-AF7B-47C0-BBF9-B7D5CA535502` | Generic widget identity, lifetime, and visibility |
| `Widget.h` | `IRedXeWidgetProvider` | `231AC0E8-1204-4BFF-BCEA-7CACF11F439D` | Type enumeration and instance creation |
| `Widget.h` | `IRedXeGpuWidget` | `DBEED29C-63EB-409E-816B-F4BDC5EF7AA9` | Direct3D 11 rendering mechanism |
| `Widget.h` | `IRedXeScheduledWidget` | `1B6B4F9E-5421-4B4E-BC2D-190EE6CE86CB` | Optional low-cadence frame deadline |
| `Widget.h` | `IRedXeWindowWidget` | `3219FA78-260B-416A-BB76-6331DBF30593` | Host-owned child-container mechanism |
| `Data.h` | `IRedXeDataSource` | `C3A81F6E-2D47-4B90-A1E5-6F8C9D0B3E21` | Plugin-side typed, bounded pull snapshots |
| `Data.h` | `IRedXeDataProvider` | `9EAE20F1-36A8-48A8-B451-F60401A898CD` | Host-side dataset discovery and subscription |
| `Data.h` | `IRedXeDataSink` | `F9834987-EBC6-411E-9F28-A49E4DBB49D9` | Synchronous borrowed-snapshot delivery on the host worker |
| `Data.h` | `IRedXeDataSubscription` | `B8912B7D-89AD-4830-9CFB-73F4E72027FB` | Active/inactive subscription lifetime and callback drain |

The GPU and native-window interfaces are independent mechanisms. The host selects GPU when an instance exposes both;
otherwise it uses the one supported mechanism. An instance exposing neither is rejected with
`HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)`.

Every public COM declaration MUST use the MSVC
`interface __declspec(uuid("...")) __declspec(novtable) Name : IUnknown` form. Declaring a COM interface with the C++
`struct` keyword is forbidden. Every interface in the table is a direct child of `IUnknown`; rendering, scheduling,
host, and data mechanisms MUST NOT inherit from `IRedXeWidget` or from one another.

`IRedXeWidget` adds only `SetVisible`. A widget implementation exposes that interface and each supported rendering
mechanism as sibling COM interfaces on one object. `QueryInterface(IID_IUnknown)` from every interface MUST return the
same controlling `IUnknown` pointer. This preserves COM identity without creating a C++ inheritance relationship or
an accidental vtable dependency between the generic widget and a rendering mechanism.

Widgets are initially invisible. `SetVisible` is synchronous, idempotent, and runs on the RedXe UI thread for every
widget, regardless of rendering mechanism. `SetVisible(FALSE)` quiesces widget-owned animation, subscriptions,
timers, and other visibility-dependent work before returning. `SetVisible(TRUE)` may resume only work required by
visible content. The host enables visibility only after the selected mechanism is ready and disables it before that
mechanism is detached. Attach/detach and device lifetime remain on their mechanism-specific interfaces.

Every public record starts with `sizeBytes`. It is retained so a future production compatibility policy can define
safe record evolution. Under the current pre-production contract it is an exact stale-binary and malformed-input
guard: consumers require `sizeBytes == sizeof(current-record)` and reject both smaller and larger values.

## Identifiers and strings

- ABI plugin IDs, widget type IDs, and runtime instance IDs are stable UTF-8 ASCII strings. Version 4 user settings
  expose only the plugin ID; the host maps that settings-visible ID to the module's internal widget type and generates
  runtime instance IDs.
- IDs contain 1–128 characters, start with an ASCII alphanumeric character, and otherwise use only
  `[A-Za-z0-9_.-]`.
- IDs compare case-insensitively using ASCII ordinal rules. The host persists canonical plugin spelling.
- Localized display names and descriptions are borrowed UTF-16 Windows strings.
- Borrowed module strings and descriptor arrays remain valid while the module is mapped.

## Binary discovery and lifetime

- RedXe loads DLLs by absolute path using
  `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`; it never searches the working directory.
- `RedXeCreate` and `RedXeEnumeratePlugins` are required. Every settings-visible plugin MUST also export
  `RedXeGetPluginSettingsContract`. `RedXePluginShutdown` is optional because most modules need no global shutdown.
- Every factory call names one non-empty plugin ID. Null and empty IDs are invalid, including in single-plugin DLLs.
- Factory, enumeration, widget creation, device notification, GPU rendering, native-window lifecycle, host-service,
  data-source, provider, and data-sink calls are synchronous and non-reentrant. Widget visibility and native-window
  calls run on the RedXe UI thread; data-sink callbacks run only on the host acquisition worker.
- Rendering interfaces and generic widgets are released before providers, optional shutdown, and process teardown.
- Modules remain mapped until process teardown. Exceptions must not cross ABI or Win32 boundaries.

## Factory contract

`Factory.h` declares export signatures, aliases, metadata, helpers, and names. `FactoryImpl.h` supplies shared bundled
plugin factory behavior.

- A null output returns `E_POINTER`; a present output is cleared before other validation.
- Unsupported IIDs return `E_NOINTERFACE`.
- A null or empty plugin ID returns `E_INVALIDARG`.
- Unknown non-empty IDs return `HRESULT_FROM_WIN32(ERROR_NOT_FOUND)`.
- A present `RedXeFactoryOptions` requires its exact current size; smaller and larger records return `E_INVALIDARG`.
  The current record contains `debugLevel`, a borrowed `configurationJsonUtf8` pointer, and `configurationBytes`.
  Configuration length excludes a terminator and MUST NOT exceed 8192 bytes.
- A null configuration pointer with zero bytes selects plugin defaults. Every other null/length mismatch returns
  `E_INVALIDARG`. Plugins that consume configuration JSON MUST parse and copy it before `RedXeCreate` returns;
  no plugin retains the borrowed pointer.
- Host-created providers receive only the compact internal
  `{"plugin":{},"instance":<effective-settings>}` envelope. Plugins MUST parse and copy it before returning. This
  envelope is an ABI normalization detail, not the user document model; direct effective-settings objects are invalid.
- Successful creation returns one caller-owned COM reference.
- Enumeration returns 1–256 contiguous module-owned metadata records.
- Malformed metadata, duplicate IDs, or missing required capability fail module discovery safely.

### Static settings contract

`RedXeGetPluginSettingsContract` discovers plugin-owned validation metadata without creating a provider, widget,
device resource, HWND, timer, or worker. It returns one borrowed immutable `RedXePluginSettingsContract` for the
requested plugin ID. The record and its UTF-8 strings remain valid while the module is mapped.

- A null output returns `E_POINTER`; a present output is cleared before validation.
- An unknown plugin ID returns `HRESULT_FROM_WIN32(ERROR_NOT_FOUND)`.
- `sizeBytes` must equal `sizeof(RedXePluginSettingsContract)`.
- Schema and defaults are JSON objects bounded at 4096 bytes each. The schema uses Draft 2020-12 and may contain
  bounded `x-ui-*` annotations. Defaults MUST validate against the schema.
- One settings-visible plugin ID selects one widget kind. A DLL may publish several IDs, each with its own contract.
- The host copies or parses borrowed strings synchronously and never frees them.
- Rotating Triangle and GDI Orbit publish closed empty-object schemas and `{}` defaults. Matrix Rain publishes its
  complete closed schema, ranges, color syntax, and runtime defaults. Process Viewer publishes a closed object with
  required integer `topN` from 1 through 32 and default 10. Network Meter and GPU Processes publish the same closed
  `topN` object with range 1 through 16 and default 8. System Pulse, CPU Meter, Memory Meter, Storage Meter, GPU Meter,
  Power Meter, and Thermal Meter publish closed empty-object schemas and `{}` defaults. Studio Clock publishes its
  complete closed boolean, color, and date-format schema and defaults. Desk Clock publishes its complete closed duration
  and color schema and defaults.

The metadata capability surface advertises factory-created plugin services through
`RedXePluginCapabilityWidgetProvider` and `RedXePluginCapabilityDataSource`. Rendering mechanisms are discovered on
widget instances by IID, not by a capability or rendering-path enum.

## Data discovery and delivery

`IRedXeDataSource` is the plugin-side pull interface created by `RedXeCreate`. It enumerates immutable typed-table
descriptors and performs synchronous, non-reentrant batch snapshot collection. `CollectSnapshots` takes one exact-size
`RedXeDataCollectRequest` (`sizeof` 24) of unique borrowed module-owned dataset IDs, at most
`RedXeDataCollectMaximumDataSets` (32). A successful `RedXeDataCollectResult` (`sizeof` 32) returns one non-null
snapshot pointer per request ID, plus one common sequence and `timestampFileTime100ns` for the whole batch. Unknown
IDs, duplicate IDs, a zero or oversized count, a null ID, or a mismatched `sizeBytes` fail the entire call and clear
the result. Descriptors remain valid while the module is mapped. Returned snapshots and all referenced rows, values,
and UTF-16 strings remain valid until the next `CollectSnapshots` call on that source or source release; consumers copy
retained data before then. Widgets never receive a data source.

`IRedXeHost::GetDataProvider` accepts one provider plugin ID and returns the host-side `IRedXeDataProvider` for that
source. A null output returns `E_POINTER`; a present output is cleared first. Invalid IDs return `E_INVALIDARG`, an
unknown ID returns `HRESULT_FROM_WIN32(ERROR_NOT_FOUND)`, and a known plugin without a data source returns
`E_NOINTERFACE`. Repeated lookup returns the same controlling provider identity. A widget stores the returned
provider, discovers datasets through `GetDataSets`, and calls `Subscribe` with only a dataset ID, requested interval
from 1 through 60,000 milliseconds, and sink. Each call returns a distinct inactive subscription or fails without
retaining the sink.

`PluginHost` owns every loaded module and local data-provider runtime for one `PluginManager`. The first provider
lookup lazily maps its catalog module, creates one `IRedXeDataSource`, validates and caches at most 256 datasets, and
returns a host provider facade. All local sources share one acquisition worker and at most 32 subscriptions in total.
Active subscriptions for the same provider and dataset share one collection at the shortest requested interval,
clamped to the source recommendation. When multiple datasets on one source are due in the same worker pass, the host
gathers those unique IDs, orders them deterministically with `source.status` last, and issues one `CollectSnapshots`
call. It delivers only when the result count, record sizes, and snapshot IDs match the request. Different providers
remain distinct and may be used concurrently. Multiple
viewers may hold the same provider and subscribe independently. With no active subscription the worker blocks
indefinitely on its change and stop events; it owns no polling or periodic wake-up. `RedXeDataSetFlagDeviceLane` marks
datasets that would use a shared host device-I/O lane; no such lane is created until timeout, `CancelIoEx`, and
teardown drain are measured. Sources MUST NOT create their own acquisition threads.

The host provider does not retain or duplicate a source snapshot. It synchronously invokes each active sink while the
source storage is borrowed. A sink copies only bounded values it needs, performs no blocking work or provider/host
re-entry, and MUST NOT activate, deactivate, or release a subscription from inside `OnDataSnapshot`. A sink failure is
isolated and does not stop later sinks or acquisition cycles. After a successful delivery to any active sink,
`PluginHost` coalesces one UI-thread frame invalidation (`WM_APP + 3`) so GPU data widgets can start a sample-driven
ease without a child HWND. `SetActive(FALSE)` and subscription release drain an
in-flight callback before returning when called outside the callback. Shutdown signals and joins the worker before
releasing providers and sources. Push delivery and schema-based automatic source selection are outside the current
contract; bindings use explicit provider and dataset IDs.

Values are unsigned integers, finite doubles, or length-delimited UTF-16 strings and carry `Good`, `Unavailable`, or
`Initializing` quality. Snapshot tables remain within the descriptor's row bound and set `Truncated` when bounded
provider storage cannot represent every source row or string.

## Generic widget provider

`IRedXeWidgetProvider::GetWidgetTypes` returns a module-owned immutable descriptor array and count. Both outputs are
cleared before validation. A descriptor contains stable identity, localized labels, design-canvas size hints, and
generic scheduling flags only.

`CreateWidget` takes a type ID and non-empty instance ID and returns `IRedXeWidget`. Every successful call creates a
distinct stateful instance. The host then queries that object for supported rendering interfaces. A widget may expose
more than one mechanism; host policy chooses one without changing `Widget.h`.

A provider MAY publish a lower bounded instance limit for a production type. Matrix Rain permits one live widget per
provider and returns `HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES)` for a second live instance.

## Scheduled widget contract

`IRedXeScheduledWidget` lets a low-cadence widget request its next visible frame without a continuous-render flag, plugin
timer, worker, HWND, or host re-entry. It is an optional sibling interface and does not change the generic widget or
GPU vtables.

- `GetNextFrameDelayMilliseconds` is synchronous and non-reentrant. A null output returns `E_POINTER`; every present
  output is cleared first.
- `S_OK` returns a relative delay from 1 through 86,400,000 milliseconds. `S_FALSE` requests no deadline.
- The callback performs no allocation, I/O, wait, synchronization, device work, or host re-entry.
- After a successful static frame, `DashboardHost` queries active scheduled widgets and selects the earliest valid
  delay. During a swipe, only the current and staged adjacent dashboards participate.
- `Application` converts the delay immediately to a monotonic deadline and blocks in one message-aware wait. Deadline
  expiry coalesces one ordinary frame invalidation; unrelated messages do not render a clean static page.
- Continuous animation supersedes a scheduled wait and adds no additional wake-up. Hidden, minimized, suspended,
  display-off, occluded, inactive-page, and shutdown states retain no scheduled deadline.
- A malformed or failed scheduling result is isolated and never creates a retry loop.

## GPU widget contract

`IRedXeGpuWidget` is the general Direct3D 11 path. It contains no plugin-specific geometry or commands.

- `OnDeviceCreated` receives the borrowed host device, target format, and selected feature level. A widget may create
  and retain its own device resources and may share immutable resources across instances.
- `OnDeviceLost` is idempotent and releases all plugin-owned device resources before the host releases its device.
- `Render` receives generic widget dimensions/timing, the borrowed immediate context, and the widget viewport.
- The host binds its render target and viewport before every callback. A widget binds every pipeline state it depends
  on and may issue arbitrary D3D11 work within its viewport.
- A widget must not retain the immediate context or frame records, present, resize the swap chain, or access the
  top-level HWND. It never receives the swap chain or back buffer.
- Isolation, deferred command lists, and offscreen composition are outside the current contract.

Device resources belong to the plugin implementation. The host owns the device, immediate context, swap chain,
render target, viewport placement, device-loss sequence, and presentation.

## Native-window widget contract

`IRedXeWindowWidget` is the general child-HWND path. The host owns one `WS_CHILD` container for each selected native-window
widget and passes that borrowed container to `Attach`. The plugin may paint with GDI or create child controls, media
hosts, or a WebView controller inside it.

- `Attach`, `Resize`, and `Detach` run synchronously on the RedXe UI thread.
- The plugin may retain the container HWND only from successful `Attach` until `Detach` begins. It MUST NOT subclass,
  destroy, reparent, or change the host container, and it never receives the top-level HWND.
- The plugin owns every HWND, timer, controller, COM object, GDI object, and other resource it creates. Before
  `Detach` returns it MUST stop callbacks and timers, destroy its child HWNDs, and release those resources.
- `Attach` creates plugin content for the supplied physical pixel size and DPI. A failed call leaves the widget
  detached. Repeated `Detach` is safe.
- `Resize` receives the new physical container size and destination-monitor DPI after the host has repositioned the
  container. It performs no continuous work when size and DPI are unchanged.
- Native children are composed above the parent's D3D surface. They can be ordered relative to other child windows,
  but cannot be interleaved or alpha-composited with GPU widgets. Effects requiring D3D-layer composition use the GPU
  mechanism.
- Ordinary child-window pointer, keyboard, focus, accessibility, and IME behavior remains inside the widget bounds.
  Global commands and page policy remain host-owned. Interactive WebView security and navigation policy require a
  separate normative contract before a bundled web widget ships.

## Host rendering and resources

- `PluginManager` owns widget providers, generic widgets, and queried rendering-interface references.
- Its `PluginHost` owns one shared module store, all lazily created host data providers and plugin data sources, one
  event-blocked acquisition worker, and subscription drain lifetime. Widgets and subscriptions are released before
  `PluginHost`.
- `PluginHost` has exactly one module slot per bundled module-catalog plugin ID. Catalog plugin IDs and widget type IDs
  MUST stay unique; module file names MAY repeat. The first slot that maps a given module name owns the `HMODULE`;
  later catalog rows with the same name share exports after metadata validation for their own plugin ID. Optional
  `RedXePluginShutdown` runs only on the owning slot. Mapped modules remain loaded until process teardown. The widget
  projection MUST remain within the settings limit of 64 plugin declarations.
- Static discovery validates every referenced plugin and effective widget on every page. `PluginManager` creates only
  the current page, plus its adjacent transition page during a swipe, and may share providers only when doing so is
  behaviorally invisible to independent widget instances.
- `DashboardHost` owns compiled adaptive split paths, cached responsive placements, host child containers,
  transition offsets, native-window lifecycle, and frame-scheduling policy.
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
- Hidden, minimized, display-off, and DXGI-occluded states call `IRedXeWidget::SetVisible(FALSE)` so every widget
  quiesces visibility-dependent work. Recovery calls `SetVisible(TRUE)` only after visible rendering resumes.

## Bundled plugins

`Plugins/RotatingTriangle` exposes settings-visible plugin ID `builtin.rotating-triangle`, internally maps it to type
ID `rotating-triangle`, publishes closed `{}` settings and defaults, and exposes `IRedXeGpuWidget` on each widget.

The DLL owns its triangle geometry, build-time HLSL source and embedded shader bytecode, immutable vertex buffer,
shared constant buffer, animation, aspect correction, and color selection. It does not link or load the runtime shader
compiler. Shared device resources live once per provider rather than once per widget instance. The Debug first page
uses two independent instances. The Release gallery references the plugin, so static discovery maps its DLL at load,
but the first-page runtime creates no Triangle provider, widget, or device resource.

`Plugins/GdiOrbit` exposes settings-visible plugin ID `builtin.gdi-orbit`, internally maps it to type ID `gdi-orbit`,
publishes closed `{}` settings and defaults, and exposes `IRedXeWindowWidget`. The plugin creates one child window,
caches a resize-owned 32-bit DIB and GDI
objects, paints a double-buffered Xenon orbit at 30 FPS while visible, and kills its timer while hidden or detached.
Painting performs no heap allocation and creates no GDI objects.

The host has one compile-time bundled module catalog and one widget projection. The module catalog binds every bundled
plugin ID, including `builtin.system-data`, to its DLL. Plugin IDs stay unique; several catalog rows MAY share one
module file name. `PluginHost` is the only module loader and maps that path once; widget discovery and `GetDataProvider`
both use it, so a DLL cannot be mapped or validated by competing paths. The
widget projection binds only settings-visible widget plugin IDs to their internal type IDs and drives schema/template
coverage. This is a bundled allowlist, not automatic filesystem discovery: adding any bundled plugin requires one
module entry; adding a widget also requires its widget entry, schema/parser support, settings contract, and a real
placed example in both shipped templates. Template tests iterate the widget projection.

`Plugins/MatrixRain` is the production-oriented bundled GPU plugin. It exposes settings-visible plugin ID
`builtin.matrix-rain`, internally maps it to type ID `matrix-rain`, publishes its complete closed settings schema and
defaults, and permits one continuous-animation widget per configured provider. The Release first page gives it the
complete client rectangle; its second page is a varied gallery of every bundled plugin. The Debug second page is the
equivalent complete bundled-plugin gallery.

The host passes every bundled provider its compact effective settings in the normalized ABI envelope. Matrix Rain strictly rejects malformed,
duplicate, unknown, and out-of-range members, copies normalized numeric settings during creation, and retains no
borrowed JSON. A live page or settings change creates required providers and widgets transactionally; successfully
mapped modules remain loaded.

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

`Plugins/SystemData` exposes `builtin.system-data` and only factory-created `IRedXeDataSource`. It currently publishes
`source.status` (at most 32 rows, 5 s), `system.summary` (1 row, 1 s, not local-sensitive), `cpu.summary` (1 row, 1 s),
`cpu.logical` (1,024 rows, 1 s), `memory.summary` (1 row, 1 s), `process.list` (2,048 rows, 2 s, local-sensitive),
`thread.list` (8,192 rows, 2 s, local-sensitive), `network.interface` (256 rows, 1 s, local-sensitive),
`network.protocol` (16 rows, 1 s), `storage.disk` (128 rows, 1 s, local-sensitive), `storage.volume` (256 rows, 5 s,
local-sensitive), `gpu.adapter` (32 rows, 1 s), `gpu.engine` (512 rows, 1 s), `gpu.process` (2,048 rows, 2 s,
local-sensitive), `power.summary` (1 row, 5 s), `battery.list` (32 rows, 5 s, local-sensitive), `thermal.sensor`
(128 rows, 10 s, local-sensitive), and `fan.sensor` (128 rows, 10 s, local-sensitive). It reads local counters with the
caller's token, does not elevate or collect command lines, full executable paths, MAC or IP addresses, serial numbers,
user names, or wireless identities, and degrades inaccessible per-process values to `Unavailable`. Network rows identify
interfaces by `InterfaceLuid`. Protocol rows are IPv4/IPv6 TCP/UDP aggregates; they do not
enumerate endpoints. Disk activity uses overlapped `IOCTL_DISK_PERFORMANCE` where the device accepts it and never
issues `IOCTL_DISK_PERFORMANCE_OFF`; PDH `PhysicalDisk` instance names are not mapped because the mapping is unstable.
Volume rows publish GUID, mount, filesystem, capacity, and extents and never copy whole-disk activity. GPU adapters
come from DXGI without creating a D3D device and join D3DKMT sensors by LUID only. GPU engine rows are D3DKMT nodes;
process GPU rows parse GPU Engine counter instance names with a strict `pid_/luid_/phys_/eng_/engtype_` grammar.
Machine-wide GPU memory use, node utilization, and DXGI `integrated` stay `Unavailable` (DXCore is not linked; D3DKMT
statistics records remain reserved). Adapter `software` is the DXGI software flag and does not require a dedicated
WARP-only host. The source may retain a DXGI factory, D3DKMT adapter handles, and one PDH GPU Engine query, released
when the source is destroyed. Power uses `GetSystemPowerStatus` and cached `GetPwrCapabilities`; unknown sentinels
(`255`, `0xFFFFFFFF`) stay unavailable. An AC-only host publishes `batteryPresent` 0 and a zero-row `battery.list`.
Battery rows use SetupAPI `GUID_DEVICE_BATTERY` plus read-only overlapped IOCTLs and never publish serial numbers. Thermal rows re-project GPU,
storage, and battery temperatures and publish ACPI zones only when tenths-Kelvin converts to a plausible Celsius
reading (zero tenths-K is not published). Fan rows are GPU RPM when D3DKMT `MaxFanRpm` is non-zero; generic
`GUID_DEVICE_FAN` presence does not invent motherboard RPM. Battery, ACPI, and storage-temperature IOCTLs use the same
overlapped timeout and `CancelIoEx` drain as disks. There is no device-I/O thread. The source MUST NOT include WMI/CIM headers, link WMI libraries, create an `IWbem*`
service, execute a CIM query, or load `wbemprox.dll`, `fastprox.dll`, or `wbemcomn.dll` when any dataset is collected.
CPU deltas use fixed prior-sample tables; collection allocates no plugin heap storage after source creation and creates
no worker, timer, outbound network request, or persistent process handle. The source may retain one IP Helper
`NotifyIpInterfaceChange` registration (callback sets a dirty flag only) and bounded read-only disk and battery handles,
both released when the source is destroyed. Only `PluginHost` calls `CollectSnapshots`. Consumers obtain the
corresponding host provider through `IRedXeHost::GetDataProvider("builtin.system-data", ...)`. A missing NIC, disk, GPU
adapter, battery, or thermal sensor is a valid empty or `Unavailable` snapshot; live rename/removal remains
host-specific churn rather than a second catalog shape. ARM64 shares the x64 64-bit native record layouts; a live ARM64
host is not required for the shipped catalog.

`SystemDataTestContract.h` declares a test-only sibling interface that reports fixed source bounds and drives the same
per-process row-population path with a requested synthetic row count. It is not a published plugin ABI, dataset,
host dependency, or production acquisition mode. Its controlling `IUnknown`, exact record size, excessive-row
rejection, and 2,048-row Release resource measurement are validated by `SystemDataTests`.

`Plugins/ProcessViewer` remains `ProcessViewer.dll` and publishes ten settings-visible plugin IDs, each mapped to one
widget type. Every widget exposes sibling `IRedXeGpuWidget` and `IRedXeScheduledWidget` interfaces on one controlling
`IUnknown` and does not set `RedXeWidgetFlagContinuousAnimation`. None of these widgets expose `IRedXeWindowWidget`;
`GdiOrbit` remains the shipped native-window example.

| Plugin ID | Type ID | Settings | Datasets |
| --- | --- | --- | --- |
| `builtin.process-viewer` | `process-viewer` | `topN` 1–32, default 10 | `process.list` (2 s) |
| `builtin.system-pulse` | `system-pulse` | `{}` | `system.summary` (1 s) |
| `builtin.cpu-meter` | `cpu-meter` | `{}` | `cpu.summary` + `cpu.logical` (1 s) |
| `builtin.memory-meter` | `memory-meter` | `{}` | `memory.summary` (1 s) |
| `builtin.network-meter` | `network-meter` | `topN` 1–16, default 8 | `network.interface` + `network.protocol` (1 s) |
| `builtin.storage-meter` | `storage-meter` | `{}` | `storage.volume` (5 s) + `storage.disk` (1 s) |
| `builtin.gpu-meter` | `gpu-meter` | `{}` | `gpu.adapter` (1 s) |
| `builtin.gpu-processes` | `gpu-processes` | `topN` 1–16, default 8 | `gpu.process` + `process.list` (2 s) |
| `builtin.power-meter` | `power-meter` | `{}` | `power.summary` + `battery.list` (5 s) |
| `builtin.thermal-meter` | `thermal-meter` | `{}` | `thermal.sensor` + `fan.sensor` (10 s) |

Do not ship dashboard viewers for `source.status`, `thread.list`, or `gpu.engine`. Each widget asks the host for
`builtin.system-data` and subscribes only while visible. Unique datasets on a fully visible System page stay at 15,
under the host cap of 32. Widgets MAY share one host provider; identical datasets coalesce. GPU Processes also
subscribes to `process.list` so image names can join on PID; that subscription coalesces with Process Viewer and does
not raise the unique-dataset count. Process Viewer keeps the leading `process.list` columns, ranks available CPU
percentage descending with working-set and PID tie-breakers, and caches at most `topN` rows with bounded process-name
storage.

The family presents ranked lists, capacity bars, KPI tiles, adapter cards, heatmaps, and sparklines rather than raw
tables. Visual language is a near-black panel (`#111111`) with a muted hairline. Accent red (`#FF1616`) is a high-band
signal, not a fill: capacity tracks are charcoal with silver fill, amber from 70%, and red from 85%. Network and disk
byte-rate bars use a log10 mapping from 1 KB/s to link speed (or 1 GB/s when speed is unknown); they MUST NOT
normalize to the loudest sibling. When a widget rectangle is wide enough for two packed columns, ranked process, GPU
process, network, storage, and thermal lists MUST split into a two-column grid instead of stretching a single row
across empty width. System Pulse places the CPU numeral on the left and packs RAM, process, thread, handle, core,
uptime, and commit chips into two columns when width allows. Storage volumes and thermal sensors render as cards
with a vertical mercury fill; temperature
cards lead with the Celsius numeral. CPU Meter places a recency-faded, right-aligned history beside the core heatmap.
That history uses stacked translucent bars, a brighter live-edge cap, and sample-driven scrolling as new values shift
in from the right. Network Meter uses the same history treatment for aggregate throughput. Sparklines and histories
window-normalize to the history maximum except CPU percent, which stays 0–100. Temperatures use a banded fill
(25 / 70 / 85 °C) so a cool sensor does not read as an alarm. CPU heatmap cells use squared luminance and drop the
grid when a cell would be smaller than 6 px. Each widget picks a density rung from its inner height (hero / compact /
standard), keeps type floors of 16 / 18 / 30 / 48 px and a title floor of 26 px that grow with leftover tile height
among the rows or cards that fit, and omits columns, rows, heatmaps, and sparks that do not fit instead of shrinking
below those floors. Lists and adapter cards MUST consume the widget rectangle: row or card height is inner height
divided by the visible count, not a theoretical maximum budget. An AC-only power tile centers `AC` and `no battery`.
Ranked-row slide, 320 ms value eases, 60-sample histories, and a brief accent pulse while a utilization or capacity
KPI remains at or above 85% stay sample-driven. Decorative per-panel glow and idle breathing are not used; history
recency fade and a live-edge highlight are sample-driven. Empty and `Unavailable` values render as muted em dashes; an
AC-only desktop renders compact `AC` status, never invented zeros. Widget-local sparkline history is at most 60 samples
in fixed storage and MUST NOT move into System Data. Eases use a 320 ms ease-out; animation is sample-driven:
`GetNextFrameDelayMilliseconds` returns 1 while an ease or pulse is in flight, then the dataset interval. A settled
System page MUST NOT request continuous frames.

Shared GPU resources live once per Process Viewer device, not once per widget instance: build-time Shader Model 5.0
blobs, one instanced panel/bar/heatmap/glyph pipeline, one 1024×1024 `R8` atlas, and one dynamic instance buffer.
DirectWrite and system fonts load only while filling new atlas glyphs, then release. Visible process, adapter, and
interface names rasterize only when the displayed string set changes. Each visible widget frame maps its instance
buffer and issues one `DrawInstanced`. The generic widget root MUST NOT grow plugin drawing records. Snapshot delivery
updates fixed storage on the acquisition worker and MUST NOT render; the host coalesced UI invalidation starts the
ease. Hidden, minimized, display-off, occluded, and detached widgets stop eases, drain subscriptions, and do not
replay missed motion.

`Plugins/StudioClock` exposes settings-visible plugin ID `builtin.studio-clock`, internally maps it to type ID
`studio-clock`, and exposes sibling `IRedXeGpuWidget` and `IRedXeScheduledWidget` interfaces on one controlling
`IUnknown`. Its provider returns one of two module-static immutable descriptors: without a date the design size is
720×720 with a 160×160 minimum; with a date the design size is 720×800 with a 160×178 minimum. Neither descriptor
requests continuous animation. Both shipped galleries reference it; the Release first page remains the full-canvas
Matrix composition and therefore creates no Studio Clock provider, widget, deadline, or device resource.

Studio Clock publishes and consumes this complete effective settings object:

```json
{
  "showSecondProgress": true,
  "externalDotsAlwaysOn": true,
  "showSeconds": true,
  "secondsColor": "#FF1616",
  "showDate": false,
  "dateFormat": "dd-mm-yyyy",
  "timeColor": "#FF1616",
  "backgroundColor": "#111111"
}
```

The schema is closed. Colors are exact `#RRGGBB` strings with case-insensitive hexadecimal digits; supported date
orders are `dd-mm-yyyy`, `mm-dd-yyyy`, and `yyyy-mm-dd`. The host merges defaults before factory creation. The plugin
strictly rejects missing effective members, duplicate or unknown members, malformed booleans, colors, and date
formats, converts values once during provider creation, and retains no borrowed JSON.

The clock displays zero-padded local 24-hour `HH:MM` with an always-lit colon. Optional zero-padded seconds and the
clockwise progress ring use `secondsColor`. The ring contains 60 ordinary second positions and one companion at every
multiple of five seconds, including zero; every companion is farther from the center than its ordinary position. At
second `s`, ordinary positions zero through `s` are fully active and future ordinary positions use 18 percent alpha.
By default `externalDotsAlwaysOn` keeps every outward companion fully active. When disabled, only companions whose
represented second is not greater than `s` are fully active and future companions use 18 percent alpha. The four
primary digits use measured oblique seven-segment paths with four dots per segment, while optional seconds use three
dots per segment at the target scale. Main, seconds, and ring dots share the target LED diameter. Optional Gregorian
local date uses smaller three-dot segments in `timeColor` and the selected fixed numeric order. Its two three-dot
hyphens are positioned to leave balanced clear space before the following numeric group. With no date, the largest
fitting square clock is centered in the viewport. With a date, rendering centers an unstretched 10:9 composition whose
upper square remains the clock and whose lower band contains the date wholly below that square. The procedural
composition contains no copied branding, runtime font, DirectWrite, WIC, texture, or loose image asset.

Studio Clock owns four embedded stripped Shader Model 5.0 blobs, one provider-shared immutable shader/state resource
set per device, and one 160-byte dynamic constant buffer per attached widget. A frame issues one opaque fullscreen
triangle and one alpha-blended instanced dot draw. Submitted instances are 114 for `HH:MM`, plus 42 for seconds, 174
for the date, and 72 for the ordinary and five-second companion ring dots, bounded at 402. Constants map once only when
the time bucket, settings, viewport, or DPI-derived state changes; an unrelated continuous sibling frame reuses them.
Rendering performs no heap allocation,
I/O, wait, synchronization, CPU dot loop, runtime shader compilation, timer, worker, or HWND work.

When either seconds element is visible, Studio Clock requests one millisecond after the next local second boundary;
otherwise it requests one millisecond after the next local minute boundary. This post-boundary guard prevents a
coarse or early host wait from rendering the prior wall-clock bucket twice. A detected wall-clock discontinuity
requests one 1 ms corrective frame, after which a fresh guarded boundary is established. Host visibility, power,
suspend, occlusion, and active-page policy owns deadline retention, so missed hidden ticks are not replayed and the
first recovery frame shows current local time. Device loss
preserves validated CPU state, releases the constant buffer and shared device set idempotently, and rebuilds them
transactionally on the replacement device.

`Plugins/DeskClock` exposes settings-visible plugin ID `builtin.desk-clock`, internally maps it to type ID
`desk-clock`, and exposes sibling `IRedXeGpuWidget` and `IRedXeScheduledWidget` interfaces on one controlling
`IUnknown`. Its static descriptor has a 1600×600 design size and 320×120 minimum and does not request continuous
animation. Both shipped galleries reference it. Static Release discovery maps its DLL, while the unchanged first-page
Matrix composition creates no Desk Clock provider, widget, deadline, device resource, or render work.

Desk Clock publishes and consumes this complete effective settings object:

```json
{
  "flipDurationMilliseconds": 420,
  "backgroundColor": "#000000",
  "cardColor": "#FF3B43",
  "digitColor": "#FFFFFF",
  "dateColor": "#D8D8D8"
}
```

The schema is closed. `flipDurationMilliseconds` is an integer from 250 through 800. Colors are exact `#RRGGBB`
strings with case-insensitive hexadecimal digits. The host merges defaults before factory creation; the plugin accepts
only the normalized factory envelope and strictly rejects missing,
duplicate, unknown, malformed, or out-of-range members, converts settings once during provider creation, and retains
no borrowed JSON.

The clock renders zero-padded local 24-hour `HH:MM:SS` as six slim warm-red rounded cards on an opaque background,
with tight within-pair spacing, wider separator gaps, small fixed colon dots, subtle centered hinge seams, and clean
high-contrast condensed digits. Its compact invariant-English date line is title case in `ddd D MMM` form and does not
pad a single-digit day. Desk Clock privately uses DirectWrite during transactional device-resource initialization to
rasterize the required glyphs from the in-box Windows Bahnschrift SemiBold Condensed face, with a deterministic in-box
fallback, into one bounded grayscale atlas. It preserves the font's contours, proportions, common baseline, and real
date advances; it does not stretch individual glyphs into synthetic fixed-width shapes. The system32 DirectWrite
module is loaded through an isolated factory only while building the atlas. Its module handle, factory, font faces,
analysis objects, and temporary CPU coverage are released before initialization returns. Inactive discovery and the
steady render path do no font lookup, shaping, rasterization, or allocation. The composition scales uniformly and
centers within landscape, portrait, minimum, and DPI-adjusted viewports without stretching individual cards. It loads
no WIC, loose font or image asset, or runtime shader compiler.

At a changed local-time sample, all changed tiles share one two-phase transition: the old upper half rotates to the
hinge during the first half, then the new lower half rotates from the hinge during the second half. Unchanged tiles
remain static. Smooth monotonic progress uses measured frame delta, angle-dependent shading remains bounded, and the
final target has no retained moving face. A date change cross-fades the date once. A gap greater than two seconds,
device recovery, or first visible frame snaps to current local time and never replays missed ticks.

The provider owns four embedded stripped Shader Model 5.0 blobs, one 1024×1024 `R8_UNORM` atlas, one 400-byte
dynamic visual constant buffer, three 16-byte immutable instance-offset buffers, and immutable sampler, blend,
rasterizer, and depth states. It creates the set transactionally per provider/device and releases it idempotently.
Geometry comes from `SV_VertexID`; there are no vertex, index, per-tile, off-screen, depth-texture, or post-process
resources. A static frame submits one constant upload only when cached visual state changed, one background draw, one
12-instance card draw, and one 28-instance glyph/separator/date draw. An active flip adds one six-instance moving-face
draw. Thus a frame is bounded to 40 submitted instances and three draws while static or 46 submitted instances and
four draws while animating; logical instance indices remain within 0 through 59.

The first frame samples local time and requests one immediate bootstrap frame when necessary. A static clock requests
the remaining 1–1000 milliseconds to the next local second. A detected changed sample and every active flip frame
request one millisecond; completion returns immediately to the remaining second deadline. Ordinary execution samples
time at most once per displayed second. The plugin owns no timer, worker, HWND, wait, I/O, or synchronization, and its
render path performs no heap allocation. Host visibility, power, suspension, occlusion, active-page, and continuous-
sibling policy owns deadline retention, pacing, and suppression. `WM_TIMECHANGE` invalidates one host frame.

## Required validation

1. Run `./format.ps1` and `./validate-skills.ps1`.
2. Run Debug and Release x64 `test.ps1`; both must pass the plugin contract executable, the production host/plugin
   harness, and the hidden application WARP smoke frame.
3. Build Release ARM64 and confirm the host, plugins, contract tests, and host/plugin harness compile.
4. Verify factory null outputs, unsupported IIDs, non-empty IDs, exact current record sizes, rejection of smaller and
   larger records, every
   configuration pointer/length mismatch, the 8192-byte cap, synchronous configuration copying, and borrowed array
   stability.
5. Verify generic widget/rendering-interface negotiation, root visibility, and controlling-IUnknown identity. The
   GPU-only widget rejects the window IID and the window-only widget rejects the GPU IID.
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
11. Verify through `HostPluginTests` that Release statically discovers every gallery plugin but creates only the
    first-page Matrix runtime, gives it the full design canvas, survives suspend/restore and transactional
    reconfiguration, preserves the active widget after a rejected dashboard, and leaves no Matrix objects after host
    teardown. The `RedXeMatrixRainGetTestDiagnostics` export is a read-only test seam, not a published plugin ABI or
    host dependency.
12. Verify every static contract and effective settings object, exact case-sensitive declaration resolution,
    current-page-only provider creation outside swipes, independent instances, and adaptive composition order.
13. Compile-time checks MUST validate unique bundled plugin IDs and module names, keep every widget projection entry
    backed by one module entry, and keep that projection within the 64-plugin settings limit.
14. Keep `/W4`, `/permissive-`, SDL checks, and warnings-as-errors green.
15. Verify the system-data factory and `IRedXeDataSource`, controlling-IUnknown identity, static descriptors including
    unique IDs and `source.status`, unknown-dataset and malformed-batch rejection, common batch sequence/timestamp,
    sequence advance, summary shape, process-row bounds, current-process visibility, value types and quality,
    CPU/memory/thread tables, network interface/protocol, storage disk/volume, GPU adapter/engine/process, power
    summary, battery list, and thermal/fan sensor tables, and truncated-snapshot behavior through `SystemDataTests`.
    That suite MUST also scan every column ID for prohibited identity fields, require first-sample network rates to be
    `Initializing` and a later elapsed sample to be `Good` when counters exist, require GPU `software` to be Good 0/1
    without requiring a WARP adapter, require adapter utilization and machine-wide GPU memory columns to stay
    `Unavailable`, collect every catalog ID in one batch, create and destroy extra sources, couple AC-only
    `batteryPresent` 0 to a zero-row `battery.list`, and prove that collecting every dataset does not load
    `wbemprox.dll`, `fastprox.dll`, or `wbemcomn.dll`. The Release `--benchmark` MUST exercise two steady 2,048-row
    samples plus an amortized CPU probe, report fixed source storage, CPU/wall time, private bytes, working set, heap,
    and handles, and fail on heap growth, handle growth, or source storage of 16 MiB or more. The Release `--domains`
    measurement MUST time each dataset and one full-catalog batch and fail only on collect failure or a hang exceeding
    five seconds per collection.
16. Verify `IRedXeHost::GetDataProvider` output validation, unknown and non-data plugin rejection, stable provider
    identity, dataset discovery, and shared module loading. Verify two Process Viewer instances share one widget
    provider and host data provider while retaining distinct inactive-until-visible subscriptions, then receive
    bounded live process delivery as GPU scheduled widgets, request zero continuous frames, drain on hide, and fully
    tear down through settings and host integration tests. The hidden interval MUST exceed the two-second dataset
    cadence without another delivered sample. Verify the ten System Data viewer IDs enumerate from `ProcessViewer.dll`,
    reject factory creation without a host, and that a ten-widget System page attaches as GPU+scheduled widgets,
    renders on hidden WARP, rebuilds after device loss, stays non-continuous, and drains every subscription when hidden.
17. Verify Studio Clock defaults and normalized effective settings, every visibility toggle, all date formats, color
    validation, controlling-IUnknown identity, guarded second/minute delays, non-continuous host scheduling,
    transactional reconfiguration, inactive-gallery absence, ordinary 00/01/30/59 ring states, 12 default-always-lit
    outward companions, disabled-option companion counts of 1/1/7/12, four-dot oblique primary and three-dot seconds
    paths, common target LED diameter, balanced date separator gaps, dated/undated descriptor variants,
    square-plus-lower-date-band WARP readback across landscape, portrait, square, and minimum sizes, device recreation,
    zero steady render allocations, one-upload/two-draw and 402-instance bounds, resource sharing,
    five-minute scheduled host soak, and complete teardown through `StudioClockTests` and `HostPluginTests`.
18. Verify Desk Clock defaults and normalized effective settings, strict duration/color validation,
    controlling-IUnknown identity, second-boundary and active-flip scheduling, transactional reconfiguration,
    inactive-gallery absence, deterministic rollover and date-change phases, landscape/portrait/minimum WARP readback,
    reference-aligned slim-card, crisp native-font glyph, common-baseline, and compact real-advance date bounds
    including an unpadded single-digit day, unchanged-tile visibility and pixel stability throughout every flip phase,
    isolated DirectWrite initialization with no inactive or steady font work, device recreation, zero steady render
    allocations, the one-upload/four-draw and 46-instance bounds, absence of persistent DirectWrite, the runtime shader
    compiler, WIC, and loose font/image assets, five-minute scheduled production-host soak, and complete teardown
    through `DeskClockTests`,
    `SettingsTests`, and `HostPluginTests`.

The automated Debug host composition must contain two GPU fixtures, the GDI fixture, and Matrix Rain. The automated
Release first-page composition must contain one full-canvas Matrix widget. The second page of each shipped template
must demonstrate every settings-visible plugin in the compile-time bundled widget projection, including Process Viewer,
Studio Clock, and Desk Clock. The third page of each shipped template is named `System` and MUST place one instance of
every Process Viewer family widget.
Scheduler tests must prove
that hidden, minimized,
suspended, display-off, and occluded states select an event-blocked action. Contract tests must confirm that loading
the GPU plugins does not load `d3dcompiler_47.dll`.
