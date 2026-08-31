---
name: win32-windowing
description: Implement or revise this project's native Win32 window, WndProc routing, message loop, input, DPI changes, resize, and HWND lifetime. Do not use for Direct3D pipeline or shader work.
---

# Win32 Windowing

Before changing startup window mode, display selection, sizing, or DPI behavior, read
`Specs/UI/UI_XeneonDisplayWindowing.md`. It is the normative contract; update it in the same change when intended
behavior changes.

Work in `RedXe/Application.*` and keep the callback boundary narrow.

- Bind `Application*` from `CREATESTRUCTW::lpCreateParams` during `WM_NCCREATE`, store it in `GWLP_USERDATA`, and
  clear it during `WM_NCDESTROY`.
- Route messages to small handlers when logic exceeds a few lines.
- No exception may cross `WindowProcedure`; use `HRESULT` or explicit state for fallible work.
- Forward `WM_SIZE` dimensions to `Renderer::Resize`. A minimized zero-sized client area is normal.
- For `WM_DPICHANGED`, follow the domain spec: use the suggested destination, recompute a titled window's outer frame
  for the target DPI and logical client canvas, and leave fullscreen popup bounds monitor-controlled.
- Suppress background erase and validate `WM_PAINT`; the message-loop idle path owns continuous rendering.
- Do not use `CS_HREDRAW` or `CS_VREDRAW` for the real-time render window. Subscribe to
  `GUID_SESSION_DISPLAY_STATUS` and block rendering while the session display is powered off.
- Own the top-level window with `wil::unique_hwnd`. Destroy it through `.reset()`, release stale ownership during
  `WM_NCDESTROY` when needed, and unregister the class after the window is gone. Apply the `wil-raii` skill for other
  handles.
- Keep hidden-window creation working because `.\test.ps1` depends on it. Message dispatch must never make a hidden,
  minimized, suspended, or display-off window render one extra frame.

After lifetime, DPI, or message-loop changes, run the Debug and Release checks required by the domain spec and manually
launch `.\build.ps1 -Run` when interactive behavior changed. DPI changes also require a live cross-monitor move test
with a Per-Monitor-V2 probe so diagnostic virtualization cannot hide a sizing defect.

If the work closes a WIP plan, merge lasting requirements into the domain spec before moving the plan to
`Specs/Plans/Done/`. Apply the `spec-workflow` skill for that closeout.
