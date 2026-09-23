#pragma once
#include "core/usage.hpp"
#include <cstdint>
#include <vector>

namespace usage::ui {
// A rendered surface: top-down rows of premultiplied 0xAARRGGBB, which is BGRA
// in memory on little-endian machines. Windows' layered windows and X11's
// 32-bit ARGB visuals both take it as is.
struct Pixels {
    int width{}, height{};
    std::vector<std::uint32_t> data;
};

// Software stand-ins for effects Windows gets from the window manager, used by
// hosts whose windows can only take pixels.

// Clears everything outside a rectangle with rounded corners of `radius` pixels
// spanning rows [top, bottom) and the full width; the curve is antialiased.
void round_corners(Pixels& pixels, int radius, int top = 0, int bottom = -1);
// Multiplies every pixel's opacity by `opacity` (0..1).
void fade(Pixels& pixels, float opacity);
// Composites the pixels over a solid `color` whose own opacity is `alpha`.
void fill_behind(Pixels& pixels, Color color, std::uint8_t alpha);
// Moves the image down by `offset` rows (up when negative), clearing what it uncovers.
void shift_rows(Pixels& pixels, int offset);
} // namespace usage::ui
