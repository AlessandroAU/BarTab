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
// Text sizes are relative to each surface's base scale below, in 5% steps.
inline constexpr SettingRange text_percent{65, 200, 5};
inline constexpr SettingRange widget_width{100, 400, 10};
inline constexpr SettingRange widget_height{16, 80, 1};
inline constexpr SettingRange hover_opacity{50, 100, 1};
inline constexpr SettingRange widget_opacity{0, 100, 1};
inline constexpr SettingRange bar_height{3, 9, 1};
inline constexpr SettingRange position{0, 100, 1};
inline constexpr SettingRange provider_interval{15, 900, 1};
} // namespace preference_limits
// What 100% text draws at, as a scale on the layouts' nominal font sizes: the
// taskbar at 1.5x and the hover card at 1.3x. Layout thresholds are written
// against these effective scales, not the preference.
inline constexpr int taskbar_text_base = 150, hover_text_base = 130;

struct Appearance {
    // Taskbar and hover card text sizes are independent percentages of their
    // base scales.
    int text_percent{100}, hover_text_percent{100};
    // The taskbar widget's actual width; text size never changes it.
    int widget_width{225}, hover_opacity{95};
    int bar_height{7};
    // Position along the taskbar as a percentage from the left edge.
    int position{100};
    bool show_resets{true}, hover_enabled{true};
    bool twelve_hour_time{false};
    // Per surface: the Windows UI font family's bold face instead of its regular
    // one. The family is whatever the system reports; only the weight is ours.
    bool bold_taskbar{true}, bold_hover{true}, bold_settings{false};
    // A widget on every monitor's taskbar, not only the primary one.
    bool all_taskbars{false};
    // The floating widget's height, so it can be sized to fit a desktop panel.
    // A taskbar host takes the taskbar's height instead.
    int widget_height{38};
    // How opaque the panel a floating widget draws behind its text is.
    int widget_opacity{92};
    // Which windows the taskbar draws a bar for; the hover card and settings
    // still list every window.
    bool codex_session_bar{true}, codex_weekly_bar{true};
    bool claude_session_bar{true}, claude_weekly_bar{true}, claude_model_bar{true};
    // Per provider: percentages and bar fills show how much is used rather than what remains.
    bool codex_show_used{false}, claude_show_used{false};
    // The effective scales the layouts use.
    int taskbar_text_scale() const {
        return (text_percent * taskbar_text_base + 50) / 100;
    }
    int hover_text_scale() const {
        return (hover_text_percent * hover_text_base + 50) / 100;
    }
    auto values() const {
        return std::tie(text_percent, hover_text_percent, widget_width, hover_opacity, bar_height, position,
                        show_resets, hover_enabled, twelve_hour_time, bold_taskbar, bold_hover, bold_settings,
                        all_taskbars, widget_height, widget_opacity, codex_session_bar, codex_weekly_bar,
                        claude_session_bar, claude_weekly_bar, claude_model_bar, codex_show_used,
                        claude_show_used);
    }
    bool operator==(const Appearance& other) const {
        return values() == other.values();
    }
    void normalize() {
        text_percent = preference_limits::text_percent.clamp(text_percent);
        hover_text_percent = preference_limits::text_percent.clamp(hover_text_percent);
        widget_width = preference_limits::widget_width.clamp(widget_width);
        widget_height = preference_limits::widget_height.clamp(widget_height);
        hover_opacity = preference_limits::hover_opacity.clamp(hover_opacity);
        widget_opacity = preference_limits::widget_opacity.clamp(widget_opacity);
        bar_height = preference_limits::bar_height.clamp(bar_height);
        position = preference_limits::position.clamp(position);
    }
};
struct Preferences {
    Appearance appearance;
    bool codex_enabled{true}, claude_enabled{true};
    int codex_interval{60}, claude_interval{60};
    // Ask GitHub once a day whether a newer release exists, and offer it.
    bool check_updates{true};
    // Download and install a newer release without asking, restarting BarTab.
    bool install_updates{false};
    bool operator==(const Preferences& other) const {
        return appearance == other.appearance && codex_enabled == other.codex_enabled &&
               claude_enabled == other.claude_enabled && codex_interval == other.codex_interval &&
               claude_interval == other.claude_interval && check_updates == other.check_updates &&
               install_updates == other.install_updates;
    }
    void normalize() {
        appearance.normalize();
        codex_interval = preference_limits::provider_interval.clamp(codex_interval);
        claude_interval = preference_limits::provider_interval.clamp(claude_interval);
    }
};
} // namespace usage
