# Done: Studio Clock Direct3D plugin

Status: `COMPLETE`
Created: 2026-08-31
Completed: 2026-08-31
Owner: RedXe plugin, settings, frame scheduling, and Direct3D rendering

## Purpose

Create a bundled low-resource Direct3D 11 studio clock inspired by the user-provided reference: large red dot-matrix
hours and minutes, smaller numeric seconds, and a circular 60-step seconds-progress display on a dark panel.

The reference photograph is visual inspiration only. RedXe will create original procedural geometry and must not copy
the pictured product's logo, enclosure, exact industrial design, or other branding.

This completed plan is a non-normative historical implementation record. Durable behavior is normative in:

- `Specs/Plugins/Plugins_API.md` for the plugin and scheduled-widget ABI;
- `Specs/Core/Core_Settings.md` and the Studio Clock static plugin schema for settings behavior;
- `Specs/Core/Core_PerformanceAndResources.md` for any accepted cross-cutting scheduling rule;
- `Specs/UI/UI_Dashboard.md` only if generic active-page scheduling behavior changes; and
- both shipped templates under `Settings/`.

## Request interpretation

The first implementation uses these explicit interpretations of the requested settings:

- `showSecondProgress` controls whether the circular 60-step progress display is rendered.
- `secondsColor` controls both the numeric seconds and the active progress dots.
- `showDate` controls whether a date line is rendered.
- Numeric seconds remain independently configurable through `showSeconds`.

The first version is a 24-hour clock. Time-zone selection, network time synchronization, alarms, countdowns, and
12-hour AM/PM presentation are outside this plan.

## Proposed outcome

- Add `Plugins/StudioClock/StudioClock.dll` as a bundled settings-visible plugin.
- Advertise plugin ID `builtin.studio-clock` and widget type ID `studio-clock`.
- Implement `IRedXeWidget` and the sibling `IRedXeGpuWidget` rendering mechanism.
- Add a generic scheduled-widget sibling IID so low-cadence widgets can request their next frame without continuous
  presentation, a plugin timer, a worker, or plugin access to an HWND.
- Keep the Release first-page Matrix Rain composition unchanged and add Studio Clock to the shipped gallery page.
- Demonstrate Studio Clock in both Debug and Release templates as required for every settings-visible bundled plugin.
- Apply valid settings changes through the existing transactional settings reload path.

## Product behavior

### Visual contract

1. The plugin renders an opaque near-black panel without a simulated metal bezel or third-party branding.
2. `HH:MM` is the dominant central element and uses an original fixed 5-by-7 circular-dot glyph design.
3. When enabled, two-digit seconds appear on a smaller centered line below the primary time.
4. When enabled, the date appears below the time content using the same dot vocabulary and the configured fixed
   numeric format.
5. The seconds progress contains 60 evenly spaced circular positions, starts at twelve o'clock, and advances
   clockwise. At second `s`, positions zero through `s` use `secondsColor`; future positions use the same color at
   18 percent opacity. The display resets to one active top position at the next minute.
6. The progress display and all glyphs use antialiased circular dots with consistent visual weight. No runtime font,
   font rasterizer, or image asset is required.
7. The canonical composition is centered in the largest square that fits the widget viewport. Extra horizontal or
   vertical space uses the background color; the clock is never stretched.
8. Enabling both seconds and date produces two compact secondary rows. Scaling preserves the complete configured
   content rather than clipping or silently hiding a row.
9. The layout remains legible and correctly clipped at every valid viewport and DPI. Degenerate space fails the
   widget frame safely instead of drawing outside its viewport.

### Time and date semantics

- The display uses the Windows local system time and Gregorian local date. The plugin performs no network or device
  time synchronization.
- The primary time is always zero-padded `HH:MM` in 24-hour form. Numeric seconds are zero-padded `SS`.
- Supported date formats are `dd-mm-yyyy`, `mm-dd-yyyy`, and `yyyy-mm-dd`; the default is `dd-mm-yyyy`.
- A wall-clock, time-zone, daylight-saving, resume, visibility-recovery, or settings change invalidates one frame and
  recomputes the next boundary.
- After a long hidden, suspended, display-off, or occluded interval, the first visible frame shows current time. The
  plugin does not replay missed ticks.
- The colon remains illuminated. Blinking, tenths, frame counts, and leap-second indication are outside version 1.

## Plugin and settings contract

### Identity

| Field | Proposed value |
| --- | --- |
| DLL | `StudioClock.dll` |
| Plugin ID | `builtin.studio-clock` |
| Plugin display name | `Studio Clock` |
| Widget type ID | `studio-clock` |
| Capability | `RedXePluginCapabilityWidgetProvider` |
| Rendering interface | `IRedXeGpuWidget` |
| Scheduling interface | proposed `IRedXeScheduledWidget` sibling IID |

The plugin descriptor does not set `RedXeWidgetFlagContinuousAnimation`. It relies on the scheduled-widget mechanism
and ordinary host invalidations.

### Settings proposal

The static plugin contract publishes a closed Draft 2020-12 object schema and this complete defaults object:

```json
{
  "showSecondProgress": true,
  "showSeconds": true,
  "secondsColor": "#FF1616",
  "showDate": false,
  "dateFormat": "dd-mm-yyyy",
  "timeColor": "#FF1616",
  "backgroundColor": "#111111"
}
```

| Member | Type | Allowed values | Default | Effect |
| --- | --- | --- | --- | --- |
| `showSecondProgress` | boolean | `true` or `false` | `true` | Shows the circular 60-step seconds progress |
| `showSeconds` | boolean | `true` or `false` | `true` | Shows the numeric seconds row |
| `secondsColor` | string | exact `#RRGGBB` | `#FF1616` | Colors numeric seconds and active/dim progress dots |
| `showDate` | boolean | `true` or `false` | `false` | Shows the local date row |
| `dateFormat` | string enum | `dd-mm-yyyy`, `mm-dd-yyyy`, `yyyy-mm-dd` | `dd-mm-yyyy` | Selects numeric date order |
| `timeColor` | string | exact `#RRGGBB` | `#FF1616` | Colors `HH:MM` and the optional date |
| `backgroundColor` | string | exact `#RRGGBB` | `#111111` | Colors the opaque widget panel |

- Settings members are optional because the host merges the published defaults before provider creation. Unknown,
  duplicate, malformed, and out-of-contract values reject the complete candidate settings document.
- Colors accept case-insensitive hexadecimal digits and are converted once to normalized numeric values during
  provider creation. The plugin retains no source JSON or borrowed string.
- The existing V2 factory envelope carries the effective settings. No Studio Clock-specific ABI or mutable
  configuration COM interface is added.
- A valid live edit stages a new provider/widget and commits transactionally. Failure preserves the previous clock.

## Scheduled-frame ABI proposal

Add `Common/PlugInterfaces/ScheduledWidget.h` with one optional COM interface derived directly from `IUnknown`. Studio
Clock owns this shared prerequisite for the separate Desk Clock plan. Its IID is
`1B6B4F9E-5421-4B4E-BC2D-190EE6CE86CB` and is immutable once published.

The interface exposes one synchronous, non-reentrant query for the delay until the widget's next visible change:

```cpp
interface __declspec(uuid("1B6B4F9E-5421-4B4E-BC2D-190EE6CE86CB")) __declspec(novtable)
    IRedXeScheduledWidget : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetNextFrameDelayMilliseconds(
        uint32_t* delayMilliseconds) noexcept = 0;
};
```

Contract proposal:

- A null output returns `E_POINTER`; a present output is cleared before validation.
- `S_OK` returns a delay from 1 through 86,400,000 milliseconds. `S_FALSE` requests no deadline.
- The result is relative to the callback and is converted immediately to a host-owned monotonic deadline.
- The callback performs no allocation, I/O, wait, synchronization, device work, or host re-entry.
- The host queries the sibling interface from each active widget and caches the earliest deadline after a successful
  frame. During a swipe, aggregation covers only the current and staged adjacent pages. It does not add the interface
  to `IRedXeWidget` or `IRedXeGpuWidget`.
- The UI thread waits for either queued input or the earliest deadline with a blocking message-aware wait. Expiry
  coalesces one ordinary frame invalidation; it never starts a periodic polling loop.
- When no visible scheduled widget has a deadline, the existing indefinite message wait remains in use.
- Hidden, minimized, suspended, display-off, and occluded states discard scheduled waits and block on existing
  visibility/occlusion signals. Visibility recovery invalidates once and establishes a fresh deadline.
- Clock instances request one millisecond after the next local second boundary when either seconds display is
  enabled. When both are disabled, they request one millisecond after the next local minute boundary because `HH:MM`
  is then the next changing field. The post-boundary guard prevents a coarse or early host wait from rendering the
  prior wall-clock bucket twice.
- If another visible widget already requires continuous frames, the clock adds no wake-up. It reuses cached time and
  constants until the wall-clock bucket changes but still redraws its content because the host target is rebuilt.
- A failed schedule query does not busy-loop. The host isolates the failure and waits for an unrelated invalidation.

This resolves low-cadence invalidation for the Studio Clock without resolving the active RFC's broader data-provider
callback problem. Implementation measurements must also confirm that this single two-draw widget does not justify a
host-owned primitive-batching IID; the frozen immediate-context GPU interface remains unchanged.

## Direct3D rendering design

### Procedural dot geometry

- Store digits and separators as small compile-time bit masks. Do not ship a texture atlas or load DirectWrite, WIC,
  a font, or a loose image.
- Compile Shader Model 5.0 HLSL at build time and embed stripped bytecode. Do not link or load the runtime shader
  compiler.
- Draw the opaque background with one generated fullscreen triangle.
- Draw ring positions and all lit glyph cells with one instanced quad draw. The vertex shader expands each quad from
  `SV_VertexID`; the instance ID selects a ring or glyph cell. The pixel shader produces an antialiased circular dot.
- Bound the maximum submitted dot instances at 576, covering the ring, `HH:MM`, seconds, and the longest date form.
  Optional hidden rows reduce the submitted count; they do not allocate alternate geometry.
- Cache square bounds, scale, row origins, dot radius, and spacing until viewport or DPI changes.

### Time sampling and uploads

- Sample local system time only when the cached wall-clock bucket expires, after a time-change invalidation, or when
  an instance first becomes visible. A continuously animated sibling must not cause 60 system-time queries per
  second.
- Convert the current digits and configured colors into one fixed-size constant record.
- Upload at most one 256-byte constant buffer when time, settings, viewport, or DPI-derived state changes. Frames
  caused only by another continuous widget reuse the existing constant contents.
- Rendering performs no CPU loop over submitted dots; glyph selection and dot placement are derived in shaders from
  bounded constants and instance IDs.

### Device lifecycle

- Provider-owned reusable resources are the embedded shaders and immutable blend, rasterizer, and depth states.
  Compatible instances share one immutable device-resource set.
- Widget-owned state is one 160-byte dynamic constant buffer, bounded validated settings, cached time digits, layout
  values, and next-boundary state. A buffer is not shared because each widget can have independent time/layout state.
- `OnDeviceCreated` creates the complete device-resource set transactionally.
- `OnDeviceLost` releases device resources idempotently before the host releases its device.
- Partial creation failure leaves the provider device-lost and releases every temporary resource.
- The plugin never receives or retains the HWND, swap chain, back buffer, render-target view, immediate context, frame
  record, or scheduling host internals.

## Mandatory performance and resource budget

| Area | Acceptance budget |
| --- | --- |
| Clock-only visible cadence | At most one clock-scheduled wake, frame, and present per displayed second; one per minute when both seconds displays are off |
| Hidden/inactive cadence | Zero plugin-owned periodic wake-ups, frames, or presents |
| Frame-path heap work | Zero allocations and zero frees |
| Frame-path synchronization and I/O | None |
| Constant upload | At most one map/unmap and 256 bytes when cached visual state changes |
| Draw submissions | At most two draws per rendered instance |
| Dot instances | At most 576 |
| Textures and runtime glyph work | None |
| Threads, timers, and HWNDs | None in the plugin |
| Shader payload | At most 64 KiB embedded in the DLL |
| Plugin CPU state | At most 16 KiB per provider and 2 KiB per widget, excluding COM/runtime overhead |

Release measurement at 2560x720 MUST record clock-disabled versus clock-enabled CPU submission time, D3D timestamp
time, private bytes, working set, steady allocation deltas, visible wake cadence, and hidden wake cadence. A missed
structural budget blocks closeout; a measured regression requires explicit approval and contract justification.

## Error and isolation behavior

- Malformed settings fail static validation or provider creation without replacing the active dashboard.
- Invalid frame prefixes, null borrowed pointers, unsupported device state, non-finite dimensions, or a zero-sized
  viewport fail safely without a draw or dereference.
- One clock render or scheduling failure does not prevent later widgets from rendering and never turns the host into
  a retry loop.
- Device loss and recreation preserve validated CPU settings and rebuild only device-owned resources.
- The first implementation supports the existing D3D11 feature-level floor and hidden WARP path.

## Implementation anchors

- New public scheduling interface: `Common/PlugInterfaces/ScheduledWidget.h`
- Plugin implementation and build-time HLSL: `Plugins/StudioClock/`
- Module discovery and interface lifetime: `RedXe/PluginManager.*`
- Deadline aggregation and active-page scheduling: `RedXe/DashboardHost.*`
- Message-aware deadline wait and time-change invalidation: `RedXe/Application.*`, `RedXe/FrameScheduler.h`
- Shipped examples: both files under `Settings/`
- Contract and pixel tests: `Tests/PluginContractTests/`
- Production scheduling, WARP, reload, and soak tests: `Tests/HostPluginTests/`

The plugin project is added to all four solution configurations, copies its DLL under the host output `Plugins`
directory, and is referenced by RedXe for build ordering only, without static import-library linkage.

## Required validation

### Contract and settings

- Verify factory null outputs, unknown IDs, unsupported IIDs, V1 defaults, V2 effective settings, the 4096-byte
  configuration bound, synchronous copying, and static schema/default validity.
- Verify controlling-`IUnknown` identity and reference counting across `IRedXeWidget`, `IRedXeGpuWidget`, and
  `IRedXeScheduledWidget`. Existing plugin IIDs and vtables remain unchanged.
- Verify every boolean combination, all three date formats, accepted mixed-case colors, malformed colors, duplicate
  members, unknown members, and transactional valid/invalid live reload.
- Verify both shipped templates reference Studio Clock and that an inactive gallery page creates no Studio Clock
  provider, widget, device resource, or deadline.

### Scheduling and host behavior

- Use deterministic time inputs in tests to prove guarded second-boundary and minute-boundary delay calculations,
  including midnight, month/year rollover, leap day, daylight-saving/time-zone notification, resume, and clock
  rollback.
- Prove that clock-only pages do not select continuous rendering; due deadlines coalesce exactly one invalidation.
- Prove that queued messages wake the deadline wait, unrelated messages do not create a frame, and a continuous
  sibling adds no extra clock wake.
- Prove hidden, minimized, suspended, display-off, occluded, inactive-page, and shutdown states retain no clock
  deadline and do not spin.

### Rendering and resources

- WARP readback proves the configured background, primary time, numeric seconds, optional date, active progress color,
  and dim future dots are present in the correct regions.
- Deterministic snapshots cover seconds `00`, `01`, `30`, and `59`, minute reset, each visibility toggle, each date
  order, portrait and landscape viewports, non-square letterboxing, DPI change, and minimum supported geometry.
- Count active ring positions and verify clockwise order, twelve-o'clock origin, and the `secondsColor` linkage to
  both seconds elements.
- Verify device loss/recreation, the 576-instance bound, one-upload/two-draw maximum, steady-state zero allocations,
  and absence of runtime shader compiler, DirectWrite, WIC, font, timer, worker, and HWND use.
- Run a five-minute Release host soak and confirm stable provider/widget/device-resource counts, bounded memory,
  expected visible tick count, hidden zero-wake behavior, and complete device-resource release on teardown.

### Repository matrix

- Run `./format.ps1`.
- Run `./test.ps1 -Configuration Debug -Platform x64 -Rebuild`.
- Run `./test.ps1 -Configuration Release -Platform x64 -Rebuild`.
- Build Release ARM64.
- Run `./validate-skills.ps1` and `git diff --check`.
- Perform one visual review at the 2560x720 design size against the reference's hierarchy and proportions; automated
  WARP evidence remains the required functional acceptance path.

## Implementation sequence

1. [x] Review and freeze the visual behavior, settings defaults, and `IRedXeScheduledWidget` semantics.
2. [x] Add the sibling scheduling IID, host deadline aggregation, message-aware wait, and focused scheduler/ABI tests.
3. [x] Add the Studio Clock project, metadata, factory/static settings contract, and all solution configurations.
4. [x] Implement strict effective-settings parsing and transactional host integration.
5. [x] Implement procedural dot shaders, embedded bytecode, resource lifecycle, and cached time/layout state.
6. [x] Add the clock to both shipped gallery templates without changing the Release startup composition.
7. [x] Add deterministic settings, schedule, WARP pixel, device-loss, import, rollback, and teardown coverage.
8. [x] Measure the resource budget, run the full validation matrix, reconcile the active architecture RFC, and update
   every owning normative contract.
9. [x] Record completion evidence, move this plan to `Specs/Plans/Done/`, and remove its WIP index row.

## Completion evidence

All validation was performed on 2026-08-31 without desktop automation.

- `./format.ps1` formatted the complete source set successfully.
- `./test.ps1 -Configuration Debug -Platform x64 -Rebuild` and
  `./test.ps1 -Configuration Release -Platform x64 -Rebuild` passed the complete build, plugin ABI, System Data,
  Studio Clock, settings/schema/watcher, production host/plugin, hidden WARP, and isolated crash-test matrix.
- `./build.ps1 -Configuration Release -Platform ARM64 -Rebuild` compiled the host, Studio Clock, focused tests, and
  production host harness for ARM64. An earlier run reported one transient file-delete warning on a generated
  settings output; the final ARM64 rebuild completed cleanly.
- `./validate-skills.ps1` and `git diff --check` passed. JSON and MSBuild XML inputs parsed successfully, and Release
  import inspection found no runtime HLSL compiler, DirectWrite, or WIC dependency in `StudioClock.dll`.
- Focused tests cover every settings visibility combination, all date orders, accepted/rejected colors and members,
  V1/V2 factory behavior, controlling-IUnknown identity, guarded second/minute scheduling, transaction rollback,
  active ring counts at 00/01/30/59, all configured colors, square fit at 900x500, 500x900, and 160x160, DPI cache
  refresh, device recreation, shared immutable resources, per-widget buffers, zero steady render allocations, and
  complete teardown.
- The final 2560x720 reference capture shows the intended hierarchy and proportions: dominant `HH:MM`, smaller
  numeric seconds, optional date, a clockwise 60-position ring beginning at twelve o'clock, configured red active
  dots, dim future dots, and no copied branding or enclosure.
- Release `StudioClockTests.exe --benchmark` at 2560x720 measured 0.159 microseconds/frame for the disabled baseline
  and 244.319 microseconds/frame with Studio Clock, a 244.160 microsecond CPU submission delta including an explicit
  immediate-context flush per measured WARP frame. D3D timestamp time was 0.0858 ms/frame. Private bytes changed by
  +110,592 and working set by +176,128 bytes. The unchanged final frame used zero maps, two draws, and 272 submitted
  instances; the maximum-content path is bounded at 558 instances and one 160-byte map.
- Release `HostPluginTests.exe --studio-clock-soak --seconds=300` ran the production scheduled host for five minutes:
  300 total initial-plus-scheduled frames, stable provider/widget/shared-device-set/constant-buffer counts of 1/1/1/1,
  private-byte delta +2,273,280, working-set delta +16,728,064, zero plugin-owned time sampling during the two-second
  hidden interval, and device-resource counts returned to zero on shutdown.
- The measured two-draw low-cadence clock does not justify a host primitive-batching IID. The active architecture RFC
  now limits that open decision to a materially larger widget family, while the scheduled sibling IID and Studio
  Clock behavior are normative in the owning plugin, settings, resource, and dashboard contracts.

## Closeout condition

This plan is complete only when implementation and tests pass, measured resource behavior satisfies the budgets, the
scheduled-widget decision and Studio Clock behavior are normative in their owning contracts, both templates remain
valid, and the active architecture RFC is narrowed accordingly. A completed plan MUST NOT remain under WIP.
