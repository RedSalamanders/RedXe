# AV Control: Direct3D audio and webcam profiles

Status: HOLD — AV release gates remain open (real IME/AT, matched text/UIA performance, G1 audio, G2 camera)
Date: 2026-09-05
Requested deliverables: complete AV Control implementation, native integration, tests and specification closeout
Proposed identity: `builtin.av-control` / widget type `av-control` / `AVControl.dll`

## Purpose and authority

Latest pause checkpoint (2026-09-05 21:15 UTC):
[AVControl-Continuation.md](AVControl-Continuation.md). It supersedes older progress notes for pins,
test results, failed CI/performance evidence, restored negative controls and ordered resume steps.

AV Control gives the XENEON EDGE a touch-friendly control surface for selecting an audio output, microphone, and
webcam together, muting each independently, and adjusting system output volume and microphone input level.
The user confirmed that video means webcam selection and feed on/off.

This RFC specifies the intended product and implementation acceptance criteria. It does not change the current
shipped contracts or register an unavailable plugin. Requirements below are proposed release requirements, not claims
that RedXe or Windows already implements them. Backend feasibility gates remain open in the implementation checklist.

Owning contracts:

- [Plugin API](../../Plugins/Plugins_API.md): factory, GPU/input interfaces, host services, module lifetime.
- [Dashboard](../../UI/UI_Dashboard.md): tile/raised geometry, touch arbitration, keyboard and accessibility integration.
- [Settings](../../Core/Core_Settings.md): schema, bounded persistence, live reload, shipped examples.
- [Performance and resources](../../Core/Core_PerformanceAndResources.md): event-driven work and resource bounds.
- [DxUi consumer contract](../../Core/Core_DxUiIntegration.md): pin, restore, module linkage, and COM/POD adapters.
- Historical extraction sequencing: [RFC_Core_DxUiSharedProject.md](../Done/RFC_Core_DxUiSharedProject.md).
- Planned feature owner at implementation: `Specs/Plugins/Plugins_AVControl.md`.

AV Control is the first application feature to consume the independent `Z:\src\DxUi` library, hosted in private
[RedSalamanders/DxUi](https://github.com/RedSalamanders/DxUi) with default branch `main`. Its single `DxUi.lib`
contains public controls and supplied-device embedded rendering. RedXe pins `3208083836a89d2c3348e4389b105cf3c2b453fc`
and ships synthetic preparation/input/text/UIA adapters. Real IME/touch/screen-reader, matched text/UIA performance,
and AV audio/camera backends remain this plan's release gates. AV does not copy RedSalamander source or fork
controls. RedSalamander's later migration is independent. The browser mockup does not prove native host or backend
behavior.

The interactive design source is [av-control.html](../../../Mockups/av-control.html). It uses synthetic devices and
local interaction only. It does not enumerate hardware, change Windows settings, record audio, or open a webcam.

## Required user outcomes

| ID | Requirement | Observable result |
| --- | --- | --- |
| AV-01 | Save different device associations as named profiles. | Each profile binds one output, one microphone, and one webcam route. |
| AV-02 | Apply an already configured profile with one click or tap. | One command applies all three bindings; the UI reports verified success, incomplete setup, or partial failure. |
| AV-03 | Mute/unmute output with one click. | The selected output endpoint's master mute changes without changing its saved level. |
| AV-04 | Mute/unmute microphone with one click. | The selected capture endpoint's mute changes; the UI names that microphone and its scope. |
| AV-05 | Turn the webcam feed off/on with one click. | The controlled camera route stops/starts delivering physical camera frames; unsupported routes never report a successful off state. |
| AV-06 | Manage global output volume. | A 0–100% master slider controls the selected Windows render endpoint, across sessions using that endpoint. |
| AV-07 | Manage global microphone gain. | A 0–100% input-level slider controls the selected Windows capture endpoint; reported dB is optional device information. |

“Global” means endpoint-wide, not every connected device or every application's private control. Applications pinned
to another endpoint, driver-specific paths, and application processing remain outside that scope. Windows EndpointVolume
can use hardware or software controls; software controls do not govern exclusive-mode streams. The interface must not
claim a hardware privacy guarantee. [Microsoft EndpointVolume API](https://learn.microsoft.com/en-us/windows/win32/coreaudio/endpointvolume-api)

V1 excludes per-application mixing, media playback transport, recording, streaming, background effects, speaker tests,
always-on level meters, and continuous webcam preview. These are not needed for the requested controls.

## Platform feasibility and release gates

| Capability | Proposed backend and evidence | Release condition |
| --- | --- | --- |
| Audio enumeration and live state | MMDevice plus endpoint volume interfaces; callbacks for changes. | Validate with render and capture endpoints, hotplug, and service restart. |
| Volume and microphone level | `IAudioEndpointVolume`, scalar 0–1 mapped to UI 0–100. | Read back committed state and handle unsupported/access-denied endpoints. |
| Audio default selection | A private, isolated audio-policy adapter; no documented default setter was established in this research. | G1 must identify the actual API, compatibility policy, supported OS builds, and failure behavior. |
| Camera selection and off/on | Proposed Windows 11 `RedXe Camera` virtual route, with a physical source selected by the profile. | G2 must prove source switching, off-state frame behavior, lifetime, packaging, and client interoperability. |
| Windows 10 webcam control | Only a separately validated supported application/device integration. | Otherwise show `Camera control unavailable`; do not claim AV-02/AV-05 are fully delivered on Windows 10. |
| Native control library | Pinned standalone DxUi.lib, with embedded D3D11 hosting. | Library extraction, pin, and synthetic adapters are Done. Remaining G3 work is real IME/AT and measured resource acceptance, not another library extraction. |

Default audio switching is a distinct problem from endpoint enumeration or changing volume. Microsoft's MMDevice
documentation describes reading defaults and observing role changes; its legacy role guidance does not supply a setter.
Do not invent a `SetDefaultAudioEndpoint` method on `IMMDeviceEnumerator`, silently write audio-policy registry data,
or hide an undocumented COM dependency. An undocumented adapter is a candidate requiring explicit compatibility review,
not an accepted ABI in this RFC. [Device-role guidance](https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-roles-in-windows-vista)

The documented virtual-camera creation API requires Windows build 22000 or newer. Microsoft's camera sample also
identifies newer source-wrapping facilities on build 22621. Neither source establishes universal camera switching or
muting for arbitrary applications. The proposed route is a design inference: each consuming application selects
`RedXe Camera` once, then profiles change its upstream source. Applications using a physical webcam directly remain
outside this control. [MFCreateVirtualCamera](https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera),
[Microsoft virtual-camera sample](https://github.com/microsoft/Windows-Camera/blob/master/Samples/VirtualCamera/README.md)

Do not use device disable/enable, driver removal, registry privacy edits, injected keystrokes, or elevation as the mute
implementation. Windows camera consent belongs to the user. Access denial is actionable UI state, not a setting to
bypass. [Camera privacy handling](https://learn.microsoft.com/en-us/windows/apps/develop/camera/camera-privacy-setting)

The full feature cannot be declared complete by shipping volume sliders alone. If G1 or G2 cannot be met, retain the
affected outcome as **[blocked]**, document the missing backend evidence, and settle the supported feature scope before
implementation closeout. No existing operating-system setting is changed by this specification work.

## Profiles and one-click application

### Profile contents

Support up to four profiles in V1. A profile has a stable local ID, a name, three device IDs, an audio role scope,
and optional output/input levels. The editor offers `All Windows roles` (default) and `Communications only`.
The first addresses console, multimedia, and communications for both render and capture; the second changes only
communications defaults. The panel states the selected scope. Role assignment is separate for render and capture.
[Windows device roles](https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-roles)

The proposed examples are `Studio`, `Meeting`, and `Headset`. These are illustrative associations, not assumptions
about installed hardware. A fresh install has no profiles and offers `Create profile` with discovered devices.
Shipped settings use an empty profile array so they are valid on every machine.

One click means one committed activation after initial configuration. Creating a profile or selecting `RedXe Camera`
in a conferencing app is setup, not part of every profile switch. Editing a draft does not switch devices; `Save`
persists the draft, and the profile button applies it. A missing device keeps its association for repair.

### Apply sequence

1. Resolve all three stored IDs and capabilities, snapshot current defaults for affected roles, target levels/mutes,
   camera route state, and state revisions. Missing devices or unsupported required operations fail preflight without
   changing anything. Show exactly which binding needs attention. No automatic substitute or name-based match.
2. Serialize one process-wide profile operation. Show `Applying…` immediately, disable additional profile activation,
   and retain independent mute/off commands. A later mute/off supersedes any pending unmute/on.
3. Preserve privacy and silence across device changes. The destination microphone/output remains muted if either
   the previous controlled endpoint or destination was muted. Camera off remains off across source changes. A profile
   never turns a muted microphone or camera back on; the dedicated control is required to do that.
4. Establish safe target mute/off states, apply optional levels to targets, change requested audio roles, and switch
   the camera source behind the existing feed gate. Keep camera frames blocked throughout source replacement.
5. Read back each requested result, including every affected audio role, before showing the profile as applied.
   Mark `Active` only when route bindings match; append `Adjusted` when optional restored levels differ. Mute/off is
   deliberately independent of profile matching. Clicking an already active profile is idempotent unless its saved
   optional levels need restoring.
6. On failure after mutation, stop remaining stages and attempt compensating rollback only for fields still owned by
   this operation. Do not overwrite a newer external/user revision. Never undo a later mute/off. Rollback is best
   effort: Windows audio and camera changes are not an atomic transaction. Show actual per-device results and
   `Partially applied` if any state cannot be restored. `Retry` is explicit; no background retry loop.

No profile is applied on launch, reload, device reconnect, resume, page activation, or device-loss recovery. Observe
current state first. External changes produce `Custom` or `Adjusted`; do not fight Windows, headset buttons, or another
application by reapplying the profile. Endpoint changes affect apps following Windows defaults; already open or
explicitly pinned streams may need application-side reselection. [Windows stream routing](https://learn.microsoft.com/en-us/windows/win32/coreaudio/getting-the-default-device-endpoint-for-stream-routing)

### Independent controls

- Output and mic mute each read the confirmed state, enqueue a desired boolean, and update the confirmed label after
  acknowledgement. During an operation show `Muting…`/`Unmuting…`; repeated clicks coalesce to the newest intent.
- Never implement mute by setting volume to zero. Changing a slider while muted updates the level and keeps mute on.
- Input level is the Windows endpoint control, not a fabricated hardware preamp/boost. Use `Microphone gain` as the
  main UI label and `Windows input level` as supporting text; minimum tiles abbreviate it to `Mic`. Show dB only when
  reported by that endpoint. Never derive dB by a
  linear mapping from percent, and do not promise a boost range. Scalar controls use Windows' audio-tapered curve.
  [Scalar level API](https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nf-endpointvolume-iaudioendpointvolume-setmastervolumelevelscalar),
  [Device dB range](https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nf-endpointvolume-iaudioendpointvolume-getvolumerange)
- Slider dragging previews locally and commits on release in V1. Keyboard arrows change 1%, Page Up/Down 5%, and
  Home/End select 0/100. Display the acknowledged value if the driver clamps it. Cancel restores the prior local
  display and sends no setter. Bind each gesture to the endpoint/revision captured on Down; cancel if it changes.
- Camera `Off` means the controlled route emits only a neutral camera-off frame and no previous physical frame. Stop
  physical acquisition and release that source when no other route consumer needs it. A lit hardware LED or another
  direct consumer cannot be used as evidence of this route's mute state. Label scope `Apps using RedXe Camera`.
- A hardware shutter, privacy denial, missing camera, or busy source has a distinct state. Show `Blocked`, `Missing`,
  `Busy`, or `Control unavailable`; never translate unknown state into `Off` or `On`.

## UI specification

### Workflow hierarchy

Revised after the user's UX review on 2026-09-05. There are three workflows; defining a profile is a subordinate step
of switching profiles, not a fourth peer task.

| Workflow | Frequency / priority | Default presentation |
| --- | --- | --- |
| Switch profile; optionally define/edit one | Uncommon, lower priority: keep a selected profile for a while. | Quiet current-profile selector. Available profiles appear only on demand; `Define profiles` is inside that view. |
| Mute/unmute output or mic; camera off/on | Frequent live use, medium priority. | Three direct, persistent controls with readable confirmed state. |
| Adjust output volume or input gain | Frequent live use, same medium priority as mute. | Both controls remain direct and visible alongside the three toggles. |

The default view is a live control surface. Its five functions are output mute, mic mute, camera off/on, output level,
and mic level. No level control may disappear into a menu before the equally important mute controls. Profiles,
device descriptions, explanations, and decoration yield space first. Do not display an expanded preset strip or a
standalone profile-edit action above the live controls. No camera preview, filler metric, or decorative animation.

### Target geometry and the half-screen limit

The owning [display contract](../../UI/UI_XeneonDisplayWindowing.md) specifies a 2560×720 landscape XENEON EDGE
at 96 DPI. The maximum landscape plugin rectangle is therefore **1280×720**, including its own padding/chrome.
The previous 960×720 target did not express that requirement. Both the normal placement and raised content must
stay within the host's half-client-width, full-client-height envelope; never request full-screen expansion.
Use `RedXeRaisedExtentHalf`. When the plugin already occupies its maximum rectangle, open the local profile view
without raising again. The dashboard retains ownership of the neighboring half.

Dimensions below are logical content dimensions at 96 DPI. Use actual host content width/height after host chrome
and edge reservations, convert with the current DPI, and select density from **both** axes. Do not infer density
from percentage of screen area or browser width alone. A 640×720 tile and a 1280×360 tile have equal area but need
different arrangements. At a rotated 720×2560 client, the same half-width rule yields **360×2560**, not 1280×720.
A title-bar fallback or increased DPI can reduce available logical space; the same fit rules still apply.

### Responsive disclosure contract

The plugin must fit its allocated rectangle. No scrolling, hidden below-the-fold live control, viewport-scale trick,
or reduced touch target is an acceptable way to fit. Browser review scaling is explicitly labeled and does not
change the logical target sizes. Within each density tier, spend additional room on larger controls and type.

| Example rectangle | Presentation | Keep visible | Remove / defer |
| --- | --- | --- | --- |
| 1280×720 maximum | Three large toggle tiles, two generous level panels below; quiet profile selector. | All five live controls, device names, numeric levels, current profile, camera scope, useful status. | Profile list and definition fields remain closed. |
| 640×720 or 320×720 tall | Three toggle rows, then both level controls stacked. | All five, profile selector, device names when they fit. | Repeated action hints and verbose level explanations. |
| 1280×360 wide | Three shallow toggles, two level panels side by side. | All five, numeric values, quiet profile selector. | Full device descriptions and redundant footer text. |
| 640×360 or 320×360 compact | Three toggles above two stacked sliders. | All five, numeric values, quiet profile selector. | Device names, repeated hints, scope prose; complete names/scope remain in the raised profile view and accessible names. |
| 640×180 or 320×180 strip/small | Three 48-pixel toggle targets, then output and mic levels with decrement/increment targets. | All five, confirmed toggle states, both values, compact profile identity. | Slider tracks, selector, names, and help; profile workflow uses host raise. |
| 160×180 supported minimum | Same five functions with `Out`, `Mic`, `Cam` labels and two compact level rows. | Three 48×48 toggles and four 48×48 step buttons; values remain visible. | All secondary content. Never retain profile setup at the cost of these controls. |
| Below 160 wide or 180 high | Not a usable direct-control placement. | Host-accessible raise/status affordance. | Do not pretend all seven touch targets fit; placement validation must report the minimum and permit correction. |

Deterministic candidate tier order, evaluated at a size/DPI change: below minimum is unusable; otherwise width below
320 or height below 300 selects steppers; width below 800 with height at least 620 selects tall; width at least 800
with height 300–479 selects wide; width below 800 or height below 480 selects compact; otherwise use the largest tier.
The required examples and their boundary neighborhoods are acceptance cases, not the only supported rectangles.
Text measurement can shorten secondary content within a tier; it must not reorder the five live functions.

Steppers change the selected endpoint by 5 percentage points per activation, clamped to 0–100. They never change its
mute state. Keyboard range control retains its 1% precision. Smaller tiles do not change endpoints, profile contents,
levels, or mute states when their presentation changes. Render the same confirmed model at each size; cancel any
in-progress drag before moving its hit rectangles. The long-to-short labels have full accessible equivalents.

### Profile workflow and return to live controls

1. Activate the quiet current-profile selector, or use the existing host raise command/gesture on a minimum tile.
   This reveals the profile chooser within at most half the client. Revealing a low-frequency chooser is one
   navigation step; applying one of its visible profiles is still one committed activation with no second confirmation.
2. Select a profile to apply all three bindings. Close the chooser and return to the original live-control size;
   show pending state there while the command completes. The three mute/off controls stay available during apply.
3. `Define profiles` opens the uncommon editor inside the same maximum-size view. Save updates the definition only;
   it returns to the chooser without changing device associations. Cancel discards the draft. Back/Escape returns
   through this same hierarchy, restoring the originating tile when leaving the chooser.

The chooser and editor are deliberate temporary modes, not persistent panes pushing live controls below the tile.
Their focus is contained while open. They must fit the half-screen content rectangle; in narrow portrait use a
single-column form and bounded steps if necessary. Profile views must not grow a new browser-like page. A profile
selection command may need a generic host raise request; that is part of G3 and must not be invented as a current ABI.
The mockup's header double-click simulates the existing host gesture; it is not a new 16-pixel-high touch button.
Native keyboard/UI Automation must expose an equivalent host raise action for minimum tiles.

Use the existing RedXe near-black panel language in dark mode, Segoe UI typography, restrained separators, and a
quiet selected-profile treatment. Muted/off states pair color with explicit text. All host chrome icons use
`FluentIcons.h`; plugin icons use DxUi's shared glyph support with Segoe Fluent Icons / MDL2 / Unicode fallback and
prepared text/icon resources.
The browser mockup uses Lucide as a visual stand-in, not a new native dependency. Light appearance in the mockup is
an exploration option, not a change to the existing host theme contract.

Minimum interactive target: **48×48 logical pixels**, including steppers. Full labels are at least 16 px; short
minimum-tier identifiers are 14 px, confirmed state 16 px, and level numbers 18 px. Larger tiers use 24–42 px state
and 28–64 px level numerals. Primary text contrast target: 4.5:1. These floors cannot be reduced to fit a smaller
tile. Device-name truncation is visual only; the chooser/editor and accessible names retain the identity.

Every tier distinguishes `On`, `Muted`/`Off`, and unknown/unavailable. A smallest-tier `?` may stand for unknown only
with a full accessible reason. Never show unknown as off. Error/pending feedback uses the affected control and a
compact header status when the footer is omitted. Feedback must not cover another live target or change its position.
No claimed hardware privacy guarantee: the camera action continues to affect only the RedXe route.

Focus follows visible reading order within the active view. Buttons support Space/Enter; ranges support the stated
keyboard steps. Hidden controls must be absent from focus order. Expose names, roles, values, pressed states, and
changes to UI Automation. Current GPU input provides pointer/drop only; keyboard, text editing, accessible virtual
children, host raise, and unambiguous slider capture need G3. Browser behavior does not prove the native bridge exists.

Consumed control clicks do not raise a tile. Sliders must own their gesture after hit-tested Down; horizontal dragging
must not turn into page navigation. Background gestures retain host navigation/raise. Cancel on capture loss,
hide, settings replacement, resize that invalidates geometry, or endpoint replacement. Verify tile and raised hit
rectangles independently and preserve host edge bands. Cache the density and hit rectangles only on actual size,
DPI, or relevant content changes; no per-frame measurement, rasterization, polling, or allocation is introduced.

## Proposed architecture and resource limits

`Plugins/AVControl/` owns the factory, validated profile model, backend command mapping, responsive layout policy,
and its DxUi embedded-host adapter. Shared DxUi owns reusable control behavior, state styling, text/layout and
prepared Direct2D drawing; the plugin composites its cached surfaces through Direct3D 11.
Expose `IRedXeWidget`, `IRedXeGpuWidget`, `IRedXeInteractiveWidget`, and `IRedXeRaisedWidget` on one controlling
IUnknown. No continuous-animation flag, scheduled polling, child window renderer, swap chain, or runtime shader compiler.

### Shared DxUi dependency and native control mapping

Follow [`Core_DxUiIntegration.md`](../../Core/Core_DxUiIntegration.md) for pin, restore, and module linkage. Extraction history is [RFC_Core_DxUiSharedProject.md](../Done/RFC_Core_DxUiSharedProject.md).
Link **DxUi.lib** into AVControl.dll through the supplied consumer props/targets, using API revision 2 and lock
target `["DxUi"]`. No separate Controls/Embedded/service archive or DxUi runtime DLL is required. EmbeddedHost uses
the host-created D3D11 device through one AV-module GraphicsDevice pool and separate tile/raised views. AV supplies
physical widget-local coordinates; EmbeddedHost converts to DIPs. It never starts its native WindowHost mode.
No DxUi C++ object, callback or STL ownership crosses the RedXe COM ABI. Host text/UIA transport remains a generic
RedXe integration gate even though native Win32 text/accessibility is available inside the library.

| AV behavior | Shared DxUi primitive / responsibility |
| --- | --- |
| Independent output/mic mute and camera on/off | Toggle-capable buttons with confirmed, pending, disabled and unavailable states; AV supplies actions and acknowledgements. |
| Output volume / microphone gain | Slider with preview, commit and cancel events; keyboard steps and RangeValue accessibility. AV commits the OS setter on release, never on every preview event. |
| Minimum-size level adjustment | Button pairs with the same AV level command and 5% steps; current value remains visible. |
| Profile chooser and device associations | Buttons/ComboBox with stable model IDs; visible names are not identities. |
| Profile definition | TextField, ComboBox, Checkbox and panels; AV retains and validates drafts, saves via the host, and applies only on explicit activation. |
| Density, raised view and device-loss recovery | AV chooses the reviewed layout tier; DxUi measures and prepares the visible controls, preserves logical state and rebuilds graphics only. |

The library must support AV's large touch targets and pressed/pending semantics without a local control fork.
Reusable changes belong in the canonical DxUi repository with tests; XENEON dimensions, profile rules, camera scope and endpoints
remain in AV. Use the tested `Slider::SetOnChange` phases; legacy `SetOnValueChanged` is a live-value observer.

G3 adopts the shared proposal's event-driven preparation service and GPU preparation callback, including both final
tile and raised extents. These are new generic host mechanisms. Prepare changed layout, text, brushes and offscreen
textures before frame construction; `Render` only composites coherent prepared surfaces. Marking a widget dirty at
unchanged size must work without pretending it resized. The host continues to own device/context, input/OS focus,
frame scheduling and presentation. DxUi receives neither the top-level HWND nor the swap-chain back buffer.

One AV module-owned DxUi resource pool per D3D device generation is shared across all AV instances and staged pages.
Keep at most two layout surfaces per instance, with sharing when extents/layout match. Preserve separate tile and
raised density/hit snapshots; a scaled half-screen layout is not a substitute for the compact layout contract.
Do not repaint clean textures because a sibling widget animates. The host-side input, TSF/IME and UIA bridges must
complete before the native editor is considered usable; HTML behavior is not an implementation fallback.

### Device operations and resource budgets

One process-scoped coordinator serializes system changes across instances and pages. UI preferences and authored
profiles stay per instance; confirmed OS state is shared. Use one proposed host-owned, lazy local control lane with a
bounded queue, completion delivery, cancellation, and drain. This is a **new mechanism requiring G3**: do not call
audio-policy COM, camera capture, device enumeration, or blocking I/O from `Render`, pointer, or visibility callbacks;
do not misuse the HTTP lane or add one worker per widget. Keep plugin-specific commands out of generic widget roots.

OS notifications copy bounded facts/dirty flags into coordinator storage and signal work; they never touch D3D,
persist settings, unregister themselves, or wait. Subscribe once per observed endpoint and coalesce invalidation.
Teardown unregisters outside callbacks and drains lifetime before releasing owners. Use WIL for COM/handles and keep
exceptions within module boundaries. [MMDevice notification contract](https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nn-mmdeviceapi-immnotificationclient)

Proposed budgets below are acceptance targets, not measurements:

| Resource/work | V1 target |
| --- | --- |
| Profiles | 0–4, total compact settings at most 4096 bytes. |
| Device inventory | At most 32 outputs, 32 inputs, 16 cameras; retain referenced/default devices first; show overflow explicitly. |
| Device strings | At most 1024 UTF-8 bytes per opaque ID, 256 UTF-16 units per cached display name; never truncate IDs into a different identity. |
| Commands | One profile transaction; at most 16 queued commands, plus coalesced per-slot desired mute/off state. Queue saturation cannot silently drop a safety action. |
| UI response | Pending visual state within 100 ms; ordinary local profile application target at most 1 s; bounded failure reported within 3 s. |
| Idle/hidden | Zero plugin periodic wake-ups, camera preview frames, audio samples, or presents. Unsubscribe display-only observation when no widget is visible. |
| CPU model memory | At most 1 MiB per instance plus 2 MiB shared inventory/command state, excluding OS-managed COM storage. |
| DxUi CPU state | Bounded control trees, text, preparation queues and shared caches; report their retained bytes separately from the AV model and OS/driver allocations in G3. No hidden unbounded cache. |
| Clean D3D composition | At most one cached-surface draw per tile/raised viewport; at most one bounded geometry upload when that transform changes, zero unchanged-state uploads. D2D dirty preparation is counted separately. |
| Surface payload | At most two resident BGRA layout surfaces. A native 1280×720 surface is 3.52 MiB; two are 7.03 MiB before driver overhead. Scale accounting by physical raster dimensions/DPI. |
| GPU caps | Provisional safety ceilings of 64 MiB resident surfaces and 128 MiB during replacement per instance, 256 KiB composite buffers per instance, and an initial 8 MiB shared cache target per device generation. Allocate actual extents lazily; measure driver/D2D overhead and whole-page totals. |
| Steady render | Zero heap allocation, text shaping/rasterization, D2D repaint, endpoint queries, disk/network I/O, blocking waits, or handle creation. |

These surface/cache targets replace the earlier custom-instancing/atlas proposal. The extra surface memory and
dirty D2D paint cost are explicit, unmeasured integration costs requiring G3 acceptance; static linkage and one final
draw do not prove low resource consumption. Use the shared proposal's resource accounting, replacement peaks and
clean/dirty/hidden benchmarks. Measure and settle the CPU UI-state budget before closing G3. Do not count only the
AV model, or silently exclude preparation work from rendering costs.

Use embedded shader bytecode for composition and bind every required pipeline state including scissor. The shared
embedded adapter prepares resolution-dependent surfaces and changing text on the newly sanctioned preparation path;
update the current size-only allocation/rasterization contract in the same implementation change. Never move that
work into `Render`, create a second D3D device, or copy textures through CPU memory to bypass the host integration.

Maintain separate tile and raised layout caches. Device loss discards GPU state only: it never reapplies a profile,
changes a mute, or restarts a camera. Draw callbacks accept finite negative swipe origins.

The control lane must prove bounded cancellation/teardown with slow COM and drivers. A timeout label alone does not
cancel an in-process call. G3 must either prove the selected operations are bounded or isolate potentially hanging
work in a terminable helper with explicit ownership; never use `TerminateThread` or strand callbacks into an unloaded
owner. Measure before accepting any new worker/helper baseline.

Camera streaming is a separate consumer lifetime. Hiding the tile must not terminate an application's active call.
The optional camera route runs only while a consuming app requests frames, releases physical capture when off, and
emits a cached neutral frame only at the consumer's required cadence. That media workload is explicitly separate
from UI idle. Its frame pool, negotiated resolution/rate ceiling, cross-process permissions, crash/off behavior, and
CPU/GPU/memory budgets must be measured in G2 before any camera bridge ships. An absent/inactive bridge adds no
camera capture or background worker to RedXe. No unbounded media buffering or retained recordings.

## Settings and persistence proposal

Use one closed object with a flat array of closed profile objects, within the host's supported schema subset.
No nested arrays, arbitrary script/command fields, credentials, or executable paths. Proposed effective example:

```json
{
  "profiles": [
    {
      "id": "studio",
      "name": "Studio",
      "outputId": "example-output-id",
      "microphoneId": "example-capture-id",
      "cameraId": "example-camera-symbolic-link",
      "audioRoles": "all",
      "restoreLevels": false,
      "outputLevel": 68,
      "microphoneLevel": 72
    }
  ]
}
```

Defaults: `{"profiles":[]}`. Each profile requires all shown fields; level integers are 0–100; `audioRoles` is
`all` or `communications`. `restoreLevels=false` leaves destination endpoint levels unchanged. Mute/off states are
live controls and are not saved as automatic unmute/on actions. No persisted `activeProfile` or automatic startup apply.

Profile IDs contain 1–32 ASCII identifier characters, unique ignoring ASCII case; names contain 1–48 Unicode scalar
values, no control characters. Device IDs are opaque UTF-8 strings, nonempty, no embedded NUL, and at most 1024 bytes.
Compare identities using their owning API's rules, never display names or parsed GUID guesses. Missing devices are
valid saved configuration. Reinstalled/replaced devices require an explicit association repair.

Validate UTF-8, duplicate/unknown fields, exact types, lengths, duplicate profile IDs, array bounds, and the serialized
4096-byte cap. The host schema subset cannot express all these string rules today: add narrow typed semantic
validation and tests in the same implementation change; do not publish unsupported schema keywords. A long-device-ID
combination may reach the byte cap before four profiles; the editor reports remaining capacity and rejects Save
without truncating IDs or losing existing profiles.

Persist explicit profile edits via `IRedXeHost::PersistWidgetSettings` on the UI thread. Keep a draft until persistence
succeeds; save failure leaves the original profile intact. Do not write files from the plugin. Slider adjustments,
mute changes, notifications, and profile application do not overwrite authored presets. Collect-on-exit returns only
valid committed dirty settings or `S_FALSE`. Cold load/live reload update configuration without any system mutation.

## Validation and implementation checklist

Design deliverables in this change:

- [x] Confirm webcam meaning and define the seven requested outcomes.
- [x] Record platform limitations, behavior, UI, persistence, resource budgets, and release gates in this RFC.
- [x] Provide a synthetic interactive mockup with profiles, independent controls, sliders, and profile editing.
- [x] Run mockup interaction/layout checks and inspect rendered dark/desktop and light/narrow images.
- [x] Propose the independent DxUi repository and update native AV architecture to reuse its pinned static library.
- [x] Complete `validate-skills.ps1`: initial sandbox Python/PyYAML failures were resolved by using the normal user
  context on 2026-09-05; all ten RedXe skills passed.

Authorized implementation — remains unfinished until the following gates pass:

- [ ] **G1: audio switching.** Identify/validate a default-policy backend on supported Windows 10/11 builds, x64 and
  ARM64. Exercise all-role and communications-only changes, Bluetooth/USB/HDMI removal, pinned app sessions,
  unsupported devices, access denial, and Audio Service restart. Document any undocumented ABI and compatibility policy.
- [ ] **G2: camera route.** Prove physical-source switching/off/on, stale-frame exclusion, busy/privacy/shutter
  behavior, multiple consumers, app-side initial selection, and graceful failure/crash. Decide OS minimum, installation
  and removal, source/helper lifetime beyond RedXe page/process exit, and measured active-media budgets. Windows 10
  support or its explicit limitation must be resolved before full-feature claims.
- [x] **G3a library extraction and pin.** Standalone DxUi, pinned static library, embedded preparation/composition,
  and synthetic text/UIA adapters. Durable consumer rules live in `Specs/Core/Core_DxUiIntegration.md`.
- [ ] **G3a remaining UI hosting.** Real TSF/IME text entry, screen-reader lifetimes, tile/raised resource
  acceptance, and matched CPU/GPU measurements. NativeStore/HostServices archives stay OPEN; do not waive them.
  RedSalamander migration is not required.
- [ ] **G3b: local-control work.** Specify and prove the host local-control lane, bounded completions,
  cancellation/drain and UI preparation invalidation. DxUi provides UI controls, not audio-policy/camera backends or
  protection against unbounded device calls. Neither G3 part exists merely because the mockup demonstrates it.
- [ ] Add `AVControl.dll` in all four solution configurations and host build-only references. Update bundled catalog,
  schema/parser, plugin settings contract, and real gallery placements with empty profiles in both settings templates.
- [ ] Unit/contract tests: factory/COM identity, configuration copying, full bounds, UTF-8, settings save failures,
  idempotence, per-role readback, rollback failure, external revision races, and later mute/off winning during apply.
- [ ] Input tests: one-click controls, both slider endpoints, dragging without navigation, Cancel without write,
  tile/raised coordinates, keyboard/focus, accessible names/values, and insufficient-space behavior. Verify every
  rectangle in the responsive disclosure table and one logical pixel either side of each tier threshold. Confirm
  all five live functions stay in bounds, 48×48 target floors, no live-content scrolling, 5% steppers, stable state
  through tier changes, chooser/editor fit at half-screen, return to originating size, and no enlargement beyond half.
- [ ] Hidden WARP integration: synthetic device backend only; draw/readback, hostile prior pipeline state, minimum,
  portrait, the maximum 1280×720 tile inside a 2560×720 host frame, multiple DPI values, negative origins,
  staged pages, device loss, and independent
  instances sharing one coordinator and one DxUi device-generation pool. Verify a dirty UI update at unchanged
  extent, no D2D repaint on clean composition, both layout variants and failed preparation without stale hit targets.
  No tests touch real defaults, volumes, webcam, or normal user settings.
- [ ] Release resource tests: 5-minute idle and hidden intervals with zero plugin-owned periodic work; repeated
  profile/mute/slider/hotplug cycles; stable heap/handles/GPU objects; separate dirty-preparation and composition
  counters; total surface/cache residency and replacement peaks; bounded queues and teardown.
  Measure real endpoint latency and the separate camera workload only in an explicitly selected manual test fixture.
- [ ] Run `./format.ps1`, Debug and Release x64 `test.ps1`, Release ARM64 build, and `./validate-skills.ps1`.
- [ ] Persist settled requirements in `Plugins_AVControl.md` and affected domain contracts, then move this RFC to
  `Specs/Plans/Done/` and remove its WIP index row only after implementation and required validation are complete.

## Design validation record

Initial draft checks on 2026-09-05, before the workflow/density revision:

- Mockup rendered through the bundled visualization renderer and tested in headless Edge. All three bindings change
  with a profile; microphone mute and camera off survive a switch; changing mic level preserves mute; saving a
  profile does not apply it; applying the edited profile uses the saved binding; output mute preserves its level.
- Light/dark at 1000, 736, and 360 px browser widths: no horizontal overflow, all visible buttons at least 48 px high,
  no JavaScript errors. Dark 736 px and light 360 px screenshots were visually inspected. Generated previews,
  screenshots, and the disposable interaction-check script are under `.build/mockups/av-control/`.
- Relative specification links resolve, mockup markup is literal, and `git diff --check` passes.
- **[blocked]** `validate-skills.ps1` first found a Python launcher with no installed interpreter. Retrying with the
  bundled Python reached `quick_validate.py` but failed on missing `yaml`. An isolated `.build` dependency download
  could not complete; the direct package request failed TLS authentication. Required follow-up: make PyYAML available
  to that interpreter and rerun the unchanged repository validator. No machine Python installation was modified.

Build, WARP, real-device, performance, native minimum-density/accessibility, and simulated failure-scenario coverage
are future implementation validation, not claimed results of this specification-only change. The design-control
scenarios in the mockup are illustrative; the checked browser interactions above describe the actual verification.

### UX revision review, 2026-09-05

Captured the original flow again in the Codex in-app browser before editing. Evidence is saved under
`.build/mockups/av-control/`: `01-before-half.png`, `02-before-live.png`, `03-before-volume.png`,
`04-before-setup.png`, and `05-before-small.png`.

| Reviewed workflow | Finding | Revision and observed result |
| --- | --- | --- |
| 1. Switch / define a profile | Expanded profile choices and setup were the most prominent controls. Definition extended the page below the controls. | One quiet current-profile selector; chooser then definition as subordinate views. Applying returned from half-screen to the original 640×360 tile; saving a changed association kept current devices and showed Custom. |
| 2. Mute / camera on-off | Clear state and independent controls worked, but their placement was below the initial 640×360 viewport. | Persistent toggle row or rows at every supported size. Mic mute and camera off stayed active after a profile switch. |
| 3. Output / input level | Levels were treated as large card contents and lost below the fold at small sizes. | Both levels stay alongside mute controls with equal priority. Sliders at larger sizes; 5% steppers at short/minimum sizes. Gain changed while the mic stayed muted. |

Revision validation uses fixed **tile** rectangles, not merely browser breakpoints. The in-app-browser DOM checks
covered 1280×720, 640×720, 1280×360, 640×360, 320×720, 320×360, 640×180, 320×180, 160×180, and 360×2560.
All ten had no tile overflow and no live target below 48×48 logical pixels. Both sliders or both pairs of steppers
were present with the three toggles. The initial 320-wide label collisions and minimum-height overflow were corrected
and rechecked. `09-after-minimum.png` shows actual muted/off state and stepper changes at 160×180.

The revised half, compact, and minimum layouts and the expanded editor were inspected visually. The editor with
optional saved levels visible fit its half-screen sheet without overflow. Save-only behavior, returning to the
originating size, preserved mic/camera mute across profile change, and independent 5% output/mic stepping were
observed in this run. The size selector and preview scale are review controls outside the depicted plugin.

The earlier browser-width/light-theme checks belong to the initial draft; they are not evidence for the revised
layout. Native DPI, physical touch accuracy, UI Automation, full breakpoint-boundary coverage, and live Windows
device behavior remain implementation validation. A fresh `validate-skills.ps1` attempt with bundled Python was
again **[blocked]** at importing `yaml`; no project skill content was evaluated. This UX revision changes only the
RFC and mockup, not application code or current normative host behavior.

### Shared-library bootstrap update, 2026-09-05

The approved private DxUi repository now exists on `main`, with independent Foundation builds, library contracts,
agent guidance, skills and an adoption plan. Its [hosted validation](https://github.com/RedSalamanders/DxUi/actions/runs/33958577659)
passed Linux validators and native x64/ARM64 Debug/Release tests. RedXe's unchanged `validate-skills.ps1` also
passed all ten skills in the normal user context; the earlier sandbox Python/PyYAML block is resolved.
That was Foundation-only historical evidence. The single-library implementation supersedes split Controls/Embedded
delivery; AV hardware/backend gates and RedXe adapter work remain open.


### Single-library adoption contract

Create/configure controls through the public API or ControlCatalog; keep device identities and confirmed/pending
state in AV. The graphics pool is retained across views, not recreated per widget. Prepare changed content before
host composition; Composite uses the already prepared surface. On slider Preview update the displayed draft; on
Commit issue the AV command; on Cancel restore confirmed/draft-start state without changing a Windows endpoint.
`gallery.ps1` and the public EmbeddedControls sample are library validation tools, not an implementation of AV.

### Implementation resumed, 2026-09-05

The user authorized continuing until AV Control is delivered and requested coordination with the concurrent
DxUi documentation/workflow task. This task owns RedXe integration, AV model/backend/UI, settings and tests.
DxUi changes and the validated release pin must be coordinated before adopting them; never change or reset the
other task's checkout. The saved DxUi handoff is historical, not an instruction to keep this task paused.

Execution order: bounded model/configuration and responsive geometry; generic host preparation/input/work lane;
native DxUi views and editor; real endpoint/backend and camera route; synthetic failure/lifetime/resource tests;
platform evidence and final normative closeout. Automated tests use synthetic devices and isolated settings only.
No full-feature claim is permitted while G1/G2/G3 or required validation remains unresolved.

#### Implementation checkpoint: native views and broker

- Implemented strict fixed-capacity configuration/profile matching/gesture model and all density geometry.
- Added generic host preparation and tile/raised view IDs, explicit pointer capture and cancellation, and a lazy
  bounded host control queue. Full Debug tests passed after preparation; later capture/queue changes still require
  a final full host regression pass after AV integration.
- Restored an isolated development DxUi pin (`f74a4a9c50d18e2af678ed360d86321e7dd2068b`, API 2, one library).
  The shared checkout has the other task's uncommitted docs/archive/license/performance changes and remains untouched.
  That development pin is not a validated release pin: CI 33965482623 passed validation, x64 Release and both ARM64
  configurations but failed the x64 Debug Menu owner-message-flood test. Do not combine jobs from different revisions.
- Implemented native retained live controls and offscreen screenshots for ten sizes. Inspected minimum and half-screen
  views, then improved large-view typography/device labels. Synthetic AVControlTests passed **4,538 checks** after
  those updates and the new helper fault tests (Debug). This is not whole-feature/platform acceptance.
- Added a process-isolated audio backend and explicit synthetic helper fixtures. Broker build passes with zero
  warnings. The protocol/child fault tests prove timeout/cancellation/crash/invalid-reply containment and idle waiting;
  no real hardware mutations were performed.
- Remaining: profile chooser/editor and transactional coordinator; module/DLL/host integration; event subscription
  lifetimes; real virtual-camera media source, packaging and interoperability; keyboard/IME/UIA transport; settings
  catalog/schema/templates; cross-configuration and full regression/resource/platform evidence; DxUi Menu CI fix and
  final release pin. Preserve G1/G2/G3 until evidence passes.

Subsequent checkpoint: `ProfileTransaction` and failure injection tests are implemented. The AV Debug suite passed
4,645 checks; the full root Debug regression also passed, including host integration, hidden WARP and both crash
harnesses. A subsequent small change keeps the broker notification event stable across process restarts and adds a
test for that lifetime; rebuild/run that change before considering this checkpoint current. The full coordinator,
native editor, module registration and camera route remain unfinished.

#### Current checkpoint: module and shared coordinator

- Implemented the module coordinator, event-driven helper observation, coalesced safety/level lanes, communications
  route projection, native chooser/editor, DLL factory integration and shared supplied-device graphics pool.
- Added generic keyboard focus/Tab traversal, UTF-16 characters, F6 profile entry and committed raise/dismiss results.
  Added the bundled catalog, host settings validation, schema and real examples in both settings templates.
- AV Debug tests passed 4,988 synthetic checks, including loading the actual DLL, explicit synthetic helper IPC,
  two instances sharing observation, mute readback, profile raise/dismiss, idle composites, and unsaved draft recovery.
  Audio revision tests cover fractional changes, change-back, independent mute and duplicate callback/readback.
- Full Debug regression initially found an outdated gallery widget-count assertion after AV was placed. The
  assertion and additional host settings/profile tests are under revalidation; that run is not recorded as a pass.
- DxUi touch selectors and the coordinated documentation/workflow changes are being validated together. Its
  pre-existing native Menu CI failure still blocks a release pin; local native capability skips do not close it.
- Remaining: real camera media source/capture/packaging and G2 interoperability; generic IME and UIA, wheel and
  theme changes; full configuration/platform/resource evidence; G1 real-device validation; final normative closeout.
  Camera streaming must survive tile hiding independently of display observation. The current audio-only coordinator
  stops its helper when the last view hides; extend that policy before attaching the real camera consumer lifetime.

#### Camera implementation in progress

The standalone Media Foundation source now implements the Frame Server source/stream interfaces, one explicit
1280×720 NV12/30 FPS format, a bounded six-sample allocator, four pending tokens, request-driven timer scheduling,
QPC timestamps, restart and shutdown. Synthetic tests exercise real MF events/buffers without COM registration or
opening a camera. A failed/partial transport frame is entirely replaced with neutral NV12, including padding.

The frame-channel component uses one latest image and a bounded mutex wait. Source-created Session-0 Global objects
grant access only to the user SID, Local Service and SYSTEM; tests explicitly use Local objects. A read lease extends
through media-sample publication. Off/source-change takes the same gate, clears the image and changes its revision;
late old-source frames are rejected. Expired or abandoned producer data yields neutral output. Already delivered
frames retained inside consuming apps/Frame Server cannot be recalled; the gate covers new source publications.

Remaining camera steps, before G2 can pass:

1. Add the COM activation DLL and a Release-only installer/remover into a Local-Service-readable installed directory.
   Mapped development drive paths are not an installation. Use the official Windows 11 virtual-camera API and keep
   normal user privacy consent; no registry-based hardware disabling or camera privacy bypass.
2. Bind an authenticated local named-pipe consumer handshake to the source-created frame channel. Limit clients,
   validate every descriptor/header, reject remote pipes and unrelated object-name pairs, and contain stalled peers.
3. Implement physical capture/selection in the owned helper, with one event-blocked acquisition lane, bounded samples,
   notifications, missing/busy/privacy states, and no physical capture while off or without consumers. Bridge access
   control is per user; keep its consumer lifetime independent of widget visibility.
4. Integrate real camera inventory/capabilities, frame-gate acknowledgements, profile transaction rollback and retry.
   Preserve a stable virtual-camera identity so apps choose RedXe Camera once. Define restart/exit behavior explicitly:
   an active source must fail to neutral if its helper disappears and must not replay its last physical frame.
5. Exercise the installed route in actual capture apps, security/session boundaries, x64/ARM64, unplug/busy/denied,
   off/switch races, driver hangs, process/page exit and active-media resource/latency budgets. Synthetic Media
   Foundation and channel tests do not establish hardware interoperability or package acceptance.

Primary implementation references: [Frame Server custom media source](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/frame-server-custom-media-source),
[Microsoft virtual-camera sample](https://github.com/microsoft/Windows-Camera/tree/master/Samples/VirtualCamera),
and [MF request timer](https://learn.microsoft.com/en-us/windows/win32/api/mfapi/nf-mfapi-mfscheduleworkitem).

#### Camera bridge and activation checkpoint, 2026-09-05

The exact DxUi main pin `4544d3492c33c95c061894646f6a20d28e37cb4c` passed all four native configurations
in [CI 33971459949](https://github.com/RedSalamanders/DxUi/actions/runs/33971459949). The prior dependency CI blocker
is closed. RedXe's full Debug regression passed after correcting published-schema support for bounded Unicode
strings/ASCII identifiers and the placed gallery count. That regression is `.build/av-schema-full-debug.log`;
the new camera pieces below still require the final full-configuration regression.

- Added a fixed four-consumer local named-pipe bridge. The source verifies the pipe's kernel owner SID; the helper
  identifies the peer as Session-0 Local Service before opening its channel. Same-user admission and Local mappings
  are explicit isolated-test construction only, absent from the COM activation attributes. Remote peers, wrong
  versions, stalled handshakes, excess consumers and unrelated mapping/mutex pairs are rejected.
- Added `AVControlCamera.dll`, exposing standard COM factory/activation entrypoints and one stable source CLSID.
  Loading/metadata activation starts no capture and performs no registration. Factory locks, activation/attribute
  identity, repeated activation, detach, source shutdown and DLL unload are covered through the actual DLL.
- Synthetic Debug AV tests passed **5,180 checks** at the activation checkpoint. This includes real MF source events,
  standard sample buffers, channel publication fences and pipe admission/disconnect/restart tests. No physical camera
  or normal user audio settings were touched. These tests do not prove Session-0 installation/interoperability.
- The physical capture adapter now uses an asynchronous Source Reader with one completed-sample mailbox, a reused
  NV12 output image, isolated callback sessions and an owned duplicate wake event. Its real MF reader integration
  tests and the new AV popup row/wheel assertions are being validated; do not record that run as a pass yet.

Still required: connect capture, camera inventory and virtual-device registration to the helper/coordinator; installer
and setup flow; independent media-consumer lifetime; cross-process/service/hardware G2 evidence; IME/UIA/theme work;
remaining G1/G3 and resource/configuration validation; normative closeout. The helper still reports unsupported
camera control until that production integration is complete.

Capture follow-up: Debug AV tests passed **5,253 checks**, including actual asynchronous Source Reader negotiation,
NV12 plane copies, a slow-consumer sample bound and rapid callback-session replacement. Full Debug passed in
`.build/av-camera-full-debug.log`. Release then built cleanly and passed **5,274 AV checks**, including a separate
owned child process transferring real synthetic frame pixels over the authenticated pipe/mapping and exiting when
the consumer disconnected. The complete Release regression is running; native ARM64 AV acceptance is still open.

The new `VirtualCameraRoute` implementation uses dynamically resolved Windows 11 `MFCreateVirtualCamera`, stable
CurrentUser/System-lifetime arguments and the fixed source CLSID. Machine COM registration must point to the
native-architecture DLL below Program Files; metadata loading from the development drive is not deployment.
Closing the API object preserves the persistent device; removal is an explicit operation. This new registration
component has not yet been compiled or connected to the helper at this checkpoint and has not registered a camera.

#### Controller and backend integration, 2026-09-05

Full Release regression passed in `.build/av-camera-full-release.log`. ARM64 Release cross-build passed with zero
warnings/errors in `.build/av-camera-arm64-release-build.log`; this is not native ARM64 AV runtime evidence. Debug
camera-controller integration passed in `.build/av-camera-controller-tests.log` (5,303 AV checks): real synthetic
Source Reader to frame-channel pixels, armed-without-capture, stream start without demand, pause/resume, source
switch, privacy failure, explicit retry, off and repeated shutdown. Subsequent demand-expiry/watchdog/backend changes
are under validation and need refreshed configuration results.

The Windows camera backend is now connected to the production broker, using MF inventory, native source-registration
preflight, the official persistent CurrentUser virtual-camera API and the tested controller. Camera operations and
audio operations retain independent failure results. Selected-device notifications run in the broker while hidden;
quick removal/arrival invalidates intents and turns capture off. Protocol revision 2 suspends audio observation while
retaining an armed camera helper. Driver-call progress wakes the broker's watchdog; no periodic idle watchdog runs.

The installed source/GUI setup and removal flow is still missing; no actual registration or webcam capture has been
performed. Remaining work includes watchdog fault injection, stronger IPC payload validation, retained inventory
priority, IME/UIA/theme, setup packaging, camera/audio hardware/platform G1/G2 evidence, full final resource and
configuration validation, and normative host ABI closeout. Keep this plan open until those items pass.

#### Setup and containment follow-up, 2026-09-05

Debug builds are warning-free and synthetic AV validation now passes **5,456 checks** in
`.build/av-camera-watchdog-debug-tests.log`. New checks reject malformed broker Unicode/IDs, bool representations,
duplicates, revisions and capability fields before publishing state. Explicit isolated child processes exercise
blocked capture Open, ReadFrame, and demand-expiry Close; the watchdog exits each with ERROR_TIMEOUT while the
parent performs no tile observation. Camera busy/privacy/failed state now permits explicit revision-bound retry.

`AVControlCameraSetup.exe`, `package-camera.ps1`, `install-camera.ps1` and `setup-camera.ps1` implement native Release
packaging, machine source installation/removal, and separate non-elevated per-user route registration/removal.
Read the [camera setup guide](../../../Plugins/AVControl/Camera/README.md). Debug package-rejection tests passed;
Release compilation and full package validation are under way. No camera has been installed, registered or opened.
Installed-route setup/rollback and G2 hardware acceptance are still outstanding, as is guided in-plugin setup.

The coordinated DxUi task kept the eleven original AV JSON receipts in the DxUi library archive. Those copies are
DxUi offscreen-fixture receipts, not RedXe product measurements, and are not retained in this repository. RedXe still
consumes the previously validated exact pin; concurrent uncommitted DxUi work is not imported.

Next open work: retained/default inventory priority, generic IME/TSF and UIA, theme changes, guided camera setup,
complete host ABI normative reconciliation, final regression/resource/platform evidence, and real G1/G2 acceptance.
The plan remains WIP; implementation and synthetic evidence alone do not close its release gates.

Release follow-up: `.build/av-camera-setup-release-tests.log` passed **5,457 synthetic checks** and
`.build/av-camera-package-release-tests.log` passed **12 package validation checks**. The identified x64 Release
package passed `install-camera.ps1 -WhatIf`, including native architecture, old-registration ownership and protected
target-directory checks, without writing machine files or registration. Actual installation/rollback remains unproved.

The host preparation record now carries cached appearance (56 bytes total), populated only on initialization and
Windows appearance notifications. AV applies normal light/dark or exact high-contrast surface/foreground/selection
colors without animation-only frames. New light/high-contrast WARP captures and clean-cache assertions are under
validation; no pass is recorded until the current run completes. Generic text/IME and UIA remain open.

#### Appearance and inventory follow-up, 2026-09-05

Appearance tests passed 5,495 synthetic AV checks in `.build/av-appearance-debug-tests.log`, including an exact
opaque high-contrast background check. A shared DxUi primary-button contrast defect is being fixed in the isolated
`Z:/src/DxUi-worktrees/av-high-contrast` checkout, preserving the completed DxUi task's uncommitted sample/docs work.
All 18 x64 Release library suites passed, with nine recorded Menu capability skips. Matched performance acceptance
is still open: timing failures coincided with other active compiler jobs; the original baseline and diagnostic logs
are retained. Do not adopt the new library source or label the performance gate passed yet.

The latest warning-free Debug build and `.build/av-inventory-debug-tests.log` passed **6,215 synthetic AV checks**.
Protocol revision 3 carries the bounded union of loaded-widget profile bindings. Inventory now retains all audio
defaults and the selected camera before profile references and ordinary devices. Audio membership is finalized
before subscription replacement, and the profile view explicitly reports overflow. Tests cover late enumeration,
changed defaults, independent device-class capacities, malformed preferences, actual helper overflow transport,
64-widget registration, duplicate binding reuse, definition changes and retirement while hidden. Real-device
inventory/watch hotplug behavior, release/platform regressions and the remaining IME/UIA/setup/G1/G2 gates remain.

Injected backend follow-up: `.build/av-audio-hotplug-debug-tests.log` passed **6,313 checks** with a warning-free
Debug build. The production audio backend now runs against explicit fake MMDevice/EndpointVolume COM interfaces
in component tests: full subscription arrays, late default/reference admission, eviction before replacement,
generation changes on re-admission and rapid same-ID remove/add, external volume change-back, mute-preserving
readback, default-policy suppression, local read denial and complete callback teardown. A failed enumeration marks
live and listed endpoints Unknown, and default-role notification counters commit only with a successful refresh.
The visible profile overflow heading and accessible explanation are checked at full and minimum sizes.

The isolated DxUi contrast fix is committed as `1947a5b91beb029e9b99d71e0893c6075bbb29ca` on
`codex/av-high-contrast`, with all 36 local suite receipts, nine Menu capability skips per configuration, both ARM64
cross-builds, regenerated/reviewed gallery and repository validators. Debug passed against its retained baseline.
An additional Release baseline/candidate pair with the same owned-process CPU affinity passed unchanged comparison
thresholds; unrestricted timing also varied for unchanged source. DxUi's library archive
`docs/measurements/primary-high-contrast-2026-09-05/README.md` retains raw comparisons, earlier failure logs and
the limits of this evidence. GitHub CI is pending; RedXe still consumes the previously validated 4544d34 pin.

Latest contrast adoption: CI 33980767827 passed all four native configurations and 33980767838 passed formatting.
RedXe now pins the exact `1947a5b91beb029e9b99d71e0893c6075bbb29ca` source. Its full Debug rebuild and all root
regression checks passed in `.build/av-contrast-integration-debug.log` (zero compiler warnings/errors); Release is
running. The entrypoint now explicitly returns success after all assertions pass, so an expected nonzero child exit
from its process-preflight tests cannot leak into a calling script. No real endpoints or camera setup were changed.

The next isolated library branch adds revision-checked embedded text snapshots and composition preview/commit/cancel.
The new embedded tests and inherited Debug suites passed before the independent-sample integration; undo/history
and thread tests are being added. The coordinated v2 sample is preserved, and a matching unchanged-library fixture
is being built for performance comparison. Host-side TSF/IME/UIA, guided setup and hardware/resource gates remain open.

Latest local validation: both full RedXe x64 root suites passed at 1947a5b, and both ARM64 configurations built with
zero warnings/errors. Follow-up AV suites passed 6,328 Debug and 6,330 Release checks after adding an exact pixel
assertion for the muted card's system highlight/text pair and a non-clipping minimum-width overflow caption.
Both new captures were visually reviewed. Root skill validation passed all ten skills.

The isolated input branch now passes all 18 x64 DxUi suites in each configuration (six Menu capability skips each),
including 1,569 embedded checks. Both complete-suite measurements passed unchanged thresholds against retained,
matching baselines. Earlier failed/mixed measurements remain archived. The independent benchmark entry was isolated
equally in baseline/candidate and added to their fixture hashes; older hashes are never compared to the new driver.
Text tokens reuse the existing view revision, adding no second counter write to ordinary invalidation. Further
library ARM64 builds/CI are in progress; this input source is still outside the RedXe pin.

Consumer integration must cancel an active composition before LiveView captures a retained profile draft for device
loss, and route Escape to composition cancellation before closing profiles. The host still needs OS focus,
TSF/IME/clipboard and UIA attachment. Character forwarding and the new retained-tree API alone do not satisfy them.

The embedded input component and independent samples are committed on DxUi `codex/av-input-services` (1acd177),
with relocated-consumer path fixes through ad88833. The standalone relocated Release consumer rendered both
examples and rejected five invalid pins. All local tooling validators passed. Native CI is still running; RedXe
continues to pin 1947a5b. A follow-up native text-store lifetime fix now covers callbacks that destroy controls,
change focus/text or throw; its targeted NativeTextInput suite passes. Wider performance acceptance remains under
investigation: two runs exceeded different timing bands, and an unchanged ad88833 worktree is being built for an
adjacent comparison. Preserve every original receipt and do not silently rebaseline.

The generic text transport uses a 9,304-byte POD snapshot with 4,096 UTF-16 units, 256 clause boundaries, revision
validation and widget-local physical geometry. Host/plugin conversion tests cover malformed Unicode, ranges, flags,
capacity and DPI. These changes do not yet expose AV's text interface or claim functioning OS text/UIA services.

The next transport revision is 9,312 bytes and adds a stable focus-session identity plus revision-checked point/range
geometry. Common/DxUiTextTransport.h is shared by both sides. RedXe now has source for an application-owned TSF client,
lazy message-loop attachment, clipboard routing, prepared-layout notifications and cancellation on hide/focus/resize/
page/device lifetime. AV exposes the generic interface and cancels preview before retaining profile drafts. New AV
tests cover stale edits, replaced focus, Escape-before-Back and uncommitted device-loss preview. These integration
changes require the new DxUi pin and have not yet been built; the last built RedXe state passed 6,360 checks with
the focus-ID transport alone. The canonical dependency remains 1947a5b until library validation is accepted.

The library component's final x64 Debug/Release suites pass; ARM64 builds are running. Its public-only text sample
rendered Unicode using a hidden TSF attachment and private clipboard; Embedded passes 1,588 checks with zero warm
composition allocations. Declare the next common-fixture comparison before measuring: A1/B1/B2/A2 in both Debug and
Release, where A is the retained ad88833 baseline and B is the current component, both on the same v2 harness and
owned-process affinity. Both nearest pairs must pass unchanged comparison thresholds. Keep every raw run and failed
comparison. This evidence cannot replace the outstanding real IME/UIA, hardware, presented-resource and camera gates.


#### Current continuation: embedded accessibility and minimum-size profile access

DxUi text-services commit 2ca8cc03aae08f943d00abdfeddfe6f87f7800cd passed native CI 33988110749 in all four
configurations and format CI 33988110763. Its relocated Release consumer rendered three public examples and rejected
five invalid pins. The canonical RedXe pin remains 1947a5b while the retained matched-performance gate is open.
The isolated consumer is Z:/src/RxAv; its local dependency lock is a validation override, not accepted adoption.
Full RedXe Debug regression passed with the generic text integration and confirmed-toggle semantics in
.build/av-text-root-debug-tests2.log. Release, ARM64 and final resource/real-device gates need refreshing after UIA.

The minimum native layout now keeps a direct Profile button plus all five live controls. Compact sliders replace
paired steppers, with 48-pixel hit height and 92-pixel microphone width at 160x180. The updated layout, one-tap profile
tests and confirmed mute semantics passed 6,118 synthetic checks in .build/av-minimum-profile-debug-tests.log;
the smaller assertion count reflects six targets replacing the previous seven-target stepper grid. Native captures
at 160x180, 320x180 and 640x360 were visually reviewed. Mockups/av-control.html and the existing review wrapper are
aligned; its headless minimum-layout check passed all six target bounds, overlap, profile-open and focus-return
checks at 160x180, 320x180 and 640x180, with no JavaScript errors.

On the coordinated isolated DxUi branch, the initial shared UIA adapter passes 1,709 embedded checks. It reuses
existing Toggle/RangeValue/Value/Text providers, uses the owning COM STA (including an actual marshaled cross-apartment
test), transforms physical-screen bounds once, rejects detached/hidden/replaced controls, and gives replacements
distinct runtime IDs. One thousand clean accessibility updates allocate nothing. UIA range edits now deliver the
normal committed callback; model SetValue remains silent. Prepared-state change notifications and a coalescible
application completion callback are implemented, but full validation and RedXe's COM/root adaptation remain open.
Inherited native Accessibility and NativeTextInput suites pass. A mistyped Controls suite filter was rejected; its
failed invocation is retained and cannot count as a full suite pass. The pending full matrix uses the default list.

Still required: finish/test the RedXe UIA root, generic COM site, queued focus/navigation and provider module lifetime;
guided in-plugin camera setup; actual IME/touch/screen-reader behavior; paired resource acceptance and long-run/presented
measurements; real G1 audio-policy and G2 camera installation/client/platform evidence. This plan remains active.

#### UIA integration validation and next resource comparison

The RedXe generic COM site/root, prepared publication, deferred focus/navigation and provider module lifetime now
pass the isolated AV suite. Full RedXe x64 Debug and Release root suites passed with DxUi 5b366f2; logs are
.build/av-uia-full-debug.log and .build/av-uia-full-release.log. A real OS UIA MTA client discovers the AV output
slider through WM_GETOBJECT and receives its confirmed range-value event. All three Toggle patterns retain confirmed
state until worker acknowledgment. Tests also retain a disconnected provider after plugin shutdown and loader-owner
release. Real screen-reader/IME/touch usability and hardware acceptance are still open.

DxUi 26459b4b25c8573fccf049d4947a1d221f64b182 fixes the public accessibility header's standalone COM prerequisites;
all 18 local suites in each x64 configuration, both ARM64 cross-builds and validators passed. Its exact-commit CI is
pending. Preceding 5b366f2 CI passed x64 Release and both ARM64 jobs, but x64 Debug Menu failed. The retained trace
shows the intended posted move reached and hovered row 1 before an OS-generated move at the real cursor cleared it.
The cooperating DxUi task is reviewing a deterministic fixture correction; the failure is not waived.

Declare the next comparison before running it: EmbeddedUia-20260905-01, A1/B1/B2/A2 in Debug then Release. A remains
the retained ad88833 text-services baseline; B is 26459b4, both using the identical v2 benchmark inputs and owned-process
affinity 0xFFFF. Run after compilers/linkers stop. Keep every raw receipt, both nearest-pair comparisons and same-source
controls, with unchanged thresholds. This measures cumulative library changes with accessibility inactive; it does
not replace explicit active-UIA, AV idle/hidden, presented-frame or camera-resource acceptance. Prior failed comparisons
remain archived and unresolved; no canonical dependency adoption follows from an unpaired or ambiguous result.

Camera setup guidance now renders six steps through the profile chooser, with minimum-size captures reviewed.
It explains the existing package/setup tools and app-side route selection; it does not perform installation. Large
layout typography and all six minimum captures have now been visually reviewed. The latest guided Debug synthetic
suite passed 7,370 checks. Machine install/rollback and usable release distribution remain G2 work.

#### Saved continuation, 2026-09-05 21:15 UTC

Work is paused at the user's request. The declared EmbeddedUia ABBA run completed: all four matched pairs flagged
different metrics, and same-source controls also vary. Acceptance remains OPEN; 24 JSON reports and the run log are
retained in the isolated DxUi Measurements/TextInput/EmbeddedUia-2026-09-05 archive. A longer fixture is proposed only.

The isolated owner-flood Menu fixture now uses a scoped thread hook. Both deliberate negative variants failed as
intended, sources were restored byte-for-byte, and the restored Release Menu suite passed. Production Menu has no
diff. The uncommitted test changes still require final validation and CI.

At 26459b4, CI 33991448220 passed; another run of the same commit, 33991599173, failed x64 Release in the separate
split-button Refine-row hover test. Other native configurations and both format runs passed. Preserve the failure.
Canonical RedXe now pins DxUi `main` at `3208083836a89d2c3348e4389b105cf3c2b453fc`, whose public headers match
`26459b4`. That restores compilation of the in-tree text/UIA adapters against `EmbeddedHost::CancelTextInput` and
`DxUi/EmbeddedAccessibility.h`. DxUi closed library extraction, the first-consumer pin, and synthetic adapters;
matched performance, real IME/AT, hardware, and resource gates remain open on this plan. This is not AV closeout.
