#include "windows/app.hpp"
#include "host/startup.hpp"
#include "windows/platform.hpp"
#include <ShellScalingApi.h>
#include <algorithm>
#include <cmath>

namespace usage::windows {

LRESULT CALLBACK App::menu_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* app = instance(window, message, l);
    if (app) {
        switch (message) {
        case WM_ACTIVATE:
            // A click anywhere else takes activation away: dismiss, as native menus do.
            if (LOWORD(w) == WA_INACTIVE)
                app->close_menu();
            return 0;
        case WM_CLOSE:
            app->close_menu();
            return 0;
        case WM_MOUSEMOVE:
        case WM_MOUSELEAVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_KEYDOWN:
            app->menu_event(window, message, w, l);
            return 0;
        }
    }
    return DefWindowProcW(window, message, w, l);
}

// Opens the context menu at the pointer, from the widget or the tray icon.
void App::show_menu() {
    hide_hover();
    if (!menu_) {
        menu_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, menu_class,
                                L"UsageTracker menu", WS_POPUP, 0, 0, 1, 1, controller_, nullptr,
                                GetModuleHandleW(nullptr), this);
        if (!menu_)
            return;
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    UINT dpi_x = 96, dpi_y = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)))
        dpi_x = 96;
    const float scale = static_cast<float>(dpi_x) / 96.f;
    if (scale != menu_scale_) {
        menu_view_.invalidate_measurements();
        menu_scale_ = scale;
    }
    ui::MenuModel model;
    const auto startup = host::startup_state();
    model.startup_label = "Start at boot";
    model.startup_enabled = startup.enabled;
    model.startup_available = startup.error.empty();
    model.debug_label = open_mock_panel_ ? "Mock providers..." : nullptr;
    menu_view_.open_menu(model);
    // Lay the menu out once in a roomy view to learn its size, then fit the window to it.
    menu_pointer_ = {};
    menu_pointer_.mouseX = menu_pointer_.mouseY = -100;
    renderer_.set_surface(menu_scale_, menu_view_.text_gamma());
    menu_view_.frame(usage_, menu_pointer_, 600, 600);
    const auto panel = menu_view_.menu_bounds();
    const int width = static_cast<int>(std::ceil(panel.width * menu_scale_));
    const int height = static_cast<int>(std::ceil(panel.height * menu_scale_));
    // Down and right of the pointer, flipping to whichever side has room: from
    // the tray it opens upward, above the taskbar.
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    const RECT& work = info.rcWork;
    int x = cursor.x + width > work.right ? cursor.x - width : cursor.x;
    int y = cursor.y + height > work.bottom ? cursor.y - height : cursor.y;
    x = std::max(static_cast<int>(work.left), std::min(x, static_cast<int>(work.right) - width));
    y = std::max(static_cast<int>(work.top), std::min(y, static_cast<int>(work.bottom) - height));
    SetWindowPos(menu_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    menu_open_ = true;
    render_menu(menu_pointer_);
    ShowWindow(menu_, SW_SHOW);
    // Keys and the deactivation that dismisses the menu both need it in front.
    // The click that opened it lets this process take the foreground.
    SetForegroundWindow(menu_);
    SetFocus(menu_);
}

void App::close_menu() {
    // Hiding the active menu deactivates it, which calls back in here.
    if (!menu_open_)
        return;
    menu_open_ = false;
    menu_pointer_ = {};
    menu_pointer_.mouseX = menu_pointer_.mouseY = -100;
    ShowWindow(menu_, SW_HIDE);
}

void App::render_menu(ClayWidgets_Input input) {
    if (!menu_ || !menu_open_)
        return;
    RECT rect{};
    GetClientRect(menu_, &rect);
    if (rect.right <= 0 || rect.bottom <= 0)
        return;
    renderer_.set_surface(menu_scale_, menu_view_.text_gamma());
    const auto frame = menu_view_.frame(usage_, input, static_cast<float>(rect.right) / menu_scale_,
                                        static_cast<float>(rect.bottom) / menu_scale_);
    if (frame.close || frame.menu != ui::Frame::MenuChoice::None) {
        close_menu();
        choose_menu(frame.menu);
        return;
    }
    present_layered(menu_, renderer_.render(frame.commands, rect.right, rect.bottom, menu_scale_, false));
}

void App::choose_menu(ui::Frame::MenuChoice choice) {
    using Choice = ui::Frame::MenuChoice;
    switch (choice) {
    case Choice::Settings:
        open_details(true);
        break;
    case Choice::Debug:
        if (open_mock_panel_)
            open_mock_panel_();
        break;
    case Choice::Startup: {
        const auto error = host::set_startup(!host::startup_state().enabled);
        if (!error.empty()) {
            const auto message = L"Could not change Start at boot.\n\n" + widen(error);
            MessageBoxW(controller_, message.c_str(), L"UsageTracker", MB_OK | MB_ICONERROR);
        }
        break;
    }
    case Choice::Quit:
        PostQuitMessage(0);
        break;
    case Choice::None:
        break;
    }
}

void App::menu_event(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto input = menu_pointer_;
    const bool pointer = message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
                         message == WM_RBUTTONDOWN || message == WM_RBUTTONUP;
    if (pointer) {
        input.mouseX = static_cast<short>(LOWORD(l)) / menu_scale_;
        input.mouseY = static_cast<short>(HIWORD(l)) / menu_scale_;
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);
    }
    if (message == WM_MOUSELEAVE)
        input.mouseX = input.mouseY = -100;
    // Either button picks an item, as TPM_RIGHTBUTTON menus allow.
    if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN)
        input.pointerDown = input.pointerPressed = true;
    if (message == WM_LBUTTONUP || message == WM_RBUTTONUP) {
        input.pointerDown = false;
        input.pointerReleased = true;
    }
    if (message == WM_KEYDOWN) {
        input.keyUp = w == VK_UP;
        input.keyDown = w == VK_DOWN || w == VK_TAB;
        input.keyHome = w == VK_HOME;
        input.keyEnd = w == VK_END;
        input.keyEnter = w == VK_RETURN;
        input.keySpace = w == VK_SPACE;
        input.keyEscape = w == VK_ESCAPE;
    }
    menu_pointer_ = {};
    menu_pointer_.mouseX = input.mouseX;
    menu_pointer_.mouseY = input.mouseY;
    menu_pointer_.pointerDown = input.pointerDown;
    render_menu(input);
}

} // namespace usage::windows
