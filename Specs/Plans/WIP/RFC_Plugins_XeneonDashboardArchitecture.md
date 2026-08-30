# RFC: Plugin-driven XENEON dashboard architecture

Status: `DECISION` — non-normative proposal  
Created: 2026-08-30  
Owner: RedXe plugin, settings, dashboard, and rendering architecture  
Reference: RedSalamander native factory, COM interface, schema, and host-service model

## Purpose

Define the smallest native plugin model needed to build an iCUE-style XENEON EDGE dashboard from application
settings. RedXe discovers data providers and widget providers, connects configured channels to widget instances, and
renders one or more 2560×720 pages.

This RFC is an active decision record, not shipped behavior. Approved requirements will move into:

- `Specs/Plugins/Plugins_API.md`;
- `Specs/Core/Core_Settings.md`;
- `Specs/UI/UI_Dashboard.md`;
- the corresponding validation contracts.

## Decisions

The first implementation uses these decisions:

1. Plugins are trusted native DLLs loaded in-process.
2. `RedXeCreate` directly returns the requested COM interface. There is no generic `IRedXePlugin` root.
3. The host asks for the newest IID it supports and falls back to an older IID only after `E_NOINTERFACE`.
4. Data providers and widget providers are separate interfaces. One logical plugin may expose either or both.
5. RedXe owns the window, pages, settings, Direct3D resources, and final rendering.
6. Plugin DLL additions, removals, updates, and enable-state changes require restart in v1. RedXe does not hot-unload
   modules.
7. Widget rendering is display-only in the first milestone. Input interfaces are added only when a concrete widget
   needs them.

## Scope

The first implementation must support:

- multiple logical plugins in one DLL;
- normalized numeric, integer, Boolean, and text channels;
- sensor-list, gauge, clock, graph, and animated visual widgets, with weather using the same interface after its
  separate network policy is approved;
- multiple dashboard pages and widget instances;
- host-generated plugin and widget configuration UI;
- missing or disabled plugin placeholders that preserve settings;
- x64 and ARM64 builds.

The first implementation does not provide:

- iCUE binary compatibility;
- an HTML or scripting runtime;
- process isolation for untrusted plugins;
- direct plugin access to the swap chain or mutable D3D context;
- runtime DLL replacement or hot unload;
- inline storage of passwords, tokens, or other secrets.

## Ownership

| Concern | Owner |
| --- | --- |
| XENEON discovery, window mode, DPI, and HWND | `Application` |
| Direct3D device, swap chain, targets, and presentation | `Renderer` |
| DLL discovery, catalog, creation, and diagnostics | `PluginManager` |
| Channel catalog, latest values, and coalescing | `DataBroker` |
| Pages, layout, style inheritance, and placeholders | `DashboardHost` |
| Data acquisition and channel meaning | Data-provider plugin |
| Widget types, options, and drawing commands | Widget-provider plugin |
| Settings persistence and configuration UI | RedXe host |

Plugins never own the top-level window, message loop, display policy, settings window, or D3D device lifecycle.

## Factory contract

The public header follows the RedSalamander factory shape:

```cpp
struct RedXeFactoryOptions
{
    uint32_t sizeBytes;
    uint32_t debugLevel;
    uint32_t reserved[8];
};

struct RedXePluginMetadata
{
    uint32_t sizeBytes;
    const wchar_t* id;
    const wchar_t* displayName;
    const wchar_t* description;
    const wchar_t* author;
    const wchar_t* version;
    uint32_t capabilities;
    uint32_t reserved[8];
};

extern "C"
{
    __declspec(dllexport)
    HRESULT __stdcall RedXeCreate(
        REFIID interfaceId,
        const RedXeFactoryOptions* options,
        IRedXeHost* host,
        const wchar_t* pluginId,
        void** result) noexcept;

    __declspec(dllexport)
    HRESULT __stdcall RedXeEnumeratePlugins(
        const RedXePluginMetadata** metadata,
        uint32_t* count) noexcept;

    __declspec(dllexport)
    HRESULT __stdcall RedXeGetConfigurationSchema(
        const wchar_t* pluginId,
        const char** schemaJsonUtf8) noexcept;

    __declspec(dllexport)
    void __stdcall RedXePluginShutdown() noexcept;
}
```

Only `RedXeCreate` is required. The other exports are optional:

- `RedXeEnumeratePlugins` advertises multiple logical plugins. Without it, the DLL is treated as one plugin and the
  factory receives a null or empty `pluginId`.
- `RedXeGetConfigurationSchema` returns logical-plugin configuration without creating an instance.
- `RedXePluginShutdown` is needed only for DLL-global workers or resources. It is called after all plugin objects are
  stopped and released, and must be idempotent.

Enumeration and schema lookup intentionally do not take an IID. Metadata describes all capabilities of one logical
plugin in one pass, and plugin-level configuration is shared by those capabilities. Widget-type configuration remains
part of each widget descriptor.

### Factory behavior

- A null output parameter returns `E_POINTER`.
- Every output is cleared before other validation.
- An unsupported IID returns `E_NOINTERFACE`.
- A null or empty ID is valid only for a single-plugin DLL; it returns `E_INVALIDARG` when ambiguous.
- An unknown non-empty ID returns `HRESULT_FROM_WIN32(ERROR_NOT_FOUND)`.
- Successful creation returns one caller-owned COM reference.
- Enumeration returns between 1 and 256 contiguous metadata records owned by the module.
- Metadata and borrowed schema strings remain valid until process shutdown.
- Invalid metadata or duplicate IDs disable the affected catalog entry without terminating RedXe.

A shared factory helper will implement these rules so every bundled plugin has the same error behavior.

## Interface compatibility

- A published IID has an immutable vtable and method semantics.
- A breaking change receives a new interface name and IID, for example `IRedXeWidgetProvider2`.
- The host requests the newest IID first and retries an explicitly supported older IID only on `E_NOINTERFACE`.
- The host contains an adapter for every older IID it claims to support.
- Version strings and structure sizes never select a COM interface.
- Extensible records use one leading `sizeBytes`. Consumers reject an undersized required prefix, accept larger
  records, and ignore unknown tail bytes.
- The factory signature remains stable. A new export is introduced only if that signature itself must change.
- No separate ABI-generation negotiation table is used.

Compatibility begins with the first published interface set. The v1 implementation does not invent an unused legacy
IID or adapter.

## Identifiers and metadata

- Plugin IDs are stable, non-localized ASCII identifiers such as `builtin.hardware-sensors`.
- Widget type IDs and channel IDs are stable within their owning plugin.
- IDs are 1–128 characters, start with an alphanumeric character, and otherwise use `[A-Za-z0-9_.-]`.
- IDs are compared case-insensitively with ordinal Windows semantics; the host persists the plugin's canonical case.
- Display names and descriptions are localized by the plugin.
- Capability flags state whether a logical plugin provides data, widgets, or both.

Real IIDs are generated when the public headers are approved. Placeholder or reused GUIDs are not allowed.

## Plugin interfaces

The factory returns `IRedXeDataProvider` or `IRedXeWidgetProvider` directly. The created object may expose
`IRedXeConfigurable` through `QueryInterface`.

### Configuration

`IRedXeConfigurable` provides three operations:

- get the static UTF-8 configuration schema;
- transactionally apply one complete UTF-8 JSON object;
- return a canonical UTF-8 JSON object allocated with `CoTaskMemAlloc`.

Failed configuration leaves the previous live state unchanged. Unknown plugin-owned members are preserved when
possible. A plugin-specific settings HWND is not part of v1.

### Data provider

`IRedXeDataProvider`:

1. enumerates channel descriptors into a host sink;
2. starts publishing samples to a host sink;
3. stops publishing.

A channel descriptor contains a stable ID, localized name, value kind, semantic, unit, useful range, decimals, and
recommended update interval. A sample contains the channel ID, timestamp, quality, and one typed value.

Descriptor and sample strings are borrowed only during the synchronous callback. The host copies retained data.
Publishing may occur on plugin workers. `StopPublishing` is idempotent and returns only after no new callback can
begin and every in-flight callback has completed.

### Widget provider

`IRedXeWidgetProvider`:

1. enumerates widget type descriptors into a host sink;
2. creates an isolated widget instance from a type ID and instance ID.

A widget descriptor contains its stable type ID, localized labels, default/minimum size, binding schema, and
configuration schema. A widget instance accepts configuration and builds one frame through a host-owned frame builder.

The widget receives resolved bound values and dimensions for its own local canvas. It never receives the swap chain,
render target, mutable D3D context, or an HWND.

### Host services

`IRedXeHost` is an empty `IUnknown` root. Optional services are queried by IID:

- logging with plugin identity and severity;
- widget invalidation, coalesced by the host;
- immutable image/icon asset registration when the first asset-using widget requires it.

Host services copy caller strings before returning and marshal any UI work internally. Exact service methods and IIDs
belong in the normative API spec, not this architecture RFC.

## Rendering boundary

Widgets emit a small set of normalized commands into `IRedXeFrameBuilder`: rectangles, text, icons/images, progress
bars, simple gauges, and clip push/pop. Command request records use `sizeBytes`, widget-local coordinates, and borrowed
strings valid only for the call.

The frame builder is valid only during `BuildFrame` and must not be retained. The renderer validates and executes the
commands, owns all GPU resources, and handles resize and device loss.

The exact command record layouts are frozen in `Specs/Plugins/Plugins_API.md` before implementation. They are not
speculated in this RFC.

## Settings model

The settings document remains strict JSON and yyjson-compatible. Its essential shape is:

```json
{
  "formatVersion": 1,
  "plugins": {
    "customModulePaths": [],
    "disabledPluginIds": [],
    "configurationByPluginId": {
      "builtin.hardware-sensors": {
        "pollIntervalMilliseconds": 500
      }
    }
  },
  "dashboard": {
    "activePageId": "page.performance",
    "pages": [
      {
        "id": "page.performance",
        "name": "Performance",
        "style": {
          "backgroundColor": "#FF000000",
          "fontFamily": "Consolas"
        },
        "widgets": [
          {
            "id": "widget.fans",
            "pluginId": "builtin.sensor-widgets",
            "typeId": "sensor-list",
            "boundsDip": {
              "x": 16,
              "y": 16,
              "width": 800,
              "height": 288
            },
            "configuration": {
              "showIcons": true,
              "showBar": true
            },
            "bindings": {
              "sensors": [
                {
                  "providerPluginId": "builtin.hardware-sensors",
                  "channelId": "fan.gigabyte.1"
                }
              ]
            },
            "style": {
              "inheritPage": true
            }
          }
        ]
      }
    ]
  }
}
```

The host owns document format, plugin paths, enable state, pages, layout, bindings, and style inheritance. Plugins own
their configuration objects and binding-specific extension members.

Settings are parsed and validated as a complete candidate, then published atomically. Saving uses an atomic sibling
replacement. Unknown plugin-owned members and unresolved widget/plugin references round-trip unchanged. Unknown
host-owned fields require a recognized migration or fail closed.

Page and widget bounds use the 2560×720 design canvas. Page style supplies defaults; widget style overrides apply only
when inheritance is disabled.

## Settings UI

RedXe owns one settings experience with:

- a live page preview and page selector;
- add, move, resize, order, duplicate, and remove widget operations;
- a plugin catalog with path, version, capabilities, state, and diagnostics;
- schema-generated plugin and widget controls;
- Apply/Cancel draft semantics.

The schema renderer initially supports text, number, Boolean, enum, multi-select, color, font, slider, and channel
picker fields. It also supports `x-ui-section`, `x-ui-order`, and `x-ui-hidden`. Unsupported metadata must not delete
persisted values.

Configuration and page edits may apply live. Module path, binary, and enable-state changes are saved but marked
"restart required."

## Discovery and startup

1. Discover bundled DLLs under `Plugins\` beside the executable.
2. Add absolute paths from `plugins.customModulePaths`.
3. Normalize and deduplicate paths case-insensitively; never search the current working directory.
4. Load with safe `LoadLibraryExW` search flags restricted to the module directory and Windows system directories.
5. Read optional metadata and build the logical plugin catalog.
6. Reject malformed, duplicate, disabled, or unsupported entries with diagnostics.
7. Call `RedXeCreate` directly for the required provider IID, using newest-IID-first fallback.
8. Apply configuration, enumerate channels/widget types, create configured widgets, then start data publication.

Custom DLLs run with the user's authority. The settings UI states this clearly.

## Threading and failure

- Factory, enumeration, configuration, widget creation, and `BuildFrame` run on host-defined non-reentrant threads.
- Data publication is the only v1 callback allowed from plugin worker threads.
- The host copies callback data and coalesces it before touching dashboard or renderer state.
- `BuildFrame` consumes cached state and does not perform hardware, network, disk, process, or long-lock work.
- Exceptions never cross an exported function, COM method, callback, or Win32 boundary.
- A failed widget renders a host-owned error placeholder without stopping other widgets.
- A failed provider marks its channels unavailable and may retry with bounded backoff.
- An in-process access violation cannot be isolated; untrusted plugins require a future process boundary.

## Shutdown

RedXe does not unload plugin modules at runtime in v1. On process shutdown it:

1. stops accepting new dashboard work;
2. calls `StopPublishing` and waits for callback quiescence;
3. releases widget, provider, configuration, sink, and host-service references;
4. calls optional `RedXePluginShutdown` once per loaded module;
5. leaves DLL unloading to process teardown.

No plugin callback may occur after its stop operation completes.

## Implementation sequence

### 1. Public contract and dummy plugin

- Freeze factory signatures, metadata, record prefixes, IIDs, HRESULT behavior, ownership, and threading.
- Add a shared factory helper and a dummy DLL exposing data and widget logical plugins.
- Add x64 and ARM64 ABI tests.

### 2. Settings and data broker

- Implement versioned settings, atomic persistence, migrations, and unresolved-reference round trips.
- Implement channel discovery, latest-value caching, publication coalescing, and provider shutdown.

### 3. Widget host and renderer bridge

- Implement page layout, binding resolution, widget instances, and the validated frame builder.
- Extend the WARP smoke test with a deterministic multi-widget page.

### 4. Settings UI and bundled plugins

- Implement the page editor and schema-generated controls.
- Add hardware sensor, sensor widget, clock, and one animated visual plugin through the public interface.
- Add weather only after network and credential requirements are separately approved.

## Required validation

- Factory null-output, unknown-ID, ambiguous-ID, unsupported-IID, and newest-IID fallback behavior.
- Metadata count bounds, record prefixes, borrowed string lifetime, duplicate IDs, and malformed entries.
- Current and oversized records accepted; undersized records rejected without corrupting output.
- COM references and callbacks fully quiescent after provider stop.
- Settings parsing, migration, atomic save, unknown plugin member preservation, and unresolved-widget round trip.
- Schema field rendering and Apply/Cancel behavior.
- Provider flood/coalescing, stale/unavailable/error values, and bounded memory.
- Widget clipping, invalid commands, missing assets, frame failure, resize, and device loss.
- WARP rendering without a hardware GPU and live 2560×720 XENEON validation.
- Representative 60 Hz dashboard frame timing and startup/module-load measurements.

## Remaining details before implementation

1. Assign the initial IIDs and freeze exact method/record layouts.
2. Choose the settings file location, portable mode, command-line override, and recovery policy.
3. Freeze the first frame-builder command set and asset-handle rules.
4. Define the bounded channel-history policy needed by graph widgets.

These details do not change the direct-factory architecture.

## Closeout criteria

This RFC moves to `Specs/Plans/Done/` only after:

- the remaining details are resolved in normative contracts;
- public headers and shared factory tests match those contracts;
- one multi-plugin DLL, one data provider, and multiple widget types use the interface;
- settings recreate a multi-page dashboard including unresolved-plugin placeholders;
- x64/ARM64 ABI, WARP, live XENEON, teardown, and performance validation pass;
- durable requirements are merged into the owning domain specs and the WIP index is updated.
