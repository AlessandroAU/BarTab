#include "ui/pixels.hpp"
#include <algorithm>
#include <cmath>

namespace usage::ui {
namespace {
std::uint32_t scale(std::uint32_t pixel, float factor) {
    const auto channel = [&](int shift) {
        return static_cast<std::uint32_t>(std::lround(static_cast<float>((pixel >> shift) & 0xff) * factor))
               << shift;
    };
    return channel(24) | channel(16) | channel(8) | channel(0);
}
} // namespace
void round_corners(Pixels& pixels, int radius, int top, int bottom) {
    if (bottom < 0 || bottom > pixels.height)
        bottom = pixels.height;
    top = std::clamp(top, 0, bottom);
    radius = std::min({radius, pixels.width / 2, (bottom - top) / 2});
    for (int y = 0; y < pixels.height; ++y) {
        auto* row = pixels.data.data() + static_cast<std::size_t>(y) * pixels.width;
        if (y < top || y >= bottom) {
            std::fill(row, row + pixels.width, 0u);
            continue;
        }
        // Distance into the corner band, measured from pixel centres.
        const float dy = y < top + radius       ? static_cast<float>(top + radius - y) - 0.5f
                         : y >= bottom - radius ? static_cast<float>(y - (bottom - radius)) + 0.5f
                                                : 0.f;
        if (dy <= 0.f || radius <= 0)
            continue;
        for (int x = 0; x < radius; ++x) {
            const float dx = static_cast<float>(radius - x) - 0.5f;
            const float coverage =
                std::clamp(static_cast<float>(radius) - std::sqrt(dx * dx + dy * dy) + 0.5f, 0.f, 1.f);
            if (coverage >= 1.f)
                break;
            row[x] = scale(row[x], coverage);
            row[pixels.width - 1 - x] = scale(row[pixels.width - 1 - x], coverage);
        }
    }
}
void fade(Pixels& pixels, float opacity) {
    opacity = std::clamp(opacity, 0.f, 1.f);
    if (opacity >= 1.f)
        return;
    for (auto& pixel : pixels.data)
        pixel = scale(pixel, opacity);
}
void fill_behind(Pixels& pixels, Color color, std::uint8_t alpha) {
    const std::uint32_t back = scale((0xffu << 24) | (static_cast<std::uint32_t>(color.r) << 16) |
                                         (static_cast<std::uint32_t>(color.g) << 8) | color.b,
                                     alpha / 255.f);
    for (auto& pixel : pixels.data) {
        const std::uint32_t source_alpha = pixel >> 24;
        // Premultiplied "over": source + backdrop * (1 - source alpha).
        const std::uint32_t under = scale(back, static_cast<float>(255 - source_alpha) / 255.f);
        std::uint32_t result = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            const auto sum = ((pixel >> shift) & 0xff) + ((under >> shift) & 0xff);
            result |= std::min<std::uint32_t>(sum, 255) << shift;
        }
        pixel = result;
    }
}
void shift_rows(Pixels& pixels, int offset) {
    const auto width = static_cast<std::size_t>(pixels.width);
    if (offset == 0 || width == 0)
        return;
    const int height = pixels.height;
    if (std::abs(offset) >= height) {
        std::fill(pixels.data.begin(), pixels.data.end(), 0u);
        return;
    }
    auto* data = pixels.data.data();
    if (offset > 0) {
        std::copy_backward(data, data + (height - offset) * width, data + height * width);
        std::fill(data, data + offset * width, 0u);
    } else {
        std::copy(data - offset * width, data + height * width, data);
        std::fill(data + (height + offset) * width, data + height * width, 0u);
    }
}
} // namespace usage::ui
