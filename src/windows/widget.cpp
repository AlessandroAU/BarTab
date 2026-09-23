#include "windows/app.hpp"
#include "windows/platform.hpp"
#include "windows/window_shape.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace usage::windows {
namespace {
constexpr auto hover_open_duration = std::chrono::milliseconds(200);
// The card starts this fraction of its height tall and grows to full.
constexpr float hover_open_start = 0.0f;
// Room around the widget for the burst to rise and spread into, in DIPs.
constexpr float confetti_rise = 300.f, confetti_spread = 300.f;
constexpr int confetti_pieces = 250;
} // namespace

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
            if (app->active_widget_ != window) {
                app->active_widget_ = window;
                if (app->hovered_) {
                    app->invalidate_widgets();
                    app->show_hover();
                }
            }
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
                TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
                TrackMouseEvent(&tracking);
                // No delay: the card's opening animation already eases it in.
                app->show_hover();
            }
            return 0;
        case WM_MOUSELEAVE:
            if (app->hover_ && IsWindowVisible(app->hover_))
                SetTimer(app->hover_, 1, 200, nullptr);
            else
                app->hide_hover();
            return 0;
        case WM_LBUTTONDOWN:
            // The widget never takes activation, so a click on it cannot
            // deactivate an open menu; close it here.
            app->close_menu();
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
            if (app->primary_.window == window) {
                app->primary_.window = nullptr;
                app->primary_.bounds = {};
            }
            for (auto& widget : app->secondary_)
                if (widget.window == window) {
                    widget.window = nullptr;
                    widget.bounds = {};
                }
            if (app->active_widget_ == window)
                app->active_widget_ = nullptr;
            break;
        }
    }
    return DefWindowProcW(window, message, w, l);
}

LRESULT CALLBACK App::hover_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* app = instance(window, message, l);
    if (app) {
        if (message == WM_PAINT) {
            paint_pixels(window, app->hover_pixels_, app->hover_offset_);
            return 0;
        }
        if (message == WM_SIZE) {
            app->shape_hover();
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
            GetWindowRect(app->active().window, &widget);
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
        if (hovered_)
            invalidate_widgets();
        hovered_ = false;
        widget_view_.set_hovered(false);
        return;
    }
    if (hover_) {
        KillTimer(hover_, 1);
        KillTimer(hover_, 2);
        ShowWindow(hover_, SW_HIDE);
    }
    if (const auto window = active().window; window && IsWindow(window)) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_CANCEL | TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);
    }
    if (hovered_)
        invalidate_widgets();
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
    const auto& anchor_widget = active();
    const auto& widget_bounds = anchor_widget.bounds;
    if (!preferences_.appearance.hover_enabled || !(hovered_ || hover_pinned_) ||
        !IsWindowVisible(anchor_widget.window) || widget_bounds.empty())
        return;
    if (!hover_) {
        hover_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                                 hover_class, L"Usage overview", WS_POPUP, 0, 0, 0, 0, controller_, nullptr,
                                 GetModuleHandleW(nullptr), this);
        if (!hover_)
            return;
    }
    const bool opening = !IsWindowVisible(hover_);
    const UINT dpi = GetDpiForWindow(anchor_widget.window);
    const float scale = dpi ? dpi / 96.f : 1.f;
    auto logical = ui::hover_size(usage_, preferences_.appearance.hover_text_scale());
    logical.width = widget_bounds.width / scale;
    renderer_.set_surface(scale, hover_view_.text_gamma());
    hover_view_.invalidate_measurements();
    if (usage_.live) logical.height = hover_view_.hover_height(usage_, logical.width);
    int width = static_cast<int>(std::lround(logical.width * scale));
    const int height = static_cast<int>(std::lround(logical.height * scale));
    RECT anchor{widget_bounds.x, widget_bounds.y, widget_bounds.right(), widget_bounds.bottom()};
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    width = std::min(width, static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
    logical.width = width / scale;
    const int x =
        std::max(static_cast<int>(monitor.rcWork.left),
                 std::min(widget_bounds.right() - width, static_cast<int>(monitor.rcWork.right) - width));
    const int y = std::max(static_cast<int>(monitor.rcWork.top),
                           std::min(widget_bounds.y - height - static_cast<int>(8 * scale),
                                    static_cast<int>(monitor.rcWork.bottom) - height));
    if (opening) {
        // Grow away from the taskbar: upward when the card sits above the widget.
        hover_grows_up_ = y + height / 2 < widget_bounds.y + widget_bounds.height / 2;
        const bool animate = animations_allowed_ && animations_enabled();
        hover_progress_ = animate ? 0.f : 1.f;
        hover_opened_ = std::chrono::steady_clock::now();
        if (animate)
            frames_->start();
    }
    SetWindowPos(hover_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    const auto frame = hover_view_.frame(usage_, {}, logical.width, logical.height);
    hover_pixels_ = renderer_.render(frame.commands, width, height, scale, false);
    shape_hover();
    InvalidateRect(hover_, nullptr, FALSE);
    ShowWindow(hover_, SW_SHOWNOACTIVATE);
    SetTimer(hover_, 2, 60000, nullptr);
}

// Clips the card to the part that has grown so far and fades it to match. The
// pixels are offset so the card's far edge travels with the growing edge.
void App::shape_hover() {
    if (!hover_)
        return;
    RECT bounds{};
    GetClientRect(hover_, &bounds);
    const int height = bounds.bottom;
    const int visible = std::max(1, static_cast<int>(std::lround(
        height * (hover_open_start + (1.f - hover_open_start) * hover_progress_))));
    const int top = hover_grows_up_ ? height - visible : 0;
    hover_offset_ = hover_grows_up_ ? top : visible - height;
    round_window(hover_, top, top + visible);
    SetLayeredWindowAttributes(
        hover_, 0,
        static_cast<BYTE>(std::lround(preferences_.appearance.hover_opacity * 255.f / 100.f * hover_progress_)),
        LWA_ALPHA);
}

bool App::animate_hover() {
    const float t = std::min(1.f, std::chrono::duration<float>(std::chrono::steady_clock::now() - hover_opened_) /
                                      std::chrono::duration<float>(hover_open_duration));
    // Ease out: most of the growth early, settling gently into place.
    hover_progress_ = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
    shape_hover();
    InvalidateRect(hover_, nullptr, FALSE);
    return t < 1.f;
}

void App::reset_widget() {
    hide_hover(true);
    if (primary_.window && IsWindow(primary_.window))
        DestroyWindow(primary_.window);
    primary_.window = nullptr;
    primary_.bounds = {};
    sync_secondary({});
}

const TaskbarWidget& App::active() const {
    for (const auto& widget : secondary_)
        if (widget.window && widget.window == active_widget_ && !widget.bounds.empty())
            return widget;
    return primary_;
}

void App::invalidate_widgets() const {
    if (primary_.window)
        InvalidateRect(primary_.window, nullptr, FALSE);
    for (const auto& widget : secondary_)
        if (widget.window)
            InvalidateRect(widget.window, nullptr, FALSE);
}

App::Placement App::place(TaskbarWidget& widget, const Snapshot& snapshot) {
    const auto target = ui::place_widget(preferences_.appearance, snapshot.bounds, snapshot.occupied,
                                         static_cast<float>(snapshot.dpi / 96.0));
    if (target.empty())
        return Placement::NoSpace;
    if (!widget.window) {
        const auto previous = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(snapshot.taskbar));
        widget.window = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, widget_class,
                                        L"UsageTracker taskbar widget", WS_CHILD, 0, 0, target.width,
                                        target.height, snapshot.taskbar, nullptr, GetModuleHandleW(nullptr), this);
        SetThreadDpiAwarenessContext(previous);
        widget.taskbar = snapshot.taskbar;
        // Per-pixel alpha is supplied by paint_widget. Do not call
        // SetLayeredWindowAttributes: it disables UpdateLayeredWindow.
    }
    const bool unchanged = widget.window && IsWindowVisible(widget.window) && target == widget.bounds;
    if (!widget.window ||
        !(unchanged || SetWindowPos(widget.window, HWND_TOP, target.x - snapshot.bounds.x,
                                    target.y - snapshot.bounds.y, target.width, target.height,
                                    SWP_NOACTIVATE | SWP_SHOWWINDOW)))
        return Placement::Failed;
    const bool anchor = &active() == &widget;
    widget.bounds = target;
    if (!unchanged) {
        // A pinned preview follows the widget it hangs from; a pointer card closes.
        if (anchor) {
            if (hover_pinned_)
                show_hover();
            else
                hide_hover();
        }
        InvalidateRect(widget.window, nullptr, FALSE);
    }
    return Placement::Shown;
}

// One widget per other monitor's taskbar while the preference is on. A widget
// whose taskbar went away (a monitor unplugged, Explorer restarted) is
// destroyed; one without room or a fresh layout just hides.
void App::sync_secondary(const std::vector<Snapshot>& snapshots) {
    const bool wanted = preferences_.appearance.all_taskbars;
    for (auto& widget : secondary_) {
        const bool present = std::any_of(snapshots.begin(), snapshots.end(), [&](const Snapshot& snapshot) {
            return snapshot.taskbar == widget.taskbar;
        });
        const bool healthy = widget.window && IsWindow(widget.window) && GetParent(widget.window) == widget.taskbar;
        if (wanted && present && healthy)
            continue;
        if (widget.window && widget.window == active_widget_)
            hide_hover(true);
        if (widget.window && IsWindow(widget.window))
            DestroyWindow(widget.window);
        widget.window = nullptr;
    }
    secondary_.erase(std::remove_if(secondary_.begin(), secondary_.end(),
                                    [](const TaskbarWidget& widget) { return !widget.window; }),
                     secondary_.end());
    if (!wanted)
        return;
    const auto now = std::chrono::steady_clock::now();
    for (const auto& snapshot : snapshots) {
        auto found = std::find_if(secondary_.begin(), secondary_.end(),
                                  [&](const TaskbarWidget& widget) { return widget.taskbar == snapshot.taskbar; });
        const bool fresh = snapshot.error.empty() && now - snapshot.captured <= std::chrono::seconds(5);
        if (found == secondary_.end()) {
            if (!fresh)
                continue;
            secondary_.push_back({snapshot.taskbar, nullptr, {}});
            found = secondary_.end() - 1;
        }
        if (!fresh || place(*found, snapshot) != Placement::Shown) {
            if (found->window == active_widget_ && found->window)
                hide_hover(true);
            if (found->window)
                ShowWindow(found->window, SW_HIDE);
            found->bounds = {};
            // A widget that was never created has nothing to keep.
            if (!found->window)
                secondary_.erase(found);
        }
    }
}

void App::hide_widget() {
    hide_hover(true);
    if (primary_.window)
        ShowWindow(primary_.window, SW_HIDE);
    primary_.bounds = {};
}

void App::tick() {
    const bool app_light = apps_light_theme();
    const auto os_accent = system_accent();
    const bool details_light_changed = details_view_.set_system_light(app_light);
    const bool details_accent_changed = details_view_.set_system_accent(os_accent);
    const bool hover_light_changed = hover_view_.set_system_light(app_light);
    const bool hover_accent_changed = hover_view_.set_system_accent(os_accent);
    menu_view_.set_system_light(app_light);
    menu_view_.set_system_accent(os_accent);
    if ((details_light_changed || details_accent_changed) && IsWindowVisible(popup_))
        render_details(details_pointer_);
    if ((hover_light_changed || hover_accent_changed) && IsWindowVisible(hover_))
        show_hover();
    if (widget_view_.set_system_accent(os_accent))
        invalidate_widgets();
    if (widget_view_.set_system_light(system_light_theme()))
        invalidate_widgets();
    const auto taskbars = reader_.latest();
    const auto& snapshot = taskbars.primary;
    const auto now = std::chrono::steady_clock::now();
    const auto current = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (primary_.window && (!IsWindow(primary_.window) || GetParent(primary_.window) != current))
        reset_widget();
    if (!snapshot.error.empty() || snapshot.taskbar != current || !current) {
        hide_widget();
        set_status(snapshot.error.empty() ? L"Waiting for the Windows taskbar." : snapshot.error);
    } else if (now - snapshot.captured > std::chrono::seconds(5)) {
        hide_widget();
        set_status(L"Taskbar layout is stale. Waiting for Explorer.");
    } else {
        switch (place(primary_, snapshot)) {
        case Placement::Shown:
            set_status(L"Embedded in the primary taskbar");
            break;
        case Placement::NoSpace:
            hide_widget();
            set_status(L"No free taskbar space. Use the tray icon.");
            break;
        case Placement::Failed:
            hide_widget();
            set_status(L"Could not embed the widget; retrying.");
            break;
        }
    }
    reader_.set_secondary(preferences_.appearance.all_taskbars);
    sync_secondary(taskbars.secondary);
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
        // Scale by DPI, as the hover card does, not by the height the widget was
        // rounded to: 38 px rounds to 48 at 125%, and 48 / 38 = 1.263 would
        // bake different text sizes from the card beside it on the same monitor.
        const UINT dpi = GetDpiForWindow(window);
        const float scale = dpi ? dpi / 96.f : static_cast<float>(bounds.bottom) / widget_height;
        renderer_.set_surface(scale, widget_view_.text_gamma());
        if (widget_scale_ != scale) {
            widget_view_.invalidate_measurements();
            widget_scale_ = scale;
        }
        // One view draws every monitor's widget; only the one under the pointer
        // shows the hover highlight.
        widget_view_.set_hovered(hovered_ && window == active().window);
        const auto frame = widget_view_.frame(usage_, {}, bounds.right / scale, bounds.bottom / scale);
        present_layered(window, renderer_.render(frame.commands, bounds.right, bounds.bottom, scale, true));
        ++widget_frames_;
    }
    EndPaint(window, &paint);
}

void App::present_layered(HWND window, const ui::Pixels& pixels) {
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
}

LRESULT CALLBACK App::confetti_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    instance(window, message, l);
    if (message == WM_NCHITTEST)
        return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    return DefWindowProcW(window, message, w, l);
}

// Covers the widget and the space above and beside it, clipped to its monitor,
// and launches the pieces from the widget's middle. A burst during a burst
// adds to it in the same overlay.
void App::celebrate() {
    const auto& widget = active();
    if (!animations_allowed_ || !animations_enabled() || !widget.window || !IsWindowVisible(widget.window) ||
        widget.bounds.empty())
        return;
    const UINT dpi = GetDpiForWindow(widget.window);
    const float scale = dpi ? dpi / 96.f : 1.f;
    if (!confetti_window_) {
        confetti_window_ =
            CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                            confetti_class, L"UsageTracker confetti", WS_POPUP, 0, 0, 0, 0, controller_, nullptr,
                            GetModuleHandleW(nullptr), this);
        if (!confetti_window_)
            return;
    }
    RECT overlay{};
    if (confetti_.done()) {
        RECT anchor{widget.bounds.x, widget.bounds.y, widget.bounds.right(), widget.bounds.bottom()};
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor);
        const auto spread = static_cast<LONG>(std::lround(confetti_spread * scale));
        const auto rise = static_cast<LONG>(std::lround(confetti_rise * scale));
        overlay = {std::max(monitor.rcMonitor.left, anchor.left - spread),
                   std::max(monitor.rcMonitor.top, anchor.top - rise),
                   std::min(monitor.rcMonitor.right, anchor.right + spread),
                   std::min(monitor.rcMonitor.bottom, anchor.bottom)};
        SetWindowPos(confetti_window_, HWND_TOPMOST, overlay.left, overlay.top, overlay.right - overlay.left,
                     overlay.bottom - overlay.top, SWP_NOACTIVATE);
    } else
        GetWindowRect(confetti_window_, &overlay);
    confetti_.burst((widget.bounds.x - overlay.left) / scale, widget.bounds.width / scale,
                    (widget.bounds.y + widget.bounds.height / 2 - overlay.top) / scale, confetti_pieces);
    confetti_frame_ = std::chrono::steady_clock::now();
    animate_confetti();
    ShowWindow(confetti_window_, SW_SHOWNOACTIVATE);
    frames_->start();
}

bool App::animate_confetti() {
    const auto now = std::chrono::steady_clock::now();
    confetti_.step(std::chrono::duration<float>(now - confetti_frame_).count());
    confetti_frame_ = now;
    if (confetti_.done()) {
        ShowWindow(confetti_window_, SW_HIDE);
        return false;
    }
    RECT bounds{};
    GetClientRect(confetti_window_, &bounds);
    if (bounds.right <= 0 || bounds.bottom <= 0)
        return true;
    const UINT dpi = GetDpiForWindow(confetti_window_);
    present_layered(confetti_window_,
                    renderer_.render_confetti(confetti_, bounds.right, bounds.bottom, dpi ? dpi / 96.f : 1.f));
    return true;
}

} // namespace usage::windows
