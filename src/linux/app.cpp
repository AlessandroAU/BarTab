#include "linux/app.hpp"
#include "host/platform.hpp"
#include "host/settings_store.hpp"
#include "host/startup.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <string>
#include <tuple>
#include <unistd.h>

namespace usage::linux_host {
namespace {
// Room beside the widget's layout for its backdrop, and the backdrop's corners,
// in DIPs. Its height is the preference exactly, so it can match a panel.
constexpr float widget_pad_x = 6.f, widget_radius = 8.f;
// A docked widget's gap above and below it inside the panel, in DIPs.
constexpr float dock_inset = 1.f;
// Gap between the widget and a card or window anchored to it, in DIPs.
constexpr float anchor_gap = 8.f;
// Card and settings corners, matching Windows 11's 24 px rounded windows.
constexpr float window_radius = 12.f;
// The settings window's top strip drags it, like a title bar.
constexpr float drag_strip = 48.f;
// A press that travels further than this, in DIPs, moves the widget.
constexpr float drag_threshold = 4.f;
constexpr auto hover_open_duration = std::chrono::milliseconds(200);
constexpr auto hover_grace = std::chrono::milliseconds(200);
constexpr auto tick_interval = std::chrono::milliseconds(500);
constexpr auto frame_interval = std::chrono::milliseconds(16);
// Longest eased motion in the widget set is the toggle knob at about 0.36 s;
// the loop outlives it so every transition reaches its target.
constexpr auto settle_window = std::chrono::milliseconds(500);
// Room around the widget for the burst to rise and spread into, in DIPs.
constexpr float confetti_rise = 300.f, confetti_spread = 300.f;
constexpr int confetti_pieces = 250;

int round(float value) {
    return static_cast<int>(std::lround(value));
}
Rect intersection(const Rect& a, const Rect& b) {
    const int left = std::max(a.x, b.x), top = std::max(a.y, b.y);
    const int right = std::min(a.right(), b.right()), bottom = std::min(a.bottom(), b.bottom());
    return right > left && bottom > top ? Rect{left, top, right - left, bottom - top} : Rect{};
}
// Keeps `bounds` inside `area`, preferring its top-left corner when it cannot fit.
Rect clamp_into(Rect bounds, const Rect& area) {
    bounds.x = std::max(area.x, std::min(bounds.x, area.right() - bounds.width));
    bounds.y = std::max(area.y, std::min(bounds.y, area.bottom() - bounds.height));
    return bounds;
}
// Pixels that are mostly opaque; the smoke test's evidence that something drew.
std::size_t opaque(const ui::Pixels& pixels) {
    return static_cast<std::size_t>(std::count_if(pixels.data.begin(), pixels.data.end(),
                                                  [](std::uint32_t pixel) { return (pixel >> 24) > 200; }));
}
} // namespace

App::App(bool smoke, std::shared_ptr<host::MockProviders> mock)
    : mock_(std::move(mock)), providers_(usage_, mock_, smoke), smoke_(smoke), animations_allowed_(!smoke) {
    // No taskbar here: the widget floats where the user drags it.
    ui::HostFeatures features;
    features.taskbar = false;
    features.system_name = "desktop";
    details_view_.set_host_features(features);
    scale_ = x_.scale();
    reload_ui_font();
    details_view_.set_animations(animations_allowed_ && host::animations_enabled());
    widget_view_.set_system_light(host::system_light_theme());
    widget_view_.set_system_accent(host::system_accent());
    for (auto* view : {&hover_view_, &details_view_, &menu_view_}) {
        view->set_system_light(host::apps_light_theme());
        view->set_system_accent(host::system_accent());
    }
    load_settings();
    load_position();
    usage_.live = !smoke;
    if (!smoke) {
        usage_.codex.installed = false;
        providers_.detect();
        providers_.apply(preferences_);
    }
    start_updater();
    place_widget();
    next_tick_ = Clock::now() + tick_interval;
    host::log("Started Linux host; PID " + std::to_string(getpid()) + ", scale " + std::to_string(scale_));
}

// ---- Settings, position and fonts ----

namespace {
// The settings and widget-position files; the debug build keeps its own.
std::pair<std::filesystem::path, std::filesystem::path> configuration_files(bool mock) {
    const auto directory = host::config_directory();
    if (directory.empty())
        return {};
    return {directory / (mock ? "debug-settings.ini" : "settings.ini"),
            directory / (mock ? "debug-widget.ini" : "widget.ini")};
}
} // namespace

bool App::reset_configuration(bool mock) {
    const auto [settings, position] = configuration_files(mock);
    bool removed = true;
    for (const auto& path : {settings, position}) {
        std::error_code error;
        if (!path.empty())
            std::filesystem::remove(path, error);
        removed = removed && !error;
    }
    host::log(removed ? "Configuration reset from the command line."
                      : "Could not remove the configuration files.");
    return removed;
}

void App::load_settings() {
    if (smoke_) {
        settings_path_ = host::executable_directory() / "smoke-settings.ini";
        position_path_ = host::executable_directory() / "smoke-widget.ini";
        preferences_ = Preferences{};
        // Every run starts from nothing, whatever the last one left behind.
        std::error_code ignored;
        std::filesystem::remove(settings_path_, ignored);
        std::filesystem::remove(position_path_, ignored);
    } else {
        std::tie(settings_path_, position_path_) = configuration_files(mock_ != nullptr);
        preferences_ = host::read_settings(settings_path_);
    }
    usage_.codex_enabled = preferences_.codex_enabled;
    usage_.claude_enabled = preferences_.claude_enabled;
    apply_view_preferences();
}

bool App::save_settings(Preferences value) {
    value.normalize();
    if (!host::write_settings(settings_path_, value)) {
        host::log("Could not save preferences to " + settings_path_.string());
        return false;
    }
    apply_preferences(value);
    settings_edit_.commit();
    return true;
}

void App::apply_preferences(Preferences value) {
    value.normalize();
    if (preferences_ == value)
        return;
    preferences_ = value;
    usage_.codex_enabled = value.codex_enabled;
    usage_.claude_enabled = value.claude_enabled;
    apply_view_preferences();
    providers_.apply(preferences_);
    if (updater_)
        updater_->set_policy(value.check_updates, value.install_updates);
    if (!value.appearance.hover_enabled)
        hide_hover(true);
    place_widget();
    update_usage();
    if (hover_pinned_ && !(hover_ && x_.visible(hover_)))
        show_hover();
}

void App::apply_view_preferences() {
    widget_view_.set_preferences(preferences_);
    hover_view_.set_preferences(preferences_);
}

// The position file holds the widget's top-left corner once the user moved it.
void App::load_position() {
    std::ifstream file(position_path_);
    std::string line;
    std::optional<int> x, y;
    dock_ = Dock::None;
    while (std::getline(file, line)) {
        if (line.rfind("X=", 0) == 0)
            x = std::atoi(line.c_str() + 2);
        if (line.rfind("Y=", 0) == 0)
            y = std::atoi(line.c_str() + 2);
        if (line == "Dock=top")
            dock_ = Dock::Top;
        if (line == "Dock=bottom")
            dock_ = Dock::Bottom;
    }
    if (x && y)
        saved_position_ = std::make_pair(*x, *y);
}

void App::save_position() {
    position_dirty_ = false;
    if (!saved_position_ || position_path_.empty())
        return;
    std::error_code error;
    std::filesystem::create_directories(position_path_.parent_path(), error);
    std::ofstream file(position_path_, std::ios::trunc);
    file << "[Widget]\nX=" << saved_position_->first << "\nY=" << saved_position_->second << "\nDock="
         << (dock_ == Dock::Top      ? "top"
             : dock_ == Dock::Bottom ? "bottom"
                                     : "none")
         << '\n';
}

void App::forget_position() {
    saved_position_.reset();
    dock_ = Dock::None;
    position_dirty_ = false;
    std::error_code error;
    std::filesystem::remove(position_path_, error);
    place_widget();
}

// Loads the desktop UI font's regular and bold faces side by side, so each
// surface picks its weight by font id without reloading anything.
bool App::reload_ui_font() {
    auto regular_font = host::ui_font(false), bold_font = host::ui_font(true);
    const bool regular =
        renderer_.load_font_data(ui::regular_font, std::move(regular_font.bytes), regular_font.face_index);
    const bool bold =
        renderer_.load_font_data(ui::bold_font, std::move(bold_font.bytes), bold_font.face_index);
    if (!regular && !bold)
        return false;
    widget_view_.invalidate_measurements();
    hover_view_.invalidate_measurements();
    details_view_.invalidate_measurements();
    menu_view_.invalidate_measurements();
    return true;
}

// ---- Event loop ----

int App::run() {
    if (smoke_) {
        run_smoke_test();
        return exit_code_;
    }
    loop(Clock::time_point::max());
    return exit_code_;
}

void App::loop(Clock::time_point until) {
    while (running_) {
        auto now = Clock::now();
        if (now >= until)
            return;
        auto deadline = std::min(next_tick_, until);
        if (animating())
            deadline = std::min(deadline, now + frame_interval);
        if (hover_check_at_)
            deadline = std::min(deadline, *hover_check_at_);
        if (hover_ && x_.visible(hover_))
            deadline = std::min(deadline, hover_refresh_at_);
        x_.flush();
        if (!x_.pending()) {
            const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            pollfd connection{x_.descriptor(), POLLIN, 0};
            poll(&connection, 1, static_cast<int>(std::clamp<long long>(wait, 0, 1000)));
        }
        while (running_ && x_.pending())
            handle(x_.next());
        now = Clock::now();
        if (hover_check_at_ && now >= *hover_check_at_) {
            hover_check_at_.reset();
            check_hover_leave();
        }
        if (hover_ && x_.visible(hover_) && now >= hover_refresh_at_)
            show_hover();
        if (animating())
            frame();
        if (now >= next_tick_) {
            next_tick_ = now + tick_interval;
            tick();
        }
    }
}

void App::handle(const x11::Event& event) {
    if (!event.window)
        return;
    // While the menu is open a click on any other window only dismisses it.
    if (menu_ && x_.visible(menu_) && event.window != menu_ && event.type == x11::Event::Type::Press) {
        close_menu();
        return;
    }
    if (event.window == menu_)
        menu_event(event);
    else if (event.window == widget_)
        widget_event(event);
    else if (event.window == hover_)
        hover_event(event);
    else if (event.window == popup_)
        details_event(event);
}

void App::tick() {
    ++ticks_;
    poll_updates();
    const auto update = providers_.poll();
    if (update.changed) {
        if (update.reset)
            celebrate();
        update_usage();
        if (popup_ && x_.visible(popup_))
            render_details(details_pointer_);
    }
    // Follow the desktop's scaling, theme, accent, font and motion settings.
    const float scale = x_.scale();
    bool relayout = false;
    if (scale != scale_) {
        scale_ = scale;
        relayout = true;
    }
    if (ticks_ % 10 == 0 && reload_ui_font())
        relayout = true;
    const bool app_light = host::apps_light_theme();
    const auto accent = host::system_accent();
    const bool details_changed =
        details_view_.set_system_light(app_light) | details_view_.set_system_accent(accent);
    const bool hover_changed =
        hover_view_.set_system_light(app_light) | hover_view_.set_system_accent(accent);
    menu_view_.set_system_light(app_light);
    menu_view_.set_system_accent(accent);
    const bool widget_changed =
        widget_view_.set_system_light(host::system_light_theme()) | widget_view_.set_system_accent(accent);
    details_view_.set_animations(animations_allowed_ && host::animations_enabled());
    // Also re-clamps the widget onto a monitor after the layout changed.
    place_widget();
    if (relayout || widget_changed)
        paint_widget();
    if ((relayout || hover_changed) && hover_ && x_.visible(hover_))
        show_hover();
    if ((relayout || details_changed) && popup_ && x_.visible(popup_))
        render_details(details_pointer_);
    if (position_dirty_)
        save_position();
    // Window managers that restack their own windows on top could bury an
    // override-redirect widget; lift it back, unless the menu or confetti,
    // which overlap it, are showing.
    if (widget_ && !(menu_ && x_.visible(menu_)) && confetti_.done())
        x_.raise(widget_);
}

bool App::animating() const {
    return (hover_ && x_.visible(hover_) && hover_progress_ < 1.f) ||
           (popup_ && x_.visible(popup_) && details_view_.animations() &&
            Clock::now() < details_settle_until_) ||
           !confetti_.done();
}

void App::frame() {
    if (hover_ && x_.visible(hover_) && hover_progress_ < 1.f)
        animate_hover();
    animate_details();
    if (!confetti_.done())
        animate_confetti();
}

void App::update_usage() {
    paint_widget();
    if (hover_ && x_.visible(hover_))
        show_hover();
}

// The debug build's middle click: on to the next mock scenario.
void App::cycle_mock() {
    const auto& presets = mock_presets();
    mock_preset_ = (mock_preset_ + 1) % presets.size();
    mock_->set(presets[mock_preset_].scenario);
    host::log(std::string("Mock scenario: ") + presets[mock_preset_].name);
    providers_.detect();
    providers_.refresh();
    update_usage();
    if (popup_ && x_.visible(popup_))
        render_details(details_pointer_);
}

// ---- The floating widget ----

Rect App::widget_target() {
    const auto size = ui::widget_size(preferences_.appearance);
    Rect bounds{0, 0, round((size.width + 2 * widget_pad_x) * scale_), round(size.height * scale_)};
    if (saved_position_) {
        bounds.x = saved_position_->first;
        bounds.y = saved_position_->second;
    } else {
        // Until the user places it: docked in the top panel of the monitor the
        // pointer is on, centred a third of the way in from the right - on
        // GNOME, between the clock and the status icons - or at the top of the
        // screen there when it has no top panel.
        int px{}, py{};
        x_.pointer(px, py);
        const auto monitor = x_.monitor_at(px, py);
        bounds.x = monitor.bounds.right() - monitor.bounds.width / 3 - bounds.width / 2;
        bounds.y = monitor.work.y;
        dock_ = panel_strip(monitor, Dock::Top).empty() ? Dock::None : Dock::Top;
    }
    // Stay on a monitor even after the one it was on went away. The whole
    // monitor, not its work area, so the widget can sit in a panel.
    const auto monitor = x_.monitor_at(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
    // Docked, it fills the panel, following its height; if the panel went away
    // it floats where it was.
    if (const auto slot = dock_slot(monitor, dock_); !slot.empty()) {
        bounds.y = slot.y;
        bounds.height = slot.height;
    }
    return clamp_into(bounds, monitor.bounds);
}

Rect App::dock_slot(const x11::Monitor& monitor, Dock dock) const {
    auto slot = panel_strip(monitor, dock);
    if (slot.empty())
        return slot;
    const int inset = round(dock_inset * scale_);
    slot.y += inset;
    slot.height -= 2 * inset;
    return slot;
}

void App::reset_floating_height() {
    const int standard = Appearance{}.widget_height;
    if (preferences_.appearance.widget_height == standard)
        return;
    auto value = preferences_;
    value.appearance.widget_height = standard;
    apply_preferences(value);
    // With settings open this is part of the preview, which Save keeps and
    // Cancel reverts; otherwise it is saved now.
    if (popup_ && x_.visible(popup_) && settings_mode_)
        details_view_.set_preferences(value);
    else
        host::write_settings(settings_path_, preferences_);
}

Rect App::panel_strip(const x11::Monitor& monitor, Dock dock) {
    const auto& screen = monitor.bounds;
    const auto& work = monitor.work;
    // Too thin to hold text is not a panel, just a reserved edge.
    constexpr int thinnest = 16;
    Rect strip;
    if (dock == Dock::Top)
        strip = {screen.x, screen.y, screen.width, work.y - screen.y};
    else if (dock == Dock::Bottom)
        strip = {screen.x, work.bottom(), screen.width, screen.bottom() - work.bottom()};
    return strip.height >= thinnest ? strip : Rect{};
}

void App::place_widget() {
    const auto target = widget_target();
    if (!widget_) {
        widget_ = x_.create(x11::Role::Widget, target, "BarTab");
        widget_bounds_ = target;
        x_.show(widget_);
        paint_widget();
        return;
    }
    if (target == widget_bounds_)
        return;
    const bool resized = target.width != widget_bounds_.width || target.height != widget_bounds_.height;
    x_.place(widget_, target);
    widget_bounds_ = target;
    if (resized)
        paint_widget();
    // A pinned preview follows the widget it hangs from; a pointer card closes.
    if (hover_pinned_)
        show_hover();
    else
        hide_hover();
}

void App::paint_widget() {
    if (!widget_ || widget_bounds_.empty())
        return;
    // Laid out at the window's height, which a docked widget takes from its panel.
    const float width = ui::widget_size(preferences_.appearance).width;
    const float height = static_cast<float>(widget_bounds_.height) / scale_;
    renderer_.set_surface(scale_, widget_view_.text_gamma());
    widget_view_.set_hovered(hovered_);
    widget_view_.set_pixel_scale(scale_);
    const auto frame = widget_view_.frame(usage_, {}, width, height);
    const auto inner =
        renderer_.render(frame.commands, round(width * scale_), widget_bounds_.height, scale_, false);
    ui::Pixels pixels{
        widget_bounds_.width, widget_bounds_.height,
        std::vector<std::uint32_t>(static_cast<std::size_t>(widget_bounds_.width) * widget_bounds_.height)};
    const int left = round(widget_pad_x * scale_);
    for (int y = 0; y < inner.height && y < pixels.height; ++y)
        for (int x = 0; x < inner.width && x + left < pixels.width; ++x)
            pixels.data[static_cast<std::size_t>(y) * pixels.width + x + left] =
                inner.data[static_cast<std::size_t>(y) * inner.width + x];
    // Floating over arbitrary windows, the widget brings its own panel behind it.
    const bool light = host::system_light_theme();
    const int opacity = preferences_.appearance.widget_opacity;
    ui::fill_behind(pixels, light ? Color{243, 243, 243} : Color{32, 32, 32},
                    static_cast<std::uint8_t>((opacity * 255 + 50) / 100));
    ui::round_corners(pixels, round(widget_radius * scale_));
    widget_pixels_ = std::move(pixels);
    x_.present(widget_, widget_pixels_);
}

// One step of a drag: keep the point that was pressed under the pointer, and
// dock into a panel the pointer is over, filling its height.
void App::drag_widget_to(int root_x, int root_y) {
    const auto monitor = x_.monitor_at(root_x, root_y);
    auto dock = Dock::None;
    if (root_y < monitor.work.y)
        dock = Dock::Top;
    else if (root_y >= monitor.work.bottom())
        dock = Dock::Bottom;
    Rect bounds = widget_bounds_;
    bounds.x = root_x - press_->x;
    if (const auto slot = dock_slot(monitor, dock); !slot.empty()) {
        bounds.y = slot.y;
        bounds.height = slot.height;
    } else {
        const bool undocking = dock_ != Dock::None;
        dock = dock_ = Dock::None;
        if (undocking)
            reset_floating_height();
        bounds.height = round(static_cast<float>(preferences_.appearance.widget_height) * scale_);
        bounds.y = root_y - std::min(press_->y, bounds.height - 1);
    }
    bounds = clamp_into(bounds, monitor.bounds);
    dock_ = dock;
    if (!(bounds == widget_bounds_)) {
        const bool resized = bounds.height != widget_bounds_.height;
        widget_bounds_ = bounds;
        saved_position_ = std::make_pair(bounds.x, bounds.y);
        x_.place(widget_, bounds);
        if (resized)
            paint_widget();
    }
}

void App::widget_event(const x11::Event& event) {
    using Type = x11::Event::Type;
    switch (event.type) {
    case Type::Paint:
        x_.present(widget_, widget_pixels_);
        break;
    case Type::Configure:
        // Moved from outside the app; remember where it went.
        if (!event.bounds.empty() &&
            (event.bounds.x != widget_bounds_.x || event.bounds.y != widget_bounds_.y)) {
            widget_bounds_ = event.bounds;
            saved_position_ = std::make_pair(event.bounds.x, event.bounds.y);
            position_dirty_ = true;
            if (hover_pinned_)
                show_hover();
        }
        break;
    case Type::Enter:
    case Type::Motion:
        pointer_over_widget_ = true;
        if (press_ && event.buttons_held &&
            (dragging_ || std::hypot(event.root_x - press_->root_x, event.root_y - press_->root_y) >
                              drag_threshold * scale_)) {
            if (!dragging_) {
                dragging_ = true;
                hide_hover(true);
            }
            drag_widget_to(event.root_x, event.root_y);
            break;
        }
        if (hover_ && x_.visible(hover_))
            hover_check_at_.reset();
        if (preferences_.appearance.hover_enabled && !hovered_ && !event.buttons_held) {
            hovered_ = true;
            paint_widget();
            // No delay: the card's opening animation already eases it in.
            show_hover();
        }
        break;
    case Type::Leave:
        pointer_over_widget_ = false;
        if (hover_ && x_.visible(hover_))
            hover_check_at_ = Clock::now() + hover_grace;
        else
            hide_hover();
        break;
    case Type::Press:
        if (event.button == 1) {
            hide_hover();
            press_ = event;
        }
        break;
    case Type::Release:
        if (event.button == 1 && dragging_) {
            // Where it is let go counts: a quick flick out of a panel can
            // release before any motion outside it arrives.
            drag_widget_to(event.root_x, event.root_y);
            dragging_ = false;
            press_.reset();
            save_position();
            if (hover_pinned_)
                show_hover();
        } else if (event.button == 1 && press_) {
            press_.reset();
            open_details();
        } else if (event.button == 3) {
            show_menu();
        } else if (event.button == 2 && mock_) {
            cycle_mock();
        }
        break;
    default:
        break;
    }
}

// ---- The hover card ----

void App::show_hover() {
    // The context menu suppresses the card, pinned or not, until it closes.
    if (!preferences_.appearance.hover_enabled || !(hovered_ || hover_pinned_) || !widget_ ||
        !x_.visible(widget_) || widget_bounds_.empty() || (menu_ && x_.visible(menu_)))
        return;
    if (!hover_)
        hover_ = x_.create(x11::Role::Card, {0, 0, 1, 1}, "Usage overview");
    const bool opening = !x_.visible(hover_);
    auto logical = ui::hover_size(usage_, preferences_.appearance.hover_text_scale());
    logical.width = widget_bounds_.width / scale_;
    renderer_.set_surface(scale_, hover_view_.text_gamma());
    hover_view_.invalidate_measurements();
    if (usage_.live)
        logical.height = hover_view_.hover_height(usage_, logical.width);
    const auto& widget = widget_bounds_;
    const auto work = x_.monitor_at(widget.x + widget.width / 2, widget.y + widget.height / 2).work;
    const int width = std::min(round(logical.width * scale_), work.width);
    const int height = round(logical.height * scale_);
    logical.width = width / scale_;
    // Above a widget in the lower half of the screen, below one in the upper half.
    const bool above = widget.y + widget.height / 2 > work.y + work.height / 2;
    const int gap = round(anchor_gap * scale_);
    const Rect bounds = clamp_into(
        {widget.right() - width, above ? widget.y - height - gap : widget.bottom() + gap, width, height},
        work);
    if (opening) {
        // Grow away from the widget: upward when the card sits above it.
        hover_grows_up_ = bounds.y + height / 2 < widget.y + widget.height / 2;
        const bool animate = animations_allowed_ && host::animations_enabled();
        hover_progress_ = animate ? 0.f : 1.f;
        hover_opened_ = Clock::now();
    }
    x_.place(hover_, bounds);
    const auto frame = hover_view_.frame(usage_, {}, logical.width, logical.height);
    hover_pixels_ = renderer_.render(frame.commands, width, height, scale_, false);
    if (opening)
        x_.show(hover_);
    present_hover();
    hover_refresh_at_ = Clock::now() + std::chrono::minutes(1);
}

// Clips the card to the part that has grown so far and fades it to match. The
// pixels are offset so the card's far edge travels with the growing edge.
void App::present_hover() {
    if (!hover_ || hover_pixels_.data.empty())
        return;
    auto pixels = hover_pixels_;
    const int height = pixels.height;
    const int visible = std::max(1, round(static_cast<float>(height) * hover_progress_));
    const int top = hover_grows_up_ ? height - visible : 0;
    ui::shift_rows(pixels, hover_grows_up_ ? top : visible - height);
    ui::round_corners(pixels, round(window_radius * scale_), top, top + visible);
    ui::fade(pixels, static_cast<float>(preferences_.appearance.hover_opacity) / 100.f * hover_progress_);
    x_.present(hover_, pixels);
}

bool App::animate_hover() {
    const float t = std::min(1.f, std::chrono::duration<float>(Clock::now() - hover_opened_) /
                                      std::chrono::duration<float>(hover_open_duration));
    // Ease out: most of the growth early, settling gently into place.
    hover_progress_ = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
    present_hover();
    return t < 1.f;
}

void App::hide_hover(bool force) {
    hover_check_at_.reset();
    const bool was_hovered = hovered_;
    hovered_ = false;
    widget_view_.set_hovered(false);
    if (was_hovered)
        paint_widget();
    // A pinned card stays as the settings preview; only the highlight goes.
    if (hover_pinned_ && !force)
        return;
    if (hover_)
        x_.hide(hover_);
    pointer_over_card_ = false;
}

void App::pin_hover() {
    hover_pinned_ = true;
    show_hover();
}

void App::unpin_hover() {
    hover_pinned_ = false;
    hide_hover();
}

void App::check_hover_leave() {
    if (!pointer_over_card_ && !pointer_over_widget_)
        hide_hover();
}

void App::hover_event(const x11::Event& event) {
    using Type = x11::Event::Type;
    if (event.type == Type::Paint)
        present_hover();
    else if (event.type == Type::Enter || event.type == Type::Motion) {
        pointer_over_card_ = true;
        hover_check_at_.reset();
    } else if (event.type == Type::Leave) {
        pointer_over_card_ = false;
        hover_check_at_ = Clock::now() + hover_grace;
    }
}

// ---- Settings and details ----

void App::open_details(bool settings) {
    settings = settings || usage_.live;
    hide_hover();
    if (!popup_)
        popup_ = x_.create(x11::Role::Dialog, {0, 0, 1, 1}, "BarTab - Settings and usage");
    if (x_.visible(popup_) && settings_mode_ == settings) {
        x_.activate(popup_, x_.last_input_time());
        return;
    }
    if (x_.visible(popup_))
        close_details();
    details_rendered_ = {};
    settings_mode_ = settings;
    if (settings) {
        settings_edit_.begin(preferences_);
        details_view_.set_preferences(preferences_);
    }
    providers_.detect();
    details_view_.set_surface(settings ? ui::Surface::Settings : ui::Surface::Details);
    details_view_.set_preferences(preferences_);
    details_view_.reset_focus();
    details_pointer_ = {};
    details_pointer_.mouseX = details_pointer_.mouseY = -100;
    const auto& widget = widget_bounds_;
    const auto work = x_.monitor_at(widget.x + widget.width / 2, widget.y + widget.height / 2).work;
    const int width = std::min(round((usage_.live ? ui::settings_width : 400) * scale_), work.width);
    const int height = std::min(round((usage_.live ? ui::settings_height : 470) * scale_), work.height);
    Rect bounds{work.x + (work.width - width) / 2, work.y + (work.height - height) / 2, width, height};
    if (!settings) {
        // The demo details hang off the widget like the card does.
        const bool above = widget.y + widget.height / 2 > work.y + work.height / 2;
        const int gap = round(anchor_gap * scale_);
        bounds = clamp_into(
            {widget.right() - width, above ? widget.y - height - gap : widget.bottom() + gap, width, height},
            work);
    } else {
        pin_hover();
        avoid_hover(bounds, work);
    }
    x_.place(popup_, bounds);
    x_.show(popup_);
    render_details(details_pointer_);
    x_.activate(popup_, x_.last_input_time());
}

// Slides the settings window left, then up, so it never covers the pinned hover card.
void App::avoid_hover(Rect& bounds, const Rect& work) {
    if (!hover_pinned_ || !hover_ || !x_.visible(hover_))
        return;
    const auto card = x_.bounds(hover_);
    const int gap = round(anchor_gap * scale_);
    auto overlaps = [&] {
        return bounds.x < card.right() + gap && bounds.right() > card.x - gap &&
               bounds.y < card.bottom() + gap && bounds.bottom() > card.y - gap;
    };
    if (!overlaps())
        return;
    if (card.x - gap - bounds.width >= work.x)
        bounds.x = card.x - gap - bounds.width;
    else if (card.right() + gap + bounds.width <= work.right())
        bounds.x = card.right() + gap;
    if (overlaps())
        bounds.y = std::max(work.y, card.y - gap - bounds.height);
}

void App::close_details() {
    reset_place_on_save_ = false;
    details_pointer_ = {};
    details_pointer_.mouseX = details_pointer_.mouseY = -100;
    details_view_.reset_focus();
    if (popup_)
        x_.hide(popup_);
    unpin_hover();
    if (const auto original = settings_edit_.cancel())
        apply_preferences(*original);
}

// Input or a data change: render now and, when animating, keep frames coming
// for the settle window so eased motion runs to completion.
void App::render_details(ClayWidgets_Input input) {
    if (!popup_)
        return;
    if (details_view_.animations())
        details_settle_until_ = Clock::now() + settle_window;
    render_details_frame(input);
}

bool App::animate_details() {
    if (!popup_ || !x_.visible(popup_) || !details_view_.animations() ||
        Clock::now() >= details_settle_until_)
        return false;
    render_details_frame(details_pointer_);
    return true;
}

void App::render_details_frame(ClayWidgets_Input input) {
    if (!popup_)
        return;
    // Elapsed time since the previous frame drives the easing; a long idle gap is
    // capped so a transition never jumps most of the way on its first frame.
    const auto now = Clock::now();
    input.deltaTime = details_rendered_ == Clock::time_point{}
                          ? 0.f
                          : std::min(0.05f, std::chrono::duration<float>(now - details_rendered_).count());
    details_rendered_ = now;
    const auto bounds = x_.bounds(popup_);
    if (bounds.empty())
        return;
    renderer_.set_surface(scale_, details_view_.text_gamma());
    if (details_scale_ != scale_) {
        details_view_.invalidate_measurements();
        details_scale_ = scale_;
    }
    const float width = static_cast<float>(bounds.width) / scale_,
                height = static_cast<float>(bounds.height) / scale_;
    auto frame = details_view_.frame(usage_, input, width, height);
    if (frame.close) {
        close_details();
        return;
    }
    if (frame.refresh) {
        providers_.detect();
        frame.changed = true;
        providers_.refresh();
    }
    if (frame.check_updates && updater_)
        updater_->check_now();
    if (frame.install_update)
        install_update();
    if (frame.reset_all)
        reset_place_on_save_ = true;
    if (frame.save) {
        if (!save_settings(details_view_.preferences()))
            return;
        if (reset_place_on_save_)
            forget_position();
        close_details();
        update_usage();
        return;
    }
    if (frame.changed) {
        if (settings_mode_)
            apply_preferences(settings_edit_.preview(details_view_.preferences()));
        else
            update_usage();
        // Settle the borrowed percentage labels after the slider changes data.
        // Same instant as the frame above, so no further easing time elapses.
        auto settle = details_pointer_;
        settle.deltaTime = 0.f;
        frame = details_view_.frame(usage_, settle, width, height);
    }
    details_pixels_ = renderer_.render(frame.commands, bounds.width, bounds.height, scale_, false);
    ui::round_corners(details_pixels_, round(window_radius * scale_));
    x_.set_hand_cursor(popup_, details_view_.cursor() == CLAY_WIDGETS_CURSOR_POINTER);
    present_details();
}

void App::present_details() {
    if (popup_)
        x_.present(popup_, details_pixels_);
}

void App::details_event(const x11::Event& event) {
    using Type = x11::Event::Type;
    if (event.type == Type::Paint) {
        present_details();
        return;
    }
    if (event.type == Type::Close) {
        close_details();
        return;
    }
    // A drag that leaves the window keeps reporting real coordinates.
    if (event.type == Type::Leave && event.buttons_held)
        return;
    if (event.type == Type::Configure || event.type == Type::Enter)
        return;
    auto input = details_pointer_;
    if (event.type == Type::Motion || event.type == Type::Press || event.type == Type::Release) {
        input.mouseX = static_cast<float>(event.x) / scale_;
        input.mouseY = static_cast<float>(event.y) / scale_;
        if (event.type == Type::Motion && input.mouseX == details_pointer_.mouseX &&
            input.mouseY == details_pointer_.mouseY)
            return;
    }
    if (event.type == Type::Leave)
        input.mouseX = input.mouseY = -100;
    if (event.type == Type::Press) {
        if (event.button == 4 || event.button == 5)
            input.scrollY = event.button == 4 ? 1.f : -1.f;
        else if (event.button != 1)
            return;
        else if (static_cast<float>(event.y) < drag_strip * scale_) {
            // The heading strip works as a title bar.
            x_.begin_move(popup_, event);
            return;
        } else
            input.pointerDown = input.pointerPressed = true;
    }
    if (event.type == Type::Release) {
        if (event.button != 1)
            return;
        input.pointerDown = false;
        input.pointerReleased = true;
    }
    if (event.type == Type::Key) {
        using x11::Key;
        input.shiftDown = event.shift;
        input.keyTab = event.key == Key::Tab;
        input.keyLeft = event.key == Key::Left;
        input.keyRight = event.key == Key::Right;
        input.keyUp = event.key == Key::Up;
        input.keyDown = event.key == Key::Down;
        input.keyHome = event.key == Key::Home;
        input.keyEnd = event.key == Key::End;
        input.keyEnter = event.key == Key::Enter;
        input.keySpace = event.key == Key::Space;
        input.keyEscape = event.key == Key::Escape;
    }
    details_pointer_ = {};
    details_pointer_.mouseX = input.mouseX;
    details_pointer_.mouseY = input.mouseY;
    details_pointer_.pointerDown = input.pointerDown;
    render_details(input);
}

// ---- Context menu ----

void App::show_menu() {
    hide_hover(true);
    if (!menu_)
        menu_ = x_.create(x11::Role::Menu, {0, 0, 1, 1}, "BarTab menu");
    ui::MenuModel model;
    const auto startup = host::startup_state();
    model.startup_enabled = startup.enabled;
    model.startup_available = startup.error.empty();
    model.debug_label = mock_ ? "Next mock scenario" : nullptr;
    using update::State;
    update_label_ = "Update to version " + update_status_.latest;
    if (update_status_.state == State::Available || update_status_.state == State::Ready)
        model.update_label = update_label_.c_str();
    menu_view_.open_menu(model);
    // Lay the menu out once in a roomy view to learn its size, then fit the window to it.
    menu_pointer_ = {};
    menu_pointer_.mouseX = menu_pointer_.mouseY = -100;
    renderer_.set_surface(scale_, menu_view_.text_gamma());
    menu_view_.frame(usage_, menu_pointer_, 600, 600);
    const auto panel = menu_view_.menu_bounds();
    const int width = static_cast<int>(std::ceil(panel.width * scale_));
    const int height = static_cast<int>(std::ceil(panel.height * scale_));
    // Down and right of the pointer, flipping to whichever side has room.
    int px{}, py{};
    x_.pointer(px, py);
    const auto work = x_.monitor_at(px, py).work;
    Rect bounds{px, py, width, height};
    if (bounds.right() > work.right())
        bounds.x = px - width;
    if (bounds.bottom() > work.bottom())
        bounds.y = py - height;
    x_.place(menu_, clamp_into(bounds, work));
    x_.show(menu_);
    render_menu(menu_pointer_);
    if (!x_.grab(menu_))
        host::log("Could not grab input for the context menu; it closes on its next click or Escape.");
}

void App::close_menu() {
    if (!menu_ || !x_.visible(menu_))
        return;
    x_.ungrab();
    x_.hide(menu_);
    if (hover_pinned_)
        show_hover();
}

void App::render_menu(ClayWidgets_Input input) {
    const auto bounds = x_.bounds(menu_);
    if (bounds.empty())
        return;
    renderer_.set_surface(scale_, menu_view_.text_gamma());
    const auto frame = menu_view_.frame(usage_, input, static_cast<float>(bounds.width) / scale_,
                                        static_cast<float>(bounds.height) / scale_);
    using Choice = ui::Frame::MenuChoice;
    if (frame.close || frame.menu != Choice::None) {
        close_menu();
        switch (frame.menu) {
        case Choice::Settings:
            open_details(true);
            break;
        case Choice::Debug:
            if (mock_)
                cycle_mock();
            break;
        case Choice::Startup: {
            const auto error = host::set_startup(!host::startup_state().enabled);
            if (!error.empty())
                host::log("Could not change Start at login: " + error);
            break;
        }
        case Choice::Update:
            install_update();
            break;
        case Choice::Quit:
            host::log("Quit from the context menu.");
            running_ = false;
            break;
        case Choice::None:
            break;
        }
        return;
    }
    menu_pixels_ = renderer_.render(frame.commands, bounds.width, bounds.height, scale_, false);
    x_.set_hand_cursor(menu_, menu_view_.cursor() == CLAY_WIDGETS_CURSOR_POINTER);
    x_.present(menu_, menu_pixels_);
}

void App::menu_event(const x11::Event& event) {
    using Type = x11::Event::Type;
    if (event.type == Type::Paint) {
        x_.present(menu_, menu_pixels_);
        return;
    }
    auto input = menu_pointer_;
    switch (event.type) {
    case Type::Enter:
    case Type::Motion:
        input.mouseX = static_cast<float>(event.x) / scale_;
        input.mouseY = static_cast<float>(event.y) / scale_;
        break;
    case Type::Leave:
        input.mouseX = input.mouseY = -100;
        break;
    // Either button picks an item, as in native menus; through the grab, a
    // press outside the app's windows arrives here and dismisses the menu.
    case Type::Press:
        if (event.button != 1 && event.button != 3)
            return;
        input.mouseX = static_cast<float>(event.x) / scale_;
        input.mouseY = static_cast<float>(event.y) / scale_;
        input.pointerDown = input.pointerPressed = true;
        break;
    case Type::Release:
        if (event.button != 1 && event.button != 3)
            return;
        input.pointerDown = false;
        input.pointerReleased = true;
        break;
    case Type::Key:
        input.keyUp = event.key == x11::Key::Up;
        input.keyDown = event.key == x11::Key::Down || event.key == x11::Key::Tab;
        input.keyHome = event.key == x11::Key::Home;
        input.keyEnd = event.key == x11::Key::End;
        input.keyEnter = event.key == x11::Key::Enter;
        input.keySpace = event.key == x11::Key::Space;
        input.keyEscape = event.key == x11::Key::Escape;
        break;
    default:
        return;
    }
    menu_pointer_ = {};
    menu_pointer_.mouseX = input.mouseX;
    menu_pointer_.mouseY = input.mouseY;
    menu_pointer_.pointerDown = input.pointerDown;
    render_menu(input);
}

// ---- Confetti ----

// Covers the widget and the space above and beside it, clipped to its monitor,
// and launches the pieces from the widget's middle. A burst during a burst adds
// to it in the same overlay.
void App::celebrate() {
    const auto& widget = widget_bounds_;
    if (!animations_allowed_ || !host::animations_enabled() || !widget_ || !x_.visible(widget_) ||
        widget.empty())
        return;
    if (!confetti_window_)
        confetti_window_ = x_.create(x11::Role::Overlay, {0, 0, 1, 1}, "BarTab confetti");
    if (confetti_.done()) {
        const auto monitor = x_.monitor_at(widget.x + widget.width / 2, widget.y + widget.height / 2).bounds;
        const int spread = round(confetti_spread * scale_), rise = round(confetti_rise * scale_);
        const Rect area{widget.x - spread, widget.y - rise, widget.width + 2 * spread, widget.height + rise};
        confetti_bounds_ = intersection(area, monitor);
        x_.place(confetti_window_, confetti_bounds_);
    }
    confetti_.burst(
        static_cast<float>(widget.x - confetti_bounds_.x) / scale_, static_cast<float>(widget.width) / scale_,
        static_cast<float>(widget.y + widget.height / 2 - confetti_bounds_.y) / scale_, confetti_pieces);
    confetti_frame_ = Clock::now();
    x_.show(confetti_window_);
    animate_confetti();
}

bool App::animate_confetti() {
    const auto now = Clock::now();
    confetti_.step(std::chrono::duration<float>(now - confetti_frame_).count());
    confetti_frame_ = now;
    if (confetti_.done()) {
        x_.hide(confetti_window_);
        return false;
    }
    if (confetti_bounds_.empty())
        return true;
    x_.present(confetti_window_,
               renderer_.render_confetti(confetti_, confetti_bounds_.width, confetti_bounds_.height, scale_));
    return true;
}

// ---- Smoke test ----

// Offline and deterministic: demo data, no animations, isolated settings and
// position files beside the executable. Writes smoke-test.txt there and exits 0
// when every check passes.
void App::run_smoke_test() {
    std::ofstream report(host::executable_directory() / "smoke-test.txt", std::ios::trunc);
    bool ok = true;
    auto check = [&](bool value, const char* name) {
        report << (value ? "ok     " : "FAILED ") << name << '\n';
        std::cout << (value ? "ok     " : "FAILED ") << name << '\n';
        ok = ok && value;
    };
    auto pump = [&](int milliseconds) { loop(Clock::now() + std::chrono::milliseconds(milliseconds)); };
    auto event = [](x11::Event::Type type, x11::WindowId window) {
        x11::Event result;
        result.type = type;
        result.window = window;
        result.button = 1;
        return result;
    };
    pump(500);
    check(widget_ && x_.visible(widget_) && !widget_bounds_.empty(), "Widget window shown");
    check(opaque(widget_pixels_) > widget_pixels_.data.size() / 2, "Widget painted over its backdrop");
    check(std::adjacent_find(widget_pixels_.data.begin(), widget_pixels_.data.end(), std::not_equal_to<>()) !=
              widget_pixels_.data.end(),
          "Widget draws its usage, not just a backdrop");

    widget_event(event(x11::Event::Type::Enter, widget_));
    pump(100);
    const auto card = hover_ ? x_.bounds(hover_) : Rect{};
    check(hover_ && x_.visible(hover_) && opaque(hover_pixels_) > 0, "Hover card opens on pointer entry");
    check(!card.empty() && intersection(card, widget_bounds_).empty(), "Hover card sits beside the widget");
    widget_event(event(x11::Event::Type::Leave, widget_));
    pump(400);
    check(!x_.visible(hover_), "Hover card closes after the pointer leaves");
    // Onto the card and off it again, as XWayland reports it when the pointer
    // moves on to a Wayland window: crossing events, but a stale position.
    widget_event(event(x11::Event::Type::Enter, widget_));
    widget_event(event(x11::Event::Type::Leave, widget_));
    hover_event(event(x11::Event::Type::Enter, hover_));
    pump(400);
    check(x_.visible(hover_), "Hover card stays open while the pointer is on it");
    hover_event(event(x11::Event::Type::Leave, hover_));
    pump(400);
    check(!x_.visible(hover_), "Hover card closes when the pointer leaves the card");

    widget_event(event(x11::Event::Type::Press, widget_));
    widget_event(event(x11::Event::Type::Release, widget_));
    pump(100);
    check(popup_ && x_.visible(popup_) && !settings_mode_ && opaque(details_pixels_) > 0,
          "A click opens the demo details");
    close_details();

    // Live data from here on (still offline: demo mode never polls), so
    // Settings shows the real panel rather than the demo one.
    usage_.live = true;
    usage_.codex.installed = usage_.claude.installed = true;
    open_details(true);
    pump(200);
    check(x_.visible(popup_) && details_view_.bounds("SaveSettings").width > 0, "Settings open");
    check(details_view_.bounds("WidgetWidth").width > 0 &&
              details_view_.bounds("WidgetPosition").width == 0 &&
              details_view_.bounds("AllTaskbars").width == 0,
          "Floating widget settings leave out taskbar placement");
    check(hover_pinned_ && x_.visible(hover_),
          "Hover card stays pinned as a preview while settings are open");
    const int width_before = widget_bounds_.width;
    auto wider = preferences_;
    wider.appearance.widget_width = 320;
    apply_preferences(settings_edit_.preview(wider));
    pump(100);
    check(widget_bounds_.width > width_before, "Widget width previews live");
    auto escape = event(x11::Event::Type::Key, popup_);
    escape.key = x11::Key::Escape;
    details_event(escape);
    pump(100);
    check(!x_.visible(popup_) && preferences_.appearance.widget_width == Appearance{}.widget_width &&
              widget_bounds_.width == width_before,
          "Escape cancels the preview");

    open_details(true);
    auto thicker = preferences_;
    thicker.appearance.bar_height = 4;
    check(save_settings(thicker) && host::read_settings(settings_path_) == thicker,
          "Saving writes the settings file");
    close_details();

    saved_position_ = std::make_pair(widget_bounds_.x - 10, widget_bounds_.y - 10);
    save_position();
    const auto placed = saved_position_;
    saved_position_.reset();
    load_position();
    check(saved_position_ == placed, "The widget position survives a restart");

    auto taller = preferences_;
    taller.appearance.widget_height = 30;
    apply_preferences(taller);
    pump(100);
    check(x_.bounds(widget_).height == round(30 * scale_), "The height setting sizes the widget exactly");
    // The left edge, halfway down, is in the padding before any text.
    auto edge_alpha = [this] {
        return widget_pixels_.data[static_cast<std::size_t>(widget_pixels_.height / 2) * widget_pixels_.width + 1] >>
               24;
    };
    const auto opaque_alpha = edge_alpha();
    auto clear = preferences_;
    clear.appearance.widget_opacity = 0;
    apply_preferences(clear);
    pump(100);
    check(opaque_alpha == 235 && edge_alpha() == 0, "The background opacity setting fades the widget's panel");
    apply_preferences(taller);
    // From the middle of the screen, so the drag has room in every direction.
    const auto middle = x_.monitor_at(0, 0).bounds;
    saved_position_ = std::make_pair(middle.x + middle.width / 2, middle.y + middle.height / 2);
    place_widget();
    pump(100);
    const auto before_drag = widget_bounds_;
    auto press = event(x11::Event::Type::Press, widget_);
    press.x = press.y = 5;
    press.root_x = before_drag.x + 5;
    press.root_y = before_drag.y + 5;
    widget_event(press);
    auto drag = press;
    drag.type = x11::Event::Type::Motion;
    drag.buttons_held = true;
    drag.root_x -= 120;
    drag.root_y -= 60;
    widget_event(drag);
    auto drop = drag;
    drop.type = x11::Event::Type::Release;
    drop.buttons_held = false;
    widget_event(drop);
    pump(100);
    const auto dragged = x_.bounds(widget_);
    saved_position_.reset();
    load_position();
    check(dragged.x == before_drag.x - 120 && dragged.y == before_drag.y - 60 &&
              saved_position_ == std::make_pair(dragged.x, dragged.y) && !(popup_ && x_.visible(popup_)),
          "Dragging moves the widget and saves the spot, without opening details");

    // Docking: publish a 30 px top panel the way GNOME does, as a work area
    // that starts below it, then drag into it and back out.
    const auto drag_to = [&](int root_x, int root_y) {
        const auto from = widget_bounds_;
        auto grab = event(x11::Event::Type::Press, widget_);
        grab.x = grab.y = 5;
        grab.root_x = from.x + 5;
        grab.root_y = from.y + 5;
        widget_event(grab);
        auto move = grab;
        move.type = x11::Event::Type::Motion;
        move.buttons_held = true;
        move.root_x = root_x;
        move.root_y = root_y;
        widget_event(move);
        auto release = move;
        release.type = x11::Event::Type::Release;
        release.buttons_held = false;
        widget_event(release);
        pump(100);
    };
    const auto screen = x_.monitor_at(0, 0).bounds;
    const auto area = std::to_string(screen.x) + ", " + std::to_string(screen.y + 30) + ", " +
                      std::to_string(screen.width) + ", " + std::to_string(screen.height - 30);
    if (std::system(("xprop -root -f _GTK_WORKAREAS_D0 32c -set _GTK_WORKAREAS_D0 '" + area + "' 2>/dev/null")
                        .c_str()) == 0) {
        drag_to(screen.x + 300, screen.y + 10);
        const auto docked = x_.bounds(widget_);
        saved_position_.reset();
        load_position();
        check(docked.y == screen.y + round(dock_inset * scale_) &&
                  docked.height == 30 - 2 * round(dock_inset * scale_) && dock_ == Dock::Top,
              "Dropped onto a panel, the widget docks inside it, just inset from its edges");
        drag_to(screen.x + 300, screen.y + 300);
        check(x_.bounds(widget_).height == round(38 * scale_) && dock_ == Dock::None &&
                  preferences_.appearance.widget_height == 38 &&
                  host::read_settings(settings_path_) == preferences_,
              "Dragged back out, it returns to the standard 38 px height, saved");
        // A flick out of the panel: the only motion is inside it, and the
        // button comes up far below.
        drag_to(screen.x + 300, screen.y + 10);
        {
            const auto from = widget_bounds_;
            auto grab = event(x11::Event::Type::Press, widget_);
            grab.x = grab.y = 5;
            grab.root_x = from.x + 5;
            grab.root_y = from.y + 5;
            widget_event(grab);
            auto inside = grab;
            inside.type = x11::Event::Type::Motion;
            inside.buttons_held = true;
            inside.root_x += 40;
            inside.root_y = screen.y + 12;
            widget_event(inside);
            auto flick = inside;
            flick.type = x11::Event::Type::Release;
            flick.buttons_held = false;
            flick.root_y = screen.y + 400;
            widget_event(flick);
            pump(100);
        }
        const auto flicked = x_.bounds(widget_);
        check(dock_ == Dock::None && flicked.height == round(38 * scale_) && flicked.y > screen.y + 300,
              "Flicked out of the panel, it lands where it is let go, at full height");
        // Nothing saved yet: the first-run spot, in the top panel a third of
        // the way in from the right.
        saved_position_.reset();
        dock_ = Dock::None;
        place_widget();
        pump(100);
        const auto first = x_.bounds(widget_);
        check(first.y == screen.y + round(dock_inset * scale_) && dock_ == Dock::Top &&
                  std::abs(first.x + first.width / 2 - (screen.right() - screen.width / 3)) <= 1,
              "By default the widget docks in the top panel, a third in from the right");
        std::system("xprop -root -remove _GTK_WORKAREAS_D0 2>/dev/null");
    } else {
        report << "skipped panel docking: xprop is not installed\n";
    }

    ui::Confetti burst(3);
    burst.burst(0, 100, 150, 60);
    check(opaque(renderer_.render_confetti(burst, 300, 200, 1.f)) > 0, "Confetti renders");

    // Reset all settings, then Save: preferences and the widget's place are
    // back at their defaults, and the position file is gone.
    {
        auto custom = preferences_;
        custom.appearance.widget_width = 330;
        custom.claude_interval = 300;
        save_settings(custom);
        saved_position_ = std::make_pair(widget_bounds_.x + 40, widget_bounds_.y + 40);
        save_position();
        place_widget();
        open_details(true);
        details_view_.set_settings_page(ui::SettingsPage::General);
        render_details(details_pointer_);
        const auto reset = details_view_.bounds("ResetAll");
        const auto save = details_view_.bounds("SaveSettings");
        auto press = event(x11::Event::Type::Press, popup_);
        auto click_at = [&](const Clay_BoundingBox& box) {
            press.x = static_cast<int>((box.x + box.width / 2) * scale_);
            press.y = static_cast<int>((box.y + box.height / 2) * scale_);
            details_event(press);
            auto release = press;
            release.type = x11::Event::Type::Release;
            details_event(release);
        };
        click_at(reset);
        const bool previewed = preferences_ == Preferences{} && std::filesystem::exists(position_path_);
        click_at(save);
        pump(100);
        check(reset.width > 0 && previewed && !x_.visible(popup_) && preferences_ == Preferences{} &&
                  host::read_settings(settings_path_) == Preferences{} && !saved_position_ &&
                  !std::filesystem::exists(position_path_),
              "Reset all settings, saved, restores the defaults and the widget's place");
    }

    // The context menu: right-click opens it, and clicks land on its items by
    // their laid-out bounds, as the pointer would.
    // Opened over a pinned card, the menu hides it until it closes.
    pin_hover();
    pump(100);
    const bool card_before_menu = hover_ && x_.visible(hover_);
    auto right = event(x11::Event::Type::Release, widget_);
    right.button = 3;
    widget_event(right);
    pump(100);
    const auto menu = menu_ ? x_.bounds(menu_) : Rect{};
    check(menu_ && x_.visible(menu_) && menu.width >= 180 &&
              opaque(menu_pixels_) > menu_pixels_.data.size() / 2,
          "Right-click opens the context menu, sized to its panel");
    hovered_ = true;
    show_hover();
    check(card_before_menu && !x_.visible(hover_), "The open menu suppresses the hover card");
    auto escape_menu = event(x11::Event::Type::Key, menu_);
    escape_menu.key = x11::Key::Escape;
    menu_event(escape_menu);
    check(!x_.visible(menu_) && x_.visible(hover_), "Escape closes the menu, and a pinned card returns");
    unpin_hover();
    auto click_item = [&](const char* name) {
        const auto item = menu_view_.bounds(name);
        auto press = event(x11::Event::Type::Press, menu_);
        press.x = static_cast<int>((item.x + item.width / 2) * scale_);
        press.y = static_cast<int>((item.y + item.height / 2) * scale_);
        auto release = press;
        release.type = x11::Event::Type::Release;
        menu_event(press);
        menu_event(release);
        pump(100);
    };
    widget_event(right);
    auto outside = event(x11::Event::Type::Press, menu_);
    outside.x = outside.y = -20;
    menu_event(outside);
    check(!x_.visible(menu_), "A click outside closes the menu");
    widget_event(right);
    click_item("MenuSettings");
    check(!x_.visible(menu_) && popup_ && x_.visible(popup_) && settings_mode_,
          "The menu's Settings opens settings");
    close_details();
    widget_event(right);
    click_item("MenuQuit");
    check(!running_, "The menu's Quit ends the app");

    report << (ok ? "PASS" : "FAIL") << '\n';
    std::cout << (ok ? "PASS" : "FAIL") << '\n';
    exit_code_ = ok ? 0 : 1;
}
} // namespace usage::linux_host
