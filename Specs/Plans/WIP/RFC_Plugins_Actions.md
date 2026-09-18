# Actions: normalized bindings, plugin-published namespaces, and the Zoom publisher

Status: ACTIVE — phase 1 (core and Zoom) is implemented and its durable behavior lives in the normative contracts;
phases 2–4 remain. Every decision D1–D10 was taken as proposed except the D9 platform note (see "Phase 1 outcome").
Date: 2026-09-17 (decided and phase 1 landed the same day)
Requested deliverables: one normalized `action` / `target` binding shape shared by Logicon and Launcher; a complete
action catalog by namespace; a publication ABI through which any plugin DLL (widget provider, service, or a
dedicated `<namespace>.action.dll`) publishes namespaces and actions, with namespace collisions shown to the end
user; a hardcoded set of default namespaces in the host; and `zoom.action.dll` as the first action DLL, built on the
Zoom Plugin SDK for Windows
Proposed identity: `Common/PlugInterfaces/Action.h` (ABI), `Common/Actions/` (shared target grammars),
`RedXe/HostActions.*` (default namespaces), `Plugins/Actions/<Namespace>/` → `<namespace>.action.dll`,
`Plugins/Actions/Zoom/` → `zoom.action.dll` publishing service `builtin.zoom` and namespace `zoom`

## Purpose and authority

Today a Logicon key runs one of eleven actions that the host or the service knows by name (`page.next`, `launch`,
`keys`, …) and a Launcher shortcut can only launch its `target`. The host executes dashboard actions from a fixed enum
(`RedXeHostAction`), Logicon injects media keys itself on its device lane, and Launcher and `Application` each carry
their own copy of the `ShellExecuteExW` policy. Every new action grows the enum, two switch statements, and three
schemas, and code that talks to another application (Zoom, a monitor over DDC/CI) has no place to live.

This RFC proposes:

1. **One binding shape.** Everywhere a user binds something to a physical control or a tile, the same closed members
   apply: `"action": "<namespace>.<verb>"` and `"target": "<string>"`. Presentation members (`label`, `icon`, `color`,
   `face`) stay owner-specific.
2. **Default namespaces in the host.** `page`, `widget`, `redxe`, `system`, `keys`, and `mouse` are hardcoded in
   `RedXe/HostActions.*` — no DLL, no registration. They cover the dashboard, launching, power, and input injection.
3. **Published namespaces.** Any bundled plugin DLL — a widget provider, a service, or a dedicated
   `<namespace>.action.dll` — may publish one or more namespaces through a static contract export and execute them
   through `IRedXeActionPack`. Which plugin owns which namespace is catalogued in `RedXe/BundledPlugins.h`; a module
   that publishes a namespace it does not own, or one nobody registered, is a **collision** that the host shows to the
   end user and refuses.
4. **Zoom first.** `zoom.action.dll` is the first published-namespace DLL. It is a service (`builtin.zoom`) that owns a
   Zoom Plugin SDK session on its device lane and publishes `zoom.*` (join, leave, mute, video, share, record,
   reactions, chat, …) over that session.

Owning contracts changed at implementation:

- [Plugin API](../../Plugins/Plugins_API.md): host services (`RequestAction`, `ExecuteAction`), the action contract
  export, the `RedXePluginCapabilityActions` capability, the static-schema subset, and the deletion of the
  `RedXeHostAction` enum.
- [Logicon](../../Plugins/Plugins_Logicon.md): key, dial, roller, and button bindings; `dialpad.turns`; faces; the
  `logicon` namespace it publishes.
- [Settings](../../Core/Core_Settings.md) and [`Settings.schema.json`](../../Settings.schema.json): the binding shape
  in `services` and in Launcher `shortcuts`, the `builtin.zoom` service object, both templates.
- [Performance and resources](../../Core/Core_PerformanceAndResources.md): the publisher budget below.
- New at implementation: `Specs/Plugins/Plugins_Actions.md` owns the binding shape, the default namespaces, the ABI,
  the registry, and the host runtime; `Specs/Plugins/Plugins_Zoom.md` owns the Zoom service; `docs/actions.md` and
  `docs/plugins/zoom.md` are the user pages.

## Required outcomes

| ID | Requirement | Observable result |
| --- | --- | --- |
| AC-01 | One binding shape. | A Logicon key, a dialpad button, a dial turn, and a Launcher shortcut all accept the same `action` and `target` members with the same meaning. |
| AC-02 | Default namespaces need no DLL. | `page`, `widget`, `redxe`, `system`, `keys`, and `mouse` execute inside `RedXe.exe`; a document that binds only those maps no module for actions. |
| AC-03 | Any plugin DLL can publish. | A widget provider, a service, or a dedicated action DLL publishes namespaces through `RedXeGetActionContract` and executes them through `IRedXeActionPack`; Logicon publishes `logicon`, the Zoom service publishes `zoom`. |
| AC-04 | Validation creates no object. | Settings parse and face composition validate a published action by reading the publisher's static contract (mapping the module image when the document references its namespace, exactly like settings contracts); no provider, service, pack object, thread, or window is created for validation. |
| AC-05 | Execution objects are created on use. | A dedicated action DLL's `IRedXeActionPack` is created the first time one of its actions executes, once per process, and never released before process teardown; a service that publishes actions is the object the host already started. |
| AC-06 | Collisions are visible. | Two publishers of one namespace, a publisher of an unregistered namespace, a registered publisher that does not publish its namespace, or a missing publisher DLL: one Error log line each, one bounded line each in the host's settings-error dialog while the dashboard stays active, and every affected binding drawn as an invalid marker that never dispatches. |
| AC-07 | Bounded execution. | Host-native actions and `Execute` run on the UI thread within the budget below or hand the work to a host-owned lane; between executions a publisher owns nothing beyond what its service contract already allows. |
| AC-08 | Automated hosts never act. | `--self-test`, `HostPluginTests`, and `REDXE_AUTOMATED_HOST=1` count executions; no input is injected, no process starts, no display or power state changes, no IPC to Zoom. |
| AC-09 | Zoom over the Plugin SDK. | `zoom.*` drives the installed Zoom Workplace client through the Zoom Plugin SDK for Windows (IPC proxy), never through injected shortcuts or UI Automation. |

## Binding shape

A **binding** is a closed object owned by a Logicon or Launcher settings array. Its core is the same everywhere:

| Member | Contract |
| --- | --- |
| `action` | String. `none` (default), or `<namespace>.<verb>[.<verb>]` matching `^[a-z][a-zA-Z0-9]*(\.[a-z][a-zA-Z0-9]*){1,3}$`, at most 64 bytes. The name must resolve to a default namespace or to a registered publisher's contract. |
| `target` | String, 0 through 512 bytes (`kRedXeMaximumHostActionTargetBytes`). Its grammar is fixed per action (below). Actions whose target kind is `none` ignore it; every other action treats an unsatisfied target as invalid, never as a document error. |

Owner-specific members stay where they are: Logicon `page`, `slot`, `label`, `icon`, `color`, `face`; dialpad
`button`; the new dialpad `control` and `direction`; Launcher `icon` (see below).

### No compatibility

There is no alias layer. The former names `launch`, `keys`, `keyPage.next`, `keyPage.previous`, the `dialpad.dial` /
`dialpad.roller` presets, and Launcher `iconPng` are unknown names or members and reject the document on the authored
path. Both templates, `docs/`, and every test fixture move to the catalog names in the same change.

### Logicon

`keys[]` and `dialpad.buttons[]` keep their shape. The `dial` and `roller` members are replaced by `turns`, an array
of at most four closed objects: required `control` (`dial` or `roller`), required `direction` (`cw` / `ccw` for the
dial, `up` / `down` for the roller; a mismatch rejects the document), plus the binding core. Two entries for one
`(control, direction)` reject the document; a direction without an entry does nothing. One detent runs the binding
once, exactly like a button press.

```jsonc
"dialpad": {
  "turns": [
    { "control": "dial",   "direction": "cw",   "action": "page.next" },
    { "control": "dial",   "direction": "ccw",  "action": "page.previous" },
    { "control": "roller", "direction": "up",   "action": "screen.brightness", "target": "+10@xeneon" },
    { "control": "roller", "direction": "down", "action": "screen.brightness", "target": "-10@xeneon" }
  ],
  "buttons": [ { "button": 2, "action": "zoom.mute", "target": "toggle" } ]
}
```

### Launcher

Each `shortcuts[]` item becomes: optional `action` (default `system.launch`, which is what a taskbar import writes),
`target` (required unless the action's target kind is `none`), and optional `icon` with Logicon's grammar (a Segoe
Fluent Icons glyph name, or `png:<absolute path>`); `iconPng` is removed. A non-launch action has no file to extract
an icon from and therefore requires `icon`; a glyph is rasterized with DirectWrite into the same 256×256 slice the
shell icon would fill, at extract time, never in `Render`. Taps dispatch through `IRedXeHost::ExecuteAction`
(below); the launch motion plays for every action. The shipped templates keep `{"shortcuts":[]}` so the taskbar
import stays exercised.

```jsonc
"Launcher": { "plugin": "builtin.launcher", "shortcuts": [
  { "target": "C:\\Tools\\Code.exe" },
  { "action": "system.launch", "target": "C:\\Tools\\Code.exe" },
  { "action": "zoom.join", "target": "https://zoom.us/j/1234567890?pwd=abc", "icon": "Video" },
  { "action": "screen.input", "target": "hdmi1@xeneon", "icon": "TVMonitor" },
  { "action": "widget.toggle", "target": "1", "icon": "FullScreen" }
]}
```

## Namespace ownership

The first segment of an action name is its **namespace**. Every namespace has exactly one executor:

| Class | Namespaces | Declared where | Executes where |
| --- | --- | --- | --- |
| Default (hardcoded) | `page`, `widget`, `redxe`, `system`, `keys`, `mouse` | `RedXe/HostActions.h` (`kRedXeDefaultActionNamespaces`, a `constexpr` table of descriptors) | `Application` / `HostActions` on the UI thread |
| Published | `logicon` (Logicon.dll), `zoom` (zoom.action.dll), later `screen`, `window`, `navigate`, `audio`, `clipboard` (`<namespace>.action.dll`) | The publisher's `RedXeGetActionContract`, registered to a plugin id in `kRedXeBundledActionNamespaces` | The publisher's `IRedXeActionPack::Execute` on the UI thread |

Default namespaces are reserved: a publisher contract that names one is a collision. A publisher may publish several
namespaces (`builtin.zoom` publishes only `zoom`; a future AV Control could publish `av`).

### Registry — `RedXe/BundledPlugins.h`

```cpp
struct RedXeBundledActionNamespaceSpec final
{
    const char* actionNamespace;  // ^[a-z][a-zA-Z0-9]*$
    const char* pluginId;         // a row of kRedXeBundledPlugins
};

inline constexpr std::array kRedXeBundledActionNamespaces{
    RedXeBundledActionNamespaceSpec{"logicon", "builtin.logicon"},
    RedXeBundledActionNamespaceSpec{"zoom", "builtin.zoom"},
};
```

The consteval catalog check adds: every namespace unique, none equal to a default namespace, every plugin id present
in `kRedXeBundledPlugins`. Adding a publisher is one row here plus its `kRedXeBundledPlugins` row (and a
`kRedXeBundledServices` row when it is a service), like every other bundled plugin.

### Collision detection and display

The host reads a module's action contract **whenever it maps that module**, for any reason (a placed widget, a
configured service, a bound namespace) and registers every published namespace:

| Condition | Log (`Error`, once per process) | Dialog line | Effect |
| --- | --- | --- | --- |
| A contract publishes a default namespace, or one registered to a different plugin id | `action-namespace-collision` with both plugin ids and the module path | "Action namespace `X` is published by both `A.dll` and `B.dll`; only `A.dll` is used." | The registered (or host) executor keeps the namespace; the other publication is refused. Bindings keep working. |
| A contract publishes a namespace absent from the registry | `action-namespace-unregistered` | "Action namespace `X` from `B.dll` is not registered and is ignored." | Refused. (No document can bind it: the name never validated.) |
| The registered plugin's contract does not publish its namespace | `action-namespace-missing` | "Action namespace `X` is not provided by `A.dll`; its bindings are disabled." | Every `X.*` binding becomes an invalid marker. |
| The registered plugin's module cannot be mapped or created | `action-publisher-unavailable` | "Actions `X.*` are unavailable: `A.dll` could not be loaded." | Same. |

The dialog is the existing settings-error dialog (`Application::ShowSettingsError`): non-stacking, bounded, "the
current dashboard remains active". Lines are appended to whatever settings errors exist; a later valid settings save
that no longer references the namespace clears its line. While the dialog is up, host actions are dropped per the
existing rule; dismissing it resumes them. The Logicon monitor tile prints the same text in its warning slot.

Because every bundled publisher is compiled from one tree, a collision can only come from a mismatched deployment
(a stale or foreign DLL beside `RedXe.exe`). `HostPluginTests` maps every catalogued module and asserts that no
contract collides, so a build never ships one; the runtime path exists for the deployed case.

## Action catalog

`target` kinds: **none** (ignored), **text** (1–512 bytes), **path|uri** (an absolute Win32 path, a UNC path, or a
URI with an alphabetic scheme of at least two characters — today's launch rule), **path** (absolute path only),
**cmdline** (an absolute executable path, optionally quoted, followed by arguments; run with `CreateProcessW`, never
through a shell), **pageRef** (authored or generated page id, or a zero-based index), **widgetRef** (`<pageId>/<ordinal>`
or `<ordinal>`), **int a–b**, **delta a–b** (`n`, `+n`, or `-n`), **enum**, **chords**, **point**, **monitor**,
**window**, **meeting** (grammars in the next section). A kind followed by `?` is optional. `[@monitor]` and
`[@window]` are optional suffixes appended to the value with `@`. Every kind is a `RedXeActionTargetKind` value in
`Action.h` with its parameters in the descriptor, so the host validates a published action's target without running
publisher code.

Flags: **I** injects input (`SendInput`), **D** completes asynchronously on a host-owned lane (`Execute` returns
`S_FALSE`), **X** destructive (its target must name the confirming argument; there is never a dialog).

### Default namespaces (host)

#### `page.*` and `widget.*`

| Action | Target | Effect |
| --- | --- | --- |
| `page.next`, `page.previous` | none | Slide one page, honoring `wrapPages`. |
| `page.first`, `page.last` | none | Go to the first / last page. |
| `page.goto` | pageRef | Go to that page. |
| `widget.raise` | widgetRef | Raise the widget; a page id other than the current page is dropped. |
| `widget.toggle` | widgetRef | Raise, or dismiss when it is the one raised. |
| `widget.dismiss` | none | Dismiss the raised widget, if any. |
| `widget.next`, `widget.previous` | none | Raise the next / previous widget in authored order, cycling; dismiss-then-raise when one is raised. |
| `widget.command` | `<widgetRef>/<command>` | Deferred (phase 4): forward a named command to a widget that exposes a future `IRedXeCommandTarget`. Name reserved. |

#### `redxe.*`

| Action | Target | Effect |
| --- | --- | --- |
| `redxe.settings.reload` | none | Re-read the settings file as if the watcher fired. |
| `redxe.settings.edit` | none | Open the active settings file with its default handler (`ShellExecuteExW`, `SEE_MASK_FLAG_NO_UI`). |
| `redxe.logs.open` | none | Open the `Logs` sibling directory in Explorer. |
| `redxe.screenshot` | `<png path>[@<pageId>[/<ordinal>]]` | Capture through `Common/WindowCapture.cpp` exactly as `--screenshot` does; the page defaults to the current one. |
| `redxe.quit` | `now` (X) | `CloseMainWindow`. |

#### `system.*`

| Action | Target | Effect and API |
| --- | --- | --- |
| `system.launch` | path\|uri | `ShellExecuteExW`, null verb, `SEE_MASK_FLAG_NO_UI`, `SW_SHOWNORMAL`, working directory = the file's directory for a file path (the one implementation of today's Launcher and `Application::LaunchHostTarget` policy). |
| `system.open` | path\|uri | Same code path as `system.launch`; kept for readability. |
| `system.run` | cmdline | `CreateProcessW` with the executable's directory as working directory, no shell, no window inheritance; the handle is closed at once. |
| `system.lock` | none | `LockWorkStation`. |
| `system.sleep`, `system.hibernate` | `now` (X) | `SetSuspendState(FALSE/TRUE, FALSE, FALSE)`. |
| `system.logoff` | `now` (X) | `ExitWindowsEx(EWX_LOGOFF, …)`. |
| `system.shutdown`, `system.restart` | `now` or int 0–3600 seconds (X) | `InitiateSystemShutdownExW` with that timeout after enabling `SE_SHUTDOWN_NAME`; a delayed shutdown shows the Windows countdown. |
| `system.shutdown.cancel` | none | `AbortSystemShutdownW`. |
| `system.process.close` | window | `WM_CLOSE` to every visible top-level window of the selected process (graceful close, never `TerminateProcess`). |
| `system.power.plan` | enum `balanced`, `highPerformance`, `powerSaver`, or a scheme GUID | `PowerSetActiveScheme`. |
| `system.theme` | enum `light`, `dark`, `toggle` | `AppsUseLightTheme` / `SystemUsesLightTheme` under `HKCU\…\Themes\Personalize` plus `WM_SETTINGCHANGE` `ImmersiveColorSet` (registry-backed; there is no official API). |
| `system.taskManager` | none | `system.launch` of `%SystemRoot%\System32\Taskmgr.exe`. |
| `system.eject` | path (drive root) | `CM_Request_Device_EjectW` for the volume's device instance (D, control lane). |
| `system.notify` | text | Reserved: a Win32 toast needs an AUMID or a tray icon RedXe does not have. |

#### `keys.*` (I)

| Action | Target | Effect |
| --- | --- | --- |
| `keys.press` | chords | Press and release each chord in order (`Ctrl+Shift+Esc`, `Win+D`, `Ctrl+K,Ctrl+S`). |
| `keys.down`, `keys.up` | chords (one chord) | Hold / release a chord so a following press composes with it. A held chord is released after 2 s or at shutdown so nothing sticks. |
| `keys.type` | text | Type Unicode text with `KEYEVENTF_UNICODE`, at most 512 bytes per press, sent in batches of 32 `INPUT`s. |
| `keys.media` | enum `play-pause`, `stop`, `next-track`, `previous-track`, `volume-up`, `volume-down`, `mute` | Media keys, injected by the host instead of the Logicon lane. |
| `keys.lock` | enum `caps`, `num`, `scroll` | Toggle that lock key. |
| `keys.layout` | text (`en-US`, `fr-FR`, or a KLID) | `ActivateKeyboardLayout` on the foreground window's thread via `WM_INPUTLANGCHANGEREQUEST`. |

#### `mouse.*` (I)

| Action | Target | Effect |
| --- | --- | --- |
| `mouse.move` | point | Move the cursor; absolute coordinates use `MOUSEEVENTF_ABSOLUTE \| MOUSEEVENTF_VIRTUALDESK`, relative ones `+dx,+dy`. |
| `mouse.click`, `mouse.doubleClick` | enum `left`, `right`, `middle`, `x1`, `x2` `[@point]` | Click at the cursor or at the point. |
| `mouse.down`, `mouse.up` | same button enum | Hold / release a button (released after 2 s or at shutdown). |
| `mouse.scroll` | delta −100–100, optional `h` prefix (`h+3`) | Vertical or horizontal wheel, `WHEEL_DELTA` per unit. |
| `mouse.drag` | `<point>><point>` | Down at the first point, move, up at the second. |
| `mouse.speed` | int 1–20 | `SystemParametersInfoW(SPI_SETMOUSESPEED)` without persisting to the profile. |

### Published namespaces

#### `logicon.*` — published by `Logicon.dll` (service `builtin.logicon`)

| Action | Target | Effect |
| --- | --- | --- |
| `logicon.keyPage.next`, `logicon.keyPage.previous` | none | Cycle key pages. |
| `logicon.keyPage.goto` | int 0–3 | Select a key page. |
| `logicon.brightness` | delta 1–100 | Keypad brightness, clamped, until the next settings apply. |

`Execute` records the request and wakes the lane (D). Because the namespace is published, a Launcher shortcut may
bind `logicon.keyPage.goto` as legitimately as a key can.

#### `zoom.*` — published by `zoom.action.dll` (service `builtin.zoom`)

See the dedicated section below. Summary table:

| Action | Target | SDK toolkit (package `zoom-plugin-sdk-windows-7.1.0.2020`) |
| --- | --- | --- |
| `zoom.signIn` | none | RedXe-owned OAuth PKCE flow (browser + loopback), then `StartToolSuiteAuth`. |
| `zoom.signOut` | `now` (X) | Delete the stored credential, `UninitZMToolSuite`. |
| `zoom.join` | meeting | `premeeting::GetPreMeetingToolkit()->JoinMeeting(JoinMeetingParam{displayName, meetingNumber, …})`. |
| `zoom.start` | meeting? | Pre-meeting toolkit start (PMI when a number is given, else instant). |
| `zoom.leave` | `now` (X) | `meeting::GetMeetingToolkit(instance)` leave. |
| `zoom.end` | `now` (X) | Meeting toolkit end (host role required; refused otherwise). |
| `zoom.audio` | enum `join`, `leave` | `meeting::GetAudioToolkit()` join/leave computer audio. |
| `zoom.mute` | enum `on`, `off`, `toggle` | Audio toolkit mute/unmute self; `toggle` reads the SDK-reported state. |
| `zoom.video` | enum `on`, `off`, `toggle` | `meeting::GetVideoToolkit()` start/stop self video. |
| `zoom.share` | enum `monitor[:n]`, `app@<window>`, `pause`, `resume`, `stop` | `meeting::GetShareToolkit(instance)`: `CanStartShare` first, then monitor share, `StartAppShare(appID, …)`, pause/resume/stop. |
| `zoom.record` | enum `local.start`, `local.stop`, `cloud.start`, `cloud.stop`, `pause`, `resume` | Recording toolkit. |
| `zoom.raiseHand` | enum `on`, `off`, `toggle` | Participants toolkit, self. |
| `zoom.reaction` | enum (the SDK's reaction set, e.g. `thumbsUp`, `clap`, `heart`, `joy`, `openMouth`, `tada`) | Reaction toolkit. |
| `zoom.chat.send` | text | Chat toolkit, to everyone. |
| `zoom.captions` | enum `on`, `off` | Captions toolkit, self view. |
| `zoom.participants.muteAll`, `zoom.participants.admitAll` | none (host role) | Participants / waiting-room toolkits. |
| `zoom.focus` | none | Front the Zoom meeting window through the shared window selector (I). |

### `screen.*`, `window.*`, `navigate.*`, `audio.*`, `clipboard.*` — later `<namespace>.action.dll` publishers

Unchanged from the catalog agreed earlier in this RFC; each becomes one dedicated action DLL registered in
`kRedXeBundledActionNamespaces` in its phase:

| Namespace | Verbs | Notes |
| --- | --- | --- |
| `screen` (D) | `brightness`, `contrast`, `input`, `off`, `on`, `mode`, `rotate`, `resolution`, `primary`, `hdr`, `capture` | DDC/CI (`SetMonitorBrightness`, `SetVCPFeature 0x60`), `SetDisplayConfig`, `ChangeDisplaySettingsExW`, `DisplayConfigSetDeviceInfo`; `@xeneon` addresses the monitor RedXe sits on, so a console key can switch the XENEON EDGE's input or brightness. |
| `window` | `focus`, `minimize`, `maximize`, `restore`, `close`, `toMonitor`, `snap`, `alwaysOnTop`, `minimizeAll`, `showDesktop`, `switch` | `SetForegroundWindow` after one synthetic `Alt` (never `AttachThreadInput`), `ShowWindowAsync`, `SetWindowPos`. |
| `navigate` | `url`, `folder`, `settings` (`ms-settings:`), `search` (`search-ms:`), `back`/`forward`/`refresh`/`stop`/`home` (`VK_BROWSER_*`), `tab.*`, `desktop.*`, `start`, `taskView`, `actionCenter`, `run` | Shell URIs and injected chords. |
| `audio` | `volume.set`, `volume.up`, `volume.down`, `mute`, `mic.mute`, `mic.volume.set`; `output.set`, `output.next`, `mic.set` blocked on AV Control gate G1 | `IAudioEndpointVolume` on the default endpoint resolved per execution; no `IMMNotificationClient`. |
| `clipboard` | `set`, `clear`, `copy`, `paste`, `cut`, `pasteText`, `history` | `CF_UNICODETEXT`; `GlobalAlloc` is the API-mandated allocation. |

## Shared target grammars (`Common/Actions/ActionTargets.*`)

Pure parsers compiled into the host and every publisher, keyed by `RedXeActionTargetKind`, so validation and
execution agree by construction and the host validates a foreign publisher's target from its descriptor alone.

| Grammar | Syntax | Notes |
| --- | --- | --- |
| **chords** | `chord(,chord)*`, at most 8; `chord` = `(Mod+)*Key`; `Mod` ∈ `Ctrl`, `Shift`, `Alt`, `Win`; `Key` ∈ `A`–`Z`, `0`–`9`, `F1`–`F24`, `Enter`, `Esc`, `Tab`, `Space`, `Backspace`, `Delete`, `Insert`, `Home`, `End`, `PageUp`, `PageDown`, `Left`, `Right`, `Up`, `Down`, `PrintScreen`, `Pause`, `CapsLock`, `NumLock`, `ScrollLock`, `Apps`, `Num0`–`Num9`, `NumAdd`, `NumSub`, `NumMul`, `NumDiv`, `NumDot`, `Plus`, `Minus`, `Comma`, `Period`, `Semicolon`, `Quote`, `Slash`, `Backslash`, `LBracket`, `RBracket`, `Grave`, `VK:<hex>` | Injected as scan codes (`MapVirtualKeyW`, extended flag for the navigation cluster). Case-insensitive names. |
| **point** | `<x>,<y>` (physical pixels of the virtual screen), `+<dx>,+<dy>` (relative), `center`, any of them `@monitor` (monitor-relative) | |
| **monitor** | `primary`, `xeneon` (the monitor hosting RedXe's window), `all`, `<n>` (1-based `EnumDisplayMonitors` order), `name:<substring>` (friendly or device name, case-insensitive) | `all` only where a table says so. |
| **window** | `foreground`, `exe:<image.exe>`, `class:<window class>`, `title:<substring>` | First visible, non-tool top-level window in Z order; `EnumWindows` stops at the first match and allocates nothing. |
| **meeting** | `https://<host>/j/<id>[?pwd=<passcode>]`, `<id>[:<passcode>]` with `id` 9–11 digits | Parsed into `meetingNumber` and passcode for `JoinMeetingParam`. |
| **enum** | one of the descriptor's `\|`-separated options | |
| **delta a–b** | `n`, `+n`, `-n`; relative forms are clamped to `[a, b]` at execution | |

## Publication ABI — `Common/PlugInterfaces/Action.h`

```cpp
#pragma once
#include "Host.h"

inline constexpr uint32_t kRedXeMaximumActionNameBytes = 64;
inline constexpr uint32_t kRedXeMaximumActionNamespaces = 4;   // per plugin id
inline constexpr uint32_t kRedXeMaximumActionsPerNamespace = 64;

// Target grammar of one action; parameters live in RedXeActionDescriptor.
enum RedXeActionTargetKind : uint32_t
{
    RedXeActionTargetNone = 0,
    RedXeActionTargetText, RedXeActionTargetPathOrUri, RedXeActionTargetPath, RedXeActionTargetCommandLine,
    RedXeActionTargetPageRef, RedXeActionTargetWidgetRef, RedXeActionTargetInteger, RedXeActionTargetDelta,
    RedXeActionTargetEnum, RedXeActionTargetChords, RedXeActionTargetPoint, RedXeActionTargetMonitor,
    RedXeActionTargetWindow, RedXeActionTargetMeeting,
};

enum RedXeActionFlags : uint32_t
{
    RedXeActionFlagNone = 0,
    RedXeActionFlagTargetOptional = 1U << 0U,
    RedXeActionFlagInjectsInput = 1U << 1U,
    // Execute returns S_FALSE and completes on a host-owned lane (control lane or the publisher's device lane).
    RedXeActionFlagDeferred = 1U << 2U,
    // The target must carry the confirming argument (`now` or a delay); the host validator enforces it.
    RedXeActionFlagDestructive = 1U << 3U,
    RedXeActionFlagMonitorSuffix = 1U << 4U,   // accepts `@monitor`
    RedXeActionFlagWindowSuffix = 1U << 5U,    // accepts `@window`
};

// One action. Module-owned, immutable while the module is mapped.
struct RedXeActionDescriptor final
{
    uint32_t sizeBytes;
    uint32_t flags;
    const char* name;            // "zoom.mute" — full name including the namespace
    const wchar_t* displayName;
    const wchar_t* targetSyntax; // human-readable, for docs and the monitor tile
    uint32_t targetKind;         // RedXeActionTargetKind
    int32_t targetMinimum;       // Integer / Delta
    int32_t targetMaximum;
    const char* targetOptions;   // Enum: "on|off|toggle"; null otherwise
};
static_assert(sizeof(RedXeActionDescriptor) == 56);
static_assert(offsetof(RedXeActionDescriptor, name) == 8);
static_assert(offsetof(RedXeActionDescriptor, targetOptions) == 48);

struct RedXeActionNamespace final
{
    uint32_t sizeBytes;
    uint32_t actionCount;
    const char* name;                         // "zoom"
    const RedXeActionDescriptor* actions;     // every name starts with "<name>."
};
static_assert(sizeof(RedXeActionNamespace) == 24);

// Static contract for one plugin id. Discovered like RedXeGetPluginSettingsContract: no object is created.
struct RedXeActionContract final
{
    uint32_t sizeBytes;
    uint32_t namespaceCount;
    const RedXeActionNamespace* namespaces;
};
static_assert(sizeof(RedXeActionContract) == 16);

extern "C" REDXE_PLUGIN_API HRESULT __stdcall RedXeGetActionContract(
    const char* pluginId, const RedXeActionContract** contract) noexcept;   // ERROR_NOT_FOUND for a non-publisher

enum RedXeActionRequestFlags : uint32_t
{
    RedXeActionRequestFlagNone = 0,
    // Automated host: validate, count, return S_OK (or S_FALSE + a no-op completion); act on nothing.
    RedXeActionRequestFlagDeviceAccessDisabled = 1U << 0U,
};

// Borrowed request; strings are valid for the duration of Execute only.
struct RedXeActionRequest final
{
    uint32_t sizeBytes;
    uint32_t flags;
    const char* actionUtf8;      // full name, at most kRedXeMaximumActionNameBytes
    const char* targetUtf8;      // null when none; at most kRedXeMaximumHostActionTargetBytes
    const char* sourcePluginId;  // borrowed requester id for logs, or null
};
static_assert(sizeof(RedXeActionRequest) == 32);

// Executor for the namespaces a plugin id publishes.
//   - A dedicated action DLL: created once per process through RedXeCreate(IID_IRedXeActionPack, options, host,
//     pluginId, …) on first execution, envelope {"plugin":{},"instance":{}}.
//   - A service: queried as a sibling of IRedXeService on the started service object (same controlling IUnknown).
//   - A widget provider: created once per process through RedXeCreate(IID_IRedXeActionPack, …) on first execution.
// Execute runs on the RedXe UI thread inside the host-action drain, non-reentrant, within
// kRedXeActionExecuteBudgetMilliseconds; deferred actions return S_FALSE and complete on a host-owned lane. A
// publisher MUST NOT call back into the host from Execute except QueueControlWork, Log, and RequestAction.
interface __declspec(uuid("3C7A9E10-5B2D-4F81-9A6E-0D4C8B2F7E51")) __declspec(novtable) IRedXeActionPack : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Execute(const RedXeActionRequest* request) noexcept = 0;
};

inline constexpr uint32_t kRedXeActionExecuteBudgetMilliseconds = 20;
```

- `Factory.h` gains `RedXePluginCapabilityActions = 1U << 3U` and the `RedXeGetActionContract` export name. A module
  that advertises the capability for a plugin id MUST answer the contract for it; the host validates every
  descriptor (name prefix, grammar, kind, bounds, counts) once per map and refuses a malformed contract with
  `action-contract-invalid` as an `action-publisher-unavailable` case.
- A dedicated action DLL is an ordinary plugin module: `Plugins/Actions/<Namespace>/`, `TargetName`
  `<namespace>.action`, plugin id `builtin.actions.<namespace>` unless it is also a service (then its service id,
  `builtin.zoom`), the existing exports, shared `Common/Actions/*.cpp` compiled in. `build.ps1` copies every action
  DLL beside `RedXe.exe`.
- `RedXe/HostActions.h` declares `kRedXeDefaultActionNamespaces` with the same `RedXeActionNamespace` records for
  `page`, `widget`, `redxe`, `system`, `keys`, `mouse`, so the settings validator, the monitor tile, and
  `docs/actions.md` generation treat default and published actions identically.

## Host runtime

### Host services (`IRedXeHost`, vtable grows)

```cpp
// Asks the host to perform one named action later on the UI thread. Safe from any thread, including a service's
// device lane; allocation-free and never blocking. 16-slot ring, identical pending (action, target) coalesces to
// S_FALSE, a full ring returns ERROR_BUSY, one coalesced WM_APP + 5 post per batch. An unknown or refused name, an
// overlong name or target, a null record, or a mismatched sizeBytes returns E_INVALIDARG / E_POINTER.
virtual HRESULT STDMETHODCALLTYPE RequestAction(const RedXeActionRequest* request) noexcept = 0;

// Performs one named action now. UI thread only, synchronous, non-reentrant; allowed from OnPointer (committed
// activation), OnKey/OnCharacter, and OnDrop; forbidden from device, size, visibility, raise, Render, Prepare, and
// every worker callback. Returns S_OK, S_FALSE (deferred), or the failure. Never queued, never coalesced, never
// dropped for a busy dashboard; a dashboard mutation that cannot run during a swipe returns ERROR_BUSY.
virtual HRESULT STDMETHODCALLTYPE ExecuteAction(const RedXeActionRequest* request) noexcept = 0;
```

`RequestHostAction`, `RedXeHostAction`, and `RedXeHostActionRequest` are deleted in the same change; Logicon and
`HostPluginTests` item 20 move to `RequestAction` (RedXe is pre-production and every consumer is source-coordinated,
so there is no shim).

### Ring, drain, and dispatch

- One 16-slot ring (`PluginHost`), each slot `{char action[65]; char target[513]; uint32_t flags; bool used;}`
  (~9 KiB static).
- `Application::HandleHostAction` (UI thread, outside input and render dispatch) resolves the namespace:
  - default → `HostActions::Execute` (`page`/`widget`/`redxe` through the existing dashboard paths;
    `system`/`keys`/`mouse` through `RedXe/HostActions.cpp`). The **busy drop** (page swipe, raise settle, settings
    error dialog) applies only to `page.*` and `widget.*`.
  - published → `PluginHost::ExecutePublishedAction`: resolve the plugin id from the registry, obtain the executor
    (below), `Execute`. Never dropped for a busy dashboard (decision D2).
- Every executed or dropped action is followed by one host-state publication to started services (unchanged).

### Contracts, executors, and loading

- **Validation** (settings parse, on the UI thread): for each namespace a document binds, the host resolves the
  registry row, binds that plugin's module through the existing `BindModule` (mapping the image if it is not yet
  mapped — the same cost and moment as reading its settings contract), reads `RedXeGetActionContract`, and validates
  names and targets. No object is created. A namespace bound by no document maps nothing.
- **Executor** (first execution, UI thread): a service publisher's executor is `QueryInterface(IID_IRedXeActionPack)`
  on the started service object (a service that is configured but failed to start, or is not configured at all,
  yields `action-publisher-unavailable` and invalid markers for its namespace); a dedicated action DLL or a widget
  provider gets one `RedXeCreate(IID_IRedXeActionPack, …)` per process, retained in its `PublisherSlot`.
- Executors are released in `PluginHost::Shutdown` after `StopServices` and after the control lane has drained and
  suppressed completions, then `RedXePluginShutdown` runs once per module. A module is never unmapped earlier.
- A load, contract, or create failure follows the collision table above; the next settings apply clears the mark so a
  repaired deployment retries once.
- `PluginHost::DeviceAccessEnabled() == false` sets `RedXeActionRequestFlagDeviceAccessDisabled` on every request.

### Execution budget and deferral

- `Execute` and every host-native action MUST return within 20 ms on the UI thread and MUST NOT wait on another
  thread, pump messages, or show UI. **D** actions return `S_FALSE` after handing the work to a host-owned lane: the
  control lane (`QueueControlWork`, 16 slots, three-second budget, coalesced reruns) or, for a service publisher, its
  own device lane through a bounded request slot and `wakeEvent`. A deferred action that must apply every press
  (brightness steps) carries its accumulated delta in the work object and re-reads it in `Run`.
- Input injection batches at most 32 `INPUT`s per `SendInput` call and never sleeps between batches. Held keys or
  buttons are released by the matching `up`, by any later execution after 2 s, or at shutdown.
- Steady state allocates nothing: bounded stack buffers (`std::array<wchar_t, 513>`, 64 `INPUT`s), no strings, no
  vectors. Accepted allocations are those an API mandates (`GlobalAlloc` for the clipboard, `GetDisplayConfigBufferSizes`
  arrays, the SDK's own request objects).
- Between executions a dedicated action DLL owns no thread, timer, window, hook, or COM registration. A service
  publisher owns exactly what `Plugins_API.md`'s service contract already allows it (its device lane and what that
  lane holds).
- Diagnostics (`IRedXeHost::Log`, never per detent): `action-contract-loaded` (Info: plugin id, namespaces, action
  count), the four collision events, `action-failed` (Debug: name, `HRESULT`; a publisher MAY log a Warning once per
  distinct failure), `action-dropped`.

### Automated hosts

With `RedXeActionRequestFlagDeviceAccessDisabled`, an executor validates the target, records the request in its test
contract, and returns `S_OK` (or `S_FALSE` with a no-op completion). `system.launch` therefore stops calling
`ShellExecuteExW` in tests through the same flag Launcher uses today (`REDXE_AUTOMATED_HOST`), Logicon's media keys
stop depending on its own `_deviceAccess` gate, and the Zoom service never initializes the SDK or opens IPC.

## Owner integration

### Logicon

- `LogiconSettings` validates `action` against the default namespaces plus every registered contract through the
  host's `ActionValidator` (compiled into the host beside the shared Logicon model). The red `!` face, the accent
  ring for `widget.raise` / `widget.toggle` / `page.goto`, and the shape of `keys[]` and `dialpad.buttons[]` are
  unchanged. `dialpad.turns[]` is new.
- Logicon.dll advertises `RedXePluginCapabilityActions` for `builtin.logicon`, answers the `logicon` contract, and
  exposes `IRedXeActionPack` as a sibling of `IRedXeService`. `DispatchBinding` forwards every binding through
  `RequestAction` from the lane; the service MAY short-circuit its own namespace on the lane without a host round
  trip. `SendMediaKey` and the injection gate leave the service (`keys.media` is host-native). Dial and roller detents
  dispatch their `turns` binding once per detent with the existing remainder carry.
- The monitor tile prints the action name per slot and the collision/unavailable text in its warning slot.

### Launcher

- Settings model: `action` (default `system.launch`), `target` (required unless the action's target kind is `none`),
  `icon` (Logicon grammar). Duplicate detection compares `(action, target)`.
- An unsatisfied target (relative path, schemeless name, empty target for a launch) no longer rejects the document:
  the tile draws the `Warning` glyph from `FluentIcons.h` in the icon slot, and a tap does nothing (decision D1).
- A committed tap calls `IRedXeHost::ExecuteAction` and keeps the launch motion; a failed result reports
  `RedXeWidgetStatusDegraded` `Launch failed` exactly as today. `Application::LaunchHostTarget` and Launcher's
  private `ShellExecuteExW` block are deleted in favor of `HostActions`.

### Future owners and publishers

Any widget or service that binds user controls reuses the binding core and the host validator; any plugin that can
do something useful on request publishes a namespace instead of a private vocabulary (AV Control profiles as
`av.profile.apply`, Weather as `weather.refresh`).

## Zoom publisher — `zoom.action.dll` on the Zoom Plugin SDK for Windows

Sources: the Zoom Plugin SDK for Windows get-started page
(<https://developers.zoom.us/docs/plugin-sdk/windows/get-started/>) and Zoom's agent skill for it
(<https://github.com/zoom/skills/blob/main/skills/plugin-sdk/windows/SKILL.md>, with `references/lifecycle-and-integration.md`
and `references/package-and-api-map.md`). Everything named below was read from those pages on 2026-09-17; method
names not quoted there are pinned from the package headers (`export_h/`) at implementation, since Zoom states that
the public web docs cover fewer features than the headers.

### What the SDK is

- A **separate native application controls the installed Zoom Workplace client over IPC**. The SDK is a proxy
  library that runs inside our process (`RedXe.exe`) and talks to the client; RedXe never bundles a meeting client and
  never runs inside Zoom.
- Package `zoom-plugin-sdk-windows-7.1.0.2020`, per architecture (`x86`, `x64`): `<arch>/export_h/` headers,
  `<arch>/lib/zToolSuiteIPCProxy.lib`, `<arch>/bin/zToolSuiteIPCProxy.dll` plus its matching runtime dependency set
  ("ship the complete matching `bin/` runtime dependency set beside the executable"), and `<arch>/demo/PSDKTest.sln`
  whose `PSDKTestDlg.cpp` is the reference call order. Native C++, Visual Studio 2019 or later. **No ARM64 package.**
- Requirements: Zoom Workplace **7.0.2 or later** running on the same machine; a Zoom Marketplace **General app with
  Plugin SDK enabled**; a **user OAuth access token** carrying the scope **`plugin_sdk:read:connection_meta`**,
  obtained with the **PKCE** flow for native apps (redirect URL registered in the Marketplace app; `code_challenge =
  BASE64URL(SHA256(code_verifier))`, `code_challenge_method=S256`; exchange at `https://zoom.us/oauth/token`). "Do not
  embed an OAuth client secret in a distributed desktop binary."
- Entry points in `ToolSuiteProxyInterface.h`; enums, models, parameters, and results in `ToolSuiteProxyDef.h`;
  feature interfaces in `ToolSuiteAudioInterface.h`, `ToolSuiteVideoInterface.h`, `ToolSuiteShareInterface.h`
  (`IShareToolkit`), `ToolSuiteChatInterface.h`, `ToolSuiteRecordingInterface.h`, `ToolSuiteReactionInterface.h`,
  `ToolSuiteMeetingInterface.h`.
- Lifecycle (quoted order): `InitZMToolSuite()` → build `ZMToolSuiteProxyAuthContext` (domain, access token) →
  `StartToolSuiteAuth(context)` → `SetToolSuiteProxyListener(this)` with a retained `IZMToolSuiteProxyListener`
  implementing `OnAuthResult` (`AUTH_RESULT_*`), `OnIPCConnectStatusChanged` (`IPC_STATUS_*`), and
  `OnMeetingStatusChanged` (`MEETING_STATUS_*`) → wait for `AUTH_RESULT_SUCCESS` and `IPC_STATUS_CONNECTED` →
  `premeeting::GetPreMeetingToolkit()` for start/join → wait for `MEETING_STATUS_INMEETING` → feature toolkits →
  `UninitZMToolSuite()` and remove the listener at shutdown. Version mismatch surfaces as
  `kZMToolSuiteProxyErrorsVersionIncompatible`.
- Toolkits: `premeeting::GetPreMeetingToolkit()` (start/join, settings, navigation), `meeting::GetMeetingToolkit(instance)`
  (status, properties, leave/end, statistics), `meeting::GetParticipantsToolkit(instance)`, `meeting::GetAudioToolkit()`,
  `meeting::GetVideoToolkit()`, `meeting::GetShareToolkit(instance)` (screen/window/monitor/camera/file/whiteboard;
  `CanStartShare` before sharing; `bool StartAppShare(void* appID, onStartAppShareResult callback)`), recording,
  waiting room, chat, captions, Q&A, and webinar toolkits, `setting::GetSettingToolkit()`. Meeting instances:
  `ZMToolSuiteMeetingInstance_Default`, `_GreenRoom`, `_NewBO`.
- Completion model: a toolkit method returns `bool` for *submission*, then a `std::function` completion carries the
  typed result — "do not treat `true` as completed success"; check `BaseResult.error_code` and the status callbacks.
  Callback-borrowed strings and vectors must be copied before the callback returns; UI work must be marshalled to the
  UI thread; listener callbacks must not block. Check role, policy, and `Can*`/`IsSupport*` before privileged calls.

### RedXe design

`zoom.action.dll` publishes plugin id `builtin.zoom` with capabilities **Service + Actions** and the namespace `zoom`.
It is catalogued in `kRedXeBundledPlugins`, `kRedXeBundledServices`, and `kRedXeBundledActionNamespaces`. It is a
service because the SDK session (IPC connection, retained listener, callbacks) and the sign-in flow need a lane that
blocks on events, settings that hold the Marketplace client id, and `Start`/`ApplySettings`/`Stop` — exactly what
`Plugins_API.md`'s service contract provides. It is the same shape as Logicon with the SDK where Logicon has HID.

**Settings** (`services` root, closed object, published contract):

| Member | Default | Contract |
| --- | --- | --- |
| `clientId` | required | The Marketplace General app's client id (1–128 bytes). No secret is ever authored or stored. |
| `redirectPort` | `48123` | Loopback port of the PKCE redirect URL `http://127.0.0.1:<port>/redirect`, registered verbatim in the Marketplace app (1024–65535). |
| `domain` | `"zoom.us"` | `ZMToolSuiteProxyAuthContext` domain. |
| `displayName` | `""` | `JoinMeetingParam::displayName`; empty uses the client's profile name. |
| `autoConnect` | `false` | `true` starts the SDK session at `Start` when a credential exists; `false` starts it at the first `zoom.*` execution. |

**Credential:** the refresh token, access token, and expiry are stored in Windows Credential Manager
(`CredWriteW`, target `RedXe/Zoom/<clientId>`, per user, `CRED_PERSIST_LOCAL_MACHINE`), never in the settings file
or the log. `zoom.signOut` deletes it.

**Lane** (`IRedXeDeviceWorker::RunDeviceWork`, MTA, blocks on `stopEvent`, `wakeEvent`, and its own events):

1. Delay-loads `zToolSuiteIPCProxy.dll` (`/DELAYLOAD`, resolved with `__HrLoadAllImportsForDll` on the lane) so a
   deployment without the SDK fails with `zoom-sdk-unavailable` instead of failing the module map.
2. Session: `InitZMToolSuite`, listener registration, `StartToolSuiteAuth` with a fresh access token (refreshed over
   WinHTTP with the stored refresh token when expired; a refresh failure logs `zoom-auth-failed` once and leaves the
   service `signed-out`), then waits for `AUTH_RESULT_SUCCESS` and `IPC_STATUS_CONNECTED`. Reconnects on
   `IPC_STATUS_*` disconnect with the 1, 2, 4, 8 s backoff Logicon uses, then only on the next execution.
3. Requests: `Execute` (UI thread) copies the request into one of 4 bounded lane slots, returns `S_FALSE`, and
   signals `wakeEvent`; the lane submits the toolkit call and records the completion outcome. A full slot set returns
   `ERROR_BUSY`.
4. Callbacks arrive on the SDK's thread; the listener copies bounded fields under a lock, updates the snapshot
   (auth, IPC, meeting status, self audio muted, self video on, share active, hand raised, recording), and wakes the
   lane. Nothing blocks inside a callback.
5. Sign-in: `zoom.signIn` opens the authorize URL with `system.launch` semantics and the lane listens on
   `127.0.0.1:<redirectPort>` with one Winsock socket for at most 5 minutes (event-blocked, cancelled by `stopEvent`),
   answers the redirect with a one-line HTML page, exchanges the code with the `code_verifier` over WinHTTP, and stores
   the credential. One sign-in at a time; a second request while one is pending is coalesced.
6. `Stop`: `UninitZMToolSuite`, listener removed, socket closed; the lane returns within
   `kRedXeDeviceWorkerDrainMilliseconds`.

**Actions** map one-to-one to toolkit calls (summary table above). `toggle` variants use the snapshot the SDK's
callbacks maintain (the SDK reports state; nothing is guessed). Host-only actions (`zoom.end`,
`zoom.participants.*`) check the role through the SDK's `Can*` methods and fail with `E_ACCESSDENIED` otherwise.
Actions that need a meeting fail with `E_NOT_VALID_STATE` unless the status is `MEETING_STATUS_INMEETING`;
`zoom.join` / `zoom.start` need `IPC_STATUS_CONNECTED` and `AUTH_RESULT_SUCCESS`; without a credential every action but
`zoom.signIn` fails with `E_NOT_VALID_STATE` and logs `zoom-signed-out` once. The Zoom window is fronted for
`zoom.focus` through the shared window selector (`exe:Zoom.exe`), the only injected input in this publisher.

**State for faces (phase 4 hook):** the snapshot is exposed through a `zoom.status` data set on a future data-source
capability so a Logicon face can show mute/video/hand state with an accent ring; not part of this RFC's first phase.

**Build and deployment:** the SDK is downloaded from the Marketplace by the developer under the Zoom SDK license and
is not committed; `ThirdParty/ZoomPluginSdk/` holds an import script that verifies the package version and hash and
copies `export_h/`, `lib/`, and `bin/` for x64. `Zoom.vcxproj` is x64-only (`Condition` on `Platform`); an ARM64
build does not produce `zoom.action.dll`, so an ARM64 document that configures `builtin.zoom` gets `create-failed` and
the `action-publisher-unavailable` notice. `build.ps1` copies `zToolSuiteIPCProxy.dll` and its runtime set beside
`RedXe.exe`. A missing SDK fails the Zoom project with a message naming the import script, never the rest of the
build (decision D9).

**Automated hosts and tests:** the lane logic sits behind `IZoomSession` (`ZoomSession.h`), implemented once over
the SDK and once synthetically (`ZoomSynthetic.*`, like Logicon's synthetic keypad) so `ZoomTests` drive sign-in,
auth, IPC, meeting status, every action, role refusals, backoff, and stop without Zoom, network, or the SDK DLL.
Device access disabled means the SDK is never initialized.

## Settings, schema, and validation

- `Specs/Settings.schema.json`: `$defs/actionName` (the pattern above, `default: "none"`), `$defs/actionTarget`
  (`maxLength: 512`), `$defs/logiconTurn`; `logiconKey.action`, `logiconDialButton.action`, and the Launcher shortcut
  item reference them; Launcher items gain `action` and `icon` and lose `iconPng`; `logiconDialpad` loses `dial`,
  `roller`, and `$defs/logiconDialAction` and gains `turns` (`maxItems: 4`); `serviceDefinition` gains the
  `builtin.zoom` alternative with `zoomSettings`.
- Plugin static contracts (`Plugins_API.md` subset): the host accepts one more fixed `pattern`,
  `^[a-z][a-zA-Z0-9]*(\.[a-z][a-zA-Z0-9]*){1,3}$`, so a plugin schema can type an action name without enumerating
  every action. Name validity is the host validator's job (default namespaces plus registered contracts).
- Both templates are rewritten to the catalog names and to `turns`. The Debug template configures `builtin.zoom`
  with a placeholder `clientId` and binds at least one action of every class (default, Logicon-published,
  Zoom-published) so `--self-test` exercises validation, the service, and the executor path with device access
  disabled. The Release template keeps its current Logicon bindings under the new names and does not configure Zoom.
- `Core_Settings.md` documents the binding core once and points Logicon, Launcher, and Zoom at it.

## Performance and resource budget

| Item | Bound |
| --- | --- |
| Default namespaces and grammars | `constexpr` tables in the host image, ≤ 16 KiB; no runtime registration. |
| Registry | `constexpr`; contracts are read once per mapped module and validated in place (borrowed, never copied). |
| Ring | 16 slots × ~580 bytes, static. |
| Dedicated action DLL | ≤ 256 KiB image, mapped only when a document binds its namespace; executor created on first use; no idle thread, timer, window, hook, or registration; ≤ 8 KiB private working set. |
| Zoom service | Mapped and started only when `services` configures it. Idle: one lane blocked on events plus whatever `zToolSuiteIPCProxy.dll` holds for its IPC connection (measured in the receipt; the SDK's threads are the SDK's). Requests: 4 fixed slots. No polling anywhere; the SDK's callbacks are the only wake source besides the host events. |
| `Execute` | ≤ 20 ms on the UI thread, allocation-free except API-mandated allocations; longer work through a host-owned lane. |
| Input | ≤ 32 `INPUT`s per `SendInput`, ≤ 8 chords or 512 bytes of text per action. |
| Logging | Never per detent, per frame, or per SDK callback; once per distinct failure. |

Measured receipts (`.build/receipts/`): host-native action wall times; per publisher the map time, private bytes
after create, `Execute` wall time per action, lane time per deferred action; for Zoom the private bytes of the SDK
session idle and in a meeting, the callback rate, and the round trip of `zoom.mute` from key press to the
`OnMeetingStatusChanged`/audio-status callback.

## Safety and security

- Publishers load only from the executable's directory with the plugin loader's search flags; there is no
  user-configured module path and no `LoadLibrary` of a document-named module. A foreign or stale DLL that claims a
  namespace is refused and shown, never executed.
- Destructive actions (`system.shutdown`, `system.restart`, `system.logoff`, `system.sleep`, `system.hibernate`,
  `zoom.leave`, `zoom.end`, `zoom.signOut`, `redxe.quit`) require the confirming target so a binding cannot be created
  without naming the consequence; a missing target is an invalid marker, never dispatched. There is no confirmation
  dialog: a keypad key is a deliberate control.
- `system.run` never goes through `cmd.exe` or a shell; the executable path must be absolute.
- Input injection is subject to UIPI: RedXe is never elevated, so an elevated foreground window silently ignores
  injected input. Documented; not worked around.
- Zoom: no client secret anywhere (PKCE); tokens only in Credential Manager; the loopback listener binds
  `127.0.0.1` only, accepts one request, and closes; the access token never appears in logs or the monitor tile;
  privileged meeting operations are gated by the SDK's role checks.
- `system.theme` writes two documented per-user registry values; no action writes HKLM, changes security settings,
  or elevates. `clipboard.set` writes only document-authored text; no action reads the clipboard.

## Required validation

- `ActionTests` (new, no hardware): name grammar; every default descriptor has a kind, bounds, and flags consistent
  with its table row; every target grammar with accepted and rejected vectors; the host validator over default and
  published contracts; contract validation (bad prefix, over-count, malformed descriptor) refused; collision,
  unregistered, missing, and unavailable classification from synthetic contracts; held-key release after 2 s and at
  shutdown; automated-host counting for every default action.
- `HostPluginTests`: the named ring; first execution creates a dedicated executor and the second does not; a service
  publisher resolves to the started service object; every catalogued module's contract maps and no two collide (the
  build gate); a synthetic colliding module produces one Error line, one dialog line, and a refused namespace;
  `ExecuteAction` synchronous result including `S_FALSE` and its lane completion; published actions execute during a
  busy dashboard while `page.*` is dropped; shutdown order (services, control lane, executors, modules).
- `LogiconTests`: the former names, `dial`, and `roller` reject; `turns` parsing, mismatch, and duplicate rejection;
  a detent runs its turn binding once; the `logicon` contract and `IRedXeActionPack` on the service object;
  `keys.media` no longer injects from the lane; the monitor shows the collision text.
- `LauncherTests`: `action` default, `icon` glyph rasterization off `Render`, invalid-target warning tile and
  no-dispatch, `iconPng` rejected, `ExecuteAction` on tap with a failure reported as `Degraded`, WARP pixel and hit
  tests for the warning tile.
- `ZoomTests` (new, no Zoom, no network, no SDK DLL): settings model and rejections; credential round trip through an
  injected store; PKCE verifier/challenge vectors; the synthetic session through sign-in, auth, IPC, meeting status,
  every action's toolkit submission and completion, role refusals, `E_NOT_VALID_STATE` cases, reconnect backoff,
  request-slot coalescing and `ERROR_BUSY`, stop within the drain budget; the shipped DLL's metadata, contracts, and
  refusal to touch the SDK with device access disabled.
- `SettingsTests`: the new `$defs`, the third fixed pattern, both templates, an unknown action rejected on its
  authored path, an unsatisfied target accepted, the `builtin.zoom` service object.
- `--self-test`: the Debug template's bindings validate and execute with device access disabled and count.
- Hardware and live receipts (never in `docs/`): Zoom Workplace 7.0.2+ with a real Marketplace app — sign-in,
  every `zoom.*` in a meeting, role refusals, client restart and reconnect, the round-trip latency above; later
  phases add each `screen.*` action against the XENEON EDGE and the map-time / private-bytes table.

## Documentation

`docs/actions.md` lists the default namespaces and every published namespace with a table of `action`, `target`
syntax, and what happens, the destructive-target rule, the UIPI note, and the collision notice text.
`docs/plugins/zoom.md` explains the Marketplace app setup (General app, Plugin SDK enabled, redirect URL), the
`services` members, `zoom.signIn`, and the actions. `docs/plugins/logicon.md` and `docs/plugins/launcher.md` describe
their binding members and link to `docs/actions.md`.

## Implementation phases

1. **Core and Zoom** — `Action.h`, target grammars, the host validator, `RedXe/HostActions.*` with the six default
   namespaces, `RequestAction` / `ExecuteAction`, the named ring and drain, the registry and collision display,
   contract reading on map, executors, Logicon (publishes `logicon`, `turns`) and Launcher migration, schema and
   templates, enum deletion, `docs/actions.md`; **`zoom.action.dll`** with the SDK import script, the service, the
   synthetic session, `ZoomTests`, `Plugins_Zoom.md`, and `docs/plugins/zoom.md`.
2. **Display, window, navigation, clipboard** — `screen`, `window`, `navigate`, `clipboard` action DLLs over the
   shared `Common/Actions/` code; helper-process decision for DDC/CI after the receipt.
3. **Audio** — `audio.action.dll`; `audio.output.*` stays blocked on AV G1.
4. **Deferred** — `widget.command` and `IRedXeCommandTarget`; publisher state as data sets (`zoom.status`) so faces
   can show it; `system.notify`; further publishers (`av.*`, `weather.*`).

$1
### Phase 1 outcome (2026-09-17)

Landed and specified in [`Plugins_Actions.md`](../../Plugins/Plugins_Actions.md),
[`Plugins_Zoom.md`](../../Plugins/Plugins_Zoom.md), [`Plugins_API.md`](../../Plugins/Plugins_API.md),
[`Plugins_Logicon.md`](../../Plugins/Plugins_Logicon.md), and [`Core_Settings.md`](../../Core/Core_Settings.md);
user pages `docs/actions.md`, `docs/plugins/zoom.md`, and the updated Logicon and Launcher pages. Debug and Release
x64 `test.ps1` pass (contract, host/plugin, settings, Launcher, Logicon, Zoom, WARP self-test); ARM64 Debug builds.

Deviations from the text above, all recorded in the owning contracts:

- **AC-02 / AC-04, validation of published verbs.** The settings parser checks the grammar, the namespace (default or
  registered), and a default namespace's verb; it does not read a publisher's contract, so an unknown verb in a
  registered published namespace is a runtime invalid marker (`ValidateAction` → `ERROR_NOT_FOUND`), not a document
  error. Reading contracts from the parser would map publisher modules during settings validation on every reload
  path, including the persist path that runs inside a widget callback.
- **`ValidateAction` is a third host service** beside `RequestAction` and `ExecuteAction`, so owners (Logicon,
  Launcher) validate bindings through the host instead of a host-side validator compiled into their DLLs.
- **D9, platform.** `zoom.action.dll` builds on ARM64 too (synthetic session only, like an x64 build without the
  SDK import) so both templates parse and start every catalogued service on every platform; the SDK props are
  imported on x64 only.
- **SDK runtime placement.** The package ships its own CRT copies, so the runtime is deployed to `<Plugins>\ZoomSdk\`
  and mapped through a delay-load hook with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`, not copied beside `RedXe.exe` as
  written above. The lane pumps the proxy's message queue because `InitZMToolSuite` binds its window to the calling
  thread. The adapter was pinned against 7.1.0.2020 on 2026-09-17 (see `Plugins_Zoom.md` and the live receipt).
- **Not published in phase 1:** `system.eject`, `system.notify`, `widget.command`; they wait for phases 3–4.
- **Local path (D11, 2026-09-18).** `Plugins_Zoom.md` "Routing and the local path": `mode`, the route table, the
  MSAA toolbar read and press with the `labels` members, the `zoommtg://` join, and `ZoomTests.exe --live --local`
  as the manual check against the running client without any Marketplace app (passed 9/9 against Zoom Workplace
  7.1.5). The first cut of D11 injected Zoom's keyboard shortcuts with a UI Automation state read; the live meeting
  showed Zoom exposes its toolbar only through MSAA and honours `accDoDefaultAction`, which presses the button
  without a keystroke or a focus change, so the shortcut injection and the `globalShortcuts` member were dropped.

## Decisions (taken 2026-09-17 as proposed; D9 amended per the phase 1 outcome)

| ID | Question | Decision |
| --- | --- | --- |
| D1 | Launcher: an unsatisfied target flags the tile (Logicon's rule) instead of rejecting the document. | Flag. One bad shortcut must not take the dashboard down; drops always produce valid targets anyway. |
| D2 | Published actions execute during a page swipe or raise settle (only `page.*` / `widget.*` are dropped). | Execute. |
| D3 | Which namespaces are hardcoded in the host. | `page`, `widget`, `redxe`, `system`, `keys`, `mouse`: the dashboard plus everything that is plain Win32 with no external dependency and that Launcher or Logicon need on day one. Everything else is published. |
| D4 | Namespace ownership is a compile-time registry (`kRedXeBundledActionNamespaces`) with runtime verification and display, rather than a startup scan of every module's contract. | Registry. A scan would map every bundled DLL at startup, which the lazy module policy forbids; the registry keeps mapping on demand and still surfaces every deployed collision the moment the offending module is mapped, with `HostPluginTests` as the build gate. |
| D5 | The collision notice reuses the settings-error dialog (non-stacking, dashboard stays active) plus invalid markers, rather than a new banner. | Reuse. |
| D6 | Destructive actions need a confirming target and never a dialog. | Yes. |
| D7 | `ExecuteAction` (synchronous UI-thread variant) is added beside `RequestAction` so Launcher keeps its failure feedback. | Add. |
| D8 | Zoom is a service (`builtin.zoom`, `services` root, own lane) that also publishes `zoom`, rather than a stateless action DLL. | Service. The SDK session, callbacks, sign-in listener, and client id need the service contract's lane and settings. |
| D9 | The Zoom SDK package is developer-imported (not committed), x64 only, delay-loaded, with the Zoom project failing alone when it is absent. | Yes; ARM64 ships without `zoom.action.dll`. |
| D10 | OAuth tokens live in Windows Credential Manager; the loopback redirect port is a settings member with default `48123`. | Yes. |
| D11 | When the SDK cannot serve (no credential, the account refuses the app, no SDK in the build), `zoom.*` falls back to the Zoom client's own meeting toolbar, read and pressed through its MSAA accessibility objects; `mode` (`auto`/`sdk`/`local`) and `labels` are settings members. Taken 2026-09-18 after the corporate-account question. | Fallback inside the Zoom service (option a); device-level mute stays phase 3 (`audio.action.dll`). |

## Non-goals

- A user-installed or document-named publisher path; publishers are bundled and catalogued like every other plugin.
- Hold, repeat, or long-press semantics on bindings; owners dispatch on the press edge as today.
- ~~Driving Zoom through injected shortcuts, `zoommtg://`, or UI Automation; the SDK is the only path.~~ Reversed by
  D11 (2026-09-18): the SDK needs a Marketplace app the account must allow, which a managed corporate account
  typically refuses, so the service falls back to the client's own meeting toolbar (read and pressed through its
  MSAA accessibility objects) and the `zoommtg://` join URI.
- Global hotkeys owned by RedXe (`RegisterHotKey`); actions are triggered by bindings, not by the keyboard.
- Terminating processes, editing HKLM, elevation, or anything that bypasses a Windows consent prompt.
