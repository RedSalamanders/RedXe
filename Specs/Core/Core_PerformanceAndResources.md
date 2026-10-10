# RedXe performance and resource contract

Status: current normative contract
Last reviewed: 2026-10-11

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
- Named actions (`Specs/Plugins/Plugins_Actions.md`) use one static 16-slot ring drained by one posted message; every
  host-native action and every publisher `Execute` returns within 20 ms on the UI thread without waiting, pumping,
  or showing UI, and defers longer work to the control lane, the launch worker, or the publisher's own device lane
  through bounded slots. Input injection batches at most 32 `INPUT`s per call and never sleeps; validation maps only
  the publisher modules a document binds, at the same moment as their settings contracts; between executions a
  dedicated action DLL owns no thread, timer, window, or hook.
- Launches (`system.launch`, `system.open`, `system.run`, `system.taskManager`, and the tray's Edit settings) MUST
  NOT call `ShellExecuteExW`, `CreateProcessW`, or a file probe on the UI thread, where a target on an offline share
  froze every frame for about 42 s. They run on one host-owned launch worker (`RedXe/LaunchWorker.*`): a thread
  created only by the first launch, in a single-threaded COM apartment, with 8 fixed slots of about 3 KiB and two
  events. Idle, it blocks in one message-aware wait and owns no timer or periodic wake; a full set of slots refuses a
  launch rather than waiting. Each finished launch posts the existing coalesced host-action message, and the UI
  thread logs the result. Shutdown, also one a stuck device lane ends early, and a session end before its log flush
  drop queued launches and wait at most 1000 ms (a session end only what is left of its deadline), once per process,
  for a launch still in the shell, and then retain that thread's storage until process exit instead of joining it; a
  later stop (the shutdown after a session end, or the process runtime's second shutdown) only checks whether the
  thread has exited, unless the earlier wait was zero and found no launch, when it waits for the exiting thread again.
- A queued action that injects input and waited more than 1000 ms for the UI thread is dropped, not replayed into
  whatever window is foreground after the stall; the check is one tick comparison per drained slot.
- RedXe and plugins must share immutable device resources across compatible widget instances and minimize dynamic
  uploads, state changes, render-target switches, and draw calls without restricting what a GPU widget may render.
- Page retirement MUST release resources retained by immediate-context bindings. After retiring widget work is
  quiescent, cancellation/replacement of a prepared neighbor and successful primary adoption clear the context before
  `OnDeviceLost`. This cold operation does not flush or recreate the device/swap chain, allocate storage or run per
  frame. Failed adoption retains the existing pages. The next draw rebinds required state under the GPU contract.
- ProcessViewer and Weather share one reusable CPU glyph atlas of one MiB per DLL while GPU users exist. Cold widget
  construction owns no atlas allocation. The last GPU user, device loss and failed initial resource construction
  MUST release that CPU allocation. A surviving user retains the shared atlas; a revisit reconstructs identical
  glyph pixels. Each atlas uses one virtual-memory owner, keeping cold release independent of CRT heap retention
  and sanitizer quarantine. No per-instance atlas, worker, timer or steady-frame allocation is added.
- Derived display state such as DPI, design-canvas transforms, and widget viewports must be cached and recomputed only
  when its inputs change.
  Provider staging retains at most 1 KiB of compact keys instead of 32 copied 8 KiB envelopes. Key comparisons
  remain collision-safe by referencing the original immutable staging settings; they allocate and copy nothing.
  The provider/widget/key transaction arrays together MUST remain below 28 KiB on the construction stack.
- Placeholder decisions in `Render` and `PrepareWidgets` MUST read one atomic status through a pinned per-widget
  binding; they MUST NOT acquire the status-table lock or scan instance-name strings. Registration/report/reason
  lookup and release remain cold paths in the existing 64-slot table. Generations reject stale bindings and retire
  before wraparound; widget callbacks/interfaces drain before the final pin is released. The cache adds at most
  1.5 KiB across two full 32-widget pages, no allocation, thread, timer or additional invalidation wake.
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
- The screen-edge dock (`Specs/UI/UI_XeneonDisplayWindowing.md`) adds no periodic wake-up in any state. An autohide
  reveal or hide never calls `ResizeBuffers`, recomputes the layout, calls `OnTargetSizeChanged`, or allocates, because
  the dock swap chain keeps the full bar size under `DXGI_SCALING_NONE`. With `animationMilliseconds` 0 it is one
  `SetWindowPos` and one frame; otherwise it is a slide of that duration (200 ms by default), visible motion like a
  raise settle: one `SetWindowPos` and one presented frame per display refresh until it ends, a top or left bar also
  moving its cached viewports (and any native container) by the slide offset, then the host blocks again; at most one
  one-shot timer (dwell or hide delay) is armed and every state exit kills it; no hook and no cursor polling exist, the
  peek strip being the window itself. A collapsed dock presents exactly one grip frame and then blocks like a minimized
  window, retaining its full-size swap chain (about 4 MiB for a 3840×270 bar) so a reveal presents at once without
  rebuilding it. Shell traffic (`SHAppBarMessage`) happens only on placement, activation, window-position changes,
  shell notifications, and the first-run install (two `ABM_GETAUTOHIDEBAREX` queries, made only when the default
  settings file is installed because it is missing, so a start that finds the file makes neither), never per frame,
  and never for a reveal, a hide, or a slide step. The work-area reservation (the
  whole bar of a fixed reserving bar, the peek strip of an autohide bar) is queried and set again only when a placement
  changes the reserved rectangle, its edge, row, or monitor DPI, or follows a shell change (`ABN_POSCHANGED`,
  `ABN_STATECHANGE`, `WM_DISPLAYCHANGE`), so the desktop is not re-laid out by the reveal state or by a placement that
  leaves the reservation alone (`PlanDockAppBar`). An inner-edge drag moves the window per pointer update but coalesces
  dashboard, swap-chain, and widget size callbacks to one 16 ms timer; release flushes the final size. A live switch
  between the standard window and the dock is a cold settings-reload path: one renderer rebuild (device and swap chain,
  as on an adapter change) and one dashboard resize, with no widget re-creation. The first-run bar on a display without
  a XENEON retains the back buffer of its XENEON-proportioned full size (3840×1080, about 16 MiB, on a 150 % 4K
  display), the same buffer as the titled fallback window it replaces there.
- The notification-area icon (`Specs/UI/UI_XeneonDisplayWindowing.md`) costs one hidden owner window of its own class
  and one small-icon `HICON` on the UI thread, with no thread, hook, or periodic wake-up in any state. Its only timer
  is the one-shot retry of an add that a running taskbar refused: at most four, with doubling delays from 1 s, one
  armed at a time, and none once the icon is added or the tries are spent. Shell traffic (`Shell_NotifyIconW`) happens
  only when the icon is added or removed, on those retries, on `TaskbarCreated`, on a settings apply while the icon is
  missing, on a menu cancelled from the keyboard (`NIM_SETFOCUS`), and on a DPI change of the owner; its callbacks, the
  menu, and the editor launch run only on user interaction, and none of them invalidates a frame. The editor launch,
  with any Open With picker or shell error box, runs on the launch worker, so the dashboard keeps presenting. While its
  menu is open the system's modal menu loop runs on the UI thread and the dashboard presents nothing, like any other
  modal UI.
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
- A local source retains storage only while an external provider reference or an allocated subscription needs it.
  Inactive and removing subscriptions retain the source. The existing acquisition worker reclaims unleased sources
  after collection and reserved callbacks finish, outside the subscription lock; before that worker exists, a last
  provider release may reclaim synchronously. This adds two lock-protected flags and bounded per-batch storage, no
  thread, timer, allocation or per-frame sweep. A revisit recreates source state behind the same retained facade and
  provider ID. Mapped modules and their immutable metadata remain valid until process teardown.
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
  Non-dock reachable geometry is UI-thread cached; unchanged frame, hover and hit-test queries MUST NOT enumerate
  displays. Window geometry/style/lifetime, DPI, display topology and system-settings messages invalidate before
  dispatch. The cache uses 24 bytes including its test walk counter, with no allocation, timer or worker.
- The top-level window is created with `WS_EX_NOREDIRECTIONBITMAP` and nothing paints it with GDI, so DWM keeps no
  client-sized redirection surface for it. Host-owned native-widget containers are `WS_EX_LAYERED` children with their
  own DWM surface; while another widget is raised they carry the dim as window alpha (`255 − dim`) and return to
  full alpha on dismiss. The hosting process manifest MUST declare a Windows 8 or later `supportedOS`, which is what
  makes a layered child legal (`UI_XeneonDisplayWindowing.md`). A native child HWND over the swap chain still forces composed presentation for the whole
  window while it exists, which is why no shipped page places one (`Core_Settings.md` template coverage exception).
- Resolution-dependent plugin resources are rebuilt on `IRedXeGpuWidget::OnTargetSizeChanged`, never in `Render`. That
  callback is the sanctioned place for rasterizing, creating textures for, and allocating such resources, because it is
  event driven: the host reports only an actual change in the largest viewport it will draw that widget at, never a
  position-only change and never per frame. Freezing such a resource at device-creation size instead is a defect, not
  a saving -- it produces wrong output at every other size. What does not depend on the drawn size, such as per-device
  lookup tables, is created and drawn once in `OnDeviceCreated`; that callback and the optional
  `IRedXePreparedGpuWidget::Prepare` phase are the only other GPU callbacks that may allocate or rasterize
  (`Specs/Plugins/Plugins_API.md` "GPU widget contract").
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
  create one worker per provider. Busy-waiting is prohibited. Sources MUST NOT create acquisition threads. The
  dedicated device-I/O lane is host-owned (`PluginHost` service slots, `Specs/Plugins/Plugins_API.md` service
  contract): one thread per started service that exposes `IRedXeDeviceWorker`, at most four, created at service start
  and joined at stop. Its bounds are normative: the plugin blocks only in one wait on the host stop and wake events
  and its own overlapped-I/O events (message-aware only while the lane owns a registered Raw Input sink window,
  whose queue it drains on the same thread in bounded batches of 256 messages, and, when it removes the sink, until
  empty, at most 16,384 messages, before it destroys the window, so input left in the queue never ends a later wait
  and every queued packet reaches the window), every device command, write, and feature report
  carries a 1 s timeout followed by `CancelIoEx`, restore at stop gives up at the first command a device does not
  answer, hotplug arrival is a CfgMgr32 notification that sets the wake event rather than a poll, a failed open
  retries at most four times with doubling delays from 1 s counted from the end of the failed attempt and then
  waits for the next arrival, and stop drains within 3 s or
  the host logs one `device-lane-drain-timeout` and tombstones the slot, service COM object, thread, events, and
  module until `RunDeviceWork` returns. If a driver never returns, that exceptional storage remains until process
  exit to avoid releasing memory still in use, and shutdown waits at most 1 s in all for the log writer to empty its
  queue instead of joining it. A replacement lane cannot start in that slot while tombstoned; the late lane's return
  posts one message that lets the UI thread reap the slot and restart a service the document still configures, with
  no polling in between. A HID request whose cancel is not complete after a 100 ms drain keeps its port's request
  slot instead of forcing a reconnect; only closing that port retires its I/O block. Each block holds one of eight
  per-process places from the open on, and a retired block keeps it, so open and retired blocks never exceed eight and
  Logicon opens no collection while all eight are held. Idle cost with a connected keypad is zero wake-ups. A present
  dialpad with a bound turn requires a process-wide Raw Input mouse sink, so the lane thread wakes for every mouse
  packet until the dialpad leaves or no turn is bound; packets from other mice are drained inside the wait without a
  lane turn, snapshot, or frame request, and each drain is capped at 256 messages. The
  lane owns at most one 434×434 BGRA compose surface, one 128 KiB JPEG buffer, one 4095-byte
  report buffer, and the key faces' signatures while a device or a monitor tile needs faces, and releases the
  surfaces at stop. A service's System Data faces ride the shared acquisition worker at the shortest requested
  interval (Logicon: 1 s for three data sets, only while such a face is bound) and never render there.
  `RedXeDataSetFlagDeviceLane` for data sources remains unimplemented.
- Screenshot capture owns at most one temporary worker and one pending request. The worker waits for the first
  Windows.Graphics.Capture frame; the UI thread continues dispatching messages and its normal frame policy. A second
  screenshot request while one is pending is refused (`ERROR_BUSY`), so replacing the worker cannot synchronously join
  on the UI thread. Completion posts one UI message and releases the worker before a `--screenshot` request closes the
  window; an action request leaves it open. A worker that cannot start ends its request on the same loop turn, so an
  idle loop never waits for a completion that will not come.
- Logging and diagnostics must not format or emit per-frame success messages. Repeated GPU render failure MUST
  share one transition guard for debug output and JSONL; it does no
  diagnostic work until the HRESULT changes or that instance recovers across its entire drawn frame. The renderer
  uses its existing fixed HRESULT slots and one 4-byte failed-instance mask, with no steady slot scan or allocation.
  Primary page adoption resets the diagnostic identity. `IRedXeHost::Log` copies a bounded
  record into a 32-slot 1024-byte ring and wakes one event-blocked writer. The writer appends JSONL under the settings
  sibling `Logs` directory using a UTC-dated file (`RedXe-debug-YYYY-MM-DD.jsonl` / `RedXe-YYYY-MM-DD.jsonl`) and
  deletes files older than `logRetentionDays` (default 15) on open, UTC date change, and retention-value change.
  Ordinary lines and reapplying the same retention value MUST NOT enumerate the directory or allocate a file path.
  The writer caches the open UTC date and last swept policy (20 bytes); it adds no thread or periodic wake.
  A policy change wakes the existing writer even with no queued records, and `FlushLog` waits for that cleanup.
  Release omits
  `RedXeLogLevelDebug`. The call is allocation-free on the
  caller, never blocks on disk, and is forbidden from GPU `Render` and GDI paint. Only `--self-test`, which opens no
  log directory, also writes each Warning and Error line to stderr on the calling thread, from the line it formatted
  on its stack, under a lock (one write per line to a pipe or a file, 256-byte chunks to a console); that test mode's
  reader drains the pipe. Plugin HTTP bodies that can exceed a few kilobytes MUST live on the heap; a 256 KiB automatic
  array on the network worker overflows the default thread stack (`STATUS_STACK_OVERFLOW`).
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

On 2026-10-10, matched x64 Release build 282 page cycles with identical portable scenes measured last-consumer
source reclamation. Returned Matrix private bytes fall from 124.72 to 110.34 MiB; relative to each run's cold Matrix
(37.11 / 38.59 MiB), retention falls from 87.61 to 71.75 MiB, a 15.86 MiB reduction. Returned working set falls from
114.88 to 98.75 MiB and handles from 1395 to 1385. Submissions remain 60.152 / 60.135 per second; process CPU is
1.848 / 2.005 percent of one core, so no CPU saving is established. Threads remain 43 and modules 102; local adapter
usage remains 47.26 MiB versus cold 25.01. Hidden/minimized intervals submit no frames. These measurements support
the source lifetime change, not the proposed cold-Matrix +4 MiB return budget or attribution of remaining caches.
Receipts: `.build/receipts/runtime-2026-10-10-p36-before`, `-p36-after`, and `-p36-source`.

The separately validated GPU binding retirement cut clears the context only at page retirement. Hidden WARP tests
prove release of resources retained in shader, vertex/index and constant-buffer slots; cancellation, replacement
and promotion preserve the surviving/incoming triangle pixels and device/swap-chain identity. Failed promotion
retains existing bindings and recovers without a rebuild. Release, Debug and ASan Debug native gates pass; ARM64
Release compiles. The matched build-282 D capture observes retained private 69.54 / 69.08 MiB, returned working set
97.36 / 97.37 MiB and local adapter usage 47.26 / 46.26 MiB; cold adapter usage is 25.01 in both. Returned
submissions remain 60.152 / 60.133 per second, and hidden/minimized phases submit zero. This is structural lifetime
proof and a small observed return reduction, not heap/driver attribution, a CPU saving or acceptance of cold +4 MiB.
The first after capture changed pages during its settled return and is rejected. Receipts:
`.build/receipts/runtime-2026-10-10-p36-gpu-before`, `-p36-gpu-after-idle`, `-p36-gpu-after` and `-p36-gpu`.

On 2026-10-10, an accepted sampled-CPU capture of x64 Release build 282 on the System page uses the same portable
scene, disabled services, XENEON rectangle/DPI and AMD rendering adapter as the runtime probe. Only the trace
controller is elevated; RedXe runs as the normal user. The 65-second probe reports 2.392 percent of one core and
12.364 submissions per second. The ETL has zero lost events/buffers; copied private PDBs and matching Microsoft
symbols resolve the host, source and OS call chains. The steady window excludes its first eight and last one seconds.
Of 1,251 named sampled-CPU stacks, 900 include the acquisition worker, 578 include `RedXeNativeQueryWalk`, and 20
include `OpenProcess`. There are 1,336 owned stacks before the named-event filter; inclusive counts overlap and
are not exact call durations or whole-process CPU shares. Driver-private symbols remain unavailable.
The bulk process walk is the first measured CPU investigation target: that captured source owns four MiB but starts
at one MiB and retries larger lengths each collection. The subsequent single-capacity change is measured separately
below. This capture accepts sampled attribution only; input/presented
latency, adapter moves and a CPU saving remain unqualified. The earlier broad capture dropped 704,274 events and
is rejected. Receipts: `.build/receipts/runtime-2026-10-10-p02-cpu-sampled` and `-cpu-stacks-visible`.

The native process/thread walk now queries its existing four-MiB buffer once, with no smaller retry, added storage
or changed columns/caps/fallback. Failed queries and invalid `ReturnLength` expose no readable bytes. In 64 quiet,
alternating-order standalone NT-query pairs on 2026-10-10, both paths return the same 647 process rows and at most
1,592,880 bytes. The old path makes two calls per collection; full capacity makes one. Median wall time is 20.121 /
11.678 ms and p95 22.054 / 14.018 ms. This is an algorithm diagnostic, not complete-source CPU qualification.
Matched build-282 normal-user System captures have equal settings, XENEON rectangle/DPI and rendering adapter.
Before / after-1 / after-2 report CPU 2.186 / 2.653 / 2.238 percent of one core, submissions 12.324 / 13.166 / 11.991
per second and private 57.84 / 58.39 / 59.01 MiB. Threads 43, modules 78 and local adapter usage 26.26 MiB remain
equal; handles 1177 / 1178 / 1177. These observations establish no whole-process CPU or submission saving and do not
accept the proposed System budgets. The capacity test rejects the old retry algorithm and accepts the single query;
oversized, failed, empty and invalid-length queries expose no bytes. Real DLL field/catalog, row-cap, history and
allocation checks pass in all 16 Release scopes and 15 Debug/15 ASan Debug native scopes. Source storage remains
16,437,760 bytes. ARM64 Release compiles and all 10 skills pass. Receipts:
`.build/receipts/runtime-2026-10-10-p21-nt-query-before`, `-p21-before`,
`-p21-after-1`, `-p21-after-2` and `-p21`.

The independent CPU-atlas lifetime cut replaces ProcessViewer and Weather's inline one-MiB arrays with lazy,
shared virtual-memory owners. Real DLL tests reject the old lifetime and verify cold construction, two users,
surviving-user draws, repeated device loss, last release, failure after CPU construction and retry. Process Viewer
Unicode readback remains identical across shared recovery and complete re-acquisition; all eight Weather WARP
images remain byte-identical to their prior Release outputs, including hero twins, accents, alerts and raised view.
One matched build-282 Release D-cycle pair across 2026-10-10/11 has identical scene, rectangle, DPI, platform and
DxUi identity. Only ProcessViewer.dll and Weather.dll change among its runtime binaries. Cold Matrix private is
38.23 / 35.51 MiB; returned private 106.39 / 105.06 and working set 98.16 / 96.16. Return-over-cold remains
68.16 / 69.55 MiB, so this pair does not accept the proposed cold-plus-four-MiB budget. Local adapter usage remains
25.01 MiB cold and 46.26 returned. Returned submissions are 60.150 / 60.102 per second, hidden/minimized zero;
CPU 2.057 / 1.692 percent of one core establishes no causal saving from one pair. The storage tests establish
exact release of the two CPU owners; the process measurements do not attribute every retained heap or driver byte.
All 16 Release scopes, 15 Debug and 15 ASan Debug native scopes, ARM64 Release compilation and 10 skill checks pass.
ASan keeps its default quarantine and reports process-byte growth diagnostically; ordinary soaks retain the
four-MiB private/eight-MiB working-set guards.
Receipts: `.build/receipts/runtime-2026-10-10-p28-before`,
`.build/receipts/runtime-2026-10-11-atlas-after`, `-atlas-red`, `-atlas-weather-pixels-before` and `-p28-atlas`.

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
dots (804 submitted instances with LED glow), one 160-byte map only when cached visual state changes (which the two
differently sized views of a raised clock do on every frame they are drawn), one shared immutable device-resource set,
and one per-widget constant buffer. It owns no texture, font, HWND, timer, or worker.
This bounded consumer does not by itself justify a host primitive-batching IID; a materially larger family must be
measured again before that decision.
At 2560×720, three Release WARP runs of the overlay-aligned 228-instance default produced a representative median CPU
submission delta of 267.403 microseconds/frame and GPU timestamp time of 0.0829 ms/frame. This is lower than the prior
404-instance reference-aligned medians of 287.055 microseconds/frame and 0.0842 ms/frame while preserving its visual
contract; draw, upload, allocation, scheduling, and device-resource budgets do not regress.
After adding the selectable outward-dot state, six Release WARP runs produced representative medians of 280.825
microseconds/frame and 0.0993 ms/frame. The bounded 13.422-microsecond CPU and 0.0164-ms WARP timestamp increases are
accepted for the dynamic per-companion setting at the clock's one-Hz cadence; instance count, constant-buffer size,
maps, draws, allocations, resources, and wake frequency remain unchanged.
The default LED glow (`glowPercent` 35) submits one additive halo quad of four LED radii per dot ahead of the cores in
the same draw, so the 228-dot default submits 456 instances. On 2026-09-26, six alternating Release WARP runs of that
2560×720 benchmark (a 720-pixel clock, the raised size) produced medians of 499.845 microseconds/frame and
0.2212 ms/frame against 241.912 microseconds/frame and 0.0800 ms/frame with `glowPercent` 0 in the same build. The
software rasterizer's cost grows with the halo pixels, about 16 times each dot's core quad; draws, maps,
constant-buffer size, allocations, resources, and wake frequency are unchanged. The clock alone renders once per
second, but the host redraws every widget on every presented frame, so beside a continuous sibling (the shipped
galleries put it on one page with Matrix Rain and the rotating triangle) it pays the halo fill at that sibling's frame
rate. Its tile there is about 256 pixels square, where the halos cover about 50,000 pixels per frame. Raised beside
such a sibling it draws twice per frame, the tile and the 720-pixel overlay slice (the benchmarked size), and because
the two sizes differ each draw rebuilds and maps its 160-byte constants, two maps per frame. That combination was not
measured separately; from the figures above it costs about 0.22 ms of WARP GPU time and 0.5 ms of WARP CPU submission
per frame for the overlay draw plus the much smaller tile draw, about 3 percent of a 60 Hz frame on WARP and
negligible on a hardware GPU. The increase is accepted because it is fill-rate only, stays at the 256-pixel tile's
cost except while the clock is raised next to a continuous widget, and `glowPercent` 0 submits no halo at all.

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
`DrawInstanced`. The acquisition worker updates bounded pending storage with no font/GPU access. UI-thread
preparation freezes the paint sample and uploads only changed 48×48 atlas cells (2,304 bytes each), outside frame
construction and its draw lock. Tile and raised draws consume that same prepared sample; later deliveries stay
pending. Clean preparation has no allocation, rasterization or upload, and Render does no atlas upload. DirectWrite
loads only while filling new atlas glyphs: one temporary session is reused across that preparation's strings and
released before drawing, including failure. One additional 11,160-byte pending sample per widget (111,600 bytes
for the default System page, plus bounded interface/phase bookkeeping) is accepted to freeze complete paint while
delivery continues, without adding a timer, thread or render allocation. Ranked lists copy at most
`topN` rows; the CPU heatmap stores at most 64 display cells. Sparkline history is 60 samples of widget-local fixed
storage. Eases last 320 ms from a fixed displayed start pose; tile and raised draws share the host timestamp. Delivery
already coalesces a host invalidation. Settled viewers return `S_FALSE` with zero delay; only real easing or the
bounded 420 ms high-band pulse requests a 33 ms visual deadline, with no Render-side immediate frame request.
Unchanged targets and sub-pixel projected changes snap to the exact current values in the delivery frame, preserving
labels, quality, histories and intent colors. Celsius numeral changes of at least one degree still ease even on a
saturated level track. CPU heat changes below an 8-bit luminance step snap except band
crossings. Auxiliary data and identical targets do not restart an ease. A continuous sibling/host motion can draw
sooner without another deadline wake. The fixed target/start pose adds at most 1,320 bytes per widget (12.9 KiB for
ten), accepted for cadence-independent interpolation and avoiding repeated empty bursts; it adds no allocation,
thread, timer, GPU resource or synchronization to scheduling. Sample/draw-list storage otherwise remains unchanged.
Glyph rasterization runs when a snapshot arrives or at device creation, never inside `Render`.
A settled System page MUST NOT set continuous animation. Hidden, minimized, suspended, occluded, and
display-off states stop eases and drain subscriptions; recovery draws current values and does not replay missed
motion. Snapshot copy stays on the acquisition worker. After delivery, the host coalesces one UI-thread invalidation.
This family remains on the immediate-context GPU path. The 2026-10-10 full gates measured a 0.399 ms Release and
0.388 ms Debug mean production `Renderer::Render` over the 32-frame hidden WARP fixture at 2560×720 after warmup
and device-loss rebuild. This is off-screen submission evidence, not hardware presented-frame/input latency or
CPU-thread attribution. Each visible widget issues one instance-buffer map and one `DrawInstanced`; this fixture
does not justify a host primitive-batching IID. The older September timings are historical, unmatched evidence.

## Production resource probe

`--probe[=seconds]` enables a UI-thread diagnostic timeout, 1–3600 seconds (default 5). Normal launches have no
diagnostic timeout, thread, handle, allocation or query per frame. Enabled sampling uses process-local thread-ID
snapshots (PSS without context/handle/VA capture), PSAPI module counts and process-resource
snapshots and read-only DXGI/AV queries outside Prepare/Render; those OS queries may allocate temporary storage and
delay the UI thread. `sample_us` reports their elapsed cost. The existing event-blocked JSONL writer receives one
complete record per actual interval in its unchanged 32×1024 ring. No diagnostic flush waits on the UI thread.
Hidden/minimized/occluded/display-off states remain inactive: the diagnostic timeout never invalidates or presents a
frame. Delayed samples restart the interval from their completion, without a catch-up loop. The waits preserve the
earlier scheduled-frame deadline. A valid probe is ignored by `--self-test`.

Record `event: "probe", v: 1` includes UTC `ts`, `pid`, monotonic completion `tick`, actual interval `ms`,
query cost `sample_us`, current page index `page`, host `state`, and cached absolute scheduled `deadline` (0 = none).
The numeric arrays keep even maximum-width values within one logger slot:

| Field | Array order |
| --- | --- |
| `process` | validity bits; private bytes; working-set bytes; peak commit (`PeakPagefileUsage`); peak working set; cumulative kernel+user CPU in 100 ns; handles; threads; GDI objects; USER objects; mapped modules |
| `present` | swap-chain generation; GetLastPresentCount HRESULT; cumulative count; delta-validity bits; interval count; GetFrameStatistics HRESULT; PresentCount delta; PresentRefreshCount delta; SyncRefreshCount delta; SyncQPCTime delta; SyncGPUTime delta |
| `local`, `nonlocal` | QueryVideoMemoryInfo HRESULT; budget; current usage; available reservation; current reservation (bytes, adapter node 0) |
| `av` | query HRESULT; live widgets; preparations; composites; surface bytes; summed per-view replacement peaks; surface allocations; cached brushes; cached text formats |

HRESULTs are unsigned 32-bit values. Only S_OK (0) supplies a successful value; AV S_FALSE (1) means the module is
absent. Process validity bits 0–6 correspond to memory, CPU, handles, threads, modules, GDI and USER queries. Host
state bits 0–7 mean continuous, visible, display on, renderer suspended, DXGI occluded, dock hidden, navigation and
overlay/dock motion. Visible is the host's show state and can remain set while minimized; renderer suspended
identifies that inactive case. Present delta bits 0/1 mean count/statistics deltas. A first sample, resize/recreation,
disjoint/failing statistics, or regressing QPC/GPU clock makes the affected delta unavailable until two matching
successful samples exist. UINT counters wrap modulo 2^32. Zero without its validity bit MUST NOT be treated as a
measured zero. Frame-statistics refresh/QPC/GPU deltas are DXGI counters, not input latency or hardware FPS.
Adapter memory queries are driver-reported usage for this process on the device adapter; they are not whole-process
GPU attribution across every adapter or AV helper-process memory. Replacement peaks are summed per-view historical
high-water marks, not a simultaneous process peak. Lifetime AV counters disappear with the owning views.

Required automated coverage: disabled/no-deadline behavior, competing frame/probe deadlines, late sampling, UINT
wrap, missing/disjoint statistics, generation and clock resets, extreme-width complete JSON, logger drain, real
process-resource sampling, read-only hidden WARP queries, and two-instance AV aggregation/hide/destruction. The
end-to-end screenshot fixture also runs `--probe=1` to exercise the real application loop without focus or input.

`measure-runtime.ps1` is the reusable Release x64 receipt collector. It uses portable template copies, fresh
processes per starting page and an all-page return/inactive run. It records scene, phase/window/machine context,
source/binary hashes before and after, raw logs and validity-aware summaries under `.build/receipts/`. Its
non-activating 2560×720, 96-DPI XENEON popup disables Logicon/tray and is explicitly a measurement variant;
it does not qualify the exact shipped-services budget or helper-process/PresentMode/input-latency acceptance.
`-InactiveOnly` keeps fresh starting-page windows hidden and collects retained-memory/zero-present evidence only.
Its accepted receipt covers those requested inactive phases, never the visible-page budgets. The visible collector
rejects display-off/occluded/unsettled baselines and missing records; a diagnostic wake cannot manufacture that evidence.
Hidden phases require the visible flag to clear; minimized phases require renderer suspension. Both require zero
present deltas in complete intervals, excluding intervals that straddle the state change.

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
- Current composition accounting (2026-10-11) is retained in `.build/receipts/runtime-2026-10-11-p04-accounting`
  and `-p04-accounting-canonical`. The 2560×720 / 96-DPI Focus Matrix grid calculates 5,248 glyph instances;
  Gallery's 640×720 Matrix calculates 1,066. These are source/scene calculations. Successful host submissions in
  the accepted D capture are 60.150/s on Focus and 60.100/s over a ten-second Gallery visit. Gallery's 512×720 AV
  tile reports 1,474,560 bytes; the expected 1280×720 overlay adds 3,686,400 bytes, still requiring a production
  raised hardware capture. The canonical WARP fixture reports its different 4,608,000-byte tile/overlay total,
  zero clean preparations and zero hidden storage/work against the actual current linked archive. Retain the
  earlier 192-DPI completion outlier and its repeat; these offscreen runs establish neither hardware FPS nor a
  matched I19 adoption regression.
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
- The automated `HostPlugin` scope runs separate 60-second Matrix, Studio Clock and Desk Clock WARP soaks.
  After 1,024 Matrix frames or 20 seconds of scheduled clock warm-up, peak sampled private/working-set growth
  in uninstrumented builds MUST remain within 4/8 MiB; sample every five seconds and at the end. ASan keeps its
  normal quarantine/shadow settings, reports process bytes diagnostically and enforces the same functional
  object/resource, scheduling and teardown bounds. These coarse whole-process regression
  guards include WARP/runtime caches. They supplement fixed resource counts, scheduling bounds and teardown;
  they do not replace hardware budgets, allocation checks or the five-minute plugin qualification.
