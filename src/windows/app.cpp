#include "windows/app.hpp"
#include "windows/platform.hpp"
#include "windows/resource.h"
#include "windows/startup.hpp"
#include <stdexcept>

namespace usage::windows {

App::App(bool smoke, bool live_test)
    : animations_allowed_(!smoke), smoke_(smoke), demo_mode_(smoke), live_test_(live_test) {
    register_class(controller_class, controller_proc);
    register_class(widget_class, widget_proc);
    register_class(popup_class, popup_proc);
    register_class(hover_class, hover_proc);
    controller_ =
        CreateWindowExW(WS_EX_TOOLWINDOW, controller_class, L"UsageTracker controller", WS_OVERLAPPED, 0, 0,
                        0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!controller_)
        throw std::runtime_error("Could not create controller window");
    update_system_font();
    update_animation_preference();
    load_settings();
    usage_.live = !smoke;
    if (!smoke) {
        usage_.codex.installed = false;
        detect_providers();
        apply_providers();
    }
    taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
    add_tray();
    if (!SetTimer(controller_, 1, 500, nullptr))
        throw std::runtime_error("Could not create update timer");
    log(L"Started native prototype; PID " + std::to_wstring(GetCurrentProcessId()));
}

// Loads the Windows UI font's regular and bold faces side by side, so each
// surface picks its weight by font id without reloading anything. Drops every
// cached measurement when either face changed, then refreshes the surfaces that
// lay themselves out on their own Clay context. The details popup is left to
// the caller: re-entering its render from inside its own frame would nest
// layouts on one context.
bool App::reload_ui_font() {
    const bool regular = renderer_.load_font_data(ui::regular_font, windows_ui_font(false));
    const bool bold = renderer_.load_font_data(ui::bold_font, windows_ui_font(true));
    if (!regular && !bold)
        return false;
    widget_view_.invalidate_measurements();
    hover_view_.invalidate_measurements();
    details_view_.invalidate_measurements();
    if (widget_) InvalidateRect(widget_, nullptr, FALSE);
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
    details_view_.set_animations(animations_allowed_ && client_animations_enabled());
}

App::~App() {
    if (controller_)
        KillTimer(controller_, 1);
    Shell_NotifyIconW(NIM_DELETE, &tray_);
    if (popup_)
        DestroyWindow(popup_);
    reset_widget();
    if (hover_)
        DestroyWindow(hover_);
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
    cls.hIcon = LoadIconW(cls.hInstance, MAKEINTRESOURCEW(IDI_USAGE_TRACKER));
    cls.hIconSm = static_cast<HICON>(LoadImageW(cls.hInstance, MAKEINTRESOURCEW(IDI_USAGE_TRACKER),
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
        if (message == WM_SETTINGCHANGE || message == WM_THEMECHANGED || message == WM_SYSCOLORCHANGE) {
            app->update_system_font();
            app->update_animation_preference();
            app->tick();
            if (app->widget_)
                InvalidateRect(app->widget_, nullptr, FALSE);
            return 0;
        }
        if (message == WM_TIMER) {
            const bool codex_changed = app->codex_ && app->codex_->take(app->usage_.codex);
            const bool claude_changed = app->claude_ && app->claude_->take(app->usage_.claude);
            if (codex_changed || claude_changed) {
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
        LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_USAGE_TRACKER), IMAGE_ICON,
                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    update_tooltip();
    Shell_NotifyIconW(NIM_ADD, &tray_);
}

void App::update_tooltip() {
    std::wstring text = L"UsageTracker";
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
        text = L"UsageTracker - demo data";
    wcsncpy_s(tray_.szTip, text.c_str(), _TRUNCATE);
}

void App::update_usage() {
    update_tooltip();
    Shell_NotifyIconW(NIM_MODIFY, &tray_);
    if (widget_)
        InvalidateRect(widget_, nullptr, FALSE);
    if (IsWindowVisible(hover_))
        show_hover();
}

void App::show_menu() {
    hide_hover();
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    const auto startup = startup_state();
    AppendMenuW(menu, MF_STRING, 4, L"Settings");
    AppendMenuW(menu,
                MF_STRING | (startup.enabled ? MF_CHECKED : MF_UNCHECKED) |
                    (startup.error == ERROR_SUCCESS ? 0 : MF_GRAYED),
                6, L"Start at boot");
    AppendMenuW(menu, MF_STRING, 3, L"Quit");
    POINT position{};
    GetCursorPos(&position);
    SetForegroundWindow(controller_);
    const auto selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, position.x, position.y, 0,
                                         controller_, nullptr);
    DestroyMenu(menu);
    PostMessageW(controller_, WM_NULL, 0, 0);
    if (selected == 3)
        PostQuitMessage(0);
    if (selected == 4)
        open_details(true);
    if (selected == 6) {
        const auto status = set_startup(!startup.enabled);
        if (status != ERROR_SUCCESS) {
            wchar_t detail[512]{};
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                           static_cast<DWORD>(status), 0, detail, static_cast<DWORD>(std::size(detail)),
                           nullptr);
            const auto message = L"Could not change Start at boot.\n\n" + std::wstring(detail) +
                                 L"\nWindows error: " + std::to_wstring(status);
            MessageBoxW(controller_, message.c_str(), L"UsageTracker", MB_OK | MB_ICONERROR);
        }
    }
}

void App::detect_providers() {
    if (demo_mode_)
        return;
    detect_service(Service::Codex, usage_.codex);
    detect_service(Service::Claude, usage_.claude);
}
void App::apply_providers() {
    if (demo_mode_)
        return;
    if (usage_.codex_enabled) {
        if (!codex_)
            codex_ = std::make_unique<UsageReader>(Service::Codex, usage_.codex);
        codex_->set_interval(preferences_.codex_interval);
    } else
        codex_.reset();
    if (usage_.claude_enabled) {
        if (!claude_)
            claude_ = std::make_unique<UsageReader>(Service::Claude, usage_.claude);
        claude_->set_interval(preferences_.claude_interval);
    } else
        claude_.reset();
}

} // namespace usage::windows
