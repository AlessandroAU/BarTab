#include "windows/app.hpp"
#include "windows/platform.hpp"
#include "windows/window_shape.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace usage::windows {

LRESULT CALLBACK App::widget_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* app = instance(window,message,l);
    if (app) {
        switch (message) {
        case WM_PAINT: app->paint_widget(window); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_MOUSEMOVE:
            if (app->preferences_.appearance.hover_enabled && !app->hovered_ && !(w & (MK_LBUTTON | MK_RBUTTON))) {
                app->hovered_ = true;
                app->widget_view_.set_hovered(true);
                InvalidateRect(window,nullptr,FALSE);
                TRACKMOUSEEVENT tracking{sizeof(tracking),TME_HOVER | TME_LEAVE,window,static_cast<DWORD>(app->preferences_.appearance.hover_delay)};
                TrackMouseEvent(&tracking);
            }
            return 0;
        case WM_MOUSEHOVER: app->show_hover(); return 0;
        case WM_MOUSELEAVE: app->hide_hover(); return 0;
        case WM_LBUTTONDOWN: app->hide_hover(); SetCapture(window); return 0;
        case WM_LBUTTONUP: {
            if (GetCapture() != window) return 0;
            ReleaseCapture();
            RECT bounds{}; GetClientRect(window,&bounds);
            const POINT point{static_cast<short>(LOWORD(l)),static_cast<short>(HIWORD(l))};
            if (PtInRect(&bounds,point)) app->open_details();
            return 0;
        }
        case WM_RBUTTONUP: app->show_menu(); return 0;
        case WM_NCDESTROY:
            app->hide_hover();
            if (app->widget_ == window) { app->widget_ = nullptr; app->widget_bounds_ = {}; }
            break;
        }
    }
    return DefWindowProcW(window,message,w,l);
}

LRESULT CALLBACK App::hover_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* app = instance(window,message,l);
    if (app) {
        if (message == WM_PAINT) { paint_pixels(window,app->hover_pixels_); return 0; }
        if (message == WM_SIZE) { round_window(window); return 0; }
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (message == WM_NCHITTEST) return HTTRANSPARENT;
    }
    return DefWindowProcW(window,message,w,l);
}

void App::hide_hover() {
    if (hover_) ShowWindow(hover_,SW_HIDE);
    if (widget_ && IsWindow(widget_)) {
        TRACKMOUSEEVENT tracking{sizeof(tracking),TME_CANCEL | TME_HOVER | TME_LEAVE,widget_,0};
        TrackMouseEvent(&tracking);
        if (hovered_) InvalidateRect(widget_,nullptr,FALSE);
    }
    hovered_ = false;
    widget_view_.set_hovered(false);
}

void App::show_hover() {
    if (!preferences_.appearance.hover_enabled || !hovered_ || !IsWindowVisible(widget_) || widget_bounds_.empty()) return;
    if (!hover_) {
        hover_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
            hover_class,L"Usage overview",WS_POPUP,0,0,0,0,controller_,nullptr,GetModuleHandleW(nullptr),this);
        if (!hover_) return;

    }
    SetLayeredWindowAttributes(hover_,0,static_cast<BYTE>(std::lround(preferences_.appearance.hover_opacity*255.f/100.f)),LWA_ALPHA);
    const UINT dpi = GetDpiForWindow(widget_);
    const float scale = dpi ? dpi/96.f : 1.f;
    auto logical = ui::hover_size(usage_,preferences_.appearance.text_percent);
    logical.width=preferences_.appearance.hover_width*preferences_.appearance.text_percent/100.f;
    int width = static_cast<int>(std::lround(logical.width*scale));
    const int height = static_cast<int>(std::lround(logical.height*scale));
    RECT anchor{widget_bounds_.x,widget_bounds_.y,widget_bounds_.right(),widget_bounds_.bottom()};
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&anchor,MONITOR_DEFAULTTONEAREST),&monitor);
    width=std::min(width,static_cast<int>(monitor.rcWork.right-monitor.rcWork.left));
    logical.width=width/scale;
    const int x = std::max(static_cast<int>(monitor.rcWork.left),std::min(widget_bounds_.right()-width,static_cast<int>(monitor.rcWork.right)-width));
    const int y = std::max(static_cast<int>(monitor.rcWork.top),std::min(widget_bounds_.y-height-static_cast<int>(8*scale),static_cast<int>(monitor.rcWork.bottom)-height));
    SetWindowPos(hover_,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE);
    renderer_.set_scale(scale);
    hover_view_.invalidate_measurements();
    const auto frame = hover_view_.frame(usage_,{},logical.width,logical.height);
    hover_pixels_ = renderer_.render(frame.commands,width,height,scale,false);
    InvalidateRect(hover_,nullptr,FALSE);
    ShowWindow(hover_,SW_SHOWNOACTIVATE);
}

void App::reset_widget() {
    hide_hover();
    if (widget_ && IsWindow(widget_)) DestroyWindow(widget_);
    widget_ = nullptr;
    widget_bounds_ = {};
}

void App::hide_widget() {
    hide_hover();
    if (widget_) ShowWindow(widget_,SW_HIDE);
    widget_bounds_ = {};
}

void App::tick() {
    if (widget_view_.set_system_light(system_light_theme()) && widget_) InvalidateRect(widget_,nullptr,FALSE);
    const auto snapshot = reader_.latest();
    const auto now = std::chrono::steady_clock::now();
    const auto current = FindWindowW(L"Shell_TrayWnd",nullptr);
    if (widget_ && (!IsWindow(widget_) || GetParent(widget_) != current)) reset_widget();
    if (!snapshot.error.empty() || snapshot.taskbar != current || !current) {
        hide_widget();
        set_status(snapshot.error.empty() ? L"Waiting for the Windows taskbar." : snapshot.error);
    } else if (now - snapshot.captured > std::chrono::seconds(5)) {
        hide_widget(); set_status(L"Taskbar layout is stale. Waiting for Explorer.");
    } else {
        const double scale = snapshot.dpi / 96.0;
        const auto logical=ui::widget_size(usage_,preferences_.appearance);
        auto target = find_space(snapshot.bounds,snapshot.occupied,static_cast<int>(std::lround(logical.width * scale)),
            static_cast<int>(std::lround(widget_height * scale)),static_cast<int>(std::ceil(8 * scale)),
            preferences_.appearance.position);
        if (target.empty()) { hide_widget(); set_status(L"No free taskbar space. Use the tray icon."); }
        else {
            if (!widget_) {
                const auto previous = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(current));
                widget_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,widget_class,L"UsageTracker taskbar widget",
                    WS_CHILD,0,0,target.width,target.height,current,nullptr,GetModuleHandleW(nullptr),this);
                SetThreadDpiAwarenessContext(previous);
                // Per-pixel alpha is supplied by paint_widget. Do not call
                // SetLayeredWindowAttributes: it disables UpdateLayeredWindow.
            }
            const bool unchanged = widget_ && IsWindowVisible(widget_) && target == widget_bounds_;
            if (widget_ && (unchanged || SetWindowPos(widget_,HWND_TOP,target.x-snapshot.bounds.x,target.y-snapshot.bounds.y,
                target.width,target.height,SWP_NOACTIVATE | SWP_SHOWWINDOW))) {
                widget_bounds_ = target;
                if (!unchanged) { hide_hover(); InvalidateRect(widget_,nullptr,FALSE); }
                set_status(L"Embedded in the primary taskbar");
            } else { hide_widget(); set_status(L"Could not embed the widget; retrying."); }
        }
    }
    if (smoke_ && now - started_ >= std::chrono::seconds(8)) finish_smoke_test();
    if (live_test_ && now - started_ >= std::chrono::seconds(8) &&
        ((usage_.account.updated && usage_.claude.updated) || now-started_ >= std::chrono::seconds(25))) finish_live_test();
}

void App::paint_widget(HWND window) {
    PAINTSTRUCT paint{};
    BeginPaint(window,&paint);
    RECT bounds{}; GetClientRect(window,&bounds);
    if (bounds.right > 0 && bounds.bottom > 0) {
        const float scale = static_cast<float>(bounds.bottom) / widget_height;
        renderer_.set_scale(scale);
        if (widget_scale_ != scale) { widget_view_.invalidate_measurements(); widget_scale_ = scale; }
        const auto frame = widget_view_.frame(usage_,{},bounds.right/scale,widget_height);
        const auto pixels = renderer_.render(frame.commands,bounds.right,bounds.bottom,scale,true);
        HDC dc = GetDC(nullptr);
        HDC buffer = CreateCompatibleDC(dc);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = pixels.width;
        info.bmiHeader.biHeight = -pixels.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* bits{};
        HBITMAP bitmap = CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if (bitmap && buffer) {
            auto old = SelectObject(buffer,bitmap);
            std::memcpy(bits,pixels.data.data(),pixels.data.size()*sizeof(std::uint32_t));
            POINT source{};
            SIZE size{pixels.width,pixels.height};
            BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
            UpdateLayeredWindow(window,dc,nullptr,&size,buffer,&source,0,&blend,ULW_ALPHA);
            SelectObject(buffer,old);
        }
        if (bitmap) DeleteObject(bitmap);
        if (buffer) DeleteDC(buffer);
        ReleaseDC(nullptr,dc);
        ++widget_frames_;
    }
    EndPaint(window,&paint);
}

} // namespace usage::windows
