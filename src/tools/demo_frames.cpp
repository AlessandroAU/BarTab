// Renders the README's demo clips: the real widget, hover card, settings window
// and confetti, driven by a scripted pointer on a painted Windows 11 desktop.
//
// A small offscreen stand-in for the Windows host: it places the widget with the
// same free-space rules, opens and grows the hover card, pins it and moves
// Settings aside, previews settings live and bursts confetti exactly as
// src/windows does, but with a scripted clock and pointer instead of window
// messages. demo_media.py paints what Windows supplies (wallpaper, taskbar, the
// native context menu, cursors) and encodes what this streams to stdout: one
// JSON header line, then raw RGB frames through a panning camera.
//
//     demo_frames <art directory> <scene>
#include "ui/raylib_renderer.hpp"
#include "ui/views.hpp"
#include "host/platform.hpp"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {
using namespace usage;

constexpr int fps = 60;
constexpr float frame_time = 1.f / fps;
// The camera's output; the desktop renders at 2x, so a camera 800 DIPs wide is 1:1.
constexpr int output_width = 1600, output_height = 1000;
// As src/windows: the card grows over 200 ms and closes 200 ms after the pointer
// leaves it; confetti rises and spreads into this much room around the widget.
constexpr float hover_open_seconds = 0.2f, hover_grace_seconds = 0.2f;
constexpr float confetti_rise = 300.f, confetti_spread = 300.f;
constexpr int confetti_pieces = 250;
// Windows 11 fades a context menu in while sliding it a few DIPs.
constexpr float menu_open_seconds = 0.15f, menu_slide = 6.f;

struct Point {
    float x{}, y{};
};
struct Box {
    float x{}, y{}, w{}, h{};
    bool contains(Point p) const {
        return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h;
    }
};
Box box(const Rect& r) {
    return {static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.width),
            static_cast<float>(r.height)};
}
float ease_out(float t) {
    return 1.f - (1.f - t) * (1.f - t) * (1.f - t);
}
float ease_in_out(float t) {
    return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) / 2.f;
}

ui::Pixels load_raw(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::uint32_t size[2]{};
    if (!in.read(reinterpret_cast<char*>(size), sizeof(size)))
        throw std::runtime_error("Could not read " + path.string());
    ui::Pixels pixels;
    pixels.width = static_cast<int>(size[0]);
    pixels.height = static_cast<int>(size[1]);
    pixels.data.resize(static_cast<std::size_t>(pixels.width) * pixels.height);
    if (!in.read(reinterpret_cast<char*>(pixels.data.data()),
                 static_cast<std::streamsize>(pixels.data.size() * sizeof(std::uint32_t))))
        throw std::runtime_error("Truncated " + path.string());
    return pixels;
}

// Premultiplied source over premultiplied destination, clipped to `clip`.
void blit(ui::Pixels& target, const ui::Pixels& source, int left, int top, const Rect& clip, float opacity = 1.f) {
    const int x0 = std::max({left, clip.x, 0}), y0 = std::max({top, clip.y, 0});
    const int x1 = std::min({left + source.width, clip.right(), target.width});
    const int y1 = std::min({top + source.height, clip.bottom(), target.height});
    const auto scale = static_cast<std::uint32_t>(std::lround(std::clamp(opacity, 0.f, 1.f) * 256.f));
    if (scale == 0)
        return;
    for (int y = y0; y < y1; ++y) {
        const auto* from = &source.data[static_cast<std::size_t>(y - top) * source.width + (x0 - left)];
        auto* to = &target.data[static_cast<std::size_t>(y) * target.width + x0];
        for (int x = x0; x < x1; ++x, ++from, ++to) {
            std::uint32_t s = *from;
            if (scale < 256) {
                s = ((((s & 0x00ff00ffu) * scale) >> 8) & 0x00ff00ffu) |
                    ((((s >> 8) & 0x00ff00ffu) * scale) & 0xff00ff00u);
            }
            const std::uint32_t alpha = s >> 24;
            if (alpha == 0)
                continue;
            if (alpha == 255) {
                *to = s;
                continue;
            }
            const std::uint32_t d = *to, keep = 255 - alpha;
            const auto channel = [&](int shift) {
                return std::min<std::uint32_t>(255, ((s >> shift) & 0xff) + ((d >> shift) & 0xff) * keep / 255)
                       << shift;
            };
            *to = channel(24) | channel(16) | channel(8) | channel(0);
        }
    }
}

// Everything demo_media.py painted, in physical pixels.
struct Art {
    float scale{2};
    std::int64_t now{};
    Rect screen, taskbar;
    std::vector<Rect> occupied;
    ui::Pixels desktop;
    ui::Pixels arrow, hand;
    Point arrow_hotspot, hand_hotspot;
    std::map<std::string, ui::Pixels> menus;
    Rect menu_panel;
    std::vector<Rect> menu_items;

    explicit Art(const std::filesystem::path& directory) {
        std::ifstream in(directory / "desktop.json");
        const auto spec = nlohmann::json::parse(in);
        const auto rect = [](const nlohmann::json& v) {
            return Rect{v[0].get<int>(), v[1].get<int>(), v[2].get<int>(), v[3].get<int>()};
        };
        scale = spec["scale"].get<float>();
        now = spec["now"].get<std::int64_t>();
        screen = {0, 0, spec["screen"][0].get<int>(), spec["screen"][1].get<int>()};
        taskbar = rect(spec["taskbar"]);
        for (const auto& r : spec["occupied"])
            occupied.push_back(rect(r));
        desktop = load_raw(directory / spec["desktop"].get<std::string>());
        const auto& cursors = spec["cursors"];
        arrow = load_raw(directory / cursors["arrow"]["file"].get<std::string>());
        hand = load_raw(directory / cursors["hand"]["file"].get<std::string>());
        arrow_hotspot = {cursors["arrow"]["hotspot"][0].get<float>(), cursors["arrow"]["hotspot"][1].get<float>()};
        hand_hotspot = {cursors["hand"]["hotspot"][0].get<float>(), cursors["hand"]["hotspot"][1].get<float>()};
        const auto& menu = spec["menu"];
        for (const auto& [key, file] : menu["sprites"].items())
            menus[key] = load_raw(directory / file.get<std::string>());
        menu_panel = rect(menu["panel"]);
        for (const auto& r : menu["items"])
            menu_items.push_back(rect(r));
    }
    Rect work() const {
        return {screen.x, screen.y, screen.width, taskbar.y - screen.y};
    }
};

// Fixed readings, as in the screenshots: Codex on both windows, Claude with its
// overall and Fable weekly allowances.
Usage demo_usage(std::int64_t now) {
    const std::int64_t session_reset = now + 8040, weekly_reset = now + 94440, fable_reset = now + 180840;
    Usage data;
    data.live = true;
    data.codex.installed = true;
    data.codex.plan = "ChatGPT Pro";
    data.codex.credit_balance = "12.5";
    data.codex.available_resets = 2;
    data.codex.executable_path = "C:/Users/demo/AppData/Local/Codex/codex.exe";
    data.codex.windows = {{"5 hour", 62, session_reset}, {"Weekly", 81, weekly_reset}};
    data.codex.updated = now - 60;
    data.claude.installed = true;
    data.claude.plan = "Claude Max";
    data.claude.executable_path = "C:/Users/demo/AppData/Local/Claude/claude.exe";
    data.claude.windows = {{"5 hour", 97, session_reset}, {"Weekly", 44, weekly_reset},
                           {"Fable weekly", 23, fable_reset}};
    data.claude.updated = now - 60;
    return data;
}

// The Windows host's behaviour, minus windows: every surface is a rectangle in
// screen pixels with the pixels last rendered for it.
class Host {
  public:
    explicit Host(const Art& art) : art_(art), usage_(demo_usage(art.now)) {
        auto regular = host::ui_font(false), bold = host::ui_font(true);
        if (!renderer_.load_font_data(ui::regular_font, std::move(regular.bytes), regular.face_index) ||
            !renderer_.load_font_data(ui::bold_font, std::move(bold.bytes), bold.face_index))
            throw std::runtime_error("Could not read the system UI font");
        for (auto* view : {&widget_view_, &hover_view_, &settings_view_})
            view->set_reference_time(art.now);
        settings_view_.set_animations(true);
        preferences_.normalize();
        apply_preferences(preferences_);
    }

    float scale() const {
        return art_.scale;
    }
    Usage& usage() {
        return usage_;
    }
    Box widget() const {
        return box(widget_);
    }
    Box card() const {
        return box(card_);
    }
    Box settings() const {
        return box(settings_);
    }
    bool card_visible() const {
        return card_visible_;
    }
    // A settings element's rectangle on screen, as of the last frame.
    Box element(const char* id) {
        const auto b = settings_view_.bounds(id);
        return {settings_.x + b.x * scale(), settings_.y + b.y * scale(), b.width * scale(), b.height * scale()};
    }
    // A context menu item's rectangle on screen while the menu is open.
    Box menu_item(int index) const {
        const auto& item = art_.menu_items[static_cast<std::size_t>(index)];
        return {static_cast<float>(menu_.x + art_.menu_panel.x + item.x),
                static_cast<float>(menu_.y + art_.menu_panel.y + item.y),
                static_cast<float>(item.width), static_cast<float>(item.height)};
    }
    void set_page(ui::SettingsPage page) {
        settings_view_.set_settings_page(page);
    }

    // ---- input, as window messages would deliver it

    void pointer_move(Point p) {
        pointer_ = p;
        if (menu_open_) {
            menu_highlight_ = -1;
            for (int i = 0; i < static_cast<int>(art_.menu_items.size()); ++i)
                if (menu_item(i).contains(p))
                    menu_highlight_ = i;
            return;
        }
        // Outside Settings the view sees the pointer leave, unless a drag holds it.
        const bool to_settings = settings_captured_ || (settings_open_ && settings().contains(p));
        settings_input_.mouseX = to_settings ? (p.x - settings_.x) / scale() : -100.f;
        settings_input_.mouseY = to_settings ? (p.y - settings_.y) / scale() : -100.f;
        const bool over_widget = widget().contains(p) && !(settings_open_ && settings().contains(p));
        if (over_widget) {
            leave_timer_ = -1.f;
            if (!hovered_ && !left_down_ && preferences_.appearance.hover_enabled) {
                hovered_ = true;
                show_hover();
            }
        } else if (card_visible_ && card().contains(p)) {
            leave_timer_ = -1.f;
        } else if ((hovered_ || card_visible_) && leave_timer_ < 0.f) {
            if (card_visible_)
                leave_timer_ = hover_grace_seconds;
            else
                hide_hover();
        }
    }
    void left_press() {
        left_down_ = true;
        if (menu_open_) {
            if (menu_highlight_ < 0)
                close_menu(-1);
            return;
        }
        if (settings_open_ && settings().contains(pointer_)) {
            settings_captured_ = true;
            settings_input_.pointerPressed = settings_input_.pointerDown = true;
        } else if (widget().contains(pointer_)) {
            widget_captured_ = true;
            hide_hover();
        }
    }
    void left_release() {
        left_down_ = false;
        if (menu_open_) {
            if (menu_highlight_ >= 0)
                close_menu(menu_highlight_);
            return;
        }
        if (settings_captured_) {
            settings_captured_ = false;
            settings_input_.pointerDown = false;
            settings_input_.pointerReleased = true;
        } else if (widget_captured_) {
            widget_captured_ = false;
            if (widget().contains(pointer_))
                open_settings();
        }
    }
    void right_click() {
        if (!widget().contains(pointer_) || menu_open_)
            return;
        // TrackPopupMenu opens below and right of the pointer, flipping above
        // when it would run off the bottom of the monitor.
        menu_open_ = true;
        menu_highlight_ = -1;
        menu_opened_ = time_;
        hide_hover(true);
        const auto& sprite = art_.menus.at("on0");
        menu_.x = static_cast<int>(pointer_.x) - art_.menu_panel.x;
        menu_.y = static_cast<int>(pointer_.y) - art_.menu_panel.y;
        if (pointer_.y + art_.menu_panel.height > art_.screen.bottom())
            menu_.y = static_cast<int>(pointer_.y) - art_.menu_panel.height - art_.menu_panel.y;
        if (pointer_.x + art_.menu_panel.width > art_.screen.right())
            menu_.x = static_cast<int>(pointer_.x) - art_.menu_panel.width - art_.menu_panel.x;
        menu_.width = sprite.width;
        menu_.height = sprite.height;
    }

    // ---- the app's own reactions

    void set_providers(bool codex, bool claude) {
        auto value = preferences_;
        value.codex_enabled = codex;
        value.claude_enabled = claude;
        apply_preferences(value);
    }
    // A usage window reset: the reading jumps back up and confetti bursts.
    void celebrate() {
        const float s = scale();
        const auto& w = widget_;
        confetti_area_.x = std::max(art_.screen.x, w.x - static_cast<int>(std::lround(confetti_spread * s)));
        confetti_area_.y = std::max(art_.screen.y, w.y - static_cast<int>(std::lround(confetti_rise * s)));
        confetti_area_.width =
            std::min(art_.screen.right(), w.right() + static_cast<int>(std::lround(confetti_spread * s))) -
            confetti_area_.x;
        confetti_area_.height = w.bottom() - confetti_area_.y;
        confetti_.burst((w.x - confetti_area_.x) / s, w.width / s, (w.y + w.height / 2.f - confetti_area_.y) / s,
                        confetti_pieces);
    }
    void data_changed() {
        render_widget();
        if (card_visible_)
            show_hover();
    }

    // Advances one frame and composites every surface onto `frame` within `clip`.
    void step(ui::Pixels& frame, const Rect& clip) {
        time_ += frame_time;
        if (leave_timer_ >= 0.f && (leave_timer_ -= frame_time) < 0.f)
            hide_hover();
        if (settings_open_)
            render_settings();
        settings_input_.pointerPressed = settings_input_.pointerReleased = false;
        render_widget();
        if (!confetti_.done())
            confetti_.step(frame_time);

        blit(frame, widget_pixels_, widget_.x, widget_.y, clip);
        if (settings_open_)
            blit(frame, settings_pixels_, settings_.x, settings_.y, clip);
        if (card_visible_) {
            const float progress = ease_out(std::min(1.f, (time_ - card_opened_) / hover_open_seconds));
            auto pixels = card_pixels_;
            const int height = pixels.height;
            const int visible = std::max(1, static_cast<int>(std::lround(height * progress)));
            const int top = height - visible; // the card sits above the taskbar and grows upward
            ui::shift_rows(pixels, top);
            ui::round_corners(pixels, static_cast<int>(std::lround(12 * scale())), top, top + visible);
            ui::fade(pixels, preferences_.appearance.hover_opacity / 100.f * progress);
            blit(frame, pixels, card_.x, card_.y, clip);
        }
        if (!confetti_.done()) {
            const auto pixels =
                renderer_.render_confetti(confetti_, confetti_area_.width, confetti_area_.height, scale());
            blit(frame, pixels, confetti_area_.x, confetti_area_.y, clip);
        }
        if (menu_open_) {
            const float t = std::min(1.f, (time_ - menu_opened_) / menu_open_seconds);
            const auto key = std::string(startup_ ? "on" : "off") + std::to_string(menu_highlight_ + 1);
            const int slide = static_cast<int>(std::lround((1.f - ease_out(t)) * menu_slide * scale()));
            blit(frame, art_.menus.at(key), menu_.x, menu_.y + slide, clip, ease_out(t));
        }
        const bool hand = settings_open_ && !menu_open_ && (settings().contains(pointer_) || settings_captured_) &&
                          settings_view_.cursor() == CLAY_WIDGETS_CURSOR_POINTER;
        const auto& cursor = hand ? art_.hand : art_.arrow;
        const auto hotspot = hand ? art_.hand_hotspot : art_.arrow_hotspot;
        blit(frame, cursor, static_cast<int>(std::lround(pointer_.x - hotspot.x)),
             static_cast<int>(std::lround(pointer_.y - hotspot.y)), clip);
    }

    void open_settings() {
        hide_hover();
        settings_open_ = true;
        saved_ = preferences_;
        settings_view_.set_preferences(preferences_);
        settings_view_.reset_focus();
        const float s = scale();
        const auto work = art_.work();
        settings_.width = static_cast<int>(std::lround(ui::settings_width * s));
        settings_.height = static_cast<int>(std::lround(ui::settings_height * s));
        settings_.x = work.x + (work.width - settings_.width) / 2;
        settings_.y = work.y + (work.height - settings_.height) / 2;
        pinned_ = true;
        show_hover();
        // Slide left, then up, so Settings never covers the pinned card.
        if (card_visible_) {
            const int gap = static_cast<int>(std::lround(8 * s));
            const auto overlaps = [&] {
                return settings_.x < card_.right() + gap && settings_.right() > card_.x - gap &&
                       settings_.y < card_.bottom() + gap && settings_.bottom() > card_.y - gap;
            };
            if (overlaps()) {
                if (card_.x - gap - settings_.width >= work.x)
                    settings_.x = card_.x - gap - settings_.width;
                else if (card_.right() + gap + settings_.width <= work.right())
                    settings_.x = card_.right() + gap;
                if (overlaps())
                    settings_.y = std::max(work.y, card_.y - gap - settings_.height);
            }
        }
        pointer_move(pointer_);
        render_settings();
    }

  private:
    const Art& art_;
    ui::Renderer renderer_;
    ui::View widget_view_{ui::Surface::Widget, ui::Renderer::measure_callback, &renderer_};
    ui::View hover_view_{ui::Surface::Hover, ui::Renderer::measure_callback, &renderer_};
    ui::View settings_view_{ui::Surface::Settings, ui::Renderer::measure_callback, &renderer_};
    Usage usage_;
    Preferences preferences_, saved_;
    float time_{};
    Point pointer_{-100, -100};
    bool left_down_{}, widget_captured_{}, settings_captured_{};

    Rect widget_;
    ui::Pixels widget_pixels_;
    bool hovered_{}, pinned_{}, card_visible_{};
    float card_opened_{}, leave_timer_{-1.f};
    Rect card_;
    ui::Pixels card_pixels_;

    bool settings_open_{};
    Rect settings_;
    ClayWidgets_Input settings_input_{};
    ui::Pixels settings_pixels_;

    bool menu_open_{}, startup_{true};
    int menu_highlight_{-1};
    float menu_opened_{};
    Rect menu_;

    ui::Confetti confetti_;
    Rect confetti_area_;

    void apply_preferences(Preferences value) {
        value.normalize();
        preferences_ = value;
        usage_.codex_enabled = value.codex_enabled;
        usage_.claude_enabled = value.claude_enabled;
        widget_view_.set_preferences(value);
        hover_view_.set_preferences(value);
        widget_ = ui::place_widget(value.appearance, art_.taskbar, art_.occupied, scale());
        render_widget();
        if (!value.appearance.hover_enabled)
            hide_hover(true);
        else if (card_visible_ || pinned_)
            show_hover();
    }
    void render_widget() {
        renderer_.set_surface(scale(), widget_view_.text_gamma());
        widget_view_.set_hovered(hovered_);
        const auto frame =
            widget_view_.frame(usage_, {}, static_cast<float>(widget_.width) / scale(),
                               static_cast<float>(widget_.height) / scale());
        widget_pixels_ = renderer_.render(frame.commands, widget_.width, widget_.height, scale(), true);
    }
    void show_hover() {
        if (!preferences_.appearance.hover_enabled || !(hovered_ || pinned_) || menu_open_)
            return;
        const float s = scale();
        renderer_.set_surface(s, hover_view_.text_gamma());
        hover_view_.invalidate_measurements();
        auto logical = ui::hover_size(usage_, preferences_.appearance.hover_text_scale());
        logical.width = widget_.width / s;
        logical.height = hover_view_.hover_height(usage_, logical.width);
        const auto work = art_.work();
        card_.width = widget_.width;
        card_.height = static_cast<int>(std::lround(logical.height * s));
        card_.x = std::max(work.x, std::min(widget_.right() - card_.width, work.right() - card_.width));
        card_.y = std::max(work.y, std::min(widget_.y - card_.height - static_cast<int>(8 * s),
                                            work.bottom() - card_.height));
        if (!card_visible_)
            card_opened_ = time_;
        card_visible_ = true;
        const auto frame = hover_view_.frame(usage_, {}, logical.width, logical.height);
        card_pixels_ = renderer_.render(frame.commands, card_.width, card_.height, s, false);
    }
    // A pinned card stays as the settings preview unless forced; only the widget's
    // highlight goes.
    void hide_hover(bool force = false) {
        leave_timer_ = -1.f;
        hovered_ = false;
        if (pinned_ && !force)
            return;
        card_visible_ = false;
    }
    void close_menu(int choice) {
        menu_open_ = false;
        if (choice == 0)
            open_settings();
        else if (choice == 1)
            startup_ = !startup_;
        if (pinned_ && !card_visible_)
            show_hover();
        pointer_move(pointer_);
    }
    void close_settings(bool save) {
        settings_open_ = false;
        settings_captured_ = false;
        settings_input_ = {};
        pinned_ = false;
        hide_hover();
        if (save)
            saved_ = settings_view_.preferences();
        apply_preferences(saved_);
        pointer_move(pointer_);
    }
    void render_settings() {
        const float s = scale();
        const float w = settings_.width / s, h = settings_.height / s;
        auto input = settings_input_;
        input.deltaTime = frame_time;
        renderer_.set_surface(s, settings_view_.text_gamma());
        auto frame = settings_view_.frame(usage_, input, w, h);
        if (frame.close || frame.save) {
            close_settings(frame.save);
            return;
        }
        if (frame.changed) {
            apply_preferences(settings_view_.preferences());
            auto settle = settings_input_;
            settle.pointerPressed = settle.pointerReleased = false;
            settle.deltaTime = 0.f;
            renderer_.set_surface(s, settings_view_.text_gamma());
            frame = settings_view_.frame(usage_, settle, w, h);
        }
        settings_pixels_ = renderer_.render(frame.commands, settings_.width, settings_.height, s, false);
        ui::round_corners(settings_pixels_, static_cast<int>(std::lround(12 * s)));
    }
};

// Resamples a camera rectangle of the composited screen to the output size with
// a tent filter widened to the zoom, so zoomed-out frames stay free of aliasing.
class Camera {
  public:
    Camera(int width, int height) : width_(width), height_(height), rgb_(static_cast<std::size_t>(width) * height * 3) {}
    const std::vector<unsigned char>& shoot(const ui::Pixels& frame, Box view) {
        const auto columns = taps(view.x, view.w, width_, frame.width);
        const auto rows = taps(view.y, view.h, height_, frame.height);
        const int first_row = rows.front().first;
        const int last_row = rows.back().first + static_cast<int>(rows.back().weights.size());
        // Horizontal pass over the rows the vertical pass will read.
        horizontal_.assign(static_cast<std::size_t>(last_row - first_row) * width_ * 3, 0.f);
        for (int y = first_row; y < last_row; ++y) {
            const auto* line = &frame.data[static_cast<std::size_t>(y) * frame.width];
            float* out = &horizontal_[static_cast<std::size_t>(y - first_row) * width_ * 3];
            for (int x = 0; x < width_; ++x) {
                const auto& tap = columns[static_cast<std::size_t>(x)];
                float r = 0, g = 0, b = 0;
                for (std::size_t i = 0; i < tap.weights.size(); ++i) {
                    const auto p = line[tap.first + static_cast<int>(i)];
                    r += tap.weights[i] * ((p >> 16) & 0xff);
                    g += tap.weights[i] * ((p >> 8) & 0xff);
                    b += tap.weights[i] * (p & 0xff);
                }
                out[x * 3] = r, out[x * 3 + 1] = g, out[x * 3 + 2] = b;
            }
        }
        for (int y = 0; y < height_; ++y) {
            const auto& tap = rows[static_cast<std::size_t>(y)];
            auto* out = &rgb_[static_cast<std::size_t>(y) * width_ * 3];
            for (int x = 0; x < width_ * 3; ++x) {
                float value = 0;
                for (std::size_t i = 0; i < tap.weights.size(); ++i)
                    value += tap.weights[i] *
                             horizontal_[static_cast<std::size_t>(tap.first - first_row + static_cast<int>(i)) *
                                             width_ * 3 + x];
                out[x] = static_cast<unsigned char>(std::clamp(std::lround(value), 0l, 255l));
            }
        }
        return rgb_;
    }

  private:
    struct Tap {
        int first{};
        std::vector<float> weights;
    };
    int width_, height_;
    std::vector<unsigned char> rgb_;
    std::vector<float> horizontal_;
    static std::vector<Tap> taps(float start, float span, int count, int limit) {
        const float ratio = span / count, support = std::max(1.f, ratio);
        std::vector<Tap> result(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            const float center = start + (i + 0.5f) * ratio;
            int lo = static_cast<int>(std::floor(center - support)), hi = static_cast<int>(std::ceil(center + support));
            lo = std::clamp(lo, 0, limit - 1);
            hi = std::clamp(hi, lo + 1, limit);
            auto& tap = result[static_cast<std::size_t>(i)];
            tap.first = lo;
            float total = 0;
            for (int s = lo; s < hi; ++s) {
                const float w = std::max(0.f, 1.f - std::abs((s + 0.5f - center) / support));
                tap.weights.push_back(w);
                total += w;
            }
            if (total <= 0) {
                tap.weights.assign(1, 1.f);
                continue;
            }
            for (auto& w : tap.weights)
                w /= total;
        }
        return result;
    }
};

// Scene scripts: straight-line pointer choreography, each call rendering the
// frames it spans. Positions are in DIPs, as Windows would report at 100%.
class Director {
  public:
    Director(const Art& art, Host& host) : art_(art), host_(host), camera_(output_width, output_height) {
        frame_ = art.desktop;
    }
    float s() const {
        return art_.scale;
    }
    // Layout helpers, in DIPs.
    Point widget(float fx = 0.5f, float fy = 0.5f) const {
        return in(host_.widget(), fx, fy);
    }
    Point card(float fx, float fy) const {
        return in(host_.card(), fx, fy);
    }
    Point element(const char* id, float fx = 0.5f, float fy = 0.5f) const {
        return in(host_.element(id), fx, fy);
    }
    Point menu_item(int index) const {
        return in(host_.menu_item(index), 0.4f, 0.5f);
    }
    // Where a settings slider's thumb sits for `value` in `range`.
    Point slider(const char* id, int value, SettingRange range) const {
        const auto b = host_.element(id);
        const float ratio = static_cast<float>(value - range.min) / static_cast<float>(range.max - range.min);
        return {(b.x + ratio * b.w) / s(), (b.y + b.h / 2) / s()};
    }

    void place(Point p) {
        pointer_ = p;
        host_.pointer_move(scaled(p));
    }
    void look(Box view) {
        view_ = view_from_ = view_to_ = aspect(view);
        pan_frames_ = 0;
    }
    // Starts a camera move that runs alongside whatever comes next.
    void pan(Box view, float seconds) {
        view_from_ = view_;
        view_to_ = aspect(view);
        pan_frames_ = std::max(1, static_cast<int>(std::lround(seconds * fps)));
        pan_frame_ = 0;
    }
    void wait(float seconds) {
        for (int i = frames(seconds); i > 0; --i)
            frame();
    }
    // Lets time pass off camera, for a scene that opens mid-way.
    void skip(float seconds) {
        recording_ = false;
        wait(seconds);
        recording_ = true;
    }
    // A hand-drawn path: eased, with a slight bow, like a real pointer.
    void move(Point to, float seconds) {
        const Point from = pointer_;
        const float dx = to.x - from.x, dy = to.y - from.y;
        const float bow = 0.08f;
        const Point control{(from.x + to.x) / 2 - dy * bow, (from.y + to.y) / 2 + dx * bow};
        const int count = frames(seconds);
        for (int i = 1; i <= count; ++i) {
            const float t = ease_in_out(static_cast<float>(i) / count), u = 1 - t;
            pointer_ = {u * u * from.x + 2 * u * t * control.x + t * t * to.x,
                        u * u * from.y + 2 * u * t * control.y + t * t * to.y};
            host_.pointer_move(scaled(pointer_));
            frame();
        }
    }
    void click() {
        host_.left_press();
        wait(0.09f);
        host_.left_release();
        frame();
    }
    void right_click() {
        wait(0.09f);
        host_.right_click();
        frame();
    }
    // Presses where the pointer is, drags in a straight eased line and releases.
    void drag(Point to, float seconds) {
        host_.left_press();
        wait(0.12f);
        const Point from = pointer_;
        const int count = frames(seconds);
        for (int i = 1; i <= count; ++i) {
            const float t = ease_in_out(static_cast<float>(i) / count);
            pointer_ = {from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t};
            host_.pointer_move(scaled(pointer_));
            frame();
        }
        wait(0.12f);
        host_.left_release();
        frame();
    }
    void frame() {
        if (pan_frames_ > 0 && pan_frame_ < pan_frames_) {
            const float t = ease_in_out(static_cast<float>(++pan_frame_) / pan_frames_);
            view_ = {view_from_.x + (view_to_.x - view_from_.x) * t, view_from_.y + (view_to_.y - view_from_.y) * t,
                     view_from_.w + (view_to_.w - view_from_.w) * t, view_from_.h + (view_to_.h - view_from_.h) * t};
        }
        const Box pixels{view_.x * s(), view_.y * s(), view_.w * s(), view_.h * s()};
        // Only what the camera sees needs compositing.
        const int margin = static_cast<int>(std::ceil(pixels.w / output_width)) + 2;
        Rect clip{static_cast<int>(pixels.x) - margin, static_cast<int>(pixels.y) - margin,
                  static_cast<int>(pixels.w) + 2 * margin + 1, static_cast<int>(pixels.h) + 2 * margin + 1};
        clip.x = std::max(0, clip.x), clip.y = std::max(0, clip.y);
        clip.width = std::min(frame_.width - clip.x, clip.width);
        clip.height = std::min(frame_.height - clip.y, clip.height);
        for (int y = clip.y; y < clip.bottom(); ++y)
            std::copy_n(&art_.desktop.data[static_cast<std::size_t>(y) * frame_.width + clip.x], clip.width,
                        &frame_.data[static_cast<std::size_t>(y) * frame_.width + clip.x]);
        host_.step(frame_, clip);
        if (!recording_)
            return;
        const auto& rgb = camera_.shoot(frame_, pixels);
        std::fwrite(rgb.data(), 1, rgb.size(), stdout);
        ++count_;
    }
    int count() const {
        return count_;
    }

  private:
    const Art& art_;
    Host& host_;
    Camera camera_;
    ui::Pixels frame_;
    Point pointer_{-100, -100};
    Box view_{}, view_from_{}, view_to_{};
    int pan_frames_{}, pan_frame_{}, count_{};
    bool recording_{true};

    Point in(Box b, float fx, float fy) const {
        return {(b.x + b.w * fx) / s(), (b.y + b.h * fy) / s()};
    }
    Point scaled(Point p) const {
        return {p.x * s(), p.y * s()};
    }
    static int frames(float seconds) {
        return std::max(1, static_cast<int>(std::lround(seconds * fps)));
    }
    // Grows the shorter side to the output's aspect ratio around the same centre,
    // then keeps the view on screen.
    Box aspect(Box view) const {
        const float target = static_cast<float>(output_width) / output_height;
        if (view.w / view.h < target) {
            const float w = view.h * target;
            view.x -= (w - view.w) / 2, view.w = w;
        } else {
            const float h = view.w / target;
            view.y -= (h - view.h) / 2, view.h = h;
        }
        const float sw = art_.screen.width / s(), sh = art_.screen.height / s();
        view.w = std::min(view.w, sw), view.h = std::min(view.h, sh);
        view.x = std::clamp(view.x, 0.f, sw - view.w);
        view.y = std::clamp(view.y, 0.f, sh - view.h);
        return view;
    }
};

// ---- scenes

// Close on the taskbar corner: the widget, the tray and room above for the card.
Box taskbar_view(Director& d) {
    const auto w = d.widget(0, 0);
    return {w.x - 380, 380, 900, 520};
}
// Settings beside the pinned card, down to the taskbar.
Box settings_view(Director& d, Host& host) {
    const float s = d.s();
    const auto set = host.settings();
    return {set.x / s - 24, set.y / s - 24, 1440 - (set.x / s - 24), 900 - (set.y / s - 24)};
}

void hover_scene(Director& d, Host&) {
    d.look(taskbar_view(d));
    d.place({700, 560});
    d.wait(0.6f);
    d.move(d.widget(0.42f, 0.55f), 1.1f);
    d.wait(1.5f);
    d.move(d.card(0.6f, 0.42f), 0.7f);
    d.wait(0.5f);
    d.move(d.card(0.4f, 0.78f), 0.8f);
    d.wait(1.1f);
    d.move(d.widget(0.55f, 0.5f), 0.5f);
    d.wait(0.6f);
    d.move({760, 520}, 0.8f);
    d.wait(1.2f);
}

void menu_scene(Director& d, Host& host) {
    d.look(taskbar_view(d));
    d.place({760, 600});
    d.wait(0.4f);
    d.move(d.widget(0.5f, 0.55f), 1.0f);
    d.wait(0.8f);
    d.right_click();
    d.wait(0.7f);
    d.move(d.menu_item(1), 0.5f);
    d.wait(0.6f);
    d.move(d.menu_item(0), 0.35f);
    d.wait(0.5f);
    d.click();
    d.pan(settings_view(d, host), 0.9f);
    d.wait(2.2f);
}

void settings_scene(Director& d, Host& host) {
    d.look(taskbar_view(d));
    d.place({780, 560});
    d.wait(0.4f);
    d.move(d.widget(0.5f, 0.5f), 1.0f);
    d.wait(0.7f);
    d.click();
    d.pan(settings_view(d, host), 0.9f);
    d.wait(1.0f);
    const auto text = preference_limits::text_percent;
    d.move(d.slider("TextSizeSlider", 100, text), 0.8f);
    // Past 120% the rows go side by side; stay stacked so the growth reads.
    d.drag(d.slider("TextSizeSlider", 115, text), 1.0f);
    d.wait(0.8f);
    d.move(d.element("BoldTaskbar"), 0.7f);
    d.click();
    d.wait(0.9f);
    d.click();
    d.wait(0.6f);
    d.move(d.element("NavHover", 0.3f, 0.5f), 0.7f);
    d.click();
    d.wait(0.6f);
    d.move(d.slider("HoverTextSizeSlider", 100, text), 0.7f);
    d.drag(d.slider("HoverTextSizeSlider", 115, text), 0.9f);
    d.wait(0.9f);
    d.move(d.element("SaveSettings", 0.45f, 0.5f), 0.8f);
    d.click();
    d.pan(taskbar_view(d), 0.9f);
    d.move({760, 540}, 0.9f);
    d.wait(1.2f);
}

// Codex and Claude switched off and on in turn from the Providers page: the
// taskbar and the pinned card change layout with them.
void providers_scene(Director& d, Host& host, bool close_up) {
    host.set_page(ui::SettingsPage::Providers);
    d.place(d.widget());
    host.open_settings();
    d.place({640, 420});
    d.look(close_up ? taskbar_view(d) : settings_view(d, host));
    d.skip(0.6f);
    d.wait(0.9f);
    d.move(d.element("EnableCodex"), 0.9f);
    d.click();
    d.wait(2.2f);
    d.click();
    d.wait(0.5f);
    d.move(d.element("EnableClaude"), 0.8f);
    d.click();
    d.wait(2.2f);
    d.click();
    d.wait(1.6f);
}

void reset_scene(Director& d, Host& host) {
    d.look(taskbar_view(d));
    d.place({300, 300});
    auto& session = host.usage().codex.windows.front();
    session.remaining = 4;
    host.data_changed();
    d.wait(1.2f);
    session.remaining = 100;
    session.resets_at += 5 * 3600;
    host.data_changed();
    host.celebrate();
    d.wait(3.6f);
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: demo_frames <art directory> <scene>\n";
        return 2;
    }
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    try {
        const Art art(argv[1]);
        Host host(art);
        Director director(art, host);
        const std::string scene = argv[2];
        const std::map<std::string, std::function<void()>> scenes{
            {"hover", [&] { hover_scene(director, host); }},
            {"menu", [&] { menu_scene(director, host); }},
            {"settings", [&] { settings_scene(director, host); }},
            {"providers", [&] { providers_scene(director, host, false); }},
            {"modes", [&] { providers_scene(director, host, true); }},
            {"reset", [&] { reset_scene(director, host); }},
        };
        const auto found = scenes.find(scene);
        if (found == scenes.end()) {
            std::cerr << "unknown scene: " << scene << '\n';
            return 2;
        }
        std::printf("{\"width\": %d, \"height\": %d, \"fps\": %d}\n", output_width, output_height, fps);
        std::fflush(stdout);
        found->second();
        std::fflush(stdout);
        std::cerr << scene << ": " << director.count() << " frames\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
