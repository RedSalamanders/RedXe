# Logicon service and monitor

Status: current normative product contract
Last reviewed: 2026-09-17
Owner: `Plugins/Logicon`, `Tests/LogiconTests`, the `services` root of `../Core/Core_Settings.md`

The service ABI, device lane, and developer-only widget class are owned by [`Plugins_API.md`](Plugins_API.md); the
binding shape, the action catalog, publication, and the host action ring by
[`Plugins_Actions.md`](Plugins_Actions.md). Resource bounds remain owned by
[`../Core/Core_PerformanceAndResources.md`](../Core/Core_PerformanceAndResources.md); settings storage and merging
by [`../Core/Core_Settings.md`](../Core/Core_Settings.md).

## What Logicon does

`builtin.logicon` is a headless service that drives the Logitech MX Creative Console **keypad** (USB, VID `0x046D`,
PID `0xC354`): it paints the nine LCD key faces, sets panel brightness, reads the nine keys and the two page buttons,
and turns presses into named actions ([`Plugins_Actions.md`](Plugins_Actions.md)). It also drives the MX Creative
**Dialpad** (Bluetooth LE, PID `0xBC00`): its bound buttons through HID++ diversion and its dial and roller through
Raw Input, each turned into one action per detent. The service publishes the `logicon` action namespace (key pages
and brightness) and executes it on its own lane. Release builds run it without any
tile. Debug builds also ship `builtin.logicon-monitor`, the developer tile that shows and drives the same service.

## Settings (service object)

| Member | Default | Contract |
| --- | --- | --- |
| `brightness` | `70` | Integer 1–100 sent through HID++ `0x8040` at connect and whenever it changes. 0 is never sent (it resets the device). |
| `restoreLogoOnExit` | `true` | On stop or removal the service restores the page buttons' original reporting flags; when true it also sends the feature-report reset so the panel shows the Logi splash. |
| `pageButtons` | `keyPages` | `keyPages`: the physical `<`/`>` buttons cycle Logicon key pages. `dashboardPages`: they request `page.previous`/`page.next`. |
| `keys` | `[]` | Flat array of at most 36 bindings. |
| `dialpad` | `{"turns":[],"buttons":[]}` | Closed object. `turns` is an array of at most four closed objects with required `control` (`dial` or `roller`) and `direction` (`cw` / `ccw` for the dial, `up` / `down` for the roller; a mismatch rejects the document) plus the binding core `action`/`target`; two entries for one `(control, direction)` reject the document, and a direction without an entry does nothing. `buttons` is an array of at most four closed objects with a required `button` 0–3 (Back, Forward, Button 6, Button 7) plus `action`/`target` only (no face, label, icon, or color). Two entries for one button reject the document. |

Each binding is a closed object: `page` 0–3 (default 0), required `slot` 0–8 (reading order, top-left first),
`action`, `target` (≤ 512 bytes), `label` (≤ 16 code points, one ellipsized line), `icon` (≤ 260 bytes), `color`
(`#RRGGBB` background), and `face`. Two bindings for the same `(page, slot)` reject the document. The key-page count is
the highest bound `page` plus one; runtime key-page selection is never persisted.

`action` is `none` or any action name (`Plugins_Actions.md`): a default namespace (`page.*`, `widget.*`, `redxe.*`,
`system.*`, `keys.*`, `mouse.*`) or a registered published one (`logicon.*`, `zoom.*`); `target` is that action's
argument (at most 512 bytes). The document parser rejects a name outside the grammar, an unregistered namespace, or an
unknown default verb. Because control dispatch is press-only, it also rejects `keys.down` and `mouse.down` for every
key, dialpad button, or turn binding; other actions may use the host's bounded hold timer. The service validates every
binding through `IRedXeHost::ValidateAction` at create, `Start`, and
`ApplySettings` and keeps the result, so a target that does not satisfy its action, an unknown published verb, or an
unavailable publisher is accepted, drawn as a red `!` face, and never dispatched. Valid bindings are dispatched from
the lane through `IRedXeHost::RequestAction`, except the service's own namespace, which runs on the lane without a
host round trip. The service injects no input itself: `keys.media` and every other injecting action are host-native
and are counted, not performed, while device access is disabled (`--self-test`, host tests).

Published namespace `logicon` (`RedXeGetActionContract` for `builtin.logicon`, sibling `IRedXeActionPack` on the
service object, every action `Deferred`): `logicon.keyPage.next` and `logicon.keyPage.previous` (no target),
`logicon.keyPage.goto` (integer 0–3), and `logicon.brightness` (delta 1–100: `n`, `+n`, or `-n`, applied within 1–100
until the next settings apply). `Execute` copies the request into one of four pending slots, wakes the lane, and
returns `S_FALSE`; a full set returns `ERROR_BUSY`.

Faces: `none` draws the `icon` (a Segoe Fluent Icons glyph name such as `ChevronRight`, `Home`, `Play`, `Mute`, or
`png:<absolute path>` decoded through WIC and fitted to 72 px) above the `label`; an unknown glyph name draws the
`Help` glyph. `clock` draws the local `HH:MM` and refreshes on the minute only while a clock face is on the current
key page. `pageIndicator` draws `n/N` for the dashboard page and uses the page name as its label when the binding has
none. `cpu`, `memory`, and `gpu` draw a rounded percentage (`37%`, or `--` while unknown) with `CPU`, `MEM`, or
`GPU` as the label when the binding has none: `cpu.summary/totalPercent`, `memory.summary` used over total physical
bytes, and `gpu.adapter/utilizationPercent` of the first discrete hardware adapter (else any hardware adapter, else
the first row) from `builtin.system-data`. Bindings whose target names the currently raised widget, or `page.goto`
whose target is the current page, draw an accent ring. A blank slot shows the dashboard background. Icons and labels
are near-white on the binding `color` or the dashboard background.

Dialpad: one detent of the dial or the roller (120 raw units; a remainder carries to the next packet) runs the
`turns` binding of that control and direction once per detent, exactly like a key press. A bound button runs its
binding exactly like a key on press; an entry with no
`action` only silences the button. Only bound buttons are diverted; an unbound button keeps its native mouse or
keyboard meaning. The dial and the roller are not diverted: the desktop still receives them as wheels.

## Device protocol (keypad)

Verified against the reference implementations named in the active plan. Byte offsets count the report id.

- Collections: every present HID collection of `046D:C354` on usage page `0xFF43` is opened non-exclusively with
  overlapped I/O. Output reports route by report id to the collection whose descriptor lists that id, else to the
  collection with the widest output report; every write is padded to that collection's `OutputReportByteLength`.
- HID++ 2.0 long report `0x11` (20 bytes): `[11][FF][feature index][function << 4 | 0x0B][params…]`. An error reply
  is `[11][FF][FF][feature][function|swid][code]`. Commands are serialized with a 1 s response timeout; unrelated
  input arriving while a command waits is still dispatched.
- Connect resolves feature indexes through root `getFeature`: `0x19A1` (contextual display, required), `0x1B04`
  (reprogrammable controls, required), `0x8040` (brightness, optional). Observed indexes `0x02`, `0x0B`, `0x0F` are
  test vectors only. Connect reads the page buttons' (`0x01A1`, `0x01A2`) reporting through `0x1B04` function 2,
  stores it, and diverts them with flags `| 0x03` through function 3; stop or disconnect restores the stored flags.
- Input: LCD keys arrive on `0x13` as `[13][FF][display feature][00][?][01][pressed key ids 1–9…][0]`; the mask of
  held keys replaces the previous one. Page buttons arrive on `0x11` as `[11][FF][reprog feature][00][cid][cid]…`
  (u16 big-endian, up to four). A press edge is a bit newly set relative to the previous report, accumulated per
  report so a tap whose press and release are drained together still dispatches once. Acks (software id `0x0B`) are
  never events.
- Panel: 480×480; key `slot` `s` is the 118×118 rectangle at `(23 + (s % 3)·158, 6 + (s / 3)·158)`; the 434×434
  rectangle at `(23, 6)` covers all nine keys and the gaps.
- Image stream: JPEG (baseline, quality 80, 4:4:4) through `0x14` reports of 4095 bytes. First report:
  `[14][FF][display feature][2B][seq][01 display][defer][01 image][00 JPEG][x u16][y u16][w u16][h u16][len u24]`
  then data from byte 20; continuations repeat bytes 0–3, carry the sequence byte, and continue data at byte 5. The
  sequence byte is `0x20 | (1-based index & 0x0F)` with `0x80` on the first and `0x40` on the last report. Faces are
  cached by a signature over their inputs; a change touching three or more slots (or a reconnect) writes the 434×434
  composite once, otherwise each changed tile is written with `defer` set until the last. After a batch the lane
  pauses 10 ms before the next write, as the references do. Brightness is `0x8040` function 2 with a big-endian u16.
- Splash reset: feature report `0x03` `[03][02][00…]` (32 bytes) on the collection that carries it.
- Hotplug: `CM_Register_Notification` on `GUID_DEVINTERFACE_HID` counts the change and signals the lane's wake
  event; the lane re-enumerates whatever is not open only for wakes that carry a change (settings and host-state
  wakes do not enumerate). A device-gone error (`ERROR_DEVICE_NOT_CONNECTED` and friends) on read or write detaches
  and waits for the next arrival. A failed open or connect retries after 1, 2, 4, and 8 s, then only on arrival.

## Device protocol (dialpad)

Captured on 2026-09-17 from a dialpad paired over Bluetooth LE (firmware `REV 0016`); a Bolt-receiver pairing (a
receiver child with a HID++ device index other than `0xFF`) is not driven.

- Collections: `046D:BC00` exposes a mouse collection (`Col01`, usage page `0x01` usage `0x02`, one 8-byte input
  report id `0x02`) that Windows opens exclusively, and one vendor collection (`Col02`, page `0xFF43` usage `0x0202`,
  20-byte `0x11` in and out). Only the vendor collection is opened; the same framing, command timeout, and tracing as
  the keypad apply.
- Feature set: 29 features; `0x1B04` at `0x0A` (v6) and `0x4610` at `0x0D` (v1, undocumented; its functions answer
  `02` / `28 00 1F 00` / zeros, and a `setMode`-style write of `0x29` is rejected). The root answers index 0 for
  `0x19A1`, `0x8040`, `0x2121`, `0x2150`, and `0x2110`. Connect resolves `0x1B04` only.
- Buttons: the four `0x1B04` controls are `0x0053` (Back), `0x0056` (Forward), `0x0059` ("Button 6"), `0x005A`
  ("Left Scroll As Button 7"), all flags `0x31`, group 1. Connect stores the reporting of every button the settings
  bind and diverts those with `| 0x03`; they then arrive as `[11][FF][0A][00][cid][cid]…` and are folded into a
  four-bit dial-button mask with the same press-edge rule as the page buttons. Stop or disconnect restores the
  stored flags; a settings apply that binds a different set restores and reconnects at once.
- Dial and roller: no HID++ event arrives for either while turning them (75 s capture with every control diverted),
  so they are read as the mouse collection's wheels through Raw Input: only while the dialpad is connected, the lane
  creates one hidden top-level window (class `RedXe.Logicon.RawInput`) on the lane thread and registers
  `usage page 1 / usage 2` with `RIDEV_INPUTSINK` if no process user already owns that registration. It waits with
  `MsgWaitForMultipleObjectsEx`, dispatches at most 256 messages per turn, and folds every `RIM_TYPEMOUSE` packet whose device name carries
  `VID&02046d_PID&bc00` (Bluetooth) or `VID_046D&PID_BC00` (USB) — matched case-insensitively and cached per
  `hDevice`, forgotten on every hotplug change. `RI_MOUSE_HWHEEL` is taken as the dial and `RI_MOUSE_WHEEL` as the
  roller, each summed in raw HID units (120 per detent on a classic wheel) with an event count; the monitor prints
  the raw source next to each so a wrong assignment is visible. Raw mouse buttons and X/Y motion are counted for
  diagnostics only. The window is destroyed and its sink unregistered on dialpad disconnect; a later process owner
  is never unregistered. Raw Input does
  not divert: the desktop still receives the wheel (`0x4610`, the only candidate for diverting it, stays undecoded by
  decision).
- The dialpad never blocks the keypad: discovery, backoff, and disconnects are tracked per device, and either may be
  present alone.
- Logi Options+ coexistence: the lane never terminates another process or claims exclusive access. When
  `logioptionsplus_agent.exe`, `logioptionsplus.exe`, or `logipluginservice.exe` is running at connect, the service
  logs `service-degraded` once and the monitor shows the warning; the documented remedy is to quit Options+ or remove
  the keypad from its profile.

## Host integration

- The lane requests faces only while a keypad is connected or a monitor tile is attached; without either it keeps no
  surfaces. Host state (`IRedXeService::OnHostState`) drives the `pageIndicator` and accent faces and arrives on
  every page, raise, visibility, or settings change.
- System faces: while the settings bind at least one `cpu`, `memory`, or `gpu` face the service holds one
  `IRedXeDataProvider` for `builtin.system-data` with three subscriptions (`cpu.summary`, `memory.summary`,
  `gpu.adapter`, 1000 ms) created and activated on the UI thread in `Start` or `ApplySettings`; a document without
  such faces pauses them (values kept), `Stop` releases them (draining callbacks) before the service can be
  released. The sink runs on the acquisition worker, reduces the snapshot to three rounded integers under its own
  lock, and wakes the lane only when one changed; the lane recomposes only the faces whose value changed. A missing
  provider or data set logs `system-data-unavailable` once and the faces show `--`.
- Key presses, button presses, and detents call `IRedXeHost::RequestAction` from the lane; the host executes on the
  UI thread. A `page.*` or `widget.*` request during a swipe or raise settle is refused by the host, not queued;
  `logicon.*` requests are executed on the lane itself.
- Diagnostics (`IRedXeHost::Log`, never per report): `lane-started`, `lane-stopped`, `device-connected` (feature
  indexes and collection count), `device-disconnected`, `connect-failed` (once per distinct failure until success),
  `dialpad-connected` (its `0x1B04` index, collection count, whether the wheels are read), `dialpad-disconnected`,
  `dialpad-connect-failed` (same once rule), `rawinput-unavailable`, `system-data-unavailable`, `faces-unavailable`, `hotplug-unavailable`,
  `face-write-failed`, `input-read-failed`, `settings-rejected`, `service-degraded`.

## Monitor tile (Debug builds)

`builtin.logicon-monitor` (`Plugins/Logicon/LogiconMonitor.cpp`) is a Direct3D, prepared, interactive tile with a
closed empty settings object. It shows the nine key faces exactly as composed for the keypad (the 434×434 surface as
one texture), a highlight around every held key, red and blue outlines for invalid and overridden slots, the slot
number and action name of every key, both page buttons with their held state, the connection (`usb`, `synthetic`,
`disconnected`), device access, feature indexes, port count, key page, brightness, faces written, in/out report
counts, image count, HID++ errors, the last host state, the last action, the Options+ warning or last failure, a
dialpad section (connection and `0x1B04` index or the last failure, the dial and the roller as detents with their
raw sum, event count, and raw-input source, and the four buttons as chips lit while held), and the newest HID++
frames of both devices merged by tick (`k`/`d` prefixes) as many as fit, at most eight. Text and chips size from the
tile: 18 px text on a 22 px line and 30 px chips at the descriptor's 420 px, scaled up to 1.6× for taller tiles and
down to 0.9× when the status column is narrower than 400 px; key labels sit on a dark pill so faces never hide
them. It redraws only when the service publishes a changed snapshot (`RequestFrame`), uploads the face texture in
`Prepare` when the face generation changed, and allocates nothing in `Render`.

Taps: a key cell in `Press` mode injects a press on pointer down and the release on pointer up, running that key's
binding exactly as the hardware would; `Color` mode pushes the next palette color to the key; `Picture` mode pushes a
generated gradient test picture; `Clear` mode restores the configured face; page-button and dialpad-button cells
inject their control; `Synthetic` toggles the in-memory keypad so the whole pipeline runs without hardware; `Bright
-`/`Bright +` change brightness by ten until the next settings apply. Overrides and the brightness override are
cleared by a new settings object. Release builds keep the tile catalogued but refuse the instance (host placeholder).

## Required validation

- `LogiconTests` (no hardware): byte-exact `0x11` framing for `getFeature(0x19A1)`, `setCidReporting(0x01A1, 0x03)`,
  and `setBrightness(70)` against the reference writes; error, ack, key, and page-button parsing; key geometry;
  `VlpPacketCount` for 0, 1, 4075, 4076, and 20000 bytes; the first-packet header and every continuation of a
  20000-byte stream; defer and out-of-panel rejection; the settings model's defaults, authored values, key pages,
  action names (`none` clearing, grammar), `dialpad.turns` (control/direction pairing, duplicates), and rejections
  including 37 keys and the former `launch`/`keys`/`dial`/`roller` names; the face renderer's compose, ellipsis, accent ring, invalid
  face, JPEG markers, WIC round trips at 118×118 and 434×434, and strided sub-rectangle encoding; the device session
  over the synthetic keypad (feature resolution, diversion, brightness, image reassembly, press edges across one
  drain, restore, splash reset); the dialpad (the captured button event and its mask, raw-input name matching in
  both spellings, wheel and button folding, listener start/stop, and the session over the synthetic dialpad:
  `0x1B04` at `0x0A`, four diversions, button edges apart from page buttons, restore); and the shipped DLL's
  metadata, contract, monitor provider (constructs in Debug, refuses in Release), service creation and rejection,
  identity, lane start and drain, host state, synthetic connect, faces, actions from injected and raw presses reaching the fake host's `RequestAction`,
  bindings validated through the fake host's `ValidateAction`, key pages and brightness through the published
  `logicon.*` actions executed locally, a dialpad button binding and dial/roller `turns` bindings, System Data faces through a fake provider (lookup only once a face is bound,
  three one-second subscriptions, a pushed value reaching the face, pause without faces, release on stop),
  overrides, brightness, settings apply, and release.
- `HostPluginTests`: the action ring, the `logicon` contract, and the service lifetime (`Plugins_API.md` items 20
  and 21). `SettingsTests`: the
  `services` grammar and both templates. `--self-test` starts the service with device access disabled and renders the
  Debug `logicon` page under WARP.
- Hardware validation (a receipt under `.build/receipts/`, never `docs/`): plug and unplug while running, sleep and
  resume, Options+ running then quit, every action, key pages, brightness, `restoreLogoOnExit`, and the measured
  per-report write time that decides whether the composite path stays the default.
