# RFC: Plugin-driven XENEON dashboard architecture

Status: `DECISION` — non-normative proposal  
Created: 2026-08-30  
Owner: RedXe plugin, settings, dashboard, and rendering architecture  
Reference implementation studied: RedSalamander native plugin factory and host-service model

## Purpose

Define a plugin architecture that can build an iCUE-style XENEON EDGE dashboard from application settings. The host
loads a set of normalized native plugins, discovers their data channels and widget types, and composes configured
widgets into one or more 2560×720 pages.

This RFC is an active decision record. It does not define shipped RedXe behavior. Once approved and implemented, its
durable requirements are expected to move into future normative contracts:

- `Specs/Plugins/Plugins_API.md`
- `Specs/Core/Core_Settings.md`
- `Specs/UI/UI_Dashboard.md`
- relevant testing and performance contracts

## Reference scenario

The supplied design reference shows the intended composition model:

- multiple dashboard pages with an active-page selector;
- several independently placed widgets on one 32:9 canvas;
- sensor-list widgets binding fan, temperature, load, memory, and bus values;
- clock, animated visual, and weather widgets;
- widget size presets and per-widget toggles;
- one or more sensor/channel bindings with individual accent colors;
- page-level styling inherited by widgets unless custom widget styling is enabled;
- brightness, blur, typography, background, icons, bars, decimals, and row-background controls.

The proposal treats these as host-owned page and widget-instance settings. Plugins provide normalized capabilities and
rendering content; they do not own the dashboard window or settings application.

## Goals

- Compose a complete dashboard from JSON settings without recompiling the host.
- Support multiple logical plugins per DLL and multiple widget instances per plugin.
- Separate data acquisition from widget presentation so a widget can bind channels from any compatible provider.
- Keep page navigation, layout, theme inheritance, settings persistence, Direct3D ownership, and error presentation in
  the RedXe host.
- Use a native, HRESULT-based, COM-style ABI that works on x64 and ARM64.
- Make ownership, threading, callback shutdown, module unload, and ABI evolution explicit from the first version.
- Preserve configurations and widget instances when a plugin is temporarily missing or disabled.
- Permit bundled and explicitly configured third-party modules to use the same interface.

## Non-goals for the first implementation

- Binary compatibility with iCUE plugins or use of undocumented iCUE interfaces.
- Letting a plugin replace the RedXe top-level HWND, message loop, display policy, or swap chain.
- Giving plugins unrestricted access to the host's `ID3D11DeviceContext` or mutable renderer state.
- Running untrusted plugins safely in-process. Process isolation is a possible later generation.
- A general web, HTML, or scripting runtime.
- Persisting passwords, API tokens, or other secrets in ordinary JSON settings.

## Proposed architecture

```text
RedXe application
├── SettingsStore
│   ├── plugin module/configuration settings
│   └── dashboard pages, widget instances, bindings, and styles
├── PluginManager
│   ├── module discovery and interface compatibility
│   ├── logical plugin catalog
│   └── lifecycle, diagnostics, and unload coordination
├── DataBroker
│   ├── provider/channel catalog
│   ├── subscriptions and latest-value cache
│   └── worker-to-render-thread coalescing
├── DashboardHost
│   ├── pages and active-page selection
│   ├── widget layout and settings editor
│   └── unresolved-plugin placeholders
└── Renderer
    ├── host-owned Direct3D/DXGI resources
    └── normalized widget drawing-command execution

Plugin modules
├── data providers       hardware sensors, weather, external services
├── widget providers     sensor list, gauge, clock, matrix, graph
└── combined plugins     a provider and one or more matching widget types
```

### Ownership boundary

| Concern | Proposed owner |
| --- | --- |
| XENEON discovery, fullscreen/windowed mode, DPI, HWND | RedXe `Application` |
| Direct3D device, swap chain, render targets, presentation | RedXe `Renderer` |
| Module loading, interface discovery, enable/disable, unload | `PluginManager` |
| Polling/subscription normalization and latest values | `DataBroker` |
| Pages, widget placement, z-order, selection, page switching | `DashboardHost` |
| Page style and widget style inheritance | `DashboardHost` |
| Data acquisition and channel meaning | Data-provider plugin |
| Widget-specific options and drawing commands | Widget plugin |
| Host-generated configuration controls | RedXe settings UI |

## RedSalamander concepts retained

The proposal carries forward these proven patterns:

- DLL discovery plus one DLL exposing multiple logical plugin IDs;
- one exported factory surface and COM `IUnknown` objects;
- a minimal `IRedXeHost` root extended through `QueryInterface`;
- stable long plugin IDs used in settings;
- plugin metadata plus JSON configuration schemas;
- HRESULT/noexcept ABI boundaries and WIL `com_ptr` ownership in the host;
- bounded enumeration, duplicate-ID rejection, and explicit string ownership;
- module quiet-point exports for global workers and driver-backed resources;
- fixed same-IID vtables, leading `sizeBytes` in extensible records, and new IIDs for breaking evolution.

The loader intentionally stays simple: a permanently stable C factory creates the COM root, and normal
`QueryInterface` calls provide capability discovery and interface-version fallback.

## Module entry points and interface negotiation

Each module exports one permanently stable factory and may export the catalog, schema, and module quiet-point helpers:

```cpp
struct RedXeFactoryOptions
{
    uint32_t sizeBytes;
    uint32_t debugLevel;
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

    __declspec(dllexport) void __stdcall RedXePluginShutdown() noexcept;
    __declspec(dllexport) BOOL __stdcall RedXePluginCanUnloadNow() noexcept;
}
```

Proposed compatibility rules:

- `RedXeCreate` is the only required export, and its signature never changes.
- `RedXeEnumeratePlugins` is optional. If it is absent, the DLL represents one logical plugin and `pluginId` is null or
  empty when the factory is called.
- The static schema and module quiet-point exports are optional. A created object may expose `IRedXeConfigurable` for
  instance configuration, while quiet-point exports are required only for modules with global workers or resources.
- The host asks for the newest IID it supports. On `E_NOINTERFACE`, it asks for the immediately supported older IID and
  uses a host-side adapter. It does not fallback for any other failure.
- A vtable never changes for an existing IID. A breaking method-set change creates a new interface and IID, such as
  `IRedXeWidgetProvider2`; optional capabilities use their own IIDs.
- `sizeBytes` supports append-only growth of plain data records such as options and metadata. It does not version COM
  interfaces or replace IID negotiation.
- The host bounds one module to at most 256 logical plugin entries.
- A missing required factory, invalid metadata, duplicate ID, unsupported architecture, or unsupported IID produces a
  disabled catalog entry with diagnostics; it does not terminate RedXe.
- If the factory boundary itself ever needs to change, add a new explicitly versioned export then. Do not add a generic
  ABI-generation protocol without a concrete incompatible factory requirement.

## Metadata and identifiers

```cpp
enum RedXePluginCapability : uint32_t
{
    REDXE_PLUGIN_CAPABILITY_DATA_PROVIDER   = 0x00000001,
    REDXE_PLUGIN_CAPABILITY_WIDGET_PROVIDER = 0x00000002,
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
```

Identifier proposal:

- Plugin IDs are stable, non-localized, ASCII identifiers such as `builtin.hardware-sensors` or
  `com.example.weather`.
- Widget type IDs and provider channel IDs are stable within their owning plugin.
- Settings store plugin ID and local ID separately instead of constructing an ambiguous path string.
- IDs are 1–128 characters, use `[A-Za-z0-9_.-]`, begin with an alphanumeric character, and are unique under ordinal
  case-insensitive comparison. The host persists the canonical spelling returned by the plugin.
- Display names and descriptions are already localized by the plugin; IDs never change with locale.
- Metadata strings are module-owned and remain valid until module unload.

Actual IIDs are generated only when the interface header is approved and committed. Placeholder or reused GUIDs are
not acceptable.

## Root plugin and configuration interfaces

The factory normally creates the stable `IRedXePlugin` root; optional capabilities are discovered with
`QueryInterface`. If the root itself eventually needs a breaking revision, the host requests `IRedXePlugin2` from
`RedXeCreate` first and retries with `IRedXePlugin` only when the first call returns `E_NOINTERFACE`. The same
newest-IID-first rule applies when querying versioned capability interfaces on an existing root object.

The host supports only IIDs and adapters that it explicitly implements and tests. It does not infer compatibility from
plugin versions, metadata strings, or structure sizes.

```cpp
// IID assigned when the ABI is approved.
interface __declspec(novtable) IRedXePlugin : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Start() noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Stop() noexcept = 0;
};

interface __declspec(novtable) IRedXeConfigurable : public IUnknown
{
    // Static UTF-8 schema; plugin-owned until module unload.
    virtual HRESULT STDMETHODCALLTYPE GetConfigurationSchema(
        const char** schemaJsonUtf8) noexcept = 0;

    // Complete UTF-8 JSON object. Application is transactional.
    virtual HRESULT STDMETHODCALLTYPE SetConfiguration(
        const char* configurationJsonUtf8) noexcept = 0;

    // CoTaskMemAlloc UTF-8 JSON object; host frees with CoTaskMemFree.
    virtual HRESULT STDMETHODCALLTYPE GetConfiguration(
        char** configurationJsonUtf8) noexcept = 0;
};
```

Configuration proposal:

- `SetConfiguration` parses and validates the full candidate before changing live state.
- Failure leaves the previous configuration and derived resources unchanged.
- `GetConfiguration` returns the canonical normalized object that should be persisted.
- Unknown object members are preserved when practical so a newer configuration can survive an older UI.
- Secrets never appear in schemas or returned configuration. A future host credential service receives a separate IID.
- The host renders common field types: boolean, integer, number, text, enum, multi-select, color, font, slider, file
  path, and data-channel picker.
- A plugin-specific custom HWND settings page is deferred. This keeps theme, DPI, accessibility, transaction, and
  persistence behavior host-owned in the initial release.

## Data-provider interface

Data providers publish normalized channels into the host `DataBroker`.

```cpp
enum RedXeValueKind : uint32_t
{
    REDXE_VALUE_NUMBER  = 1,
    REDXE_VALUE_INTEGER = 2,
    REDXE_VALUE_BOOLEAN = 3,
    REDXE_VALUE_TEXT    = 4,
};

enum RedXeValueQuality : uint32_t
{
    REDXE_QUALITY_GOOD        = 1,
    REDXE_QUALITY_STALE       = 2,
    REDXE_QUALITY_UNAVAILABLE = 3,
    REDXE_QUALITY_ERROR       = 4,
};

struct RedXeChannelDescriptor
{
    uint32_t sizeBytes;
    const wchar_t* channelId;
    const wchar_t* displayName;
    const wchar_t* groupName;
    const wchar_t* semantic; // temperature, fan-speed, utilization, clock, weather, ...
    const wchar_t* unit;     // Cel, rpm, %, byte, bit/s, or plugin-defined display unit
    RedXeValueKind valueKind;
    double suggestedMinimum;
    double suggestedMaximum;
    uint32_t suggestedDecimals;
    uint32_t recommendedUpdateMilliseconds;
    uint32_t reserved[8];
};

struct RedXeDataSample
{
    uint32_t sizeBytes;
    const wchar_t* channelId;
    int64_t timestampUnixMilliseconds;
    RedXeValueKind valueKind;
    RedXeValueQuality quality;
    double numberValue;
    int64_t integerValue;
    BOOL booleanValue;
    const wchar_t* textValue;
    uint32_t reserved[8];
};

interface __declspec(novtable) IRedXeChannelSink : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE AddChannel(
        const RedXeChannelDescriptor* descriptor) noexcept = 0;
};

interface __declspec(novtable) IRedXeDataSink : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Publish(
        const RedXeDataSample* samples,
        uint32_t count) noexcept = 0;
};

interface __declspec(novtable) IRedXeDataProvider : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE EnumerateChannels(
        IRedXeChannelSink* sink) noexcept = 0;

    virtual HRESULT STDMETHODCALLTYPE StartPublishing(
        IRedXeDataSink* sink) noexcept = 0;

    virtual HRESULT STDMETHODCALLTYPE StopPublishing() noexcept = 0;
};
```

Data rules under consideration:

- Channel descriptors and sample strings are borrowed only for the duration of the synchronous call; the host copies
  what it retains.
- `Publish` may be called from plugin worker threads. The host callback is thread-safe and coalesces values for the
  render thread.
- A plugin retains the sink only between successful `StartPublishing` and completed `StopPublishing`.
- `StopPublishing` is idempotent and returns only after no new callback can begin. Any in-flight callback must finish
  before the provider is released.
- The host keeps the latest sample, timestamp, quality, and optional bounded history per subscribed channel.
- Widget instances bind a `(providerPluginId, channelId)` pair. Missing channels remain configured and render an
  unavailable state rather than being deleted.

## Widget-provider interface

A widget provider describes reusable widget types and creates isolated widget instances.

```cpp
struct RedXeWidgetTypeDescriptor
{
    uint32_t sizeBytes;
    const wchar_t* typeId;
    const wchar_t* displayName;
    const wchar_t* description;
    uint32_t defaultWidthDip;
    uint32_t defaultHeightDip;
    uint32_t minimumWidthDip;
    uint32_t minimumHeightDip;
    const char* bindingSchemaJsonUtf8;
    const char* configurationSchemaJsonUtf8;
    uint32_t reserved[8];
};

interface __declspec(novtable) IRedXeWidgetTypeSink : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE AddWidgetType(
        const RedXeWidgetTypeDescriptor* descriptor) noexcept = 0;
};

interface __declspec(novtable) IRedXeWidgetProvider : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE EnumerateWidgetTypes(
        IRedXeWidgetTypeSink* sink) noexcept = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateWidget(
        const wchar_t* typeId,
        const wchar_t* instanceId,
        IRedXeWidget** widget) noexcept = 0;
};
```

The binding schema lets the host generate selectors such as Sensor 1 and Sensor 2 without the widget knowing how the
settings application implements a combo box. A proposed schema fragment is:

```json
{
  "version": 1,
  "slots": [
    {
      "key": "sensors",
      "label": "Sensors",
      "cardinality": "many",
      "acceptedValueKinds": ["number", "integer"],
      "acceptedSemantics": ["temperature", "fan-speed", "utilization", "memory"]
    }
  ]
}
```

## Widget instance and host-owned rendering

Plugins do not receive the swap chain or mutable D3D device context. A widget emits normalized commands into a
host-owned frame builder.

```cpp
struct RedXeBoundValue
{
    uint32_t sizeBytes;
    const wchar_t* bindingKey;
    const wchar_t* providerPluginId;
    RedXeDataSample sample;
};

struct RedXeWidgetFrameContext
{
    uint32_t sizeBytes;
    uint32_t widthPixels;
    uint32_t heightPixels;
    uint32_t dpi;
    double elapsedSeconds;
    double deltaSeconds;
    const RedXeBoundValue* values;
    uint32_t valueCount;
    uint32_t reserved[8];
};

interface __declspec(novtable) IRedXeFrameBuilder : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE FillRectangle(/* sized request */) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawText(/* sized request */) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawIcon(/* sized request */) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawProgressBar(/* sized request */) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawGauge(/* sized request */) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE PushClip(/* sized request */) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE PopClip() noexcept = 0;
};

interface __declspec(novtable) IRedXeWidget : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SetConfiguration(
        const char* configurationJsonUtf8) noexcept = 0;

    virtual HRESULT STDMETHODCALLTYPE GetConfiguration(
        char** configurationJsonUtf8) noexcept = 0;

    virtual HRESULT STDMETHODCALLTYPE BuildFrame(
        const RedXeWidgetFrameContext* context,
        IRedXeFrameBuilder* builder) noexcept = 0;
};
```

The exact drawing request structures remain an approval item. Each will use a leading `sizeBytes`, design-space
coordinates local to the widget, host-resolved theme colors/fonts, borrowed input strings for the duration of the
call, and no retained builder pointer.

This boundary allows native clock, sensor list, bar, gauge, graph, weather, and animated matrix widgets while keeping
device-loss recovery and D3D state inside `Renderer`. A later high-performance interface may accept immutable host
resource handles or command batches; direct device-context access is not the default.

## Minimal host services

`IRedXeHost` is an empty `IUnknown` root. Plugins query optional service IIDs:

- `IRedXeHostLog`: thread-safe structured diagnostics with plugin ID and severity.
- `IRedXeHostInvalidation`: request that a widget instance be redrawn; the host coalesces requests.
- `IRedXeHostAssets`: register or resolve immutable image/icon assets without exposing D3D ownership.
- a future `IRedXeHostCredentials`: retrieve protected secrets by opaque setting/profile ID without exposing them in
  plugin configuration JSON.

Host-service methods copy caller-provided strings before returning. UI work is marshalled internally. Plugins do not
send private Win32 messages to the RedXe window and do not store raw HWNDs unless a later interface explicitly grants
one.

## Proposed application settings

The current single-value embedded sample setting is expected to evolve into a versioned settings document. This
example is strict JSON compatible with yyjson:

```json
{
  "formatVersion": 1,
  "plugins": {
    "customModulePaths": [
      "C:\\Program Files\\RedXe Plugins\\WeatherPlugin.dll"
    ],
    "disabledPluginIds": [],
    "configurationByPluginId": {
      "builtin.hardware-sensors": {
        "pollIntervalMilliseconds": 500
      },
      "com.example.weather": {
        "location": "Paris",
        "units": "metric"
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
          "backgroundBrightness": 0.2,
          "glassBlurDip": 0,
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
            "zIndex": 0,
            "configuration": {
              "showDecimals": false,
              "showIcons": true,
              "showBar": true,
              "showRowBackground": false
            },
            "bindings": {
              "sensors": [
                {
                  "providerPluginId": "builtin.hardware-sensors",
                  "channelId": "fan.gigabyte.1",
                  "accentColor": "#FFFF922E"
                },
                {
                  "providerPluginId": "builtin.hardware-sensors",
                  "channelId": "fan.gigabyte.2",
                  "accentColor": "#FFFF922E"
                }
              ]
            },
            "style": {
              "inheritPage": true
            }
          },
          {
            "id": "widget.clock",
            "pluginId": "builtin.clock",
            "typeId": "digital-clock",
            "boundsDip": {
              "x": 80,
              "y": 352,
              "width": 560,
              "height": 288
            },
            "zIndex": 0,
            "configuration": {
              "showSeconds": true,
              "showDate": true
            },
            "bindings": {},
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

### Settings ownership

Host-owned fields:

- `formatVersion` and migrations;
- plugin paths, disabled IDs, and the per-plugin configuration map;
- active page, page order, page names, page style, widget instance identity, bounds, z-order, plugin/type references,
  channel bindings, and style inheritance.

Plugin-owned fields:

- each object under `plugins.configurationByPluginId[pluginId]`;
- each widget instance's `configuration` object;
- plugin-defined binding-slot option extensions beneath host-validated binding records.

### Proposed C++ settings model

```cpp
struct PluginSettings final
{
    std::vector<std::filesystem::path> customModulePaths;
    std::vector<std::wstring> disabledPluginIds;
    std::unordered_map<std::wstring, OwnedJsonValue> configurationByPluginId;
};

struct ChannelBinding final
{
    std::wstring providerPluginId;
    std::wstring channelId;
    OwnedJsonValue options;
};

struct WidgetInstanceSettings final
{
    std::wstring id;
    std::wstring pluginId;
    std::wstring typeId;
    RectF boundsDip;
    int32_t zIndex = 0;
    OwnedJsonValue configuration;
    OwnedJsonValue bindings;
    OwnedJsonValue style;
};

struct DashboardPageSettings final
{
    std::wstring id;
    std::wstring name;
    OwnedJsonValue style;
    std::vector<WidgetInstanceSettings> widgets;
};

struct AppSettings final
{
    uint32_t formatVersion = 1;
    PluginSettings plugins;
    std::wstring activePageId;
    std::vector<DashboardPageSettings> pages;
};
```

`OwnedJsonValue` is a placeholder for an owning settings representation. It must not retain `yyjson_val*` or borrowed
strings after the source document is freed. An owning tree or canonical serialized UTF-8 object are both candidates.

### Settings validation and persistence

- Parse into a complete candidate, validate cross-references, then publish atomically.
- Save through a temporary sibling and atomic replacement; interrupted writes must not destroy the last valid file.
- Validate unique page and widget IDs, finite bounds, canvas limits, z-order range, plugin IDs, widget type IDs, binding
  shape, and configuration object types.
- Page/widget bounds use the 2560×720 logical design canvas from the windowing contract. The editor may snap to a
  grid, but persisted `boundsDip` is authoritative.
- Per-page style provides defaults. `style.inheritPage == false` enables validated widget overrides such as
  background brightness, blur, font, foreground, and accent colors.
- Missing or disabled plugin references remain as unresolved placeholder widgets. Their configuration and bindings
  round-trip unchanged so re-enabling the plugin restores the page.
- Plugin configuration and widget configuration are applied transactionally and replaced with canonical values
  returned by the plugin.
- Unknown plugin-owned members are preserved. Unknown host-owned fields require a recognized migration or fail closed.
- A generated aggregate schema may insert discovered plugin schemas beneath the configuration maps for settings UI
  and diagnostics, following the RedSalamander model.
- Secrets are referenced by opaque IDs and stored through a future protected credential service, never inline.

## Settings editor workflow

The host settings experience is proposed to provide:

1. A live 2560×720 page preview.
2. Page chips and an Add Page action.
3. A widget catalog populated from `IRedXeWidgetProvider` descriptors.
4. Drag, resize, z-order, duplicate, remove, and page assignment operations owned by the host.
5. A Widget Setup panel generated from the widget configuration and binding schemas.
6. A Widget Personalization panel that defaults to page inheritance and enables widget-specific overrides.
7. A Plugins page showing logical ID, module path, version, capabilities, enable state, load error, and schema-driven
   plugin configuration.
8. Transactional draft settings: preview may update from a draft, Apply validates every affected plugin and commits
   atomically, and Cancel restores the last committed model.

Plugins provide localized labels, descriptions, field metadata, widget thumbnails/icons, and schemas. They do not
create the settings window or control page navigation.

## Discovery and loading proposal

1. Enumerate bundled modules from `Plugins\*.dll` next to the executable.
2. Add absolute user-configured paths from `plugins.customModulePaths`.
3. Normalize paths, deduplicate them case-insensitively, and never search the current working directory.
4. Load with safe `LoadLibraryExW` search flags scoped to the module directory and Windows system directories.
5. Resolve the required `RedXeCreate` export and any optional catalog, schema, or quiet-point exports.
6. Enumerate a bounded metadata list and reject malformed or duplicate logical IDs.
7. Preserve disabled entries in the catalog without creating plugin objects.
8. Create enabled root objects by requesting the newest supported IID and falling back only on `E_NOINTERFACE`; apply
   configuration, query optional capabilities, enumerate channels/widget types, and
   start providers only after the settings candidate is valid.

Custom DLLs run in-process with the user's authority. The settings UI should say this clearly. Signature display and an
optional allowlist are useful, but code signing alone is not process isolation.

## Threading, frame, and failure contract

- Factory, metadata, configuration, widget creation, and `BuildFrame` calls occur on a host-defined thread and are not
  reentrant unless a specific interface says otherwise.
- Data providers may publish from workers only through the thread-safe sink.
- The host copies callback data and marshals/coalesces it before touching dashboard or renderer state.
- Plugins do not call Win32 or D3D methods on host-owned objects from worker threads.
- `BuildFrame` must not block on hardware, network, disk, process launch, or long locks. It consumes cached state only.
- A failed widget frame renders a host-owned error placeholder and does not stop other widgets.
- A failed provider marks its channels unavailable and may retry with bounded backoff.
- C++ exceptions never cross an exported function or COM method boundary. Expected failures use `HRESULT`.
- An access violation inside a trusted in-process plugin cannot be safely isolated. A later out-of-process generation
  is required for untrusted marketplace plugins.

Initial performance targets to validate, not yet normative:

- 60 Hz host presentation on the 2560×720 XENEON.
- No plugin work on the render thread except bounded `BuildFrame` command generation.
- Aggregate widget command generation below 4 ms at the 95th percentile for a representative 12-widget page.
- Sensor publication coalesced so a fast provider cannot grow an unbounded UI queue.
- Bounded history and asset caches with explicit memory measurements.

## Shutdown, disable, and reload ordering

Proposed quiet point:

1. Stop accepting new widget/page work for the affected plugin.
2. Stop provider subscriptions and wait for callback quiescence.
3. Release widget instances and provider/configuration interfaces.
4. Release plugin-held host service references.
5. Call root `Stop()` and release root objects.
6. Call optional module `shutdownModule()` idempotently.
7. Query optional `canUnloadNow()` without blocking.
8. If unload is allowed, release module-owned assets and call `FreeLibrary`.
9. If unload is deferred, keep the DLL mapped, skip same-path reload, surface the state, and retry with bounded
   scheduling until the module becomes unloadable or the process exits.

The host never kills plugin-created independent processes and never unloads a module while callbacks, widget objects,
or driver-backed work may still execute in it.

## Proposed repository shape

```text
src/RedXe/
  PluginInterfaces/
    Factory.h
    Host.h
    Plugin.h
    DataProvider.h
    Widget.h
  PluginManager.*
  DataBroker.*
  DashboardModel.*
  DashboardHost.*
  Settings.*
Plugins/
  HardwareSensors/
  SensorWidgets/
  Clock/
  Matrix/
  Weather/
Specs/
  Plugins/Plugins_API.md
  Core/Core_Settings.md
  UI/UI_Dashboard.md
```

The public ABI headers should be dependency-light, Unicode, C-compatible where practical, and shared by x64 and
ARM64 builds. Implementation helpers remain private and do not leak STL types, exceptions, allocators, or C++ runtime
ownership across the DLL boundary.

## Implementation phases

### Phase 0 — approve architecture

- Resolve the open decisions below.
- Freeze the permanent factory signature plus initial ownership, threading, and string/allocation rules.
- Generate real IIDs only after method sets are reviewed.
- Split approved requirements into normative plugin, settings, dashboard, and test specs.

### Phase 1 — ABI and dummy plugin

- Add dependency-light public headers and a shared factory helper.
- Implement `PluginManager` discovery, IID fallback, catalog, duplicate rejection, and diagnostics.
- Add a dummy module exposing multiple logical plugins.
- Add x64/ARM64 ABI boundary tests for current, undersized, oversized, missing, duplicate, newest-IID, and fallback-IID
  inputs.

### Phase 2 — settings foundation

- Replace embedded sample-only settings with a versioned load/save model and schema.
- Implement opaque plugin/widget JSON ownership with yyjson-safe lifetimes.
- Add atomic persistence, migrations, missing-plugin round trips, and transactional Apply/Cancel tests.

### Phase 3 — data broker

- Implement provider discovery, channel catalog, sink lifetime, coalescing, quality, and bounded history.
- Add fake providers for deterministic timing, shutdown, callback, and queue-pressure tests.

### Phase 4 — widget host and renderer bridge

- Implement page model, layout, binding resolution, missing-widget placeholder, and frame builder.
- Keep D3D resources and device-loss recovery host-owned.
- Extend the WARP smoke test to compose and render a deterministic multi-widget page.

### Phase 5 — settings UI and bundled plugins

- Add the page editor, plugin manager, generated Widget Setup, and Widget Personalization panels.
- Build initial sensor, sensor-list/gauge, clock, matrix, and weather examples through the public interface.
- Validate keyboard access, DPI, localization, theme inheritance, and 2560×720 layout.

## Required test plan before closeout

- Permanent factory signature, newest-IID selection, `E_NOINTERFACE` fallback, and `sizeBytes` prefix behavior on x64
  and ARM64.
- Multiple logical plugins per module and duplicate ID rejection.
- Missing export, invalid metadata, wrong architecture, disabled plugin, missing plugin, and bad configuration behavior.
- COM reference ownership and no callbacks after stop/unsubscribe.
- Module unload success, unload deferral, retry, and process-shutdown retention policy.
- Settings parse, validation, canonical save, migration, atomic replacement, unknown plugin member preservation, and
  unresolved widget round trip.
- Schema-generated controls for booleans, numbers, colors, fonts, enums, channel pickers, and repeated bindings.
- Provider flood/coalescing, stale/unavailable/error quality, bounded history, and teardown under load.
- Widget frame failures, clipping, invalid commands, missing assets, and device loss.
- WARP rendering of a representative multi-page dashboard without a hardware GPU.
- Live XENEON rendering, page switching, widget editing, and per-monitor DPI transitions.
- Frame-time, publication queue, startup, module-load, and memory-retention measurements.

## Open decisions

1. **Compatibility promise:** decide whether v1 documents support for third-party modules or only bundled lockstep
   modules. In either case, publish the permanent factory and immutable IID rules; claim compatibility with an older IID
   only after its adapter and fixture are tested.
2. **Rendering surface:** approve the normalized frame-builder command interface, or define another host-owned retained
   scene representation. Direct mutable D3D context access is not recommended.
3. **Settings location and precedence:** choose the user settings path, portable mode behavior, command-line override,
   and recovery/backup policy.
4. **Built-in packaging:** ship initial plugins as real DLLs, or compile them into the executable behind an adapter
   while keeping identical logical interfaces. Real DLLs provide stronger end-to-end coverage.
5. **Hot reload:** support Apply-time unload/reload in v1, or require restart for module path and enable-state changes
   while still applying ordinary configuration live.
6. **Network policy:** decide whether weather/network plugins use direct WinHTTP or a future host network service with
   proxy, TLS, caching, and rate-limit policy.
7. **Credential service:** define Windows Credential Manager/DPAPI/Windows Hello requirements before any plugin needs
   a secret.
8. **Interaction:** decide whether initial widgets are display-only or need normalized pointer/keyboard actions.
9. **History:** decide which channel types may retain samples, per-channel limits, and whether history persists.
10. **Marketplace trust:** decide whether unsigned custom modules receive only a warning, require an allowlist, or are
    disabled by policy.

## Recommended initial decisions

- Keep `RedXeCreate` permanent and use newest-IID-first `QueryInterface`/factory requests, with explicit fallback
  adapters only for older IIDs the host actually supports.
- Treat v1 custom plugins as trusted, in-process native code and state that clearly.
- Keep Direct3D/DXGI ownership entirely in the host and use a normalized frame builder.
- Separate data providers from widget providers; allow one logical plugin to implement both.
- Ship at least one bundled provider and several bundled widget plugins as DLLs to exercise the real loader.
- Require restart for module add/remove/update in the first UI iteration; allow validated configuration and page edits
  to apply live.
- Keep widgets display-only in the first rendering milestone, then add a sized input-event interface if a concrete
  widget requires interaction.
- Store only bounded latest/history data in memory and do not persist sensor history in format version 1.

## Closeout criteria

This RFC can move to `Specs/Plans/Done/` only when:

- the open decisions required for the first implementation are resolved;
- approved behavior is split into authoritative plugin, settings, dashboard, and validation specs;
- public headers and ownership rules match those specs;
- at least one multi-plugin DLL, one data provider, and multiple widget types use the normalized interface;
- settings can recreate a multi-page XENEON dashboard including unresolved-plugin round trips;
- x64 and ARM64 build/ABI tests, WARP composition tests, live XENEON checks, teardown tests, and performance evidence
  are green;
- the active-plan index is updated and this file is moved rather than copied.
