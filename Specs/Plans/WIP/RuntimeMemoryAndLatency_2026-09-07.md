# Runtime memory, lag, and whole-process resource budget

Status: `ACTIVE`
Created: 2026-09-07 (rewritten the same day after a live Release measurement)
Owner: host renderer, scheduler, plugin runtime, DxUi consumer accounting, and every bundled plugin's retained cost

## Goal

The live Release process lags and holds hundreds of megabytes. This plan is the execution record for a measured,
whole-process cut. The measurement that produced this rewrite showed that the largest costs are not the plugin buffers
the first draft budgeted: the Direct3D device is created on the wrong GPU, the settled System page is not idle, swipe
staging does the whole document's work on the UI thread, and process-scoped leftovers survive every page change.

Product requirements restated by the owner on 2026-09-07:

1. RedXe MUST render on the adapter that owns the display it is on, and MUST follow the window when it moves to a
   display owned by another adapter.
2. Data providers use arena storage: one reserved region per provider, committed per table on first use, no
   per-table small allocations, no memset of tables nobody subscribed to.
3. `process.list` MUST NOT open a process handle per process.
4. A page change is uncommon and MUST be fluid; every byte the swipe needed beyond the settled page MUST be released
   once the page is stable.
5. All budgets are Release x64 on hardware.

This file does not change normative contracts by itself. Durable budgets discovered here MUST land in
[`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md) and
[`../../UI/UI_XeneonDisplayWindowing.md`](../../UI/UI_XeneonDisplayWindowing.md) at closeout, with plugin and
dashboard consumers updated in the same change.

Owning contracts:

- [`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md)
- [`../../UI/UI_XeneonDisplayWindowing.md`](../../UI/UI_XeneonDisplayWindowing.md) (adapter-of-output rule)
- [`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md)
- [`../../UI/UI_Dashboard.md`](../../UI/UI_Dashboard.md)
- [`../../Core/Core_Settings.md`](../../Core/Core_Settings.md)
- [`../../Core/Core_DxUiIntegration.md`](../../Core/Core_DxUiIntegration.md)
- [`../../Plugins/Plugins_AVControl.md`](../../Plugins/Plugins_AVControl.md) (AV surface and helper process only)

Coordinate, do not fork:

- [`RFC_Plugins_AVControl.md`](RFC_Plugins_AVControl.md) still owns real IME/AT, G1 audio, G2 camera, and matched
  text/UIA performance. This plan only accounts AV's retained surfaces, coordinator, and helper-process cost.
- [`WeatherPlugin_2026-09-04.md`](WeatherPlugin_2026-09-04.md) still owns the network lane and forecast fetch.
- [`PluginDashboardRemainingCloseout_2026-09-02.md`](PluginDashboardRemainingCloseout_2026-09-02.md) still owns
  leftover architecture gates. Do not open host primitive batching from this file.
- [`PluginBoundaryHardening_2026-09-03.md`](PluginBoundaryHardening_2026-09-03.md) already made `PluginHost`
  process-scoped. Remaining G2/H3 work stays there.

DxUi checkout inspected 2026-09-07 (read-only; no reset, clean, or overwrite):

| Tree | HEAD | Notes |
| --- | --- | --- |
| `Z:/src/DxUI` / `Z:/src/DxUi` (same volume) | `7b571b734ee078cc5c68ad63ed2334c7453441e5` on `page-indicator` | Matches current `Dependencies/DxUi.lock.json` |
| `Z:/src/DxUi-worktrees/av-high-contrast` | `24081e2a` on `codex/av-text-bridge` | Isolated AV text/UIA work; leave it |
| `Z:/src/DxUi-worktrees/text-services-baseline` | detached `ad888331` | Baseline; leave it |
| `Z:/src/DxUi-worktrees/av-input-baseline` | detached `1947a5b` plus local edits | Leave it |
| `Z:/src/DxUi-worktrees/input-pr-repair` | `e42126c8` on `codex/input-receipt-repair` | Leave it |

`restore-dxui.ps1` remains the only way RedXe consumes DxUi. Library changes are filed upstream, never made from here.

## Live evidence (2026-09-07, Release x64, hardware)

Machine: XENEON EDGE 2560×720 attached to the **AMD Radeon iGPU**; primary 4K display on the **NVIDIA RTX 5080**;
Ryzen 9 9950X3D, Windows 11 26200. Process `.build\x64\Release\RedXe.exe`, launched 13:31:28, visited Matrix Focus
(+0.1 s), Plugin Gallery (+2.8 s), System (+9.0 s), then settled on System for three minutes:

| Metric | Value |
| --- | --- |
| Private bytes / working set | 153 MiB / 155 MiB (peaks 185 / 177) |
| Threads | 147 (141 created in the first second) |
| Handles | 1,831 |
| Mapped modules | 121, 359 MiB of images: `nvgpucomp64.dll` 106 MiB, `nvwgf2umx.dll` 86 MiB, `amdxx64.dll` 48 MiB, `nvppex.dll` 7 MiB, `windows.storage.dll` 8.7 MiB, `shell32.dll` 7.6 MiB, `d2d1.dll` 6.2 MiB, `UIAutomationCore.dll` 4.1 MiB, `dcomp.dll` 2.1 MiB |
| CPU on the settled System page | 6% of one core: acquisition worker 2.9%, UI thread 2.1%, driver threads ~1% |
| GPU memory committed by this pid | 63.6 MiB on the RTX 5080 LUID, 18.2 MiB on the AMD LUID |
| GPU engines used by this pid | NVIDIA 3D, NVIDIA copy, **and** AMD 3D at the same time |

Root cause of the last three rows: `Renderer::CreateDevice` (`RedXe/Renderer.cpp:344`) calls
`D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, …)`, which selects adapter 0 (the NVIDIA). The window lives on
a monitor owned by the AMD. DXGI/DWM bridge this with an internal device on the AMD and a copy per present.

Per-adapter device cost measured the same day (one fresh process per row, `D3D11CreateDevice` only, nothing drawn):

| Device | Threads added | Private bytes added | Driver images | Creation time |
| --- | --- | --- | --- | --- |
| NVIDIA RTX 5080 (adapter 0, today's default) | +37 | +53 MiB | 200 MiB (`nvwgf2umx`, `nvgpucomp64`, `nvppex`, …) | 127–147 ms |
| AMD Radeon iGPU (adapter 1, owns the XENEON) | +32 | +22–26 MiB | 48 MiB (`amdxx64`) | 43–69 ms |
| WARP | +0 | +1.5 MiB | 6 MiB | 9–23 ms |
| Either with `D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS` | no change | no change | | | 

RedXe currently pays both hardware rows: ~69 driver threads and ~75–80 MiB of private bytes before a single widget
exists, plus the cross-adapter staging surfaces (17.4 MiB "shared" on the NVIDIA LUID and 16.5 MiB on the AMD LUID).
That is roughly half of the threads and half of the private bytes of the measured process. The threading flag is
worthless on both drivers and is not part of this plan.

Runtime log (`%LocalAppData%\RedXe\Logs\RedXe-2026-09-07.jsonl`): the top CPU thread (2.9% of a core) was created at
exactly +9.0 s, when the ten System viewers subscribed; it is the host acquisition worker. The 2026-09-06 log shows
153 `forecast-failed` warnings at two per second and 1,647 `widget-created` events in four runs, i.e. one cold Weather
fetch per Gallery visit during swipe testing.

## Why the process is slow and large today (structural, with evidence)

### Host

| Cost | Evidence | Effect |
| --- | --- | --- |
| Device on adapter 0, not on the output's adapter; no re-check when the window moves | `Renderer.cpp:344`; no `WM_DISPLAYCHANGE` handler; `WM_MOVE` only refreshes accessibility (`Application.cpp:3806`) | cross-adapter present, two driver stacks, composed flip, extra frame of latency |
| `Present(1, 0)` blocks the UI thread; device-level latency 1; swap chain flags 0 | `Renderer.cpp:381`, `:401-410`, `:932`; `Application.cpp:3622` waits on the message queue only | input queued during the block waits a full frame |
| Top-level window has a DWM redirection surface it never paints | `Application.cpp:1043` (`WS_EX_APPWINDOW` only); only the edge and overlay child HWNDs call `BeginPaint` (`:1840`, `:3437`) | one 7 MiB DWM surface and copy per frame in composed mode |
| Full clear + full present, `FLIP_DISCARD` | `Renderer.cpp:817`, `:932` | 7 MiB cleared and presented per frame; partial presentation impossible with discard |
| One continuous tile redraws all tiles; `changedWidgets` discarded | `Application.cpp:3641-3645`, `:778`; `Renderer.cpp:876-884` | every tile renders at vsync on Gallery |
| `EnumDisplayMonitors` per frame and per wait iteration, before the cache check | `Application.cpp:1533-1542`, `:1646`, via `:3712` from `:760`/`:797`; twice per `WM_MOUSEMOVE` | 60+ monitor enumerations per second |
| Locked 64-slot string scan per widget per frame, twice | `DashboardHost.cpp:794`, `PluginHost.cpp:1415-1433`, `Renderer.cpp:757`, `:826` | ~4,000 compares per frame |
| Unthrottled `OutputDebugStringW` in the render loop | `Renderer.cpp:859` | global mutex per failing widget per frame |
| Staging: whole-document validation and per-widget JSON parse **twice**, `AppSettings` deep copy, 256 KiB `ProviderBuildKey` stack array, `OnDeviceCreated` + `SetVisible(TRUE)` + network activation for every neighbor widget, teardown and rebuild on direction flip, staged neighbor pins vsync with no finger down | `Application.cpp:1219-1280`, `:2215-2222`, `:3649`; `PluginManager.cpp:777`, `:839-842`, `:935-937`; `PluginManager.h:91-96`; `Renderer.cpp:90-106` | dropped frames at gesture start; one cold Weather fetch and one Launcher icon extraction per cancelled swipe |
| Synchronous `MOVEFILE_WRITE_THROUGH` settings write on the UI thread, up to 32× per promote, ~4 document copies per persist | `Settings.cpp:1259` (also `:875`, `:941`, `:962`), `:2229`, `:1764`, `:1780-1818`; `DashboardHost.cpp:649-664`; `Application.cpp:4235` | multi-ms stalls at the end of a swipe |
| UI thread can wait `INFINITE` on plugin callbacks | `PluginHost.cpp:1643`, `:1663`, `:1894`, `:2010`, `:2204` | hang on a slow `RunNetworkWork` |
| Two directory enumerations per log line | `PluginHost.cpp:777`, `:789`, `:900`, `:917` | log thread churn |
| Module discovery maps every plugin in `declare` and on every page at first `Initialize` | `PluginManager.cpp:786-832` | Weather + curl + zlib, AV, ProcessViewer, SystemData mapped on Matrix Focus |
| Data sources are created at `GetDataProvider` and released only at process exit | `PluginHost.cpp:1240-1322`, `:1843-1869` (only caller: `Shutdown`) | `SystemDataSource` survives every page change |
| `sourceDocument` retained (≤ 1 MiB); `WidgetInstanceSettings` 5,816 B each regardless of content; 19×256 `DataSetRuntime` static | `SettingsV4.cpp:1705`; `Settings.h:194-205`; `PluginHost.h:160`, `:227` | 0.3–4 MiB of settings shadow, duplicated during staging |
| Raise animation: 2–3 GDI regions + `SetWindowPos` per frame; edge band HWND created per hover | `Application.cpp:3136-3183`, `WidgetRaise.h:249-294`, `Application.cpp:1565-1582` | GDI churn |
| Scheduled deadlines on `GetTickCount64` | `Application.cpp:3701` | up to 15.6 ms jitter; acceptable for 1 Hz widgets; document, do not add a 1 ms timer |

### DxUi library (`7b571b73`) and the AV consumer

| Cost | Evidence | Effect |
| --- | --- | --- |
| A hidden or zero-extent `EmbeddedHost` keeps its texture, SRV, and D2D bitmap | `src/Rendering/Embedded.cpp:191-212`, `:249-257`; `Tests/Embedded/EmbeddedTests.cpp:218-222` | surfaces survive hide until `Detach` |
| `Prepare` re-rasters the whole surface; `AdvanceAnimation` calls `MarkDirty()` even when nothing ticked | `Embedded.cpp:328-340`, `:232` | one caret blink = one full raster |
| `TextField::Tick` true while focused; `ComboBox::Tick` while open; indeterminate `ProgressBar::Tick` forever | `src/Controls/DxUi.TextInput.cpp:1639-1659`, `DxUi.ComboBox.cpp:1749-1756`, `DxUi.Controls.cpp:3352-3376` | host pinned awake at tick rate |
| Popup drop shadows: 2 command lists + 2 effects per dirty frame | `DxUi.Controls.cpp:138-188`, `:689-702` | popup churn |
| Unbounded brush and text-format caches | `include/DxUi/DxUi.h:3980`, `:3958` | slow growth |
| ~54 heap allocations per dirty `Prepare` (83-control fixture); only `composeAllocations == 0` is asserted | `.build/reports/Embedded-x64-Release.json`; `DxUi.Grid.cpp:1904`, `:1917` | allocation on the prepare path |
| 128 MiB replacement ceiling is prose | `Embedded.cpp:264-270`, `:300` | not enforced |
| AV: both views attached and both control trees built at device creation; hidden views still receive `SetState`/`SetProfiles`/`SetAppearance`; overlay surface never released after dismiss; `ProfileControls` never destroyed | `Plugins/AVControl/AVControl.cpp:208-210`, `:243-256`, `:167-185`; `AVControlView.cpp:95-96`, `:389-421`, `:631-638` | ~3.5 MiB overlay surface per collapsed widget after its first raise |
| AV: module-global `coordinator` (~428 KiB heap + ~215 KiB broker mapping + broker process + threadpool wait) released only in `RedXePluginShutdown` | `AVControl.cpp:33`, `:79`, `:646-648` | survives leaving the Gallery |
| AV: ~267 KiB copied per observation cycle (133 KiB `Inventory` twice) | `AVControlBroker.cpp:123`, `Coordinator.cpp:357` | copy churn |

### Bundled plugins

| Plugin | Retained | Wake | Hot-path cost |
| --- | --- | --- | --- |
| **SystemData** | ~15.6 MiB, **all memset at construction** because every member carries `{}` (`SystemData.cpp:3695-3821`, `new` at `:3838`): 4 MiB NT walk buffer (`NativeQuery.h:220`), 3.0 MiB `_threadValues` (`:3759`), 2.4 MiB `_processValues` (`:3765`), 1 MiB PDH array (`GpuQuery.h:141`), 0.95 MiB walk sample, 0.6 MiB `cpu.logical` values, 0.4 MiB thread history, 0.3 MiB process history | none owned (correct); host worker at 1/2/5/10/60 s | `OpenProcess` per row per collection (`:3597-3601`) feeding `GetProcessInformation`, `GetProcessAffinityMask`, `IsWow64Process2`, `IsProcessCritical` (`:3394-3449`) = the 73.9 ms `process.list`; ~23 KiB stack zero+copy per batch (`:1354`, `:1504-1534`); 688 KiB memset on partial failure (`:1550-1551`). **No shipped viewer reads those four columns** (`ProcessViewer.cpp` reads columns 0–7 only). `thread.list` opens no handles. |
| **ProcessViewer** ×10 | 1 MiB CPU atlas as a namespace-scope object never freed (`ViewerGpu.cpp:31`, `:385`) + 1 MiB GPU atlas + 64 KiB instance buffer; ~18 KiB per widget | `_restDelay` = min catalog interval, returned whether or not anything changed (`:1011-1015`, `:1283`) ⇒ ~7 scheduled wakes/s; `BeginEase()` on **every** snapshot (`:1085`) then `RequestFrame` per frame for 320 ms (`:1260-1263`) ⇒ ~20 presents/s on the settled System page | full 1 MiB `UpdateSubresource` inside `Render` (`ViewerGpu.cpp:589`, `:604`); `dwrite.dll` load + isolated factory + system font collection per glyph miss (`:87-108`, `:479-484`); glyph raster on the acquisition worker under the lock `Render` holds (`:1092`, `:1248-1256`); 64 KiB draw-list zero-init per widget per frame (`:1247`) + 10.9 KiB sample copy under lock (`:1242`) |
| **MatrixRain** | 16 KiB immutable atlas per provider, 128 B CB, no CPU instance buffer | continuous | one 128 B map + 2 draws; 6,519 instances at Focus, 1,271 on Gallery. Clean. |
| **RotatingTriangle** | 56 B | continuous | one map + one draw. Clean; the flag is the cost. |
| **GdiOrbit** | ~1 MiB DIB | `SetTimer` 33 ms on a plugin child HWND, the only timer in the tree (`GdiOrbit.cpp:30`, `:191`) | CPU repaint + `BitBlt` at 30 Hz; the child HWND forbids independent flip |
| **StudioClock** | 160 B CB, shared immutable dots | 1 s / 1 min boundary | one map on change + one draw. Clean. |
| **DeskClock** | atlas 1.31 MiB at scale 1, 5.25 MiB at scale 2, new texture built before the old is released (6.56 MiB transient, `DeskClock.cpp:1111-1130`); per provider configuration (`:1838`) | 1 s boundary; **1 ms scheduled delay during the flap** (`:1520-1543`) | full 72-glyph DirectWrite re-raster on scale change; retained soak grew +9.2 MiB private in 300 s (`.build\fix-desk-clock-soak.log`) |
| **Launcher** (uncommitted edit on disk) | 8 MiB `Texture2DArray` per widget (`Launcher.cpp:77`, `:879-889`; was 2 MiB) + up to 8 MiB CPU BGRA (`:326`), allocated in `OnDeviceCreated` even for `"shortcuts": []` | event-driven; `RequestFrame` from `Render` during settle/pan/launch (`:1415`) | every `SetVisible(TRUE)` re-extracts every icon synchronously on the UI thread with no cache (`:1205` → `:1832` → `:1876-1882`), five `CoCreateInstance(WICImagingFactory2)` sites; `UploadIcons` zero-fills 256 KiB on the stack 32× and uploads all 32 slices (`:932-942`) |
| **Weather** | 1 MiB static CPU atlas never freed (`WeatherGpu.cpp:33`) + 1 MiB GPU; 8×256 KiB pre-sized cache slabs freed only in `RedXePluginShutdown` (`WeatherHttp.cpp:43-58`, `:224-228`, `:302`); two 256 KiB bodies per fetch (`Weather.cpp:579`, `:618`) | scheduled 5–60 min; `S_FALSE` hidden (correct) | `curl_easy_init`/`cleanup` per request, no connection reuse (`WeatherHttp.cpp:388`, `:430`); full-atlas upload inside `Render` (`WeatherGpu.cpp:766`); glyph re-raster on every `OnTargetSizeChanged` (`Weather.cpp:422`) |
| **AVControl** | see DxUi table; two 23 KiB views per widget | event-driven (correct) | copies above; UIA pin on first connect (documented, budget for it) |
| **All modules** | mapped at first `Initialize` for the whole document, never unmapped | | no-unload stays; map-on-first-use is the change |

Shipped page shapes, and what survives leaving each page today: Matrix Focus (one continuous 2560×720 Matrix);
Plugin Gallery (Triangle + Orbit + Matrix + StudioClock + DeskClock + Weather + Launcher + AV); System (ten scheduled
viewers). Leaving System keeps the 15.6 MiB source. Leaving Gallery keeps the AV coordinator and broker, the Weather
HTTP slabs, and both static atlas staging buffers. Every page keeps every module image.

## Acceptance budgets (measure first, then commit)

Every budget is whole-process, Release x64, hardware, XENEON at 2560×720, 96 DPI, AC power, captured by the
in-product probe (P0.1). Debug rows are informational only and MUST NOT pass or fail a budget.

| Scenario | Private bytes | Threads | Wake / present | Other |
| --- | --- | --- | --- | --- |
| A. First page after startup, 60 s settled | record; after Phases 1 and 4 target **≤ 64 MiB** with Matrix Focus continuous (Matrix-only on the NVIDIA path measured 75 MiB on 2026-08-31) | record; target ≤ 80 after Phase 1, then attribute the remainder | display refresh if Matrix stays first; PresentMode `Hardware: Independent Flip` | exactly one adapter LUID in the GPU counters; no NVIDIA image mapped |
| B. Plugin Gallery, 60 s settled, no raise, no camera | record; target ≤ 110 MiB | ≤ 88 | one continuous source or zero; Orbit ≤ 33 ms only while visible | AV `surfaceBytes` = tile only |
| C. System page, 60 s settled | record; SystemData commits only subscribed tables (≤ 10 MiB for the shipped viewers) | ≤ 88 | ≤ 2 presents/s average at 1 Hz meters; process CPU ≤ 1.5% of a core; acquisition ≤ 1% | `process.list` ≤ 10 ms |
| D. All pages visited, back to first page, settled 60 s | **A + 4 MiB** | A | A | no `SystemDataSource`, no Launcher array, no AV overlay surface, no AV coordinator, no Weather slabs |
| E. Hidden / minimized / occluded / display-off, any page | no growth | no growth | 0 frames, 0 Orbit timers, 0 acquisition delivery (already tested for System) | |
| F. Mid-swipe peak Gallery↔System | settled + 8 MiB | | stage ≤ 2 ms UI-thread CPU per swipe; no dropped frame from first finger movement to settle | no second source, no second Launcher array, no cold Weather fetch, no icon extraction for a cancelled swipe |
| G. AV tile only, clean composition | AV CPU model ≤ 1+2 MiB as proposed | | 0 D2D in `Render` | `surfaceBytes` = tile extent × 4; `surfaceAllocations` unchanged across 1,000 composites (already tested) |
| H. Hardware presented frame, Gallery with current continuous widgets | p95 ≤ 16.7 ms at 60 Hz; input-to-present ≤ 2 frames (PresentMon) | | | WARP 8 ms System-page mean is **not** this gate |

If Phase 0 shows a scenario already under target, keep the measured number and do not spend complexity chasing a
smaller one.

## Sequenced work

Do not implement a later phase before the earlier pass condition is recorded. Measurement before each cut.

### Phase 0 — receipts from the real process (no behavior change)

| ID | Item | Pass condition |
| --- | --- | --- |
| P0.1 | **In-product probe.** `RedXe.exe --probe[=seconds]` (default 5) writes one `probe` JSONL record per interval through the existing log writer: private bytes, working set, peaks, handle count, thread count, `GetGuiResources`, module count, current page, continuous flag, scheduled deadline, presents in the interval (`IDXGISwapChain::GetLastPresentCount`), `DXGI_FRAME_STATISTICS` deltas, `IDXGIAdapter3::QueryVideoMemoryInfo` local/non-local, and the AV `EmbeddedStatistics` sums when an AV widget is live. Move `QueryProcessMemorySnapshot` and `CountProcessThreads` out of `Tests/HostPluginTests/HostPluginTests.cpp:50-70,168-186` into a shared header under `Common/`. Always (probe or not) log one `device-created` record with adapter name, LUID, driver type, and `sameAdapterAsOutput`. | Receipts A–H under `.build/receipts/` (never `docs/`), one per scenario; `device-created` present in every log. |
| P0.2 | **External captures, repeated per phase:** PresentMon (`PresentMode`, `MsBetweenPresents`, input latency) and WPR (`wpr -start CPU -start GPU -filemode`, 60 s, `wpr -stop`) analyzed in WPA for CPU by thread, thread start stacks (attribute the ~70 non-driver threads), and image loads with stacks (who pulls in `shell32`, `windows.storage`, `MSCTF`, `textinputframework`, `UIAutomationCore`, `dcomp`). | Thread and module attribution table in the receipt. |
| P0.3 | **Soaks become tests.** Wire `--matrix-soak`, `--studio-clock-soak`, `--desk-clock-soak` (60 s variants) into `test.ps1` with asserted `private_bytes_delta` and `working_set_delta`. Investigate the Desk Clock +9.2 MiB before setting the bar. | Soaks fail on growth above the recorded bar. |
| P0.4 | Record Matrix instance count and present rate on Matrix Focus and Gallery; record the DxUi ComplexUi library receipt identity (`7b571b73`) versus AV Gallery tile+overlay surface bytes. | Numbers match the grid math (6,519 / 1,271) or explain why not; library FPS is not treated as application FPS. |

### Phase 1 — render on the GPU that owns the panel; never block on Present

| ID | Item | Pass condition |
| --- | --- | --- |
| P1.1 | **Adapter-of-output device.** `Renderer::CreateDevice` takes the window's `HMONITOR` (`MonitorFromWindow(_window, MONITOR_DEFAULTTONULL)`), enumerates `IDXGIFactory1::EnumAdapters1` → `IDXGIAdapter1::EnumOutputs` → `IDXGIOutput::GetDesc().Monitor`, skips `DXGI_ADAPTER_FLAG_SOFTWARE`, and creates with `D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, …)`. Stores the adapter LUID and monitor. Falls back to the default-adapter path only when no output matches (hidden test HWND at −32000, headless), then WARP as before. **Landed 2026-09-07:** `RedXe/AdapterSelection.h` (pure policy), `Renderer::FindAdapterForMonitor`, `Renderer::RecordDeviceIdentity` (the `device-created` record), `Renderer::DeviceInfo`; `HostPluginTests` `TestAdapterSelectionPolicy` and `TestRendererDeviceIdentity`. **Receipt (Release on the live XENEON, `.build/receipts/2026-09-07-adapter-of-output.md`):** `device-created` = AMD Radeon Graphics, LUID 0001C3CC, adapter owns window monitor: yes; only `amdxx64.dll` mapped; one LUID in `GPU Process Memory` (22.3 MiB) and `GPU Engine` (AMD 3D 13%); Matrix Focus settles at 40.1 MiB private bytes, 61.7 MiB working set, 42 threads, 1,127 handles, 2.5% of one core (was 147 threads / 153 MiB after all pages, 75 MiB Matrix-only on the old path). | `device-created` reports `adapter owns window monitor: yes` (met); GPU counters show one LUID (met); no `nvwgf2umx.dll`/`nvgpucomp64.dll` mapped (met); threads −37 or better (met: −105 vs. the all-pages baseline); PresentMon `Hardware: Independent Flip` and UI-thread CPU < 1% on Matrix Focus **still to capture**: PresentMon 2.5.1 is installed, but the CLI needs elevation and the 2026-09-07 elevated attempts hung behind pre-existing `PresentMon` / `Gv_PresentMon` ETW sessions; capture from an elevated shell with `presentmon --session_name RedXeProbe --process_name RedXe.exe --output_file <csv> --timed 12 --v2_metrics` once those sessions are stopped, or read PresentMode from the PresentMon GUI. 2.5% process CPU was measured at 60 fps continuous. |
| P1.2 | **Device follows the window.** `Renderer::EnsureDeviceForWindowMonitor` compares the LUID of the adapter owning the window's current monitor with the device's; `Application::CheckDeviceAdapter` runs it on `WM_MOVE`, `WM_EXITSIZEMOVE`, `WM_DISPLAYCHANGE`, and after `WM_DPICHANGED`; a mismatch runs the existing `CreateDeviceResources()` rebuild (the same path `RecoverDevice` uses, which already notifies widgets through `OnDeviceLost`/`OnDeviceCreated`). Release popup, Debug titled window, and XENEON re-placement all use the same rule. **Landed 2026-09-07 with the live receipt:** moving the Debug titled window onto the RTX 5080 display logged `device-created` on the NVIDIA (LUID 0001AE51, owns window monitor: yes), moving it back logged the AMD again; the process stayed alive across both rebuilds and closed with exit code 0. | Dragging the Debug window from the 4K display to the XENEON and back rebuilds the device on the target adapter each time (log shows two `device-created` records, LUIDs differ); no rebuild on moves within one adapter. |
| P1.3 | **Waitable swap chain.** `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` (also passed to `ResizeBuffers`), `IDXGISwapChain2::SetMaximumFrameLatency(1)`, device-level latency call removed. `Application::WaitForFrameLatency` waits on the waitable handle together with the message queue before `PrepareWidgets`; a message arriving first is dispatched before the frame is built; `Present` no longer blocks. The idle path (`WaitUntilMessage`) and the scheduler table are unchanged. **Landed 2026-09-07**; `TestRendererDeviceIdentity` proves the object exists, is signaled before the first frame and after a presented frame, survives resize, and is released at shutdown. | ETW shows no blocking inside `Present`; PresentMon input-to-present ≤ 2 frames; `HostPluginTests` scheduler table green. |
| P1.4 | **No redirection surface.** `WS_EX_NOREDIRECTIONBITMAP` on the top-level window. **Dropped 2026-09-07:** plain `WS_CHILD` windows (the host's native-widget containers and GdiOrbit's child) draw into the parent's redirection surface, so removing it blanks every native widget. It becomes possible only if native containers are created as `WS_EX_LAYERED` children with their own surfaces; measure that separately before reopening. | Not pursued. |
| P1.5 | Independent flip needs no child HWND over the swap chain. Matrix Focus qualifies; Gallery with GdiOrbit and any raised overlay do not. Keep "no two continuous widgets plus a 33 ms GDI timer on the shipped Gallery" (options: drop Triangle, move Orbit, or give Triangle a scheduled path) and record PresentMode per shipped page. | Default Gallery present rate is one continuous source or zero; per-page PresentMode table in the receipt. |
| P1.6 | Product option, default off: `continuousFrameRate: "display" \| 30` → `Present(2, 0)` on continuous pages. Owner: the first-page decision (Matrix Focus stays first unless product says otherwise; this plan does not make that call). | Setting parsed, documented, measured on Matrix Focus. |

### Phase 2 — make settled pages idle

| ID | Item | Pass condition |
| --- | --- | --- |
| P2.1 | **No process handles in `process.list`.** Remove the per-row `OpenProcess` from `CollectProcessesFromWalk` (`SystemData.cpp:3583-3636`). Columns 25–30 and 37–38 (affinity, WOW64, critical, EcoQoS) become `RedXeDataQualityUnavailable` in `process.list`; they move to a new optional `process.detail` dataset (10 s cadence, at most 64 rows chosen by the source's own partial sort on CPU then working set, allocation-free) for any future consumer. The Toolhelp fallback (`CollectProcesses`, `:3642-3693`) stays fallback-only. | `SystemDataTests --domains`: `process.list` ≤ 10 ms for ~700 processes; allocation-free hot path preserved; no hang > 5 s; `Plugins_API.md` column table updated. |
| P2.2 | Cache `ReachableClientRect`; move the apply-key check in `RefreshPageEdgeAffordances` above it (`Application.cpp:1533-1542`); invalidate on `WM_DISPLAYCHANGE`, `WM_SETTINGCHANGE`, `WM_DPICHANGED`, `WM_MOVE`, `WM_SIZE`. | 10 s idle ETW trace on a clocks page shows zero `EnumDisplayMonitors`. |
| P2.3 | `RequiresPlaceholderAt`: per-slot atomic status written by `ReportWidgetStatus`; no locked string scan per frame (`DashboardHost.cpp:794`, `PluginHost.cpp:1415-1433`). | No `_widgetStatusLock` acquisition in `Render`/`PrepareWidgets`. |
| P2.4 | Dedupe `OutputDebugStringW` at `Renderer.cpp:859` like the log record beside it. | One debug string per failure transition. |
| P2.5 | `PurgeExpiredLogs` only on open and day change (`PluginHost.cpp:777`, `:789`, `:917`). | Zero directory enumerations per log line. |
| P2.6 | Settings persist off the UI thread: coalesce per promote, write on the log worker, replace `MOVEFILE_WRITE_THROUGH` (`Settings.cpp:1259`) with `FlushFileBuffers` on the temp file before the atomic rename, one source copy per persist. | No file write on the UI thread during promote (ETW); stamp dedupe still holds. |
| P2.7 | **ProcessViewer wake discipline.** `GetNextFrameDelayMilliseconds` returns the maximum delay when no ease or pulse is in flight (snapshots already coalesce a frame through the host); ease only when a displayed value moved by at least one device pixel or a rank changed; ease at 30 Hz through a 33 ms scheduled delay rather than `RequestFrame` every frame; draw list as a widget member; no sample copy under the lock beyond the rows drawn. | System page settled: ≤ 2 presents/s average at 1 Hz meters (probe); UI-thread CPU ≤ 0.3% of a core. |
| P2.8 | **Atlas and DirectWrite discipline (ProcessViewer, Weather).** Upload only dirty 48×48 cells with a destination box, from the snapshot/`Prepare` path, never from `Render`; one cached DirectWrite factory + font face per module for the module's lifetime (released by `RedXePluginTrim`, P3.6); rasterize outside the draw lock; Weather does not re-raster on raise/dismiss when DPI and cell size are unchanged. | No `UpdateSubresource` in `Render` (debug layer capture); no `dwrite.dll` load after the first glyph fill. |
| P2.9 | Bound the `INFINITE` waits at `PluginHost.cpp:1643`, `:1663`, `:1894`, `:2010`, `:2204` with the existing 3 s control-lane budget and a logged timeout. | A deliberately slow fixture `RunNetworkWork` does not stall the UI thread beyond the budget. |

### Phase 3 — fluid swipe, nothing left behind

The rule: a swipe MAY allocate whatever a smooth slide needs; the settled page MUST end with exactly the resources of a
cold visit to that page, and nothing from the page that was left.

| ID | Item | Pass condition |
| --- | --- | --- |
| P3.1 | Validate only the target page in `StageTransitionPage`; the document was validated at load. Drop the second per-widget parse at `PluginManager.cpp:839-842` by keeping the validated value doc from `ValidateAppSettings`. | Stage ≤ 2 ms UI-thread CPU for the shipped document (ETW). |
| P3.2 | `ProviderBuildKey`: 64-bit hash + index instead of an 8 KiB copy (`PluginManager.h:91-96`); the 256 KiB stack frame at `PluginManager.cpp:935-937` disappears. | `PluginManager::Initialize` stack frame < 32 KiB. |
| P3.3 | **Lazy neighbor.** No `OnDeviceCreated`, `SetVisible(TRUE)`, or `SetNetworkWidgetActive(true)` for the neighbor until its first visible pixel; no teardown on direction flip (`Application.cpp:2215-2222`), stage the other side lazily; a staged-but-still neighbor MUST NOT force `Render` (`Application.cpp:3649`). Stage on gesture recognition (two-finger horizontal lock), not after the first pan frame. | Scenario F; no `gpu-ready`, `forecast-*`, or icon extraction for a cancelled swipe. |
| P3.4 | **Time-sliced construction with a frame budget.** If P3.1–P3.3 still exceed 8 ms for a shipped page, construct neighbor widgets across frames under a 4 ms per-frame budget, drawing not-yet-ready tiles as a flat page background (not the failure placeholder) until they exist. | No frame > 16.7 ms during any shipped swipe (PresentMon). |
| P3.5 | **Product option: snapshot slide.** Keep the last presented frame of a visited page as one 2560×720 or 1280×360 texture for its two neighbors (7 MiB or 1.8 MiB each on UMA), slide textures, construct after promote, release the retired page's snapshot at the settle sweep. Only if P3.4 cannot hold the frame budget. | Decision recorded with the memory cost. |
| P3.6 | **Settle sweep and `RedXePluginTrim`.** When the loop first reaches `WaitForMessage` after a promote, clear, or settle, the host runs `PluginHost::TrimIdle()`: release every provider whose active subscription count is zero (drops `SystemDataSource`), then call the new optional `extern "C" void __stdcall RedXePluginTrim() noexcept` export (same shape as `RedXePluginShutdown`, `PluginHost.cpp:1034`) on every module with zero live widgets. ProcessViewer and Weather free their atlas staging and cached DirectWrite objects, Weather frees its HTTP slabs, AV resets `coordinator` (which ends the broker process), Launcher frees icon pixels. No-unload policy unchanged; module images stay mapped. | Scenario D: private bytes within 4 MiB of scenario A; `TrimIdle` runs once per settle, never during a gesture; `HostPluginTests` proves the sweep on a hidden WARP host. |

### Phase 4 — retained memory and arenas

| ID | Item | Pass condition |
| --- | --- | --- |
| P4.1 | **Arena-backed `SystemDataSource`.** The source becomes a small object plus one `VirtualAlloc(MEM_RESERVE)` region of 16 MiB. Tables are page-aligned spans grouped by dataset family: *cheap* (summary, cpu.summary, memory, status, security; always committed, < 64 KiB), *process* (NT walk buffer, walk sample, `_processValues`, rows, names, history, `_currentCpu`; ~8 MiB), *thread* (`_threadValues`, rows, history; ~3.6 MiB), *cpu.logical* (~0.7 MiB), *gpu* (PDH array, engines, processes, values; ~1.9 MiB), *net/storage* (~1.0 MiB), *power/thermal/fan/npu* (~0.5 MiB). `CollectSnapshots` commits a family with `MEM_COMMIT` (zero pages, no memset) the first time one of its datasets is requested; the `{}` initializers on those members go away. Release through P3.6 (`VirtualFree`). Caps, `RedXeDataValue` layout, and the 16 MiB ceiling are unchanged; `thread.list` still works when a future widget subscribes. | Shipped System page: source commit ≤ 10 MiB (probe); construction ≤ 1 ms; `SystemDataTests` storage receipt reports committed bytes per family; scenario D has no source. |
| P4.2 | Host-side small allocations: `PluginHost::_providers[].dataSets[256]` sized to `descriptorCount` at provider creation (`PluginHost.h:160`, `:227`); `WidgetInstanceSettings.privateConfiguration` as a content-sized heap string instead of a 4,104-byte inline array; drop `sourceDocument` retention (re-read the file when patching); map plugin modules on first page use instead of at discovery (`PluginManager.cpp:786-832`). | Matrix Focus maps only `MatrixRain.dll`; settings shadow ≤ 64 KiB for the shipped document. |
| P4.3 | **Launcher.** Icon array slices = shortcut count (grow in steps of 4, cap 32); one arena for CPU icon bytes sized to the count, holding the PNG/ICO bytes already read (or re-extract on device loss) instead of 32 × 256 KiB BGRA; empty launcher allocates nothing; icon extraction cached by target + mtime and moved to `QueueControlWork`; `UploadIcons` uploads `display.count` slices with one hoisted blank buffer. Update the spec sentence that mandates the 8 MiB CPU copy (`Core_PerformanceAndResources.md:58-61`). | Empty launcher: 0 texture bytes; 8 icons: 2 MiB GPU, ≤ 0.5 MiB CPU; no shell call on `SetVisible` when cached. |
| P4.4 | **AV.** Overlay host attached on first raise and `Detach`ed on dismiss; tile host `Detach`ed on `SetVisible(FALSE)` (rebuild is one `Build()` + one prepare); hidden views skip `SetState`/`SetProfiles`/`SetAppearance`; destroy `ProfileControls` on close; copy only changed inventory rows. **Surface part landed 2026-09-07 through the DxUi pin `6051a8cf` (branch `perf/embedded-surface-lifetime`):** the library releases the surface of a hidden or zero-extent view, so AV's existing `SetVisible(false)` calls already drop the tile surface while hidden and the overlay surface while unraised with no plugin change; `AVControlTests` asserts `surfaceBytes` 0 while hidden and one reallocation on show. Control-tree detach, hidden-view update skipping, `ProfileControls` destruction, and inventory diffing remain. | `surfaceBytes` = tile extent × 4 when settled unraised; 0 when hidden (met); ≤ 12 KiB copied per unchanged observation cycle (open). |
| P4.5 | **Weather.** Cache bodies allocated to `response.bytes` on store in one slab; one curl easy handle per widget reused across requests; one in-flight fetch per location process-wide with a shared failure memo; keep the ≥ 5 min clamp. | Cold Weather delta excludes 2 MiB of slabs; one TLS handshake per host per cycle; no `forecast-failed` bursts during swipe testing. |
| P4.6 | **Clocks and Orbit.** DeskClock releases the old atlas before building the new scale, keys resources by device rather than provider configuration, and uses `RequestFrame` during the flap instead of a 1 ms scheduled delay; GdiOrbit stays off the shipped Gallery (P1.5) or becomes a GPU widget. | DeskClock transient ≤ 5.3 MiB; no 1 ms scheduled delays in the soak; Desk Clock soak growth explained and bounded. |

### Phase 5 — DxUi upstream and spec closeout

| ID | Item | Pass condition |
| --- | --- | --- |
| P5.1 | DxUi library round. **Landed 2026-09-07 as DxUi commit `6051a8cf496b841b6e8206ef1ff5e9daa524c63d` on `perf/embedded-surface-lifetime` (local, not pushed) and pinned by `Dependencies/DxUi.lock.json`:** surface release on hide/zero extent; `AdvanceAnimation` dirties only through control invalidation and every `Tick` that changes visual state invalidates (caret flips, transitions, tooltips, Grid/Tree/Menu animations); brush cache ≤ 256 and text-format cache ≤ 96 with `EmbeddedStatistics::cachedBrushes/cachedTextFormats`; benchmark allocation gate (clean 0, dirty ≤ 64 per frame Release, ≤ 320 Debug); the 128 MiB replacement peak documented as bounded by the 64 MiB single-surface cap; `EmbeddedTests` `TestSurfaceLifetime`, `TestTickDirtying`, `TestCacheBounds`; specs, docs, changelog, and Done plan `EmbeddedSurfaceLifetime_2026-09-07.md` in the DxUi repo. Remaining upstream items: cached popup shadow effects, a per-tick delay hint so hosts can schedule caret blinks instead of polling, and a paired performance receipt on a quiet desktop (the 2026-09-07 paired runs were noise-bound while the desktop was in use; alternating same-state runs show no direction). | Landed except the three remaining items. |
| P5.2 | `Core_PerformanceAndResources.md`: adapter-of-output and device-follows-window rules (with `UI_XeneonDisplayWindowing.md`), waitable swap chain rule, `WS_EX_NOREDIRECTIONBITMAP`, per-page whole-process budgets from P0, thread and handle budgets, soak thresholds, staging cost budget, settle sweep and `RedXePluginTrim` contract (also `Plugins_API.md`), arena rule for sources, consumer-side EmbeddedHost lifetime rule, `process.list` cost. Move measurement prose into `.build/receipts/`. | No durable number lives only in this plan. |
| P5.3 | Shipped templates and `HostPluginTests` fixtures match the P1.5/P1.6 decisions; `docs/` only if a user scenario changes. | Templates, schema, fixtures agree. |
| P5.4 | Move this plan to `Specs/Plans/Done/` and remove the active index row. | |

## Validation (every implementation phase)

```powershell
.\build.ps1 -Configuration Release
.\test.ps1 -Configuration Release
.\build.ps1 -Configuration Release -Platform ARM64
.\validate-skills.ps1
.\.build\x64\Release\RedXe.exe --probe=5
```

- Read `probe` and `device-created` records from `%LocalAppData%\RedXe\Logs\RedXe-<date>.jsonl`; compare to
  `.build/receipts/`.
- PresentMon on `RedXe.exe` per shipped page: `PresentMode`, `MsBetweenPresents` p95, input-to-present.
- WPR 60 s per scenario; WPA: CPU by thread, thread start stacks, image loads.
- `Get-Process RedXe` private bytes, working set, handles, threads; the `GPU Process Memory` and `GPU Engine`
  counters for the pid show exactly one LUID after Phase 1.
- Debug and Release x64 WARP smoke and the `HostPluginTests` scheduler table stay green. WARP Present time is never a
  hardware gate. `build.ps1` defaults to Debug; every number in this plan comes from `-Configuration Release`.
- No desktop automation. Hidden HWND + existing production host is sufficient for memory/wake probes; the adapter and
  present-mode checks need the real XENEON.

## Excluded

- Host-owned primitive batching IID.
- Raising System Data row caps, cadences, or the 16 MiB ceiling.
- Unloading plugin modules while COM objects can remain (no-unload stays; sources, textures, caches, and coordinators
  are released by the settle sweep).
- Implementing AV G1/G2/IME/AT or changing the DxUi pin from this plan.
- WebView or push providers.
- Trading Matrix Focus visual quality without the P1.6 product decision.
- `D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS` (measured: no effect on either driver).
