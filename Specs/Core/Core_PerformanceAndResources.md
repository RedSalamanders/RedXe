# RedXe performance and resource contract

Status: current normative contract
Last reviewed: 2026-08-31

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
- After `Present` reports occlusion, RedXe must stop frame construction, wait for the DXGI factory's registered
  occlusion-status window message, and use `DXGI_PRESENT_TEST` to detect recovery without presenting content.
  Occlusion polling and periodic timers are prohibited.
- The single host swap chain uses a maximum frame latency of one so the CPU does not queue unnecessary frames.
- Static pages must render only after explicit invalidation. Resize, DPI, settings changes that alter the active
  runtime, `WM_PAINT`, show/display recovery, and occlusion/device recovery invalidate one coalesced frame. Mouse,
  cursor, keyboard, native-child timer, and other unrelated dispatched messages MUST NOT cause a host `Present`.
  Future plugin invalidation must be coalesced before waking the UI thread.
- Plugin `Render` calls use borrowed frame and D3D context records. Render, resize, and visibility callbacks must not
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
  quiesced when their owning feature is no longer active, except for the documented v1 no-unload plugin policy.
- Background acquisition must block on events or timers at the lowest useful rate, coalesce redundant samples, and
  keep bounded history. Busy-waiting is prohibited.
- Logging and diagnostics must not format or emit per-frame success messages.

The frozen immediate-context GPU interface remains appropriate for Matrix-class work. A new production family of
cheap host primitives MUST NOT be added as separate immediate-context callbacks without measurement. When such a
consumer exists, the design review MUST first evaluate a host-owned bounded command/batch mechanism under a new IID;
the generic widget root and frozen `IRedXeGpuWidget` vtable remain unchanged.

## ABI and data-layout rules

- Hot ABI records contain only fields consumed on the hot path. They do not carry speculative reserved arrays.
- Widget rendering mechanisms and breaking object-interface changes use new COM IIDs. Generic roots must not absorb
  plugin-specific drawing records or implementation concepts.
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
- The plugin contract test must validate factory behavior, borrowed metadata, rendering-IID negotiation, and COM
  identity.
- The multi-widget WARP smoke frame must notify the GPU widgets of device creation, render every configured widget,
  and notify them before device release.
- Review must confirm that steady-state GPU callbacks allocate no heap memory and reuse bounded device resources.
- `HostPluginTests` must exercise the production `PluginManager`, `DashboardHost`, and `Renderer` with a hidden
  off-screen HWND and WARP. It must verify the scheduler decision table for hidden, minimized/suspended, display-off,
  occluded, clean-static, invalidated-static, and continuous states without automating the desktop. The scheduler
  input MUST NOT contain an any-message redraw proxy.
- Changes to the acquisition of operating-system visibility, power, or DXGI occlusion signals that are not represented
  by the scheduler decision seam additionally require a live check that inactive windows do not spin and recovery
  resumes rendering.
