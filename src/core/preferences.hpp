#pragma once
#include <algorithm>
#include <tuple>

namespace usage {
struct SettingRange {
    int min, max, step;
    int clamp(int value) const {
        return std::clamp(value, min, max);
    }
};
namespace preference_limits {
inline constexpr SettingRange text_percent{100, 300, 10};
inline constexpr SettingRange font{0, 2, 1};
inline constexpr SettingRange accent{0, 2, 1};
inline constexpr SettingRange theme{0, 1, 1};
inline constexpr SettingRange widget_width{100, 400, 10};
inline constexpr SettingRange hover_width{240, 600, 20};
inline constexpr SettingRange hover_opacity{50, 100, 1};
inline constexpr SettingRange hover_delay{50, 1500, 50};
inline constexpr SettingRange bar_height{3, 9, 1};
inline constexpr SettingRange corner_radius{0, 12, 1};
inline constexpr SettingRange position{0, 100, 1};
inline constexpr SettingRange provider_interval{15, 900, 1};
} // namespace preference_limits

struct Appearance {
    int text_percent{150}, font{0}, accent{0}, theme{0};
    int widget_width{150}, hover_width{360}, hover_opacity{95}, hover_delay{50};
    int bar_height{7}, corner_radius{10};
    // Position along the taskbar as a percentage from the left edge.
    int position{100};
    bool show_resets{true}, hover_enabled{true};
    auto values() const {
        return std::tie(text_percent, font, accent, theme, widget_width, hover_width, hover_opacity,
                        hover_delay, bar_height, corner_radius, position, show_resets, hover_enabled);
    }
    bool operator==(const Appearance& other) const {
        return values() == other.values();
    }
    void normalize() {
        text_percent = preference_limits::text_percent.clamp(text_percent);
        font = preference_limits::font.clamp(font);
        accent = preference_limits::accent.clamp(accent);
        theme = preference_limits::theme.clamp(theme);
        widget_width = preference_limits::widget_width.clamp(widget_width);
        hover_width = preference_limits::hover_width.clamp(hover_width);
        hover_opacity = preference_limits::hover_opacity.clamp(hover_opacity);
        hover_delay = preference_limits::hover_delay.clamp(hover_delay);
        bar_height = preference_limits::bar_height.clamp(bar_height);
        corner_radius = preference_limits::corner_radius.clamp(corner_radius);
        position = preference_limits::position.clamp(position);
    }
};
struct Preferences {
    Appearance appearance;
    bool codex_enabled{true}, claude_enabled{true};
    int codex_interval{60}, claude_interval{60};
    bool operator==(const Preferences& other) const {
        return appearance == other.appearance && codex_enabled == other.codex_enabled &&
               claude_enabled == other.claude_enabled && codex_interval == other.codex_interval &&
               claude_interval == other.claude_interval;
    }
    void normalize() {
        appearance.normalize();
        codex_interval = preference_limits::provider_interval.clamp(codex_interval);
        claude_interval = preference_limits::provider_interval.clamp(claude_interval);
    }
};
} // namespace usage
