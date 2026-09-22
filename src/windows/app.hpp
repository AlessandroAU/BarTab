#pragma once
#include "windows/taskbar.hpp"
#include "core/settings_edit.hpp"
#include "windows/providers.hpp"
#include "ui/raylib_renderer.hpp"
#include <shellapi.h>
#include <filesystem>

namespace usage::windows {
inline constexpr UINT tray_message = WM_APP + 1;
inline constexpr wchar_t controller_class[] = L"UsageTracker.Controller.Cpp";
inline constexpr wchar_t widget_class[] = L"UsageTracker.Widget.Cpp";
inline constexpr wchar_t popup_class[] = L"UsageTracker.Popup.Cpp";
inline constexpr wchar_t hover_class[] = L"UsageTracker.Hover.Cpp";

class App {
  public:
    explicit App(bool smoke, bool live_test = false);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    int run();

  private:
    Usage usage_;
    std::unique_ptr<UsageReader> codex_, claude_;
    ui::Renderer renderer_;
    ui::View widget_view_{ui::Surface::Widget, ui::Renderer::measure_callback, &renderer_};
    ui::View details_view_{ui::Surface::Details, ui::Renderer::measure_callback, &renderer_};
    ui::View hover_view_{ui::Surface::Hover, ui::Renderer::measure_callback, &renderer_};
    ui::Pixels hover_pixels_;
    bool hovered_{};
    // Settings keep the hover card open beside the taskbar as a live preview.
    bool hover_pinned_{};
    bool settings_mode_{};
    Preferences preferences_;
    SettingsEdit settings_edit_;
    std::filesystem::path settings_path_;
    ui::Pixels details_pixels_;
    ClayWidgets_Input details_pointer_{};
    // The popup is the only animated surface. Input starts a short frame loop
    // that keeps rendering with real deltaTime until the motion has settled.
    const bool animations_allowed_;
    std::chrono::steady_clock::time_point details_rendered_{};
    std::chrono::steady_clock::time_point details_settle_until_{};
    unsigned widget_frames_{}, details_frames_{};
    float widget_scale_{}, details_scale_{};
    TaskbarReader reader_;
    HWND controller_{}, widget_{}, popup_{}, hover_{};
    NOTIFYICONDATAW tray_{};
    UINT taskbar_created_{};
    Rect widget_bounds_{};
    std::wstring status_{L"Waiting for the taskbar."};
    bool smoke_{};
    bool demo_mode_{};
    bool live_test_{};
    const std::chrono::steady_clock::time_point started_{std::chrono::steady_clock::now()};

    void load_settings();
    void update_system_font();
    void update_animation_preference();
    bool save_settings(Preferences value);
    void begin_settings_preview();
    void preview_settings(Preferences value);
    void cancel_settings_preview();
    void apply_view_preferences();
    void apply_providers();
    void apply_preferences(Preferences value);
    void detect_providers();

    void register_class(const wchar_t* name, WNDPROC procedure);
    static App* instance(HWND window, UINT message, LPARAM parameter);
    static LRESULT CALLBACK controller_proc(HWND window, UINT message, WPARAM w, LPARAM l);
    static LRESULT CALLBACK widget_proc(HWND window, UINT message, WPARAM w, LPARAM l);
    static LRESULT CALLBACK hover_proc(HWND window, UINT message, WPARAM w, LPARAM l);
    // A pinned card ignores pointer-driven hides unless forced.
    void hide_hover(bool force = false);
    void show_hover();
    void pin_hover();
    void unpin_hover();
    void avoid_hover(int& x, int& y, int width, int height, const RECT& work) const;
    static LRESULT CALLBACK popup_proc(HWND window, UINT message, WPARAM w, LPARAM l);
    void set_status(const std::wstring& value);
    void add_tray();
    void update_tooltip();
    void update_usage();
    void reset_widget();
    void hide_widget();
    void tick();
    void paint_widget(HWND window);
    float popup_scale() const;
    void close_details();
    void render_details(ClayWidgets_Input input);
    void render_details_frame(ClayWidgets_Input input);
    void animate_details();
    void paint_details(HWND window);
    static void paint_pixels(HWND window, const ui::Pixels& pixels);
    void details_event(HWND window, UINT message, WPARAM w, LPARAM l);
    void show_menu();
    void open_details(bool settings = false);
    void finish_smoke_test();
    void finish_live_test();
};
} // namespace usage::windows
