#pragma once
#include "core/usage.hpp"
#include <clay.h>
#include <clay-widgets/widgets.h>
#include <memory>
#include <string>
#include <string_view>
#include <deque>

namespace usage::ui {
Clay_Dimensions widget_size(const Usage& data, const Appearance& appearance);
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
    void reset_focus();
    void set_surface(Surface value) { surface_ = value; reset_focus(); }
    void set_text_percent(int value);
    void set_preferences(Preferences value) { value.normalize(); preferences_ = value; }
    const Preferences& preferences() const { return preferences_; }
    void set_providers(bool codex, bool claude) { preferences_.codex_enabled = codex; preferences_.claude_enabled = claude; }
    bool codex_enabled() const { return preferences_.codex_enabled; }
    bool claude_enabled() const { return preferences_.claude_enabled; }
    int text_percent() const { return preferences_.appearance.text_percent; }
    void set_hovered(bool value) { hovered_ = value; }
    bool set_system_light(bool value) { const bool changed=system_light_!=value; system_light_=value; return changed; }
    void invalidate_measurements();
    ClayWidgets_Cursor cursor() const;
private:
    Surface surface_;
    void* arena_{};
    Clay_Context* clay_{};
    std::unique_ptr<ClayWidgets_Context> widgets_;
    std::string session_, weekly_;
    std::string session_used_, weekly_used_;
    bool hovered_{};
    bool system_light_{};
    Preferences preferences_;
    std::string text_size_label_;
    std::deque<std::string> labels_;
    const char* label(std::string value);
    void settings_panel(const Usage& data, Frame& result, ClayWidgets_Input input);
    bool choice_dropdown(const char* id, const char* title, const char* const* choices, int count, int& value);
    bool interval_dropdown(const char* id, int& seconds);
    bool setting_slider(const char* id, const char* title, int& value, int min, int max, int step, const char* unit);
    void wrapped_text(const std::string& value, uint16_t size, Clay_Color tint);
    Clay_Color accent_color() const;
    Clay_Color background_color() const;
    Clay_Color provider_color(const char* name, bool secondary = false) const;
    void hover_usage(const Usage& data);
    void live_usage(const char* provider, const AccountUsage& account, Frame& result, ClayWidgets_Input input);
    void text(const char* value, uint16_t size, Clay_Color tint);
    void compact_bar(const char* id, const char* label, int value, std::string_view percent, bool aligned = false, float percent_width = 0);
    void claude_only(const AccountUsage& account, const Allowance& weekly, const Allowance& fable);
    void taskbar_allowance(const char* id, const char* name, const Allowance& allowance, bool stale, bool show_reset = true, bool date_only = false);
    void claude_bars(const AccountUsage& account, const Allowance& weekly, const Allowance& fable);
};
} // namespace usage::ui
