# Zoom service and the `zoom` action namespace

Status: current normative product contract
Last reviewed: 2026-09-17
Owner: `Plugins/Actions/Zoom` (`zoom.action.dll`), `Tests/ZoomTests`, `ThirdParty/ZoomPluginSdk`, the `services` root
of `../Core/Core_Settings.md`

The service ABI and device lane are owned by [`Plugins_API.md`](Plugins_API.md); the binding shape, publication
ABI, registry, and host runtime by [`Plugins_Actions.md`](Plugins_Actions.md). The user page is
[`docs/plugins/zoom.md`](../../docs/plugins/zoom.md).

## What the service does

`builtin.zoom` is a headless service (`RedXePluginCapabilityService | RedXePluginCapabilityActions`, `IRedXeService`
plus `IRedXeDeviceWorker` plus `IRedXeActionPack` on one controlling `IUnknown`) that drives the installed **Zoom
Workplace** client through the **Zoom Plugin SDK for Windows** — a proxy library that runs inside `RedXe.exe` and
talks to the client over IPC — and publishes the `zoom` namespace. RedXe never bundles a meeting client, never runs
inside Zoom, and never drives Zoom through injected shortcuts or UI Automation (the only injected input is the `Alt`
tap of `zoom.focus`). It has no tile. It is catalogued in `kRedXeBundledPlugins` (`zoom.action.dll`),
`kRedXeBundledServices`, and `kRedXeBundledActionNamespaces` (`zoom` → `builtin.zoom`). The DLL is the first
dedicated action DLL (`Plugins/Actions/<Namespace>/` → `<namespace>.action.dll`).

## Settings (service object)

Closed object, published through `RedXeGetPluginSettingsContract`; the model (`ZoomSettings.cpp`) is compiled into
the host and the DLL.

| Member | Default | Contract |
| --- | --- | --- |
| `clientId` | `""` (defaults validate; the model requires it non-empty) | The Marketplace **General app** client id, 1–128 bytes. No client secret is ever authored, stored, or sent. |
| `redirectPort` | `48123` | Loopback port of the PKCE redirect URL `http://127.0.0.1:<port>/redirect`, registered verbatim in the Marketplace app; 1024–65535. |
| `domain` | `"zoom.us"` | Sign-in and token host; 1–128 bytes. |
| `displayName` | `""` | Name used when joining; ≤ 128 bytes; empty uses the Zoom profile name. |
| `autoConnect` | `false` | `true` connects the session at `Start` when a credential exists; `false` connects at the first `zoom.*` execution. |

Both shipped templates configure the service with a placeholder client id; until it is replaced, `zoom.signIn`
fails at the token exchange and every other action reports signed-out.

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
- Two implementations: `ZoomSdkSession.cpp` over the SDK (compiled only with `ZOOM_PLUGIN_SDK_AVAILABLE`, delay-loads
  `zToolSuiteIPCProxy.dll` on the lane with `__HrLoadAllImportsForDll` so a deployment without the runtime logs
  `zoom-sdk-unavailable` once instead of failing the module map) and `ZoomSynthetic.cpp`, the in-memory client tests
  and the automated host drive. Without the SDK import `SdkSessionAvailable()` is false, `CreateSdkSession()` returns
  null, and every `zoom.*` action is refused after the one `zoom-sdk-unavailable` warning.
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
  synthetic session is selected through the test export; requests are counted and dropped.

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
| `zoom.focus` | None (`InjectsInput`) | `SelectWindows(exe:Zoom.exe)` + `BringToForeground`; `ERROR_NOT_FOUND` without a Zoom window. |

## Build and deployment

- The SDK (`zoom-plugin-sdk-windows-7.1.0.2020`, x64; Zoom publishes no ARM64 package) is licensed by Zoom, downloaded
  from the App Marketplace by each developer, and never committed. `ThirdParty/ZoomPluginSdk/Import-ZoomSdk.ps1`
  verifies the package layout and pinned version, copies `x64\export_h`, `x64\lib\zToolSuiteIPCProxy.lib`, and
  `x64\bin\*`, and writes `ZoomSdk.props` (defines `ZOOM_PLUGIN_SDK_AVAILABLE`, adds the include and library
  directories, links the proxy delay-loaded, and copies the `bin` runtime beside `RedXe.exe` after the build).
  `Zoom.vcxproj` imports the props only on x64 and only when they exist; everything else under the directory is
  git-ignored.
- `ZoomSdkSession.cpp` is pinned against the package headers at every `ZOOM_SDK_PIN` marker; with the SDK imported
  the build stops at those markers until the pinning is done, so a half-pinned adapter never ships.
- Without the import, `zoom.action.dll` builds on every platform (x64 and ARM64) with the synthetic session only;
  it still publishes the contract and the settings contract, so both templates parse everywhere and the WARP
  self-test starts the service.
- `zoom.action.dll` imports `bcrypt.dll`, `winhttp.dll`, `ws2_32.dll`, `advapi32.dll`, and `yyjson.dll` (copied
  beside it by `Directory.Build.targets`).

## Test contract

Declared in `ZoomTestContract.h` behind `REDXE_ZOOM_TEST_API`, present in Release, never called by the host:
`RedXeZoomGetTestDiagnostics` (`RedXeZoomTestDiagnostics`, 184 bytes: lane, device access, SDK availability,
synthetic, credential, sign-in, session states and flags, request counters, session starts, reconnects, sign-ins,
listener port, last failure, last action), `RedXeZoomUseSyntheticSession`, `RedXeZoomSeedCredential`,
`RedXeZoomSyntheticSetHost`, `RedXeZoomSyntheticDropConnection`, `RedXeZoomSyntheticCounters`, and
`RedXeZoomStoredCredential`. The synthetic session and the in-memory credential store and token transport are
selected only through these exports, never by a setting.

## Diagnostics (`IRedXeHost::Log`, never per request)

`lane-started`, `lane-stopped`, `zoom-sdk-unavailable`, `zoom-signed-out`, `zoom-sign-in-started`,
`zoom-sign-in-complete`, `zoom-sign-in-failed`, `zoom-listener-failed`, `zoom-auth-failed`, `zoom-connected`,
`zoom-disconnected`, `zoom-session-failed`, `zoom-action-failed`.

## Required validation

- `ZoomTests` (no Zoom, no network, no SDK): the settings model (defaults alone rejected for the missing client id,
  authored values, a privileged port, a `clientSecret` member, an empty client id); auth material (distinct
  base64url verifiers, the RFC 7636 challenge vector, base64url encoding, the authorize URL, token-response parsing
  including `invalid_grant` → `E_ACCESSDENIED` and a response without an access token, the memory credential
  store's `S_FALSE`/`S_OK`/idempotent delete); the loopback listener (ephemeral port, nothing pending, a wrong-state
  redirect answered 400 and ignored, the matching redirect delivered and the listener stopped); and the shipped
  module through the synthetic session and a fixed token transport: metadata and
  contracts, service creation with a valid and an invalid envelope, signed-out refusal of every action but
  `zoom.signIn`, the sign-in flow through a delivered redirect, sign-out cancelling a pending sign-in, a seeded
  credential connecting at the next action and running the deferred request, every in-meeting action against the
  synthetic client, role refusal of `zoom.end` and `zoom.participants.muteAll`, reconnect with backoff after a
  dropped connection, join by meeting reference, sign-out deleting the credential, and stop within the drain budget.
- `HostPluginTests`: the `zoom` contract registers from the shipped DLL without a notice (`Plugins_Actions.md`).
  `SettingsTests`: both templates' Zoom objects. `--self-test` starts the service with device access disabled.
- Live validation against Zoom Workplace with the SDK imported is manual-only (a receipt under `.build/receipts/`)
  and MUST NOT be a CI pass condition.
