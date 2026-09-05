# AV Control: Direct3D audio and webcam profiles

Status: DECISION — proposed feature specification; no runtime implementation
Date: 2026-09-05
Requested deliverables: specification and interactive UI mockup; shared DxUi integration proposal
Proposed identity: `builtin.av-control` / widget type `av-control` / `AVControl.dll`

## Purpose and authority

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
- [Shared DxUi project proposal](RFC_Core_DxUiSharedProject.md): standalone library, pinned static linkage,
  embedded rendering, input/accessibility adapters and RedXe-first adoption.
- Planned feature owner at implementation: `Specs/Plugins/Plugins_AVControl.md`.

AV Control is the first application feature to consume the proposed independent `Z:\src\DxUi` library. Its native
controls reuse that library; it does not copy `RedSalamander/Common/DxUI` into this plugin or implement a second set
of sliders, selectors and text fields. RedSalamander's later port is independent of AV delivery. DxUi extraction and
host integration remain unfinished prerequisites, not capabilities proved by the browser mockup.

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
| Native control library | Pinned standalone DxUi static targets, with embedded D3D11 hosting. | D0-D3 of the shared-library proposal and G3 below must prove preparation, input, text/UIA and resource behavior before AV ships. |

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

Follow [RFC_Core_DxUiSharedProject.md](RFC_Core_DxUiSharedProject.md) for extraction, ownership and build integration.
Link `DxUi.Controls.lib` and `DxUi.Embedded.lib` into `AVControl.dll` from a pinned source revision through the supplied
MSBuild targets. RedXe's OS-side adapter may link `DxUi.Win32Services.lib`; AV must not link the window-presentation
target or create its own window host. No separate `DxUi.dll` is required by this proposal. No DxUi C++ objects or
STL ownership cross the RedXe COM ABI.

| AV behavior | Shared DxUi primitive / responsibility |
| --- | --- |
| Independent output/mic mute and camera on/off | Toggle-capable buttons with confirmed, pending, disabled and unavailable states; AV supplies actions and acknowledgements. |
| Output volume / microphone gain | Slider with preview, commit and cancel events; keyboard steps and RangeValue accessibility. AV commits the OS setter on release, never on every preview event. |
| Minimum-size level adjustment | Button pairs with the same AV level command and 5% steps; current value remains visible. |
| Profile chooser and device associations | Buttons/ComboBox with stable model IDs; visible names are not identities. |
| Profile definition | TextField, ComboBox, Checkbox and panels; AV retains and validates drafts, saves via the host, and applies only on explicit activation. |
| Density, raised view and device-loss recovery | AV chooses the reviewed layout tier; DxUi measures and prepares the visible controls, preserves logical state and rebuilds graphics only. |

The library must support AV's large touch targets and pressed/pending semantics without a local control fork.
Reusable changes belong upstream in DxUi with tests; XENEON dimensions, profile rules, camera scope and endpoints
remain in AV. Existing `SetOnValueChanged` support alone is not proof of preview/commit/cancel behavior.

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
- [x] Propose the independent DxUi repository and update native AV architecture to reuse its pinned static libraries.
- [ ] **[blocked]** Complete `validate-skills.ps1`: the available Python runtime lacks PyYAML and the package download
  failed TLS authentication. No repository skill failed its content validation; the validator could not start.

Future implementation — remains unfinished and is outside this design deliverable:

- [ ] **G1: audio switching.** Identify/validate a default-policy backend on supported Windows 10/11 builds, x64 and
  ARM64. Exercise all-role and communications-only changes, Bluetooth/USB/HDMI removal, pinned app sessions,
  unsupported devices, access denial, and Audio Service restart. Document any undocumented ABI and compatibility policy.
- [ ] **G2: camera route.** Prove physical-source switching/off/on, stale-frame exclusion, busy/privacy/shutter
  behavior, multiple consumers, app-side initial selection, and graceful failure/crash. Decide OS minimum, installation
  and removal, source/helper lifetime beyond RedXe page/process exit, and measured active-media budgets. Windows 10
  support or its explicit limitation must be resolved before full-feature claims.
- [ ] **G3a: shared DxUi and UI hosting.** Complete the shared proposal's D0-D3 gates: standalone repository and
  extracted controls, pinned static-library build, embedded preparation/composition, capture, keyboard/focus,
  TSF/IME text entry, UIA lifetimes, tile/raised layouts and measured CPU/GPU resource acceptance. Update current
  ABI headers, library/RedXe normative contracts and all consumers together. RedSalamander migration is not required.
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
