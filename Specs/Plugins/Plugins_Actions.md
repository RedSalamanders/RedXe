# Actions: bindings, namespaces, publication, and the host runtime

Status: current normative product contract
Last reviewed: 2026-09-19
Owner: `Common/PlugInterfaces/Action.h`, `Common/Actions`, `RedXe/HostActionCatalog.*`, `RedXe/HostActions.*`, the
action runtime of `RedXe/PluginHost.*` and `RedXe/Application.*`, `RedXe/BundledPlugins.h`
(`kRedXeBundledActionNamespaces`), `Tests/HostPluginTests`

The plugin ABI, module discovery, service lifetime, and device lane are owned by [`Plugins_API.md`](Plugins_API.md).
The settings documents that carry bindings are owned by [`../Core/Core_Settings.md`](../Core/Core_Settings.md); the
Logicon and Launcher members by [`Plugins_Logicon.md`](Plugins_Logicon.md) and `Plugins_API.md`; the Zoom publisher by
[`Plugins_Zoom.md`](Plugins_Zoom.md). Resource bounds remain owned by
[`../Core/Core_PerformanceAndResources.md`](../Core/Core_PerformanceAndResources.md). The user page is
[`docs/actions.md`](../../docs/actions.md).

## What an action is

An **action** is a named operation a binding asks the host to perform: `"<namespace>.<verb>[.<verb>[.<verb>]]"` plus an
optional `target` string whose grammar is fixed per action. A **binding** is the closed settings object that names
one: a Logicon key, a dialpad button, a dialpad turn, or a Launcher shortcut. Every binding owner uses the same two
members with the same meaning; presentation members (`label`, `icon`, `color`, `face`, `slot`, `button`, `control`,
`direction`) stay owner-specific.

| Member | Contract |
| --- | --- |
| `action` | String. `none` (the Logicon default; a Launcher item defaults to `system.launch`), or a name matching `^[a-z][a-zA-Z0-9]*(\.[a-z][a-zA-Z0-9]*){1,3}$` of at most 64 bytes (`kRedXeMaximumActionNameBytes`; `RedXeIsActionNameSyntax`). Its first segment is the **namespace**. |
| `target` | String, 0 through 512 bytes (`kRedXeMaximumActionTargetBytes`). Ignored by actions whose target kind is `None`; every other action treats a target that does not satisfy its grammar as an **invalid binding** (drawn as a marker, never dispatched), not as a document error. |

There is no alias or compatibility layer: the former names `launch`, `keys`, `keyPage.*`, the `dialpad.dial` /
`dialpad.roller` presets, and Launcher `iconPng` are unknown names or members and reject the document.

## Namespace ownership

Every namespace has exactly one executor.

| Class | Namespaces | Declared where | Executes where |
| --- | --- | --- | --- |
| Default (hardcoded, no DLL) | `page`, `widget`, `redxe`, `system`, `keys`, `mouse` | `RedXe/HostActionCatalog.cpp` (`constexpr` descriptor tables; `kRedXeDefaultActionNamespaceNames` in `BundledPlugins.h` mirrors the list) | `page`, `widget`, `redxe` in `Application::HandleHostAction`; `system`, `keys`, `mouse` in `RedXe/HostActions.cpp`; always on the UI thread |
| Published | `logicon` (`builtin.logicon`, `Logicon.dll`), `zoom` (`builtin.zoom`, `zoom.action.dll`) | The publisher's `RedXeGetActionContract`, registered to its plugin id in `kRedXeBundledActionNamespaces` | The publisher's `IRedXeActionPack::Execute` on the UI thread |

Default namespaces are reserved: a publisher contract that names one is a collision. A plugin id may publish up to
`kRedXeMaximumActionNamespacesPerPlugin` (4) namespaces of at most 32 bytes each, each with at most
`kRedXeMaximumActionsPerNamespace` (64) actions.

### Registry — `RedXe/BundledPlugins.h`

`kRedXeBundledActionNamespaces` is a `constexpr` array of `RedXeBundledActionNamespaceSpec{actionNamespace, pluginId}`.
The consteval catalog check enforces: every namespace matches `^[a-z][a-zA-Z0-9]*$`, is unique, and is not a default
namespace; every plugin id is a row of `kRedXeBundledPlugins`. Adding a publisher is one row here plus its
`kRedXeBundledPlugins` row (and a `kRedXeBundledServices` row when it is a service). Every publisher advertises
`RedXePluginCapabilityActions` (`Factory.h`, `1U << 3U`) for the publishing plugin id.

### Contract reading and collisions

`PluginHost` reads a module's action contract **whenever it maps that module** for any reason (a placed widget, a
configured service, a bound namespace validated on first use) and registers every published namespace. Each row
below logs one `Error` line (`IRedXeHost::Log`) and appends one bounded notice (at most 8 notices of 256 characters)
that `Application::ShowActionNotices` shows through the existing settings-error dialog (`ShowSettingsError`,
non-stacking, ending "The current dashboard remains active.") after the next service apply or drained action, only
while device access is enabled.

| Condition | Log event | Effect |
| --- | --- | --- |
| A contract publishes a default namespace, or a registered namespace whose plugin id is another module's | `action-namespace-collision` | The host or the registered publisher keeps the namespace; the other publication is refused. Bindings keep working. |
| A contract publishes a namespace absent from the registry | `action-namespace-unregistered` | Refused; no document can bind it because the name never validates. |
| The registered plugin's module lacks the capability, or its contract does not publish its namespace | `action-namespace-missing` | Every binding in that namespace is an invalid marker (`ERROR_NOT_READY` from validation). |
| The registered plugin's module cannot be mapped, publishes an invalid contract, or cannot create its executor | `action-publisher-unavailable` (invalid contract: `action-contract-invalid`) | Same. |

A contract is invalid when its `sizeBytes`, a namespace's `sizeBytes`, a descriptor's `sizeBytes`, a count bound, a
namespace name, an action name (grammar, namespace prefix, uniqueness), a target kind, or the required
`targetOptions` for an `Enum` kind is wrong; the whole module's publication is refused, never a part of it.
`ResetActionPublishers` (every settings apply) clears the notices and returns `Unavailable` slots to `Unresolved` so
a repaired deployment retries once; `Missing` is permanent for the process because the mapped image cannot change.
`action-contract-loaded` (`Info`) records a successful read. Because every bundled publisher is compiled from one
tree, a collision can only come from a mismatched deployment; `HostPluginTests` proves the shipped catalog collides
nowhere.

## Validation policy

Two layers decide what a document may say and what a control does:

1. **Document validation** (settings parse, `HostActionCatalog::IsKnownActionName`): the name must match the grammar
   and its namespace must be a default namespace or a registered one; a default namespace's verb must exist in the
   host catalog. A failure is a document error on the authored path (`Core_Settings.md`). The parser cannot read a
   publisher's contract, so a published namespace's verb is not checked here.
2. **Binding validation** (`IRedXeHost::ValidateAction`, UI thread, at factory create, `Start`, `ApplySettings`, and
   settings application): resolves the descriptor (default catalog, or the registered publisher's contract, mapping
   its module on first use exactly like a settings contract) and checks the target with the shared grammars plus the
   host's extra rules (`HostActions::ValidateExtra`: `system.power.plan` names a known plan or a GUID, `keys.down` /
   `keys.up` carry exactly one chord, `mouse.scroll*` is non-zero). `S_OK` means dispatchable; `ERROR_NOT_FOUND`
   (unknown verb), `E_INVALIDARG` (unsatisfied target), and `ERROR_NOT_READY` (publisher missing or unavailable) make
   the binding **invalid**: Logicon draws the red `!` face, Launcher draws the `Warning` glyph tile, and neither ever
   dispatches it. No object, thread, or window is created for validation.

An unsatisfied target therefore never rejects a document (including a Launcher launch target that is not an absolute
path or URI); only the closed shape, the grammar, and the namespace do.

## Default action catalog

Target kinds are `RedXeActionTargetKind` values; flags are `RedXeActionFlags`. **I** = `InjectsInput`, **X** =
`Destructive` (the target must be the confirming argument; there is never a dialog), **D** = `Deferred`. Every
default action returns synchronously.

### `page.*` and `widget.*` (`Application`)

| Action | Target kind | Behavior |
| --- | --- | --- |
| `page.next`, `page.previous` | None | `NavigateToAdjacentPage`. |
| `page.first`, `page.last` | None | `NavigateToPage(0)` / last page. |
| `page.goto` | PageRef: a page `id`, or a zero-based index 0–15 | Stage-and-settle to that page; an unknown page returns `ERROR_NOT_FOUND`. |
| `widget.raise` | WidgetRef: `<pageId>/<ordinal>` or `<ordinal>` | `TryRaiseWidgetAt`; a page id other than the current page or an ordinal past the page's widget count returns `ERROR_NOT_FOUND`; another raised widget returns `ERROR_BUSY`. |
| `widget.toggle` | WidgetRef | Dismiss when that widget is the raised one, else raise it. |
| `widget.dismiss` | None | `DismissWidgetRaise`; `S_OK` when nothing is raised. |
| `widget.next`, `widget.previous` | None | Raise the next / previous widget in page order (wrapping); when one is raised it is dismissed immediately (no cross-fade) before the next rises. |

**Busy rule:** a `page.*` or `widget.*` action that arrives during a page swipe, a page settle, an overlay motion,
or while the settings error dialog is up returns `ERROR_BUSY` / `E_NOT_VALID_STATE` and is never queued for later;
`page.*` also refuses while a widget is raised. Every other namespace executes regardless of dashboard motion.

### `redxe.*` (`Application`)

| Action | Target | Behavior |
| --- | --- | --- |
| `redxe.settings.reload` | None | Forgets the dedup stamps and runs the change path (`OnSettingsChanged`). |
| `redxe.settings.edit`, `redxe.logs.open` | None | `system.launch` of the settings path / logs directory. |
| `redxe.screenshot` | Text: `<absolute png path>[@<pageId>[/<ordinal>]]` | `RequestScreenshot` (the `--screenshot` pipeline); a relative path is `E_INVALIDARG`. |
| `redxe.quit` | Enum `now`, **X** | `CloseMainWindow`. |
| `redxe.dock.show`, `redxe.dock.hide`, `redxe.dock.toggle` | None | Reveal, collapse, or flip an autohide dock (`Specs/UI/UI_XeneonDisplayWindowing.md` "Autohide"): `show` reveals at once and keeps the bar until another hold appears and clears; `hide` collapses even with the pointer inside but is inert while a raise, capture, dialog, or pending screenshot holds it; `toggle` is `show` when the strip shows, else `hide`. `S_FALSE` (counted, inert) without an autohide dock. |

### `system.*` (`HostActions`)

| Action | Target | Behavior |
| --- | --- | --- |
| `system.launch`, `system.open` | PathOrUri | `ShellExecuteExW` with the default verb, `SEE_MASK_FLAG_NO_UI`, a null window, no wait; a file's parent directory is the working directory. |
| `system.run` | CommandLine: an absolute executable path, optionally quoted, plus arguments | `CreateProcessW` (no shell, no window inheritance), handles closed at once. |
| `system.lock` | None | `LockWorkStation`. |
| `system.sleep`, `system.hibernate` | Enum `now`, **X** | `SetSuspendState`. |
| `system.logoff` | Enum `now`, **X** | `ExitWindowsEx(EWX_LOGOFF)`. |
| `system.shutdown`, `system.restart` | NowOrSeconds 0–3600, **X** | `InitiateSystemShutdownExW` under `SE_SHUTDOWN_NAME` with the countdown; `now` is 0 s. |
| `system.shutdown.cancel` | None | `AbortSystemShutdownW`. |
| `system.process.close` | Window selector | `WM_CLOSE` to the selected window; never terminates a process. |
| `system.power.plan` | Text: `balanced`, `highPerformance`, `powerSaver`, or a GUID | `PowerSetActiveScheme`. |
| `system.theme` | Enum `light` / `dark` / `toggle` | `AppsUseLightTheme` and `SystemUsesLightTheme` under `HKCU\...\Themes\Personalize`, then `WM_SETTINGCHANGE` `ImmersiveColorSet`. |
| `system.taskManager` | None | Launches `Taskmgr.exe`. |

### `keys.*` (`HostActions`, **I**)

| Action | Target | Behavior |
| --- | --- | --- |
| `keys.press` | Chords (1–8) | Each chord's modifiers down, key down, key up, modifiers up, in order. |
| `keys.down`, `keys.up` | One chord | Hold / release; the host tracks one held chord. |
| `keys.type` | Text | `KEYEVENTF_UNICODE` per UTF-16 unit. |
| `keys.media` | Enum `play-pause`, `stop`, `next-track`, `previous-track`, `volume-up`, `volume-down`, `mute` | The matching `VK_MEDIA_*` / `VK_VOLUME_*` tap. |
| `keys.lock` | Enum `caps`, `num`, `scroll` | Tap of the lock key. |
| `keys.layout` | Text: a language tag or an 8-digit KLID (not **I**) | `LoadKeyboardLayoutW` and `WM_INPUTLANGCHANGEREQUEST` to the foreground window. |

Chords are injected as scan codes (`MapVirtualKeyW`, extended flag for the navigation cluster) in batches of at most
`kMaximumInputBatch` (32) `INPUT`s per `SendInput`, never sleeping between batches. Anything held by `keys.down` or
`mouse.down` is released by the matching `up`, by any later execution after `kHeldReleaseMilliseconds` (2000), and at
runtime shutdown (`HostActions::ReleaseHeld`). Windows does not deliver injected input to an elevated window; RedXe
never runs elevated and does not work around it.

### `mouse.*` (`HostActions`, **I** except `mouse.speed`)

| Action | Target | Behavior |
| --- | --- | --- |
| `mouse.move` | Point (`x,y`, `+dx,+dy`, `center`, each optionally `@<monitor>`) | `SetCursorPos` / relative move. |
| `mouse.click`, `mouse.doubleClick` | Enum `left`, `right`, `middle`, `x1`, `x2` | Button down/up (twice for a double-click) at the pointer. |
| `mouse.down`, `mouse.up` | Enum (same) | Hold / release; one held button is tracked. |
| `mouse.scroll`, `mouse.scroll.horizontal` | Delta −100–100, non-zero | `WHEEL_DELTA` × n. |
| `mouse.speed` | Integer 1–20 | `SystemParametersInfoW(SPI_SETMOUSESPEED)`. |

Deliberately **not** published in this phase: `system.eject`, `system.notify`, `widget.command`, and the future
`screen.*`, `window.*`, `navigate.*`, `audio.*`, and `clipboard.*` publishers; adding one follows the registry rule
above, never a private vocabulary.

## Shared target grammars — `Common/Actions`

`ActionTargets.h/.cpp` is compiled into the host and into every publisher (Logicon, Launcher, Zoom) so validation and
execution agree by construction. `RedXeActions::ValidateTarget(descriptor, target)` dispatches on the descriptor's
kind and returns `S_OK`, `E_INVALIDARG`, or (for a null/empty target on a kind other than `None` without
`TargetOptional`) `E_INVALIDARG`; `SplitSuffix` separates a trailing `@<selector>` when the descriptor carries
`MonitorSuffix` or `WindowSuffix` (a `Point` handles its own `@monitor`). The parsers allocate nothing and copy into
caller-owned bounded storage.

| Grammar | Syntax |
| --- | --- |
| **PathOrUri** / **Path** | `IsAbsolutePath` (`X:\...` or `\\server\share\...`) or `IsUri` (an alphabetic scheme of at least two characters followed by `:`). |
| **CommandLine** | An absolute executable path, optionally in double quotes, followed by arguments (`ParseCommandLine`). |
| **PageRef** | A page id or a decimal index. |
| **WidgetRef** | `<pageId>/<ordinal>` or `<ordinal>` (`ParseWidgetRef`). |
| **Integer** | Decimal within `[targetMinimum, targetMaximum]`. |
| **Delta** | `n`, `+n`, `-n`; relative forms are clamped to the bounds at execution (`ParseDelta`). |
| **Enum** | One of the descriptor's `\|`-separated `targetOptions`, case-sensitive. |
| **Chords** | `chord(,chord)*`, at most `kMaximumChords` (8); `chord` = `(Mod+)*Key`; `Mod` ∈ `Ctrl`, `Shift`, `Alt`, `Win`; `Key` is a letter, digit, `F1`–`F24`, one of the named keys in `docs/actions.md`, or `VK:<hex>`; names are case-insensitive (`ParseChords` → `ChordSequence` of `KeyChord`). |
| **Point** | `<x>,<y>` (physical virtual-screen pixels), `+<dx>,+<dy>`, or `center`, each optionally `@<monitor>` (`ParsePoint`). |
| **Monitor** | `primary`, `xeneon` (the monitor hosting RedXe's window), `all`, `<n>` (1-based `EnumDisplayMonitors` order), `name:<substring>` (`ParseMonitorSelector`). |
| **Window** | `foreground`, `exe:<image.exe>`, `class:<class>`, `title:<substring>` (`ParseWindowSelector`; `WindowSelector.cpp` selects the first visible non-tool top-level window in Z order without allocating, and `BringToForeground` taps `Alt` synthetically before `SetForegroundWindow`). |
| **Meeting** | `https://<host>/j/<id>[?pwd=<passcode>]` or `<id>[:<passcode>]` with a 9–11 digit id (`ParseMeeting`). |
| **NowOrSeconds** | `now`, or a decimal delay in seconds within the bounds (`ParseNowOrSeconds`). |

`FluentGlyphNames.h` (the Segoe Fluent Icons name table shared by Logicon faces and Launcher tiles) and
`GlyphIcon.cpp` (`RasterizeFluentGlyph` through DirectWrite into caller-owned BGRA) live beside the grammars.

## Publication ABI — `Common/PlugInterfaces/Action.h`

- Records are size-pinned: `RedXeActionDescriptor` (56 bytes: `sizeBytes`, `flags`, `name`, `displayName`,
  `targetSyntax`, `targetKind`, `targetMinimum`, `targetMaximum`, `targetOptions`), `RedXeActionNamespace` (24:
  `sizeBytes`, `actionCount`, `name`, `actions`), `RedXeActionContract` (16: `sizeBytes`, `namespaceCount`,
  `namespaces`), `RedXeActionRequest` (32: `sizeBytes`, `flags`, `actionUtf8`, `targetUtf8`, `sourcePluginId`).
  Every `sizeBytes` must equal the current `sizeof`.
- `extern "C" HRESULT __stdcall RedXeGetActionContract(const char* pluginId, const RedXeActionContract** contract)`
  returns the borrowed, immutable contract of one plugin id or `HRESULT_FROM_WIN32(ERROR_NOT_FOUND)`; it creates
  nothing and is read like `RedXeGetPluginSettingsContract`. A module that advertises
  `RedXePluginCapabilityActions` for an id MUST answer for it.
- `IRedXeActionPack` (`3C7A9E10-5B2D-4F81-9A6E-0D4C8B2F7E51`) has one method, `Execute(const RedXeActionRequest*)`.
  A service publisher exposes it as a sibling of `IRedXeService` on the same controlling `IUnknown` and the host
  queries it on the started service object (never retained, so a stopped service cannot execute through a stale
  reference; a service that is not configured or failed to start yields `ERROR_NOT_READY`). A dedicated action DLL or
  a widget provider gets one `RedXeCreate(IID_IRedXeActionPack, options, host, pluginId, …)` per process with the
  ordinary `{"plugin":{},"instance":{}}` envelope on the first execution in one of its namespaces, retained in its
  `PublisherSlot` and released in `PluginHost::Shutdown` after `StopServices` and after the control lane has drained
  and suppressed completions, before `RedXePluginShutdown` and unmapping.
- `Execute` runs synchronously on the UI thread inside the host-action drain, is non-reentrant, and MUST return
  within `kRedXeActionExecuteBudgetMilliseconds` (20) without waiting on another thread, pumping messages, or showing
  UI. A `Deferred` action returns `S_FALSE` after handing the work to a host-owned lane (`QueueControlWork`, or the
  publisher's own device lane through a bounded slot and its wake event). From `Execute` a publisher MAY call only
  `QueueControlWork`, `Log`, and `RequestAction`. Return codes: `S_OK`, `S_FALSE` (deferred), `E_INVALIDARG` (name or
  target the publisher does not accept), `ERROR_BUSY` (its bounded slots are full), `E_NOT_VALID_STATE` /
  `E_ACCESSDENIED` (state or role).
- Between executions a dedicated action DLL owns no thread, timer, window, hook, or COM registration; a service
  publisher owns exactly what the service contract allows it.
- `RedXeActionRequestFlagDeviceAccessDisabled` is set on every request while `PluginHost::DeviceAccessEnabled()` is
  false (`--self-test`, `HostPluginTests`, automated hosts). The executor then validates, counts the request in its
  test contract, and returns `S_OK` or `S_FALSE`; it MUST NOT inject input, start a process, or change display,
  power, audio, clipboard, or meeting state.

## Host runtime

### Host services (`IRedXeHost`)

- `RequestAction(const RedXeActionRequest*)`: safe from any thread including a service's device lane,
  allocation-free, never blocking. The host copies the request into a 16-slot ring of
  `HostActionSlot{action[65], target[513], used}`, coalesces an identical pending `(action, target)` pair
  (`S_FALSE`), returns `ERROR_BUSY` when the ring is full, and posts one coalesced `WM_APP + 5`. A null record, a
  mismatched `sizeBytes`, a name outside the grammar, a name outside a default or registered namespace, or an
  overlong name or target returns `E_POINTER` / `E_INVALIDARG`; after shutdown, `E_UNEXPECTED`. The verb and target
  are resolved at drain time, not at request time.
- `ExecuteAction(const RedXeActionRequest*)`: UI thread only, synchronous, non-reentrant; allowed from `OnPointer`
  (committed activation), `OnKey` / `OnCharacter`, and `OnDrop`; forbidden from device, size, visibility, raise,
  `Render`, `Prepare`, and every worker callback. Never queued, coalesced, or dropped; it returns the executor's
  result (`ERROR_BUSY` for the busy rule above, `ERROR_NOT_FOUND` for an unknown action, `E_INVALIDARG` for an
  unsatisfied target).
- `ValidateAction(const RedXeActionRequest*, const RedXeActionDescriptor**)`: the binding validator above; the
  optional descriptor output borrows the owning record and is null for an unknown action.
- `RequestHostAction`, `RedXeHostAction`, and `RedXeHostActionRequest` no longer exist.

### Drain and dispatch

`Application::DrainHostActions` runs on the UI thread outside input and render dispatch. For each slot
`PluginHost::ExecuteNow` resolves the namespace: `page` / `widget` / `redxe` go to the registered application
handler (`Application::HandleHostAction`); the other default namespaces validate the target, apply
`ValidateExtra`, and run `HostActions::Execute(descriptor, target, deviceAccess)`; a published namespace obtains the
executor (`EnsurePublisherExecutor`), validates the target against the published descriptor, and calls `Execute`
with the device-access flag. A failed result logs `action-failed` (`Debug`, name and `HRESULT`). After every drained
action the host publishes one host state to started services and shows pending notices.

### Automated hosts and counters

`HostActions::Counters` (`executed`, `injectedInputs`, `launches`, `processes`, `powerRequests`, `heldReleases`,
`lastAction`) count every execution; with device access disabled `HostActions::Execute` validates, counts, and
performs nothing (no `SendInput`, `ShellExecuteExW`, `CreateProcessW`, power, registry, or layout call). Launcher's
own launch counter and Logicon's `localExecuted` / `lastAction` diagnostics observe the same rule. Live shell, input,
and power effects are manual-only and MUST NOT be a CI pass condition.

### Diagnostics

`IRedXeHost::Log`, never per detent: `action-contract-loaded` (Info), `action-contract-invalid`,
`action-namespace-collision`, `action-namespace-unregistered`, `action-namespace-missing`,
`action-publisher-unavailable` (Error, once per condition per process), `action-failed` (Debug). A publisher MAY
log a Warning once per distinct failure.

## Owner integration

- **Logicon** (`Plugins_Logicon.md`): `keys[]`, `dialpad.buttons[]`, and `dialpad.turns[]` carry the binding core;
  `LogiconService` validates every binding through `ValidateAction` at create, `Start`, and `ApplySettings`, keeps
  the result in the binding (`valid`), draws invalid ones as the red `!` face, and dispatches valid ones from its
  lane through `RequestAction` — except its own namespace, which it executes locally on the lane without a host
  round trip. `Logicon.dll` advertises `Service | Actions` and publishes `logicon` as a sibling `IRedXeActionPack`
  on the service object.
- **Launcher** (`Plugins_API.md`): `shortcuts[]` items are `action` (default `system.launch`), `target`, and
  `icon`; the shared parser `Plugins/Launcher/LauncherBindings.h` (`ParseShortcutItem`) is the single source of the
  closed shape for the DLL and the host's settings validator; a committed tap calls `ExecuteAction`, and a failure
  reports `Degraded` "Launch failed".
- **Zoom** (`Plugins_Zoom.md`): the first dedicated action DLL, a service publishing `zoom`.

## Performance and resource bounds

- The ring is static (16 × ~580 bytes); publisher slots are one per registry row; notices are 8 × 256 characters.
  Steady state allocates nothing: bounded stack buffers (`std::array<wchar_t, 513>`, 32 `INPUT`s), no strings, no
  vectors. Accepted allocations are those an API mandates.
- Validation maps at most the modules whose namespaces a document binds, at the same moment and cost as reading a
  settings contract; a document that binds only default namespaces maps no module for actions.
- No polling: the ring drains on one posted message; held-input release is checked on the next execution and at
  shutdown, not on a timer.

## Safety

- Destructive actions (`redxe.quit`, `system.sleep`, `system.hibernate`, `system.logoff`, `system.shutdown`,
  `system.restart`, `zoom.signOut`, `zoom.leave`, `zoom.end`) require the confirming target; there is never a
  confirmation dialog, because a physical control is a deliberate input.
- `system.process.close` sends `WM_CLOSE` only. `system.run` never goes through a shell. Nothing elevates.
- A collision or a foreign DLL can disable bindings but never crashes the host or blocks the dashboard.

## Required validation

- `HostPluginTests`: the named ring (bounds, coalescing, `ERROR_BUSY`, `ExecuteAction`), and `TestActionValidation`:
  default descriptors and target grammars through `ValidateAction` (page ref, absolute versus relative launch
  target, destructive confirmation and delay, chord sequences and an unknown modifier, the one-chord rule of
  `keys.down`, monitor-relative and half-relative points, named and unknown power plans), an unknown default verb
  and an unregistered namespace (`ERROR_NOT_FOUND`), the Logicon and Zoom publishers resolved from their shipped
  modules (`Ready`, a target outside the published bounds `E_INVALIDARG`, an unknown published verb
  `ERROR_NOT_FOUND`, a window suffix and a meeting reference where the descriptor allows them), the shipped catalog
  producing no notice, and device-access-disabled execution of `keys`, `system`, and `mouse` actions that counts
  inputs, launches, and power requests without performing them.
- `SettingsTests`: document-level acceptance of both templates' bindings (`page.*`, `widget.*`, `keys.media`, dialpad
  `turns`; in Debug also `logicon.keyPage.*`, `logicon.brightness`, and `zoom.mute`) and of the `builtin.zoom`
  service object, rejection of unknown names, unknown default verbs, `iconPng`, and a non-launch Launcher item
  without `icon`, and acceptance of a launch target that is not a path (decision D1).
- `LogiconTests`, `LauncherTests`, `ZoomTests`: their owning specs. `--self-test` renders both templates with device
  access disabled, so every template binding is validated on every run without a side effect.
