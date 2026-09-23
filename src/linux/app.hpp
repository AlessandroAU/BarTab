#pragma once
#include "core/settings_edit.hpp"
#include "host/providers.hpp"
#include "linux/x11.hpp"
#include "ui/raylib_renderer.hpp"
#include <chrono>
#include <filesystem>
#include <optional>

// The Linux desktop host. The widget floats above other windows wherever the
// user drags it, rather than sitting in a taskbar; the hover card, settings and
// confetti work as on Windows. Everything goes through X11 (XWayland on Wayland
// desktops), which lets a program place its own windows and keep them on top.
namespace usage::linux_host {
using Clock = std::chrono::steady_clock;

class App {
  public:
    // `smoke` runs the offline self-test and exits. With `mock`, providers are
    // the debug build's mock endpoints and settings go to files of their own.
    App(bool smoke, std::shared_ptr<host::MockProviders> mock);
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    // Returns the process exit code.
    int run();
    // Deletes the saved settings and widget position, for --reset; the next
    // start uses the defaults. Returns false when a file could not be removed.
    static bool reset_configuration(bool mock);

  private:
    // Declared first so it outlives the views that measure with it, and so the
    // X connection closes last.
    x11::Connection x_;
    ui::Renderer renderer_;
    ui::View widget_view_{ui::Surface::Widget, ui::Renderer::measure_callback, &renderer_};
    ui::View hover_view_{ui::Surface::Hover, ui::Renderer::measure_callback, &renderer_};
    ui::View details_view_{ui::Surface::Details, ui::Renderer::measure_callback, &renderer_};
    ui::View menu_view_{ui::Surface::Menu, ui::Renderer::measure_callback, &renderer_};
    Usage usage_;
    std::shared_ptr<host::MockProviders> mock_;
    host::ProviderSession providers_;
    std::size_t mock_preset_{};
    const bool smoke_;
    const bool animations_allowed_;
    bool running_{true};
    int exit_code_{};

    Preferences preferences_;
    SettingsEdit settings_edit_;
    std::filesystem::path settings_path_, position_path_;
    // The widget's saved top-left corner; nullopt until the user places it.
    std::optional<std::pair<int, int>> saved_position_;
    // A widget dropped onto a desktop panel docks in it: it fills the panel's
    // height and follows it, rather than taking the height preference.
    enum class Dock { None, Top, Bottom };
    Dock dock_{Dock::None};
    // Reset all settings also forgets the widget's place, once saved.
    bool reset_place_on_save_{};
    bool position_dirty_{};

    float scale_{1.f};
    x11::WindowId widget_{}, hover_{}, popup_{}, confetti_window_{}, menu_{};
    Rect widget_bounds_{};
    ui::Pixels widget_pixels_, hover_pixels_, details_pixels_, menu_pixels_;
    // The pointer as the menu last saw it, carried between its frames.
    ClayWidgets_Input menu_pointer_{};

    // A left press on the widget becomes a click on release, or a drag once the
    // pointer travels; the widget then follows it, anywhere on the monitor,
    // including over a desktop panel.
    std::optional<x11::Event> press_;
    bool dragging_{};

    bool hovered_{};
    // Settings keep the hover card open beside the widget as a live preview.
    bool hover_pinned_{};
    // The card opens by growing out of the widget's side of it while fading in;
    // progress is eased 0..1, 1 once open.
    float hover_progress_{1.f};
    bool hover_grows_up_{true};
    Clock::time_point hover_opened_{};
    // Leaving the widget or card closes the card after a short grace period.
    std::optional<Clock::time_point> hover_check_at_;
    // Whether the pointer is over each, from crossing events. XWayland cannot
    // report the pointer's position once it is over a Wayland window, so
    // querying it at the end of the grace period can claim it never left.
    bool pointer_over_widget_{}, pointer_over_card_{};
    Clock::time_point hover_refresh_at_{};

    bool settings_mode_{};
    ClayWidgets_Input details_pointer_{};
    Clock::time_point details_rendered_{}, details_settle_until_{};
    float details_scale_{};

    ui::Confetti confetti_;
    Rect confetti_bounds_{};
    Clock::time_point confetti_frame_{};

    Clock::time_point next_tick_{};
    unsigned ticks_{};

    void load_settings();
    bool save_settings(Preferences value);
    void apply_preferences(Preferences value);
    void apply_view_preferences();
    void load_position();
    void save_position();
    // Back to the first-run spot, and the position file removed.
    void forget_position();
    bool reload_ui_font();

    // Runs the event loop until `until` or until the app quits.
    void loop(Clock::time_point until);
    void handle(const x11::Event& event);
    void widget_event(const x11::Event& event);
    void hover_event(const x11::Event& event);
    void details_event(const x11::Event& event);
    void tick();
    bool animating() const;
    void frame();
    void update_usage();
    void cycle_mock();

    // Where the widget belongs: its saved spot, kept on a monitor, and filling
    // its panel when docked.
    Rect widget_target();
    // The strip a top or bottom panel takes on `monitor`: the gap between the
    // monitor's edge and its work area. Empty when there is no such panel.
    static Rect panel_strip(const x11::Monitor& monitor, Dock dock);
    // Where a docked widget sits in that strip: inset from its edges like the
    // panel's own items, so it reads as part of the panel. Empty without one.
    Rect dock_slot(const x11::Monitor& monitor, Dock dock) const;
    // Undocking returns the widget to the default floating height.
    void reset_floating_height();
    void place_widget();
    // Moves a dragged widget to follow the pointer at (root_x, root_y).
    void drag_widget_to(int root_x, int root_y);
    void paint_widget();

    void show_hover();
    void hide_hover(bool force = false);
    void pin_hover();
    void unpin_hover();
    void present_hover();
    bool animate_hover();
    void check_hover_leave();

    void open_details(bool settings = false);
    void close_details();
    void render_details(ClayWidgets_Input input);
    void render_details_frame(ClayWidgets_Input input);
    bool animate_details();
    void present_details();
    void avoid_hover(Rect& bounds, const Rect& work);

    // The context menu, opened at the pointer by a right click on the widget.
    void show_menu();
    void close_menu();
    void menu_event(const x11::Event& event);
    void render_menu(ClayWidgets_Input input);

    void celebrate();
    bool animate_confetti();

    void run_smoke_test();
};
} // namespace usage::linux_host
