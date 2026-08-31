# WIP: Desk Clock Direct3D split-flap plugin

Status: `ACTIVE`
Created: 2026-08-31
Owner: RedXe plugin, settings, scheduled frame delivery, and Direct3D rendering

## Purpose

Create a bundled low-resource Direct3D 11 desk clock that displays local time and date as a clean split-flap clock.
The user-provided image establishes the composition: six large red digit cards on black, two colon separators, and a
small centered date line. The image contains no implementation instructions and is not a runtime asset.

RedXe MUST create original shaders, geometry, glyphs, colors, proportions, and tests. It MUST NOT embed, trace, or
redistribute pixels from the reference. The vertical overflow menu shown in the image is outside the widget.

This WIP is a non-normative implementation plan. Before closeout, durable behavior MUST move into:

- `Specs/Plugins/Plugins_API.md` for the bundled plugin and rendering/scheduling contracts;
- `Specs/Core/Core_Settings.md` and the Desk Clock static plugin schema for settings behavior;
- `Specs/Core/Core_PerformanceAndResources.md` only if the shared scheduled-frame work adds a cross-cutting rule;
- `Specs/UI/UI_Dashboard.md` only if generic active-page scheduling behavior changes; and
- `Specs/Settings.schema.json` plus both shipped templates under `Settings/`.

## Relationship to the Studio Clock WIP

[`StudioClock_Direct3DPlugin_2026-08-31.md`](StudioClock_Direct3DPlugin_2026-08-31.md) remains a separate dot-matrix
clock proposal. Desk Clock uses different plugin IDs, settings, visuals, assets, and tests.

Both clocks need a low-cadence widget to wake at a wall-clock boundary without a plugin timer or permanent continuous
rendering. The plans MUST implement one shared `IRedXeScheduledWidget` sibling interface and one host deadline path,
not competing IIDs or duplicate schedulers. Before implementation begins, one WIP MUST be selected as the owner of
that shared prerequisite; the other records it as a dependency. Desk Clock additionally uses the same mechanism for
its short smooth-animation frame burst.

## Proposed outcome

- Add `Plugins/DeskClock/DeskClock.dll` as a bundled settings-visible plugin.
- Advertise plugin ID `builtin.desk-clock` and widget type ID `desk-clock`.
- Implement `IRedXeWidget` and the sibling `IRedXeGpuWidget` rendering mechanism without changing either published
  interface.
- Reuse the shared optional `IRedXeScheduledWidget` proposal for second-boundary wake-up and animation frames.
- Display zero-padded 24-hour `HH:MM:SS` and invariant-English `ddd DD MMM` local date by default.
- Animate only the digit cards whose value changes, including simultaneous multi-digit rollovers.
- Keep the Release first-page Matrix Rain composition unchanged and add Desk Clock to the gallery page.
- Demonstrate Desk Clock in both Debug and Release templates, as required for every settings-visible bundled plugin.
- Apply valid settings changes through the existing transactional settings reload path.

## Product behavior

### Visual contract

1. The widget renders an opaque black background and a centered horizontal `HH:MM:SS` clock.
2. Six individual rectangular cards form three two-digit groups. Two fixed colon separators divide hours, minutes,
   and seconds.
3. Each card uses a saturated warm-red face, a white high-contrast glyph, slightly rounded corners, and a dark
   horizontal hinge seam at half height.
4. A centered date line below the cards uses the exact invariant-English form `ddd DD MMM`, for example
   `Mon 31 Aug`.
5. The cards are the dominant element. The date remains subordinate and does not compete with the digit height.
6. Group gaps are larger than within-pair gaps. Colon dots are vertically centered on the digit optical center.
7. The complete composition preserves its design aspect ratio, scales uniformly into any valid widget viewport, and
   is centered in unused space. Cards are never stretched independently.
8. Viewport/DPI-derived card bounds, glyph scale, gaps, corner radius, seam width, and date origin are cached until
   their inputs change.
9. Very small or degenerate viewports fail the widget frame safely rather than clipping into another widget or
   submitting invalid geometry.
10. The first version has no bezel, logo, alarm indicator, overflow menu, reflection, bloom, texture noise, or
    simulated enclosure.

Canonical default colors are black `#000000`, card red `#FF3B43`, digit white `#FFFFFF`, and date gray `#D8D8D8`.
The hinge seam and angle-dependent flap shading are derived from the configured card color rather than independently
configured.

### Split-flap transition

- A time boundary first captures one immutable old digit set and one immutable target digit set. Every changed tile
  starts together; unchanged tiles remain static.
- The default transition lasts 420 milliseconds and finishes before the next one-second boundary.
- During the first half, the old upper half rotates forward from 0 to 90 degrees around the horizontal center hinge.
  The new upper half is stationary behind it while the old lower half remains visible.
- During the second half, the new lower half rotates forward from 90 to 0 degrees over the old lower half. The new
  upper half remains stationary.
- The moving half uses perspective-correct projection, clips to its card, keeps the hinge position fixed, and applies
  bounded angle-dependent darkening and a narrow hinge shadow. It does not use an off-screen blur or multipass effect.
- Each half uses monotonic ease-in/ease-out timing with no bounce, overshoot, discontinuity, or fixed per-frame angle
  increment. Progress accumulates the host's measured frame delta in bounded double-precision state so missed frames
  advance by elapsed time rather than by frame count.
- At completion, the card becomes one static target digit with no subpixel seam mismatch or retained moving face.
- Rollovers such as `09` to `10`, `59` to `00`, and `23:59:59` to `00:00:00` animate every changed tile in one
  synchronized transition.
- The date line cross-fades once when the local calendar date changes; it is not rendered as additional flap cards.
- A new wall-clock target detected during an active transition replaces the target deterministically. The plugin
  never queues or replays a backlog of missed flips.

### Time and date semantics

- Time comes from the Windows local system clock and follows local time-zone and daylight-saving changes. The plugin
  performs no network synchronization and does not modify system time.
- Version 1 always uses zero-padded 24-hour `HH:MM:SS`. Twelve-hour mode, AM/PM, alternate calendars, localization,
  alarms, countdowns, and time-zone selection are outside this plan.
- Weekday and month abbreviations are fixed invariant-English strings so the embedded glyph set and output remain
  bounded and deterministic.
- The first visible frame samples current local time. Subsequent samples occur only at a cached second boundary, on
  an explicit system time/time-zone invalidation, or after visibility recovery.
- After a hidden, minimized, suspended, display-off, or occluded gap, the first visible frame snaps directly to
  current time and establishes a new boundary. It does not animate through missed seconds.
- A manual clock change or daylight-saving transition while visible animates the changed time digits once and updates
  the date when required.
- Invalid or unexpectedly large frame deltas end the active transition at its target and resynchronize; they never
  produce extrapolated rotation or a catch-up loop.

## Plugin and settings contract

### Identity

| Field | Proposed value |
| --- | --- |
| DLL | `DeskClock.dll` |
| Plugin ID | `builtin.desk-clock` |
| Plugin display name | `Desk Clock` |
| Widget type ID | `desk-clock` |
| Capability | `RedXePluginCapabilityWidgetProvider` |
| Rendering interface | `IRedXeGpuWidget` |
| Scheduling interface | shared proposed `IRedXeScheduledWidget` sibling IID |

Desk Clock exposes the generic widget, GPU rendering, and scheduled-frame interfaces as siblings on one controlling
`IUnknown`. It adds no clock-specific public ABI and does not grow `IRedXeWidget` or `IRedXeGpuWidget`.

The descriptor does not set `RedXeWidgetFlagContinuousAnimation`. A permanently continuous clock-only page is not an
acceptable fallback for version 1 because the visual state is static between the short per-second transitions.

### Settings proposal

The static plugin contract publishes a closed Draft 2020-12 object schema and this complete defaults object:

```json
{
  "flipDurationMilliseconds": 420,
  "backgroundColor": "#000000",
  "cardColor": "#FF3B43",
  "digitColor": "#FFFFFF",
  "dateColor": "#D8D8D8"
}
```

| Member | Type | Allowed values | Default | Effect |
| --- | --- | --- | --- | --- |
| `flipDurationMilliseconds` | integer | 250 through 800 | 420 | Complete two-phase tile transition duration |
| `backgroundColor` | string | exact `#RRGGBB` | `#000000` | Opaque widget background |
| `cardColor` | string | exact `#RRGGBB` | `#FF3B43` | Static and moving card base color |
| `digitColor` | string | exact `#RRGGBB` | `#FFFFFF` | Time digit and colon color |
| `dateColor` | string | exact `#RRGGBB` | `#D8D8D8` | Date-line color |

- Members are optional because the host merges the published defaults before provider creation. Unknown, duplicate,
  malformed, or out-of-range values reject the complete candidate settings document.
- Colors accept case-insensitive hexadecimal digits and are converted once to normalized numeric values during
  provider creation. The plugin retains no source JSON or borrowed string.
- The existing V2 factory envelope carries effective settings. No mutable configuration interface is added.
- A valid live edit stages a new provider/widget and commits transactionally. Failure preserves the previous clock.

## Scheduled-frame behavior

Desk Clock reuses the scheduled-widget contract proposed by the Studio Clock WIP. This plan adds these consumer
requirements without defining a second public interface:

- While static, the widget requests the next local second boundary.
- While any tile or date transition is active, it requests the contract's minimum one-millisecond delay; the host's
  existing `Present(1)` path and maximum frame latency of one pace the burst to the display.
- When transition progress reaches one, the next request returns to the following second boundary.
- The host coalesces deadlines across only the current page and any staged adjacent swipe page. It blocks on messages
  or the earliest monotonic deadline and never polls.
- Hidden, minimized, suspended, display-off, occluded, inactive-page, and teardown states retain no Desk Clock
  deadline and produce no plugin-owned wake-up.
- If another visible widget already makes the page continuous, Desk Clock performs no extra wake-up or time query. It
  reuses cached constants except when a second boundary or active transition changes visual state.
- A failed schedule query is isolated and must not cause a zero-delay retry loop.

## Direct3D rendering design

### Glyphs and geometry

- Use an original embedded single-channel signed-distance-field atlas containing digits, colon dots, and only the
  bounded Latin characters required by the invariant date strings.
- Keep the atlas at or below 128x128 `R8_UNORM` and 32 KiB of embedded source payload. Record its generation source
  and regeneration command in `Plugins/DeskClock/README.md`.
- Compile Shader Model 5.0 HLSL at build time and embed stripped bytecode. Do not link or load a runtime shader
  compiler, DirectWrite, WIC, a font file, or a loose image.
- Generate the opaque background triangle and all card/flap quads from `SV_VertexID`. Use no vertex or index buffer.
- Use one fixed instance layout for static card halves, colon/date glyphs, and moving flap faces. The shader selects
  old/new glyph halves, rotation, clipping, lighting, and layer from bounded constants and `SV_InstanceID`.
- Use no off-screen render target, depth texture, MSAA surface, post-process pass, or per-tile texture.
- Bound the complete composition at 64 submitted instances, including static halves, moving halves, separators, and
  the longest date string.

### Time sampling and uploads

- Sample local system time once on first visibility and at most once per displayed second thereafter, except for an
  explicit time/time-zone invalidation or recovery resynchronization.
- Derive the next boundary from the sampled milliseconds and convert it immediately to a host-owned monotonic
  deadline; do not compare wall-clock values in a busy loop.
- Store old and target digits, changed-tile mask, transition start, date codes, configured colors, and cached layout in
  one bounded CPU record.
- During an active transition, update at most one fixed dynamic constant buffer per rendered frame. Outside a
  transition, upload only when time, date, settings, viewport, DPI, or device generation changes.
- Rendering performs no heap allocation, string formatting, locale lookup, date parsing, or resource creation.

### Frame submission

Each non-zero rendered frame performs at most:

1. one fixed-size constant-buffer map/unmap when cached visual state changed;
2. one opaque background draw;
3. one static card-face draw;
4. one static glyph/separator/date draw; and
5. up to two ordered moving-flap draws while a transition is active.

Every callback explicitly binds the D3D state it depends on. Host render-target and viewport rebinding between widgets
remains unchanged.

### Device lifecycle

- Provider-owned reusable resources are the embedded shaders and atlas, one dynamic constant buffer, sampler, and
  immutable blend, rasterizer, and depth states. Compatible instances share them.
- Widget-owned state is bounded validated settings, current/target time, transition state, cached date codes, and
  layout values.
- `OnDeviceCreated` creates the complete device-resource set transactionally.
- `OnDeviceLost` releases every device resource idempotently before the host releases its device.
- Partial creation failure releases temporary resources and leaves the provider device-lost.
- The plugin never receives or retains the HWND, swap chain, back buffer, render-target view, immediate context, frame
  record, or scheduling host internals.

## Mandatory performance and resource budget

| Area | Acceptance budget |
| --- | --- |
| Static visible cadence | One scheduled wake at the next displayed second boundary |
| Animation cadence | At most one frame per presentation interval for no more than the configured duration |
| Hidden/inactive cadence | Zero plugin-owned periodic wake-ups, frames, or presents |
| Local-time sampling | At most one ordinary sample per displayed second |
| Frame-path heap work | Zero allocations and zero frees |
| Frame-path synchronization and I/O | None |
| Constant upload | At most one map/unmap and 512 bytes per changed frame |
| Draw submissions | At most five draws per instance; at most three while static |
| Submitted instances | At most 64 |
| Texture uploads | None after device creation |
| Runtime shader/font work | None |
| Embedded atlas payload | At most 32 KiB |
| Plugin CPU state | At most 32 KiB per provider and 4 KiB per widget, excluding COM/runtime overhead |
| Threads, timers, HWNDs, and off-screen targets | None |

Release measurement at 2560x720 MUST record clock-disabled versus clock-enabled CPU submission time, D3D timestamp
time, private bytes, working set, steady allocation deltas, static and animation wake/frame counts, and hidden wake
count. A missed structural budget blocks closeout; a measured regression requires explicit approval and contract
justification.

## Error and isolation behavior

- Malformed settings fail static validation or provider creation without replacing the active dashboard.
- Invalid frame prefixes, null borrowed pointers, unsupported device state, non-finite timing, invalid dimensions, or
  a zero-sized viewport fail safely without a draw or dereference.
- Missing or invalid embedded shader/atlas data fails provider device initialization with the original `HRESULT`.
- One clock render or scheduling failure does not prevent later widgets from rendering and never creates a retry loop.
- Device loss and recreation preserve validated CPU state, cancel any in-flight visual transition, resample current
  time on recovery, and rebuild only device-owned resources.
- Capacity exhaustion fails one widget frame locally; it never grows storage or corrupts another widget.
- The first implementation supports the existing D3D11 feature-level floor and hidden WARP path.

## Implementation anchors

- Shared scheduling interface: `Common/PlugInterfaces/ScheduledWidget.h`
- Plugin implementation, atlas source, and build-time HLSL: `Plugins/DeskClock/`
- Module discovery and interface lifetime: `RedXe/PluginManager.*`
- Deadline aggregation and active-page scheduling: `RedXe/DashboardHost.*`
- Message-aware deadline wait and time-change invalidation: `RedXe/Application.*`, `RedXe/FrameScheduler.h`
- Static plugin schema integration: `Specs/Settings.schema.json`, `RedXe/Settings.*`
- Shipped examples: both files under `Settings/`
- Contract, settings, and deterministic WARP tests: `Tests/PluginContractTests/`, `Tests/SettingsTests/`
- Production scheduling, reload, recovery, and soak tests: `Tests/HostPluginTests/`

The plugin project is added to all four solution configurations, copies its DLL under the host output `Plugins`
directory, and is referenced by RedXe for build ordering only, without static import-library linkage.

## Required validation

### Contract and settings

- Verify factory null outputs, unknown IDs, unsupported IIDs, V1 defaults, V2 effective settings, the 4096-byte
  configuration bound, synchronous copying, and static schema/default validity.
- Verify controlling-`IUnknown` identity and reference counting across `IRedXeWidget`, `IRedXeGpuWidget`, and the
  shared scheduled-widget interface. Existing plugin IIDs and vtables remain unchanged.
- Verify duration endpoints, every color setting, accepted mixed-case colors, malformed colors, duplicate/unknown
  members, and transactional valid/invalid live reload.
- Verify both shipped templates reference Desk Clock and that an inactive gallery page creates no Desk Clock provider,
  widget, device resource, frame deadline, or render work.

### Time, scheduling, and transition behavior

- Inject deterministic local-time samples to cover ordinary seconds, `09` to `10`, `19` to `20`, `59` to `00`,
  `23:59:59` to `00:00:00`, month/year rollover, leap day, daylight-saving/time-zone change, manual rollback, and
  resume after a long gap.
- Prove one static second-boundary wake starts one coalesced animation burst, each burst is presentation-paced, and
  the widget returns to a blocked boundary wait immediately after completion.
- Prove unrelated messages do not start a frame, a continuous sibling adds no clock-owned wake-up, and hidden,
  minimized, suspended, display-off, occluded, inactive-page, and shutdown states retain no clock deadline.
- Verify changed tiles share one transition start, unchanged tiles remain pixel-stable, missed frames use absolute
  elapsed progress, large host-uptime values do not reduce transition precision, and no tick backlog is replayed.

### Rendering and resources

- WARP readback proves configured background, six red cards, white time glyphs, two colons, hinge seams, and the date
  line appear in their expected regions.
- Deterministic snapshots at 0, 25, 50, 75, and 100 percent transition prove correct old/new upper and lower faces,
  hinge anchoring, perspective direction, bounded shading, clipping, and the final seam-free target.
- Snapshot rollovers prove multiple changed tiles animate concurrently and date cross-fade occurs only at a local-date
  change.
- Test landscape and portrait viewports, non-reference aspect ratios, DPI changes, minimum supported geometry, and
  zero-size suspension.
- Verify device loss/recreation, the 64-instance bound, one-upload/five-draw maximum, steady-state zero allocations,
  and absence of runtime shader compiler, DirectWrite, WIC, font, timer, worker, HWND, and off-screen-target use.
- Run a five-minute Release host soak and confirm stable provider/widget/device-resource counts, bounded memory,
  expected boundary/animation frame counts, hidden zero-wake behavior, and complete device-resource release.

### Repository matrix

- Run `./format.ps1`.
- Run `./test.ps1 -Configuration Debug -Platform x64 -Rebuild`.
- Run `./test.ps1 -Configuration Release -Platform x64 -Rebuild`.
- Build Release ARM64.
- Run `./validate-skills.ps1` and `git diff --check`.
- Perform one visual review at 2560x720 on the target display. Confirm hierarchy, spacing, legibility, hinge alignment,
  smooth transition timing, simultaneous rollover behavior, and clean recovery after hide/show.

## Implementation sequence

1. [ ] Freeze the visual defaults, exact two-phase transition, time/date semantics, and ownership of the shared
       scheduled-widget prerequisite.
2. [ ] Add or reuse the one scheduled-widget sibling IID, host deadline aggregation, message-aware wait, and focused
       ABI/scheduler tests without changing published interfaces.
3. [ ] Add the Desk Clock project, metadata, factory/static settings contract, and all solution configurations.
4. [ ] Implement strict effective-settings parsing and transactional host integration.
5. [ ] Create the original bounded atlas and build-time shaders; implement cached layout, time state, and the
       perspective split-flap renderer.
6. [ ] Add Desk Clock to both shipped gallery templates without changing the Release startup composition.
7. [ ] Add deterministic settings, schedule, WARP phase/pixel, rollover, device-loss, import, rollback, and teardown
       coverage.
8. [ ] Measure the resource budget, run the full validation matrix, update every owning normative contract, and
       reconcile the scheduling work with the Studio Clock WIP and active architecture RFC.
9. [ ] Record completion evidence, move this plan to `Specs/Plans/Done/`, and remove its WIP index row.

## Closeout condition

This plan is complete only when implementation and tests pass, the split-flap transition is visually approved,
measured resource behavior satisfies the budgets, one shared scheduled-widget contract is normative, both templates
remain valid, every lasting requirement is merged into its owning domain contract, and the active architecture RFC is
narrowed accordingly. A completed plan MUST NOT remain under WIP.
