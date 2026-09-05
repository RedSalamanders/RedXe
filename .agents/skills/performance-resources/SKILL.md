---
name: performance-resources
description: Design, implement, or review RedXe CPU, memory, allocation, batching, wake-up, idle, and resource-lifetime behavior. Use for every material architecture or hot-path change.
---

# RedXe performance and resources

Read `Specs/Core/Core_PerformanceAndResources.md` before material architecture, rendering, plugin, data-acquisition,
or background-work changes. Its requirements are mandatory across the repository. Launcher icon textures,
launch-animation `RequestFrame`, and forbidding extract/decode in `Render` are owned by
`Specs/Core/Core_PerformanceAndResources.md` and `Specs/Plugins/Plugins_API.md`.

- Preserve correctness, security, visual quality, and required behavior first; among correct designs choose the
  lowest steady-state resource cost.
- Make steady-state hot paths allocation-free with bounded reusable storage.
- Cache derived state until an input changes and batch compatible copies, uploads, state changes, and draw calls.
- Block on messages, events, timers, or presentation pacing when work is unavailable. Never add a busy loop.
  Page pan/settle and raise/dismiss settle are presentation-paced motion; settled overlay chrome and close hover are
  not. The JSONL log writer is one event-blocked thread with a 32×1024 ring; `IRedXeHost::Log` must not allocate,
  wait on disk, or emit per-frame success. The writer uses UTC-dated files and retention, not a 1 MiB rotate.
- Keep histories, queues, caches, device resources, and plugin output bounded. Fail locally on capacity exhaustion.
- Prefer borrowed immutable views for synchronous data and explicit ownership for retained data.
- Create expensive resources lazily and release or quiesce them when inactive unless a normative lifetime rule says
  otherwise.
- Measure before accepting a regression or a complexity-increasing micro-optimization. Record justified regressions
  in the owning normative spec or active plan.

Validation must cover the affected Debug/Release and x64/ARM64 configurations named by the owning spec. Rendering
changes keep the WARP smoke test green and verify upload/draw counters when those counters are part of the contract.
