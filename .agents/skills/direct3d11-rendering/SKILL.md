---
name: direct3d11-rendering
description: Implement or debug this project's Direct3D 11 renderer, including devices, DXGI swap chains, shaders, buffers, resize, presentation, WARP, and device loss. Do not use for unrelated Win32 message handling.
---

# Direct3D 11 rendering

Read `Specs/UI/UI_XeneonDisplayWindowing.md` when renderer work changes swap-chain dimensions, resize behavior,
fullscreen interaction, or the hidden WARP validation contract. Update the spec in the same change when observable
behavior changes.

Read `Specs/Core/Core_PerformanceAndResources.md` for every rendering change. Cache size/DPI-derived state, avoid
steady-state allocations, batch uploads and compatible draws, and never spin while rendering is suspended or occluded.

Keep host graphics ownership inside `RedXe/Renderer.*`; plugin-specific shaders, geometry, and buffers stay inside
their plugin projects.

- Own COM interfaces with `wil::com_ptr_nothrow`; receive interfaces through `.put()`, access them through `.get()`,
  and never call `Release()` manually. Apply the `wil-raii` skill when ownership changes.
- Keep device resources and size-dependent resources distinct. A resize replaces the render target; device loss rebuilds
  the device, swap chain, pipeline, buffers, and target as one coherent operation.
- Use a two-buffer flip-model swap chain. Keep `DXGI_MWA_NO_ALT_ENTER` unless the project adds an explicit fullscreen
  policy.
- Treat zero width or height as suspended rendering.
- Handle `DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET`, and `DXGI_ERROR_DRIVER_INTERNAL_ERROR` by recreating
  resources. Do not log handled occlusion as an error.
- Register a DXGI factory occlusion-status window message. After `DXGI_STATUS_OCCLUDED`, stop building frames, wait
  for that message, and probe recovery with `Present(0, DXGI_PRESENT_TEST)`. Do not poll occlusion or use a periodic
  timer.
- Check every resource-creation `HRESULT` before using the result.
- Notify GPU widgets after device creation and before device release. Rebind the host target and widget viewport before
  every plugin render callback.
- Compile bundled-plugin HLSL at build time, embed the bytecode, and keep shader input layouts, C++ vertex data, and
  HLSL semantics synchronized in the same change. Do not add a runtime `d3dcompiler` dependency for fixed shaders.
- Preserve the `--warp` path and run `.\test.ps1` after renderer changes.

Rendering happens on the idle side of the UI message loop. Do not move real-time drawing into `WM_PAINT`.

If the work closes a WIP plan, merge lasting rendering requirements into the owning domain spec before moving the plan
to `Specs/Plans/Done/`. Apply the `spec-workflow` skill for that closeout.
