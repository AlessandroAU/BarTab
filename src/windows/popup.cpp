#include "windows/app.hpp"
#include "windows/window_shape.hpp"
#include <algorithm>
#include <chrono>

namespace usage::windows {
namespace {
// Longest eased motion in the widget set is the toggle knob at about 0.36 s;
// the loop outlives it so every transition reaches its target.
constexpr auto settle_window = std::chrono::milliseconds(500);
} // namespace

LRESULT CALLBACK App::popup_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* app = instance(window, message, l);
    if (app) {
        switch (message) {
        case WM_CLOSE:
            app->close_details();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            app->paint_details(window);
            return 0;
        case WM_NCHITTEST: {
            POINT point{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))};
            ScreenToClient(window, &point);
            if (point.y >= 0 && point.y < MulDiv(48, GetDpiForWindow(window), 96))
                return HTCAPTION;
            return HTCLIENT;
        }
        case WM_SETCURSOR:
            // Windows asks before every mouse move; answering with the class arrow
            // would flicker against the hand the next frame sets.
            if (LOWORD(l) == HTCLIENT) {
                app->set_details_cursor();
                return TRUE;
            }
            break;
        case WM_MOUSEWHEEL:
        case WM_MOUSEMOVE:
        case WM_MOUSELEAVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_KEYDOWN:
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            app->details_event(window, message, w, l);
            return 0;
        case WM_SIZE:
            round_window(window);
            if (app->popup_ && IsWindowVisible(window))
                app->render_details({});
            return 0;
        case WM_DPICHANGED: {
            const auto* bounds = reinterpret_cast<RECT*>(l);
            SetWindowPos(window, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
                         bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (app->popup_ && IsWindowVisible(window))
                app->render_details({});
            return 0;
        }
        }
    }
    return DefWindowProcW(window, message, w, l);
}

float App::popup_scale() const {
    const auto dpi = GetDpiForWindow(popup_);
    return dpi ? static_cast<float>(dpi) / 96.f : 1.f;
}

void App::close_details() {
    details_pointer_ = {};
    details_pointer_.mouseX = details_pointer_.mouseY = -100;
    details_view_.reset_focus();
    if (GetCapture() == popup_)
        ReleaseCapture();
    ShowWindow(popup_, SW_HIDE);
    unpin_hover();
    cancel_settings_preview();
}

// Input or a data change: render now and, when animating, keep frames coming
// for the settle window so eased motion runs to completion.
void App::render_details(ClayWidgets_Input input) {
    if (!popup_)
        return;
    if (details_view_.animations()) {
        details_settle_until_ = std::chrono::steady_clock::now() + settle_window;
        frames_->start();
    }
    render_details_frame(input);
}

bool App::animate_details() {
    if (!popup_ || !IsWindowVisible(popup_) || !details_view_.animations() ||
        std::chrono::steady_clock::now() >= details_settle_until_)
        return false;
    render_details_frame(details_pointer_);
    return true;
}

void App::render_details_frame(ClayWidgets_Input input) {
    if (!popup_)
        return;
    // Elapsed time since the previous frame drives the easing; a long idle gap is
    // capped so a transition never jumps most of the way on its first frame.
    const auto now = std::chrono::steady_clock::now();
    input.deltaTime = details_rendered_ == std::chrono::steady_clock::time_point{}
                          ? 0.f
                          : std::min(0.05f, std::chrono::duration<float>(now - details_rendered_).count());
    details_rendered_ = now;
    RECT rect{};
    GetClientRect(popup_, &rect);
    if (rect.right <= 0 || rect.bottom <= 0)
        return;
    const float scale = popup_scale();
    renderer_.set_surface(scale, details_view_.text_gamma());
    if (details_scale_ != scale) {
        details_view_.invalidate_measurements();
        details_scale_ = scale;
    }
    auto frame = details_view_.frame(usage_, input, static_cast<float>(rect.right) / scale,
                                     static_cast<float>(rect.bottom) / scale);
    if (frame.close) {
        close_details();
        return;
    }
    if (frame.refresh) {
        providers_.detect();
        frame.changed = true;
        providers_.refresh();
    }
    if (frame.save) {
        if (!save_settings(details_view_.preferences()))
            return;
        close_details();
        tick();
        update_usage();
        return;
    }
    if (frame.changed) {
        if (settings_mode_)
            preview_settings(details_view_.preferences());
        else
            update_usage();
        // Settle the borrowed percentage labels after the slider changes data.
        // Same instant as the frame above, so no further easing time elapses.
        auto settle = details_pointer_;
        settle.deltaTime = 0.f;
        frame = details_view_.frame(usage_, settle, static_cast<float>(rect.right) / scale,
                                    static_cast<float>(rect.bottom) / scale);
    }
    details_pixels_ = renderer_.render(frame.commands, rect.right, rect.bottom, scale, false);
    ++details_frames_;
    set_details_cursor();
    InvalidateRect(popup_, nullptr, FALSE);
}

void App::set_details_cursor() const {
    SetCursor(
        LoadCursorW(nullptr, details_view_.cursor() == CLAY_WIDGETS_CURSOR_POINTER ? IDC_HAND : IDC_ARROW));
}

void App::paint_details(HWND window) {
    paint_pixels(window, details_pixels_);
}

void App::paint_pixels(HWND window, const ui::Pixels& pixels, int y) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    if (!pixels.data.empty()) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = pixels.width;
        info.bmiHeader.biHeight = -pixels.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        SetDIBitsToDevice(dc, 0, y, pixels.width, pixels.height, 0, 0, 0, static_cast<UINT>(pixels.height),
                          pixels.data.data(), &info, DIB_RGB_COLORS);
    }
    EndPaint(window, &paint);
}

void App::details_event(HWND window, UINT message, WPARAM w, LPARAM l) {
    // Captured drags keep their real coordinates outside the client area.
    if (message == WM_MOUSELEAVE && GetCapture() == window)
        return;
    auto input = details_pointer_;
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) {
        input.mouseX = static_cast<short>(LOWORD(l)) / popup_scale();
        input.mouseY = static_cast<short>(HIWORD(l)) / popup_scale();
        if (message == WM_MOUSEMOVE && input.mouseX == details_pointer_.mouseX &&
            input.mouseY == details_pointer_.mouseY)
            return;
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);
    }
    if (message == WM_MOUSEWHEEL) {
        POINT point{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))};
        ScreenToClient(window, &point);
        input.mouseX = point.x / popup_scale();
        input.mouseY = point.y / popup_scale();
        input.scrollY = static_cast<short>(HIWORD(w)) / static_cast<float>(WHEEL_DELTA);
    }
    if (message == WM_MOUSELEAVE)
        input.mouseX = input.mouseY = -100;
    if (message == WM_LBUTTONDOWN) {
        SetFocus(window);
        SetCapture(window);
        input.pointerDown = input.pointerPressed = true;
    }
    if (message == WM_LBUTTONUP) {
        input.pointerDown = false;
        input.pointerReleased = true;
    }
    if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE) {
        input.pointerDown = false;
        if (message == WM_CANCELMODE && GetCapture() == window)
            ReleaseCapture();
    }
    if (message == WM_KEYDOWN) {
        input.shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        input.keyTab = w == VK_TAB;
        input.keyLeft = w == VK_LEFT;
        input.keyRight = w == VK_RIGHT;
        input.keyUp = w == VK_UP;
        input.keyDown = w == VK_DOWN;
        input.keyHome = w == VK_HOME;
        input.keyEnd = w == VK_END;
        input.keyEnter = w == VK_RETURN;
        input.keySpace = w == VK_SPACE;
        input.keyEscape = w == VK_ESCAPE;
    }
    details_pointer_ = {};
    details_pointer_.mouseX = input.mouseX;
    details_pointer_.mouseY = input.mouseY;
    details_pointer_.pointerDown = input.pointerDown;
    render_details(input);
    // Deliver the release to Clay before WM_CAPTURECHANGED clears activeId.
    if (message == WM_LBUTTONUP && GetCapture() == window)
        ReleaseCapture();
}

// Slides the settings window left, then up, so it never covers the pinned hover card.
void App::avoid_hover(int& x, int& y, int width, int height, const RECT& work) const {
    if (!hover_pinned_ || !hover_ || !IsWindowVisible(hover_))
        return;
    RECT card{};
    GetWindowRect(hover_, &card);
    const int gap = 8;
    auto overlaps = [&] {
        return x < card.right + gap && x + width > card.left - gap && y < card.bottom + gap &&
               y + height > card.top - gap;
    };
    if (!overlaps())
        return;
    if (card.left - gap - width >= work.left)
        x = card.left - gap - width;
    else if (card.right + gap + width <= work.right)
        x = card.right + gap;
    if (overlaps())
        y = std::max(static_cast<int>(work.top), static_cast<int>(card.top) - gap - height);
}

void App::open_details(bool settings) {
    settings = settings || usage_.live;
    hide_hover();
    if (!popup_) {
        popup_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, popup_class, L"UsageTracker - demo",
                                 WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 360, 370, controller_, nullptr,
                                 GetModuleHandleW(nullptr), this);
        if (!popup_)
            return;
    }
    if (IsWindowVisible(popup_) && settings_mode_ == settings) {
        SetForegroundWindow(popup_);
        return;
    }
    if (IsWindowVisible(popup_))
        close_details();
    details_rendered_ = {};
    settings_mode_ = settings;
    if (settings)
        begin_settings_preview();
    providers_.detect();
    details_view_.set_surface(settings ? ui::Surface::Settings : ui::Surface::Details);
    details_view_.set_preferences(preferences_);
    SetWindowTextW(popup_, settings ? L"UsageTracker - Settings and usage" : L"UsageTracker - Demo");
    details_view_.reset_focus();
    details_pointer_ = {};
    details_pointer_.mouseX = details_pointer_.mouseY = -100;
    const auto& widget = active();
    UINT dpi = widget.window ? GetDpiForWindow(widget.window) : GetDpiForWindow(controller_);
    if (!dpi)
        dpi = 96;
    RECT size{0, 0, MulDiv(usage_.live ? ui::settings_width : 400, static_cast<int>(dpi), 96),
              MulDiv(usage_.live ? ui::settings_height : 470, static_cast<int>(dpi), 96)};
    POINT cursor{};
    GetCursorPos(&cursor);
    const Rect anchor = widget.bounds.empty() ? Rect{cursor.x, cursor.y, 1, 1} : widget.bounds;
    RECT anchor_rect{anchor.x, anchor.y, anchor.right(), anchor.bottom()};
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromRect(&anchor_rect, MONITOR_DEFAULTTONEAREST), &monitor);
    const int width = std::min(static_cast<int>(size.right - size.left),
                               static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
    const int height = std::min(static_cast<int>(size.bottom - size.top),
                                static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
    const int x =
        settings ? monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2
                 : std::max(static_cast<int>(monitor.rcWork.left),
                            std::min(anchor.right() - width, static_cast<int>(monitor.rcWork.right) - width));
    int y = settings
                ? monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2
                : std::max(static_cast<int>(monitor.rcWork.top),
                           std::min(anchor.y - height - 8, static_cast<int>(monitor.rcWork.bottom) - height));
    int left = x;
    if (settings) {
        pin_hover();
        avoid_hover(left, y, width, height, monitor.rcWork);
    }
    SetWindowPos(popup_, HWND_TOPMOST, left, y, width, height, SWP_NOACTIVATE);
    render_details(details_pointer_);
    ShowWindow(popup_, SW_SHOW);
    SetForegroundWindow(popup_);
    SetFocus(popup_);
}

} // namespace usage::windows
