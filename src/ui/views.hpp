#pragma once
#include "core/usage.hpp"
#include <clay.h>
#include <clay-widgets/widgets.h>
#include <memory>
#include <string>
#include <string_view>
#include <deque>

namespace usage::ui {
// The width preference is the widget's width. Text keeps its size and the bars
// absorb what is left. The height preference applies to floating widgets; a
// taskbar host lays the widget out at the taskbar's widget_height.
Clay_Dimensions widget_size(const Appearance& appearance);
// The widget at its preferred width, or narrowed down to the minimum width when a
// free taskbar gap is tighter; empty when nothing fits.
Rect place_widget(const Appearance& appearance, Rect panel, const std::vector<Rect>& occupied, float scale);
// `text_scale` is the hover card's effective text scale, Appearance::hover_text_scale().
Clay_Dimensions hover_size(const Usage& data, int text_scale);

enum class Surface { Widget, Details, Hover, Settings, Menu };
// The settings panel's sidebar pages, in sidebar order.
enum class SettingsPage : int32_t { Taskbar, Hover, Providers, General };
// The settings window's size in DIPs; the tallest page, Providers, fits without scrolling.
inline constexpr int settings_width = 860, settings_height = 600;
// The host registers the Windows UI font's regular and bold faces under these
// ids; each surface draws with the one its bold preference picks.
inline constexpr uint16_t regular_font = 1, bold_font = 2;
struct Frame {
    Clay_RenderCommandArray commands{};
    bool changed{};
    bool close{};
    bool save{};
    bool refresh{};
    // Reset all settings was pressed: every preference is back at its default
    // in the preview. A host with state of its own, such as a floating widget's
    // position, resets that too when the change is saved.
    bool reset_all{};
    // The context menu item chosen this frame; `close` means it was dismissed.
    enum class MenuChoice { None, Settings, Debug, Startup, Quit } menu{MenuChoice::None};
};
// What the context menu offers. Hosts without a native menu (Linux) show the
// Menu surface in a popup window at the pointer, sized to menu_bounds().
struct MenuModel {
    // "Start at boot" on Windows, "Start at login" elsewhere.
    const char* startup_label{"Start at login"};
    bool startup_enabled{};
    // Greyed out when the host cannot read or change the setting.
    bool startup_available{true};
    // An extra entry for the debug build, such as "Mock providers..."; null hides it.
    const char* debug_label{};
};
// What the host around the views can do, so settings offer only what works there.
struct HostFeatures {
    // The widget sits in a taskbar, placed by the position slider, with a copy on
    // every monitor's taskbar (Windows). Otherwise it floats wherever the user
    // dragged it, and settings call it the widget.
    bool taskbar{true};
    // The system whose colors and font the views follow, named in settings.
    const char* system_name{"Windows"};
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
    // Whether keyboard focus is on the element, for hosts driving the view by keys.
    bool focused(const char* id);
    float hover_height(Usage& data, float width);
    void reset_focus();
    void set_surface(Surface value) {
        surface_ = value;
        reset_focus();
    }
    void set_text_percent(int value);
    void set_hover_text_percent(int value);
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
    // The taskbar widget, the hover card and the popup (settings and details)
    // each have their own bold switch.
    uint16_t font_id() const {
        const auto& a = preferences_.appearance;
        const bool bold = surface_ == Surface::Widget  ? a.bold_taskbar
                          : surface_ == Surface::Hover ? a.bold_hover
                                                       : a.bold_settings;
        return bold ? bold_font : regular_font;
    }
    int text_percent() const {
        return preferences_.appearance.text_percent;
    }
    int hover_text_percent() const {
        return preferences_.appearance.hover_text_percent;
    }
    // Device pixels per layout unit, so stacked tracks can land on whole pixels.
    void set_pixel_scale(float value) {
        pixel_scale_ = value > 0 ? value : 1.f;
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
    // leaves light-on-dark text too thin; the baked atlas thickens it with a
    // power curve. Hinted glyphs already have solid stems, so the curve is mild:
    // 1.6 made bold text blobby. Dark-on-light keeps raw coverage. Linear light
    // would thin it, but Windows draws its own dark text heavier than that, and
    // thinning made small grey labels faint. Chosen from 1x sweeps of every
    // surface (screenshots --sweep).
    float text_gamma() const {
        return system_light_ ? 1.f : 1.3f;
    }
    // Eased hover, toggle and scroll motion. Off by default so layouts, tests and
    // screenshots are deterministic; a host that enables it must keep rendering
    // frames with a real deltaTime until the motion settles.
    void set_animations(bool value) {
        widgets_->animationsEnabled = value;
    }
    bool animations() const {
        return widgets_->animationsEnabled;
    }
    void set_host_features(HostFeatures value) {
        features_ = value;
    }
    // Opens the context menu at the view's top-left corner. Menu frames then lay
    // it out until an item is chosen (Frame::menu) or it is dismissed (Frame::close).
    void open_menu(MenuModel model) {
        menu_ = model;
        menu_opening_ = true;
    }
    // The open menu's panel as of the last frame, in DIPs.
    Clay_BoundingBox menu_bounds();
    void set_settings_page(SettingsPage value) {
        settings_page_ = static_cast<int32_t>(value);
    }
    SettingsPage settings_page() const {
        return static_cast<SettingsPage>(settings_page_);
    }
    void invalidate_measurements();
    ClayWidgets_Cursor cursor() const;

  private:
    std::string date(std::int64_t timestamp) const;
    std::string hover_reset(std::int64_t timestamp, std::int64_t now) const;
    // The hover card's wording without "Resets", which the taskbar's reset
    // column implies: "in 2h 14m", "Mon, 18:14". A reset that is unknown or
    // overdue keeps its explicit wording.
    std::string taskbar_reset(std::int64_t timestamp, std::int64_t now) const;
    std::string reset_time(std::int64_t timestamp, bool date_only = false, bool day_key = false) const;
    // A taskbar reset label: a 5 hour session resets within hours, so its time
    // of day, or nothing while idle; any other window its date (with the time
    // unless `date_only`).
    std::string window_reset(const Allowance& window, bool date_only) const;
    Surface surface_;
    void* arena_{};
    Clay_Context* clay_{};
    std::unique_ptr<ClayWidgets_Context> widgets_;
    std::string session_, weekly_;
    std::string session_used_, weekly_used_;
    std::int64_t reference_time_{};
    // Padding and gap compression, derived per frame: a widget squeezed below its
    // preferred width tightens toward zero, and text between 160% and 180% needs
    // the padding gone before two rows still stack in the taskbar height.
    int widget_spacing_{100};
    int widget_spacing_for(float width) const;
    uint16_t widget_gap(int normal, int minimum = 0) const;
    int stacked_text_limit() const { return 160 + (100 - widget_spacing_) / 5; }
    bool hovered_{};
    HostFeatures features_;
    MenuModel menu_;
    bool menu_opening_{};
    bool system_light_{};
    bool connection_open_[2]{};
    int32_t settings_page_{};
    Color system_accent_{0, 120, 212};
    bool light_theme() const;
    // The hover card scales with its own preference; the taskbar uses the general one.
    // The effective scale, which the layouts are written against.
    int surface_text_percent() const {
        return surface_ == Surface::Hover ? preferences_.appearance.hover_text_scale()
                                          : preferences_.appearance.taskbar_text_scale();
    }
    Clay_Color text_color(Clay_Color tint) const;
    Preferences preferences_;
    std::deque<std::string> labels_;
    const char* label(std::string value);
    void apply_theme();
    void live_panel(const Usage& all, Frame& result, ClayWidgets_Input input);
    // The taskbar's filtered readings; kept for the frame, since text commands
    // point into its labels until the frame is drawn.
    Usage taskbar_data_;
    void demo_widget(const Usage& data);
    void demo_hover(const Usage& data);
    void demo_settings(Frame& result, ClayWidgets_Input input);
    void demo_details(Usage& data, Frame& result, ClayWidgets_Input input);
    void settings_panel(const Usage& data, Frame& result, ClayWidgets_Input input);
    bool interval_dropdown(const char* id, int& seconds);
    bool measure_dropdown(const char* id, bool& used);
    float card_label_width();
    bool setting_slider(const char* id, const char* title, int& value, SettingRange range, const char* unit);
    bool setting_toggle(const char* id, const char* title, bool& value);
    void settings_section(const char* title, bool divider);
    bool begin_settings_page(const char* title, const char* scroll_id, const char* action = nullptr);
    void taskbar_settings(Frame& result);
    void hover_settings(Frame& result);
    void providers_settings(const Usage& data, Frame& result, std::int64_t now);
    void context_menu(Frame& result);
    void general_settings(Frame& result);
    void wrapped_text(const std::string& value, uint16_t size, Clay_Color tint);
    Clay_Color accent_color() const;
    Clay_Color background_color() const;
    Clay_Color provider_color(const char* name, bool secondary = false) const;
    void hover_usage(const Usage& data);
    void allowance_row(const char* name, const char* provider, const Allowance& window,
                       std::int64_t now);
    void live_usage(const char* provider, const AccountUsage& account, Frame& result,
                    ClayWidgets_Input input);
    // `word` marks a mostly lowercase label, which the taskbar centres by its
    // lowercase letters rather than its digits and capitals.
    void text(const char* value, uint16_t size, Clay_Color tint, int text_percent = 0, bool word = false);
    void compact_bar(const char* id, const char* label, int value, std::string_view percent,
                     int text_percent = 0);
    void bar_track(const char* id, const char* provider, const char* label, int value);
    // A window's percentage as its provider's preference shows it: remaining, or used.
    bool shows_used(bool claude) const {
        return claude ? preferences_.appearance.claude_show_used : preferences_.appearance.codex_show_used;
    }
    int shown(int remaining, bool claude) const {
        return shows_used(claude) ? 100 - remaining : remaining;
    }
    // Height of each of `count` tracks sharing one 10 pt row, and the gap between
    // them, both whole device pixels expressed in layout units.
    std::pair<float, float> paired_tracks(int count = 2) const;
    // Tracks stacked in one row, re-seated on whole device pixels after layout so
    // rounding never makes one thicker than another.
    struct TrackStack {
        std::vector<uint32_t> ids;
        int height, gap; // Device pixels.
    };
    std::vector<TrackStack> track_stacks_;
    float pixel_scale_{1.f};
    void snap_track_stacks(Clay_RenderCommandArray commands) const;
    // Space between a taskbar label and its bar, and between the bar and its
    // percentage and reset date, identical in every taskbar mode. Narrow widgets
    // tighten it so their bars keep some length; placement compresses it further.
    uint16_t bar_gap() const {
        return widget_gap(narrow_widget() ? 3 : 6, 1);
    }
    // Little room relative to the text: shorter reset dates and tighter gaps. The
    // 208 px threshold is for 100% text and scales with it; the width is the one
    // being laid out, which placement may have narrowed below the preference.
    float widget_width_{};
    bool narrow_widget() const {
        const float width = widget_width_ > 0 ? widget_width_ : preferences_.appearance.widget_width;
        return width * 100 < 208 * surface_text_percent();
    }
    float text_width(const char* value, uint16_t size, int text_percent = 0);
    // A slot for one of two stacked taskbar rows: a third of the widget's height,
    // so the pair centres on one and two thirds of it. Text too tall for that
    // keeps 0.8 em, about as close as capitals and descenders get without
    // touching. The slot runs left to right because Clay centres an
    // overflowing child only across that axis.
    Clay_ElementDeclaration thirds_slot(uint16_t size, int text_percent = 0) const;
    // Widening for the parts before and after a row's bars, `leading` and
    // `trailing` wide, that puts the bars between the thirds of the widget's
    // content width. It only comes out of bars longer than a third, so when a
    // part is too wide for its third the bars keep a third and sit as near the
    // thirds as they can.
    struct ThirdsPadding {
        float lead{}, trail{};
    };
    ThirdsPadding thirds_padding(float leading, float trailing) const;
    // Fixed label and percentage column widths so stacked taskbar rows share one
    // bar start and end; zero lets each row size its own text.
    struct WidgetColumns {
        float label{}, percent{}, reset{};
    };
    // Optional fixed-width wrapper so stacked rows keep their columns flush;
    // `centred` centres the text in the column, as the taskbar's reset dates are.
    void column_text(float width, const char* value, uint16_t size, Clay_Color tint, uint16_t left_padding = 0,
                     bool word = false, bool centred = false);
    WidgetColumns widget_columns_;
    void codex_only_split(const AccountUsage& account, const Allowance& session, const Allowance& weekly);
    float frame_width_{}, frame_height_{};
    // Two stacked Claude rows from an optional session, the weekly window and an
    // optional Fable weekly; with all three, claude_three draws them.
    void claude_only(const AccountUsage& account, const Allowance* session, const Allowance& weekly,
                     const Allowance* fable);
    // Claude alone with every window: three equal bar slots with a small
    // percentage each, "Session" and "Weekly" labels, and two reset lines.
    void claude_three(const AccountUsage& account, const Allowance& session, const Allowance& weekly,
                      const Allowance& fable);
    void taskbar_allowance(const char* id, const char* name, const Allowance& allowance, bool stale,
                           bool show_reset = true, bool date_only = false);
    // The combined view's Claude row: weekly and Fable tracks, with the session above them when reported.
    void claude_bars(const AccountUsage& account, const Allowance* session, const Allowance& weekly,
                     const Allowance& fable);
};
} // namespace usage::ui
