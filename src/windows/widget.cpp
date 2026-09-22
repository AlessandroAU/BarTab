#include "windows/app.hpp"
#include "windows/platform.hpp"
#include "windows/window_shape.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace usage::windows {

LRESULT CALLBACK App::widget_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* app = instance(window, message, l);
    if (app) {
        switch (message) {
        case WM_PAINT:
            app->paint_widget(window);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_MOUSEMOVE:
            if (app->hover_ && IsWindowVisible(app->hover_)) {
                KillTimer(app->hover_, 1);
                TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
                TrackMouseEvent(&tracking);
            }
            if (app->preferences_.appearance.hover_enabled && !app->hovered_ &&
                !(w & (MK_LBUTTON | MK_RBUTTON))) {
                app->hovered_ = true;
                app->widget_view_.set_hovered(true);
                InvalidateRect(window, nullptr, FALSE);
                TRACKMOUSEEVENT tracking{sizeof(tracking), TME_HOVER | TME_LEAVE, window,
                                         static_cast<DWORD>(app->preferences_.appearance.hover_delay)};
                TrackMouseEvent(&tracking);
            }
            return 0;
        case WM_MOUSEHOVER:
            app->show_hover();
            return 0;
        case WM_MOUSELEAVE:
            if (app->hover_ && IsWindowVisible(app->hover_))
                SetTimer(app->hover_, 1, 200, nullptr);
            else
                app->hide_hover();
            return 0;
        case WM_LBUTTONDOWN:
            app->hide_hover();
            SetCapture(window);
            return 0;
        case WM_LBUTTONUP: {
            if (GetCapture() != window)
                return 0;
            ReleaseCapture();
            RECT bounds{};
            GetClientRect(window, &bounds);
            const POINT point{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))};
            if (PtInRect(&bounds, point))
                app->open_details();
            return 0;
        }
        case WM_RBUTTONUP:
            app->show_menu();
            return 0;
        case WM_NCDESTROY:
            app->hide_hover(true);
            if (app->widget_ == window) {
                app->widget_ = nullptr;
                app->widget_bounds_ = {};
            }
            break;
        }
    }
    return DefWindowProcW(window, message, w, l);
}

LRESULT CALLBACK App::hover_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* app = instance(window, message, l);
    if (app) {
        if (message == WM_PAINT) {
            paint_pixels(window, app->hover_pixels_);
            return 0;
        }
        if (message == WM_SIZE) {
            round_window(window);
            return 0;
        }
        if (message == WM_ERASEBKGND)
            return 1;
        if (message == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        if (message == WM_MOUSEMOVE) {
            KillTimer(window, 1);
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            return 0;
        }
        if (message == WM_MOUSELEAVE) {
            SetTimer(window, 1, 200, nullptr);
            return 0;
        }
        if (message == WM_TIMER && w == 1) {
            KillTimer(window, 1);
            POINT pointer{};
            RECT card{}, widget{};
            GetCursorPos(&pointer);
            GetWindowRect(window, &card);
            GetWindowRect(app->widget_, &widget);
            if (!PtInRect(&card, pointer) && !PtInRect(&widget, pointer))
                app->hide_hover();
            return 0;
        }
        if (message == WM_TIMER && w == 2) {
            app->show_hover();
            return 0;
        }
    }
    return DefWindowProcW(window, message, w, l);
}

void App::hide_hover(bool force) {
    if (hover_pinned_ && !force) {
        // Keep the preview card; only drop the widget's hover highlight.
        if (hover_)
            KillTimer(hover_, 1);
        if (widget_ && IsWindow(widget_) && hovered_)
            InvalidateRect(widget_, nullptr, FALSE);
        hovered_ = false;
        widget_view_.set_hovered(false);
        return;
    }
    if (hover_) {
        KillTimer(hover_, 1);
        KillTimer(hover_, 2);
        ShowWindow(hover_, SW_HIDE);
    }
    if (widget_ && IsWindow(widget_)) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_CANCEL | TME_HOVER | TME_LEAVE, widget_, 0};
        TrackMouseEvent(&tracking);
        if (hovered_)
            InvalidateRect(widget_, nullptr, FALSE);
    }
    hovered_ = false;
    widget_view_.set_hovered(false);
}

void App::pin_hover() {
    hover_pinned_ = true;
    show_hover();
}

void App::unpin_hover() {
    hover_pinned_ = false;
    hide_hover();
}

void App::show_hover() {
    if (!preferences_.appearance.hover_enabled || !(hovered_ || hover_pinned_) || !IsWindowVisible(widget_) ||
        widget_bounds_.empty())
        return;
    if (!hover_) {
        hover_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                                 hover_class, L"Usage overview", WS_POPUP, 0, 0, 0, 0, controller_, nullptr,
                                 GetModuleHandleW(nullptr), this);
        if (!hover_)
            return;
    }
    SetLayeredWindowAttributes(
        hover_, 0, static_cast<BYTE>(std::lround(preferences_.appearance.hover_opacity * 255.f / 100.f)),
        LWA_ALPHA);
    const UINT dpi = GetDpiForWindow(widget_);
    const float scale = dpi ? dpi / 96.f : 1.f;
    auto logical = ui::hover_size(usage_, preferences_.appearance.hover_text_percent);
    logical.width = widget_bounds_.width / scale;
    renderer_.set_surface(scale, hover_view_.text_gamma());
    hover_view_.invalidate_measurements();
    if (usage_.live) logical.height = hover_view_.hover_height(usage_, logical.width);
    int width = static_cast<int>(std::lround(logical.width * scale));
    const int height = static_cast<int>(std::lround(logical.height * scale));
    RECT anchor{widget_bounds_.x, widget_bounds_.y, widget_bounds_.right(), widget_bounds_.bottom()};
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    width = std::min(width, static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
    logical.width = width / scale;
    const int x =
        std::max(static_cast<int>(monitor.rcWork.left),
                 std::min(widget_bounds_.right() - width, static_cast<int>(monitor.rcWork.right) - width));
    const int y = std::max(static_cast<int>(monitor.rcWork.top),
                           std::min(widget_bounds_.y - height - static_cast<int>(8 * scale),
                                    static_cast<int>(monitor.rcWork.bottom) - height));
    SetWindowPos(hover_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    const auto frame = hover_view_.frame(usage_, {}, logical.width, logical.height);
    hover_pixels_ = renderer_.render(frame.commands, width, height, scale, false);
    InvalidateRect(hover_, nullptr, FALSE);
    ShowWindow(hover_, SW_SHOWNOACTIVATE);
    SetTimer(hover_, 2, 60000, nullptr);
}

void App::reset_widget() {
    hide_hover(true);
    if (widget_ && IsWindow(widget_))
        DestroyWindow(widget_);
    widget_ = nullptr;
    widget_bounds_ = {};
}

void App::hide_widget() {
    hide_hover(true);
    if (widget_)
        ShowWindow(widget_, SW_HIDE);
    widget_bounds_ = {};
}

void App::tick() {
    const bool app_light = apps_light_theme();
    const auto os_accent = windows_accent();
    const bool details_light_changed = details_view_.set_system_light(app_light);
    const bool details_accent_changed = details_view_.set_system_accent(os_accent);
    const bool hover_light_changed = hover_view_.set_system_light(app_light);
    const bool hover_accent_changed = hover_view_.set_system_accent(os_accent);
    if ((details_light_changed || details_accent_changed) && IsWindowVisible(popup_))
        render_details(details_pointer_);
    if ((hover_light_changed || hover_accent_changed) && IsWindowVisible(hover_))
        show_hover();
    if (widget_view_.set_system_accent(os_accent) && widget_)
        InvalidateRect(widget_, nullptr, FALSE);
    if (widget_view_.set_system_light(system_light_theme()) && widget_)
        InvalidateRect(widget_, nullptr, FALSE);
    const auto snapshot = reader_.latest();
    const auto now = std::chrono::steady_clock::now();
    const auto current = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (widget_ && (!IsWindow(widget_) || GetParent(widget_) != current))
        reset_widget();
    if (!snapshot.error.empty() || snapshot.taskbar != current || !current) {
        hide_widget();
        set_status(snapshot.error.empty() ? L"Waiting for the Windows taskbar." : snapshot.error);
    } else if (now - snapshot.captured > std::chrono::seconds(5)) {
        hide_widget();
        set_status(L"Taskbar layout is stale. Waiting for Explorer.");
    } else {
        const double scale = snapshot.dpi / 96.0;
        const auto placement = ui::place_widget(usage_, preferences_.appearance, snapshot.bounds,
                                                 snapshot.occupied, static_cast<float>(scale));
        const auto target = placement.bounds;
        const bool text_changed = widget_text_percent_ != placement.text_percent ||
                                  widget_spacing_percent_ != placement.spacing_percent;
        widget_spacing_percent_ = placement.spacing_percent;
        widget_text_percent_ = placement.text_percent;
        if (target.empty()) {
            hide_widget();
            set_status(L"No free taskbar space. Use the tray icon.");
        } else {
            if (!widget_) {
                const auto previous = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(current));
                widget_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, widget_class,
                                          L"UsageTracker taskbar widget", WS_CHILD, 0, 0, target.width,
                                          target.height, current, nullptr, GetModuleHandleW(nullptr), this);
                SetThreadDpiAwarenessContext(previous);
                // Per-pixel alpha is supplied by paint_widget. Do not call
                // SetLayeredWindowAttributes: it disables UpdateLayeredWindow.
            }
            const bool unchanged = !text_changed && widget_ && IsWindowVisible(widget_) && target == widget_bounds_;
            if (widget_ && (unchanged || SetWindowPos(widget_, HWND_TOP, target.x - snapshot.bounds.x,
                                                      target.y - snapshot.bounds.y, target.width,
                                                      target.height, SWP_NOACTIVATE | SWP_SHOWWINDOW))) {
                widget_bounds_ = target;
                if (!unchanged) {
                    // A pinned preview follows the widget; a pointer card closes.
                    if (hover_pinned_)
                        show_hover();
                    else
                        hide_hover();
                    InvalidateRect(widget_, nullptr, FALSE);
                }
                set_status(L"Embedded in the primary taskbar");
            } else {
                hide_widget();
                set_status(L"Could not embed the widget; retrying.");
            }
        }
    }
    if (smoke_ && now - started_ >= std::chrono::seconds(8))
        finish_smoke_test();
    if (live_test_ && now - started_ >= std::chrono::seconds(8) &&
        ((usage_.codex.updated && usage_.claude.updated) || now - started_ >= std::chrono::seconds(25)))
        finish_live_test();
}

void App::paint_widget(HWND window) {
    PAINTSTRUCT paint{};
    BeginPaint(window, &paint);
    RECT bounds{};
    GetClientRect(window, &bounds);
    if (bounds.right > 0 && bounds.bottom > 0) {
        const float scale = static_cast<float>(bounds.bottom) / widget_height;
        renderer_.set_surface(scale, widget_view_.text_gamma());
        if (widget_scale_ != scale) {
            widget_view_.invalidate_measurements();
            widget_scale_ = scale;
        }
        widget_view_.set_widget_spacing(widget_spacing_percent_);
        const int requested_percent = widget_view_.text_percent();
        widget_view_.set_text_percent(widget_text_percent_ > 0 ? widget_text_percent_ : requested_percent);
        const auto frame = widget_view_.frame(usage_, {}, bounds.right / scale, widget_height);
        widget_view_.set_text_percent(requested_percent);
        const auto pixels = renderer_.render(frame.commands, bounds.right, bounds.bottom, scale, true);
        HDC dc = GetDC(nullptr);
        HDC buffer = CreateCompatibleDC(dc);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = pixels.width;
        info.bmiHeader.biHeight = -pixels.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* bits{};
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bitmap && buffer) {
            auto old = SelectObject(buffer, bitmap);
            std::memcpy(bits, pixels.data.data(), pixels.data.size() * sizeof(std::uint32_t));
            POINT source{};
            SIZE size{pixels.width, pixels.height};
            BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            UpdateLayeredWindow(window, dc, nullptr, &size, buffer, &source, 0, &blend, ULW_ALPHA);
            SelectObject(buffer, old);
        }
        if (bitmap)
            DeleteObject(bitmap);
        if (buffer)
            DeleteDC(buffer);
        ReleaseDC(nullptr, dc);
        ++widget_frames_;
    }
    EndPaint(window, &paint);
}

} // namespace usage::windows
