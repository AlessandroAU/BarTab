// Regenerates the README images offscreen: no taskbar, no visible window and no
// provider processes. Every surface is rendered from fixed data so reruns on the
// same machine produce identical PNGs.
#include "ui/raylib_renderer.hpp"
#include "ui/views.hpp"
#include "windows/platform.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

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
               float width, float height, std::uint32_t backdrop, float scale = 2.f, bool hovered = false, bool light = false) {
        Usage copy = data;
        ui::View view(surface, ui::Renderer::measure_callback, &renderer_);
        view.set_preferences(preferences);
        view.set_reference_time(session_reset - 8040);
        view.set_hovered(hovered);
        view.set_system_light(light);
        renderer_.set_surface(scale, view.text_gamma());
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

  private:
    ui::Renderer renderer_;
    std::filesystem::path directory_;
};
} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path directory = argc > 1 ? argv[1] : "docs/images";
    try {
        Shooter shooter(directory);
        if (!shooter.renderer().load_font_data(1, windows::windows_ui_font()))
            throw std::runtime_error("Could not read the Windows UI font");

        const auto data = demo_usage();
        Preferences preferences;
        preferences.normalize();
        const auto& appearance = preferences.appearance;
        bool ok = true;

        const auto widget = ui::widget_size(data, appearance);
        ok = shooter.shoot("taskbar-widget", ui::Surface::Widget, data, preferences, widget.width,
                           widget.height, taskbar_backdrop) &&
             ok;

        // The same taskbar row with Codex switched off, showing Claude's split bars.
        Usage claude_only = data;
        claude_only.codex_enabled = false;
        Preferences claude_preferences = preferences;
        claude_preferences.codex_enabled = false;
        const auto claude_widget = ui::widget_size(claude_only, claude_preferences.appearance);
        ok = shooter.shoot("taskbar-widget-claude", ui::Surface::Widget, claude_only, claude_preferences,
                           claude_widget.width, claude_widget.height, taskbar_backdrop) &&
             ok;

        auto hover = ui::hover_size(data, appearance.hover_text_percent);
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
        ok = shooter.shoot("settings", ui::Surface::Settings, data, preferences, 1120, 700, page_backdrop,
                           1.5f) &&
             ok;

        ok = shooter.shoot("settings-light", ui::Surface::Settings, data, preferences, 1120, 700,
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
