# RedXe plugin API contract

Status: current normative contract
Last reviewed: 2026-09-18

## Purpose and scope

This contract owns RedXe native plugin discovery, generic widget creation, rendering-interface negotiation, GPU,
native-window, and raised-overlay widget lifetimes, and the bundled demonstration plugins.

Public ABI headers live under `Common/PlugInterfaces/`. `Widget.h` contains the complete generic, GPU, scheduled,
native-window, and raised-overlay widget surface. `Data.h` contains the complete source, provider, snapshot, sink, and
subscription surface. A created widget exposes the mechanisms it supports through `QueryInterface`.

RedXe is pre-production and rebuilds the host and every plugin from one source tree. The repository therefore defines
one current native contract: it does not load older layouts, accept newer record tails, negotiate interface versions,
or preserve a superseded IID/vtable. Interface evolution changes the current headers, host, plugins, tests, and this
contract together. A production ABI freeze requires an explicit future policy change.

The current executable host renders `IRedXeGpuWidget` and hosts `IRedXeWindowWidget` in host-owned child containers.
The native-window path supports GDI, native controls, media hosts, and future WebView implementations without exposing
the RedXe top-level window.

## Retained text services transport

`IRedXeTextInputWidget` is the optional UI-thread text-state mechanism for GPU widgets. `ReadTextState` returns a
bounded owned snapshot of focused text; `ApplyTextState` handles preview, commit and cancellation against its opaque
revision and stable focus-session identity. `CancelTextInput` cancels only that session's composition; an old OS
context cannot cancel a newly focused editor. Actual focus loss uses the keyboard interface separately. These calls
run outside Render and may request a coalesced frame after an accepted change; other reentrant host calls are
prohibited. A hidden, detached or unprepared view returns no text. The host owns OS focus, TSF/IME,
clipboard and screen coordinates; the plugin receives neither an HWND nor a host-side DxUi object.

The 9,312-byte pointer-free record carries at most 4,096 UTF-16 units and 256 clause boundaries. It rejects larger
documents without truncation. Optional indexes use `UINT32_MAX`; text is length-delimited and cannot contain NUL or
unpaired surrogates. Validate flags, lengths, ranges, clause ordering and finite ordered geometry before applying
state. Caret and viewport rectangles use widget-local physical pixels. The adapter converts DIPs once; the host
adds the displayed tile/raised offset and the window's screen position. Control-owned read-only/masking policy
cannot be overridden through incoming state. Preview never notifies the application model; commit is one edit
relative to the composition base. Newer external text must survive cancellation. `HitTestText` and
`GetTextRangeBounds` return revision-checked physical geometry for TSF candidate placement and point queries.
Missing or stale layout returns no geometry; the host must never substitute an unrelated control's bounds.

The shared text component and AV host integration have passed isolated synthetic validation. Real OS IME acceptance
remains open in the AV plan. The DxUi pin and adapters are owned by [`Core_DxUiIntegration.md`](../Core/Core_DxUiIntegration.md).

## Embedded accessibility transport

`IRedXeAccessibilityWidget` is an optional sibling mechanism for a prepared GPU widget with keyboard input. The
plugin returns its public UI Automation fragment root through `ConnectAccessibility`, using an application-supplied
`IRedXeAccessibilitySite`. C++ DxUi objects and ownership stay inside each module. All calls run on the attaching
COM STA; providers advertise COM threading so marshaled calls return to that apartment.

The 56-byte placement record carries the view (tile 0 or raised 1), nonzero process-unique attachment identity,
physical screen rectangle, and OS keyboard-focus state. Validate exact size, finite positive bounds, view, BOOL and
reserved fields. Host positioning adds the window's screen origin exactly once; the displayed dimensions must match
the prepared view. `UpdateAccessibility` returns S_OK for an existing connection, including a pending redraw or an
unchanged snapshot; S_FALSE requests reconnection. Ordinary input must not reconnect and discard a queued action.

The site supplies parent/sibling navigation, the application fragment root, and focus/action completion requests.
Completion posts coalesced UI-thread work; it must not synchronously re-enter UIA or destroy/change the tree. The
host drains `TakeAccessibilityAction` once and accepts only ordinary success, generic raise or generic dismiss.
Sites and queued actions are generation-bound: hiding, page replacement, modal view replacement and disconnect
retire the old site and all pending requests. A screen reader holding a retired provider must receive
UIA_E_ELEMENTNOTAVAILABLE, never a replacement control at the same path.

Provider code must remain mapped while an external UIA reference survives. AV pins its already-loaded image on the
first successful provider publication and disconnects providers before releasing controls, graphics or runtime.
The pin lasts until process exit; it does not retain the coordinator, control tree, device or helper. Ordinary
module shutdown still runs after owned widget/provider/work references drain. This deliberate mapping retention
prevents calls through unloaded COM vtables after host teardown.

Validation must cover controlling COM identity, independent view lifetimes, negative-origin/DPI geometry,
prepared/dirty updates, coalesced focus and navigation, unavailable siblings, same-path replacements, foreign-thread
rejection, clean-update reuse, confirmed model mutations, and a retained provider after shutdown/loader release.
Component tests are not proof of real screen-reader, IME or touch acceptance; those remain AV release gates.

The mandatory requirements in `Specs/Core/Core_PerformanceAndResources.md` apply to every plugin and host path.

## Public interfaces

| Header | Interface | IID | Purpose |
| --- | --- | --- | --- |
| `Host.h` | `IRedXeHost` | `052F039E-794D-4221-9CF2-28B9208F446F` | Host services: data-provider lookup, frame requests, widget status, settings persist, JSONL log, and named actions (request, execute, validate) |
| `Widget.h` | `IRedXeWidget` | `62DB9FB4-AF7B-47C0-BBF9-B7D5CA535502` | Generic widget identity, visibility, and collect-on-exit |
| `Widget.h` | `IRedXeWidgetProvider` | `231AC0E8-1204-4BFF-BCEA-7CACF11F439D` | Type enumeration and instance creation |
| `Widget.h` | `IRedXeGpuWidget` | `355C7084-286B-409F-9FD3-A7695DEF2A33` | Direct3D 11 rendering mechanism |
| `Widget.h` | `IRedXeScheduledWidget` | `1B6B4F9E-5421-4B4E-BC2D-190EE6CE86CB` | Optional low-cadence frame deadline |
| `Widget.h` | `IRedXeWindowWidget` | `3219FA78-260B-416A-BB76-6331DBF30593` | Host-owned child-container mechanism |
| `Widget.h` | `IRedXeRaisedWidget` | `A7E4C19B-2F58-4D13-9C6A-80B1D4E7F203` | Optional raised-overlay extent and state |
| `Widget.h` | `IRedXeInteractiveWidget` | `E4C2A91B-7D3E-4F18-B6A5-2C9D8E0F1744` | Optional pointer and OLE-drop for GPU tiles |
| `Widget.h` | `IRedXeTextInputWidget` | `87529906-149E-4A99-91B3-2A8C65BCD206` | Optional focused text and geometry transport |
| `Widget.h` | `IRedXeAccessibilityWidget` | `B8FA0B10-EE1D-44E8-8070-A7B80EEA7D4E` | Optional prepared UIA fragment connection |
| `Widget.h` | `IRedXeAccessibilitySite` | `3C430805-12D0-49B0-AAB8-7C07F3909164` | Application-owned navigation, focus and deferred-action site |
| `Widget.h` | `IRedXeNetworkWidget` | `3F8C1A70-9B24-4E61-A7D2-5C0E8B4F1D93` | Optional host-scheduled plugin-owned HTTP work |
| `Service.h` | `IRedXeService` | `9D7C1E52-4B8A-4F6E-A1C3-7E2F5B9D0A64` | Headless service started without a placed widget: start, settings apply, host state, stop |
| `Service.h` | `IRedXeDeviceWorker` | `5A3E8C41-2D97-4B6F-9E15-C7D0F2A8B3E6` | Optional host-owned device lane thread for a service |
| `Data.h` | `IRedXeDataSource` | `C3A81F6E-2D47-4B90-A1E5-6F8C9D0B3E21` | Plugin-side typed, bounded pull snapshots |
| `Data.h` | `IRedXeDataProvider` | `9EAE20F1-36A8-48A8-B451-F60401A898CD` | Host-side dataset discovery and subscription |
| `Data.h` | `IRedXeDataSink` | `F9834987-EBC6-411E-9F28-A49E4DBB49D9` | Synchronous borrowed-snapshot delivery on the host worker |
$1| `Action.h` | `IRedXeActionPack` | `3C7A9E10-5B2D-4F81-9A6E-0D4C8B2F7E51` | Executor for the action namespaces a plugin id publishes (`Plugins_Actions.md`) |

The GPU and native-window interfaces are independent mechanisms. The host selects GPU when an instance exposes both;
otherwise it uses the one supported mechanism. An instance exposing neither is rejected with
`HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)`.

Bundled plugin objects and heap-owned host COM objects MUST derive from the shared `RedXeComObject` mixin in
`FactoryImpl.h` rather than hand-writing `QueryInterface`, `AddRef`, and `Release`. The mixin takes the concrete type
followed by every interface the object exposes, supplies atomic reference counting, and returns the first interface as
the controlling `IUnknown`, which makes the identity rule below structural instead of repeated. It deletes through the
concrete type, so it adds no virtual destructor and no vtable slot. `PluginHost` itself is the exception: it is the
process runtime rather than a heap-owned object, its reference count is advisory, and `Release` never destroys it.

Every public COM declaration MUST use the MSVC
`interface __declspec(uuid("...")) __declspec(novtable) Name : IUnknown` form. Declaring a COM interface with the C++
`struct` keyword is forbidden. Every interface in the table is a direct child of `IUnknown`; rendering, scheduling,
interaction, network, host, and data mechanisms MUST NOT inherit from `IRedXeWidget` or from one another.

RedXe is pre-production: `IRedXeHost` and `IRedXeWidget` vtables MAY grow when a host service or generic widget duty
is added. Rebuild every source-coordinated consumer together. Do not add a sibling `QueryInterface` only to avoid
growing those vtables. Rendering and work mechanisms stay sibling IIDs so adding one never changes an existing
widget vtable. There is no `IRedXeSettingsEditor`. Widget settings persist is a host service on `IRedXeHost`.

`Service.h` owns the headless service declarations. `IRedXeService` and `IRedXeDeviceWorker` are direct `IUnknown`
children like every other mechanism; a service object exposes both as siblings with one controlling identity. Neither
`Widget.h` nor `Data.h` declares service behavior.

`IRedXeWidget` carries `SetVisible` and `CollectPersistentSettings`. A widget implementation exposes that interface
and each supported rendering or work mechanism as sibling COM interfaces on one object. `QueryInterface(IID_IUnknown)`
from every interface MUST return the same controlling `IUnknown` pointer. This preserves COM identity without creating
a C++ inheritance relationship or an accidental vtable dependency between the generic widget and a rendering
mechanism.

Widgets are initially invisible. `SetVisible` is synchronous, idempotent, and runs on the RedXe UI thread for every
widget, regardless of rendering mechanism. `SetVisible(FALSE)` quiesces widget-owned animation, subscriptions,
timers, and other visibility-dependent work before returning. `SetVisible(TRUE)` may resume only work required by
visible content. The host enables visibility only after the selected mechanism is ready and disables it before that
mechanism is detached. Attach/detach and device lifetime remain on their mechanism-specific interfaces.

Every widget MUST implement `CollectPersistentSettings`. The host calls it on the UI thread after `SetVisible(FALSE)`
and before `Detach` during dashboard shutdown and page teardown. `S_FALSE` means nothing to save and MUST set
`writtenBytes` to 0. `S_OK` writes a complete instance settings object or a mergeable subset into the host-owned
buffer; `writtenBytes` is the JSON length excluding a terminator. A null `writtenBytes` returns `E_POINTER`. The
widget MUST NOT write the settings file and MUST NOT call `PersistWidgetSettings` from inside collect; the host
writes. Widgets with no dirty state, including the current bundled plugins, return `S_FALSE`.

Every public record starts with `sizeBytes`. It is retained so a future production compatibility policy can define
safe record evolution. Under the current pre-production contract it is an exact stale-binary and malformed-input
guard: consumers require `sizeBytes == sizeof(current-record)` and reject both smaller and larger values.

`sizeBytes` stays as it is. It was reviewed against adopting prefix-compatible records or a single module-level ABI
version export, and neither is adopted while the repository rebuilds host and plugins together; revisiting it belongs
to the production ABI freeze. Because a size check alone cannot detect a field reorder that preserves size, every
public record in `Widget.h`, `Data.h`, `Host.h`, and `Factory.h` MUST additionally be pinned by a compile-time
`sizeof` assertion, and every record carrying a pointer MUST be pinned by `offsetof` assertions. A layout change that
is not intended therefore fails the build rather than passing the runtime guard.

The ABI headers MUST carry the consumer-facing rules on the declarations themselves: thread affinity, reentrancy
limits, borrow lifetime, and the pipeline-state guarantee. This contract stays authoritative; the headers state enough
that a plugin author reading only `Common/PlugInterfaces/` can implement a correct widget, source, and sink.

## Identifiers and strings

- ABI plugin IDs, widget type IDs, and runtime instance IDs are stable UTF-8 ASCII strings. Version 5 user documents
  expose only the plugin ID and flattened plugin keys; `Specs/Core/Core_Settings.md` owns that syntax. The host maps
  the settings-visible ID to the module's internal widget type and generates
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
  `RedXeGetPluginSettingsContract`; every plugin id that advertises `RedXePluginCapabilityActions` MUST also export
  `RedXeGetActionContract` and answer for that id ([`Plugins_Actions.md`](Plugins_Actions.md)). `RedXePluginShutdown`
  is optional because most modules need no global shutdown.
- A plugin MAY additionally export a bounded test-support surface. It is present in Release because the required
  Release validation drives it, so it is part of the shipped export set rather than a debug-only convenience. Each
  such export MUST be declared in that plugin's `*TestContract.h` behind a single `REDXE_*_TEST_API` macro, never as a
  raw `__declspec(dllexport)` in the implementation, so the shipped export set is readable from the contract header.
  The host never calls these exports. The current surface is: `RedXeMatrixRainGetTestDiagnostics`, `RedXeShadersGetTestDiagnostics`,
  `RedXeProcessViewerGetTestDiagnostics`, `RedXeStudioClockGetTestDiagnostics`, `RedXeStudioClockSetTestTime`,
  `RedXeDeskClockGetTestDiagnostics`, `RedXeDeskClockSetTestTime`, `RedXeWeatherGetTestDiagnostics`, and
  `RedXeWeatherProbeHttpGetOnSmallStack`, `RedXeWeatherBuildTestLocationSearchUrl`,
  `RedXeAVControlUseSyntheticBackend` and `RedXeAVControlTestSnapshot`, and the Logicon surface
  `RedXeLogiconGetTestDiagnostics`, `RedXeLogiconUseSyntheticDevice`, `RedXeLogiconInjectControl`,
  `RedXeLogiconInjectSyntheticReport`, `RedXeLogiconSetFaceOverride`, and `RedXeLogiconSetBrightness`, and the Zoom
  surface `RedXeZoomGetTestDiagnostics`, `RedXeZoomUseSyntheticSession`, `RedXeZoomSeedCredential`,
  `RedXeZoomSyntheticSetHost`, `RedXeZoomSyntheticDropConnection`, `RedXeZoomSyntheticCounters`, and
  `RedXeZoomStoredCredential`. AV synthetic
  mode is accepted only before providers exist and is never exposed as a user setting or environment toggle; the
  Logicon synthetic keypad and the Zoom synthetic session are selected only through their exports (or, for Logicon,
  the Debug monitor tile), never by a setting.
- Every factory call names one non-empty plugin ID. Null and empty IDs are invalid, including in single-plugin DLLs.
- Factory, enumeration, widget creation, device notification, GPU rendering, native-window lifecycle, host-service,
  data-source, provider, and data-sink calls are synchronous and non-reentrant. Widget visibility, collect-on-exit,
  persist, native-window, pointer, drop, and every `IRedXeService` call run on the RedXe UI thread; data-sink
  callbacks run only on the host acquisition worker; `IRedXeNetworkWidget::RunNetworkWork` runs only on the host
  network worker; `IRedXeDeviceWorker::RunDeviceWork` runs only on that service's host-owned device lane.
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
  The current 24-byte record contains `debugLevel`, a borrowed `configurationJsonUtf8` pointer, `configurationBytes`,
  and `backgroundColor` at offset 20. Configuration length excludes a terminator and MUST NOT exceed 8192 bytes.
- `backgroundColor` is the dashboard background the host resolved for the widget instances this provider builds, as
  opaque ARGB (`0xFFRRGGBB`, the `RedXeAppearance` convention): the document `backgroundColor`, or that instance's
  own override (`Specs/Core/Core_Settings.md`). `RedXeBackgroundRgb` in `Factory.h` yields the `0xRRGGBB` value a
  plugin's own color pipeline expects. A widget that paints an opaque background MUST paint this color and MUST NOT
  compile in its own; a widget that paints nothing behind its content needs nothing, because the host canvas already
  carries it. The host builds one provider per distinct (plugin, configuration, background) per staging pass. No
  plugin settings contract, defaults object, or factory configuration envelope contains `backgroundColor`.
- A null configuration pointer with zero bytes selects plugin defaults. Every other null/length mismatch returns
  `E_INVALIDARG`. Plugins that consume configuration JSON MUST parse and copy it before `RedXeCreate` returns;
  no plugin retains the borrowed pointer.
- Host-created providers receive only the compact internal
  `{"plugin":{},"instance":<effective-settings>}` envelope. Plugins MUST parse and copy it before returning. This
  envelope is an ABI normalization detail, not the user document model; direct effective-settings objects are invalid.
- Successful creation returns one caller-owned COM reference.
- Enumeration returns 1–256 contiguous module-owned metadata records.
- Malformed metadata, duplicate IDs, or missing required capability fail module discovery safely.

### Host services

`IRedXeHost` is borrowed for the lifetime of the host runtime. A plugin MUST NOT retain it past the release of the
object it was supplied to.

- `GetDataProvider` and `ReportWidgetStatus` are synchronous, non-reentrant, and run on the caller's thread.
- `RequestFrame` coalesces one host frame for a widget whose own state changed. It is safe from any thread, including
  a data-sink callback on the acquisition worker and `RunNetworkWork` on the network worker, performs no allocation,
  and never blocks. It does not force a
  frame: hidden, minimized, suspended, display-off, and occluded hosts still block. It is the per-instance mechanism;
  `RedXeWidgetFlagContinuousAnimation` is a property of the widget *type* and cannot describe an instance whose motion
  starts and stops. A widget MUST use `RequestFrame` rather than declaring continuous animation for intermittent
  motion, and rather than returning a short `IRedXeScheduledWidget` delay purely to be polled.
- `RequestFrame` is the only host service a sink may call from inside `OnDataSnapshot` besides `Log`. `RunNetworkWork`
  MAY call `RequestFrame` and `Log` and MUST NOT call `PersistWidgetSettings`.
- The **network lane** is host-owned scheduling for plugin-owned HTTP. There is no host HTTP ABI: the HTTP client,
  its cache, and its user agent live in the plugin module (`Weather.dll` links curl with Schannel; `RedXe.exe` MUST
  NOT link curl), so those resources map only when such a widget exists. `PluginHost` owns exactly one serial network
  worker, distinct from the acquisition worker and the control lane. It starts lazily when a visible network widget
  first needs a run, keeps at most one `RunNetworkWork` in flight and eight registered widgets, blocks on events
  while idle, hidden, or shutting down, and never polls. The host calls `RunNetworkWork` only for a visible widget
  and only while network access is enabled; `S_OK` schedules the returned delay, `S_FALSE` parks the widget until
  it is activated again, and a failure retries after one minute. Deactivating a widget (hide, detach) signals the
  borrowed cancel event and waits for the in-flight call to return before `SetVisible(FALSE)` reaches the widget;
  runtime shutdown cancels every widget, waits for idle, and joins the worker before optional module shutdown.
  `--self-test` and `HostPluginTests` disable network access before any widget is created, so the host never calls
  `RunNetworkWork` and never starts the worker there; interactive RedXe leaves it enabled. Live endpoints are
  manual-only and MUST NOT be a CI pass condition.
- `QueueControlWork` is a UI-thread service for bounded local device work. Its lazy process-wide MTA lane retains
  at most 16 distinct `IRedXeControlWork` objects and coalesces repeated submission into one rerun (`S_FALSE`);
  saturation returns `ERROR_BUSY` without accepting work. `Run` receives a borrowed cancellation event and the
  remaining part of a three-second enqueue-to-result budget. It performs no D3D, UI mutation, persistence or UI wait.
  Potentially unbounded driver calls belong in an owned terminable helper, never directly in this worker.
  Completion is posted and drained on the UI thread outside input/render dispatch; it may publish state, request
  a frame or queue another unit. Shutdown signals cancellation, drains the worker, suppresses completions and
  releases retained references on the UI thread before module shutdown. Self-tests reject device work explicitly.
  Committed input may enqueue mutations. Preparation and visibility changes may enqueue observation/cleanup only.
- `RequestAction`, `ExecuteAction`, and `ValidateAction` are the named-action services
  ([`Plugins_Actions.md`](Plugins_Actions.md)): `RequestAction` is safe from any thread including a service's device
  lane, allocation-free, never blocking (16-slot ring, identical pending request coalesced to `S_FALSE`, full ring
  `ERROR_BUSY`, one coalesced `WM_APP + 5` post, drained on the UI thread outside input and render dispatch);
  `ExecuteAction` is UI-thread only, synchronous, non-reentrant, allowed from `OnPointer` (committed activation),
  `OnKey` / `OnCharacter`, and `OnDrop`, and never queued or dropped; `ValidateAction` resolves a name and checks its
  target without executing anything and may map a registered publisher's module on first use. A `page.*` or
  `widget.*` action that arrives during a page swipe, a raise settle, or while the settings error dialog is up is
  refused (`ERROR_BUSY`) rather than queued; every other namespace still executes. Every drained action is followed by
  one host-state publication to started services.
- `ReportWidgetStatus` records the condition of one widget instance, named by the instance ID the host passed to
  `CreateWidget`. Status is one of `RedXeWidgetStatusOk`, `Initializing`, `Degraded`, or `Unavailable`, with an
  optional borrowed UTF-16 reason the host copies into bounded storage and truncates. Repeat reports are idempotent;
  only a change coalesces a frame. A malformed record, an unknown status value, or an invalid instance ID returns
  `E_INVALIDARG`. Reporting `Unavailable` makes the host own that tile and draw its placeholder; `Degraded` and
  `Initializing` are recorded but leave the widget drawing its own content.
- `PersistWidgetSettings` asks the host to persist this instance's plugin settings object, not the factory envelope.
  The widget MAY send the complete object or a subset of members. The host always merges supplied members into the
  stored instance object, validates the complete result against the 4096-byte compact cap and the plugin schema, and
  MAY write the user document. The call MUST NOT destroy or detach the calling widget. Plugins MUST NOT write the
  settings file. The call is UI-thread only, synchronous, and non-reentrant. It is forbidden from device, size,
  visibility, raise, `Render`, `CollectPersistentSettings`, `OnDataSnapshot`, and `RunNetworkWork`. It is allowed from
  `OnPointer` (committed click) and `OnDrop`. A null instance ID, a null JSON pointer, or zero bytes returns
  `E_INVALIDARG`. `--self-test` and HostPluginTests keep a successful merge in memory and MUST NOT write
  `%LocalAppData%`.
- An interactive settings save is transactional: validation or file-replacement failure MUST preserve both the
  typed instance settings and the retained source document. A committed file replacement remains success even if
  querying its deduplication stamp fails afterward; the next watcher notification may reload it.
- The optional sibling `IRedXeSettingsQueue` shares the host's controlling `IUnknown`. Workers and visibility imports MAY queue up to 4096
  JSON bytes for a 127-byte instance ID. The host copies at most eight pending instances, coalesces the same ID, and
  returns `ERROR_BUSY` when full. `S_OK` means accepted, not committed. A coalesced UI invalidation drains the queue
  through the existing validated persist handler outside callbacks/locks. Teardown discards that instance's records.
  Failed commits log once; widgets retain collect fallback. Empty queues own no heap records or timer. GPU, paint and
  scheduling callbacks MUST NOT use this service. An older queued patch precedes a newer synchronous save for that instance; a detached
  delivery batch must not consume newer submissions. Failed older saves log once and do not prevent a newer valid
  save. Host tests cover bounds, coalescing, teardown, UI delivery, save ordering and identity.
- `Log` appends one diagnostic JSONL line. It is safe from any thread, including the acquisition and network workers,
  copies bounded fields into a 32-slot 1024-byte ring, and never blocks on disk. The host writer is event-blocked and
  writes UTC-dated files (`RedXe-debug-YYYY-MM-DD.jsonl` / `RedXe-YYYY-MM-DD.jsonl`), opening a new file when the UTC
  day changes and deleting dated files at least `logRetentionDays` old (default 15, range 1–365). Interactive RedXe
  stores those files under the settings sibling `Logs` directory (`%LocalAppData%\RedXe\Logs` by default). `--self-test`
  MUST NOT open that directory. Release drops `RedXeLogLevelDebug`. A null record, a mismatched `sizeBytes`, a missing
  event id or message, or an unknown level returns `E_POINTER` / `E_INVALIDARG`. Plugins MUST NOT call `Log` from
  `Render` or GDI paint and MUST NOT emit per-frame success. Factory create, module map, placeholder construction,
  native attach, GPU device-create, GPU render failure (once per instance and HRESULT), and weather forecast outcomes
  are the required coverage.
  Escaping and truncation MUST preserve complete JSON syntax, the optional HRESULT, one trailing newline, and valid
  UTF-8. Each identity has a bounded escaped-output budget, and message truncation reserves room for the record
  suffix. A partial final UTF-8 sequence is omitted; malformed input bytes are replaced with ASCII `?`.
  The log writer's empty-queue test and idle signal MUST be synchronized with producer enqueue/reset, so a completed
  flush cannot be inferred from a stale idle event. Host tests exercise repeated bounded enqueue/flush batches and
  verify every flushed record is present on disk.

### Static settings contract

`RedXeGetPluginSettingsContract` discovers plugin-owned validation metadata without creating a provider, widget,
device resource, HWND, timer, or worker. It returns one borrowed immutable `RedXePluginSettingsContract` for the
requested plugin ID. The record and its UTF-8 strings remain valid while the module is mapped.

- A null output returns `E_POINTER`; a present output is cleared before validation.
- An unknown plugin ID returns `HRESULT_FROM_WIN32(ERROR_NOT_FOUND)`.
- `sizeBytes` must equal `sizeof(RedXePluginSettingsContract)`.
- Schema and defaults are JSON objects bounded at 4096 bytes each. The schema is written in Draft 2020-12 syntax and
  may contain bounded `x-ui-*` annotations. Defaults MUST validate against the schema.
- The host validates a bounded subset of that syntax, not the whole draft. The supported subset is exactly:
  `"object"` with `properties`, `additionalProperties`, and `required`; `"array"` with `items`, `minItems`, and
  `maxItems` where `items` is one closed `"object"` (nested arrays are forbidden); `"integer"` and `"number"` with
  `minimum` and `maximum`; `"string"` with `enum`, Unicode-scalar `minLength`/`maxLength`, and one of the three fixed
  `pattern`s `^#[0-9A-Fa-f]{6}$`, `^[A-Za-z0-9_-]+$`, or the action-name pattern
  `^[a-z][a-zA-Z0-9]*(\\.[a-z][a-zA-Z0-9]*){1,3}$` (validated with `RedXeIsActionNameSyntax`); and `"boolean"`.
  `$ref`, composition keywords, and any other `pattern` are rejected rather than silently accepted, so a plugin cannot
  publish a constraint the host does not enforce. A plugin schema MUST stay inside this subset.
- The host parses each referenced plugin's schema once per staging pass, not once per widget appearance.
- One settings-visible plugin ID selects one widget kind. A DLL may publish several IDs, each with its own contract.
- The host copies or parses borrowed strings synchronously and never frees them.
- Rotating Triangle and GDI Orbit publish closed empty-object schemas and `{}` defaults. Matrix Rain publishes its
  complete closed schema, ranges, color syntax, and runtime defaults. Process Viewer publishes a closed object with
  required integer `topN` from 1 through 32 and default 10. Network Meter and GPU Processes publish the same closed
  `topN` object with range 1 through 16 and default 8. System Pulse, CPU Meter, Memory Meter, Storage Meter, GPU Meter,
  Power Meter, and Thermal Meter publish closed empty-object schemas and `{}` defaults. Studio Clock publishes its
  complete closed boolean, color, and date-format schema and defaults. Desk Clock publishes its complete closed duration
  and color schema and defaults. 5H4D3R5 publishes its closed mode/shader enum, interval, shuffle, and render-scale
  schema and defaults. Weather publishes its closed location and unit schema and defaults. Launcher publishes
  a closed `shortcuts` array of 0 through 32 objects with optional `action` (the action-name pattern), `target`, and
  `icon`, plus optional
  `iconSize` (`small`, `medium`, `large`, `huge`, or `automatic`, default `automatic`), default
  `{"shortcuts":[],"iconSize":"automatic"}`.

The metadata capability surface advertises factory-created plugin services through
`RedXePluginCapabilityWidgetProvider`, `RedXePluginCapabilityDataSource`, `RedXePluginCapabilityService`, and
`RedXePluginCapabilityActions` (the id publishes action namespaces and answers `RedXeGetActionContract`).
Rendering mechanisms are discovered on widget instances by IID, not by a capability or rendering-path enum.

Logicon publishes its closed service schema (`brightness` 1–100, `restoreLogoOnExit`, `pageButtons`
`keyPages`/`dashboardPages`, a flat `keys` array of at most 36 closed objects with `page` 0–3, required `slot`
0–8, `action` (the action-name pattern), `target`, `label`, `icon`, `color`, and `face`, and a closed `dialpad`
object with `turns` and `buttons` arrays) and defaults
`{"brightness":70,"restoreLogoOnExit":true,"pageButtons":"keyPages","keys":[],"dialpad":{"turns":[],"buttons":[]}}`;
the Logicon Monitor publishes a closed empty object. `Specs/Plugins/Plugins_Logicon.md` owns the member semantics.
Zoom publishes its closed service schema (`clientId`, `redirectPort`, `domain`, `displayName`, `autoConnect`) and
defaults; `Specs/Plugins/Plugins_Zoom.md` owns the member semantics.

## Service contract

A service is a plugin object the host runs without a placed widget. `Plugins/Logicon` and `Plugins/Actions/Zoom` are
the shipped ones; both also publish an action namespace (`Plugins_Actions.md`).

- A DLL advertises `RedXePluginCapabilityService` and creates the object through
  `RedXeCreate(IID_IRedXeService, …)` with the same compact `{"plugin":{},"instance":<effective-settings>}` envelope
  a provider receives. The effective object comes from the `services` settings root (`Core_Settings.md`); a plugin
  that rejects it fails creation, and the host logs `create-failed` without aborting startup.
- `RedXe/BundledPlugins.h` catalogs services in `kRedXeBundledServices`. A service plugin ID is never also a widget
  plugin ID; the compile-time catalog check enforces that and rejects duplicates. `PluginHost` owns one slot per
  catalogued service, creates at most one object per plugin ID per process, and releases it at `StopServices`.
- Lifetime runs on the UI thread. `Start` follows the first successful settings apply and the first live dashboard
  page (`Application::Run` and `RunSelfTest`), before the window is shown. `ApplySettings` runs on a live reload
  whose effective object for that service changed, including a reload that leaves the active dashboard untouched; a
  reload that removes the service stops and releases it, and one that adds it creates and starts it. `OnHostState`
  runs after `Start`, after every promoted page, raise start, dismiss completion, visibility change, settings apply,
  and drained host action, carrying page index/count/id/name, widget count, the raised ordinal, and the
  `Visible`/`Raised`/`Busy` flags. `Stop` runs from `CloseMainWindow` after every dashboard host is shut down and
  before `PluginHost::ShutdownProcessRuntime`; `PluginHost::Shutdown` repeats it as an idempotent safety net before
  providers, workers, and modules go. `Start` and `Stop` are idempotent; a failed `Start` logs
  `service-start-failed` once and keeps the object so a later `ApplySettings` can retry. A service MUST NOT call
  back into the host from these calls except `RequestAction`, `RequestFrame`, `Log`, and — from `Start`,
  `ApplySettings`, and `Stop` only, on the UI thread — `ValidateAction` and `GetDataProvider` with the provider's `GetDataSets`,
  `Subscribe`, and `IRedXeDataSubscription::SetActive`, under the same sink rules as a widget. A service MUST
  release every subscription in `Stop` (which drains its sink callbacks) so the host holds no reference to it
  afterwards, and its device lane MUST NOT touch subscriptions.
- `RedXeServiceFlagDeviceAccessDisabled` is set by `--self-test` and by HostPluginTests before `StartServices`. A
  service that receives it MUST NOT open a hardware device, register hotplug notifications, open a network or IPC
  connection, or inject input; it still parses settings, keeps model state, and accepts actions and its test contract.
- The **device lane** is the host-owned thread `Core_PerformanceAndResources.md` reserves for device I/O. When a
  started service exposes `IRedXeDeviceWorker`, `PluginHost` creates one `std::jthread` for it (at most
  `kRedXeMaximumDeviceWorkers`, 4; more return `ERROR_TOO_MANY_NAMES` and log `device-lane-failed`), initializes an
  MTA apartment, and calls `RunDeviceWork(stopEvent, wakeEvent)` exactly once. Inside that call the plugin owns
  discovery, handles, overlapped I/O, and hotplug registration and blocks only in a wait on the two host events and
  its own I/O events; it MUST NOT poll, sleep-loop, touch Direct3D, wait on the UI thread, create a thread, or
  re-enter the host except through `RequestAction`, `RequestFrame`, and `Log`. A lane that needs Raw Input (a
  device Windows opens exclusively, such as a mouse collection) MAY own one hidden, never-shown top-level window on
  the lane thread, registered with `RIDEV_INPUTSINK`, and then waits with `MsgWaitForMultipleObjectsEx` and drains
  its queue on the same thread; it MUST unregister the sink and destroy the window before returning, and it MUST
  NOT register a usage another plugin's lane registers (raw-input registration is per process; today only Logicon
  registers `usage page 1 / usage 2`). The service MAY signal `wakeEvent`
  from any thread while the call runs and MUST NOT touch either handle after it returns. `StopDeviceLane` signals
  `stopEvent`, waits `kRedXeDeviceWorkerDrainMilliseconds` (3000) for the thread, joins it, and closes the events; an
  overrun logs `device-lane-drain-timeout` once, detaches the thread, and leaks the two event handles so a late wait
  inside the plugin still sees valid objects. Shutdown continues either way. `RedXeDataSetFlagDeviceLane` for data
  sources remains unimplemented.
- **Developer-only widgets** (`kRedXeDebugOnlyBundledWidgetIds`, today `builtin.logicon-monitor`) stay catalogued and
  schema-accepted in every build so both shipped templates parse everywhere. Only the Debug template places them, and
  only Debug builds of their DLL construct them: a Release build publishes the metadata and contract, lists the type,
  and returns `ERROR_NOT_SUPPORTED` from `CreateWidget`, which the host turns into its placeholder tile. Their
  `docs/plugins/` coverage lives on the owning service's page.

## Data discovery and delivery

`IRedXeDataSource` is the plugin-side pull interface created by `RedXeCreate`. It enumerates immutable typed-table
descriptors and performs synchronous, non-reentrant batch snapshot collection. `CollectSnapshots` takes one exact-size
`RedXeDataCollectRequest` (`sizeof` 24) of unique borrowed module-owned dataset IDs, at most
`RedXeDataCollectMaximumDataSets` (32). A successful `RedXeDataCollectResult` (`sizeof` 32) returns one non-null
snapshot pointer per request ID, plus one common sequence and `timestampFileTime100ns` for the whole batch. Unknown
IDs, duplicate IDs, a zero or oversized count, a null ID, or a mismatched `sizeBytes` fail the entire call and clear
the result. A failed call MUST NOT keep rate-history side effects from datasets collected earlier in that batch:
network, storage, and GPU previous samples are restored, and CPU, memory, process, and thread rate histories are
discarded so the next successful sample of those datasets is `Initializing` rather than a spike over a discarded
interval. Sequence numbers advance only on a fully successful batch. Descriptors remain valid while the module is
mapped. Returned snapshots and all referenced rows, values,
and UTF-16 strings remain valid until the next `CollectSnapshots` call on that source or source release; consumers copy
retained data before then. Widgets never receive a data source.

`IRedXeHost::GetDataProvider` accepts one provider plugin ID and returns the host-side `IRedXeDataProvider` for that
source. A null output returns `E_POINTER`; a present output is cleared first. Invalid IDs return `E_INVALIDARG`, an
unknown ID returns `HRESULT_FROM_WIN32(ERROR_NOT_FOUND)`, and a known plugin without a data source returns
`E_NOINTERFACE`. Repeated lookup returns the same controlling provider identity. A widget stores the returned
provider, discovers datasets through `GetDataSets`, and calls `Subscribe` with only a dataset ID, requested interval
from 1 through 60,000 milliseconds, and sink. Each call returns a distinct inactive subscription or fails without
retaining the sink.

`PluginHost` is the process plugin runtime. Exactly one instance serves the whole application: it owns every mapped
module and every local data-provider runtime for every `PluginManager`, including the one staged for an adjacent page
during a swipe. A `PluginManager` borrows it and never owns one. The first provider
lookup lazily maps its catalog module, creates one `IRedXeDataSource`, validates and caches at most 256 datasets, and
returns a host provider facade. Insert of a new provider re-checks identity under the exclusive subscription lock so
two overlapping creates cannot both insert. `SetActive` MUST NOT wake the worker when the requested state already
matches; deactivation still honors the drain guarantee. All local sources share one acquisition worker and at most
32 subscriptions in total.
Active subscriptions for the same provider and dataset share one collection at the shortest requested interval,
clamped to the source recommendation. When multiple datasets on one source are due in the same worker pass, the host
gathers those unique IDs, orders them deterministically with `source.status` last, and issues one `CollectSnapshots`
call. It delivers only when the result count, record sizes, and snapshot IDs match the request. Different providers
remain distinct and may be used concurrently. Multiple
viewers may hold the same provider and subscribe independently. With no active subscription the worker blocks
indefinitely on its change and stop events; it owns no polling or periodic wake-up. `RedXeDataSetFlagDeviceLane` marks
datasets that would use a shared host device-I/O lane; no such lane is created until timeout, `CancelIoEx`, and
teardown drain are measured. Sources MUST NOT create their own acquisition threads.

The host provider does not retain or duplicate a source snapshot. `PluginHost::Deliver` reserves and copies the active
sink pointers under the subscription lock, releases that lock, then invokes each `OnDataSnapshot`. A sink copies only bounded values it
needs, performs no blocking work or provider/host re-entry, and MUST NOT activate, deactivate, or release a
subscription from inside `OnDataSnapshot`: draining its own reserved callback would deadlock. A sink failure is
isolated and does not stop later sinks or acquisition cycles. After a successful delivery to any active sink,
`PluginHost` coalesces one UI-thread frame invalidation (`WM_APP + 3`) so GPU data widgets can start a sample-driven
ease without a child HWND. `SetActive(FALSE)` and subscription release drain an
in-flight callback and every reserved callback that has not yet entered before returning when called outside the
callback. A slot MUST NOT be reused until its copied sink references have been released. Draining uses an event-blocked
wait, never polling. Shutdown signals and joins the worker before
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
- `OnTargetSizeChanged` reports the largest viewport the host will draw this widget at in the current composition.
  Device creation, this callback and optional `IRedXePreparedGpuWidget::Prepare` MAY allocate bounded resources or
  rasterize; `Render` MUST NOT do either. The host calls size notification on the UI
  thread, synchronously and non-reentrantly, after `OnDeviceCreated` and before the first `Render`, and again whenever
  that size changes: resize, DPI change, layout change, and raise or dismiss.
  - It MUST NOT be called for a position-only change such as a page-swipe offset, MUST NOT be called per frame, and
    MUST NOT be called for each interpolated rectangle during a raise or dismiss settle. The host reports the final
    overlay size when raise starts and the tile size when dismiss completes. The host compares the integer viewport
    size and DPI against the last values it reported for that widget and calls only on a difference. A DPI-only change
    MUST notify even when physical dimensions are unchanged. Caches belong to widget identity: a replacement staged
    page gets initial notifications even at the same dimensions, while promotion retains its existing cache.
  - A raised widget is drawn twice in one frame, at its tile and again at the overlay slice, so the reported size is
    the larger of the two. Raster resources may be minified, but layout and input coordinates MUST match the actual
    per-frame viewport, including intermediate overlay sizes during animation.
  - A widget with no resolution-dependent resources returns `S_OK` and does nothing.
  - A failure is isolated: the host keeps the widget's previous resources and continues rendering it.
- `OnDeviceLost` is idempotent and releases all plugin-owned device resources before the host releases its device.
  The host MUST deactivate and drain current and staged widget work before device teardown, stage new pages hidden
  until setup completes, and restore prior visibility only after successful resource creation. GPU state accessed by
  snapshot or network workers MUST synchronize its lifetime check and resource access with device teardown. The
  Process Viewer and Weather resource hubs use an atomic instance-ownership flag and the shared GPU lock for this.
  During recovery, both current and staged pages receive device creation before the new render target exists;
  viewport calculation and initial size notifications MUST wait until that target has physical dimensions.
- `IRedXePreparedGpuWidget` is an optional sibling of the GPU interface. `Renderer::PrepareWidgets` runs before frame
  construction and reports final tile and raised physical extents, DPI and cached Windows appearance. The current
  preparation record is 56 bytes; its 32-byte appearance member carries dark/high-contrast flags and seven opaque
  ARGB system colors. Application reads those values only at initialization and on Windows theme, color or settings
  notifications. Neither preparation nor rendering queries the registry/system theme. The host unbinds its render target before
  preparation. Widgets coalesce dirty changes with the existing `RequestFrame`; a clean Prepare returns `S_FALSE`
  without allocation, layout or rasterization. This bounded check runs only for an already-requested frame and does
  not introduce a timer or idle polling. A widget MUST preserve requests raised during preparation for a later frame.
  A failed preparation suppresses stale input/composition and MUST NOT retry until a new request or geometry change.
  Device-loss results enter the normal host recovery path; other failures remain local to that widget.
- `RedXeGpuFrameContext::viewId` identifies the tile (0) or separately prepared raised view (1). Their final layouts
  remain independent during raise animation; position-only changes do not rerasterize. All source-coordinated
  plugins MUST rebuild against the current 56-byte frame record; old record sizes are not accepted.
- `Render` receives generic widget dimensions/timing, the borrowed immediate context, and the widget viewport. During
  a page swipe that viewport is the full design-canvas placement translated by the page offset: `TopLeftX`/`TopLeftY`
  MAY be negative and the rectangle MAY extend past the render target. `widget` width and height stay that full size.
  Direct3D clips to the target. A GPU widget MUST still draw and MUST NOT treat a finite negative origin as invalid.
  The host MUST invoke `Render` for every positive-size viewport on the current and staged pages and MUST NOT shrink
  the viewport to the visible intersection.
- Before the first widget of a frame the host clears the whole target with the document `backgroundColor`. Before a
  widget whose resolved background differs from the document color, the host fills that widget's viewport with its
  color through `ID3D11DeviceContext1::ClearView`; tiles that share the document color get no extra fill.
- Before every callback the host binds exactly two things: its render target through `OMSetRenderTargets` and the
  widget's viewport through `RSSetViewports`. Nothing else is reset between widgets. Blend, depth-stencil, and
  rasterizer state, the scissor rectangle and `ScissorEnable`, input layout, primitive topology, shaders, shader
  resource views, samplers, and constant buffers all carry over from whichever widget drew last.
- A widget MUST therefore bind every state it depends on, including scissor state, and MUST NOT rely on any state it
  did not set itself. Leaving unusual state behind is legal but hostile, because it surfaces as a defect in a sibling
  widget and only for a particular tile ordering; prefer restoring anything exotic.
- The host does not reset state between widgets: that cost is not justified for the shipped widget set, and the
  requirement above is what keeps widgets independent. Debug builds of the host instead verify a bounded subset of the
  contract after each successful widget, currently that no widget leaves `ScissorEnable` set.
- A widget must not retain the immediate context or frame records, present, resize the swap chain, or access the
  top-level HWND. It never receives the swap chain or back buffer.
- Isolation, deferred command lists, and offscreen composition are outside the current contract.

Device resources belong to the plugin implementation. The host owns the device, immediate context, swap chain,
render target, viewport placement, device-loss sequence, and presentation.

## Native-window widget contract

`IRedXeWindowWidget` is the general child-HWND path. The host owns one `WS_CHILD | WS_EX_LAYERED` container for each
selected native-window widget and passes that borrowed container to `Attach`. A layered child is legal only in a
process whose manifest declares a Windows 8 or later `supportedOS`; the host and every test binary that hosts
containers declare it, and `DashboardHost::Initialize` fails with `E_INVALIDARG` when the system rejects the
container rather than attaching the widget to an unlayered one. The plugin may paint with GDI or create
child controls, media hosts, or a WebView controller inside it. The mechanism is opt-in with a documented cost: a
child HWND over the swap chain forces DWM to compose the whole window instead of flipping it independently, so no
shipped page places a native-window widget and the bundled example (`builtin.gdi-orbit`) is catalogued, documented,
and test-covered but not placed by the shipped templates.

- `Attach`, `Resize`, and `Detach` run synchronously on the RedXe UI thread.
- The plugin may retain the container HWND only from successful `Attach` until `Detach` begins. It MUST NOT subclass,
  destroy, reparent, or change the host container, and it never receives the top-level HWND.
- The plugin owns every HWND, timer, controller, COM object, GDI object, and other resource it creates. Before
  `Detach` returns it MUST stop callbacks and timers, destroy its child HWNDs, and release those resources.
- `Attach` creates plugin content for the supplied physical pixel size and DPI. A failed call leaves the widget
  detached. Repeated `Detach` is safe.
- `Resize` receives the new physical container size and destination-monitor DPI after the host has repositioned the
  container. It performs no continuous work when size and DPI are unchanged.
- Native children are composed above the parent's D3D surface through the layered container's own DWM surface. They
  can be ordered relative to other child windows, but cannot be interleaved with GPU widgets or with host chrome; the
  host dims the container with window alpha while another widget is raised, and a raised native widget covers the
  host-drawn close control. Effects requiring D3D-layer composition use the GPU mechanism.
- Ordinary child-window pointer, keyboard, focus, accessibility, and IME behavior remains inside the widget bounds.
  Global commands and page policy remain host-owned. Interactive WebView security and navigation policy require a
  separate normative contract before a bundled web widget ships.

## Raised overlay contract

`IRedXeRaisedWidget` is an optional sibling of `IUnknown`. It MUST NOT inherit from `IRedXeWidget` or from a rendering
mechanism. Every bundled widget MUST expose it on the same controlling `IUnknown` as its generic widget.

The host MUST NOT guess overlay size. Before raising a tile it queries `GetRaisedExtent`. A null output returns
`E_POINTER`. Success writes exactly one of `RedXeRaisedExtentQuarter` (1/4), `RedXeRaisedExtentThird` (1/3),
`RedXeRaisedExtentHalf` (1/2), or `RedXeRaisedExtentFull` (1/1) of the **client width**. The host lays a full-height
slice of that width over the original column and MUST NOT shrink a tile on either axis when the client still has room.
Dimmed siblings stay in standard layout and keep rendering. Any other value, a failed query, or a missing interface
MUST leave the tile in standard layout.

`SetRaised` is synchronous, idempotent, and runs on the RedXe UI thread. `TRUE` tells the widget it now occupies a
larger host overlay content rectangle and MUST use that size to present bigger glyphs or every value that now fits,
including extra rows, bars, or history that the compact tile omitted. `FALSE` restores standard tile presentation.
The host sends `TRUE` when raise starts, before the settle eases the viewport, and `FALSE` when a dismiss settle
completes (or immediately when dismiss cannot animate).
The call MUST NOT allocate, wait, or re-enter the host.

`IRedXeInteractiveWidget` is an optional sibling for GPU tiles that have no child HWND. The host hit-tests the same
topmost widget bounds as raise, converts contacts to widget-local pixels, and forwards `OnPointer`. `S_OK` on Down/Up
consumes the contact and MUST NOT count toward double-activate raise. `S_FALSE` leaves raise and mouse edge-click
navigation unchanged. The host still forwards later Move/Up of that one-finger contact to the same widget so it can
start an internal pan after a padding Down. Two- or three-finger host page pan that locks horizontal sends `Cancel`
and does not launch. A second contact that lifts before that lock MUST NOT cancel a captured slider or drop a pending
double-activate. Mouse edge-band clicks never reach the widget. While raised, local origin is the overlay content
rectangle. The host implements `IDropTarget` on the top-level HWND after `OleInitialize`; it parses `CF_HDROP` paths
and Unicode text that is a full URL into `RedXeDropEvent` and calls `OnDrop`. Native-window children that are not drop
targets (GdiOrbit) MUST NOT steal GPU-tile drops. Plugins MUST NOT initialize OLE or call `RegisterDragDrop`.

The current pointer record is 48 bytes and carries view identity, modifiers and wheel delta. View 0 means
the tile; view 1 means the raised layout. Coordinates are widget-local physical pixels and convert to DIP exactly
once inside an embedded adapter. Two phases carry a wheel sample: `RedXePointerPhaseWheel` (vertical, positive is
wheel up) and `RedXePointerPhaseHorizontalWheel` (positive is tilt right). `wheelDelta` retains Win32 wheel units
(one notch is `WHEEL_DELTA`, 120; precision wheels and touchpads deliver fractions) and is zero for every other
phase. A wheel sample never changes capture. Non-finite samples and unsupported phases are rejected. Page pans,
owned live-control drags, hidden windows and display-off suppress wheel dispatch.

The answer to a wheel sample decides who owns the rest of that wheel sequence (`Specs/UI/UI_Dashboard.md`, *Mouse
wheel navigation*): the host offers the first sample of a sequence to the topmost interactive view under the
pointer; `S_OK` keeps every later sample of the sequence with that widget and stops the host from navigating, while
`S_FALSE` hands the sequence to the host, which then changes dashboard pages with it (wheel down or tilt right
advances) and offers no later sample to any widget until the user pauses for 500 ms. A widget that shows a page
control MUST answer `S_OK` for every sample on an axis it pages, including at its first and last page and during
its own drag, so the dashboard never changes page under a paging tile; it answers `S_FALSE` when it has no use
for that sample: a single page, or an axis it does not scroll. A paged widget SHOULD step once per whole detent
through the shared `Common/WheelDetent.h` accumulator (`RedXeWheelDetent`), answering `S_OK` for the fractions
that lead up to a step, so touchpads do not race through its pages; wheel down and tilt right SHOULD move forward,
matching the dashboard. Bundled behaviour: Launcher pages on both axes; the System Data viewers and Weather page
their overflow on the vertical axis and decline the horizontal one; AV Control forwards vertical samples to DxUi
and declines horizontal ones; the Logicon monitor declines both.

A widget with internal pages MUST present them through the shared page control: one dot per page, the current
page's dot larger and brighter, with the `DxUi::PageIndicator` metrics (20 DIP strip, 3 / 4 DIP radii, 14 DIP gap)
that `Common/PageIndicator.h` lays out and hit-tests, so Launcher, the System Data viewers, Weather, and the DxUi
control in AV Control read as one control. A tap on a dot goes to that page and is consumed. A `+N` caption is not
a page control; it remains only for items no page reaches (or where the strip cannot fit, a hero tile). A widget
draws the dots from the layout it hit-tests, so what is drawn is what is tapped; a strip narrower than the block
shrinks the gap rather than dropping dots (never below two selected radii), and no bundled widget pages beyond 32.

`RedXePointerCapture` on Down transfers that one-finger contact to the widget until Up/Cancel. One finger alone MUST
NOT steal an active slider gesture. A second or third concurrent touch may start host page navigation; it cancels that
capture only when the pan locks horizontally. Explicit OS capture is best-effort; failing `SetPointerCapture` MUST NOT Cancel the widget Down. Hidden
state, page or geometry replacement, resize, DPI change, and a canceled or out-of-contact pointer cancel the gesture
before releasing it. `RedXePointerRaise` and `RedXePointerDismiss` are committed-input results; the host applies them
after dispatch, never by lending its HWND to the plugin. Ordinary consumed contacts retain the existing
double-activation behavior described above.

`IRedXeKeyboardWidget` is an optional sibling with view-aware focus, a 20-byte key record and UTF-16 character
delivery. Host focus is independent of COM identity and clears before widget teardown, page replacement or focus
loss. Tab/Shift+Tab traverse participating widgets when the current one returns `RedXeKeyboardBoundary`;
temporary editors may trap their internal tab order. Committed keyboard activation may return raise/dismiss and
persist settings just like pointer Up. Unsupported input returns `S_FALSE`, preserving host shortcuts. Text
composition/IME and a generic UIA bridge are separate pending mechanisms; character forwarding does not imply them.

Shipped extents:

- Process Viewer, GPU Processes, Network, Storage, and Thermal: half.
- System Pulse: quarter.
- CPU, Memory, GPU, and Power: third.
- Studio Clock and Desk Clock: half.
- Launcher: half.
- AV Control: half.
- Matrix Rain: full.
- 5H4D3R5: full.
- Rotating Triangle and GdiOrbit: quarter.

System Data viewers force Standard density while raised so ranked lists and cards can use the overlay. `topN` still
caps row count. Raised System Pulse adds a physical-memory bar and CPU history under its summary chips. A widget that
already fills the client MUST NOT raise.

## Host rendering and resources

- `PluginManager` owns widget providers, generic widgets, and queried rendering-interface references.
- Its `PluginHost` owns one shared module store, all lazily created host data providers and plugin data sources, one
  event-blocked acquisition worker, and subscription drain lifetime. Widgets and subscriptions are released before
  `PluginHost`.
- `PluginHost` has exactly one module slot per bundled module-catalog plugin ID. Catalog plugin IDs and widget type IDs
  MUST stay unique; module file names MAY repeat. The first slot that maps a given module name owns the `HMODULE`;
  later catalog rows with the same name share exports after metadata validation for their own plugin ID. Mapped
  modules remain loaded until process teardown.
- Because the runtime is process scoped, staging an adjacent dashboard page reuses the already-mapped modules, the
  already-created data sources, and the already-running acquisition worker. Staging MUST NOT map a module a second
  time, create a second `IRedXeDataSource` for a provider ID, or start a second acquisition thread.
- Optional `RedXePluginShutdown` runs exactly once per module, at process teardown, after every widget, provider,
  source, and subscription has been released. It MUST NOT run while another dashboard page still uses that module. The widget
  projection MUST remain within the settings limit of 64 plugin declarations.
- Static discovery validates every referenced plugin and effective widget on every page. `PluginManager` creates only
  the current page, plus its adjacent transition page during a swipe, and may share providers only when doing so is
  behaviorally invisible to independent widget instances.
- Document-level validation failures stay fatal: a malformed document, an unknown plugin or widget type, a disabled
  plugin, or private configuration that fails its published schema rejects the document as a whole.
- A catalogued bundled module that `LoadLibraryExW` cannot map is not an unknown plugin. It is a per-instance *runtime*
  mapping failure: skip that module's published-schema check, keep going, and treat current-page instances as
  placeholders. Startup MUST NOT abort because one catalogued DLL is absent or unloadable. A mapped module that
  publishes an invalid settings contract remains document-fatal.
- A per-instance *runtime* construction failure MUST NOT fail its page or startup. The host keeps the authored
  placement, marks the slot a placeholder, records the failing `HRESULT`, and draws its own placeholder over that
  tile. Sibling widgets keep their authored geometry and continue to render. This branch is defensive: current
  document validation rejects the cases that would make a bundled plugin refuse an instance, so it covers runtime
  exhaustion such as memory or subscription slots, and a catalogued DLL that cannot be mapped.
- A widget that reports `RedXeWidgetStatusUnavailable` is drawn with the same host placeholder for as long as it says
  so, and takes its tile back when it reports any other status.
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
  DPI changes reposition the container and notify the plugin without per-frame layout work. During a swipe the host
  translates that same full-size rectangle, which MAY have a negative origin; it MUST NOT hide or shrink a container
  because it is only partly inside the client. The parent HWND clips the visible portion.
- Hidden, minimized, display-off, and DXGI-occluded states call `IRedXeWidget::SetVisible(FALSE)` so every widget
  quiesces visibility-dependent work. Recovery calls `SetVisible(TRUE)` only after visible rendering resumes.
- Dashboard shutdown and page teardown call `CollectPersistentSettings` after `SetVisible(FALSE)` and before `Detach`.
  `S_OK` with a non-empty buffer is forwarded to `IRedXeHost::PersistWidgetSettings`. A persist failure MUST NOT fail
  teardown.

## Bundled plugins

`Plugins/Logicon` exposes service plugin ID `builtin.logicon` (`RedXePluginCapabilityService`, `IRedXeService` plus
`IRedXeDeviceWorker`) and the developer-only widget plugin ID `builtin.logicon-monitor` (type `logicon-monitor`,
Direct3D, interactive, prepared). The service drives the Logitech MX Creative Console keypad over HID++ from its
device lane and turns key presses and dialpad detents into named actions (`Plugins_Actions.md`), publishing the
`logicon` namespace itself; the monitor is the Debug view of that service. Behavior,
protocol, faces, and validation are owned by [`Plugins_Logicon.md`](Plugins_Logicon.md). `Logicon.dll` imports
`hid.dll`, `cfgmgr32.dll`, `windowscodecs.dll`, `ole32.dll`, and `yyjson.dll` (copied beside it); Debug builds also
$1
`Plugins/Actions/Zoom` builds `zoom.action.dll`, the first dedicated action DLL: service plugin ID `builtin.zoom`
(`RedXePluginCapabilityService | RedXePluginCapabilityActions`, `IRedXeService` plus `IRedXeDeviceWorker` plus
`IRedXeActionPack`) publishing the `zoom` namespace over the Zoom Plugin SDK for Windows, or over the client's own meeting
toolbar read and pressed through its MSAA accessibility objects when the SDK cannot serve. Behavior, sign-in, the SDK import, and validation are owned by [`Plugins_Zoom.md`](Plugins_Zoom.md). It imports `bcrypt.dll`, `winhttp.dll`,
`ws2_32.dll`, `advapi32.dll`, and `yyjson.dll` (copied beside it).

`Plugins/RotatingTriangle` exposes settings-visible plugin ID `builtin.rotating-triangle`, internally maps it to type
ID `rotating-triangle`, publishes closed `{}` settings and defaults, and exposes sibling `IRedXeGpuWidget` and
`IRedXeRaisedWidget` interfaces. Its raised extent is quarter.

The DLL owns its triangle geometry, build-time HLSL source and embedded shader bytecode, immutable vertex buffer,
shared constant buffer, animation, aspect correction, and color selection. It does not link or load the runtime shader
compiler. Shared device resources live once per provider rather than once per widget instance. The Debug first page
uses two independent instances. The Release gallery references the plugin, so static discovery maps its DLL at load,
but the first-page runtime creates no Triangle provider, widget, or device resource.

`Plugins/GdiOrbit` exposes settings-visible plugin ID `builtin.gdi-orbit`, internally maps it to type ID `gdi-orbit`,
publishes closed `{}` settings and defaults, and exposes sibling `IRedXeWindowWidget` and `IRedXeRaisedWidget`
interfaces. Its raised extent is quarter. The plugin creates one child window,
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
defaults, and permits one continuous-animation widget per configured provider. Each widget also exposes
`IRedXeRaisedWidget` with a full-client extent. The Release first page gives it the
complete client rectangle, so a double-activate MUST NOT raise that already-full tile; its second page is a varied
gallery of every bundled plugin. The Debug second page is the
equivalent complete bundled-plugin gallery.

The host passes every bundled provider its compact effective settings in the normalized ABI envelope. Matrix Rain strictly rejects malformed,
duplicate, unknown, and out-of-range members, copies normalized numeric settings during creation, and retains no
borrowed JSON. A live page or settings change creates required providers and widgets transactionally; successfully
mapped modules remain loaded.

The plugin owns an original generated 192×288 `R8_UNORM` signed-distance-field atlas (64 vector-polyline glyphs in
24×36 texel cells, the film's bold 2 : 3 glyph shape: 46 mirrored katakana-inspired shapes, the ten digits, eight
Latin letters and symbols, none of them font data; an exact distance field so diagonals stay straight at any size;
the pixel shader thresholds it over about one screen pixel through `fwidth`), nine build-time Shader Model 5.0
shader blobs, one dynamic 128-byte constant buffer, and its immutable pipeline states. It loads no font, DirectWrite,
WIC, runtime HLSL compiler, loose glyph asset, timer, worker, or HWND. Grid values are cached until viewport or DPI
changes, and configured colors are converted to float vectors once during provider construction rather than on every
frame. The grid cell is 0.78 of the glyph height wide, the film's close-set columns. Every active column carries a
main stream and, on about half of them, a shorter sparser second stream, each with its own length, speed, and phase;
glyphs stay put in the grid and light as a head passes. Each stream sits at one of three depths (about a third in
front at full brightness, the rest at 60 % or 34 %), the film's layered look of a few sharp bright columns over a
dimmer field, and its trail is lit evenly from the head down, fading only over its last 45 %. Each cell has its own
brightness (76–100 %), a slow ±6 % flicker, and a mutation schedule that re-rolls its glyph every
`16 / mutationPerSecond` seconds at its own phase with a brief flash, so the field sparkles unevenly. The active
columns (`densityPercent` of the grid, ceiling) are picked in blocks of seven: each block holds its share of the
density (the running total `ceil(block * 7 * density / 100)`, so neighbouring blocks differ by at most one column)
shuffled by its own seeded permutation, which bounds every run of dark columns to two blocks' leftovers (four columns
at the default density) where one permutation of the whole width could leave a hole a third of the tile wide. Stream
phase, speed, trail length, column choice, and mutation are deterministic functions of the configured seed and frame
time. In a tile shorter than the configured trail (a dock bar, a small gallery tile) the streamed trail length shrinks
to two thirds of the visible rows (at least four glyphs) and the off-screen cycle padding to a third of them (two
through eight), so the bright head crosses the tile for most of each cycle instead of hanging below it; both are
grid-cache values, recomputed only with the grid.

Each non-zero visible Matrix frame performs one map/unmap, one opaque background draw, and one alpha-blended instanced
glyph draw, then, unless `glowPercent` is 0, the phosphor bloom: the same instanced draw once more into a
quarter-resolution `R11G11B10_FLOAT` target (glyph light only, heads weighted up), a separable nine-tap Gaussian in
two fullscreen passes between the two bloom targets, and one additive fullscreen composite back onto the host's
render target and viewport, which the plugin reads at entry and restores. The two bloom targets are the only
resolution-dependent resources: `OnTargetSizeChanged` sizes them to a quarter of the largest viewport, a frame draws
into the sub-rectangle it needs, and a frame the targets cannot hold skips the bloom rather than allocating. The CPU
performs no per-column or per-glyph simulation. Submitted glyphs are bounded at 65,536; density
reduces both visible columns and submitted instances. The callback performs no heap allocation, synchronization, I/O,
texture upload, or shader/font work. `OnDeviceCreated` builds the complete provider resource set transactionally and
`OnDeviceLost` releases it idempotently.

`Plugins/5H4D3R5` is the bundled GPU plugin that draws a fixed catalog of full-screen shaders and demos. It exposes
settings-visible plugin ID `builtin.5h4d3r5`, internally maps it to type ID `5h4d3r5`, publishes its complete
closed settings schema and defaults (`Core_Settings.md`), and creates any number of continuous-animation widgets per
provider, each exposing `IRedXeRaisedWidget` with a full-client extent. The catalog is the thirteen entries of
`Plugins/5H4D3R5/ShadersSettings.h` (a short lowercase name that is the `shader` value, the Shadertoy id or
none, title, author, license, and pass flags), each a build-time Shader Model 5.0 shader under
`Plugins/5H4D3R5/Shaders/`: twelve Shadertoy ports that keep the author's original header and license, every one
Creative Commons BY-NC-SA 3.0 (stated by the author or Shadertoy's default), and RedXe's own Cosmic Orb under the
repository terms. `Plugins/5H4D3R5/LICENSES.md` is the notices file. The DLL performs no runtime shader
compilation and loads no font, WIC, or DirectWrite component. The widget MUST draw exactly the catalog: it never
fetches, compiles, or accepts a shader outside it.

Selection is a pure function of the configuration, a per-widget random seed mixed from the clock, the process id and
the instance id at creation, and the host `elapsedSeconds`: `single` shows `shader`; `random` shows one catalog
entry chosen at creation; `slideshow` shows entry `(shader + cycle) mod N` for cycle `floor(elapsed /
intervalSeconds)` and catalog size `N`, or, with `shuffle`, the `cycle mod N`-th entry of a Fisher-Yates permutation
seeded by the random seed and the round `cycle / N`, so each round shows every entry once. `iTime` restarts at zero and `iFrame`
at zero with every cycle; single and random use a 3600 s cycle so `iTime` stays a small float. The image fades
linearly toward the host-resolved dashboard background over the first and last 0.75 s of every cycle (so the widget
also fades in at start). `iMouse` is always zero and `iDate` is the local date; sound channels of the originals are
not reproduced.

The widget exposes `IRedXeInteractiveWidget` so a slideshow can be skipped by hand. The clock every value above is
computed from is the host `elapsedSeconds` plus a per-widget offset, kept in double. Pointer Down answers `S_FALSE`
and records the position; a Cancel forgets it; Move, wheel, and horizontal wheel answer `S_FALSE` (the host pages
the dashboard); drag-over and drop answer `S_FALSE`. In `slideshow` mode a one-contact Up (mouse, touch, or pen)
within 24 DIP of its Down is a tap: it is consumed (`S_OK`, so it does not count toward double-activate raise) and
moves the offset so the clock stands `kFadeSeconds` (0.75 s) before the next cycle boundary, so the current entry
fades out and the next fades in exactly as a timed change does, the cycle counter (and with it the sequential
wrap or the shuffle round) advancing by one; a tap inside that final fade changes nothing because the change is
imminent. A drag past the slop, and every Up in `single` or `random` mode, answers `S_FALSE` and changes nothing, so
double-activate raise keeps working on those tiles. `OnPointer` allocates nothing and does not call the host.

Device resources are split so siblings share what is immutable: the vertex shader, sixteen pixel shaders, the blit
shader, three samplers, the pipeline states, and Heartfelt's procedural 512×512 mipmapped background (generated once
per device on the CPU, never from `Render`) belong to the provider and are built by the first `OnDeviceCreated`
for a device and released by any `OnDeviceLost`; the 96-byte dynamic constant buffer, the offscreen color texture,
and two `R32G32B32A32_FLOAT` feedback buffers belong to each widget. `OnTargetSizeChanged` sizes the offscreen
texture and the feedback buffers to the largest viewport times `renderScalePercent` (offscreen only below 100 %;
feedback buffers only when the configuration can reach a feedback port) and rebuilds them transactionally. A
non-zero `Render` binds every pipeline state it uses, takes references to the host render target and viewport,
runs the feedback pass (feedback ports only, once per presented `elapsedSeconds` at the buffer size, ping-pong),
runs the image pass into the top-left `viewport × scale` region of the offscreen texture or straight into the host
target at 100 %, restores the host target and viewport, and blits with a half-texel-clamped sample rectangle. Each
pass is one three-vertex draw and one constant-buffer map/unmap; `Render` performs no heap allocation, I/O, or
logging. Shader `fragCoord` follows Shadertoy: pixel coordinates of the pass with y up and centers at .5,
computed from `SV_Position` minus the viewport origin, so a negative swipe origin draws correctly; channel reads
flip V. The image entry clamps to the displayable range before the fade; a feedback pass stores raw values.

`Plugins/SystemData` exposes `builtin.system-data` and only factory-created `IRedXeDataSource`. It currently publishes
`source.status` (at most 32 rows, 5 s), `system.summary` (1 row, 1 s, not local-sensitive), `cpu.summary` (1 row, 1 s),
`cpu.logical` (1,024 rows, 1 s), `memory.summary` (1 row, 1 s), `process.list` (2,048 rows, 2 s, local-sensitive),
`thread.list` (8,192 rows, 2 s, local-sensitive), `network.interface` (256 rows, 1 s, local-sensitive),
`network.protocol` (16 rows, 1 s), `storage.disk` (128 rows, 1 s, local-sensitive), `storage.volume` (256 rows, 5 s,
local-sensitive), `gpu.adapter` (32 rows, 1 s), `gpu.engine` (512 rows, 1 s), `gpu.process` (2,048 rows, 2 s,
local-sensitive), `power.summary` (1 row, 5 s), `battery.list` (32 rows, 5 s, local-sensitive), `thermal.sensor`
(128 rows, 10 s, local-sensitive), `fan.sensor` (128 rows, 10 s, local-sensitive), `npu.adapter` (16 rows, 1 s),
`npu.engine` (128 rows, 1 s), `npu.process` (512 rows, 2 s, local-sensitive), and `security.posture` (1 row, 60 s).
It reads local counters with the
caller's token, does not elevate or collect command lines, full executable paths, MAC or IP addresses, serial numbers,
user names, or wireless identities, and degrades inaccessible per-process values to `Unavailable`. Network rows identify
interfaces by `InterfaceLuid`. Protocol rows are IPv4/IPv6 TCP/UDP aggregates; they do not
enumerate endpoints. Disk activity uses overlapped `IOCTL_DISK_PERFORMANCE` where the device accepts it and never
issues `IOCTL_DISK_PERFORMANCE_OFF`; PDH `PhysicalDisk` instance names are not mapped because the mapping is unstable.
Volume rows publish GUID, mount, filesystem, capacity, and extents and never copy whole-disk activity.

Adapter rows are enumerated from the D3DKMT adapter list, not from DXGI, because DXGI enumerates only adapters with a
Direct3D user-mode driver and therefore never returns a compute-only MCDM device. DXGI supplies description, vendor and
device ID, and capacity for rows whose LUID it also reports; no DXGI match is a normal outcome, not an error, and no
D3D device is created. Every adapter carries a device class established in this precedence: the DXCore runtime-agnostic
hardware-type attribute, then the `D3DKMT_ADAPTERTYPE` bits, then DXGI presence. An adapter with no evidence stays
`Unknown` and MUST NOT be assumed to be a GPU. `gpu.*` publishes every class except NPU and `npu.*` publishes only NPU,
so a graphics viewer never silently lists an accelerator. Engine rows remain D3DKMT nodes for both families; process
rows parse GPU Engine counter instance names with a strict `pid_/luid_/phys_/eng_/engtype_` grammar. Per-engine and
per-adapter utilization, machine-wide dedicated and shared memory use, adapter temperature, and `integrated` come from
`DXCoreAdapterState`, which the accelerator families require because the performance-counter set that NPU engines
appear in is not published by Microsoft. Those state items are documented but flagged prerelease, so each is
capability-probed per item per adapter and a failure leaves the column `Unavailable` rather than zero. Adapter
`software` is the DXGI software flag combined with the `D3DKMT_ADAPTERTYPE` software bit and does not require a
dedicated WARP-only host. A batch that requests both a `gpu.*` and the matching `npu.*` dataset performs one node walk
and one counter query, not two. The source may retain a DXGI factory, D3DKMT adapter handles, one `IDXCoreAdapter1` per
enumerated adapter, and one PDH GPU Engine query, all released when the source is destroyed. Power uses `GetSystemPowerStatus` and cached `GetPwrCapabilities`; unknown sentinels
(`255`, `0xFFFFFFFF`) stay unavailable. An AC-only host publishes `batteryPresent` 0 and a zero-row `battery.list`.
Battery rows use SetupAPI `GUID_DEVICE_BATTERY` plus read-only overlapped IOCTLs and never publish serial numbers. Thermal rows re-project GPU,
storage, and battery temperatures and publish ACPI zones only when tenths-Kelvin converts to a plausible Celsius
reading (zero tenths-K is not published). Fan rows are GPU RPM when D3DKMT `MaxFanRpm` is non-zero; generic
`GUID_DEVICE_FAN` presence does not invent motherboard RPM. Battery, ACPI, and storage-temperature IOCTLs use the same
overlapped timeout and `CancelIoEx` drain as disks. There is no device-I/O thread. `security.posture` publishes
code-integrity and virtualization-based-security flags from `SystemCodeIntegrityInformation`, whose two members the SDK
names, plus one documented `IsProcessorFeaturePresent` probe. These are configuration flags rather than counters: a
value that changes between two reads means the record was misread, and the test suite treats it as a failure.
`power.summary` publishes modern-standby capability alongside S3/S4, so a host reporting `systemS3` 0 is not mistaken
for one that cannot sleep. The source MUST NOT include WMI/CIM headers, link WMI libraries, create an `IWbem*`
service, execute a CIM query, or load `wbemprox.dll`, `fastprox.dll`, or `wbemcomn.dll` when any dataset is collected.

Records that the Windows SDK declares with `Reserved*` members are read through RedXe-owned overlay structs in
`Plugins/SystemData/NtLayout.h`. No third-party header is vendored, included, or linked: each overlay names the bytes
itself and pins every consumed member with a `static_assert` on `offsetof` against the matching SDK member, so an SDK
that renames or resizes a reserved block breaks the build instead of shifting a published column. Each consumed field
also has an independent documented oracle in `SystemDataTests`; a field with no oracle MUST NOT enter runtime code.
Records with a build-dependent tail are consumed by the measured `ReturnLength`, never by `sizeof`, and a per-processor
query length MUST be a whole number of records because the kernel rejects a ragged length outright. On the strength of
those proofs, `process.list` and `thread.list` take create, user, kernel, and cycle times, page and hard fault counts,
private working set, pagefile use, parent process ID, and the six I/O counters directly from the single
`SystemProcessInformation` walk. The second bulk parent-ID query is gone, no process handle is opened for those values,
and they are therefore published for every walked row rather than only for processes the caller can open. A transient
`PROCESS_QUERY_LIMITED_INFORMATION` handle remains only for affinity, architecture, critical-process state, and
efficiency mode.
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
`GdiOrbit` remains the bundled native-window example, opt-in and absent from every shipped page. Every viewer also exposes `IRedXeRaisedWidget` so the host can
ask for 1/4, 1/3, 1/2, or 1/1 before raising a tile that is not already full-client.

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
tables. Visual language is a panel in the host-resolved dashboard background with a muted hairline, inset 4 px from
the widget rectangle so adjacent tiles and the window edge keep a small gap. Interior padding is at least 12 px. Accent red (`#FF1616`) is
a high-band signal, not a fill. Progress troughs sit close to the panel so they do not read as a second grey surface;
the fill uses the same teal / amber / red intent as the KPI text (amber from 70% or 70 °C, accent red from 85% or
85 °C). Capacity tracks stay linear 0–100. Numeric utilization, capacity, and temperature values use that intent
color. Battery charge inverts that mapping so a full pack reads healthy and a low pack alarms. Temperature numerals
include the `°C` unit. Byte and byte-rate labels use 1000-based units with one fractional digit (`38.6 KB`, `3.7 TB`,
`1.2 MB/s`); whole `B` and `B/s` stay integer. Process Viewer rows show working set with those units, never an
unlabeled PID, and right-align working set then CPU percent with a measured gap. PID 0 is named `System Idle Process`.
Process and GPU-process load bars
share CPU Meter logic: heatmap idle gray, `SignalColor`, squared luminance mixed with square-root fill length, and the
same 8–14 px bar thickness. Network and disk byte-rate bars use a log10 mapping from 1 KB/s to link speed (or 1 GB/s
when speed is unknown); they MUST NOT normalize to the loudest sibling. Load and capacity tracks sit immediately under
their row text; leftover tile height stays empty below that pair instead of pinning the bar to the cell floor. When a
widget rectangle is wide enough for two packed columns, ranked process, GPU process, network, storage, and thermal
lists MUST split into a two-column grid instead of stretching a single row across empty width. A process or GPU-process
grid MUST drop back to one column when name plus right-aligned stats no longer fit at the grown type size. System
Pulse places the CPU numeral on the left and packs RAM, process, thread, handle, core, uptime, and commit chips into
two columns when width allows. Storage volumes are ranked by used percent then size, render as cards with used/total
bytes, an OK/HIGH/FULL band, and a thick horizontal capacity track that MUST sit below that text. Thermal cards lead
with the `°C` numeral, label COOL / OK / WARM / HOT from the 25 / 70 / 85 °C bands, draw a horizontal level with ticks
at 70 °C and 85 °C, and keep the sensor name above that level with a gap. CPU Meter places a recency-faded,
right-aligned history beside the core heatmap. That history uses stacked translucent bars, a brighter live-edge cap,
and sample-driven scrolling as new values shift in from the right. Network Meter uses the same history treatment for
aggregate throughput and MUST omit interfaces whose combined byte rate stays below 1 B/s for eight consecutive
samples, restoring a row on the first non-zero sample and reporting omitted adapters as idle overflow. Sparklines
and histories window-normalize to the history maximum except CPU percent, which stays 0–100. Temperatures use a
banded fill (25 / 70 / 85 °C) so a cool sensor does not read as an alarm. CPU heatmap cells use squared luminance
and drop the grid when a cell would be smaller than 6 px. Each widget picks a density rung from its inner height
(hero / compact / standard), keeps type floors of 16 / 18 / 30 / 48 px and a title floor of 26 px that grow with
leftover tile height among the rows or cards that fit, and omits columns, rows, heatmaps, and sparks that do not fit
instead of shrinking below those floors. When ranked rows, cards, adapters, or forecast hours/days do not fit, the
widget pages the remainder and exposes `IRedXeInteractiveWidget` so one-finger swipe, wheel, or a tap on the page
control pages it. A paged list reserves the shared page-control strip (`Common/PageIndicator.h`, 20 DIP) directly
below the rows it fitted and above any footer, and draws one dot per page there, centred, the current page's dot
larger and brighter — the same control Launcher draws and AV Control hosts as `DxUi::PageIndicator`. The strip is
reserved whenever the list overflows and at least six tenths of a row still fits above it; on such a short tile the
rows shrink to fit above the strip rather than the strip disappearing (a paging tile MUST show its page control),
and the `+N` caption for items no page reaches shares the strip's band. A tap on a dot (Down and Up on the same
dot, `S_OK` on Up) goes to that page. Items that no page reaches keep the bottom-right `+N` caption: idle network
adapters, non-graphics adapters, CPU heat cells beyond the grid, a hero tile, which has no room for the strip and
keeps `+N` for its remaining rows, and a tile too short for even a shrunken row plus the strip. Wheel down pages
forward one page per whole detent; a viewer with more than one page keeps every vertical wheel sample, including at
its first and last page, so the dashboard never changes page under it, while a single-page viewer and every
horizontal sample return `S_FALSE` so the host can change dashboard pages. Down returns `S_FALSE` so double-activate raise still works. Lists and adapter cards MUST consume the widget rectangle: row or card
height is inner height divided by the visible count, not a theoretical maximum budget. An AC-only power tile centers
`AC` and `no battery`. Ranked-row slide, 320 ms value eases, 60-sample histories, and a brief accent pulse while a
utilization or capacity KPI remains at or above 85% stay sample-driven. Decorative per-panel glow and idle breathing
are not used; history recency fade and a live-edge highlight are sample-driven. Empty and `Unavailable` values
render as muted em dashes; an AC-only desktop renders compact `AC` status, never invented zeros. Widget-local
sparkline history is at most 60 samples in fixed storage and MUST NOT move into System Data. Eases use a 320 ms
ease-out; animation is sample-driven: `GetNextFrameDelayMilliseconds` returns the dataset interval, and while an ease
or pulse is in flight the widget calls `IRedXeHost::RequestFrame` after each presented frame. DirectWrite fills new
atlas glyphs when a snapshot arrives or at device creation, never inside `Render`. A settled System page MUST NOT
request continuous frames.

Shared GPU resources live once per Process Viewer device, not once per widget instance: build-time Shader Model 5.0
blobs, one instanced panel/bar/heatmap/glyph pipeline, one 1024×1024 `R8` atlas, and one dynamic instance buffer.
DirectWrite and system fonts load only while filling new atlas glyphs, then release. Visible process, adapter, and
interface names rasterize only when the displayed string set changes. Each visible widget frame maps its instance
buffer and issues one `DrawInstanced`. The generic widget root MUST NOT grow plugin drawing records. Snapshot delivery
updates fixed storage on the acquisition worker and MUST NOT render; the host coalesced UI invalidation starts the
ease. Hidden, minimized, display-off, occluded, and detached widgets stop eases, drain subscriptions, and do not
replay missed motion.

`Plugins/StudioClock` exposes settings-visible plugin ID `builtin.studio-clock`, internally maps it to type ID
`studio-clock`, and exposes sibling `IRedXeGpuWidget`, `IRedXeScheduledWidget`, and `IRedXeRaisedWidget` interfaces on
one controlling `IUnknown`. Its raised extent is half. Its provider returns one of two module-static immutable
descriptors: without a date the design size is
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
  "timeColor": "#FF1616"
}
```

The schema is closed. Colors are exact `#RRGGBB` strings with case-insensitive hexadecimal digits; supported date
orders are `dd-mm-yyyy`, `mm-dd-yyyy`, and `yyyy-mm-dd`. The host merges defaults before factory creation. The plugin
strictly rejects missing effective members, duplicate or unknown members (including `backgroundColor`), malformed
booleans, colors, and date formats, converts values once during provider creation, and retains no borrowed JSON. Its
opaque background is `RedXeFactoryOptions::backgroundColor`.

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
`desk-clock`, and exposes sibling `IRedXeGpuWidget`, `IRedXeScheduledWidget`, and `IRedXeRaisedWidget` interfaces on
one controlling `IUnknown`. Its raised extent is half. Its static descriptor has a 1600×600 design size and 320×120
minimum and does not request continuous
animation. Both shipped galleries reference it. Static Release discovery maps its DLL, while the unchanged first-page
Matrix composition creates no Desk Clock provider, widget, deadline, device resource, or render work.

Desk Clock publishes and consumes this complete effective settings object:

```json
{
  "flipDurationMilliseconds": 420,
  "cardColor": "#FF3B43",
  "digitColor": "#FFFFFF",
  "dateColor": "#D8D8D8"
}
```

The schema is closed. `flipDurationMilliseconds` is an integer from 250 through 800. Colors are exact `#RRGGBB`
strings with case-insensitive hexadecimal digits. The host merges defaults before factory creation; the plugin accepts
only the normalized factory envelope and strictly rejects missing, duplicate, unknown (including `backgroundColor`),
malformed, or out-of-range members, converts settings once during provider creation, and retains no borrowed JSON.
Its opaque background is `RedXeFactoryOptions::backgroundColor`.

The clock renders zero-padded local 24-hour `HH:MM:SS` as six slim warm-red rounded cards on an opaque background,
with tight within-pair spacing, wider separator gaps, small fixed colon dots, subtle centered hinge seams, and clean
high-contrast condensed digits. Its compact invariant-English date line is title case in `ddd D MMM` form and does not
pad a single-digit day. Desk Clock privately uses DirectWrite to rasterize the required glyphs from the in-box Windows
Bahnschrift SemiBold Condensed face, with a deterministic in-box fallback, into one bounded grayscale atlas with a
short mip chain. It preserves the font's contours, proportions, common baseline, and real date advances; it does not
stretch individual glyphs into synthetic fixed-width shapes.

The atlas is rasterized during transactional device-resource initialization and again on `OnTargetSizeChanged`, at a
resolution tier chosen from the size the host is about to draw. Every cell, offset, and em size scales with the tier,
so normalized atlas coordinates are identical across tiers and the pixel shader needs only the live edge length. The
base tier is 1024 square; a second tier of 2048 square covers a widget drawn larger than the base tier can resolve,
which no amount of filtering can fix because the detail was never rasterized. Mip levels cover the opposite case, a
widget drawn smaller than the atlas. Date cells are the small ones and a filtered level erases their strokes, so date
glyphs sample level 0 explicitly while time glyphs take the chain. Total coverage stays under 1.5 MiB at the base tier
and under 6 MiB at the second, and the higher tier is only built while a widget is actually drawn that large.

The constraint is on the steady path, not on DirectWrite. Each rasterization loads the system32 DirectWrite module
through an isolated factory and releases its module handle, factory, font faces, analysis objects, and temporary CPU
coverage before returning. Inactive discovery and `Render` do no font lookup, shaping, rasterization, or allocation. A
failed tier change leaves the previous atlas intact and the widget still renders. The composition scales uniformly and
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

Weather manual city lookup MUST encode the UTF-8 location as one URL query value. ASCII unreserved bytes pass through;
all other bytes, including spaces, Unicode bytes, and query delimiters, use percent encoding. The bounded builder
accepts at most 128 input bytes and fails without a partial URL if the output buffer cannot include the terminator.
Offline WeatherTests MUST cover spaces, Unicode, reserved characters, unreserved characters, exact capacity, and
empty/overlong input; live geocoding is not a test dependency.

Weather location selection, disposable Windows helper, saved city, precipitation notices, glyph fitting and responsive
hourly/daily composition are owned by [`Plugins_Weather.md`](Plugins_Weather.md). A configured city takes precedence;
automatic discovery must never use a country centroid as the computer's city.

Weather location selection, disposable Windows helper, saved city, precipitation notices, glyph fitting and responsive
hourly/daily composition are owned by [`Plugins_Weather.md`](Plugins_Weather.md). A configured city takes precedence;
automatic discovery must never use a country centroid as the computer's city.

`Plugins/Launcher` exposes settings-visible plugin ID `builtin.launcher`, internally maps it to type ID `launcher`,
and exposes sibling `IRedXeGpuWidget`, `IRedXeInteractiveWidget`, and `IRedXeRaisedWidget` interfaces on one
controlling `IUnknown`. It rejects `IRedXeWindowWidget` and `IRedXeScheduledWidget`. Its raised extent is half. The
static descriptor is 480×480 with a 160×160 minimum and does not request continuous animation. The Debug first page
places it as the left leaf so page-1 widget count stays 4. Both shipped galleries add one Launcher leaf (page-2 count
7). It is not on the System page. Shipped examples use `{"shortcuts":[]}`.

Launcher settings are the closed object `shortcuts` plus optional `iconSize`. `shortcuts` is an array of 0 through 32
items. Each item is a closed binding object (`Plugins_Actions.md`): optional `action` (default `system.launch`, which
is what a taskbar import writes), `target` (UTF-8, at most 512 bytes; required and non-empty for `system.launch`),
and optional `icon` (a Segoe Fluent Icons glyph name or `png:<absolute path>`, at most 260 bytes; required for any
action other than `system.launch`, which has no file to extract an icon from). `Plugins/Launcher/LauncherBindings.h`
(`ParseShortcutItem`) is the single validator of that shape for the DLL and the host. `iconSize` is
`"small"` (72 DIP), `"medium"` (96 DIP), `"large"` (144 DIP), `"huge"` (fixed 192 DIP jumbo cell), or
`"automatic"` (the default: the largest icons that fit one page, another page only for icons twice the edge).
Unknown members, an unknown `action`, an empty launch target,
a non-launch item without `icon`, overlong strings, unknown `iconSize` values, nested arrays, and more than 32 items
reject the complete candidate. Duplicate shortcuts (equal actions and, for a launch path, a case-insensitive Win32
path compare, else an exact compare) reject the document. A launch `target` that is not an absolute Win32 path
(`C:\\...` or `\\\\server\\share\\...`) or a URI with an alphabetic scheme of at least two characters, or any
target `IRedXeHost::ValidateAction` refuses, is accepted and drawn as the `Warning` glyph tile that does nothing when
clicked. Compact settings remain ≤ 4096 bytes.

When the authored list is empty, `SetVisible(TRUE)` enumerates up to 32 `.lnk` files in the current user's pinned
taskbar folder (`FOLDERID_UserPinned` + `\TaskBar`, then the roaming Quick Launch `User Pinned\TaskBar` fallback),
sorted by name, skipping `desktop.ini`. Import the fitting prefix into authored shortcuts (at most 4096 compact JSON
bytes), then queue it through `IRedXeSettingsQueue` for UI-thread persistence outside the visibility callback. The
import remains dirty for collect fallback if the queue is unavailable, full, or its commit fails. A populated list
MUST NOT be reimported or repeatedly queued on later visibility changes. Later drops append to the imported list;
a failed drop save restores both the earlier shortcuts and their dirty state. Saved shortcuts are user-editable
and authoritative on subsequent creation. An empty/unavailable folder causes no save.
`--self-test`, HostPluginTests, and other automated hosts set
`REDXE_AUTOMATED_HOST=1` and MUST NOT read the live taskbar; tests inject a pin directory through
`RedXeLauncherSetTestPinDirectory`. `nullptr` restores live enumeration; an empty string means no pins.

A committed tap calls `IRedXeHost::ExecuteAction` with the item's binding on the UI thread and plays the launch
motion for every action; a failed result reports `RedXeWidgetStatusDegraded` "Launch failed". `system.launch` is
the host's `ShellExecuteExW` policy (`Plugins_Actions.md`); Launcher itself never calls the shell. Automated hosts
count launches and MUST NOT reach `ShellExecuteExW`. Icon extraction is off `Render`: a glyph `icon` is rasterized
through DirectWrite (`Common/Actions/GlyphIcon.cpp`) into the same 256×256 slice a shell icon fills, an invalid
binding draws the `Warning` glyph, `png:` via WIC (PNG container only, long edge capped at 256),
else `IExtractIconW` 256, else `IShellItemImageFactory::GetImage` 256 with `SIIGBF_BIGGERSIZEOK`, else
`SHGFI_SYSICONINDEX` plus `IImageList::GetIcon` from `SHIL_JUMBO` then XL/LARGE/SMALL. Never `SHGFI_ICON`. Jumbo
padding is trimmed. Device loss keeps CPU BGRA and re-uploads without a second shell extract.

Shared device resources live once per provider: embedded Shader Model 5.0 blobs, textured-quad pipeline, sampler, and
immutable blend/rasterizer/depth state. Each instance owns a 32-slice 256×256 `Texture2DArray` (8 MiB BGRA) for jumbo
icons, one 96-byte dynamic constant buffer (viewport, hint, `iconCount`, page count and selection, and the page-dot
pixel layout), and one 32-slot
dynamic structured buffer of instance rects and motion (32 bytes each). `Render` is allocation-free: it maps those
existing buffers with `WRITE_DISCARD` and issues at most two draws (background plus instanced icons). Instance rects
MUST NOT live in the constant buffer. Device loss keeps CPU BGRA and re-uploads the texture array without a second
shell extract. Grid geometry uses two bounded cache entries keyed by actual width, height, DPI, shortcut count, `iconSize`, and
launcher page. The tile and overlay therefore reuse distinct layouts, and pointer hit testing uses the most recently
drawn layout (the overlay draw is last while raised). A largest-target notification MUST NOT displace or clip icons in
the original tile.
A swipe viewport keeps the widget's full size and may have a negative origin; `Render` must still draw. When more
shortcuts exist than fit at the chosen cell size, Launcher paginates them and GPU-draws the shared page control
(`Common/PageIndicator.h`: the `DxUi::PageIndicator` metrics, 20 DIP strip, 3/4 DIP radii, 14 DIP gap) along the
bottom of the tile, centred; the shader draws the dots from the pixel layout Launcher computes on the CPU, and the
same layout hit-tests dot taps. The strip is reserved only when `pageCount > 1`; a single page uses the full tile
height. Fixed `iconSize` values keep one square icon edge
and pagination quantum: `small` 72 DIP, `medium` 96 DIP, `large` 144 DIP, `huge` 192 DIP jumbo. The extracted 256 px
texture MUST NOT dictate layout or half-extents. Named `iconSize` is the minimum cell used to decide pagination
(`count > columns * rows` at that DIP, including gutters). When fewer icons occupy the tile than that minimum cell
would pack, Launcher spreads them: it chooses columns and rows that cover the width×height (matching the tile aspect,
so a tall strip becomes one or two columns stretched down the tile rather than a compact cluster) without going
below the named DIP cell, grows the icon up to that named size (or `automatic`'s current size, never past 256 DIP
jumbo texture usefulness), and distributes leftover space as even gutters around and between cells
(`space-evenly`). Adjacent icon edges stay at least `kLauncherMinGutterDip` (8 DIP, DPI-scaled) apart, and edge icons
inset at least `kLauncherEdgeInsetDip` (8 DIP, matching) from the tile edge and page-dot strip. A 4 DIP inner gutter
keeps icon ink inside the cell so padding around a single-icon tile remains hittable. Named sizes MUST NOT shrink
below their DIP except when the tile is smaller than one cell plus edge insets, in which case the grid scales
uniformly to the shorter remaining edge. `automatic` (the default) weighs icon size against page count
(`LauncherChooseAutomaticGrid`): every whole grid of columns × rows is a candidate, with the cell as large as the
tile allows for that grid (at most the huge cell, never below the 72 DIP floor; a grid that pages gives up the
dot strip), scored by cell edge divided by page count, so a second page has to buy icons twice the edge of one
page's and a third three times. Eight shortcuts on a 799×559 tile at 150 % are one page of 4 × 2 icons of 123 DIP
where `huge` pages two at a time; nine on a 322×571 strip are two pages of 2 × 3 (six, then three) 95 DIP icons
where `huge` pages one at a time. Ties (the cell capped at huge) go to the fewest pages, then the fewest cells per
page. When not even one floor cell fits, the floor cell pages one shortcut at a time and the draw shrinks it to the
tile. Overflow still paginates when `count > columns * rows` at the chosen cell.
Hit testing uses the same cell centers and half-extents as `Render`. One-finger horizontal swipe or a
tap on a dot changes the launcher page and MUST NOT launch. A swipe follows the finger 1:1 after the horizontal lock
and draws the outgoing and incoming icon pages in the existing instanced draw by sliding instance-rect x positions; dots
stay pinned to the bottom strip. On release, a short ease-out cubic settle (140–280 ms, presentation-paced
`RequestFrame` from `Render`) commits when distance or flick matches the host page-pan thresholds, otherwise snaps
back. A dot tap MUST slide to that page rather than teleport, in the same direction a swipe to it would: the
target page enters from the right when moving forward and from the left when moving back. A mouse wheel pages the
same way, one launcher page per whole detent on either axis (wheel down or tilt right forward; a fraction is
accepted without paging). A launcher with page dots keeps every wheel sample, including at its first or last page
and during a one-finger drag, so the dashboard never changes page under it; a single-page launcher returns
`S_FALSE` so the host can change dashboard pages with it. Fewer than two launcher pages paint no dots.
A committed click starts a bounded 3D launch motion of at most 400 ms via `RequestFrame` from `Render` only; idle with a static
grid owns no wake-up. Page-slide settle uses the same `RequestFrame` rule and MUST NOT add a timer thread. `RequestFrame` MUST NOT be called from `SetVisible` or `OnDeviceCreated`. `OnDrop` and
`OnPointer` (committed click) MAY call `RequestFrame` and `PersistWidgetSettings`. `CollectPersistentSettings` returns
`S_FALSE` when neither imports nor edits await persistence. A queued import retains collect fallback until a later
synchronous save succeeds; queued acceptance alone is not a commit acknowledgement.

## Required validation

1. Run `./format.ps1` and `./validate-skills.ps1`.
2. Run Debug and Release x64 `test.ps1`; both must pass the plugin contract executable, the production host/plugin
   harness, and the hidden application WARP smoke frame.
3. Build Release ARM64 and confirm the host, plugins, contract tests, and host/plugin harness compile.
4. Verify factory null outputs, unsupported IIDs, non-empty IDs, exact current record sizes, rejection of smaller and
   larger records, every
   configuration pointer/length mismatch, the 8192-byte cap, synchronous configuration copying, and borrowed array
   stability.
5. Verify generic widget/rendering-interface negotiation, root visibility, collect-on-exit, and controlling-IUnknown
   identity. The
   GPU-only widget rejects the window IID and the window-only widget rejects the GPU IID. Every bundled widget exposes
   `IRedXeRaisedWidget`, returns `E_POINTER` for a null extent, reports its shipped fraction, and accepts idempotent
   `SetRaised`. A widget with nothing to save returns `S_FALSE` from `CollectPersistentSettings` and `E_POINTER` for a
   null `writtenBytes`. Compile-time contract checks MUST also pin every public record's `sizeof`, and every pointer-bearing
   record's field offsets, and MUST prove that every public COM interface derives directly from
   `IUnknown` and that neither rendering, scheduled, raised, interactive, nor network interfaces derive from
   `IRedXeWidget`. Persist is on `IRedXeHost`, not a base of `IRedXeWidget`.
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
12z. Host tests MUST prove that the host reports a GPU widget's target size once device resources exist, that
    rendering frames at an unchanged size reports nothing further, that a viewport above the composition design size
    moves Desk Clock's glyph atlas to its higher tier and back down when the viewport shrinks, and that the widget
    still renders after a tier change. Also verify one notification for a DPI-only change, none for unchanged DPI,
    correct initial notification for a same-sized replacement page, and cache preservation on promotion.
    Lifecycle tests MUST prove data/network widgets are hidden during all device callbacks and render after WARP
    recreation. Event-barrier tests MUST prove subscription release and deactivation wait for running and reserved
    callbacks, allow reactivation, and safely reuse released subscription slots.
12y. Host tests MUST prove that a page swipe which places Matrix Rain, Studio Clock, or Desk Clock partly off the
    render target still draws those widgets, including when the incoming page's viewport origin is negative. Plugin
    tests MUST prove Studio Clock and Desk Clock accept a finite negative viewport origin.
12a. Host tests MUST prove that staging an adjacent dashboard page adds no second acquisition thread, no second
    data source for a provider ID, and no second module map, and that repeated provider lookup returns one shared
    controlling identity.
12b. Host tests MUST prove that `RequestFrame` succeeds and coalesces without a UI target, that a widget status
    report is recorded, copied into bounded storage, and cleared on recovery, and that malformed status records,
    unknown status values, and null arguments are rejected.
12c. Host tests MUST prove that a valid page produces no placeholder tiles, that a widget reporting
    `RedXeWidgetStatusUnavailable` hands exactly its own tile to the host while siblings keep drawing, that
    `Degraded` and `Initializing` do not, and that recovery returns the tile to the widget. They MUST also prove that
    a catalogued plugin whose module cannot be mapped becomes a placeholder beside a constructable sibling and does
    not abort page initialization. Host tests MUST prove `Weather.dll` maps and constructs a GPU widget (its curl and
    zlib runtime DLLs MUST sit beside it in `Plugins\`). WeatherTests MUST prove `sizeof(WeatherHttpResponse)` stays
    within 64 bytes and that a cancelled `WeatherHttpGet` on a 192 KiB reserved stack returns `ERROR_CANCELLED` without
    overflowing. HTTP response bodies are 256 KiB heap buffers; they MUST NOT be automatic arrays on the network worker.
    `weathericons-regular-webfont.ttf` MUST sit beside `Weather.dll` so DirectWrite can atlas-rasterize Weather Icons
    glyphs; `Render` stays allocation-free. Host tests MUST prove the network lane with a stub widget: while access
    is disabled, activation never calls `RunNetworkWork` and starts no thread; once enabled, one worker runs it,
    deactivation cancels and drains the in-flight call before returning, and disabling access joins the worker.
12d. Host tests MUST prove that `PersistWidgetSettings` without a host handler returns `E_UNEXPECTED`, that a handler
    receives a partial settings object, and that a constructed widget with nothing to save returns `S_FALSE` from
    `CollectPersistentSettings`. Settings tests MUST prove a partial merge keeps unspecified members and rejects
    unknown plugin members.
12e. Host tests MUST prove that `Log` rejects a null record, a mismatched `sizeBytes`, a missing event or message, and
    an unknown level; that `Log` before `SetLogDirectory` succeeds and writes nothing; that `SetLogDirectory` plus
    `FlushLog` produces a UTC-dated JSONL file containing `ts`, `level`, `event`, and `message`; that retention deletes
    expired dated files and legacy undated names; and that `--self-test` never
    calls `SetLogDirectory`. Compile-time contract checks MUST pin `sizeof(RedXeLogRecord)` and its pointer offsets.
    Settings tests MUST prove the default logs directory is the `Logs` sibling of `Settings` and that `logRetentionDays`
    defaults to 15 and rejects 0 and 366.
    Long escaped identities/messages, split multibyte boundaries, and malformed UTF-8 MUST remain independently
    parseable JSONL with their HRESULT suffixes and the following record intact.
13. Compile-time checks MUST validate unique bundled plugin IDs and module names, keep every widget projection entry
    backed by one module entry, and keep that projection within the 64-plugin settings limit.
14. Keep `/W4`, `/permissive-`, SDL checks, and warnings-as-errors green.
15. Verify the system-data factory and `IRedXeDataSource`, controlling-IUnknown identity, static descriptors including
    unique IDs and `source.status`, unknown-dataset and malformed-batch rejection, common batch sequence/timestamp,
    sequence advance only on full-batch success, discarded CPU rate history after a mid-batch failure, summary shape,
    process-row bounds, current-process visibility, value types and quality,
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
      cadence without another delivered sample. Cumulative diagnostic counters must be compared with their
      pre-creation values; repeat the subscription scenario in-process after a delivered sample to cover module reuse.
      Verify the ten System Data viewer IDs enumerate from `ProcessViewer.dll`,
    reject factory creation without a host, and that a ten-widget System page attaches as GPU+scheduled widgets,
    renders on hidden WARP, rebuilds after device loss, stays non-continuous, and drains every subscription when hidden.
    On that page a Process Viewer with `topN` 32 MUST report more than one overflow page and a drawn page-control
    layout through its test diagnostics, and a Down/Up on its second dot MUST page it forward (`S_FALSE` on Down,
    `S_OK` on Up) and a tap on the first dot back, each visible in the next frame's diagnostics; that viewer MUST
    keep wheel-up at its first page (`S_OK`, no movement) and page forward on wheel-down, and every viewer MUST
    decline every horizontal wheel sample. A Process Viewer tile 144 px tall on the 720 px host (one row less than
    a row plus the strip) MUST still report its overflow pages and a dot layout inside the tile. `HostPluginTests`
    MUST also prove the
    shared page-control geometry (`Common/PageIndicator.h`): DxUi::PageIndicator metrics at 96 DPI, DPI scaling,
    right alignment, gap shrink on a narrow strip with the two-selected-radii floor, no control for one page, more
    than 32 pages, or an empty strip, and strip-height taps landing on the nearest dot within half a gap.
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
19. Verify Launcher factory/settings rejection (unknown members, 33 items, empty launch target, `iconPng`, a
    non-launch item without `icon`, overlong string), the warning tile for a relative or schemeless launch target,
    a glyph-icon non-launch item dispatched through `ExecuteAction`, injected pin-directory fallback, automated empty list without live taskbar reads,
    jumbo-or-PNG extraction, WARP two-draw icon grid, device-loss re-upload without a second extract, launch counting
    with zero `ShellExecuteExW`, imported-pin persistence/recreation, queue/commit failure and collect fallback,
    drop append/cap/rollback, overflow paging with a bottom page-dot strip, named `iconSize` spread-grid layout
    (uniform half-extents, even gutters of at least 8 DIP between icon edges and 8 DIP edge inset, no mid-tile cluster
    when leftover space exists, no clip except page-slide), one-finger follow-finger page swipe with
    settle (no launch), page-dot tap animation entering from the correct side, wheel paging (kept without moving
    at the first page, fractions accepted without paging, a whole detent pages forward from the right, tilt-left
    pages back from the left and is kept at the first page, no launch), and ordering of an older queued import
    before a newer interactive save, through
    `LauncherTests`, `SettingsTests`, and `HostPluginTests`. WARP pixel and hit-target tests MUST alternate tile and
    overlay sizes after one largest-size notification and verify zero allocations on those cached draws.

20. Verify the service contract and the device lane through `LogiconTests` (no hardware) and `HostPluginTests`:
    service creation with a valid and an invalid envelope, `Start` with device access disabled opens no device, the
    lane reports running and returns within 1.5 s of `stopEvent`, host state reaches the lane, the synthetic keypad
    connects and receives faces, brightness, page-button diversion, and the splash reset on stop, injected and raw
    key presses request the bound actions through `RequestAction`, key pages change on `logicon.keyPage.*` and page
    buttons, dialpad turns dispatch their `turns` bindings, `ApplySettings`
    re-applies and rejects without stopping, the monitor provider constructs in Debug and refuses in Release, and
    `--self-test` starts every configured service. `PluginHost` tests MUST cover the named action ring's bounds,
    coalescing, and `ERROR_BUSY`, and the services' catalog validation. `SettingsTests` MUST cover the `services`
    grammar and rejections and both templates' Logicon and Zoom objects. Plugins_Logicon.md owns the protocol and
    face vectors.

21. Verify the action contract, registry, validation, and execution through `HostPluginTests` and the Zoom service
    through `ZoomTests` as [`Plugins_Actions.md`](Plugins_Actions.md) and [`Plugins_Zoom.md`](Plugins_Zoom.md)
    require; `HostPluginTests` MUST map every catalogued module and prove no action namespace collides.

The automated Debug host composition must contain the GPU launcher, one rotating-triangle GPU fixture, the GDI
fixture, and Matrix Rain. The automated
Release first-page composition must contain one full-canvas Matrix widget. The Debug template's second page is
`logicon` and places the developer-only Logicon Monitor beside a Studio Clock. The gallery page of each shipped template
must demonstrate every settings-visible plugin in the compile-time bundled widget projection, including Process Viewer,
Studio Clock, Desk Clock, Weather, and Launcher, except the developer-only widgets, which only the Debug template places,
and 5H4D3R5, which each template places alone on its last page (`Shaders`) as a slideshow: Release sequential at
120 s and half render scale, Debug shuffled at 60 s. `PluginContractTests` MUST load `5H4D3R5.dll`, validate its
contract and configuration rejections, render every catalog entry on WARP at 50 % and 100 % render scale through the
production entry points (feedback ports long enough for their simulation to show) and prove none is one flat color,
prove the slideshow order, the frame restart at a change, the fade through the dashboard background, the tap that
skips a slideshow ahead (consumed Up, fade-out then the next entry, wrap-around after a catalog's worth of taps),
that a drag and a click in `single` mode change nothing, the shared provider resource set, device-loss recreation,
and an allocation-free steady-state `Render`.
The `System` page of each shipped template MUST place one instance of every Process Viewer family widget. Both
templates configure every catalogued service under `services`.
Scheduler tests must prove
that hidden, minimized,
suspended, display-off, and occluded states select an event-blocked action. Contract tests must confirm that loading
the GPU plugins does not load `d3dcompiler_47.dll`.

Security-posture validation treats `codeIntegrityOptions` as the complete unsigned 32-bit Windows record.
Zero is valid when no flags are enabled; unknown bits do not invalidate the snapshot. Tests require available
raw data and compare the published code-integrity and memory-integrity booleans with their documented bits.
See Microsoft's [SYSTEM_CODEINTEGRITY_INFORMATION contract](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation#system_codeintegrity_information).
