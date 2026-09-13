# RedXe performance and resource contract

Status: current normative contract
Last reviewed: 2026-09-06

## Mandate

Performance and low resource consumption are primary RedXe requirements, not optional cleanup work. Every design,
implementation, review, and validation change must minimize CPU time, memory, allocations, copies, synchronization,
GPU submissions, wake-ups, and loaded resources while preserving correctness, security, visual quality, and required
behavior.

Embedded accessibility is lazy: no root, snapshots or provider image pin before the first UIA request. Its host
queue uses 32 fixed slots and one coalesced message; it adds no worker, HWND, timer or periodic retry. Unchanged
preparations/geometry reuse published snapshots; composition performs no accessibility work. Hidden/modal-transition
views disconnect. AV keeps its already-loaded provider image mapped through process exit after first publication so
external COM references remain safe after module shutdown. All device/model/control resources still release normally.
This image-retention cost must be included in application accessibility resource measurements; the active AV plan
does not yet claim matched resource acceptance or full real-client validation.

When several correct designs exist, RedXe must use the design with the lowest steady-state resource cost unless a
measured result or a materially simpler and safer implementation justifies another choice. A resource regression must
be explicit, measured in the affected configuration, and justified in the owning normative contract or active plan.

"Zero CPU while idle" means RedXe owns no active polling loop or periodic wake-up when no frame, input, I/O completion,
or state change is pending. Normal operating-system scheduling noise is outside the application contract.

## Mandatory runtime rules

- Steady-state frame construction and rendering must not allocate or free heap memory after initialization.
- Per-frame and per-widget storage must be bounded and reused. Capacity exhaustion fails one widget frame safely; it
  must not trigger unbounded growth on the render path.
- Retained controls use `IRedXePreparedGpuWidget::Prepare` before frame construction for changed layout and raster
  work. Clean checks are allocation-free and run only on already-requested frames. Preparation measurements are
  separate from composition; neither may be omitted from the reported UI cost. Idle/hidden frame policy suppresses
  both phases, and requests raised during preparation remain coalesced for a later frame.
- Windows appearance is captured once at initialization and on theme/system-color/settings notifications, then
  copied into preparation records. Plugins must not query system colors or the registry on a clean preparation or
  render path. Opaque consumer surfaces are prepared with the changed palette; clean composition remains unchanged.
- Local device commands use one lazy, process-wide MTA control lane with 16 fixed slots and coalesced reruns.
  Enqueue-to-result time is at most three seconds, including queue age. Potentially hanging device calls execute
  in a separately owned helper process. Shutdown signals cancellation, joins bounded work, suppresses UI completions
  and releases references before unloading modules. Hidden AV display observation is suspended independently from
  an armed camera route; that route captures only while consumer sample requests maintain a 250-ms demand lease.
- RedXe and plugins must share immutable device resources across compatible widget instances and minimize dynamic
  uploads, state changes, render-target switches, and draw calls without restricting what a GPU widget may render.
- Derived display state such as DPI, design-canvas transforms, and widget viewports must be cached and recomputed only
  when its inputs change.
- Subscription deactivation and release MUST drain running and reserved callbacks using an event-blocked wait.
  Per-slot in-flight counters and fixed delivery arrays preserve the 32-subscription bound without heap allocation.
  Widgets MUST be quiescent before GPU teardown. Worker-side GPU lifetime checks and access share the resource lock;
  an atomic ownership flag alone is insufficient to protect a device resource.
- A DxUi `EmbeddedHost` that is hidden or has zero extent holds no cached surface, and it becomes dirty only through
  control invalidation, never merely because the host ticked it (library contract at the pinned DxUi commit). AV
  therefore holds no tile surface while the widget is hidden and no overlay surface while it is not raised; a settled
  unraised AV widget reports `surfaceBytes` equal to the tile extent × 4, a hidden one reports 0, and showing it again
  costs exactly one surface allocation. The library bounds its solid-brush cache to 256 entries and its configured
  text-format cache to 96, reported through `EmbeddedStatistics`.
- Launcher uses two fixed geometry entries for the tile and overlay. Each entry stores dimensions, paging, `iconSize`,
  and 32 cell rects. Their combined payload plus an 8-byte index preserves both draw geometries and avoids repeated
  grid derivation when settled; alternating WARP draws MUST allocate zero heap memory. Animated overlay sizes recompute
  only the changed entry. Overflow shortcuts paginate inside the widget at the chosen cell size; the GPU page-dot strip
  shares `DxUi::PageIndicator` DIP metrics and does not add a second constant buffer. Each visible launcher instance
  owns one 32-slice 256×256 BGRA `Texture2DArray` (8 MiB) plus a 64-byte dynamic constant buffer and a 1 KiB
  (32×32-byte) dynamic structured instance buffer. That 8 MiB texture is the justified capacity cost of 32 jumbo
  icons; `Render` only maps the existing instance and constant buffers. CPU BGRA sources stay bounded at
  32×256×256×4 and rebuild the GPU array after device loss without a second extract.
- Visible continuous animation must be paced by display presentation. Hidden, minimized, suspended, or display-off
  rendering must block on events and must not build or present a frame because an unrelated message was dispatched.
  An active page pan, settle, or staged neighbor is visible motion: the host MUST keep presenting so widget animation
  and the page-scroll ease continue. A raise or dismiss settle is the same class of visible motion and MUST keep
  presenting until it completes; settled overlay chrome MUST NOT add a periodic wake. Close-control hover is
  event-driven GDI on the overlay HWND and MUST NOT start a timer or a host Present. A host-owned native child covering
  the swap chain MUST NOT be treated as DXGI occlusion.
- After `Present` reports occlusion, RedXe must stop frame construction, wait for the DXGI factory's registered
  occlusion-status window message, and use `DXGI_PRESENT_TEST` to detect recovery without presenting content.
  Occlusion polling and periodic timers are prohibited.
- Adapter of output: the Direct3D device is created on the hardware adapter whose output scans out the monitor the
  main window sits on, never on the default adapter while a matching output exists. When the window moves to a
  monitor scanned out by another adapter (move, size/move end, DPI change, or display-topology change), the host
  rebuilds the device on that adapter through the device-loss path, so widgets see `OnDeviceLost`/`OnDeviceCreated`
  exactly once. Presentation MUST NOT cross adapters through DWM. A window that sits on no monitor (hidden test host)
  keeps the default-adapter path, and forced WARP never re-checks. Every device creation logs one `device-created`
  JSONL record with adapter name, LUID, driver type, and whether that adapter owns the window's monitor; nothing is
  logged per frame. The selection policy (`RedXeSelectAdapterRecordForMonitor`) is a pure function over flattened
  adapter/output records so it is testable without a display topology.
- The single host swap chain is created with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` and a maximum
  frame latency of one on the swap chain (`ResizeBuffers` keeps the flag). Before building a frame the UI thread waits
  on the frame-latency waitable object together with the message queue, so a free back buffer is consumed
  immediately, pending input is dispatched before the frame is built, and `Present` never blocks the UI thread. The
  idle wait is unchanged: a settled page still blocks on the message queue alone.
- Static pages must render only after explicit invalidation. Resize, DPI, settings changes that alter the active
  runtime, `WM_PAINT`, show/display recovery, and occlusion/device recovery invalidate one coalesced frame. Mouse,
  cursor, keyboard, native-child timer, and other unrelated dispatched messages MUST NOT cause a host `Present`.
  Future plugin invalidation must be coalesced before waking the UI thread.
- A visible low-cadence widget MAY expose `IRedXeScheduledWidget` instead of owning a timer or requesting continuous
  frames. After a successful static frame the host caches the earliest valid current-or-staged-adjacent delay and
  blocks in one message-aware wait. Expiry coalesces one frame. Continuous animation supersedes the wait; hidden,
  minimized, suspended, display-off, occluded, inactive-page, and shutdown states retain no deadline. Invalid or
  failed delay queries are isolated and must not create a retry loop.
- The plugin runtime is process scoped. Exactly one module store, one set of data sources, one acquisition worker, and
  one JSONL log writer serve the whole application, including the dashboard page staged for a swipe. Staging an
  adjacent page MUST NOT map a module a second time, create a second data source for a provider ID, or start a second
  acquisition thread, so a page change costs no duplicate acquisition and no thread churn.
- A plugin that needs a frame for an unpredictable state change calls `IRedXeHost::RequestFrame`, which coalesces one
  invalidation on the UI thread and adds no timer or wake-up of its own. It MUST NOT be used to emulate continuous
  animation, and it never overrides a blocked, hidden, suspended, display-off, or occluded state.
- Host chrome (the mouse edge bands and the raise overlay's dim, shadow, and close control) is drawn by the renderer
  into the swap chain as at most eight quads per frame through one shared 64-byte constant buffer, one blend state,
  and one `R8` glyph atlas of three Fluent glyphs rasterized with DirectWrite at device creation and again only when
  the DPI changes. No chrome HWND, GDI object, region, brush, or font exists. Edge-navigation chrome is event driven:
  it reveals on pointer entry, hides on pointer leave, and MUST NOT own a timer, a fade, a continuous frame, or any
  other wake-up; each reveal, hide, close-hover change, and dim step invalidates exactly one coalesced frame.
  Re-evaluating the chrome with unchanged host state MUST perform no work, so it is safe to call from the frame loop.
  Each band is full client height; only its horizontal placement follows the reachable display edge.
- The top-level window is created with `WS_EX_NOREDIRECTIONBITMAP` and nothing paints it with GDI, so DWM keeps no
  client-sized redirection surface for it. Host-owned native-widget containers are `WS_EX_LAYERED` children with their
  own DWM surface; while another widget is raised they carry the dim as window alpha (`255 − dim`) and return to
  full alpha on dismiss. The hosting process manifest MUST declare a Windows 8 or later `supportedOS`, which is what
  makes a layered child legal (`UI_XeneonDisplayWindowing.md`). A native child HWND over the swap chain still forces composed presentation for the whole
  window while it exists, which is why no shipped page places one (`Core_Settings.md` template coverage exception).
- Resolution-dependent plugin resources are rebuilt on `IRedXeGpuWidget::OnTargetSizeChanged`, never in `Render`. That
  callback is the sanctioned place for rasterization, texture creation, and allocation in a GPU widget, because it is
  event driven: the host reports only an actual change in the largest viewport it will draw that widget at, never a
  position-only change and never per frame. Freezing such a resource at device-creation size instead is a defect, not
  a saving -- it produces wrong output at every other size.
- Plugin `Render` calls use borrowed frame and D3D context records. A page-swipe viewport keeps the widget's full
  size and MAY have a negative origin; shrinking it to the visible intersection would rebuild resolution-dependent
  resources and reflow content every frame of the slide. Render, resize, and
  `IRedXeWidget::SetVisible` callbacks must not
  perform network, process creation, blocking waits, or long-held locks. `Render` must not perform disk, extract
  icons, or decode images. The launcher MAY enumerate at most 32 taskbar `.lnk` files and extract jumbo or PNG
  icons on `SetVisible(TRUE)` when the authored shortcut list is empty, and on drop or settings apply; that work MUST
  NOT run from `Render`. Launch animation uses `RequestFrame` for at most 400 ms, then returns to idle with no
  wake-up. Launcher page-slide follow uses `RequestFrame` only while the finger is moving or a 140–280 ms ease-out
  settle is in flight; a static grid owns no wake-up.
- A visible native-window animation MAY use a UI-thread timer at the lowest rate that preserves its required visual
  quality. It MUST stop the timer while hidden, minimized, display-off, occluded, or detached. GDI paint callbacks
  MUST reuse resize-owned buffers and GDI objects rather than allocate memory or create handles per paint.
- Startup-only work may allocate when required, but temporary allocations must be released promptly and persistent
  caches must have a demonstrated reuse benefit.
- Weather's empty-location discovery is a cold operation in a disposable helper. WinRT never enters RedXe/Weather.dll.
  Acquisition is bounded to eight seconds plus a four-second coarse fallback. Recent-report/default-position reads
  run only after acquisition failure; a 20-second parent bound covers the entire chain, including synchronous API
  stalls. Cancellation terminates and joins that owned helper. This adds at most five seconds of cold failure-path
  lifetime versus the former single-attempt helper, with no steady-state CPU, memory, library, or wake-up cost.
  Cached coordinates and a saved city eliminate repeated location work. The host settings queue owns at most eight
  temporary records of 4096 JSON bytes plus 127-byte IDs, allocates only on submission, releases records after UI
  dispatch/teardown, and uses the existing coalesced UI message. Empty dispatch checks one atomic without a queue lock.
- Launcher imports reuse its bounded authored identity records and the existing host settings queue. Import/encode
  occurs only on an empty-list cold path, never Render; populated instances add no polling, timers, workers or repeated
  queue submissions. An interactive save racing an older queued import may perform two ordered cold writes to retain
  unrelated fields and ensure the newer edit wins. Formatted settings allocate only on the cold save path and remain
  within the 1 MiB document limit; compact plugin settings retain the 4096-byte bound.
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
- Logging and diagnostics must not format or emit per-frame success messages. `IRedXeHost::Log` copies a bounded
  record into a 32-slot 1024-byte ring and wakes one event-blocked writer. The writer appends JSONL under the settings
  sibling `Logs` directory using a UTC-dated file (`RedXe-debug-YYYY-MM-DD.jsonl` / `RedXe-YYYY-MM-DD.jsonl`) and
  deletes files older than `logRetentionDays` (default 15) on open, day change, and retention apply. Release omits
  `RedXeLogLevelDebug`. The call is allocation-free on the
  caller, never blocks on disk, and is forbidden from GPU `Render` and GDI paint. Plugin HTTP bodies that can exceed a
  few kilobytes MUST live on the heap; a 256 KiB automatic array on the network worker overflows the default thread
  stack (`STATUS_STACK_OVERFLOW`).
- A raised overlay creates no HWND and no GDI object. Its dim strips and shadow are host quads drawn after the tiles
  and before the raised widget so plugin pixels stay undimmed; the close control is drawn after the raised widget.
  Raise and restore MAY present for a clamped 160–240 ms ease; that motion is presentation-paced and then idle.
  Settled raised content follows the same scheduled or continuous policy as the widget's tile; raising MUST NOT add a
  periodic wake. Close hover is one coalesced frame that redraws the close quads, never a timer. While raised, the host
  keeps submitting GPU work for every current-page widget so dimmed tiles stay live, then submits one extra draw for a
  raised GPU widget at the overlay slice. The extra draw is required so the focused plugin can show more information
  without freezing the rest of the dashboard.

Measured 2026-09-07 on the reference machine (XENEON EDGE on the AMD iGPU, primary 4K display on an NVIDIA RTX 5080,
Ryzen 9 9950X3D, one fresh process per row, `D3D11CreateDevice` only): a device on the RTX 5080 adds 37 threads and
53 MiB of private bytes with 200 MiB of driver images; a device on the AMD iGPU adds 32 threads and 22–26 MiB with
48 MiB of images; WARP adds no thread and 1.5 MiB. `D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS`
changes neither driver and is not used. Before the adapter-of-output rule the live Release process created its device
on the default adapter (the RTX 5080) while the window sat on the AMD-driven XENEON, so it carried both driver stacks
(147 threads, 153 MiB private bytes after visiting the three shipped pages) and DXGI copied every 2560×720 frame
across adapters; that is the justification for the rule above. With the rule in place the same Release build launched
on the XENEON creates its device on the AMD iGPU (`device-created`: LUID 0001C3CC, adapter owns window monitor: yes),
maps only `amdxx64.dll`, appears on one adapter LUID in the GPU counters, and after 10 s on Matrix Focus holds 40 MiB
of private bytes, 62 MiB of working set, and 42 threads. Moving the Debug titled window onto the RTX 5080 display and
back rebuilt the device on each adapter in turn without failure (receipt: `.build/receipts/2026-09-07-adapter-of-output.md`).

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

The user accepted the I19 static-library adoption trade-off on 2026-09-13.
The [matched AV record](../../Measurements/DxUiAdoption/2026-09-13/README.md) is the
accepted measured envelope: Release dirty composition adds 2.6-5.2 microseconds at
96 DPI, while completed offscreen throughput across the Release scenarios ranges
from -1.76% to +0.17%. Surface storage and hidden work remain unchanged. The cost is
accepted for the canonical library and corrected module ownership; it is not a
zero-regression claim or permission for further growth. Preserve the original
comparisons and baseline variation, and retain existing thresholds. Hardware AV,
presented-frame latency, real-client accessibility and long-run memory acceptance
remain with the AV owner.

- `measure-av-views.ps1` measures the production AV `LiveView` with a shared WARP device, synthetic model,
  640×360 tile and 1280×720 overlay at 96/144/192 DPI. Each clean/dirty scenario has 120 warm-up frames
  and five rounds of 600 completed frames. Report preparation and CPU composition p95 separately from
  GPU-completed offscreen throughput, private bytes and working set. Steady surface storage stays at
  4,608,000 bytes; hiding both views releases it and causes no further preparations or composites.
  The wrapper records the selected archive from the actual linker record, binary/source hashes, compiler,
  profile and machine context. Compare identical fixture/product source on the same quiet machine and
  retain all runs, including baseline-versus-baseline noise checks. These measurements do not establish
  presented FPS, peak memory, allocation counts, hardware AV or real-client accessibility acceptance.
- Debug and Release x64 WARP smoke tests must pass.
- Release ARM64 must compile.
- The plugin contract test must validate factory behavior, borrowed metadata, rendering-IID negotiation, COM
  identity, and `IRedXeRaisedWidget` extent queries.
- The multi-widget WARP smoke frame must notify the GPU widgets of device creation, render every configured widget,
  and notify them before device release.
- Review must confirm that steady-state GPU callbacks allocate no heap memory and reuse bounded device resources.
- `HostPluginTests` must exercise the production `PluginManager`, `DashboardHost`, and `Renderer` with a hidden
  off-screen HWND and WARP. It must verify the scheduler decision table for hidden, minimized/suspended, display-off,
  occluded, clean-static, invalidated-static, continuous, page-navigation-active, and overlay-motion-active states
  without automating the desktop, including that page navigation and raise/dismiss settle keep presenting when DXGI
  reports the swap chain occluded. The scheduler
  input MUST NOT contain an any-message redraw proxy.
- `HostPluginTests` MUST also prove raised-overlay geometry, Process Viewer half-width raise while sibling tiles still
  draw, no continuous wake from that raise, and GdiOrbit container move/restore, using the same hidden WARP host.
- `HostPluginTests` MUST prove the host chrome contract on the hidden WARP host: a settled page draws zero chrome
  quads, a raised half slice with close hover draws its dim strips, shadow, hover wash, and glyph (and exactly one
  quad fewer without hover), a revealed edge band draws its wash and chevron over every tile, hidden chrome draws
  nothing, an unchanged chrome state reports no change, a DPI change re-rasterizes the glyph atlas once, no child
  HWND exists over the swap chain, and shutdown releases the pipeline. Dim-strip geometry is proved pure. The
  shipped Debug first page MUST create no child HWND.
- `HostPluginTests` MUST prove the adapter-of-output decision table (`RedXeSelectAdapterRecordForMonitor`: the
  hardware adapter owning the monitor wins, software adapters never own a monitor, a null or unmapped monitor keeps
  the default adapter) and, on the hidden WARP host, that the renderer reports a software device identity, exposes a
  frame-latency waitable object signaled before the first frame and again after a presented frame, keeps it across a
  resize, and releases it at shutdown. A live launch on the XENEON MUST show one `device-created` record naming the
  adapter that owns that monitor, and the process MUST appear on exactly one adapter LUID in the `GPU Engine` and
  `GPU Process Memory` counters.
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
