#pragma once

#include <windows.h>

// Capture loss is terminal even when GetPointerInfo still describes the preceding in-contact sample.
// A canceled Up is not a release/commit. Read this before falling back to the message coordinates.
[[nodiscard]] inline bool PointerMessageCancelsGesture(UINT message, WPARAM wParam) noexcept
{
    return message == WM_POINTERCAPTURECHANGED ||
           ((message == WM_POINTERDOWN || message == WM_POINTERUPDATE || message == WM_POINTERUP) &&
            IS_POINTER_CANCELED_WPARAM(wParam));
}
