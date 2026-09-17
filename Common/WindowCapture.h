#pragma once

// Captures one top-level window of this process through Windows.Graphics.Capture, the only path that sees a
// flip-model swap chain and DirectComposition content, and writes it as a PNG. Nothing here activates, moves, or
// sends input to the window; the caller supplies a visible, non-minimized window and an output path it may write.
// Shared by RedXe's `--screenshot` mode (documentation captures) and the host tests.

#include <windows.h>

namespace RedXe
{
// Fails with E_INVALIDARG for a window of another process, a hidden window, or a minimized window; E_NOTIMPL when
// the OS has no capture support; ERROR_TIMEOUT when no frame arrives within 5 s. The PNG is 32-bit BGRA at the
// window's content size, or, when clientCrop is given (client pixel coordinates), that rectangle clipped to the
// client area; an empty intersection fails with E_INVALIDARG.
[[nodiscard]] HRESULT SaveWindowScreenshot(HWND window, const wchar_t* pngPath,
                                           const RECT* clientCrop = nullptr) noexcept;
} // namespace RedXe
