# AV Control

Status: normative implementation contract; feature delivery in progress
Last reviewed: 2026-09-07

The active [AV implementation plan](../Plans/WIP/RFC_Plugins_AVControl.md) tracks unfinished integration and release
gates. The browser mockup and synthetic tests are not evidence of working Windows audio or camera backends.

## Configuration and interaction model

The widget accepts exactly `{"profiles": [...]}`, with zero through four profiles and a compact serialized ceiling
of 4096 UTF-8 bytes. Each profile requires exactly id, name, outputId, microphoneId, cameraId, audioRoles,
restoreLevels, outputLevel and microphoneLevel. Reject duplicate/unknown members, wrong types, invalid UTF-8,
embedded NUL, oversized values and duplicate profile IDs ignoring ASCII case. Parse and save are transactional;
failures preserve the previous configuration and never publish partial JSON.

IDs have 1–32 ASCII letters, digits, hyphens or underscores. Names have 1–48 Unicode scalar values and no C0/C1
controls. Opaque device IDs have 1–1024 UTF-8 bytes and are never truncated. Roles are `all` or `communications`;
levels are integer percentages 0–100. Save edits definitions only. No saved setting applies hardware state on load.

A profile is active only when every affected render/capture role and camera source is confirmed. Unknown, missing
or unavailable bindings cannot match. Mute and camera-off do not affect identity. Restored level differences produce
Adjusted. Communications-only matching ignores console and multimedia roles.

Slider gestures retain the endpoint ID, generation, level revision and layout revision at Down. Preview changes
only the local draft. Commit publishes at most once and only while those identities/revisions remain coherent.
Cancel, endpoint replacement, external level change or changed geometry produces no endpoint write. Level changes
never alter mute. Keyboard ranges retain one-point precision, five-point page steps and clamp at 0–100.

## Responsive live view

The three mute/off controls and both levels remain directly accessible in every supported rectangle. Minimum size
is 160×180 logical pixels; smaller placements expose unavailable layout instead of clipped controls. Every live hit
target is at least 48×48 logical pixels. Hit targets do not overlap or extend outside their tile. Mute and camera-off
buttons use state-specific Fluent glyphs (volume/mute, microphone/mic-off) sized to 32 DIP `IconLarge` with 12 DIP
padding inside the card. Camera always uses the video glyph (`E714`), including unavailable and off, and uses
disabled text when the row cannot activate. Level sliders use DxUi's 12 DIP track and 48 DIP thumb and occupy the
full level panel height so the thumb is not a 48 DIP strip at the bottom. The leading icon and level value share a 48 DIP mute hit target that sends the same mute command as the matching
device card whenever the row is wide enough to keep both that target and the slider at 48 DIP.

Widths below 320 or heights below 300 use the minimal layout: three mute/off targets followed by two compact
sliders. A quiet, labeled Profile target shares the microphone row; the output slider retains the full row width.
Both sliders keep at least a 48-pixel hit height by filling their level panel, with compact labels in the leading
mute target. At 160×180, the microphone level row is 92 pixels
wide: a leading mute gutter plus a 48-pixel slider. Profile is 48×48. Profile selection is one tap at every size.
Otherwise widths below 800 with height at least 620 use tall
rows; widths at least 800 below height 480 use the wide layout; remaining sizes below width 800 or height 480 use
compact layout; others use the large layout. Definitions remain subordinate to selection. Raised content is at most half the
client width and full client height. Pixel-to-DIP conversion belongs at the embedded-host boundary, exactly once.

## Validation

AVControlTests covers strict configuration/roundtrip/failure preservation, Unicode and byte/scalar bounds, profile
matching across role and mute changes, coherent slider drafts, every reviewed rectangle and one-pixel neighborhoods
of all density thresholds, mute-button device names at compact size, icon-only names at the 160×180 minimum, mute/off glyph changes, a video
glyph while the camera is unavailable, and slider-leading mute hits.
Tests use synthetic model state and never change real endpoints, open webcams or write
normal user settings. Backend, native UI, accessibility, hardware interoperability and resource acceptance remain
explicit open gates until their implementation and evidence are added.

## Module, confirmation and profile editing

`builtin.av-control` / `av-control` is supplied by `Plugins/AVControl.dll`, with its private helper and yyjson runtime
staged alongside it. Both shipped settings templates contain a placed gallery example and empty profile definitions.
The strict profile model validates host-loaded settings and factory options as well as editor saves.

One module coordinator shares acknowledged inventory across widget instances. Five fixed coalescing command lanes
prioritize the three mute/off actions before the two levels. A profile transaction rejects a concurrent profile or
level command; a later mute/off remains accepted and resolves the resulting controlled route before execution.
Communications-only profiles select the communications endpoints for the subsequent live controls. Displayed state
changes only after helper readback. Failure is visible and never presented as a confirmed mute or successful profile.
Device revision changes rebuild selector options; volume notifications do not rebuild the profile form.

The shared inventory admits at most 32 outputs, 32 microphones and 16 cameras. It reads all three audio defaults
before admission, prioritizes defaults and the selected camera, then saved bindings from every loaded AV widget,
then ordinary devices in enumeration order. Defaults are not displaced by a full set of profile references.
References are deduplicated within each device class, bounded to the corresponding inventory capacity, and refreshed
when definitions or loaded-widget membership change. The two-page host limit permits 64 registered configurations.
Missing bindings remain missing; they never become another device. Excess references/devices set an explicit
"Limited devices" heading and accessible explanation in the profile view. Audio subscriptions are created only
after final membership selection, retiring evicted subscriptions first even when their devices remain active.
The smallest profile heading uses "Limited", or "!" when an existing-definition editor also needs its Delete
button; the full accessible explanation remains available. The indication must fit without shrinking either action.

The editor keeps its unsaved name, bindings and page through a graphics-device rebuild. Cancel discards edits. Save
persists only the definition through host services, and failure leaves the draft repairable. Generic committed input
results request half-screen raise or dismissal after dispatch; the widget never owns or receives the main HWND.
The keyboard mechanism supports Tab/Shift+Tab, activation, range keys, Escape and UTF-16 character delivery. F6 opens
profiles as a keyboard shortcut at every size. Real composition/IME and screen-reader acceptance remain open;
character forwarding and synthetic UIA tests do not establish them.

The shared embedded UIA adapter exposes all three confirmed-state toggles, committed scalar ranges, profile actions
and editor text through the generic host accessibility site. Pending commands do not change the accessible toggle
state. OS UIA discovery and range property-change delivery are tested with an MTA client and a hidden synthetic AV
fixture. Retained providers reject calls after view replacement, device teardown and module shutdown. Real screen
reader, touch and IME usability, plus matched resource acceptance, remain release requirements.

Camera setup guidance is subordinate to the profile chooser. Its additional Camera setup entry pages after saved
profiles and opens six bounded steps: Windows requirement, component installation, current-user registration,
restart, per-app RedXe Camera selection, and choosing a physical webcam/off-state behavior. The minimum layout uses
short instructions with full accessible descriptions; wider views show the detailed instructions. Back and Done
return to profiles, and navigation never changes hardware or profile definitions. This is guidance for the existing
package/setup tools, not an automatic installer or a claim that installation succeeded. G2 still requires the
actual installed workflow, failures/rollback, package distribution and capture-client evidence.

The host forwards vertical wheel input to the topmost interactive view at the pointer, using widget-local physical
coordinates, Win32 wheel units and the final view ID. A wheel sample never starts or terminates pointer capture and
is suppressed during an owned slider drag or a two- or three-finger host page pan. Unsupported widgets return
`S_FALSE`. Paged profile, editor, and camera-setup surfaces host `DxUi::PageIndicator` at the bottom when more than
one page is visible. The strip uses the control's 20 DIP height, does not scroll, and reports `Page N of M`. One-finger
contacts remain with the widget so sliders and the indicator stay usable.

Audio callbacks track exact scalar changes, including changes back before the next observation, separately from
mute revisions. All admitted audio endpoints have bounded subscriptions, including profile destinations and
communications defaults. Repeated identical callback/readback values do not create new revisions. Mutations check
the latest callback revisions immediately before the endpoint write; default-role callbacks also invalidate stale
role revisions. These checks are optimistic: Windows does not expose an atomic cross-device transaction.

Each admitted endpoint also tracks device-state/add/remove notifications in bounded identity slots. A rapid removal
and return with the same ID retires its old volume subscription and increments its generation before accepting
another mutation. Unrelated device notifications do not re-register unchanged endpoints. Failed inventory reads
publish Unknown and do not consume default-role callback revisions; a later successful observation preserves the
intervening change history. Injected-enumerator component tests exercise this production backend without activating
the Windows device enumerator or default-policy client.

## Embedded rendering and isolated device work

`AVControlView` builds retained controls through public DxUi headers. Pin, restore, CRT matching and the COM/POD
boundary are owned by [`Core_DxUiIntegration.md`](../Core/Core_DxUiIntegration.md). RedXe restores an exact library
commit into `.build/dependencies/DxUi/source/<commit>` and isolates outputs by commit, API revision, toolset, SDK
and CRT. `restore-dxui.ps1` never edits or resets the sibling DxUi checkout.
Consumer project references preserve Configuration/Platform when built through the RedXe solution; MSBuild's
default behavior for projects outside the solution would otherwise select Win32.

Each live view retains three toggle cards and two sliders. Toggle cards show a device icon and the confirmed
output, microphone, or camera name, clipped when the label is wider than the card. Mute and camera-off are the
confirmed button state, not a second On/Muted caption. Unavailable and pending captions replace the name. Tiles
too narrow for a name keep the icon and put the name in the accessible label. Level rows show an icon, the integer
level, and the slider, without repeating device titles. Tapping the leading icon or level value sends the same mute
or unmute command as the matching device card. The toggle
pattern reports confirmed mute/off state; activation sends a command without flipping that state optimistically.
A confirmed endpoint change invalidates any stale drag. Changed content is prepared before composition;
steady composition reuses its surface. Reduced motion is used for these immediate live actions, so the view creates
no animation-only deadlines. Tile and raised variants share the module's supplied-device graphics pool.

Appearance is supplied by the generic host preparation record. Light/dark follows the Windows application theme;
high contrast uses the supplied system background, foreground, selection, button and disabled colors with opaque
surfaces. The plugin applies changed palettes only during preparation, reuses clean cached surfaces, and keeps the
same five controls and 48-DIP targets. Toggle icon and name labels follow their confirmed button state so an Off accent
does not leave an unreadable foreground. Theme notifications coalesce a host frame and never start polling.

Device/driver calls execute in `AVControlBroker.exe`, never in the UI process. The parent uses one reusable private
mapping and unnamed request, reply and change events. Only these four handles are inherited through an explicit
process attribute list. The child is created suspended without a visible console and assigned to an owned
kill-on-close job with a one-process limit before it starts. Cancellation, deadline expiry, process death and malformed
replies invalidate that connection. No unbounded thread termination or arbitrary process-name termination is used.
The parent's change-event handle remains stable across connection restarts; its wait must drain before the broker
object is destroyed. Protocol records contain no pointers or owned C++ objects and are capped below 256 KiB. Revision 3
adds a bounded saved-binding union, copied only when it changes or the helper restarts. The added fixed mapping and
two coordinator preference buffers remain within the 2 MiB shared-model budget; no per-frame copy is introduced.
A clean helper waits on its
request event. Callback notifications signal the stable change event; one coalesced UI wake schedules observation.

The audio backend uses MMDevice and EndpointVolume to enumerate active endpoints, read defaults, set scalar levels
and mute, and return observed state. Mutations bind endpoint IDs, generations and field revisions. Default-role
mutation is isolated in `AudioPolicy.cpp`: an undocumented COM adapter using client
`870af99c-171d-4f9e-af0d-e63df40c2bc9`, interface `f8679f50-850a-41cf-9c72-430f290290c8`, and the default-endpoint
slot after ten non-IUnknown methods. Runtime activation determines capability; each changed role is read back.
There is no registry fallback or claim of a documented Windows setter. The identity/slot evidence is the
[published adapter declaration](https://github.com/tartakynov/audioswitch/blob/master/IPolicyConfig.h).
This establishes the candidate implementation, not G1 hardware/OS compatibility acceptance.

The helper now includes the Windows camera backend, asynchronous physical-capture adapter and persistent virtual
route. It reports camera control unsupported when the native source is not installed in the required Program Files
location; privacy denial remains distinct. Command-line source packaging/setup is implemented; guided in-plugin setup
and installed-route interoperability remain unfinished. Synthetic helper mode is explicit test construction, never a profile/settings switch. Synthetic camera
state proves protocol handling only; it is not evidence for AV-05 or G2.

`AVControlCamera.dll` now implements the separate Frame Server COM source. It is built with a static CRT and has no
DxUi or RedXe UI dependency. Loading the DLL and activating metadata perform no registration, capture or background
work. One 1280×720 NV12 stream requests samples at up to 30 FPS, retains at most four pending tokens and uses a
six-sample allocator. Source shutdown breaks source/stream ownership cycles; detached sources keep the DLL alive
until their consumer explicitly shuts them down. Production installation/interoperability acceptance remains open.

The camera transport keeps one latest image per admitted consumer, with a maximum of four consumers. Pipes reject
remote connections, use an explicit owner SID and a protected DACL for that owner, SYSTEM and Local Service, and
bound the entire handshake to 100 ms. The source checks the pipe object's owner; the helper identifies a Session-0
Local Service client before opening a validated paired Global mapping/mutex. This is principal authentication;
same-user hostile processes are not a separate Windows camera-authority boundary. Isolated tests use unique names,
Local objects and explicit same-user admission, which production activation cannot enable.

Frame publication holds the channel mutex through media-sample enqueue. Off/source change acquires the same gate
and invalidates older capture generations. A failed gate never returns a confirmed Off; the affected connection is
closed. A disconnected provider fails immediately to complete neutral replacement. Connected data older than 250 ms
is neutral. Frames already buffered by consuming applications cannot be recalled. No frame history or recording is
retained. The pipe lane blocks on events when idle, and callbacks never call UI or access destroyed owners.

Opening a virtual stream alone does not start physical capture. Sample requests renew a 250-ms demand lease through
one reused overlapped write/event per consumer. One-shot lease expiry releases capture even if no media callback
arrives. The selected virtual stream remains available and resumes acquisition when requests resume. Camera-on
without requesting consumers is armed; off releases the physical reader and then retires the optional pipe worker.
The camera controller's first active frame confirms acquisition through the asynchronous reader and publication gate.
Reader callbacks own an inactive-able mailbox and duplicate event, never an AV backend pointer.

Private broker protocol supports display-observation suspension and a camera-helper lifetime flag. Hiding the
last AV view removes audio observation subscriptions while retaining an armed camera helper; showing a view rebuilds
subscriptions from Windows state. Camera hotplug processing remains event-driven and independent from display
observation. Removal/replacement of the selected camera invalidates prior intents and turns capture off; a device
returning with the same identifier never automatically turns the route back on.

Potentially blocking physical-reader calls publish a 2250-ms deadline to the helper's existing broker MTA. It waits on
request, media-progress and inventory events, with an absolute timeout only while a driver call is active. A stalled
call terminates that owned helper and signals observation; Frame Server then produces neutral frames. This does not
authorize terminating arbitrary processes. Camera failures do not disable independent audio controls.

Temporary camera Busy, Denied and Failed states with a known source/revision keep an explicit retry action. They
retain the last observed error until that activation is acknowledged; reappearance or passive redraw never starts
capture. Missing/unsupported sources remain unavailable. A failed inventory refresh publishes Unknown, including
when it precedes an attempted camera mutation, instead of preserving stale Ready state.

The complete fixed-capacity reply is validated before publication: enum ranges, bool byte representations, Unicode,
ID lengths/termination, duplicate identities, levels, known capability bits and ready-state revisions. Invalid data
quarantines the owned helper and preserves the previous confirmed reply. A helper-reported timeout also invalidates
the connection so a stalled driver cannot remain behind an apparently completed failed command.

Camera packaging uses `package-camera.ps1` and exactly two identified native Release binaries plus a SHA-256 manifest.
`install-camera.ps1` validates those files, architecture, version, paths and old ownership before changing the fixed
Program Files directory and native machine COM source key. It stages replacements and attempts rollback on failure;
locked backups remain explicitly reported. It never recursively deletes directories or terminates other processes.
`setup-camera.ps1` separately invokes current-user registration/removal through the official Windows API, with one
owned process and a 15-second setup deadline. Per-user mutations reject elevation to avoid UAC account substitution.
Check and package validation never create a camera or open physical capture. A local integrity manifest is not a
publisher signature. Installed-route and distributable-package acceptance remain G2 requirements.

The native WARP test generates all ten reviewed size captures below `.build/test-artifacts/AVControl`, exercises
committed/canceled/external-revision pointer gestures, hidden input, negative viewport origins, and 1,000 composites
without new preparations or surface allocations. It also proves the pinned DxUi surface lifetime from the consumer
side: a visible tile reports `surfaceBytes` equal to its extent × 4, a hidden view reports 0 and recreates nothing
while hidden, and showing it again reallocates exactly one surface. The same library rule releases the overlay
surface whenever the widget is not raised, so a settled unraised widget holds only its tile surface. Broker tests cover explicit synthetic inventory, mute-preserving
level readback, stale commands, malformed replies, crash, timeout, cancellation, partial startup cleanup and no orphan
child. Idle synthetic helper CPU must remain below 5 ms over a 120 ms observation. Automated tests never open a real
camera or change actual audio defaults, levels or mute.

`ProfileTransaction` preflights all three bindings and capabilities before any write, snapshots endpoint and role
revisions, and applies the requested roles and optional levels while preserving mute/off. It reserves one third of
the control budget for rollback. At most eleven owned mutations are retained in a fixed undo array. Rollback uses
revision checks on the backend and skips fields affected by newer safety intent or external changes. A failed call
whose observed revision changed cannot be claimed fully rolled back. Unknown connection state is reported separately
from an ordinary rejected operation. Later safety intent supersedes a transaction; the coordinator must prioritize
that intent against its final observed route before permitting any subsequent profile operation.

Synthetic transaction tests inject failure before each of ten exercised mutation stages, verify original roles and
levels after compensation, preserve externally changed values, keep a newly muted destination muted, reject missing
or unsupported bindings without side effects, and verify communications-only application and idempotent reapplication.
