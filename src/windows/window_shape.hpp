#pragma once
#include <windows.h>

namespace usage::windows {
inline void round_window(HWND window) {
    RECT bounds{};
    GetClientRect(window, &bounds);
    const UINT dpi = GetDpiForWindow(window);
    const int diameter = MulDiv(24, dpi ? dpi : 96, 96);
    HRGN region = CreateRoundRectRgn(0, 0, bounds.right + 1, bounds.bottom + 1, diameter, diameter);
    // Windows owns the region after a successful SetWindowRgn.
    if (region && !SetWindowRgn(window, region, TRUE))
        DeleteObject(region);
}
} // namespace usage::windows
