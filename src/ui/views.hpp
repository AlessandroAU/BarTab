#pragma once
#include "core/usage.hpp"
#include <clay.h>
#include <clay-widgets.h>
#include <memory>
#include <string>
#include <string_view>
#include <deque>

namespace usage::ui {
Clay_Dimensions widget_size(const Usage& data, const Appearance& appearance, int spacing_percent = 100);
struct WidgetPlacement {
    Rect bounds;
    int text_percent{};
    int spacing_percent{100};
};
WidgetPlacement place_widget(const Usage& data, const Appearance& appearance, Rect panel,
                             const std::vector<Rect>& occupied, float scale);
Clay_Dimensions hover_size(const Usage& data, int text_percent);

enum class Surface { Widget, Details, Hover, Settings };
struct Frame {
    Clay_RenderCommandArray commands{};
    bool changed{};
    bool close{};
    bool save{};
    bool refresh{};
};

// Separate contexts prevent popup focus and layout from changing the taskbar view.
class View {
  public:
    View(Surface surface, ClayWidgets_MeasureTextFunction measure, void* measure_data);
    ~View();
    View(const View&) = delete;
    View& operator=(const View&) = delete;
    Frame frame(Usage& data, ClayWidgets_Input input, float width, float height);
    Clay_BoundingBox bounds(const char* id);
    float hover_height(Usage& data, float width);
    void reset_focus();
    void set_surface(Surface value) {
        surface_ = value;
        reset_focus();
    }
    void set_text_percent(int value);
    void set_hover_text_percent(int value);
    void set_widget_spacing(int value) { widget_spacing_ = std::clamp(value, 0, 100); }
    void set_preferences(Preferences value) {
        value.normalize();
        preferences_ = value;
    }
    const Preferences& preferences() const {
        return preferences_;
    }
    void set_providers(bool codex, bool claude) {
        preferences_.codex_enabled = codex;
        preferences_.claude_enabled = claude;
    }
    bool codex_enabled() const {
        return preferences_.codex_enabled;
    }
    bool claude_enabled() const {
        return preferences_.claude_enabled;
    }
    int text_percent() const {
        return preferences_.appearance.text_percent;
    }
    int hover_text_percent() const {
        return preferences_.appearance.hover_text_percent;
    }
    // Fixed clock for deterministic previews; zero uses the system clock.
    void set_reference_time(std::int64_t value) {
        reference_time_ = value;
    }
    void set_hovered(bool value) {
        hovered_ = value;
    }
    bool set_system_accent(Color value) {
        const bool changed = !(system_accent_ == value);
        system_accent_ = value;
        return changed;
    }
    bool set_system_light(bool value) {
        const bool changed = system_light_ != value;
        system_light_ = value;
        return changed;
    }
    // Glyph coverage is composited in sRGB rather than in linear light, which
    // leaves light-on-dark text too thin and dark-on-light text too heavy. The
    // baked atlas compensates with a power curve, so each theme needs the
    // opposite exponent. It is an approximation: an exact fix needs the
    // background at blend time, which this backend cannot read.
    float text_gamma() const {
        return system_light_ ? 1.f / 1.6f : 1.6f;
    }
    void invalidate_measurements();
    ClayWidgets_Cursor cursor() const;

  private:
    Surface surface_;
    void* arena_{};
    Clay_Context* clay_{};
    std::unique_ptr<ClayWidgets_Context> widgets_;
    std::string session_, weekly_;
    std::string session_used_, weekly_used_;
    std::int64_t reference_time_{};
    int widget_spacing_{100};
    uint16_t widget_gap(int normal, int minimum = 0) const;
    int stacked_text_limit() const { return 160 + (100 - widget_spacing_) / 5; }
    bool hovered_{};
    bool system_light_{};
    bool connection_open_[2]{};
    Color system_accent_{0, 120, 212};
    bool light_theme() const;
    // The hover card scales with its own preference; the taskbar uses the general one.
    int surface_text_percent() const {
        return surface_ == Surface::Hover ? preferences_.appearance.hover_text_percent
                                          : preferences_.appearance.text_percent;
    }
    Clay_Color text_color(Clay_Color tint) const;
    Preferences preferences_;
    std::deque<std::string> labels_;
    const char* label(std::string value);
    void apply_theme();
    void live_panel(const Usage& data, Frame& result, ClayWidgets_Input input);
    void demo_widget(const Usage& data);
    void demo_hover(const Usage& data);
    void demo_settings(Frame& result, ClayWidgets_Input input);
    void demo_details(Usage& data, Frame& result, ClayWidgets_Input input);
    void settings_panel(const Usage& data, Frame& result, ClayWidgets_Input input);
    bool interval_dropdown(const char* id, int& seconds);
    bool setting_slider(const char* id, const char* title, int& value, SettingRange range, const char* unit);
    void wrapped_text(const std::string& value, uint16_t size, Clay_Color tint);
    Clay_Color accent_color() const;
    Clay_Color background_color() const;
    Clay_Color provider_color(const char* name, bool secondary = false) const;
    void hover_usage(const Usage& data);
    void allowance_row(const char* name, const char* provider, const Allowance& window,
                       std::int64_t now);
    void live_usage(const char* provider, const AccountUsage& account, Frame& result,
                    ClayWidgets_Input input);
    void text(const char* value, uint16_t size, Clay_Color tint, int text_percent = 0);
    void compact_bar(const char* id, const char* label, int value, std::string_view percent,
                     bool aligned = false, float percent_width = 0, int text_percent = 0);
    void claude_only(const AccountUsage& account, const Allowance& weekly, const Allowance& fable);
    void taskbar_allowance(const char* id, const char* name, const Allowance& allowance, bool stale,
                           bool show_reset = true, bool date_only = false);
    void claude_bars(const AccountUsage& account, const Allowance& weekly, const Allowance& fable);
};
} // namespace usage::ui
