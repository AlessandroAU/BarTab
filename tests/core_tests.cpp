#include "core/usage.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace usage;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void placement_tests() {
    Rect panel{0, 1152, 1920, 48};
    std::vector<Rect> occupied{{6, 1152, 152, 48}, {584, 1152, 753, 48}, {1566, 1152, 354, 48}};
    auto target = find_space(panel, occupied, 208, 38, 8);
    require(target == Rect{1350, 1157, 208, 38}, "Right-hand taskbar gap");
    for (auto block : occupied)
        require(!target.intersects(block), "No button overlap");
    occupied.push_back({1337, 1152, 230, 48});
    require(find_space(panel, occupied, 208, 38, 8).right() == 576, "Fallback to left gap");
    occupied.push_back({158, 1152, 426, 48});
    require(find_space(panel, occupied, 208, 38, 8).empty(), "No space hides widget");
    require(find_space({-1920, 0, 1920, 72}, {{-300, 0, 300, 72}}, 312, 57, 12) == Rect{-624, 7, 312, 57},
            "DPI and negative coordinates");
    require(find_space({0, 0, 48, 1000}, {}, 208, 38, 8).empty(), "Vertical panel unsupported");
    require(find_space(panel, {}, 0, 38, 8).empty(), "Invalid dimensions rejected");
    require(find_space(panel, {{-50, 1152, 150, 48}, {50, 1152, 500, 48}, {1800, 1152, 300, 48}}, 208, 38,
                       8) == Rect{1584, 1157, 208, 38},
            "Overlapping and out-of-bounds blocks");
    // A range of panel layouts verifies the placement invariant independently of exact coordinates.
    for (int width = 240; width <= 2000; width += 31) {
        Rect bar{0, 0, width, 48};
        std::vector<Rect> blocks{{0, 0, width / 4, 48}, {width / 2, 0, 75, 48}, {width - 90, 0, 90, 48}};
        auto candidate = find_space(bar, blocks, 208, 38, 8);
        if (candidate.empty())
            continue;
        require(candidate.x >= 8 && candidate.right() <= width - 8, "Within panel");
        for (auto block : blocks)
            require(!candidate.intersects(block), "Collision invariant");
    }
}
void placement_preference_tests() {
    Rect panel{0, 1152, 1920, 48};
    // Two gaps fit a 208-wide widget here: 166..576 and 1345..1558.
    const std::vector<Rect> occupied{{6, 1152, 152, 48}, {584, 1152, 753, 48}, {1566, 1152, 354, 48}};
    require(find_space(panel, occupied, 208, 38, 8, 100) == find_space(panel, occupied, 208, 38, 8),
            "Hard right is the unchanged default");
    require(find_space(panel, occupied, 208, 38, 8, 0) == Rect{166, 1157, 208, 38},
            "0% reaches the first gap");
    require(find_space(panel, occupied, 208, 38, 8, 100) == Rect{1350, 1157, 208, 38},
            "100% reaches the last gap");
    // The middle of this taskbar is covered, so 50% sits as close to it as the nearest gap allows.
    require(find_space(panel, occupied, 208, 38, 8, 50) == Rect{368, 1157, 208, 38},
            "50% picks the nearest gap");
    // Within a gap the slider positions the widget directly.
    require(find_space(panel, occupied, 208, 38, 8, 20) == Rect{347, 1157, 208, 38},
            "Intermediate position inside a gap");
    // A clear taskbar honours the percentage exactly.
    require(find_space(panel, {}, 208, 38, 8, 0) == Rect{8, 1157, 208, 38}, "0% on an empty taskbar");
    require(find_space(panel, {}, 208, 38, 8, 50) == Rect{856, 1157, 208, 38}, "50% on an empty taskbar");
    require(find_space(panel, {}, 208, 38, 8, 100) == Rect{1704, 1157, 208, 38}, "100% on an empty taskbar");
    require(find_space(panel, {}, 208, 38, 8, 25) == Rect{432, 1157, 208, 38}, "25% on an empty taskbar");
    // Out-of-range values clamp rather than escaping the panel.
    require(find_space(panel, {}, 208, 38, 8, -50) == find_space(panel, {}, 208, 38, 8, 0),
            "Below 0% clamps");
    require(find_space(panel, {}, 208, 38, 8, 150) == find_space(panel, {}, 208, 38, 8, 100),
            "Above 100% clamps");
    // Sliding across the whole range never overlaps a button or leaves the panel.
    int distinct = 0;
    Rect previous{};
    for (int position = 0; position <= 100; ++position) {
        auto candidate = find_space(panel, occupied, 208, 38, 8, position);
        require(!candidate.empty(), "A gap fits at every position");
        require(candidate.x >= 8 && candidate.right() <= panel.width - 8,
                "Positioned widget stays in the panel");
        for (auto block : occupied)
            require(!candidate.intersects(block), "Position never overlaps a button");
        if (!(candidate == previous)) {
            ++distinct;
            previous = candidate;
        }
    }
    require(distinct > 10, "The slider moves the widget across many positions");
}
void preference_tests() {
    Preferences settings;
    require(settings.appearance.text_percent == 100 && settings.appearance.hover_text_percent == 100,
            "Default taskbar and hover text sizes are 100 percent");
    require(settings.appearance.taskbar_text_scale() == 150 && settings.appearance.hover_text_scale() == 130,
            "100 percent draws the taskbar at 1.5x and the hover card at 1.3x");
    require(settings.appearance.bold_taskbar && settings.appearance.bold_hover && !settings.appearance.bold_settings,
            "Taskbar and hover text default to bold, settings to regular");
    settings.appearance.text_percent = 400;
    settings.appearance.hover_text_percent = 10;
    settings.appearance.hover_opacity = -1;
    settings.appearance.widget_width = 1;
    settings.codex_interval = 1;
    settings.claude_interval = 10000;
    settings.normalize();
    require(settings.appearance.text_percent == 200 && settings.appearance.hover_text_percent == 65,
            "Appearance values are bounded");
    require(settings.appearance.hover_opacity == 50 && settings.appearance.widget_width == 100,
            "Visibility and minimum width are protected");
    require(settings.codex_interval == 15 && settings.claude_interval == 900,
            "Polling intervals are bounded independently");
    settings.appearance = Appearance{};
    require(settings.appearance == Appearance{} && settings.codex_interval == 15,
            "Appearance defaults preserve provider choices");
}
void usage_tests() {
    Usage data;
    require(data.session() == 62 && data.weekly() == 81, "Default demo percentages");
    data.set_session(-50);
    data.set_weekly(150);
    require(data.session() == 0 && data.weekly() == 100, "Clamp percentages");
    for (int percent : {0, 15, 16, 30, 31, 62, 100}) {
        data.set_session(percent);
        data.set_weekly(percent);
        const Color expected = percent <= 15   ? Color{248, 115, 123}
                               : percent <= 30 ? Color{246, 193, 97}
                                               : accent;
        require(bar_color(percent) == expected, "Warning thresholds");
    }
}
void reset_tests() {
    const std::int64_t now = 1'800'000'000;
    AccountUsage before;
    before.updated = 1;
    before.windows = {{"5 hour", 20, now + 600}, {"Weekly", 60, now + 86400}};
    auto after = before;
    require(reset_windows(before, after, now).empty(), "An unchanged reading is no reset");
    after.windows[0].remaining = 35;
    after.windows[0].resets_at += 40;
    require(reset_windows(before, after, now).empty(), "Growth before the reset time is no reset");
    require(reset_windows(before, after, now + 600) == std::vector<std::string>{"5 hour"},
            "Growth once the reset time passes is a reset");
    after.windows[0] = {"5 hour", 100, now + 5 * 3600};
    require(reset_windows(before, after, now) == std::vector<std::string>{"5 hour"},
            "A reset time jumping ahead with the allowance back is an early reset");
    after.windows[1].remaining = 100;
    after.windows[1].resets_at = now + 7 * 86400;
    require(reset_windows(before, after, now).size() == 2, "Every window that reset is reported");
    auto first = before;
    first.updated = 0;
    require(reset_windows(first, after, now).empty(), "The first reading never celebrates");
    auto failed = after;
    failed.error = "login";
    require(reset_windows(before, failed, now).empty() && reset_windows(failed, after, now).empty(),
            "Readings either side of an error never celebrate");
    after = before;
    after.windows[0].remaining = 10;
    after.windows[0].resets_at = now + 5 * 3600;
    require(reset_windows(before, after, now + 600).empty(), "A shrinking allowance is no reset");
}
} // namespace
int main() {
    try {
        placement_tests();
        placement_preference_tests();
        usage_tests();
        reset_tests();
        preference_tests();
        std::cout << "PASS: placement, position preferences, collision invariants, scaling, usage limits, "
                     "reset detection and warning thresholds.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
