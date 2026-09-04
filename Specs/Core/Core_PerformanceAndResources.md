# RedXe performance and resource contract

Status: current normative contract
Last reviewed: 2026-09-04

## Mandate

Performance and low resource consumption are primary RedXe requirements, not optional cleanup work. Every design,
implementation, review, and validation change must minimize CPU time, memory, allocations, copies, synchronization,
GPU submissions, wake-ups, and loaded resources while preserving correctness, security, visual quality, and required
behavior.

When several correct designs exist, RedXe must use the design with the lowest steady-state resource cost unless a
measured result or a materially simpler and safer implementation justifies another choice. A resource regression must
be explicit, measured in the affected configuration, and justified in the owning normative contract or active plan.

"Zero CPU while idle" means RedXe owns no active polling loop or periodic wake-up when no frame, input, I/O completion,
or state change is pending. Normal operating-system scheduling noise is outside the application contract.

## Mandatory runtime rules

- Steady-state frame construction and rendering must not allocate or free heap memory after initialization.
- Per-frame and per-widget storage must be bounded and reused. Capacity exhaustion fails one widget frame safely; it
  must not trigger unbounded growth on the render path.
- RedXe and plugins must share immutable device resources across compatible widget instances and minimize dynamic
  uploads, state changes, render-target switches, and draw calls without restricting what a GPU widget may render.
- Derived display state such as DPI, design-canvas transforms, and widget viewports must be cached and recomputed only
  when its inputs change.
- Visible continuous animation must be paced by display presentation. Hidden, minimized, suspended, or display-off
  rendering must block on events and must not build or present a frame because an unrelated message was dispatched.
  An active page pan, settle, or staged neighbor is visible motion: the host MUST keep presenting so widget animation
  and the page-scroll ease continue. A host-owned native child covering the swap chain MUST NOT be treated as DXGI
  occlusion.
- After `Present` reports occlusion, RedXe must stop frame construction, wait for the DXGI factory's registered
  occlusion-status window message, and use `DXGI_PRESENT_TEST` to detect recovery without presenting content.
  Occlusion polling and periodic timers are prohibited.
- The single host swap chain uses a maximum frame latency of one so the CPU does not queue unnecessary frames.
- Static pages must render only after explicit invalidation. Resize, DPI, settings changes that alter the active
  runtime, `WM_PAINT`, show/display recovery, and occlusion/device recovery invalidate one coalesced frame. Mouse,
  cursor, keyboard, native-child timer, and other unrelated dispatched messages MUST NOT cause a host `Present`.
  Future plugin invalidation must be coalesced before waking the UI thread.
- A visible low-cadence widget MAY expose `IRedXeScheduledWidget` instead of owning a timer or requesting continuous
  frames. After a successful static frame the host caches the earliest valid current-or-staged-adjacent delay and
  blocks in one message-aware wait. Expiry coalesces one frame. Continuous animation supersedes the wait; hidden,
  minimized, suspended, display-off, occluded, inactive-page, and shutdown states retain no deadline. Invalid or
  failed delay queries are isolated and must not create a retry loop.
- The plugin runtime is process scoped. Exactly one module store, one set of data sources, and one acquisition worker
  serve the whole application, including the dashboard page staged for a swipe. Staging an adjacent page MUST NOT map
  a module a second time, create a second data source for a provider ID, or start a second acquisition thread, so a
  page change costs no duplicate acquisition and no thread churn.
- A plugin that needs a frame for an unpredictable state change calls `IRedXeHost::RequestFrame`, which coalesces one
  invalidation on the UI thread and adds no timer or wake-up of its own. It MUST NOT be used to emulate continuous
  animation, and it never overrides a blocked, hidden, suspended, display-off, or occluded state.
- Host edge-navigation chrome is event driven. It reveals on pointer entry, hides on pointer leave, and MUST NOT own a
  timer, a fade, a continuous frame, or any other wake-up. Re-evaluating the chrome with unchanged host state MUST
  perform no window operations, so it is safe to call from the frame loop. Each band is full client height; only its
  horizontal placement follows the reachable display edge. GDI wash and icon fonts are created once per DPI, never per
  paint.
- Resolution-dependent plugin resources are rebuilt on `IRedXeGpuWidget::OnTargetSizeChanged`, never in `Render`. That
  callback is the sanctioned place for rasterization, texture creation, and allocation in a GPU widget, because it is
  event driven: the host reports only an actual change in the largest viewport it will draw that widget at, never a
  position-only change and never per frame. Freezing such a resource at device-creation size instead is a defect, not
  a saving -- it produces wrong output at every other size.
- Plugin `Render` calls use borrowed frame and D3D context records. Render, resize, and
  `IRedXeWidget::SetVisible` callbacks must not
  perform disk, network, device discovery, process creation, blocking waits, or long-held locks.
- A visible native-window animation MAY use a UI-thread timer at the lowest rate that preserves its required visual
  quality. It MUST stop the timer while hidden, minimized, display-off, occluded, or detached. GDI paint callbacks
  MUST reuse resize-owned buffers and GDI objects rather than allocate memory or create handles per paint.
- Startup-only work may allocate when required, but temporary allocations must be released promptly and persistent
  caches must have a demonstrated reuse benefit.
- Typed configuration for inactive pages MAY remain in bounded settings storage, but inactive pages MUST create no
  providers, widgets, child HWNDs, Direct3D resources, timers, or frame work. Subsystems MUST NOT retain redundant
  complete copies of the settings document when narrow cached active state is sufficient.
- Live settings reload MUST allocate at most one full-document candidate beside the authoritative document. An edit
  that leaves the active grid, active widget records, and their referenced plugin records unchanged MUST publish the
  new typed document without rebuilding active providers, widgets, HWNDs, or D3D resources.
- DLLs, devices, textures, buffers, workers, and child windows must be created lazily when practical and released or
  quiesced when their owning feature is no longer active, except for the documented current no-unload plugin policy.
- Local data sources share one host acquisition worker. Background acquisition must block on events or timers at the
  lowest useful rate, coalesce subscriptions for the same provider and dataset into one sample, batch every unique due
  dataset from one source into a single `CollectSnapshots` call, and keep bounded history. A failed batch MUST NOT keep
  rate-history mutations from datasets collected before the failure. Multiple providers must not
  create one worker per provider. Busy-waiting is prohibited. Sources MUST NOT create acquisition threads. A dedicated
  device-I/O lane is optional and host-owned; it MUST NOT ship until timeout, cancellation, and teardown drain are
  bounded.
- Logging and diagnostics must not format or emit per-frame success messages.
- A raised overlay MAY create one host child HWND, one GDI region, and GDI chrome brushes only while a widget is
  raised. Dismiss MUST destroy that HWND. Settled raised content follows the same scheduled or continuous policy as
  the widget's tile; raising MUST NOT add a periodic wake. While raised, the host keeps submitting GPU work for every
  current-page widget so dimmed tiles stay live, then submits one extra draw for a raised GPU widget at the overlay
  slice. The extra draw is required so the focused plugin can show more information without freezing the rest of the
  dashboard.

The measured baseline System Data source is an accepted bounded local pull source. Its fixed x64 source object is
786,936 bytes and owns no worker or timer. Three Release row-cap runs of the production per-process row path, using
2,048 accessible synthetic entries and two steady collections per run, produced 3.806, 3.823, and 3.921 ms average
wall time per collection; the corresponding 64-collection CPU probe medians were 3.906 ms per collection. Every run
reported zero busy-heap block/byte growth, zero handle growth, zero private-byte growth, and a 4 KiB working-set delta.
This evidence accepts the current Toolhelp/per-process-query baseline; future metric expansion must remeasure before
raising storage, cadence, worker, or per-row work. The expansion fast-lane ceiling is 16 MiB of source-owned storage
with compact previous samples; the shipped source object MUST remain under 16 MiB. After the CPU/memory/process,
network/storage, GPU, and power/thermal expansion, x64 `sizeof(SystemDataSource)` is 15,920,696 bytes. Release x64
`--domains` (2026-09-01, eight samples on a hot source after the contract suite) measured cheap datasets under 0.4 ms,
live `process.list` (673 rows) at 73.9 ms, truncated `thread.list` (8,192 rows) at 43.8 ms, `thermal.sensor` at 11.3 ms,
`gpu.engine` at 9.6 ms, and one 18-dataset batch at 127.5 ms; the hang budget is five seconds per collection. Network
sampling uses cached LUIDs plus
`GetIfEntry2`, one `NotifyIpInterfaceChange` callback that only stores an atomic dirty flag, and no source-owned
thread. Storage opens bounded overlapped disk handles, times out and cancels hung IOCTLs on the collect thread, and
never issues `IOCTL_DISK_PERFORMANCE_OFF`. GPU sampling retains one DXGI factory, D3DKMT adapter handles, and one PDH
GPU Engine query, all released when the source is destroyed; it never creates a D3D device. Battery, ACPI thermal, and
storage-temperature queries use the same overlapped timeout and `CancelIoEx` drain. Generic fan-interface presence is
counted only; it does not create RPM rows. There is no device-I/O thread.

The current immediate-context GPU interface remains appropriate for Matrix-class work. A new production family of
cheap host primitives MUST NOT be added as separate immediate-context callbacks without measurement. When such a
consumer exists, the design review MUST first evaluate a host-owned bounded command/batch mechanism; the generic
widget root must not absorb plugin-specific drawing records.

The measured Studio Clock is an accepted low-cadence `IRedXeGpuWidget`: one instance uses two draws, no more than 402
dot instances, one 160-byte map only when cached visual state changes, one shared immutable device-resource set, and
one per-widget constant buffer. It owns no texture, font, HWND, timer, or worker. This bounded consumer does not by
itself justify a host primitive-batching IID; a materially larger family must be measured again before that decision.
At 2560×720, three Release WARP runs of the overlay-aligned 228-instance default produced a representative median CPU
submission delta of 267.403 microseconds/frame and GPU timestamp time of 0.0829 ms/frame. This is lower than the prior
404-instance reference-aligned medians of 287.055 microseconds/frame and 0.0842 ms/frame while preserving its visual
contract; draw, upload, allocation, scheduling, and device-resource budgets do not regress.
After adding the selectable outward-dot state, six Release WARP runs produced representative medians of 280.825
microseconds/frame and 0.0993 ms/frame. The bounded 13.422-microsecond CPU and 0.0164-ms WARP timestamp increases are
accepted for the dynamic per-companion setting at the clock's one-Hz cadence; instance count, constant-buffer size,
maps, draws, allocations, resources, and wake frequency remain unchanged.

The measured Desk Clock is also an accepted bounded low-cadence `IRedXeGpuWidget`. One static instance uses three
draws and 40 submitted instances; its configured 250–800 ms split-flap burst uses four draws and 46 submitted
instances. It maps one 400-byte provider-shared constant buffer only when cached visual state changes, uses one
1024×1024 single-channel atlas and three immutable 16-byte instance-offset buffers, and owns no timer, worker, HWND,
off-screen target, or shader compiler. It wakes at one second boundaries and requests presentation-paced frames only
while the flap moves. This second measured clock remains within the immediate-context mechanism and does not justify a
host primitive-batching IID; a materially larger family still requires aggregate measurement and review.

The Desk Clock atlas is 1 MiB and shared once per provider/device. The system32 DirectWrite module and an isolated
factory rasterize its bounded glyph set from an in-box Windows font only during transactional device-resource
initialization; the module handle, factory, font faces, analysis objects, temporary CPU coverage, and system-font
collection references do not survive initialization. The larger atlas preserves native contours at the target display
while keeping inactive discovery and the steady path free of font work, frame uploads, additional draws, allocations,
state changes, and wake-ups.

The Process Viewer family is a sample-driven `IRedXeGpuWidget` plus `IRedXeScheduledWidget` set in one DLL. Ten
settings-visible widgets share one device resource hub: build-time Shader Model 5.0 blobs, one instanced pipeline, one
1024×1024 `R8` atlas, and one dynamic instance buffer. Each visible widget frame maps that buffer and issues one
`DrawInstanced`. DirectWrite loads only while filling new atlas glyphs, then releases. Ranked lists copy at most
`topN` rows; the CPU heatmap stores at most 64 display cells. Sparkline history is 60 samples of widget-local fixed
storage. Eases last 320 ms. `GetNextFrameDelayMilliseconds` returns the dataset interval, not a 1 ms poll; while an
ease or pulse is in flight the widget calls `IRedXeHost::RequestFrame` after each presented frame so motion is
presentation-paced. Glyph rasterization runs when a snapshot arrives or at device creation, never inside `Render`.
A settled System page MUST NOT set continuous animation. Hidden, minimized, suspended, occluded, and
display-off states stop eases and drain subscriptions; recovery draws current values and does not replay missed
motion. Snapshot copy stays on the acquisition worker. After delivery, the host coalesces one UI-thread invalidation.
This family remains on the immediate-context GPU path. Release x64 `HostPluginTests` at 2560×720 measured an 8.24 ms
mean production `Renderer::Render` over 32 frames after warmup and device-loss rebuild (Debug was 8.25 ms). The
near-identical Debug and Release times show WARP Present of the full swap chain dominates; each visible widget issues
one instance-buffer map and one `DrawInstanced`. That measurement does not justify a host primitive-batching IID.

## ABI and data-layout rules

- Hot ABI records contain only fields consumed on the hot path. They do not carry speculative reserved arrays.
- Before the production ABI freeze, host and source-coordinated plugins use one current interface set and reject any
  public record whose `sizeBytes` differs from the current `sizeof` value. There is no prefix, tail, or old-IID fallback.
- Generic roots must not absorb plugin-specific drawing records or implementation concepts.
- Machine identifiers use stable UTF-8 ASCII strings. Localized user-facing text uses UTF-16 Windows strings.
- Synchronous enumeration returns borrowed immutable arrays when the module can provide stable storage; it must not
  allocate one callback object per enumeration.
- Borrowed devices, contexts, frame records, HWND containers, and descriptor arrays have explicit call or owner
  lifetimes and must not be retained outside their contract.

## Design and review evidence

Every material design or implementation review must consider:

1. steady-state CPU wake-ups and work per frame or sample;
2. persistent and peak memory;
3. allocations and copies on hot paths;
4. D3D upload, state-change, and draw-call counts;
5. behavior while idle, minimized, hidden, display-off, or occluded;
6. bounded failure behavior under malformed or excessive plugin output.

Optimization must target measured or structurally unavoidable costs. Micro-optimizations that reduce clarity without
observable resource benefit are not required.

## Required validation

- Debug and Release x64 WARP smoke tests must pass.
- Release ARM64 must compile.
- The plugin contract test must validate factory behavior, borrowed metadata, rendering-IID negotiation, COM
  identity, and `IRedXeRaisedWidget` extent queries.
- The multi-widget WARP smoke frame must notify the GPU widgets of device creation, render every configured widget,
  and notify them before device release.
- Review must confirm that steady-state GPU callbacks allocate no heap memory and reuse bounded device resources.
- `HostPluginTests` must exercise the production `PluginManager`, `DashboardHost`, and `Renderer` with a hidden
  off-screen HWND and WARP. It must verify the scheduler decision table for hidden, minimized/suspended, display-off,
  occluded, clean-static, invalidated-static, continuous, and page-navigation-active states without automating the
  desktop, including that page navigation keeps presenting when DXGI reports the swap chain occluded. The scheduler
  input MUST NOT contain an any-message redraw proxy.
- `HostPluginTests` MUST also prove raised-overlay geometry, Process Viewer half-width raise while sibling tiles still
  draw, no continuous wake from that raise, and GdiOrbit container move/restore, using the same hidden WARP host.
- Changes to the acquisition of operating-system visibility, power, or DXGI occlusion signals that are not represented
  by the scheduler decision seam additionally require a live check that inactive windows do not spin and recovery
  resumes rendering.
- System-data validation must exercise two 2,048-row steady collections through the production row-population path,
  report CPU/wall time, private bytes, working set, heap, handles, and fixed source storage, and fail on heap or handle
  growth or source storage of 16 MiB or more. Release x64 must also time each catalog dataset and one full-catalog batch
  (`SystemDataTests --domains`) and fail only on collect failure or a hang exceeding five seconds per collection.
  Collecting every dataset MUST NOT load `wbemprox.dll`, `fastprox.dll`, or `wbemcomn.dll`. The host integration test
  must leave Process Viewer and the System page hidden for longer than the dataset interval and observe no additional
  delivered sample. `HostPluginTests` must also prove the ten System Data viewers are GPU scheduled widgets, render on
  hidden WARP at 2560×720, rebuild after device loss, and stay non-continuous when settled. The
  host subscription cap remains 32; a 128-subscription source cap was not adopted.
- Low-cadence scheduling changes must test second/minute boundary aggregation, continuous-animation supersession,
  unrelated-message behavior, inactive visibility/power states, and a scheduled production-host soak. Studio Clock
  validation additionally proves two draws, the 402-instance bound, 60 ordinary second positions, 12 outward
  five-second companion positions, default-always-lit and progress-linked companion modes, date-dependent immutable
  descriptors, the square clock plus lower date band, zero steady render allocations, unchanged-frame zero maps,
  shared immutable resources, and complete device-loss teardown.
- Desk Clock validation additionally proves deterministic rollover phases, changed-tile-only animation with every
  unchanged tile visible and pixel-stable, 40/46 static/active instance bounds, three/four draw bounds, one 400-byte
  upload only on visual change, zero steady render allocations, one shared 1 MiB DirectWrite-rasterized atlas, no
  persistent DirectWrite or runtime shader/image dependencies, a five-minute scheduled flip soak with stable resources
  and hidden zero work, and complete device-loss teardown.
