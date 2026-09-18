# Zoom service and the `zoom` action namespace

Status: current normative product contract
Last reviewed: 2026-09-18
Owner: `Plugins/Actions/Zoom` (`zoom.action.dll`), `Tests/ZoomTests`, `ThirdParty/ZoomPluginSdk`, the `services` root
of `../Core/Core_Settings.md`

The service ABI and device lane are owned by [`Plugins_API.md`](Plugins_API.md); the binding shape, publication
ABI, registry, and host runtime by [`Plugins_Actions.md`](Plugins_Actions.md). The user page is
[`docs/plugins/zoom.md`](../../docs/plugins/zoom.md).

## What the service does

`builtin.zoom` is a headless service (`RedXePluginCapabilityService | RedXePluginCapabilityActions`, `IRedXeService`
plus `IRedXeDeviceWorker` plus `IRedXeActionPack` on one controlling `IUnknown`) that drives the installed **Zoom
Workplace** client and publishes the `zoom` namespace. It has two paths to the client, chosen per request by `mode`
(below): the **Zoom Plugin SDK for Windows** — a proxy library that runs inside `RedXe.exe` and talks to the client
over IPC, needing a user OAuth token for a Marketplace app — and the **local path** — the client's own meeting
toolbar read and pressed through the accessibility objects (MSAA, `IAccessible`) Zoom exposes for screen readers —
which needs nothing from Zoom's cloud and is what a managed account that refuses the app falls back to. RedXe never
bundles a meeting client and never runs inside Zoom; the service injects no keystroke (the only injected input in
this module is the `Alt` tap of `BringToForeground` for `zoom.focus`). It has no tile. It is catalogued in
`kRedXeBundledPlugins` (`zoom.action.dll`), `kRedXeBundledServices`, and `kRedXeBundledActionNamespaces` (`zoom` →
`builtin.zoom`). The DLL is the first dedicated action DLL (`Plugins/Actions/<Namespace>/` →
`<namespace>.action.dll`). `zoom.action.dll` additionally imports `oleacc.dll` and `oleaut32.dll` for the local path.

## Settings (service object)

Closed object, published through `RedXeGetPluginSettingsContract`; the model (`ZoomSettings.cpp`) is compiled into
the host and the DLL.

| Member | Default | Contract |
| --- | --- | --- |
| `clientId` | `""` (defaults validate; the model requires it non-empty unless `mode` is `local`) | The Marketplace **General app** client id, 1–128 bytes. No client secret is ever authored, stored, or sent. |
| `redirectPort` | `48123` | Loopback port of the PKCE redirect URL `http://127.0.0.1:<port>/redirect`, registered verbatim in the Marketplace app; 1024–65535. |
| `domain` | `"zoom.us"` | Sign-in and token host; 1–128 bytes. |
| `displayName` | `""` | Name used when joining; ≤ 128 bytes; empty uses the Zoom profile name. |
| `autoConnect` | `false` | `true` connects the session at `Start` when a credential exists; `false` connects at the first `zoom.*` execution. |
| `mode` | `auto` | `auto`: the SDK when it can serve, else the local path; `sdk`: the SDK only (signed-out requests fail); `local`: the local path only, and `clientId` is not required. |
| `labels` | English names | Closed object of ten 1–64-byte strings — three state pairs `muted` `currently muted` / `unmuted` `currently unmuted`, `videoOn` `Stop my video` / `videoOff` `Start my video`, `handRaised` `Lower hand` / `handLowered` `Raise hand`, and four buttons `share` `Share,`, `record` `Record`, `leave` `Leave,`, `end` `End,` — matched case-insensitively as substrings of the toolbar button names (Zoom Workplace 7.1.5, English); members left out keep their defaults. |

Both shipped templates configure the service with the RedXe Marketplace app's client id (a public PKCE client id,
never a secret); a build for another Marketplace app replaces it in the settings.

## Credential and sign-in (OAuth PKCE)

- `zoom.signIn` generates a 64-character verifier and a 22-character state (`BCryptGenRandom`), the S256 challenge
  (`BCrypt` SHA-256, base64url), builds
  `https://<domain>/oauth/authorize?response_type=code&client_id=…&redirect_uri=http://127.0.0.1:<port>/redirect&code_challenge=…&code_challenge_method=S256&state=…`,
  and opens it through the host's own `system.launch` (`RequestAction` from the lane); the lane never calls the
  shell. The `LoopbackListener` (one Winsock socket on `127.0.0.1:<port>`, one client at a time, event-driven so the
  lane waits on its event beside `stopEvent` and `wakeEvent` and never blocks in `recv`) accepts the redirect for at
  most `kSignInTimeoutMilliseconds` (5 minutes), ignores a redirect whose `state` differs, answers with a one-line
  HTML page, and hands the code to the lane. A port in use logs `zoom-listener-failed` (remedy: change
  `redirectPort`). One sign-in at a time; a second request while one is pending is ignored.
- The code is exchanged at `https://<domain>/oauth/token` over WinHTTP with the verifier (`ExchangeCode`); a
  response without a refresh token fails the sign-in. Refreshes (`RefreshTokens`) run on the lane when the access
  token is within 60 s of expiry; `invalid_grant` (`E_ACCESSDENIED`) deletes the credential and logs
  `zoom-auth-failed` once.
- **Only the refresh token** is stored, in Windows Credential Manager (`CredWriteW`, `CRED_TYPE_GENERIC`, target
  `RedXe/Zoom/<clientId>`, `CRED_PERSIST_LOCAL_MACHINE`, per Windows user). Access tokens live in lane memory only.
  Nothing token-shaped reaches the settings file or the log. `zoom.signOut` (`now`) deletes the credential, cancels a
  pending sign-in, and disconnects. Every settings apply re-reads the stored credential once (`_credentialChecked`)
  so a credential added while signed out is picked up without a restart.

## Lane and session

`RunDeviceWork` (host device lane, MTA) owns the session, the tokens, and the listener; the UI thread only parses
settings and queues requests.

- The session is `IZoomSession` (`ZoomSession.h`): `Initialize`/`Uninitialize`, `Authenticate(domain, accessToken)`,
  `Join`, `Start`, `Leave(endForAll)`, `SetAudioJoined`, `SetMuted`, `SetVideo`, `Share`, `Record`, `SetHandRaised`,
  `React`, `SendChat`, `SetCaptions`, `MuteAll`, `AdmitAll`, and a `Snapshot()` of `auth`, `ipc`, `meeting`,
  `audioJoined`, `audioMuted`, `videoOn`, `sharing`, `handRaised`, `recording`, `isHost`, submission and completion
  counts, and the last error. Callbacks (`ISessionListener::OnSessionChanged`) arrive on the SDK's or the synthetic
  session's thread, copy bounded fields under a lock, and wake the lane; nothing blocks inside a callback.
- Two implementations: `ZoomSdkSession.cpp` over the SDK (compiled only with `ZOOM_PLUGIN_SDK_AVAILABLE`; pinned
  against package 7.1.0.2020's `export_h` and demo) and `ZoomSynthetic.cpp`, the in-memory client tests and the
  automated host drive. The SDK session delay-loads `zToolSuiteIPCProxy.dll` on the lane with
  `__HrLoadAllImportsForDll`; a delay-load hook (`__pfnDliNotifyHook2`) maps it from `<Plugins>\ZoomSdk\` with
  `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` so the package's dependency set resolves there and never shadows the process
  CRT, and a deployment without that directory logs `zoom-sdk-unavailable` once instead of failing the module map.
  `InitZMToolSuite` creates the proxy's message window on the lane thread and the proxy delivers listener callbacks
  and completions through it, so the lane waits with `MsgWaitForMultipleObjectsEx` (`QS_ALLINPUT`) and drains its
  queue (at most 256 messages per wake) after every wake; the synthetic session posts nothing. The auth context
  domain is `https://<domain>`; `StartMeeting` joins audio and leaves video off; `StartMonitorShare` takes the
  `MONITORINFOEXW` device name of the 1-based monitor (0 = primary) and `StartAppShare` the `HWND`; the hand is
  `SendFeedbackReaction(Hand)` / `(Clear)`; pause/resume map onto `PauseResumeShareToggle` against the paused
  state the callbacks report; `zoom.captions` enables the meeting's closed-caption feature and starts live
  transcription; recording pause/resume follow whichever of local/cloud is active. Completions capture a weak
  reference to the session state so a late completion after `Uninitialize` touches nothing. Without the SDK import
  `SdkSessionAvailable()` is false, `CreateSdkSession()` returns null, and every `zoom.*` action is refused after the
  one `zoom-sdk-unavailable` warning.
- Connection: authenticate with a fresh access token, wait for authenticated + IPC connected. A dropped IPC
  connection logs `zoom-disconnected` and reconnects after 1, 2, 4, and 8 s (`kReconnectBackoffSteps`), then only on
  the next request. `Stop` uninitializes the session, closes the listener, and returns within
  `kRedXeDeviceWorkerDrainMilliseconds`.
- Requests: `Execute` (UI thread) copies the request into one of `kMaximumPendingRequests` (4) slots, wakes the lane,
  and returns `S_FALSE`; a full set returns `ERROR_BUSY`. A request that arrives before the session is connected
  becomes the single deferred request and waits `kDeferredRequestMilliseconds` (15 s) for the connection (a newer
  one replaces it); without a credential every action but `zoom.signIn` fails `E_NOT_VALID_STATE` and logs
  `zoom-signed-out` once. A refused verb logs `zoom-action-failed` (Debug) and counts `requestsFailed`.
- `RedXeServiceFlagDeviceAccessDisabled`: no SDK initialization, no IPC, no browser, no listener unless the
  synthetic session is selected through the test export; the local path finds no window (unless the test
  override names one) and refuses, so nothing is pressed.

## Routing and the local path

Every request except `zoom.signIn`, `zoom.signOut`, and `zoom.focus` (which fronts the meeting window, else the
client window, in every mode) is routed on the lane (`ChooseRoute`):

| `mode` | Session authenticated and connected | Otherwise |
| --- | --- | --- |
| `local` | local path | local path |
| `sdk` | SDK | no credential: refused (`zoom-signed-out` once); else the deferred wait (15 s), then refused |
| `auto` | SDK | no SDK session in the build, the account refused the app (`AUTH_RESULT_*` failure, until the settings change or a new sign-in), or no credential: local path at once; a credential still connecting: the deferred wait, then the local path |

The credential check is one `CredReadW` on first use after every settings apply, so a signed-out user's first press
never waits. The first request through the local path logs `zoom-local-mode` (Info, once per settings apply) with
the reason.

The local path (`ZoomLocal.h/.cpp`) for one verb:

1. `zoom.join` and `zoom.start` with a meeting reference request `system.launch` of
   `zoommtg://zoom.us/join?confno=<number>[&pwd=<passcode>]` through the host (a passcode outside the URI unreserved
   set is `E_INVALIDARG`); `zoom.start` without a number is `E_NOTIMPL`.
2. The verb's button: `mute` the `muted`/`unmuted` button, `video` `videoOn`/`videoOff`, `raiseHand`
   `handRaised`/`handLowered`, `share` start (`monitor`, `app`) the `share` button (Zoom's own picker opens),
   `record` `local.start`/`cloud.start` the `record` button, `leave` the `leave` button else `end` (a host; Zoom then
   asks whether to end or leave). `audio`, `reaction`, `chat.send`, `captions`, `end`, `participants.*`, share
   stop/pause/resume, and record stop/pause/resume have no local path and fail `E_NOTIMPL`.
3. The meeting window: the visible top-level `ConfMultiTabContentWndClass` (Zoom Workplace 7.x), else
   `ZPContentViewWndClass`, else `ZPFloatVideoWndClass`, through the shared window selector; none means not in a
   meeting, `E_NOT_VALID_STATE`, nothing pressed.
4. The toolbar (`ReadToolbar`): `AccessibleObjectFromWindow(OBJID_CLIENT)` on the window's `ZPControlPanelClass`
   child (the window itself when there is none), walked depth-first through `AccessibleChildren` at most 8 levels
   and 512 nodes deep, collecting at most 64 visible push and split buttons (`ROLE_SYSTEM_PUSHBUTTON`,
   `ROLE_SYSTEM_SPLITBUTTON`, not `STATE_SYSTEM_INVISIBLE`) with their names and accessible objects. Zoom does not
   expose these controls through UI Automation (its tree stops at the panes), which is why MSAA is used directly.
5. `ReadState` matches every button name against the `labels` pairs (case-insensitive substrings; the "yes" label
   wins when both match); the read counts `stateReads` and updates the published meeting, muted, video, and hand
   state. For `on` / `off` a state already equal to the request returns `S_OK` without a press; a state neither
   label matched fails `E_NOT_VALID_STATE` and logs `zoom-state-unknown` once per settings apply. `toggle` never
   checks.
6. `Press` performs `IAccessible::accDoDefaultAction` on the matched button — what a click does, delivered by Zoom
   itself — and the expected state is recorded until the next read; a button the labels do not match fails
   `ERROR_NOT_FOUND` (logged once as `zoom-state-unknown`). On an automated host without the test override nothing
   is pressed.

While the local path is active the published session state carries the meeting, muted, video, and hand values it
last read or expected; the session's own connection state and counters are untouched.

## Actions

All `zoom.*` actions are `Deferred`. `on` / `off` use the snapshot the session reports; `toggle` flips it. Actions
that need a meeting fail `E_NOT_VALID_STATE` unless the meeting state is in-meeting; host-only actions (`zoom.end`,
`zoom.participants.*`) fail `E_ACCESSDENIED` when `isHost` is false.

| Action | Target | Session call |
| --- | --- | --- |
| `zoom.signIn` | None | Sign-in flow above. |
| `zoom.signOut` | Enum `now`, destructive | Delete credential, disconnect. |
| `zoom.join` | Meeting (URL or `<id>[:<passcode>]`) | `Join(number, passcode, displayName)`. |
| `zoom.start` | Meeting, optional (PMI or empty) | `Start(number or 0)`. |
| `zoom.leave`, `zoom.end` | Enum `now`, destructive | `Leave(false)` / `Leave(true)`. |
| `zoom.audio` | Enum `join` / `leave` | `SetAudioJoined`. |
| `zoom.mute`, `zoom.video`, `zoom.raiseHand` | Enum `on` / `off` / `toggle` | `SetMuted`, `SetVideo`, `SetHandRaised`. |
| `zoom.share` | Enum `monitor`, `app`, `pause`, `resume`, `stop`, with `MonitorSuffix` (`monitor@<monitor>`) and `WindowSuffix` (`app@<window>`) | `Share(command, monitorIndex, HWND)`. |
| `zoom.record` | Enum `local.start`, `local.stop`, `cloud.start`, `cloud.stop`, `pause`, `resume` | `Record`. |
| `zoom.reaction` | Enum `thumbsUp`, `clap`, `heart`, `joy`, `openMouth`, `tada` (`kReactionOptions`) | `React`. |
| `zoom.chat.send` | Text | `SendChat` to everyone. |
| `zoom.captions` | Enum `on` / `off` | `SetCaptions`. |
| `zoom.participants.muteAll`, `zoom.participants.admitAll` | None | `MuteAll`, `AdmitAll`. |
| `zoom.focus` | None (`InjectsInput`) | Meeting window (else `exe:Zoom.exe`) + `BringToForeground`; `ERROR_NOT_FOUND` without a Zoom window; needs no session. |

## Build and deployment

- The SDK (`zoom-plugin-sdk-windows-7.1.0.2020`, x64; Zoom publishes no ARM64 package) is licensed by Zoom, downloaded
  from the App Marketplace by each developer, and never committed. `ThirdParty/ZoomPluginSdk/Import-ZoomSdk.ps1`
  verifies the package layout and pinned version, copies `x64\export_h`, `x64\lib\zToolSuiteIPCProxy.lib`, and
  `x64\bin\*` (minus archives), and writes `ZoomSdk.props` (defines `ZOOM_PLUGIN_SDK_AVAILABLE` and `WIN32`, adds
  the include and library directories, links the proxy delay-loaded, and copies the `bin` runtime to
  `<Plugins>\ZoomSdk\` after the build — never beside `RedXe.exe`, because the package carries its own
  `vcruntime140`, `msvcp140`, and `ucrtbase` copies).
  `Zoom.vcxproj` imports the props only on x64 and only when they exist; everything else under the directory is
  git-ignored.
- `ZoomSdkSession.cpp` is pinned against package 7.1.0.2020 (`ToolSuiteProxyInterface.h`'s `IZMToolSuiteProxyListener`
  with all 33 callbacks, `ToolSuiteProxyDef.h`, and the premeeting/meeting/audio/video/share/recording/reaction/
  chat/closed-caption/waiting-room/participants toolkits). Importing another package version needs `-Force` and a
  review of the adapter against its `export_h`.
- Without the import, `zoom.action.dll` builds on every platform (x64 and ARM64) with the synthetic session only;
  it still publishes the contract and the settings contract, so both templates parse everywhere and the WARP
  self-test starts the service.
- `zoom.action.dll` imports `bcrypt.dll`, `winhttp.dll`, `ws2_32.dll`, `advapi32.dll`, and `yyjson.dll` (copied
  beside it by `Directory.Build.targets`).

## Test contract

Declared in `ZoomTestContract.h` behind `REDXE_ZOOM_TEST_API`, present in Release, never called by the host:
`RedXeZoomGetTestDiagnostics` (`RedXeZoomTestDiagnostics`, 196 bytes: lane, device access, SDK availability,
synthetic, credential, sign-in, session states and flags, request counters, session starts, reconnects, sign-ins,
listener port, local mode, local requests, state reads, last failure, last action), `RedXeZoomUseSyntheticSession`,
`RedXeZoomSeedCredential`, `RedXeZoomSyntheticSetHost`, `RedXeZoomSyntheticDropConnection`,
`RedXeZoomSyntheticCounters`, `RedXeZoomStoredCredential`, and `RedXeZoomSetMeetingWindow` (the window the local
path treats as the meeting window; null restores discovery). The synthetic session, the in-memory credential store
and token transport, and the meeting-window override are selected only through these exports, never by a setting.

## Diagnostics (`IRedXeHost::Log`, never per request)

`lane-started`, `lane-stopped`, `zoom-sdk-unavailable`, `zoom-signed-out`, `zoom-sign-in-started`,
`zoom-sign-in-complete`, `zoom-sign-in-failed`, `zoom-listener-failed`, `zoom-auth-failed`, `zoom-connected`,
`zoom-disconnected`, `zoom-session-failed`, `zoom-local-mode`, `zoom-state-unknown`, `zoom-action-failed`.

## Required validation

- `ZoomTests` (no Zoom, no network, no SDK): the settings model (defaults alone rejected for the missing client id,
  authored values, a privileged port, a `clientSecret` member, an empty client id, `mode` values with `local`
  needing no client id, a partial `labels` override keeping the other defaults, and rejection of an unknown mode,
  an unknown or empty label, and an unknown member); the local path (which targets need a state read, the join URI
  and its refusals, and `ReadToolbar`/`ReadState`/`FindButton`/`Press` against a window of Win32 push buttons on
  its own pumping thread: the English 7.1.5 names, their opposites, unmatched names leaving a state unknown, a
  language override, and a default action that clicks the button); auth material (distinct
  base64url verifiers, the RFC 7636 challenge vector, base64url encoding, the authorize URL, token-response parsing
  including `invalid_grant` → `E_ACCESSDENIED` and a response without an access token, the memory credential
  store's `S_FALSE`/`S_OK`/idempotent delete); the loopback listener (ephemeral port, nothing pending, a wrong-state
  redirect answered 400 and ignored, the matching redirect delivered and the listener stopped); and the shipped
  module through the synthetic session and a fixed token transport: metadata and
  contracts, service creation with a valid and an invalid envelope, signed-out refusal of every action but
  `zoom.signIn`, the sign-in flow through a delivered redirect, sign-out cancelling a pending sign-in, a seeded
  credential connecting at the next action and running the deferred request, every in-meeting action against the
  synthetic client, role refusal of `zoom.end` and `zoom.participants.muteAll`, the local section (signed-out
  `auto` routing to the local path and refusing without a window, `sdk` refusing signed-out with
  `zoom-signed-out`, then `mode` `local` against the meeting-window override: a satisfied `on` pressing nothing,
  `off` pressing the mute button, a toggle pressing without a state check, `join` launching the `zoommtg` URI, a
  reaction refused `E_NOTIMPL`, an unreadable state refusing `on`/`off` and logging once, `leave` pressing End for
  a host, no window refusing, the session untouched, and `auto` returning to the connected session), reconnect with backoff after a dropped
  connection, join by meeting reference, sign-out deleting the credential, and stop within the drain budget.
- `HostPluginTests`: the `zoom` contract registers from the shipped DLL without a notice (`Plugins_Actions.md`).
  `SettingsTests`: both templates' Zoom objects. `--self-test` starts the service with device access disabled.
- Live validation against Zoom Workplace with the SDK imported is manual-only and MUST NOT be a CI pass condition:
  `ZoomTests.exe --live --client <marketplace client id> [--join <meeting>]` starts the shipped DLL with device
  access enabled, opens the browser for the OAuth consent when no credential is stored, starts (or joins) a meeting
  once authenticated and connected, drives mute, video, hand, reaction, chat, monitor share with pause/resume/stop,
  focus, and leave with the reported state after each step, and stops the lane; its output is the receipt under
  `.build/receipts/`. `ZoomTests.exe --live --local` validates the local path against the running client without
  any Marketplace app: it waits for a meeting the user starts, then drives mute on/off/toggle, video, hand, and
  focus through the toolbar's accessible objects, printing the state read before each step (passed on 2026-09-18
  against Zoom Workplace 7.1.5, 9 of 9 steps).
