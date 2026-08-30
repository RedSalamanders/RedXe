---
name: modern-cpp-windows
description: Apply the RedXe repository's modern C++ ownership, error-handling, warnings, Unicode, and formatting conventions when writing or reviewing C++ project code.
---

# Modern C++ for Windows

- Compile as Unicode `stdcpplatest` with `/W4`, `/permissive-`, SDL checks, and warnings as errors.
- Prefer value types and deterministic RAII. Use `wil::com_ptr_nothrow<T>` for COM ownership in HRESULT-based code, WIL
  unique wrappers for Windows resources, and standard smart pointers for heap ownership. Apply the `wil-raii` skill.
- Delete copying for unique resource owners. Make cleanup idempotent so partial initialization remains safe.
- Use `HRESULT` for Windows and DirectX operations. Capture `GetLastError()` immediately after a failing Win32 call.
- Do not throw through `wWinMain`, `WndProc`, or other ABI callbacks.
- Use explicit casts at signed/unsigned and pointer-sized Win32 boundaries.
- Keep headers small, include what they use, and avoid global mutable state.
- Format C++ with `.\format.ps1`; do not hand-reformat unrelated files.
- Preserve UTF-8 source files while calling wide-character Win32 APIs.

Run `.\test.ps1` after code changes. Build Release too when touching warning-sensitive or optimization-sensitive code.
