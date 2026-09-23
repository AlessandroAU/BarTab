#pragma once
#include "windows/taskbar.hpp"
#include "windows/frame_clock.hpp"
#include "core/settings_edit.hpp"
#include "host/providers.hpp"
#include "ui/raylib_renderer.hpp"
#include <shellapi.h>
#include <filesystem>
#include <functional>

namespace usage::windows {
inline constexpr UINT tray_message = WM_APP + 1;
// One per display refresh from the FrameClock while anything animates.
inline constexpr UINT frame_message = WM_APP + 2;
inline constexpr wchar_t controller_class[] = L"UsageTracker.Controller.Cpp";
inline constexpr wchar_t popup_class[] = L"UsageTracker.Popup.Cpp";
inline constexpr wchar_t hover_class[] = L"UsageTracker.Hover.Cpp";
inline constexpr wchar_t confetti_class[] = L"UsageTracker.Confetti.Cpp";

// A widget embedded in one taskbar, with its screen bounds.
struct TaskbarWidget {
    HWND taskbar{}, window{};
    Rect bounds{};
};

class App {
  public:
    // With `mock`, the providers are the debug build's mock endpoints rather
    // than the installed CLIs, and settings go to a file of their own.
    explicit App(bool smoke, bool live_test = false, std::shared_ptr<host::MockProviders> mock = nullptr);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    int run();
    // Where settings are saved; the debug build keeps its own file. --reset
    // deletes it before starting, so the app starts from the defaults.
    static std::filesystem::path settings_file(bool mock);
    // The mock scenario changed: detect and read the providers again.
    void mock_changed();
    // Adds a tray menu entry that reopens the debug build's mock panel.
    void set_mock_panel(std::function<void()> open) {
        open_mock_panel_ = std::move(open);
    }
    // Bursts confetti out of the widget, as when a usage window resets.
    void celebrate();

  private:
    Usage usage_;
    std::shared_ptr<host::MockProviders> mock_;
    host::ProviderSession providers_;
    std::function<void()> open_mock_panel_;
    ui::Renderer renderer_;
    ui::View widget_view_{ui::Surface::Widget, ui::Renderer::measure_callback, &renderer_};
    ui::View details_view_{ui::Surface::Details, ui::Renderer::measure_callback, &renderer_};
    ui::View hover_view_{ui::Surface::Hover, ui::Renderer::measure_callback, &renderer_};
    // While the native context menu runs its modal loop; the hover card stays hidden.
    bool menu_open_{};
    ui::Pixels hover_pixels_;
    bool hovered_{};
    // Settings keep the hover card open beside the taskbar as a live preview.
    bool hover_pinned_{};
    // The card opens by growing out of the taskbar's side of it, content riding
    // the moving edge, while fading in; progress is eased 0..1, 1 once open.
    float hover_progress_{1.f};
    bool hover_grows_up_{true};
    int hover_offset_{};
    std::chrono::steady_clock::time_point hover_opened_{};
    bool settings_mode_{};
    Preferences preferences_;
    SettingsEdit settings_edit_;
    std::filesystem::path settings_path_;
    ui::Pixels details_pixels_;
    ClayWidgets_Input details_pointer_{};
    // In the popup, input starts a short frame loop that keeps rendering with
    // real deltaTime until the motion has settled. The hover card's opening
    // moves pixels rather than re-rendering, and confetti has its own window.
    // All three step on the frame clock, once per display refresh.
    const bool animations_allowed_;
    std::unique_ptr<FrameClock> frames_;
    int animations_logged_{-1};
    std::chrono::steady_clock::time_point details_rendered_{};
    std::chrono::steady_clock::time_point details_settle_until_{};
    unsigned widget_frames_{}, details_frames_{};
    float widget_scale_{}, details_scale_{};
    TaskbarReader reader_;
    // A click-through overlay above the widget that lives only for the burst.
    ui::Confetti confetti_;
    std::chrono::steady_clock::time_point confetti_frame_{};
    HWND controller_{}, popup_{}, hover_{}, confetti_window_{};
    // The primary taskbar's widget, and one per other monitor when the
    // preference asks for it. The hover card, the settings window and confetti
    // anchor to whichever the pointer last used.
    TaskbarWidget primary_;
    std::vector<TaskbarWidget> secondary_;
    HWND active_widget_{};
    NOTIFYICONDATAW tray_{};
    UINT taskbar_created_{};
    std::wstring status_{L"Waiting for the taskbar."};
    bool smoke_{};
    bool demo_mode_{};
    bool live_test_{};
    const std::chrono::steady_clock::time_point started_{std::chrono::steady_clock::now()};

    void load_settings();
    void update_system_font();
    bool reload_ui_font();
    void update_animation_preference();
    bool save_settings(Preferences value);
    void begin_settings_preview();
    void preview_settings(Preferences value);
    void cancel_settings_preview();
    void apply_view_preferences();
    void apply_providers();
    void apply_preferences(Preferences value);

    void register_class(const wchar_t* name, WNDPROC procedure);
    static App* instance(HWND window, UINT message, LPARAM parameter);
    static LRESULT CALLBACK controller_proc(HWND window, UINT message, WPARAM w, LPARAM l);
    static LRESULT CALLBACK widget_proc(HWND window, UINT message, WPARAM w, LPARAM l);
    static LRESULT CALLBACK hover_proc(HWND window, UINT message, WPARAM w, LPARAM l);
    // A pinned card ignores pointer-driven hides unless forced.
    void hide_hover(bool force = false);
    void show_hover();
    void shape_hover();
    // Each animate_* steps one frame and returns whether it is still moving.
    bool animate_hover();
    void pin_hover();
    void unpin_hover();
    void avoid_hover(int& x, int& y, int width, int height, const RECT& work) const;
    static LRESULT CALLBACK popup_proc(HWND window, UINT message, WPARAM w, LPARAM l);
    static LRESULT CALLBACK confetti_proc(HWND window, UINT message, WPARAM w, LPARAM l);
    bool animate_confetti();
    void frame();
    // Hands per-pixel-alpha pixels to a layered window.
    static void present_layered(HWND window, const ui::Pixels& pixels);
    void set_status(const std::wstring& value);
    void add_tray();
    void update_tooltip();
    void update_usage();
    void reset_widget();
    void hide_widget();
    enum class Placement { Shown, NoSpace, Failed };
    // Creates or moves one taskbar's widget for its latest snapshot.
    Placement place(TaskbarWidget& widget, const Snapshot& snapshot);
    void sync_secondary(const std::vector<Snapshot>& snapshots);
    const TaskbarWidget& active() const;
    void invalidate_widgets() const;
    void tick();
    void paint_widget(HWND window);
    float popup_scale() const;
    void close_details();
    void render_details(ClayWidgets_Input input);
    void render_details_frame(ClayWidgets_Input input);
    void set_details_cursor() const;
    bool animate_details();
    void paint_details(HWND window);
    // `y` shifts the pixels down; the window clips whatever falls outside.
    static void paint_pixels(HWND window, const ui::Pixels& pixels, int y = 0);
    void details_event(HWND window, UINT message, WPARAM w, LPARAM l);
    void show_menu();
    void choose_menu(ui::Frame::MenuChoice choice);
    void open_details(bool settings = false);
    void finish_smoke_test();
    void finish_live_test();
};
} // namespace usage::windows
