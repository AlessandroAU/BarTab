#include "core/usage.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace usage;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void placement_tests() {
    Rect panel{0, 1152, 1920, 48};
    std::vector<Rect> occupied{{6,1152,152,48}, {584,1152,753,48}, {1566,1152,354,48}};
    auto target = find_space(panel, occupied, 208, 38, 8);
    require(target == Rect{1350,1157,208,38}, "Right-hand taskbar gap");
    for (auto block : occupied) require(!target.intersects(block), "No button overlap");
    occupied.push_back({1337,1152,230,48});
    require(find_space(panel, occupied, 208, 38, 8).right() == 576, "Fallback to left gap");
    occupied.push_back({158,1152,426,48});
    require(find_space(panel, occupied, 208, 38, 8).empty(), "No space hides widget");
    require(find_space({-1920,0,1920,72}, {{-300,0,300,72}}, 312,57,12) == Rect{-624,7,312,57}, "DPI and negative coordinates");
    require(find_space({0,0,48,1000}, {},208,38,8).empty(), "Vertical panel unsupported");
    require(find_space(panel, {},0,38,8).empty(), "Invalid dimensions rejected");
    require(find_space(panel, {{-50,1152,150,48}, {50,1152,500,48}, {1800,1152,300,48}},208,38,8)
        == Rect{1584,1157,208,38}, "Overlapping and out-of-bounds blocks");
    // A range of panel layouts verifies the placement invariant independently of exact coordinates.
    for (int width = 240; width <= 2000; width += 31) {
        Rect bar{0,0,width,48};
        std::vector<Rect> blocks{{0,0,width/4,48}, {width/2,0,75,48}, {width-90,0,90,48}};
        auto candidate = find_space(bar, blocks,208,38,8);
        if (candidate.empty()) continue;
        require(candidate.x >= 8 && candidate.right() <= width - 8, "Within panel");
        for (auto block : blocks) require(!candidate.intersects(block), "Collision invariant");
    }
}
void preference_tests() {
    Preferences settings;
    require(settings.appearance.text_percent==150,"Default text size is 150 percent");
    settings.appearance.text_percent=400; settings.appearance.font=99;
    settings.appearance.hover_opacity=-1; settings.appearance.widget_width=1; settings.appearance.hover_width=1;
    settings.codex_interval=1; settings.claude_interval=10000;
    settings.normalize();
    require(settings.appearance.text_percent==300 && settings.appearance.font==2,"Appearance values are bounded");
    require(settings.appearance.hover_opacity==50 && settings.appearance.widget_width==100 && settings.appearance.hover_width==240,"Visibility and minimum width are protected");
    require(settings.codex_interval==15 && settings.claude_interval==900,"Polling intervals are bounded independently");
    settings.appearance=Appearance{};
    require(settings.appearance==Appearance{} && settings.codex_interval==15,"Appearance defaults preserve provider choices");
}
void usage_tests() {
    Usage data;
    require(data.session() == 62 && data.weekly() == 81, "Default demo percentages");
    data.set_session(-50); data.set_weekly(150);
    require(data.session() == 0 && data.weekly() == 100, "Clamp percentages");
    for (int percent : {0,15,16,30,31,62,100}) {
        data.set_session(percent); data.set_weekly(percent);
        const Color expected = percent <= 15 ? Color{248,115,123} : percent <= 30 ? Color{246,193,97} : accent;
        require(bar_color(percent) == expected, "Warning thresholds");
    }
}
}
int main() {
    try { placement_tests(); usage_tests(); preference_tests(); std::cout << "PASS: placement, collision invariants, scaling, usage limits and warning thresholds.\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
