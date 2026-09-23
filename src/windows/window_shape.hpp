#pragma once
#include <windows.h>

namespace usage::windows {
// Rounds the window, or only the rows from `top` to `bottom` of it, leaving
// the rest invisible.
inline void round_window(HWND window, int top = 0, int bottom = -1) {
    RECT bounds{};
    GetClientRect(window, &bounds);
    if (bottom < 0)
        bottom = bounds.bottom;
    const UINT dpi = GetDpiForWindow(window);
    const int diameter = MulDiv(24, dpi ? dpi : 96, 96);
    HRGN region = CreateRoundRectRgn(0, top, bounds.right + 1, bottom + 1, diameter, diameter);
    // Windows owns the region after a successful SetWindowRgn.
    if (region && !SetWindowRgn(window, region, TRUE))
        DeleteObject(region);
}
} // namespace usage::windows
