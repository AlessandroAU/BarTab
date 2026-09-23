// Regenerates the README images offscreen: no taskbar, no visible window and no
// provider processes. Every surface is rendered from fixed data so reruns on the
// same machine produce identical PNGs.
#include "ui/raylib_renderer.hpp"
#include "ui/views.hpp"
#include "host/platform.hpp"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace {
using namespace usage;

// Windows 11's dark taskbar, so the embedded widget is shown in context.
constexpr std::uint32_t taskbar_backdrop = 0x1F1F1F;
// A neutral page backdrop behind the rounded popup and hover corners.
constexpr std::uint32_t page_backdrop = 0x14181F;

// Fixed instants (UTC) keep the reset labels stable between runs.
constexpr std::int64_t weekly_reset = 1790583271;
constexpr std::int64_t fable_reset = 1790669671;
constexpr std::int64_t session_reset = 1790496871;

Usage demo_usage() {
    Usage data;
    data.live = true;
    data.codex.installed = true;
    data.codex.plan = "ChatGPT Pro";
    data.codex.credit_balance = "12.5";
    data.codex.available_resets = 2;
    data.codex.executable_path = "C:/Users/demo/AppData/Local/Codex/codex.exe";
    data.codex.windows = {{"5 hour", 62, session_reset}, {"Weekly", 81, weekly_reset}};
    data.codex.updated = session_reset - 8100;
    data.claude.installed = true;
    data.claude.plan = "Claude Max";
    data.claude.executable_path = "C:/Users/demo/AppData/Local/Claude/claude.exe";
    data.claude.windows = {
        {"5 hour", 97, session_reset}, {"Weekly", 44, weekly_reset}, {"Fable weekly", 23, fable_reset}};
    data.claude.updated = session_reset - 8100;
    return data;
}

class Shooter {
  public:
    explicit Shooter(std::filesystem::path directory) : directory_(std::move(directory)) {
        std::filesystem::create_directories(directory_);
    }
    // Renders one surface at `scale` for a crisp image on high-density displays.
    bool shoot(const char* name, ui::Surface surface, const Usage& data, const Preferences& preferences,
               float width, float height, std::uint32_t backdrop, float scale = 2.f, bool hovered = false, bool light = false,
               float gamma_override = 0.f) {
        Usage copy = data;
        ui::View view(surface, ui::Renderer::measure_callback, &renderer_);
        view.set_preferences(preferences);
        view.set_reference_time(session_reset - 8040);
        view.set_hovered(hovered);
        view.set_system_light(light);
        view.set_settings_page(page_);
        renderer_.set_surface(scale, gamma_override > 0.f ? gamma_override : view.text_gamma());
        view.invalidate_measurements();
        // Two passes: widget state such as scroll extents settles on the second.
        view.frame(copy, {}, width, height);
        const auto frame = view.frame(copy, {}, width, height);
        const auto pixels =
            renderer_.render(frame.commands, static_cast<int>(width * scale),
                             static_cast<int>(height * scale), scale, surface == ui::Surface::Widget);
        const auto path = directory_ / (std::string(name) + ".png");
        if (!ui::Renderer::save_png(pixels, path, backdrop)) {
            std::cerr << "FAIL: could not write " << path.string() << '\n';
            return false;
        }
        std::cout << "wrote " << path.filename().string() << "  " << pixels.width << "x" << pixels.height
                  << '\n';
        return true;
    }
    ui::Renderer& renderer() {
        return renderer_;
    }
    // Which settings page later Settings shots open on.
    void set_page(ui::SettingsPage page) {
        page_ = page;
    }

  private:
    ui::Renderer renderer_;
    std::filesystem::path directory_;
    ui::SettingsPage page_{ui::SettingsPage::Taskbar};
};
} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path directory = argc > 1 ? argv[1] : "docs/images";
    try {
        Shooter shooter(directory);
        auto regular = host::ui_font(false), bold = host::ui_font(true);
        if (!shooter.renderer().load_font_data(ui::regular_font, std::move(regular.bytes),
                                               regular.face_index) ||
            !shooter.renderer().load_font_data(ui::bold_font, std::move(bold.bytes), bold.face_index))
            throw std::runtime_error("Could not read the system UI font");

        const auto data = demo_usage();
        Preferences preferences;
        preferences.normalize();
        const auto& appearance = preferences.appearance;
        bool ok = true;

        // --pages: every settings page, dark and light, for reviewing the panel.
        if (argc > 2 && std::string(argv[2]) == "--pages") {
            const std::pair<const char*, ui::SettingsPage> pages[] = {{"taskbar", ui::SettingsPage::Taskbar},
                                                                      {"hover", ui::SettingsPage::Hover},
                                                                      {"providers", ui::SettingsPage::Providers},
                                                                      {"general", ui::SettingsPage::General}};
            for (const auto& [name, page] : pages) {
                shooter.set_page(page);
                ok = shooter.shoot((std::string("settings-") + name).c_str(), ui::Surface::Settings, data,
                                   preferences, ui::settings_width, ui::settings_height, page_backdrop, 1.f) && ok;
                ok = shooter.shoot((std::string("settings-") + name + "-light").c_str(), ui::Surface::Settings,
                                   data, preferences, ui::settings_width, ui::settings_height, 0xF3F3F3, 1.f, false, true) && ok;
            }
            return ok ? 0 : 1;
        }

        // --taskbar: every taskbar layout at 1x, at the smallest and the default
        // text size, for checking pixel alignment as it lands on a 100% display.
        if (argc > 2 && std::string(argv[2]) == "--taskbar") {
            for (const int percent : {preference_limits::text_percent.min, preferences.appearance.text_percent}) {
                Preferences prefs = preferences;
                prefs.appearance.text_percent = percent;
                const auto tag = "-" + std::to_string(percent);
                const auto size = ui::widget_size(prefs.appearance);
                ok = shooter.shoot(("taskbar-both" + tag).c_str(), ui::Surface::Widget, data, prefs, size.width,
                                   size.height, taskbar_backdrop, 1.f) && ok;
                Usage codex_only = data;
                codex_only.claude_enabled = false;
                prefs.claude_enabled = false;
                ok = shooter.shoot(("taskbar-codex" + tag).c_str(), ui::Surface::Widget, codex_only, prefs,
                                   size.width, size.height, taskbar_backdrop, 1.f) && ok;
                codex_only.codex.windows.erase(codex_only.codex.windows.begin());
                ok = shooter.shoot(("taskbar-codex-weekly" + tag).c_str(), ui::Surface::Widget, codex_only, prefs,
                                   size.width, size.height, taskbar_backdrop, 1.f) && ok;
                Usage claude_only = data;
                claude_only.codex_enabled = false;
                prefs.claude_enabled = true;
                prefs.codex_enabled = false;
                ok = shooter.shoot(("taskbar-claude" + tag).c_str(), ui::Surface::Widget, claude_only, prefs,
                                   size.width, size.height, taskbar_backdrop, 1.f) && ok;
            }
            return ok ? 0 : 1;
        }

        // --sweep: every surface at 1x across text-gamma values, with default
        // and enlarged/bold text, for comparing curves side by side.
        if (argc > 2 && std::string(argv[2]) == "--sweep") {
            Preferences large = preferences;
            large.appearance.text_percent = 110;
            large.appearance.hover_text_percent = 110;
            large.appearance.bold_taskbar = large.appearance.bold_hover = large.appearance.bold_settings = true;
            large.appearance.widget_width = 225;
            large.normalize();
            for (const float gamma : {1.0f, 1.3f, 1.6f}) {
                const auto tag = std::to_string(static_cast<int>(std::lround(gamma * 10)));
                for (const auto& [label, prefs] : {std::pair{"default", preferences}, std::pair{"large", large}}) {
                    const auto size = ui::widget_size(prefs.appearance);
                    auto hover_size = ui::hover_size(data, prefs.appearance.hover_text_scale());
                    hover_size.width = size.width;
                    ui::View layout(ui::Surface::Hover, ui::Renderer::measure_callback, &shooter.renderer());
                    layout.set_preferences(prefs);
                    layout.set_reference_time(session_reset - 8040);
                    shooter.renderer().set_surface(1.f, gamma);
                    auto hover_data = data;
                    hover_size.height = layout.hover_height(hover_data, hover_size.width);
                    const std::string base = std::string(label) + "-g" + tag;
                    ok = shooter.shoot(("widget-" + base).c_str(), ui::Surface::Widget, data, prefs, size.width,
                                       size.height, 0x482626, 1.f, false, false, gamma) && ok;
                    ok = shooter.shoot(("hover-dark-" + base).c_str(), ui::Surface::Hover, data, prefs,
                                       hover_size.width, hover_size.height, page_backdrop, 1.f, false, false,
                                       gamma) && ok;
                    ok = shooter.shoot(("hover-light-" + base).c_str(), ui::Surface::Hover, data, prefs,
                                       hover_size.width, hover_size.height, 0xF3F3F3, 1.f, false, true,
                                       1.f / gamma) && ok;
                    ok = shooter.shoot(("settings-light-" + base).c_str(), ui::Surface::Settings, data, prefs,
                                       ui::settings_width, ui::settings_height, 0xF3F3F3, 1.f, false, true, 1.f / gamma) && ok;
                }
            }
            return ok ? 0 : 1;
        }

        const auto widget = ui::widget_size(appearance);
        ok = shooter.shoot("taskbar-widget", ui::Surface::Widget, data, preferences, widget.width,
                           widget.height, taskbar_backdrop) &&
             ok;

        // The same taskbar row with Codex switched off, showing Claude's split bars.
        Usage claude_only = data;
        claude_only.codex_enabled = false;
        Preferences claude_preferences = preferences;
        claude_preferences.codex_enabled = false;
        const auto claude_widget = ui::widget_size(claude_preferences.appearance);
        ok = shooter.shoot("taskbar-widget-claude", ui::Surface::Widget, claude_only, claude_preferences,
                           claude_widget.width, claude_widget.height, taskbar_backdrop) &&
             ok;

        Usage codex_only = data;
        codex_only.claude_enabled = false;
        Preferences codex_preferences = preferences;
        codex_preferences.claude_enabled = false;
        ok = shooter.shoot("taskbar-widget-codex-split", ui::Surface::Widget, codex_only,
                           codex_preferences, 400, widget_height, taskbar_backdrop) && ok;
        ok = shooter.shoot("taskbar-widget-codex-split-narrow", ui::Surface::Widget, codex_only,
                           codex_preferences, 150, widget_height, taskbar_backdrop) && ok;

        codex_only.codex.windows.erase(codex_only.codex.windows.begin());
        ok = shooter.shoot("taskbar-widget-codex-single", ui::Surface::Widget, codex_only,
                           codex_preferences, 208, widget_height, taskbar_backdrop) && ok;

        auto hover = ui::hover_size(data, appearance.hover_text_scale());
        hover.width = widget.width;
        ui::View hover_layout(ui::Surface::Hover, ui::Renderer::measure_callback, &shooter.renderer());
        hover_layout.set_preferences(preferences);
        hover_layout.set_reference_time(session_reset - 8040);
        shooter.renderer().set_surface(2.f, hover_layout.text_gamma());
        auto hover_data = data;
        hover.height = hover_layout.hover_height(hover_data, hover.width);
        ok = shooter.shoot("hover-card", ui::Surface::Hover, data, preferences, hover.width, hover.height,
                           page_backdrop, 2.f, true) &&
             ok;

        // Matches the popup size the app requests for the unified panel.
        ok = shooter.shoot("settings", ui::Surface::Settings, data, preferences, ui::settings_width, ui::settings_height, page_backdrop,
                           1.5f) &&
             ok;

        ok = shooter.shoot("settings-light", ui::Surface::Settings, data, preferences, ui::settings_width, ui::settings_height,
                           0xF3F3F3, 1.5f, false, true) && ok;
        ok = shooter.shoot("hover-card-light", ui::Surface::Hover, data, preferences, hover.width,
                           hover.height, 0xF3F3F3, 2.f, false, true) && ok;

        if (!ok)
            return 1;
        std::cout << "PASS: screenshots written to " << directory.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
