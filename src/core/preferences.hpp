#pragma once
#include <algorithm>
#include <tuple>

namespace usage {
struct Appearance {
    int text_percent{150}, font{0}, accent{0}, theme{0};
    int widget_width{150}, hover_width{360}, hover_opacity{93}, hover_delay{350};
    int bar_height{7}, corner_radius{6};
    bool show_resets{true}, hover_enabled{true};
    auto values() const { return std::tie(text_percent,font,accent,theme,widget_width,hover_width,hover_opacity,hover_delay,bar_height,corner_radius,show_resets,hover_enabled); }
    bool operator==(const Appearance& other) const { return values()==other.values(); }
    void normalize() {
        text_percent=std::clamp(text_percent,100,300); font=std::clamp(font,0,2);
        accent=std::clamp(accent,0,2); theme=std::clamp(theme,0,1);
        widget_width=std::clamp(widget_width,100,400); hover_width=std::clamp(hover_width,240,600);
        hover_opacity=std::clamp(hover_opacity,50,100); hover_delay=std::clamp(hover_delay,100,1500);
        bar_height=std::clamp(bar_height,3,9); corner_radius=std::clamp(corner_radius,0,12);
    }
};
struct Preferences {
    Appearance appearance;
    bool codex_enabled{true}, claude_enabled{true};
    int codex_interval{60}, claude_interval{60};
    bool operator==(const Preferences& other) const {
        return appearance==other.appearance && codex_enabled==other.codex_enabled && claude_enabled==other.claude_enabled &&
            codex_interval==other.codex_interval && claude_interval==other.claude_interval;
    }
    void normalize() { appearance.normalize(); codex_interval=std::clamp(codex_interval,15,900); claude_interval=std::clamp(claude_interval,15,900); }
};
}
