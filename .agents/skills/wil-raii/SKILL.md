---
name: wil-raii
description: Use WIL RAII for RedXe Windows handles, COM interfaces, and cleanup. Apply when creating, owning, transferring, or releasing HWND, HICON, HBITMAP, HDC, COM objects, or temporary cleanup actions.
---

# WIL RAII

Use the WIL package from `vcpkg.json`. Do not add a second WIL copy or return to WRL.

## Required patterns

- Use `wil::com_ptr_nothrow<T>` in `noexcept`/`HRESULT` code, `.put()` for output parameters, `.get()` for borrowed
  access, and `.reset()` for release. Use `wil::com_ptr<T>` only in code that intentionally uses WIL exceptions. Never
  call `Release()` manually.
- Use `wil::unique_hwnd`, `wil::unique_hicon`, `wil::unique_hbitmap`, `wil::unique_hdc`, and other matching wrappers for
  owned Windows resources.
- Destroy an owned HWND with `owner.reset()`, not `DestroyWindow(owner.get())`. Use `.release()` only for a documented
  ownership transfer or to clear an already-destroyed HWND during `WM_NCDESTROY`.
- Use `wil::scope_exit` for temporary cleanup that has no dedicated unique wrapper. Do not add `goto` cleanup paths.
- Keep every resource under one explicit owner immediately after successful creation. Cleanup must remain safe after
  partial initialization.

Wrap the WIL include locally because the project treats its own warnings as errors:

```cpp
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>      // when wil::com_ptr is needed
#include <wil/resource.h>
#pragma warning(pop)
```

Run `.\test.ps1` after ownership changes. Build ARM64 too when changing declarations shared across platforms.
