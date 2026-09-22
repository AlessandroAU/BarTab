#include "core/usage.hpp"
#include <algorithm>
#include <limits>

namespace usage {
bool Rect::intersects(const Rect& other) const {
    return !empty() && !other.empty() && x < other.right() && right() > other.x && y < other.bottom() && bottom() > other.y;
}
bool Rect::operator==(const Rect& other) const {
    return x == other.x && y == other.y && width == other.width && height == other.height;
}
Rect find_space(Rect panel, const std::vector<Rect>& occupied, int width, int height, int gap) {
    if (width <= 0 || height <= 0 || gap < 0 || panel.width <= panel.height || height > panel.height) return {};
    std::vector<Rect> blocks;
    for (const auto& item : occupied) if (item.intersects(panel)) blocks.push_back(item);
    std::sort(blocks.begin(), blocks.end(), [](Rect a, Rect b) { return a.x < b.x; });
    int cursor = panel.x + gap;
    int best = std::numeric_limits<int>::min();
    for (const auto& block : blocks) {
        const int end = std::min(panel.right() - gap, block.x - gap);
        if (end - cursor >= width) best = end - width;
        cursor = std::max(cursor, block.right() + gap);
    }
    if (panel.right() - gap - cursor >= width) best = panel.right() - gap - width;
    return best == std::numeric_limits<int>::min() ? Rect{} : Rect{best, panel.y + (panel.height - height) / 2, width, height};
}
void Usage::set_session(int value) { session_ = std::clamp(value, 0, 100); }
void Usage::set_weekly(int value) { weekly_ = std::clamp(value, 0, 100); }
Color bar_color(int value) {
    return value <= 15 ? Color{248, 115, 123} : value <= 30 ? Color{246, 193, 97} : accent;
}
} // namespace usage
