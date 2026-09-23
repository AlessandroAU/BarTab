#include "core/usage.hpp"
#include <algorithm>
#include <limits>

namespace usage {
bool Rect::intersects(const Rect& other) const {
    return !empty() && !other.empty() && x < other.right() && right() > other.x && y < other.bottom() &&
           bottom() > other.y;
}
bool Rect::operator==(const Rect& other) const {
    return x == other.x && y == other.y && width == other.width && height == other.height;
}
Rect find_space(Rect panel, const std::vector<Rect>& occupied, int width, int height, int gap, int position) {
    if (width <= 0 || height <= 0 || gap < 0 || panel.width <= panel.height || height > panel.height)
        return {};
    std::vector<Rect> blocks;
    for (const auto& item : occupied)
        if (item.intersects(panel))
            blocks.push_back(item);
    std::sort(blocks.begin(), blocks.end(), [](Rect a, Rect b) { return a.x < b.x; });
    // Every run of free taskbar space wide enough to hold the widget, left to right.
    std::vector<std::pair<int, int>> gaps;
    int cursor = panel.x + gap;
    for (const auto& block : blocks) {
        const int end = std::min(panel.right() - gap, block.x - gap);
        if (end - cursor >= width)
            gaps.emplace_back(cursor, end);
        cursor = std::max(cursor, block.right() + gap);
    }
    if (panel.right() - gap - cursor >= width)
        gaps.emplace_back(cursor, panel.right() - gap);
    if (gaps.empty())
        return {};
    // Where the widget would sit if the taskbar were empty, then the reachable
    // point closest to it. Ties keep the leftmost gap.
    const int span = std::max(0, panel.width - 2 * gap - width);
    const int ideal = panel.x + gap + span * std::clamp(position, 0, 100) / 100;
    int x = 0, best = std::numeric_limits<int>::max();
    for (const auto& candidate : gaps) {
        const int reachable = std::clamp(ideal, candidate.first, candidate.second - width);
        const int distance = std::abs(reachable - ideal);
        if (distance < best) {
            best = distance;
            x = reachable;
        }
    }
    return {x, panel.y + (panel.height - height) / 2, width, height};
}
void Usage::set_session(int value) {
    session_ = std::clamp(value, 0, 100);
}
void Usage::set_weekly(int value) {
    weekly_ = std::clamp(value, 0, 100);
}
std::vector<std::string> reset_windows(const AccountUsage& before, const AccountUsage& after,
                                       std::int64_t now) {
    // Refreshes nudge reset times by seconds; a real rollover moves them by hours.
    constexpr std::int64_t jump = 30 * 60;
    std::vector<std::string> result;
    if (!before.updated || !before.error.empty() || !after.error.empty())
        return result;
    for (const auto& next : after.windows)
        for (const auto& last : before.windows)
            if (last.label == next.label && last.resets_at > 0 && next.remaining > last.remaining &&
                (now >= last.resets_at || next.resets_at >= last.resets_at + jump))
                result.push_back(next.label);
    return result;
}
Color bar_color(int value) {
    return value <= 15 ? Color{248, 115, 123} : value <= 30 ? Color{246, 193, 97} : accent;
}
} // namespace usage
