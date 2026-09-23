#include "windows/app.hpp"
#include "windows/platform.hpp"
#include <dwmapi.h>
#include <cmath>
#include <fstream>

namespace usage::windows {

namespace {
bool capture_widget(Rect bounds, const std::filesystem::path& path) {
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, bounds.width, bounds.height);
    auto old = SelectObject(memory, bitmap);
    const bool copied = BitBlt(memory, 0, 0, bounds.width, bounds.height, screen, bounds.x, bounds.y,
                               SRCCOPY | CAPTUREBLT) != FALSE;
    SelectObject(memory, old);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bounds.width;
    info.bmiHeader.biHeight = -bounds.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(bounds.width) * bounds.height * 4);
    const bool read = GetDIBits(memory, bitmap, 0, static_cast<UINT>(bounds.height), pixels.data(), &info,
                                DIB_RGB_COLORS) != 0;
    unsigned accent_pixels = 0;
    for (std::size_t i = 0; read && i < pixels.size(); i += 4) {
        if (std::abs(static_cast<int>(pixels[i + 2]) - accent.r) < 12 &&
            std::abs(static_cast<int>(pixels[i + 1]) - accent.g) < 12 &&
            std::abs(static_cast<int>(pixels[i]) - accent.b) < 12)
            ++accent_pixels;
    }
    const bool visible = copied && accent_pixels > 20;
    bool saved = false;
    if (read) {
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
        file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        saved = file.good();
    }
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return visible && saved;
}
} // namespace

void App::finish_live_test() {
    live_test_ = false;
    KillTimer(controller_, 1);
    const bool codex_ok = !usage_.codex.installed || (usage_.codex.updated && usage_.codex.error.empty());
    const bool claude_ok = !usage_.claude.installed || (usage_.claude.updated && usage_.claude.error.empty());
    const bool connected =
        usage_.live && codex_ok && claude_ok && (usage_.codex.installed || usage_.claude.installed);
    const bool embedded = primary_.window && IsWindowVisible(primary_.window) && !primary_.bounds.empty();
    if (embedded) {
        UpdateWindow(primary_.window);
        DwmFlush();
        capture_widget(primary_.bounds, executable_directory() / L"codex-widget.bmp");
        hovered_ = true;
        show_hover();
        UpdateWindow(hover_);
        DwmFlush();
        if (IsWindowVisible(hover_))
            capture_widget(window_rect(hover_), executable_directory() / L"codex-hover.bmp");
    }
    open_details();
    UpdateWindow(popup_);
    DwmFlush();
    capture_widget(window_rect(popup_), executable_directory() / L"codex-details.bmp");
    std::ofstream report(executable_directory() / L"codex-live-test.txt");
    report << std::boolalpha << "Connected: " << connected << "\nEmbedded: " << embedded << '\n';
    for (const auto& window : usage_.codex.windows)
        report << window.label << ": " << window.remaining << "% remaining; reset " << window.resets_at
               << '\n';
    report << "Codex detected: " << usage_.codex.installed << "\nClaude detected: " << usage_.claude.installed
           << '\n';
    for (const auto& window : usage_.claude.windows)
        report << "Claude " << window.label << ": " << window.remaining << "% remaining; reset "
               << window.resets_at << '\n';
    if (!usage_.claude.error.empty())
        report << "Claude error: " << usage_.claude.error << '\n';
    if (!usage_.codex.error.empty())
        report << "Error: " << usage_.codex.error << '\n';
    PostQuitMessage(connected && embedded && report.good() ? 0 : 1);
}

void App::finish_smoke_test() {
    smoke_ = false;
    KillTimer(controller_, 1);
    const bool embedded = primary_.window && !primary_.bounds.empty() && IsWindowVisible(primary_.window) &&
                          GetParent(primary_.window) == FindWindowW(L"Shell_TrayWnd", nullptr) &&
                          window_rect(primary_.window) == primary_.bounds;
    bool popup = false, sliders = false, visible = false, closed = false, recreated = false, hit_area = false,
         keyboard = false, idle = false;
    bool hover_shown = false, hover_left = false, hover_click = false;
    bool settings_saved = false, settings_cancelled = false, settings_centered = false;
    if (embedded) {
        reset_widget();
        tick();
        recreated = primary_.window && IsWindowVisible(primary_.window) &&
                    GetParent(primary_.window) == FindWindowW(L"Shell_TrayWnd", nullptr);
        UpdateWindow(primary_.window);
        DwmFlush();
        hit_area = recreated;
        // Test OS hit detection, not just direct messages that bypass transparency.
        for (const POINT point : {POINT{2, 2}, POINT{100, 16}, POINT{28, 23}, POINT{205, 35}}) {
            const POINT screen{primary_.bounds.x + MulDiv(point.x, primary_.bounds.width, widget_width),
                               primary_.bounds.y + MulDiv(point.y, primary_.bounds.height, widget_height)};
            hit_area = hit_area && WindowFromPoint(screen) == primary_.window;
        }
        // Exercise the same click handler and slider notification used interactively.
        const auto foreground = GetForegroundWindow();
        SendMessageW(primary_.window, WM_MOUSEMOVE, 0, MAKELPARAM(2, 2));
        hover_shown = IsWindowVisible(hover_) && GetForegroundWindow() == foreground;
        if (hover_shown) {
            UpdateWindow(hover_);
            DwmFlush();
            capture_widget(window_rect(hover_), executable_directory() / L"hover-live.bmp");
        }
        SendMessageW(primary_.window, WM_MOUSELEAVE, 0, 0);
        const bool grace = IsWindowVisible(hover_);
        // Exercise dismissal after the grace period with the pointer off both surfaces.
        POINT saved_pointer{};
        GetCursorPos(&saved_pointer);
        SetCursorPos(0, 0);
        SendMessageW(hover_, WM_TIMER, 1, 0);
        hover_left = grace && !IsWindowVisible(hover_);
        SetCursorPos(saved_pointer.x, saved_pointer.y);
        SendMessageW(primary_.window, WM_MOUSEMOVE, 0, MAKELPARAM(2, 2));
        SendMessageW(primary_.window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
        SendMessageW(primary_.window, WM_LBUTTONUP, 0, MAKELPARAM(2, 2));
        hover_click = !IsWindowVisible(hover_);
        popup = popup_ && IsWindowVisible(popup_);
        if (popup) {
            UpdateWindow(popup_);
            DwmFlush();
            capture_widget(window_rect(popup_), executable_directory() / L"details-live.bmp");
            // Test the real Clay hit regions through native pointer messages.
            const auto click_slider = [&](const char* name, float fraction) {
                const auto rect = details_view_.bounds(name);
                const auto scale = popup_scale();
                const auto x = static_cast<int>((rect.x + rect.width * fraction) * scale);
                const auto y = static_cast<int>((rect.y + rect.height / 2) * scale);
                SendMessageW(popup_, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                SendMessageW(popup_, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
                SendMessageW(popup_, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            };
            click_slider("SessionSlider", 0.15f);
            click_slider("WeeklySlider", 0.30f);
            sliders = std::abs(usage_.session() - 15) <= 1 && std::abs(usage_.weekly() - 30) <= 1;
            const int weekly_before = usage_.weekly();
            SendMessageW(popup_, WM_KEYDOWN, VK_RIGHT, 0);
            keyboard = usage_.weekly() == weekly_before + 1;
            click_slider("SessionSlider", 0.62f);
            click_slider("WeeklySlider", 0.81f);
            const auto close = details_view_.bounds("Close");
            const int cx = static_cast<int>((close.x + close.width / 2) * popup_scale());
            const int cy = static_cast<int>((close.y + close.height / 2) * popup_scale());
            SendMessageW(popup_, WM_MOUSEMOVE, 0, MAKELPARAM(cx, cy));
            SendMessageW(popup_, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cx, cy));
            SendMessageW(popup_, WM_LBUTTONUP, 0, MAKELPARAM(cx, cy));
            closed = !IsWindowVisible(popup_);
        }
        UpdateWindow(primary_.window);
        DwmFlush();
        const unsigned before = widget_frames_ + details_frames_;
        tick();
        UpdateWindow(primary_.window);
        idle = widget_frames_ + details_frames_ == before;
        visible = capture_widget(primary_.bounds, executable_directory() / L"widget-live.bmp");
    }
    open_details(true);
    if (IsWindowVisible(popup_)) {
        const auto bounds = window_rect(popup_);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(popup_, MONITOR_DEFAULTTONEAREST), &monitor);
        settings_centered =
            std::abs(bounds.x * 2 + bounds.width - monitor.rcWork.left - monitor.rcWork.right) <= 1 &&
            std::abs(bounds.y * 2 + bounds.height - monitor.rcWork.top - monitor.rcWork.bottom) <= 1;
        UpdateWindow(popup_);
        DwmFlush();
        capture_widget(bounds, executable_directory() / L"settings-live.bmp");
        SendMessageW(popup_, WM_KEYDOWN, VK_TAB, 0);
        SendMessageW(popup_, WM_KEYDOWN, VK_END, 0);
        SendMessageW(popup_, WM_KEYDOWN, VK_HOME, 0);
        // Seven 5% steps up from the 65% minimum land back on the 100% default.
        for (int i = 0; i < 7; ++i)
            SendMessageW(popup_, WM_KEYDOWN, VK_RIGHT, 0);
        // Focus order: taskbar text, hover text, reset, save.
        SendMessageW(popup_, WM_KEYDOWN, VK_TAB, 0);
        SendMessageW(popup_, WM_KEYDOWN, VK_TAB, 0);
        SendMessageW(popup_, WM_KEYDOWN, VK_TAB, 0);
        SendMessageW(popup_, WM_KEYDOWN, VK_RETURN, 0);
        settings_saved =
            !IsWindowVisible(popup_) && preferences_.appearance.text_percent == 100 &&
            GetPrivateProfileIntW(L"Appearance", L"TextSize", 0, settings_path_.c_str()) == 100 &&
            GetPrivateProfileIntW(L"Appearance", L"HoverTextSize", 0, settings_path_.c_str()) == 100;
        open_details(true);
        SendMessageW(popup_, WM_KEYDOWN, VK_TAB, 0);
        SendMessageW(popup_, WM_KEYDOWN, VK_HOME, 0);
        SendMessageW(popup_, WM_KEYDOWN, VK_ESCAPE, 0);
        open_details(true);
        settings_cancelled =
            preferences_.appearance.text_percent == 100 && details_view_.text_percent() == 100;
        close_details();
    }
    // Exercise the normal unified panel with deterministic provider data.
    usage_.live = true;
    usage_.codex.windows = {{"5 hour", 62, 0}, {"Weekly", 81, 0}};
    usage_.codex.plan = "Demo plan";
    usage_.codex.executable_path = "C:/Demo/Codex/codex.exe";
    usage_.codex.updated = std::time(nullptr);
    usage_.claude.installed = true;
    usage_.claude.windows = {{"5 hour", 97, 0}, {"Weekly", 44, 0}, {"Fable weekly", 23, 0}};
    usage_.claude.executable_path = "C:/Demo/Claude/claude.exe";
    usage_.claude.updated = std::time(nullptr);
    open_details();
    // The panel shows one sidebar page at a time; these switch pages and move
    // keyboard focus the way a user would, without counting tab stops.
    const auto show_page = [&](ui::SettingsPage page) {
        details_view_.set_settings_page(page);
        render_details(details_pointer_);
    };
    const auto tab_to = [&](const char* name) {
        for (int i = 0; i < 32 && !details_view_.focused(name); ++i)
            SendMessageW(popup_, WM_KEYDOWN, VK_TAB, 0);
        return details_view_.focused(name);
    };
    show_page(ui::SettingsPage::Taskbar);
    bool unified = settings_mode_ && details_view_.bounds("TextSizeSlider").width > 0 &&
                   details_view_.bounds("BoldTaskbar").width > 0;
    show_page(ui::SettingsPage::Hover);
    unified = unified && details_view_.bounds("HoverTextSizeSlider").width > 0 &&
              details_view_.bounds("BoldHover").width > 0;
    show_page(ui::SettingsPage::General);
    unified = unified && details_view_.bounds("BoldSettings").width > 0;
    show_page(ui::SettingsPage::Providers);
    unified = unified && details_view_.bounds("LiveUsageCodex").width > 0 &&
              details_view_.bounds("LiveUsageClaude").width > 0 &&
              details_view_.bounds("RefreshUsage").height > 0;
    show_page(ui::SettingsPage::Taskbar);
    unified = tab_to("TextSizeSlider") && unified;
    SendMessageW(popup_, WM_KEYDOWN, VK_HOME, 0);
    open_details(true);
    const bool unified_edits = details_view_.text_percent() == 65 &&
                               preferences_.appearance.text_percent == 65 &&
                               widget_view_.text_percent() == 65 && hover_view_.text_percent() == 65 &&
                               hover_view_.hover_text_percent() == 100;
    // Settings pin the hover card open as a live preview without taking focus from the panel.
    RECT pinned_card{}, pinned_popup{};
    const bool hover_pinned = hover_pinned_ && IsWindowVisible(hover_) && GetWindowRect(hover_, &pinned_card) &&
                              GetWindowRect(popup_, &pinned_popup) && GetForegroundWindow() == popup_;
    RECT pinned_overlap{};
    const bool hover_clear = hover_pinned && !IntersectRect(&pinned_overlap, &pinned_card, &pinned_popup);
    UpdateWindow(popup_);
    DwmFlush();
    capture_widget(window_rect(popup_), executable_directory() / L"unified-settings.bmp");
    close_details();
    const bool hover_unpinned = !hover_pinned_ && !IsWindowVisible(hover_);
    open_details();
    const bool unified_cancel = details_view_.text_percent() == 100 &&
                                preferences_.appearance.text_percent == 100 &&
                                widget_view_.text_percent() == 100 && hover_view_.text_percent() == 100;
    close_details();
    const auto foreground = GetForegroundWindow();
    hovered_ = true;
    show_hover();
    BYTE opacity{};
    DWORD layered_flags{};
    const bool compact_hover = IsWindowVisible(hover_) && GetForegroundWindow() == foreground &&
                               hover_view_.bounds("HoverCodex").width > 0 &&
                               hover_view_.bounds("HoverClaude").width > 0 &&
                               GetLayeredWindowAttributes(hover_, nullptr, &opacity, &layered_flags) &&
                               opacity == 242 && (layered_flags & LWA_ALPHA);
    UpdateWindow(hover_);
    DwmFlush();
    capture_widget(window_rect(hover_), executable_directory() / L"combined-hover.bmp");
    hide_hover();
    open_details();
    const auto click_setting = [&](const char* name) {
        const auto bounds = details_view_.bounds(name);
        const auto point = MAKELPARAM(static_cast<int>((bounds.x + bounds.width / 2) * popup_scale()),
                                      static_cast<int>((bounds.y + bounds.height / 2) * popup_scale()));
        SendMessageW(popup_, WM_MOUSEMOVE, 0, point);
        SendMessageW(popup_, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(popup_, WM_LBUTTONUP, 0, point);
    };
    const bool font_preview = details_view_.bounds("FontChoice").width == 0;
    const bool theme_preview = details_view_.bounds("ThemeChoice").width == 0;
    show_page(ui::SettingsPage::Providers);
    click_setting("CodexInterval");
    UpdateWindow(popup_);
    DwmFlush();
    capture_widget(window_rect(popup_), executable_directory() / L"interval-dropdown.bmp");
    SendMessageW(popup_, WM_KEYDOWN, VK_END, 0);
    SendMessageW(popup_, WM_KEYDOWN, VK_RETURN, 0);
    const bool interval_preview = preferences_.codex_interval == 900 && preferences_.claude_interval == 60;
    click_setting("ResetAppearance");
    const bool appearance_reset =
        preferences_.appearance == Appearance{} && preferences_.codex_interval == 900;
    UpdateWindow(popup_);
    DwmFlush();
    capture_widget(window_rect(popup_), executable_directory() / L"settings-default.bmp");
    click_setting("EnableCodex");
    if (primary_.window)
        UpdateWindow(primary_.window);
    const bool provider_preview =
        !usage_.codex_enabled && usage_.claude_enabled && widget_view_.bounds("ClaudeGeneral").width > 0 &&
        widget_view_.bounds("ClaudeFable").width > 0 &&
        GetPrivateProfileIntW(L"Providers", L"Codex", 0, settings_path_.c_str()) == 1;
    const auto preview_foreground = GetForegroundWindow();
    hovered_ = true;
    show_hover();
    const bool hover_preview =
        IsWindowVisible(hover_) && IsWindowVisible(popup_) && GetForegroundWindow() == preview_foreground;
    hide_hover();
    click_setting("SaveSettings");
    const bool providers_saved =
        !usage_.codex_enabled && usage_.claude_enabled &&
        GetPrivateProfileIntW(L"Providers", L"Codex", 1, settings_path_.c_str()) == 0 &&
        GetPrivateProfileIntW(L"Providers", L"Claude", 0, settings_path_.c_str()) == 1 &&
        GetPrivateProfileIntW(L"Providers", L"CodexInterval", 0, settings_path_.c_str()) == 900 &&
        GetPrivateProfileIntW(L"Appearance", L"HoverOpacity", 0, settings_path_.c_str()) == 95;
    open_details();
    show_page(ui::SettingsPage::Providers);
    click_setting("EnableClaude");
    const bool all_disabled_preview = !usage_.codex_enabled && !usage_.claude_enabled;
    SendMessageW(popup_, WM_KEYDOWN, VK_ESCAPE, 0);
    open_details();
    show_page(ui::SettingsPage::Providers);
    const bool retained_detection =
        details_view_.bounds("EnableCodex").width > 0 && !usage_.codex_enabled && usage_.codex.installed;
    click_setting("ClaudeInterval");
    SendMessageW(popup_, WM_KEYDOWN, VK_END, 0);
    SendMessageW(popup_, WM_KEYDOWN, VK_RETURN, 0);
    SendMessageW(popup_, WM_CLOSE, 0, 0);
    open_details();
    const bool full_cancel = preferences_.appearance == Appearance{} && preferences_.claude_interval == 60 &&
                             preferences_.codex_interval == 900;
    const bool providers_cancelled = !details_view_.codex_enabled() && details_view_.claude_enabled() &&
                                     !usage_.codex_enabled && usage_.claude_enabled;
    close_details();
    usage_.claude.windows[1].resets_at = 1790583271;
    usage_.claude.windows[2].resets_at = 1790669671;
    tick();
    if (primary_.window) {
        InvalidateRect(primary_.window, nullptr, FALSE);
        UpdateWindow(primary_.window);
        DwmFlush();
    }
    const bool claude_split =
        widget_view_.bounds("ClaudeGeneral").width > 0 && widget_view_.bounds("ClaudeFable").width > 0;
    capture_widget(primary_.bounds, executable_directory() / L"claude-only-taskbar.bmp");
    open_details();
    show_page(ui::SettingsPage::Taskbar);
    click_setting("WidgetWidth");
    SendMessageW(popup_, WM_KEYDOWN, VK_HOME, 0);
    if (primary_.window)
        UpdateWindow(primary_.window);
    hovered_ = true;
    show_hover();
    UpdateWindow(hover_);
    DwmFlush();
    const bool smaller_widths =
        preferences_.appearance.widget_width == 100 &&
        primary_.bounds.width == MulDiv(100, GetDpiForWindow(primary_.window), 96) && IsWindowVisible(hover_);
    capture_widget(primary_.bounds, executable_directory() / L"small-widget.bmp");
    capture_widget(window_rect(hover_), executable_directory() / L"small-hover.bmp");
    close_details();
    std::ofstream report(executable_directory() / L"smoke-test.txt");
    report << std::boolalpha << "Embedded: " << embedded << "\nPopup: " << popup
           << "\nSlider updates: " << sliders << "\nKeyboard: " << keyboard
           << "\nNo redraw on idle tick: " << idle << "\nClose button: " << closed
           << "\nChild window recreated: " << recreated << "\nFull rectangular hit area: " << hit_area
           << "\nSmaller width preview: " << smaller_widths << "\nTheme dropdown preview: " << theme_preview
           << "\nFont preview: " << font_preview << "\nInterval preview: " << interval_preview
           << "\nAppearance reset: " << appearance_reset << "\nFull cancel: " << full_cancel
           << "\nDisabled provider detection retained: " << retained_detection
           << "\nProvider preview: " << provider_preview << "\nHover during preview: " << hover_preview
           << "\nAll-disabled preview: " << all_disabled_preview
           << "\nProvider preferences saved: " << providers_saved
           << "\nProvider cancel: " << providers_cancelled << "\nClaude split bars: " << claude_split
           << "\nCompact translucent hover: " << compact_hover << "\nUnified panel: " << unified
           << "\nHover pinned during settings: " << hover_pinned << "\nPinned hover clear of settings: "
           << hover_clear << "\nHover unpinned on close: " << hover_unpinned
           << "\nUnified edits retained: " << unified_edits << "\nUnified cancel: " << unified_cancel
           << "\nSettings centered: " << settings_centered << "\nSettings saved: " << settings_saved
           << "\nSettings cancel: " << settings_cancelled << "\nHover without activation: " << hover_shown
           << "\nHover dismissed on leave: " << hover_left << "\nHover dismissed on click: " << hover_click
           << "\nVisible bar pixels: " << visible << "\nBounds: " << primary_.bounds.x << ','
           << primary_.bounds.y << ' ' << primary_.bounds.width << 'x' << primary_.bounds.height << '\n';
    const bool passed = embedded && popup && sliders && closed && recreated && hit_area && keyboard && idle &&
                        visible && hover_shown && hover_left && hover_click && settings_centered &&
                        hover_pinned && hover_clear && hover_unpinned &&
                        settings_saved && settings_cancelled && unified && unified_edits && unified_cancel &&
                        compact_hover && providers_saved && providers_cancelled && claude_split &&
                        provider_preview && hover_preview && all_disabled_preview && font_preview &&
                        interval_preview && appearance_reset && full_cancel && retained_detection &&
                        smaller_widths && theme_preview && report.good();
    report.close();
    PostQuitMessage(passed ? 0 : 1);
}

} // namespace usage::windows
