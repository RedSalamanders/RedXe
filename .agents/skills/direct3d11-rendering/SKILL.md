---
name: direct3d11-rendering
description: Implement or debug this project's Direct3D 11 renderer, including devices, DXGI swap chains, shaders, buffers, resize, presentation, WARP, and device loss. Do not use for unrelated Win32 message handling.
---

# Direct3D 11 rendering

Read `Specs/UI/UI_XeneonDisplayWindowing.md` when renderer work changes swap-chain dimensions, resize behavior,
fullscreen interaction, or the hidden WARP validation contract. Update the spec in the same change when observable
behavior changes.

Keep graphics ownership inside `src/RedXe/Renderer.*`.

- Own COM interfaces with `wil::com_ptr_nothrow`; receive interfaces through `.put()`, access them through `.get()`,
  and never call `Release()` manually. Apply the `wil-raii` skill when ownership changes.
- Keep device resources and size-dependent resources distinct. A resize replaces the render target; device loss rebuilds
  the device, swap chain, pipeline, buffers, and target as one coherent operation.
- Use a two-buffer flip-model swap chain. Keep `DXGI_MWA_NO_ALT_ENTER` unless the project adds an explicit fullscreen
  policy.
- Treat zero width or height as suspended rendering.
- Handle `DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET`, and `DXGI_ERROR_DRIVER_INTERNAL_ERROR` by recreating
  resources. Do not log handled occlusion as an error.
- Check every resource-creation `HRESULT` before using the result.
- Keep shader input layouts, C++ vertex data, and HLSL semantics synchronized in the same change.
- Preserve the `--warp` path and run `.\test.ps1` after renderer changes.

Rendering happens on the idle side of the UI message loop. Do not move real-time drawing into `WM_PAINT`.

If the work closes a WIP plan, merge lasting rendering requirements into the owning domain spec before moving the plan
to `Specs/Plans/Done/`. Apply the `spec-workflow` skill for that closeout.
