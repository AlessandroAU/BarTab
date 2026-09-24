#include "windows/app.hpp"
#include "windows/platform.hpp"
#include "windows/resource.h"
#include "host/startup.hpp"
#include <wtsapi32.h>
#include <stdexcept>

namespace usage::windows {

App::App(bool smoke, bool live_test, std::shared_ptr<host::MockProviders> mock)
    : mock_(std::move(mock)), providers_(usage_, mock_, smoke), animations_allowed_(!smoke), smoke_(smoke),
      demo_mode_(smoke), live_test_(live_test) {
    register_class(controller_class, controller_proc);
    register_class(widget_class, widget_proc);
    register_class(popup_class, popup_proc);
    register_class(hover_class, hover_proc);
    register_class(confetti_class, confetti_proc);
    controller_ =
        CreateWindowExW(WS_EX_TOOLWINDOW, controller_class, L"BarTab controller", WS_OVERLAPPED, 0, 0,
                        0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!controller_)
        throw std::runtime_error("Could not create controller window");
    frames_ = std::make_unique<FrameClock>(controller_, frame_message);
    update_system_font();
    update_animation_preference();
    load_settings();
    usage_.live = !smoke;
    if (!smoke) {
        usage_.codex.installed = false;
        providers_.detect();
        apply_providers();
    }
    taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
    shell_hook_ = RegisterWindowMessageW(L"SHELLHOOK");
    if (RegisterShellHookWindow(controller_))
        EnumWindows(
            [](HWND window, LPARAM app) {
                reinterpret_cast<App*>(app)->track_button(window);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
    else
        shell_hook_ = 0;
    add_tray();
    update_theme();
    if (!start_timer())
        throw std::runtime_error("Could not create update timer");
    // Smoke tests run on the fixed timer whatever the machine's power state.
    if (!smoke && !live_test)
        watch_power();
    log(L"Started native prototype; PID " + std::to_wstring(GetCurrentProcessId()));
}

// Loads the Windows UI font's regular and bold faces side by side, so each
// surface picks its weight by font id without reloading anything. Drops every
// cached measurement when either face changed, then refreshes the surfaces that
// lay themselves out on their own Clay context. The details popup is left to
// the caller: re-entering its render from inside its own frame would nest
// layouts on one context.
bool App::reload_ui_font() {
    auto regular_font = ui_font(false), bold_font = ui_font(true);
    const bool regular =
        renderer_.load_font_data(ui::regular_font, std::move(regular_font.bytes), regular_font.face_index);
    const bool bold =
        renderer_.load_font_data(ui::bold_font, std::move(bold_font.bytes), bold_font.face_index);
    if (!regular && !bold)
        return false;
    widget_view_.invalidate_measurements();
    hover_view_.invalidate_measurements();
    details_view_.invalidate_measurements();
    invalidate_widgets();
    if (IsWindowVisible(hover_)) show_hover();
    return true;
}

void App::update_system_font() {
    if (!reload_ui_font())
        return;
    if (IsWindowVisible(popup_)) render_details(details_pointer_);
}

// Smoke tests drive the popup synchronously and read layout back, so they keep
// the deterministic path; otherwise follow the Windows accessibility switch.
void App::update_animation_preference() {
    const bool enabled = animations_allowed_ && animations_enabled();
    // Logged at start and on change: Windows' "Animation effects" switch turning
    // everything off is the usual answer to "why is nothing animating".
    if (animations_logged_ != static_cast<int>(enabled)) {
        animations_logged_ = enabled;
        log(enabled ? L"Animations on" : L"Animations off (Windows animation effects are disabled)");
    }
    details_view_.set_animations(enabled);
}

App::~App() {
    if (controller_) {
        KillTimer(controller_, 1);
        if (shell_hook_)
            DeregisterShellHookWindow(controller_);
        if (display_notification_)
            UnregisterPowerSettingNotification(display_notification_);
        WTSUnRegisterSessionNotification(controller_);
    }
    Shell_NotifyIconW(NIM_DELETE, &tray_);
    if (popup_)
        DestroyWindow(popup_);
    reset_widget();
    if (hover_)
        DestroyWindow(hover_);
    if (confetti_window_)
        DestroyWindow(confetti_window_);
    if (controller_)
        DestroyWindow(controller_);
    log(L"Stopped native prototype.");
}

int App::run() {
    MSG message{};
    int result{};
    while ((result = static_cast<int>(GetMessageW(&message, nullptr, 0, 0))) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return result < 0 ? 1 : static_cast<int>(message.wParam);
}

void App::register_class(const wchar_t* name, WNDPROC procedure) {
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = procedure;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(cls.hInstance, MAKEINTRESOURCEW(IDI_BARTAB));
    cls.hIconSm = static_cast<HICON>(LoadImageW(cls.hInstance, MAKEINTRESOURCEW(IDI_BARTAB),
                                                IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                                GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    cls.lpszClassName = name;
    if (!RegisterClassExW(&cls))
        throw std::runtime_error("Could not register window class");
}

App* App::instance(HWND window, UINT message, LPARAM parameter) {
    if (message == WM_NCCREATE) {
        auto* app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(parameter)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        return app;
    }
    return reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

LRESULT CALLBACK App::controller_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* app = instance(window, message, l);
    if (app) {
        if (app->shell_hook_ && message == app->shell_hook_) {
            // Activation and redraw notices leave the buttons where they are, and
            // most windows the shell reports never get a button at all.
            const auto code = w & 0x7fff;
            const auto target = reinterpret_cast<HWND>(l);
            if ((code == HSHELL_WINDOWCREATED && app->track_button(target)) ||
                (code == HSHELL_WINDOWDESTROYED && app->buttons_.erase(target)) || code == HSHELL_WINDOWREPLACED)
                app->reader_.poke();
            return 0;
        }
        if (message == WM_POWERBROADCAST) {
            app->power_changed(static_cast<UINT>(w), l);
            return TRUE;
        }
        if (message == WM_WTSSESSION_CHANGE) {
            if (w == WTS_SESSION_LOCK || w == WTS_SESSION_UNLOCK) {
                app->locked_ = w == WTS_SESSION_LOCK;
                app->update_power();
            }
            return 0;
        }
        if (message == WM_DISPLAYCHANGE || message == WM_DPICHANGED) {
            app->reader_.poke();
            return DefWindowProcW(window, message, w, l);
        }
        if (message == WM_SETTINGCHANGE || message == WM_THEMECHANGED || message == WM_SYSCOLORCHANGE ||
            message == WM_DWMCOLORIZATIONCOLORCHANGED) {
            // Taskbar alignment and its search box and widgets buttons arrive here too.
            app->reader_.poke();
            app->update_system_font();
            app->update_animation_preference();
            app->update_theme();
            app->tick();
            app->invalidate_widgets();
            return 0;
        }
        if (message == frame_message) {
            app->frame();
            return 0;
        }
        if (message == WM_TIMER) {
            const auto update = app->providers_.poll();
            if (update.changed) {
                if (update.reset)
                    app->celebrate();
                app->update_usage();
                if (IsWindowVisible(app->popup_))
                    app->render_details(app->details_pointer_);
            }
            app->tick();
            return 0;
        }
        if (message == WM_CLOSE) {
            PostQuitMessage(0);
            return 0;
        }
        if (app->taskbar_created_ && message == app->taskbar_created_) {
            app->reset_widget();
            app->add_tray();
            app->reader_.poke();
            return 0;
        }
        if (message == tray_message) {
            if (l == WM_LBUTTONUP)
                app->open_details();
            if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU)
                app->show_menu();
            return 0;
        }
    }
    return DefWindowProcW(window, message, w, l);
}

void App::set_status(const std::wstring& value) {
    if (status_ == value)
        return;
    status_ = value;
    log(value);
}

void App::add_tray() {
    tray_ = {};
    tray_.cbSize = sizeof(tray_);
    tray_.hWnd = controller_;
    tray_.uID = 1;
    tray_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray_.uCallbackMessage = tray_message;
    tray_.hIcon = static_cast<HICON>(
        LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_BARTAB), IMAGE_ICON,
                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    update_tooltip();
    Shell_NotifyIconW(NIM_ADD, &tray_);
}

void App::update_tooltip() {
    std::wstring text = mock_ ? L"BarTab (mock providers)" : L"BarTab";
    if (usage_.live) {
        for (const auto& entry : {std::pair<const wchar_t*, const AccountUsage*>{L"Codex", &usage_.codex},
                                  {L"Claude", &usage_.claude}}) {
            if (!(entry.second == &usage_.codex ? usage_.codex_active() : usage_.claude_active()))
                continue;
            text += L"\n" + std::wstring(entry.first) + L": ";
            if (!entry.second->error.empty())
                text += L"stale/unavailable ";
            for (const auto& window : entry.second->windows)
                text += std::wstring(window.label.begin(), window.label.end()) + L" " +
                        std::to_wstring(window.remaining) + L"% ";
        }
    } else
        text = L"BarTab - demo data";
    wcsncpy_s(tray_.szTip, text.c_str(), _TRUNCATE);
}

void App::update_usage() {
    update_tooltip();
    Shell_NotifyIconW(NIM_MODIFY, &tray_);
    invalidate_widgets();
    if (IsWindowVisible(hover_))
        show_hover();
}

void App::apply_providers() {
    providers_.apply(preferences_);
}

void App::frame() {
    const bool hover = hover_ && IsWindowVisible(hover_) && hover_progress_ < 1.f && animate_hover();
    const bool details = animate_details();
    const bool confetti = !confetti_.done() && animate_confetti();
    if (!hover && !details && !confetti)
        frames_->stop();
    frames_->frame_done();
}

// Explorer's rule for which top-level windows get a taskbar button. Only those
// can move the buttons, so only they are remembered and only they poke the
// reader; the shell reports every other window too, and most are invisible.
bool App::track_button(HWND window) {
    if (!IsWindowVisible(window))
        return false;
    const auto extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const bool button = (extended & WS_EX_APPWINDOW) ||
                        (!GetWindow(window, GW_OWNER) && !(extended & WS_EX_TOOLWINDOW));
    if (button)
        buttons_.insert(window);
    return button;
}

// The display notification reports the current state on registering; each
// later change arrives as a message.
void App::watch_power() {
    display_notification_ =
        RegisterPowerSettingNotification(controller_, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);
    WTSRegisterSessionNotification(controller_, NOTIFY_FOR_THIS_SESSION);
    update_power();
}

void App::power_changed(UINT event, LPARAM l) {
    if (event == PBT_POWERSETTINGCHANGE) {
        const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(l);
        // 0 is off, 1 on, 2 dimmed; a dimmed display is still being looked at.
        if (setting && setting->PowerSetting == GUID_CONSOLE_DISPLAY_STATE && setting->DataLength >= sizeof(DWORD))
            display_on_ = *reinterpret_cast<const DWORD*>(setting->Data) != 0;
    }
    // Waking from sleep: whatever was read before it is out of date.
    if (event == PBT_APMRESUMEAUTOMATIC) {
        providers_.refresh();
        reader_.poke();
    }
    if (event == PBT_POWERSETTINGCHANGE)
        update_power();
}

void App::update_power() {
    const bool away = !display_on_ || locked_;
    if (away == away_)
        return;
    away_ = away;
    log(away ? L"Away: polling paused" : L"Back: polling resumed");
    providers_.set_power(away ? host::PowerMode::Away : host::PowerMode::Normal);
    reader_.set_paused(away);
    // Nothing changes while away, so the update timer stops too. Its first tick
    // back comes after the taskbar reader, just resumed, has read a fresh layout.
    if (away)
        KillTimer(controller_, 1);
    else if (!start_timer())
        log(L"Could not restart the update timer.");
}

// Nothing here needs to be on time to the millisecond, so Windows may batch
// its wake-ups with other timers'.
bool App::start_timer() {
    return SetCoalescableTimer(controller_, 1, 500, nullptr, 250) != 0;
}

void App::mock_changed() {
    providers_.detect();
    providers_.refresh();
    update_usage();
    if (IsWindowVisible(popup_))
        render_details(details_pointer_);
}

} // namespace usage::windows
