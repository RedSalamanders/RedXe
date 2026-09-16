# Logicon: Logitech MX Creative Console service plugin

Status: `ACTIVE`
Created: 2026-09-16
Owner: host service-plugin lifetime, host-owned device lane, host action queue, `services` settings root, bundled
`Logicon.dll`, Debug-only `Logicon Monitor` tile

## Goal

Ship **Logicon**, a bundled plugin that drives the Logitech MX Creative Console from RedXe:

- reads every keypad control: the nine LCD keys and the two page buttons;
- paints the nine key faces (icon, label, color, live dashboard state) and sets panel brightness;
- maps controls to dashboard actions: page navigation, raise/dismiss a widget, launch a target, media keys, and
  Logicon key pages;
- later reads the MX Creative Dialpad (dial, roller, four buttons) once its protocol is captured.

In **Release** Logicon has no dashboard tile. It is a headless *service* the host starts at launch and stops at exit.
In **Debug** the same DLL also publishes a `Logicon Monitor` GPU tile that shows live control state, the current key
faces, HID++ traffic, and lets a developer press keys, push test faces, and change brightness from the dashboard.

Before the plugin exists, this plan settles three host mechanisms the repository does not have:

1. a **service plugin** that runs without a widget instance (today every plugin object is created for a placed
   widget or for a data provider some widget requested);
2. the **host-owned device-I/O lane** that `Core_PerformanceAndResources.md` permits but forbids to ship "until
   timeout, cancellation, and teardown drain are bounded";
3. a **host action queue** so a plugin can ask the host to change page, raise or dismiss a widget, or launch a target.

Owning contracts (updated at closeout, not by this file alone):
[`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md),
[`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md),
[`../../Core/Core_Settings.md`](../../Core/Core_Settings.md),
[`../../UI/UI_Dashboard.md`](../../UI/UI_Dashboard.md), and a new `Specs/Plugins/Plugins_Logicon.md`.

Historical context:

- [`../Done/WeatherPlugin_2026-09-04.md`](../Done/WeatherPlugin_2026-09-04.md) — plugin-owned client on a host-owned lane; the
  factorization this plan repeats for HID.
- [`../../Plugins/Plugins_AVControl.md`](../../Plugins/Plugins_AVControl.md) — isolated helper process and the
  bounded control lane; the alternative this plan rejects for HID and keeps as fallback.
- [`../Done/SystemDataPlugin_2026-08-31.md`](../Done/SystemDataPlugin_2026-08-31.md) — no plugin-owned threads; the
  host owns every worker.

## Scope

### Included

- Host: `RedXePluginCapabilityService`, `IRedXeService`, `IRedXeDeviceWorker`, one host-owned device thread per
  started service (at most 4), `IRedXeHost::RequestHostAction`, host-state publication to services, the `services`
  settings root (version 5.1), and a Debug-only bundled-widget catalog class.
- `Plugins/Logicon/`: HID transport (`hid.dll`, `cfgmgr32.dll`, overlapped I/O, hotplug), HID++ 2.0 protocol (root
  feature lookup, `0x1B04` control diversion, `0x8040` brightness, `0x19A1` VLP image stream), key-face renderer
  (DirectWrite alpha + CPU compose + WIC JPEG), bindings (settings → actions), key pages, and host-state feedback.
- Debug-only `builtin.logicon-monitor` Direct3D tile in the same DLL.
- `Tests/LogiconTests` with a fake HID++ device, HostPluginTests coverage for service lifetime and the action queue,
  SettingsTests coverage for `services`, and a hardware validation checklist.
- `docs/plugins/logicon.md` (configuration) and a `docs/usage.md` note.

### Excluded

- Logi Options+ / Logi Plugin Service (Actions SDK, `.lplug4`) integration. Logicon is a direct-HID alternative and
  requires Options+ to not own the keypad; it never terminates Options+ processes.
- hidapi, libusb, libjpeg, or any new vcpkg port. The transport is `hid.dll` + `cfgmgr32.dll`; JPEG is WIC.
- A generic host HID ABI (`IRedXeHidService`). The transport stays plugin-owned on a host lane, as curl did.
- Mirroring live dashboard pixels onto the keys. Revisit after Phase 0 measures transfer throughput.
- Firmware/DFU, Bluetooth pairing, Bolt receiver management.
- Dialpad behavior beyond discovery until a capture exists (Phase D gates on it).
- ABI freeze, settings migration beyond the additive 5.1 root member, or push data providers.

## Verified device protocol

Sources: `packages/core/src` of
[node-logitech-mx-creative-console](https://github.com/Julusian/node-logitech-mx-creative-console) (keypad only; the
dialpad is commented out), `API.md` and `src/hid.ts` / `src/display.ts` of
[mx-keypad-ahp-bridge](https://github.com/digitarald/mx-keypad-ahp-bridge), and the feature dump in
[Solaar issue #3100](https://github.com/pwr-Solaar/Solaar/issues/3100). The analysis attached to the request (nine
64×64 RGB565 screens, report `0x02`, PID `0xB377`) matches none of them and is not used.

| Item | Value |
| --- | --- |
| Keypad | USB-C, VID `0x046D`, PID `0xC354`, HID++ 4.5, vendor usage page `0xFF43` |
| Dialpad | Bluetooth / Logi Bolt, model `BC00`; `0x1B04` shows Back, Forward, "Button 6", "Left Scroll As Button 7"; dial and roller events not yet captured |
| Reports | `0x11` HID++ long (20 B incl. ID, in/out), `0x13` VLP control (32 B, in/out), `0x14` image stream (4095 B, out), `0x04` system control, `0x05` consumer control (1 B, in) |
| HID++ frame | `[id][deviceIndex 0xFF][featureIndex][function<<4 \| softwareId][params…]`; Logicon uses software ID `0x0B` like the references |
| Root `0x0000` | function 0 `getFeature(featureId)` → feature index (0 = absent). Observed indexes: `0x19A1`→`0x02`, `0x1B04`→`0x0B`, `0x8040`→`0x0F`. Logicon resolves them at connect and treats the observed values as test vectors only |
| `0x1B04` Reprogrammable controls v4 | fn 0 count, fn 1 `getCidInfo(index)`, fn 2 `getCidReporting(cid)`, fn 3 `setCidReporting(cid, flags, remap, flags2)`; flags `0x03` = divert + dvalid. Event 0 on `0x11` lists up to four pressed CIDs (u16 BE). CIDs `0x0001–0x0009` LCD keys, `0x01A1` previous page, `0x01A2` next page |
| `0x8040` Brightness | fn 2 `setBrightness(u16 BE)`, 1–100; 0 resets the device |
| `0x19A1` Contextual display | display index 1 (index 0 accepts writes but does not replace the splash). Event 0 arrives on `0x13`: `[FF][02][00][?][01 display][pressed key IDs 1–9 …][0]`. Function 2 = set image over `0x14` |
| Panel | 480×480. Keys 118×118 at `x = 23 + column·158`, `y = 6 + row·158`. The 434×434 rectangle at (23, 6) covers all nine keys and the 40 px gaps and can be written as one JPEG |
| `0x14` first frame | `[0]=0x14 [1]=0xFF [2]=featureIndex [3]=0x2B [4]=VLP byte [5]=display 1 [6]=defer update [7]=image count 1 [8]=format 0 (JPEG) [9..10]=x [11..12]=y [13..14]=w [15..16]=h (u16 BE) [17..19]=JPEG length (u24 BE) [20..]=data` |
| `0x14` continuation | bytes 0–3 repeated, `[4]` = VLP byte, data from byte 5 |
| VLP byte | `0x80` first, `0x40` last, `0x20` data, low nibble = 1-based sequence |
| Image | JPEG. References use quality 78 with 4:4:4 subsampling (bridge) or 95 (node) |
| Feature report `0x03` | `[03][02][00…]` (32 B) resets the panel to the Logi splash |
| Timing | node inserts a 10 ms pause after each report batch "to prevent the device skipping draws"; the bridge sets `defer update` on every tile but the last of a batch so one refresh shows them together |
| Windows | every `WriteFile` to a HID collection must be `HIDP_CAPS::OutputReportByteLength` bytes; reads are `InputReportByteLength`; open non-exclusively (`FILE_SHARE_READ \| FILE_SHARE_WRITE`) with `FILE_FLAG_OVERLAPPED` |

## Decisions

These choices are settled for this plan. Implementation follows them rather than reopening them.

### 1. A service plugin, not a hidden widget

The dashboard model creates plugin objects only for placed widgets and for providers a widget asked for. Logicon
must run with no tile in Release, so the factory gains a third capability and a third creatable root:

- `RedXePluginCapabilityService = 1U << 2U` in `Factory.h`.
- `Common/PlugInterfaces/Service.h` declares `IRedXeService` (direct `IUnknown` child) and its records.
  `Widget.h` stays widget-only and `Data.h` data-only, as `Plugins_API.md` requires.
- `PluginHost` owns one `ServiceSlot` per catalogued service (`kRedXeBundledServices` in `BundledPlugins.h`,
  first entry `{"builtin.logicon", L"Logicon.dll"}`). It creates the service through `RedXeCreate(IID_IRedXeService,
  …)` with the compact `{"plugin":{},"instance":<effective-settings>}` envelope, exactly like a provider.
- Lifetime, all on the UI thread: `Start(const RedXeServiceStartContext*)` after the first successful settings
  apply and before the first dashboard page is staged; `ApplySettings(json, bytes)` on a live reload whose service
  object changed; `OnHostState(const RedXeHostState*)` when page index/count/name or raise state changes;
  `Stop()` during shutdown after every `PluginManager` is destroyed and before providers are released. `Start` and
  `Stop` are idempotent. `--self-test` and HostPluginTests construct services but MUST NOT open a device unless the
  test injects a fake transport through the test contract.
- One service object per catalogued plugin ID per process. A service that fails `Start` logs once
  (`service-start-failed`) and stays created so `ApplySettings` can retry; it is never a fatal document.

### 2. Plugin-owned HID transport on a host-owned device lane

| Option | Idle cost | Hang isolation | Complexity | Verdict |
| --- | --- | --- | --- | --- |
| Plugin-owned thread | 0 | none; violates "sources MUST NOT create threads" | low | rejected: the host must own every wake-up and drain |
| Broker process (AV pattern) | +1 process, ~4 MiB, mapping + 3 events | full (job kill) | high | fallback only; HID overlapped I/O is cancellable with `CancelIoEx`, unlike MF driver calls |
| **Host-owned device lane** | 0 (event-blocked) | bounded by `CancelIoEx` + 3 s drain budget | medium | **selected** |

`IRedXeDeviceWorker` is a sibling of `IRedXeService` on the same object:

```text
HRESULT RunDeviceWork(HANDLE stopEvent, HANDLE wakeEvent) noexcept;   // device lane only
```

- The host creates one `std::jthread` per started service that exposes the interface (bounded at 4 process-wide,
  `ERROR_TOO_MANY_NAMES` beyond that) and initializes it as an MTA apartment so WIC and `CM_Register_Notification`
  are legal there. It is created lazily on `Start` and joined on `Stop`.
- Inside the call the plugin owns discovery, `CreateFileW`, overlapped `ReadFile`/`WriteFile`, `HidD_*`/`HidP_*`,
  and `CM_Register_Notification`. It blocks only in `WaitForMultipleObjects` on `{stopEvent, wakeEvent, read
  overlapped event, write overlapped event}`. It MUST NOT poll, sleep-loop, touch Direct3D, wait on the UI thread,
  create a thread, or re-enter the host except through `RequestHostAction`, `RequestFrame`, and `Log`.
- Bounds owned by the host and checked by tests: `stopEvent` → the plugin calls `CancelIoEx` and returns within
  500 ms in the fake-device test; the host waits at most 3 s (the existing control-lane budget), logs
  `device-lane-drain-timeout` once, and continues shutdown. Every HID++ command has a 1 s response timeout; every
  write has a 1 s completion timeout; a timeout counts as a device failure and reconnects.
- Reconnect is event-driven: `CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE` for `GUID_DEVINTERFACE_HID` signals
  `wakeEvent`; `CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE` on the open handle reports query-remove/remove-complete. A failed
  open retries once each at 1, 2, 4, and 8 s through a waitable timer, then only on the next arrival notification.
  No periodic wake while disconnected.
- This ships the device lane `Core_PerformanceAndResources.md` describes as optional; the closeout replaces "MUST
  NOT ship until…" with the bounds above. `RedXeDataSetFlagDeviceLane` for data sources stays unimplemented.

### 3. Host action queue on `IRedXeHost`

`IRedXeHost` MAY grow. Add:

```text
HRESULT RequestHostAction(const RedXeHostActionRequest* request) noexcept;   // any thread, allocation-free
```

- `RedXeHostActionRequest{sizeBytes, action, argument (int32), target (borrowed UTF-8, copied, ≤ 512 B)}`.
- The host copies the record into a 16-slot ring under an SRW lock, coalesces an identical pending action, posts one
  `WM_APP + 5` (`kHostActionMessage`) to `Application`, and returns `ERROR_BUSY` when the ring is full. Nothing
  runs on the caller's thread.
- `Application` drains the ring outside input and render dispatch and executes on the UI thread:
  `PageNext` / `PagePrevious` → `NavigateToAdjacentPage`; `PageGoTo(pageIdOrIndex)` → a new direct stage-and-promote
  (`StageTransitionPage` + settle, not repeated adjacent hops); `WidgetRaise` / `WidgetDismiss` / `WidgetToggle`
  with target `"<pageId>/<ordinal>"` resolved against the current page → `TryRaiseWidgetAt` / `DismissWidgetRaise`;
  `Launch(target)` → the same validated `ShellExecuteExW` policy Launcher uses (absolute path or URI with an
  alphabetic scheme, never a relative path). Actions during a swipe, raise settle, or settings error are dropped and
  logged at Debug level, never queued for later.
- Media and system keys (`volume-up`, `volume-down`, `mute`, `play-pause`, `next-track`, `previous-track`) are
  plugin-side `SendInput` from the device lane with a closed enum; no arbitrary virtual-key injection is accepted.

### 4. Settings: a `services` root member (version 5.1)

Version 5 has no plugin registry or plugin-private object, and every settings object is per widget instance. A
service needs process-wide configuration, so 5.1 adds one optional additive root member:

```jsonc
"services": {
  "Logicon": {
    "plugin": "builtin.logicon",
    "brightness": 70,
    "restoreLogoOnExit": true,
    "pageButtons": "keyPages",            // or "dashboardPages"
    "keys": [
      { "page": 0, "slot": 0, "action": "page.previous", "label": "Prev", "icon": "ChevronLeft" },
      { "page": 0, "slot": 1, "action": "page.next",     "label": "Next", "icon": "ChevronRight" },
      { "page": 0, "slot": 2, "action": "widget.toggle", "target": "System/0", "label": "Pulse", "icon": "Diagnostic" },
      { "page": 0, "slot": 3, "action": "launch", "target": "C:\\Tools\\Code.exe", "label": "Code", "icon": "png:C:\\Icons\\code.png" },
      { "page": 0, "slot": 4, "action": "keys", "target": "mute", "label": "Mute", "icon": "Mute" },
      { "page": 0, "slot": 7, "face": "clock" },
      { "page": 0, "slot": 8, "face": "pageIndicator" }
    ]
  }
}
```

- `services` is an object with at most 8 members. Each value is a flattened plugin object with the same grammar as
  `declare` (required `plugin`, no nested `settings`, `use` allowed). The plugin MUST be catalogued in
  `kRedXeBundledServices`; a widget-only or unknown plugin ID rejects the complete candidate. Two members naming the
  same service plugin reject the candidate. Omitted `services` starts no service.
- Older RedXe (5.0 readers) ignore the additive member; this build reads 5.0 and 5.1.
- The Logicon object obeys the 4096-byte compact cap and the host validator subset (`Plugins_API.md` static settings
  contract): `keys` is one flat array of at most 36 closed objects (`page` 0–3, `slot` 0–8, `action` enum, `target`
  string ≤ 512 B, `label` ≤ 16 code points, `icon` ≤ 260 B, `color` `#RRGGBB`, `face` enum). It is flat rather than
  `keyPages[].keys[]` because the validator forbids nested arrays. Duplicate `(page, slot)` pairs, `target` semantics
  (absolute path / URI / `<pageId>/<ordinal>` / media key name) and icon references are validated by the plugin at
  `ApplySettings` and reported through `Log` plus a red "invalid" face on that key; they never reject the document.
- Runtime key-page selection is not persisted. Logicon persists nothing in this plan.

### 5. Key faces are CPU-composed and WIC-encoded; Release needs no Direct3D

The keypad wants a JPEG. RedXe already rasterizes glyphs with DirectWrite alpha textures and decodes PNG with WIC
(Launcher). Faces therefore never touch the renderer:

- One bounded compose surface: 434×434 BGRA (753,424 B) reused for every write, plus one 118×118 scratch for a
  single-tile update, one 128 KiB JPEG output buffer, and one 4095-byte report buffer. Allocated on first connect,
  released on disconnect and `Stop`.
- Icon sources: a bounded table of Segoe Fluent Icons glyph names (reusing `RedXe/FluentIcons.h` code points; falls
  back like host chrome), or `png:<absolute path>` decoded once through WIC and scaled to fit 72 px. Labels use the
  UI font, one line, ellipsized. Backgrounds use `color`, defaulting to the dashboard background.
- Dynamic faces: `pageIndicator` (current page name and `n / N`), `clock`, `raised` (highlight when the target
  widget is raised), and, in Phase B2, System Data values (`cpu.percent`, `memory.percent`, `gpu.percent`) through
  `IRedXeHost::GetDataProvider("builtin.system-data")` at ≥ 1000 ms; snapshots are copied on the acquisition worker
  and signal `wakeEvent`, never rendered there.
- Every face has a 64-bit signature (settings + dynamic values). The lane re-encodes only faces whose signature
  changed; when three or more faces changed it writes the 434×434 composite once, otherwise each tile with `defer
  update` set until the last. JPEG quality 80, 4:4:4, and the encoder's subsampling option are measured in Phase 0.
- Clock faces tick at most once per minute via a waitable timer on the lane, only while connected and only when a
  clock face is on the current key page.

### 6. The Debug tile is compiled and catalogued only in `_DEBUG`

- `kRedXeBundledWidgets` gains `{"builtin.logicon-monitor", "logicon-monitor"}` inside `#if defined(_DEBUG)`, listed
  in a new `kRedXeDebugOnlyBundledWidgetIds`. `SettingsTests::CoversBundledPluginCatalog` requires it in
  `RedXe-debug.settings.json` and its absence from `RedXe.settings.json`. A Release document that places it is an
  unknown plugin ID and stays a fatal document with the usual diagnostic, which is the intended "no display in
  Release".
- The tile is a plain Direct3D widget like the System Data viewers (`IRedXeGpuWidget`, `IRedXeInteractiveWidget`,
  `IRedXeScheduledWidget`, DirectWrite atlas, quads), not DxUi: it needs nine tappable cells, two page-button cells,
  a brightness row, a status line, and a bounded 32-line HID++ trace. DxUi remains available if controls grow.
- It borrows the module's service object (one per DLL) under a lock, uploads the current composite as a texture in
  `Prepare` only when the composite generation changed, and forwards taps as simulated control events through the
  test contract (`RedXeLogiconInjectControl`, `RedXeLogiconSetBrightness`, `RedXeLogiconGetDiagnostics`). A tap on a
  key cell runs that key's binding exactly as the hardware would.

### 7. Coexistence with Logi Options+

Windows lets several processes open one HID collection; input reports reach every handle, but two writers repaint
the panel against each other. Logicon detects a competing writer heuristically (a `logioptionsplus_agent.exe`
process, or a `0x19A1` ack for a software ID other than `0x0B`) and reports `service-degraded` with a reason the
Debug tile shows. It never terminates another process and never claims exclusive access. Setup guidance lives in
`docs/plugins/logicon.md` (quit Options+ or remove the keypad from its profile).

### 8. Leave the device as found

On connect Logicon reads and stores the `0x1B04` reporting flags of every control it diverts (page buttons on the
keypad, all controls on the dialpad) and restores them on `Stop`, disconnect, and `ApplySettings` that drops a
binding. `restoreLogoOnExit` (default `true`) sends the `0x03` feature reset on `Stop` so the panel returns to the
splash rather than a stale dashboard face.

## Architecture

```text
Common/PlugInterfaces/
  Factory.h          RedXePluginCapabilityService
  Service.h          IRedXeService, IRedXeDeviceWorker, RedXeServiceStartContext, RedXeHostState (sizeof/offsetof pinned)
  Host.h             RequestHostAction, RedXeHostActionRequest, RedXeHostAction enum
RedXe/
  BundledPlugins.h   kRedXeBundledServices, kRedXeDebugOnlyBundledWidgetIds, catalog validation
  PluginHost.*       ServiceSlot[], device lane threads, action ring, host-state fan-out
  Application.*      kHostActionMessage drain, NavigateToPage, raise-by-ordinal, launch policy, host-state publish
  Settings.*         services root, 5.1 minor, service plugin validation
Plugins/Logicon/
  Logicon.cpp             factory, metadata, settings contract, service + (Debug) provider COM objects
  LogiconHid.{h,cpp}      collection discovery, open, overlapped read/write, hotplug notifications, padding
  LogiconHidpp.{h,cpp}    HID++ 2.0 framing, root lookup, 0x1B04, 0x8040, error reports, command/response
  LogiconVlp.{h,cpp}      0x19A1 image packets, key event parsing
  LogiconFaces.{h,cpp}    glyph/PNG compose, signatures, WIC JPEG
  LogiconBindings.{h,cpp} settings model, key pages, actions, host-state feedback
  LogiconMonitor.{h,cpp}  Debug tile (_DEBUG only), shaders
  LogiconTestContract.h   REDXE_LOGICON_TEST_API exports, fake transport injection
  Probe/LogiconProbe.exe  Phase 0 console tool (not shipped)
Tests/LogiconTests/       fake HID++ device, framing vectors, state machines, drain timing, JPEG round trip
Specs/Plugins/Plugins_Logicon.md
docs/plugins/logicon.md
```

`Logicon.dll` imports only `hid.dll`, `cfgmgr32.dll`, `windowscodecs.dll`, `dwrite.dll`, and, in Debug, `d3d11`.
No vcpkg runtime DLL is copied beside it. The project carries all four configurations on x64 and ARM64 and a
build-only project reference from RedXe, like every bundled DLL.

## Workstreams

### Phase 0 — Discovery and measurement (hardware required)

Build `Plugins/Logicon/Probe/LogiconProbe.exe` (console, links the transport and protocol sources, no host):

- enumerate every HID collection of `046D:C354`, print usage page/usage, report lengths, and which collection carries
  `0x11`/`0x13`/`0x14`;
- dump the feature table through `IFeatureSet` and confirm the `0x19A1`, `0x1B04`, `0x8040` indexes;
- divert the page buttons, print raw `0x11`/`0x13` reports for every control, confirm the `0x19A1` event layout;
- write a solid face, a glyph face, and the 434×434 composite; time each 4095-byte report and the full sequence,
  with and without the 10 ms pause, with and without `defer update`;
- repeat with Options+ running to document the conflict;
- enumerate the dialpad over Bluetooth and, if a Bolt receiver is present, over the receiver; dump its feature
  table and raw reports while turning the dial, the roller, and pressing each button.

Deliverable: `.build/receipts/2026-09-XX-logicon-probe.md` with the numbers, plus test vectors copied into
`LogiconTests`. The budgets below are provisional until this receipt exists.

### Phase A — Host mechanisms (no hardware)

1. `Service.h`, `Factory.h`, `Host.h` records and interfaces with `sizeof`/`offsetof` pins; `Plugins_API.md`
   interface table rows.
2. `PluginHost`: service slots, create/start/apply/stop ordering, device lane thread with MTA init, drain budget,
   action ring, host-state fan-out. `--self-test` starts services with device access disabled.
3. `Application`: `kHostActionMessage`, `NavigateToPage`, raise-by-ordinal, launch policy shared with Launcher.
4. `Settings`: `services` root, 5.1 minor, catalog validation, both templates (Debug places the monitor tile and a
   Logicon service; Release places the service only), schema file, `Core_Settings.md`.
5. Tests: HostPluginTests service lifetime with a stub service (a test-only `RedXeCreate` in
   `PluginContractTests`), action-ring bounds and coalescing, drain-timeout logging; SettingsTests for the new root.

### Phase B — Logicon service (Release path)

1. Transport and protocol from Phase 0 with the fake device tests.
2. Faces: glyph table, PNG decode, compose, signature cache, WIC encode, composite-vs-tile policy.
3. Bindings: settings model, key pages, page-button policy, actions through `RequestHostAction`, media keys.
4. Host-state feedback: `pageIndicator`, `raised`, `clock` faces.
5. B2: System Data faces through a provider subscription from the service (sink on the acquisition worker,
   copy + wake only).
6. Docs page and usage note.

### Phase C — Debug monitor tile

Catalog class, provider `#if defined(_DEBUG)`, atlas + quads, taps, brightness row, trace, WARP smoke in
HostPluginTests, Debug template placement, screenshot under `docs/screenshots/` only if the tile is documented.

### Phase D — Dialpad (gated on the Phase 0 capture)

Transport over Bluetooth HID and over a Bolt receiver (HID++ device index ≠ `0xFF`), `0x1B04` diversion of its
four buttons, dial and roller decoding (candidates: `0x2121` hi-res wheel, `0x2150` thumbwheel, or a `0x19A1`
sibling; decided by the capture), dial acceleration, actions `dial.volume`, `dial.page`, `dial.raisedScroll`, and
Debug tile rows. Until the capture exists this phase is `DECISION` inside an `ACTIVE` plan.

### Phase E — Closeout

Merge durable requirements into `Plugins_API.md`, `Core_PerformanceAndResources.md`, `Core_Settings.md`,
`UI_Dashboard.md`, and `Plugins_Logicon.md`; update `docs/`; move this file to `Done/` and remove its index row.

## Performance and resource budget (provisional until Phase 0)

| Item | Budget |
| --- | --- |
| Idle CPU | 0: the lane blocks in one `WaitForMultipleObjects`; CM callbacks only set an event |
| Threads | +1 per started service (Logicon: 1). No thread while the service is stopped |
| Private bytes, connected | ≤ 1.5 MiB plugin-owned (compose 736 KiB, JPEG 128 KiB, faces/signatures, report buffers) plus the WIC image mapping |
| Private bytes, disconnected | ≤ 64 KiB; surfaces are released |
| Key down → host action posted | ≤ 5 ms after the input report arrives |
| Face change → last packet written | ≤ 150 ms per tile, ≤ 1 s for the composite (measure) |
| HID++ command / write timeout | 1 s each, then reconnect |
| `Stop` → lane exit | 500 ms typical, 3 s hard budget with one log line |
| Action ring | 16 slots, coalesced, `ERROR_BUSY` when full |
| Settings object | ≤ 4096 B compact; ≤ 36 keys |

## Validation

- `LogiconTests` (no hardware): byte-exact `0x14` framing against the reference packets for a 1-byte, 4074-byte,
  4075-byte, and 20,000-byte payload; VLP byte sequence; Windows padding to `OutputReportByteLength`; root lookup
  and error-report handling; `0x1B04` save/divert/restore; `0x19A1` and `0x11` key state machines including
  simultaneous keys and release; reconnect on `ERROR_DEVICE_NOT_CONNECTED`; stop-drain under 500 ms; face signature
  stability; JPEG encode → WIC decode round trip at 118×118 and 434×434; composite-vs-tile policy; settings model
  bounds and invalid-target reporting.
- `HostPluginTests`: service create/start/apply/stop ordering, device lane limit and drain timeout log, action ring
  bounds/coalescing/`ERROR_BUSY`, host-state fan-out, Debug monitor WARP render and tap forwarding, `--self-test`
  never opens a device.
- `SettingsTests`: `services` grammar, 5.1 minor acceptance, unknown/duplicate service plugin rejection, catalog
  coverage per configuration.
- Hardware checklist (recorded as a receipt, not in `docs/`): plug/unplug while running, sleep/resume, Options+
  running then quit, key pages, every action, brightness, `restoreLogoOnExit`, ARM64 build.
- `./format.ps1`, `./test.ps1 -Configuration Debug -Platform x64 -Rebuild`,
  `./test.ps1 -Configuration Release -Platform x64 -Rebuild`, an ARM64 build, `./validate-skills.ps1`.

## Exit criteria

1. Release RedXe with the keypad plugged in starts the service, paints the configured faces, and navigates pages
   from the keys with no tile placed; unplugging and replugging recovers without restart; exit restores the splash.
2. Debug RedXe shows the monitor tile with live state, and a tap on a key cell performs the same action as the
   hardware key.
3. Idle CPU stays at zero with the keypad connected; the budget table is measured and recorded.
4. All required validation passes on x64 Debug and Release and ARM64 builds.
5. Every durable requirement is in the owning specs, `docs/` is updated, and this plan is moved to `Done/`.

## Open decisions

| Item | Default until decided |
| --- | --- |
| Dialpad transport and dial/roller feature | Phase D waits for the Phase 0 capture |
| Composite JPEG quality / subsampling | 80 and 4:4:4; measured against panel appearance and transfer time |
| Whether `services` may also host future non-device services (for example a scheduler) | The grammar is generic; only Logicon is catalogued |
| Physical page buttons when `pageButtons` is `dashboardPages` | Adjacent navigation only; no named-page jump from the hardware buttons |
