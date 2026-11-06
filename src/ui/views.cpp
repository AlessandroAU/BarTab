#include "ui/views.hpp"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <ctime>

namespace usage::ui {
namespace {
constexpr uint16_t settings_body = 18;
constexpr uint16_t settings_help = 16;
Clay_String string(const char* text) {
    return {false, static_cast<int32_t>(std::strlen(text)), text};
}
Clay_ElementId id(const char* text) {
    return Clay_GetElementId(string(text));
}
Clay_Color color(Color value) {
    return {static_cast<float>(value.r), static_cast<float>(value.g), static_cast<float>(value.b), 255};
}
Clay_ElementDeclaration column(uint16_t padding, uint16_t gap) {
    Clay_ElementDeclaration result{};
    result.layout.sizing.width = CLAY_SIZING_GROW(0);
    result.layout.sizing.height = CLAY_SIZING_FIT(0);
    result.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    result.layout.padding = CLAY_PADDING_ALL(padding);
    result.layout.childGap = gap;
    return result;
}
std::string format_date(std::int64_t timestamp, bool twelve_hour) {
    if (timestamp <= 0)
        return std::string("Unavailable");
    const auto time = static_cast<std::time_t>(timestamp);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &time))
        return std::string("Unavailable");
#else
    if (!localtime_r(&time, &local))
        return std::string("Unavailable");
#endif
    char buffer[40]{};
    std::strftime(buffer, sizeof(buffer), twelve_hour ? "%d %b %I:%M %p" : "%d %b %H:%M", &local);
    return std::string(buffer);
}

std::string format_hover_reset(std::int64_t timestamp, std::int64_t now, bool twelve_hour) {
    if (timestamp <= 0)
        return "Reset unavailable";
    const auto seconds = timestamp - now;
    if (seconds <= 0)
        return "Reset due - awaiting update";
    if (seconds < 86400) {
        const auto minutes = (seconds + 59) / 60;
        return "Resets in " + (minutes >= 60 ? std::to_string(minutes / 60) + "h " : "") +
               std::to_string(minutes % 60) + "m";
    }
    const auto time = static_cast<std::time_t>(timestamp);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &time))
        return "Reset unavailable";
#else
    if (!localtime_r(&time, &local))
        return "Reset unavailable";
#endif
    char buffer[40]{};
    std::strftime(buffer, sizeof(buffer), seconds < 7 * 86400 ? (twelve_hour ? "%a, %I:%M %p" : "%a, %H:%M") : (twelve_hour ? "%d %b, %I:%M %p" : "%d %b, %H:%M"), &local);
    return std::string("Resets ") + buffer;
}
std::string freshness(std::int64_t updated, std::int64_t now) {
    if (updated <= 0)
        return "Not updated yet";
    const auto minutes = std::max<std::int64_t>(0, now - updated) / 60;
    if (!minutes)
        return "Updated just now";
    return "Updated " + std::to_string(minutes < 60 ? minutes : minutes < 1440 ? minutes / 60 : minutes / 1440) +
           (minutes < 60 ? "m ago" : minutes < 1440 ? "h ago" : "d ago");
}

std::string format_reset_time(std::int64_t timestamp, bool date_only, bool day_key, bool twelve_hour) {
    if (timestamp <= 0)
        return "?";
    const auto time = static_cast<std::time_t>(timestamp);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &time))
        return "?";
#else
    if (!localtime_r(&time, &local))
        return "?";
#endif
    char buffer[24]{};
    std::strftime(buffer, sizeof(buffer), day_key ? "%Y-%m-%d" : date_only ? "%d/%m" : (twelve_hour ? "%d/%m %I:%M %p" : "%d/%m %H:%M"), &local);
    return buffer;
}

// One line per setting: the label, the control, and for sliders the value, so a
// page's rows share their columns and read as a table.
constexpr float settings_label_width = 150;
constexpr float settings_value_width = 100;
Clay_ElementDeclaration setting_row() {
    auto row = column(0, 16);
    row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    row.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    row.layout.sizing.height = CLAY_SIZING_FIT(32);
    return row;
}
Clay_ElementDeclaration fixed_cell(float width, Clay_LayoutAlignmentX align = CLAY_ALIGN_X_LEFT) {
    Clay_ElementDeclaration cell{};
    cell.layout.sizing.width = CLAY_SIZING_FIXED(width);
    cell.layout.childAlignment.x = align;
    cell.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    return cell;
}
} // namespace
std::string View::date(std::int64_t timestamp) const {
    return format_date(timestamp, preferences_.appearance.twelve_hour_time);
}
std::string View::hover_reset(std::int64_t timestamp, std::int64_t now) const {
    return format_hover_reset(timestamp, now, preferences_.appearance.twelve_hour_time);
}
std::string View::window_reset(const Allowance& window, bool date_only) const {
    if (window.label != "5 hour")
        return reset_time(window.resets_at, date_only);
    return window.resets_at > 0 ? reset_time(window.resets_at).substr(6) : std::string{};
}
std::string View::taskbar_reset(std::int64_t timestamp, std::int64_t now) const {
    auto text = hover_reset(timestamp, now);
    constexpr std::string_view prefix = "Resets ";
    if (text.compare(0, prefix.size(), prefix) == 0)
        text.erase(0, prefix.size());
    return text;
}
std::string View::reset_time(std::int64_t timestamp, bool date_only, bool day_key) const {
    return format_reset_time(timestamp, date_only, day_key, preferences_.appearance.twelve_hour_time);
}
Clay_Color View::accent_color() const {
    return color(system_accent_);
}
bool View::light_theme() const {
    return system_light_;
}
Clay_Color View::text_color(Clay_Color tint) const {
    if (!light_theme()) return tint;
    // Keep provider and semantic colors; translate the neutral dark-theme text palette.
    if (tint.r == 236 && tint.g == 236) return {35, 35, 40, 255};
    if (tint.r > 230 && tint.g > 230 && tint.b > 230) return {28, 28, 28, 255};
    if (tint.b > tint.r && tint.g >= 150) return {82, 88, 96, 255};
    if (tint.r == 240 && tint.g == 180) return {145, 86, 0, 255};
    return tint;
}
Clay_Color View::background_color() const {
    if (light_theme()) return {243, 243, 243, 255};
    return {32, 32, 32, 255};
}
Clay_Color View::provider_color(const char* name, bool secondary) const {
    if (std::strstr(name, "Claude"))
        return secondary ? Clay_Color{232, 171, 145, 255} : Clay_Color{217, 119, 87, 255};
    if (std::strstr(name, "Codex"))
        return light_theme() ? Clay_Color{35, 35, 40, 255}
                                                            : Clay_Color{236, 236, 241, 255};
    return accent_color();
}
void View::wrapped_text(const std::string& value, uint16_t size, Clay_Color tint) {
    Clay_TextElementConfig config{};
    config.fontSize = size;
    config.fontId = font_id();    config.textColor = text_color(tint);
    config.wrapMode = CLAY_TEXT_WRAP_WORDS;
    CLAY_TEXT(string(label(value)), config);
}
bool View::setting_slider(const char* name, const char* title, int& value, SettingRange range,
                          const char* unit) {
    bool changed = false;
    CLAY_AUTO_ID (setting_row()) {
        CLAY_AUTO_ID (fixed_cell(settings_label_width)) {
            text(title, settings_body, {236, 243, 250, 255});
        }
        float current = static_cast<float>(value);
        ClayWidgets_SliderOptions options{};
        options.minValue = static_cast<float>(range.min);
        options.maxValue = static_cast<float>(range.max);
        options.step = static_cast<float>(range.step);
        options.showThumb = true;
        if (ClayWidgets_Slider(widgets_.get(), id(name), &current, options)) {
            value = static_cast<int>(std::lround(current));
            changed = true;
        }
        CLAY_AUTO_ID (fixed_cell(settings_value_width, CLAY_ALIGN_X_RIGHT)) {
            text(label(std::to_string(value) + unit), settings_help, {157, 174, 193, 255});
        }
    }
    return changed;
}
bool View::setting_toggle(const char* name, const char* title, bool& value) {
    bool changed = false;
    CLAY_AUTO_ID (setting_row()) {
        text(title, settings_body, {236, 243, 250, 255});
        Clay_ElementDeclaration spacer{};
        spacer.layout.sizing.width = CLAY_SIZING_GROW(0);
        CLAY_AUTO_ID (spacer) {}
        changed = ClayWidgets_Toggle(widgets_.get(), id(name), CLAY_STRING(""), &value);
    }
    return changed;
}
void View::settings_section(const char* title, bool divider) {
    if (divider) {
        auto line = column(0, 0);
        line.layout.sizing.height = CLAY_SIZING_FIXED(1);
        line.backgroundColor = widgets_->theme.borderColor;
        CLAY_AUTO_ID (line) {}
    }
    // Always the bold face, so sections stand apart from their rows whichever
    // weight the rows use.
    Clay_TextElementConfig config{};
    config.fontId = bold_font;
    config.fontSize = settings_body;
    config.textColor = text_color({236, 243, 250, 255});
    CLAY_TEXT(string(title), config);
}
// A page is its title row, then its controls in one scrolling surface; `action`
// is an optional button for the title row's right edge.
bool View::begin_settings_page(const char* title, const char* scroll_id, const char* action) {
    bool pressed = false;
    auto heading = column(0, 8);
    heading.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    heading.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    heading.layout.sizing.height = CLAY_SIZING_FIXED(44);
    CLAY_AUTO_ID (heading) {
        text(title, 21, {236, 243, 250, 255});
        if (action) {
            Clay_ElementDeclaration spacer{};
            spacer.layout.sizing.width = CLAY_SIZING_GROW(0);
            CLAY_AUTO_ID (spacer) {}
            pressed = ClayWidgets_Button(widgets_.get(), id("RefreshUsage"), string(action));
        }
    }
    ClayWidgets_ScrollPanelOptions scroll{};
    scroll.width = CLAY_SIZING_GROW(0);
    scroll.height = CLAY_SIZING_GROW(0);
    scroll.padding = 16;
    scroll.childGap = 12;
    scroll.fadeMargin = 8;
    ClayWidgets_BeginScrollPanel(widgets_.get(), id(scroll_id), scroll);
    return pressed;
}
void View::taskbar_settings(Frame& result) {
    auto& a = preferences_.appearance;
    begin_settings_page(features_.taskbar ? "Taskbar" : "Widget", "TaskbarScroll");
    settings_section("Text", false);
    result.changed = setting_slider("TextSizeSlider", "Text size", a.text_percent,
                                    preference_limits::text_percent, "%") || result.changed;
    result.changed = setting_toggle("BoldTaskbar", "Bold text", a.bold_taskbar) || result.changed;
    result.changed = setting_toggle("ShowResets", "Show reset dates", a.show_resets) || result.changed;
    settings_section(features_.taskbar ? "Size and position" : "Size", true);
    result.changed = setting_slider("WidgetWidth", "Widget width", a.widget_width,
                                    preference_limits::widget_width, " px") || result.changed;
    // A floating widget is placed by dragging it, and there is no taskbar per monitor.
    if (features_.taskbar) {
        result.changed = setting_slider("WidgetPosition", "Position", a.position, preference_limits::position,
                                        "% from left") ||
                         result.changed;
        result.changed =
            setting_toggle("AllTaskbars", "Show on every monitor", a.all_taskbars) || result.changed;
    } else {
        // A floating widget can be sized to sit in a desktop panel.
        result.changed = setting_slider("WidgetHeight", "Widget height", a.widget_height,
                                        preference_limits::widget_height, " px") ||
                         result.changed;
        wrapped_text(
            "Drag the widget onto a desktop panel to dock it there; it then takes the panel's height.",
            settings_help, {166, 187, 208, 255});
    }
    result.changed = setting_slider("BarHeight", "Bar thickness", a.bar_height, preference_limits::bar_height,
                                    " px") || result.changed;
    // A taskbar is the widget's background; a floating widget draws its own.
    if (!features_.taskbar) {
        settings_section("Background", true);
        result.changed = setting_slider("WidgetOpacity", "Opacity", a.widget_opacity,
                                        preference_limits::widget_opacity, "%") ||
                         result.changed;
    }
    ClayWidgets_EndScrollPanel(widgets_.get(), CLAY_ID("TaskbarScroll"));
}
void View::hover_settings(Frame& result) {
    auto& a = preferences_.appearance;
    begin_settings_page("Hover card", "HoverScroll");
    result.changed = setting_toggle("HoverEnabled", "Show hover card", a.hover_enabled) || result.changed;
    settings_section("Appearance", true);
    result.changed = setting_slider("HoverTextSizeSlider", "Text size", a.hover_text_percent,
                                    preference_limits::text_percent, "%") || result.changed;
    result.changed = setting_toggle("BoldHover", "Bold text", a.bold_hover) || result.changed;
    result.changed = setting_slider("HoverOpacity", "Opacity", a.hover_opacity,
                                    preference_limits::hover_opacity, "%") || result.changed;
    wrapped_text(std::string("While settings are open the hover card stays pinned beside the ") +
                     (features_.taskbar ? "taskbar" : "widget") + ", so these changes preview live.",
                 settings_help, {166, 187, 208, 255});
    ClayWidgets_EndScrollPanel(widgets_.get(), CLAY_ID("HoverScroll"));
}
void View::general_settings(Frame& result) {
    auto& a = preferences_.appearance;
    begin_settings_page("General", "GeneralScroll");
    settings_section("Settings window", false);
    result.changed = setting_toggle("BoldSettings", "Bold text", a.bold_settings) || result.changed;
    settings_section("Time", true);
    CLAY_AUTO_ID (setting_row()) {
        CLAY_AUTO_ID (fixed_cell(settings_label_width)) {
            text("Time format", settings_body, {236, 243, 250, 255});
        }
        Clay_ElementDeclaration spacer{};
        spacer.layout.sizing.width = CLAY_SIZING_GROW(0);
        CLAY_AUTO_ID (spacer) {}
        CLAY_AUTO_ID (fixed_cell(200)) {
            Clay_String time_formats[] = {CLAY_STRING("24-hour"), CLAY_STRING("12-hour (AM/PM)")};
            int32_t time_format = a.twelve_hour_time ? 1 : 0;
            if (ClayWidgets_Combo(widgets_.get(), CLAY_ID("TimeFormat"), CLAY_STRING(""), time_formats, 2,
                                  &time_format)) {
                a.twelve_hour_time = time_format == 1;
                result.changed = true;
            }
        }
    }
    wrapped_text(std::string("Reset times on the ") + (features_.taskbar ? "taskbar" : "widget") +
                     ", the hover card and here use this format. Colors, accent and font family follow " +
                     features_.system_name + " settings.",
                 settings_help, {166, 187, 208, 255});
    update_settings(result);
    settings_section("Reset", true);
    CLAY_AUTO_ID (setting_row()) {
        if (ClayWidgets_Button(widgets_.get(), CLAY_ID("ResetAll"), CLAY_STRING("Reset all settings"))) {
            preferences_ = Preferences{};
            result.changed = result.reset_all = true;
        }
    }
    wrapped_text(std::string("Restores every setting") +
                     (features_.taskbar ? "" : " and the widget's place") +
                     " to its default. Save keeps the reset; Cancel undoes it.",
                 settings_help, {166, 187, 208, 255});
    ClayWidgets_EndScrollPanel(widgets_.get(), CLAY_ID("GeneralScroll"));
}
void View::update_settings(Frame& result) {
    using update::State;
    const auto& status = update_status_;
    if (status.state == State::Unavailable)
        return;
    settings_section("Updates", true);
    result.changed =
        setting_toggle("CheckUpdates", "Check for updates", preferences_.check_updates) || result.changed;
    const bool disabled = !preferences_.check_updates && ClayWidgets_BeginDisabled(widgets_.get());
    result.changed = setting_toggle("InstallUpdates", "Install updates automatically",
                                    preferences_.install_updates) ||
                     result.changed;
    if (disabled)
        ClayWidgets_EndDisabled(widgets_.get());
    const auto current = "Version " + status.current;
    std::string line;
    const char* action = nullptr;
    switch (status.state) {
    case State::Unavailable:
    case State::Off:
    case State::Idle:
    case State::Failed:
        line = current + ".";
        action = "Check now";
        break;
    case State::Checking:
        line = current + ". Checking for updates...";
        break;
    case State::UpToDate:
        line = current + " is the latest.";
        action = "Check now";
        break;
    case State::Available:
        line = "Version " + status.latest + " is available. You have " + status.current + ".";
        action = "Install and restart";
        break;
    case State::Downloading:
        line = "Downloading version " + status.latest + "...";
        break;
    case State::Ready:
        line = "Version " + status.latest + " is downloaded and verified.";
        action = "Restart to update";
        break;
    }
    CLAY_AUTO_ID (setting_row()) {
        text(label(line), settings_body, {236, 243, 250, 255});
        Clay_ElementDeclaration spacer{};
        spacer.layout.sizing.width = CLAY_SIZING_GROW(0);
        CLAY_AUTO_ID (spacer) {}
        if (action && ClayWidgets_Button(widgets_.get(), CLAY_ID("UpdateAction"), string(action))) {
            if (status.state == State::Available || status.state == State::Ready)
                result.install_update = true;
            else
                result.check_updates = true;
        }
    }
    if (!status.error.empty())
        wrapped_text(status.error, settings_help, {240, 180, 90, 255});
    wrapped_text("BarTab asks GitHub for its latest release once a day. Every update is signed, and BarTab "
                 "checks the signature before installing it. Installing restarts BarTab and discards "
                 "unsaved changes here.",
                 settings_help, {166, 187, 208, 255});
}
bool View::interval_dropdown(const char* name, int& seconds) {
    std::vector<int> values{15, 30, 60, 120, 300, 600, 900};
    // Keep existing non-preset intervals intact until the user chooses a value.
    if (std::find(values.begin(), values.end(), seconds) == values.end()) {
        values.push_back(seconds);
        std::sort(values.begin(), values.end());
    }
    std::vector<Clay_String> items;
    for (const auto value : values) {
        const auto caption = value % 60 == 0
                                 ? std::to_string(value / 60) + (value == 60 ? " minute" : " minutes")
                                 : std::to_string(value) + " seconds";
        items.push_back(string(label(caption)));
    }
    int32_t selected =
        static_cast<int32_t>(std::find(values.begin(), values.end(), seconds) - values.begin());
    bool changed = false;
    CLAY_AUTO_ID (setting_row()) {
        CLAY_AUTO_ID (fixed_cell(card_label_width())) {
            text("Update every", settings_body, {210, 220, 234, 255});
        }
        changed = ClayWidgets_Combo(widgets_.get(), id(name), CLAY_STRING(""), items.data(),
                                    static_cast<int32_t>(items.size()), &selected);
    }
    if (changed)
        seconds = values[static_cast<std::size_t>(selected)];
    return changed;
}
// The provider cards' dropdown labels share the widest one's width, so their
// dropdowns start on the same x.
float View::card_label_width() {
    return text_width("Update every", settings_body, 100);
}
bool View::measure_dropdown(const char* name, bool& used) {
    Clay_String items[] = {CLAY_STRING("Usage remaining"), CLAY_STRING("Usage used")};
    int32_t selected = used ? 1 : 0;
    bool changed = false;
    CLAY_AUTO_ID (setting_row()) {
        CLAY_AUTO_ID (fixed_cell(card_label_width())) {
            text("Show", settings_body, {210, 220, 234, 255});
        }
        changed = ClayWidgets_Combo(widgets_.get(), id(name), CLAY_STRING(""), items, 2, &selected);
    }
    if (changed)
        used = selected == 1;
    return changed;
}
void View::providers_settings(const Usage& data, Frame& result, std::int64_t now) {
    result.refresh = begin_settings_page("Providers", "ProvidersScroll", "Refresh") || result.refresh;
    auto cards = column(0, 14);
    cards.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    CLAY (CLAY_ID("ProviderCards"), cards) {
        for (int index = 0; index < 2; ++index) {
            const bool is_codex = index == 0;
            const auto& account = is_codex ? data.codex : data.claude;
            auto card = column(12, 10);
            card.backgroundColor = background_color();
            card.cornerRadius = CLAY_CORNER_RADIUS(10);
            CLAY (id(is_codex ? "LiveUsageCodex" : "LiveUsageClaude"), card) {
                auto& enabled = is_codex ? preferences_.codex_enabled : preferences_.claude_enabled;
                auto header = column(0, 6);
                header.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
                header.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
                CLAY_AUTO_ID (header) {
                    text(is_codex ? "Codex usage" : "Claude usage", 19,
                         provider_color(is_codex ? "Codex" : "Claude"));
                    auto spacer = column(0, 0);
                    CLAY_AUTO_ID (spacer) {}
                    result.changed = ClayWidgets_Toggle(widgets_.get(),
                        id(is_codex ? "EnableCodex" : "EnableClaude"), CLAY_STRING(""), &enabled) || result.changed;
                }
                wrapped_text("Plan: " + (account.plan.empty() ? std::string("Not reported") : account.plan),
                             settings_body, {157, 174, 193, 255});
                text(label(std::string(!account.installed ? "Not detected" : !enabled ? "Disabled" :
                                       !account.error.empty() ? "Refresh failed" : "Connected") +
                           " \xC2\xB7 " + freshness(account.updated, now)),
                     settings_help, {157, 174, 193, 255});
                auto& interval =
                    is_codex ? preferences_.codex_interval : preferences_.claude_interval;
                result.changed =
                    interval_dropdown(is_codex ? "CodexInterval" : "ClaudeInterval", interval) ||
                    result.changed;
                // What this provider's bars measure, and which windows get one on the taskbar.
                auto& a = preferences_.appearance;
                result.changed = measure_dropdown(is_codex ? "CodexMeasure" : "ClaudeMeasure",
                                                  is_codex ? a.codex_show_used : a.claude_show_used) ||
                                 result.changed;
                if (is_codex) {
                    result.changed = setting_toggle("CodexSessionBar", "5 hour bar", a.codex_session_bar) ||
                                     result.changed;
                    result.changed =
                        setting_toggle("CodexWeeklyBar", "Weekly bar", a.codex_weekly_bar) || result.changed;
                } else {
                    result.changed = setting_toggle("ClaudeSessionBar", "Session bar", a.claude_session_bar) ||
                                     result.changed;
                    result.changed =
                        setting_toggle("ClaudeWeeklyBar", "Weekly bar", a.claude_weekly_bar) || result.changed;
                    result.changed = setting_toggle("ClaudeModelBar", "Model weekly bar", a.claude_model_bar) ||
                                     result.changed;
                }
                if (!account.error.empty())
                    wrapped_text("Last error: " + account.error, settings_body,
                                 {240, 180, 90, 255});
                if (!enabled && !account.windows.empty())
                    wrapped_text("Retained reading; updates paused.", settings_body,
                                 {240, 180, 90, 255});
                if (account.windows.empty())
                    wrapped_text(account.installed
                                     ? "No usage reading yet."
                                     : "Install and sign in, then refresh detection.",
                                 settings_body, {166, 187, 208, 255});
                for (std::size_t i = 0; i < account.windows.size(); ++i) {
                    const auto& window = account.windows[i];
                    allowance_row(label(std::string(is_codex ? "SettingsCodex" : "SettingsClaude") +
                                        std::to_string(i)),
                                  is_codex ? "Codex" : "Claude", window, now);
                }
                auto divider = column(0, 0);
                divider.layout.sizing.height = CLAY_SIZING_FIXED(1);
                divider.backgroundColor = widgets_->theme.borderColor;
                CLAY_AUTO_ID (divider) {
                }
                const auto connection_id = id(is_codex ? "CodexConnection" : "ClaudeConnection");
                if (ClayWidgets_BeginCollapsible(widgets_.get(), connection_id,
                                                 CLAY_STRING("Connection details"), &connection_open_[index])) {
                    wrapped_text(account.updated
                                     ? "Last update: " + date(account.updated) + " (local)"
                                     : "Last update: Never",
                                 settings_body, {166, 187, 208, 255});
                    text("Executable", settings_body, {166, 187, 208, 255});
                    if (account.executable_path.empty())
                        text("Not found", settings_body, {166, 187, 208, 255});
                    else {
                        // Preserve every character while wrapping long executable paths.
                        for (std::size_t offset = 0; offset < account.executable_path.size();) {
                            auto end = std::min(offset + 23, account.executable_path.size());
                            while (end < account.executable_path.size() &&
                                   (static_cast<unsigned char>(account.executable_path[end]) &
                                    0xc0) == 0x80)
                                ++end;
                            text(label(account.executable_path.substr(offset, end - offset)),
                                 settings_body, {166, 187, 208, 255});
                            offset = end;
                        }
                    }
                    ClayWidgets_EndCollapsible(widgets_.get(), connection_id);
                }
            }
        }
    }
    ClayWidgets_EndScrollPanel(widgets_.get(), CLAY_ID("ProvidersScroll"));
}
// A sidebar of pages beside the selected page, then the footer. Appearance edits
// preview live on the taskbar and the pinned hover card; Save or Cancel ends them.
void View::settings_panel(const Usage& data, Frame& result, ClayWidgets_Input input) {
    const auto now = reference_time_ ? reference_time_ : static_cast<std::int64_t>(std::time(nullptr));
    auto root = column(16, 14);
    root.layout.sizing.height = CLAY_SIZING_GROW(0);
    root.backgroundColor = background_color();
    CLAY (CLAY_ID("SettingsPanel"), root) {
        auto heading = column(0, 12);
        heading.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        heading.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
        CLAY (CLAY_ID("SettingsHeading"), heading) {
            text("Settings", 24, {236, 243, 250, 255});
            text("Changes preview live. Cancel restores saved settings.", settings_help, {166, 187, 208, 255});
        }
        auto body = column(0, 16);
        body.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        body.layout.sizing.height = CLAY_SIZING_GROW(0);
        CLAY (CLAY_ID("SettingsBody"), body) {
            auto nav = column(0, 4);
            nav.layout.sizing.width = CLAY_SIZING_FIXED(150);
            nav.layout.sizing.height = CLAY_SIZING_GROW(0);
            nav.layout.padding.top = 44 + 4; // level with the page's first row, below its title
            CLAY (CLAY_ID("SettingsNav"), nav) {
                const struct {
                    const char* id;
                    const char* title;
                    SettingsPage page;
                } pages[] = {{"NavTaskbar", features_.taskbar ? "Taskbar" : "Widget", SettingsPage::Taskbar},
                             {"NavHover", "Hover card", SettingsPage::Hover},
                             {"NavProviders", "Providers", SettingsPage::Providers},
                             {"NavGeneral", "General", SettingsPage::General}};
                for (const auto& entry : pages)
                    ClayWidgets_TabEx(widgets_.get(), id(entry.id), string(entry.title),
                                      static_cast<int32_t>(entry.page), &settings_page_,
                                      CLAY_WIDGETS_TAB_STYLE_SIDEBAR);
            }
            auto page = column(0, 10);
            page.layout.sizing.height = CLAY_SIZING_GROW(0);
            CLAY (CLAY_ID("SettingsPage"), page) {
                switch (static_cast<SettingsPage>(settings_page_)) {
                case SettingsPage::Taskbar:
                    taskbar_settings(result);
                    break;
                case SettingsPage::Hover:
                    hover_settings(result);
                    break;
                case SettingsPage::Providers:
                    providers_settings(data, result, now);
                    break;
                case SettingsPage::General:
                    general_settings(result);
                    break;
                }
            }
        }
        auto actions = column(0, 12);
        actions.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        actions.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
        CLAY (CLAY_ID("SettingsActions"), actions) {
            if (ClayWidgets_Button(widgets_.get(), CLAY_ID("ResetAppearance"), CLAY_STRING("Reset appearance"))) {
                preferences_.appearance = Appearance{};
                result.changed = true;
            }
            Clay_ElementDeclaration spacer{};
            spacer.layout.sizing.width = CLAY_SIZING_GROW(0);
            CLAY_AUTO_ID (spacer) {
            }
            result.close =
                ClayWidgets_Button(widgets_.get(), CLAY_ID("CancelSettings"), CLAY_STRING("Cancel")) ||
                input.keyEscape;
            ClayWidgets_ButtonOptions save{};
            save.variant = CLAY_WIDGETS_BUTTON_PRIMARY;
            result.save = ClayWidgets_ButtonEx(widgets_.get(), CLAY_ID("SaveSettings"),
                                               CLAY_STRING("Save settings"), save);
        }
    }
}
void View::set_text_percent(int value) {
    preferences_.appearance.text_percent = preference_limits::text_percent.clamp(value);
}
void View::set_hover_text_percent(int value) {
    preferences_.appearance.hover_text_percent = preference_limits::text_percent.clamp(value);
}
void View::text(const char* value, uint16_t size, Clay_Color tint, int text_percent, bool word) {
    if (text_percent == 0)
        text_percent = surface_text_percent();
    Clay_TextElementConfig config{};
    config.fontSize = static_cast<uint16_t>(std::lround(
        size * (surface_ == Surface::Widget || surface_ == Surface::Hover ? text_percent / 100.f : 1.f)));
    config.fontId = font_id();
    config.textColor = text_color(tint);
    config.wrapMode = surface_ == Surface::Hover ? CLAY_TEXT_WRAP_WORDS : CLAY_TEXT_WRAP_NONE;
    if (surface_ != Surface::Widget) {
        CLAY_TEXT(string(value), config);
        return;
    }
    // A line box keeps room for descenders, so centring it leaves digits and
    // capitals about 1/32 em low beside a bar (measured at the default 150%);
    // padding a sixteenth of the size underneath lifts them onto the bar's
    // centre line. A word reads as centred somewhere between its capitals and
    // its lowercase letters, which sit another 1/10 em lower, so a word gets an
    // eighth of the size: a quarter centred the lowercase and looked high.
    Clay_ElementDeclaration lift{};
    lift.layout.padding.bottom =
        static_cast<uint16_t>(std::lround(config.fontSize / (word ? 8.f : 16.f)));
    CLAY_AUTO_ID (lift) {
        CLAY_TEXT(string(value), config);
    }
}
View::View(Surface surface, ClayWidgets_MeasureTextFunction measure, void* measure_data)
    : surface_(surface), widgets_(std::make_unique<ClayWidgets_Context>()) {
    const auto capacity = Clay_MinMemorySize();
    arena_ = std::malloc(capacity);
    if (!arena_)
        throw std::bad_alloc();
    clay_ = Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(capacity, arena_), {400, 460}, {});
    Clay_SetMeasureTextFunction(measure, measure_data);
    ClayWidgets_Init(widgets_.get(), ClayWidgets_DefaultTheme());
    ClayWidgets_SetMeasureTextFunction(widgets_.get(), measure, measure_data);
    widgets_->animationsEnabled = false;
    widgets_->theme.accentColor = accent_color();
    widgets_->theme.accentMutedColor = {35, 100, 91, 255};
    widgets_->theme.focusRingColor = accent_color();
    widgets_->theme.surfaceColor = {29, 36, 47, 255};
    widgets_->theme.surfaceAltColor = {44, 54, 68, 255};
    widgets_->theme.borderColor = {55, 68, 84, 255};
    widgets_->theme.textColor = {236, 243, 250, 255};
    widgets_->theme.textMutedColor = {157, 174, 193, 255};
    widgets_->theme.radiusSm = 5;
    widgets_->theme.radiusMd = 10;
}
View::~View() {
    if (Clay_GetCurrentContext() == clay_)
        Clay_SetCurrentContext(nullptr);
    std::free(arena_);
}
void View::reset_focus() {
    widgets_->focusedId = 0;
    widgets_->activeId = 0;
    widgets_->openComboId = 0;
}
void View::invalidate_measurements() {
    Clay_SetCurrentContext(clay_);
    Clay_ResetMeasureTextCache();
}
ClayWidgets_Cursor View::cursor() const {
    return ClayWidgets_GetCursor(widgets_.get());
}
Clay_BoundingBox View::bounds(const char* name) {
    Clay_SetCurrentContext(clay_);
    return Clay_GetElementData(id(name)).boundingBox;
}
bool View::focused(const char* name) {
    Clay_SetCurrentContext(clay_);
    return widgets_->focusedId == id(name).id;
}
uint16_t View::widget_gap(int normal, int minimum) const {
    return static_cast<uint16_t>(surface_ == Surface::Widget
        ? std::lround(minimum + (normal - minimum) * widget_spacing_ / 100.f) : normal);
}
float View::text_width(const char* value, uint16_t size, int text_percent) {
    if (text_percent == 0)
        text_percent = surface_text_percent();
    Clay_TextElementConfig config{};
    config.fontId = font_id();
    config.fontSize = static_cast<uint16_t>(std::lround(size * text_percent / 100.f));
    const Clay_StringSlice sample{static_cast<int32_t>(std::strlen(value)), value, value};
    return std::ceil(widgets_->measureText(sample, &config, widgets_->measureTextUserData).width);
}
Clay_ElementDeclaration View::thirds_slot(uint16_t size, int text_percent) const {
    if (text_percent == 0)
        text_percent = surface_text_percent();
    auto slot = column(0, 0);
    slot.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    slot.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    slot.layout.sizing.height =
        CLAY_SIZING_FIXED(std::max(frame_height_ / 3.f, std::ceil(size * text_percent / 100.f * 0.8f)));
    return slot;
}
View::ThirdsPadding View::thirds_padding(float leading, float trailing) const {
    // The taskbar root's side padding.
    const float third = (frame_width_ - 2.f * widget_gap(6, 2)) / 3.f;
    ThirdsPadding result{std::max(0.f, third - leading), std::max(0.f, third - trailing)};
    const float spare = std::max(0.f, 2.f * third - leading - trailing);
    const float wanted = result.lead + result.trail;
    if (wanted > spare) {
        result.lead *= spare / wanted;
        result.trail *= spare / wanted;
    }
    result.lead = std::floor(result.lead);
    result.trail = std::floor(result.trail);
    return result;
}
std::pair<float, float> View::paired_tracks(int count) const {
    // Each track is one pixel thinner than a full bar, capped so the tracks and
    // their gaps never outgrow the row's label. Three tracks keep a single
    // device pixel between them; a pair keeps its roomier gap.
    const float s = pixel_scale_;
    const float row_text = static_cast<float>(std::lround(10 * surface_text_percent() / 100.f)) * s;
    const int gap = count > 2 ? 1 : std::max(1, static_cast<int>(std::lround((row_text >= 14 * s ? 2.f : 1.f) * s)));
    const int fit = static_cast<int>(std::floor((row_text - static_cast<float>(gap * (count - 1))) / count));
    const int wanted = static_cast<int>(std::lround((static_cast<float>(preferences_.appearance.bar_height) - 1.f) * s));
    const int height = std::max(2, std::min(wanted, fit));
    return {static_cast<float>(height) / s, static_cast<float>(gap) / s};
}
void View::snap_track_stacks(Clay_RenderCommandArray commands) const {
    const float s = pixel_scale_;
    for (const auto& stack : track_stacks_) {
        std::vector<int> tracks;
        for (const auto track : stack.ids)
            for (int i = 0; i < commands.length; ++i)
                if (commands.internalArray[i].id == track &&
                    commands.internalArray[i].commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
                    tracks.push_back(i);
                    break;
                }
        if (tracks.empty() || tracks.size() != stack.ids.size())
            continue;
        // Keep the stack centred where the layout put it, starting on a whole pixel.
        const auto& first = commands.internalArray[tracks.front()].boundingBox;
        const auto& last = commands.internalArray[tracks.back()].boundingBox;
        const float centre = (first.y + last.y + last.height) / 2 * s;
        const int count = static_cast<int>(tracks.size());
        const float top = std::round(centre - static_cast<float>(count * stack.height + (count - 1) * stack.gap) / 2);
        for (int k = 0; k < count; ++k) {
            const float y = (top + static_cast<float>(k * (stack.height + stack.gap))) / s;
            const float height = static_cast<float>(stack.height) / s;
            const auto old = commands.internalArray[tracks[k]].boundingBox;
            // The track, then the fill drawn inside it on the same line.
            for (int i = tracks[k]; i < commands.length; ++i) {
                auto& command = commands.internalArray[i];
                if (i != tracks[k] && (command.commandType != CLAY_RENDER_COMMAND_TYPE_RECTANGLE ||
                                       std::abs(command.boundingBox.y - old.y) > 0.01f ||
                                       std::abs(command.boundingBox.height - old.height) > 0.01f))
                    break;
                command.boundingBox.y = y;
                command.boundingBox.height = height;
            }
        }
    }
}
void View::compact_bar(const char* name, const char* label, int value, std::string_view percent,
                       int text_percent) {
    if (text_percent == 0)
        text_percent = surface_text_percent();
    const uint16_t text_size = surface_ == Surface::Settings ? settings_body : 10;
    const bool hover = surface_ == Surface::Hover;
    const float label_width = hover ? 90.f * text_percent / 100.f : widget_columns_.label;
    const float percent_width = hover ? 28.f * text_percent / 100.f : widget_columns_.percent;
    auto row = column(0, 0);
    row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    row.layout.childGap = bar_gap();
    row.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    CLAY (id(name), row) {
        if (label_width > 0) {
            auto name_column = column(0, 0);
            name_column.layout.sizing.width = CLAY_SIZING_FIXED(label_width);
            CLAY_AUTO_ID (name_column) {
                text(label, text_size, {172, 190, 210, 255}, text_percent, true);
            }
        } else
            text(label, text_size, {172, 190, 210, 255}, text_percent, true);
        bar_track(this->label(std::string(name) + "Track"), name, label, value);
        if (percent_width > 0) {
            auto value_column = column(0, 0);
            value_column.layout.sizing.width = CLAY_SIZING_FIXED(percent_width);
            value_column.layout.childAlignment.x = hover ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_LEFT;
            CLAY_AUTO_ID (value_column) {
                text(percent.data(), text_size, {236, 243, 250, 255}, text_percent);
            }
        } else
            text(percent.data(), text_size, {236, 243, 250, 255}, text_percent);
    }
}
// One bar: a full-width track filled to `value`, in the provider's colour
// (its lighter one for a Fable window) or, unbranded, by how much is left.
void View::bar_track(const char* track_id, const char* name, const char* label, int value) {
    const bool branded = std::strstr(name, "Codex") || std::strstr(name, "Claude");
    Clay_ElementDeclaration track{};
    track.layout.sizing.width = CLAY_SIZING_GROW(0);
    track.layout.sizing.height = CLAY_SIZING_FIXED(static_cast<float>(preferences_.appearance.bar_height));
    track.backgroundColor = widgets_->theme.borderColor;
    track.cornerRadius = CLAY_CORNER_RADIUS(12);
    CLAY (id(track_id), track) {
        Clay_ElementDeclaration fill{};
        fill.layout.sizing.width = CLAY_SIZING_PERCENT(static_cast<float>(value) / 100.f);
        fill.layout.sizing.height = CLAY_SIZING_GROW(0);
        fill.backgroundColor = branded ? provider_color(name, std::strstr(name, "Fable") || std::strstr(label, "Fable"))
                               : value > 30 ? accent_color()
                                            : color(bar_color(value));
        fill.cornerRadius = CLAY_CORNER_RADIUS(12);
        CLAY_AUTO_ID (fill) {
        }
    }
}
void View::codex_only_split(const AccountUsage& account, const Allowance& session, const Allowance& weekly) {
    const int percent = std::min(preferences_.appearance.taskbar_text_scale(), 160);
    const auto previous = widget_columns_;
    const auto title = account.error.empty() ? "Codex" : "Codex *";
    const auto first = std::to_string(shown(session.remaining, false)) + "%";
    const auto second = std::to_string(shown(weekly.remaining, false)) + "%";
    widget_columns_ = {text_width("Week", 10, percent),
                       std::max(text_width(first.c_str(), 10, percent), text_width(second.c_str(), 10, percent))};
    const auto now = reference_time_ ? reference_time_ : static_cast<std::int64_t>(std::time(nullptr));
    const auto first_reset = taskbar_reset(session.resets_at, now);
    const auto second_reset = taskbar_reset(weekly.resets_at, now);
    const float reset_width = std::max(text_width(first_reset.c_str(), 9, percent),
                                       text_width(second_reset.c_str(), 9, percent));
    const float title_width = text_width(title, 10, percent);
    // Keeps the provider name clear of the row labels beside the bars.
    const float title_gap = static_cast<float>(bar_gap() + widget_gap(4, 2));
    const bool show_resets = preferences_.appearance.show_resets &&
        frame_width_ >= 12 + title_width + title_gap + widget_columns_.label + widget_columns_.percent +
                        reset_width + 24 + 3 * bar_gap();
    // The bars start a third of the way in, spare room going between the name
    // and the row labels; they then run up to the trailing text, which keeps
    // only its own width.
    const float lead = thirds_padding(title_width + title_gap + widget_columns_.label + bar_gap(),
                                      bar_gap() + widget_columns_.percent +
                                          (show_resets ? bar_gap() + reset_width : 0.f))
                           .lead;
    constexpr float trail = 0;
    auto root = column(0, 0);
    root.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    root.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    CLAY (CLAY_ID("CodexSplit"), root) {
        auto name = column(0, 0);
        name.layout.sizing.width = CLAY_SIZING_FIXED(title_width + title_gap + lead);
        CLAY (CLAY_ID("CodexSplitName"), name) {
            text(title, 10, provider_color("Codex"), percent, true);
        }
        auto rows = column(0, 0);
        CLAY_AUTO_ID (rows) {
            for (int i = 0; i < 2; ++i) {
                auto row = thirds_slot(10, percent);
                row.layout.childGap = bar_gap();
                CLAY_AUTO_ID (row) {
                    compact_bar(i == 0 ? "Codex5h" : "CodexWeekly", i == 0 ? "5h" : "Week",
                                shown(i == 0 ? session.remaining : weekly.remaining, false),
                                label(i == 0 ? first : second), percent);
                    if (show_resets) {
                        auto reset = column(0, 0);
                        reset.layout.sizing.width = CLAY_SIZING_FIXED(reset_width + trail);
                        reset.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
                        CLAY_AUTO_ID (reset) {
                            text(label(i == 0 ? first_reset : second_reset), 9,
                                 {166, 187, 208, 255}, percent);
                        }
                    }
                }
            }
        }
    }
    widget_columns_ = previous;
}
void View::claude_only(const AccountUsage& account, const Allowance* session, const Allowance& weekly,
                       const Allowance* fable) {
    const bool stale = !account.error.empty();
    const bool side_by_side = preferences_.appearance.taskbar_text_scale() > stacked_text_limit();
    const bool narrow = narrow_widget();
    if (session && fable) {
        // Stacked, every window gets a bar; side by side there is room for the weekly pair.
        if (!side_by_side) {
            claude_three(account, *session, weekly, *fable);
            return;
        }
        session = nullptr;
    }
    struct Row {
        const char* id;
        std::string name, percent, reset;
        const Allowance* allowance;
    };
    std::vector<Row> rows;
    if (session)
        rows.push_back({"ClaudeSession", "Session", {}, {}, session});
    // Beside a separate model row the weekly window is the general allowance.
    rows.push_back({"ClaudeGeneral", fable ? "General" : "Weekly", {}, {}, &weekly});
    if (fable)
        rows.push_back({"ClaudeFable", "Fable", {}, {}, fable});
    const auto previous = widget_columns_;
    widget_columns_ = {};
    for (auto& row : rows) {
        if (stale)
            row.name += " *";
        row.percent = std::to_string(shown(row.allowance->remaining, true)) + "%";
        widget_columns_.label = std::max(widget_columns_.label, text_width(row.name.c_str(), 10));
        widget_columns_.percent = std::max(widget_columns_.percent, text_width(row.percent.c_str(), 10));
    }
    // Two reset lines must fit the fixed taskbar height at every text scale.
    const auto reset_size = static_cast<uint16_t>(std::min(9.f, 1600.f / preferences_.appearance.taskbar_text_scale()));
    float reset_width = 0;
    if (preferences_.appearance.show_resets) {
        auto& weekly_reset = rows[session ? 1 : 0].reset;
        weekly_reset = reset_time(weekly.resets_at, narrow);
        if (fable) {
            const bool shared = weekly.resets_at > 0 && fable->resets_at > 0 &&
                                reset_time(weekly.resets_at, true, true) == reset_time(fable->resets_at, true, true);
            auto& second = rows.back().reset;
            second = reset_time(fable->resets_at, narrow);
            if (shared) {
                weekly_reset = reset_time(weekly.resets_at, true);
                second = reset_time(weekly.resets_at).substr(6);
                const auto other = reset_time(fable->resets_at).substr(6);
                if (second != other)
                    second += "/" + other;
            }
        }
        // The session resets within five hours, so its time of day is enough; an
        // idle one has no reset yet.
        if (session && session->resets_at > 0)
            rows.front().reset = reset_time(session->resets_at).substr(6);
        for (const auto& row : rows)
            reset_width = std::max(reset_width, text_width(row.reset.c_str(), reset_size));
    }
    // Stacked bars run from right after the short row labels up to the
    // percentages and resets, which keep only their own width.
    constexpr float trail = 0;
    auto row = column(0, bar_gap());
    row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    row.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    CLAY (CLAY_ID("ClaudeOnly"), row) {
        auto bars = column(0, 0);
        if (side_by_side) {
            bars.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
            bars.layout.childGap = widget_gap(12, 2);
        }
        CLAY (CLAY_ID("ClaudeOnlyBars"), bars) {
            for (const auto& entry : rows) {
                if (side_by_side) {
                    compact_bar(entry.id, label(entry.name), shown(entry.allowance->remaining, true),
                                label(entry.percent));
                } else {
                    CLAY_AUTO_ID (thirds_slot(10)) {
                        compact_bar(entry.id, label(entry.name), shown(entry.allowance->remaining, true),
                                    label(entry.percent));
                    }
                }
            }
        }
        widget_columns_ = previous;
        if (reset_width > 0) {
            auto resets = column(0, 0);
            resets.layout.sizing.width = CLAY_SIZING_FIXED(reset_width + trail);
            CLAY (CLAY_ID("ClaudeResetColumn"), resets) {
                // Stacked, each reset shares its bar's slot so it reads along the
                // same line; beside side-by-side bars the pair keeps to its own size.
                auto slot = thirds_slot(side_by_side ? reset_size : 10);
                slot.layout.childAlignment.x = side_by_side ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_CENTER;
                for (const auto& entry : rows) {
                    CLAY_AUTO_ID (slot) {
                        text(label(entry.reset), reset_size, {166, 187, 208, 255});
                    }
                }
            }
        }
    }
}
void View::claude_three(const AccountUsage& account, const Allowance& session, const Allowance& weekly,
                        const Allowance& fable) {
    const bool stale = !account.error.empty();
    // Three equal slots, centred; the labels, percentages and resets are placed
    // against them. "Session" is centred on the top slot, so the slots stay
    // small enough for its full-size line, lift padding included, to fit above.
    const float label_line = std::round(10 * surface_text_percent() / 100.f) * (1 + 1 / 8.f);
    const float slot = std::floor(std::min((frame_height_ - 2.f * widget_gap(2)) / 3.f,
                                           (frame_height_ - label_line) / 2));
    // As large as the slots allow: digits stand well inside their line box, so
    // a line may run a fifth over its slot before neighbours touch. Never
    // larger than the labels' 10.
    const auto small = static_cast<uint16_t>(
        std::clamp(std::floor(slot * 1.2f / surface_text_percent() * 100.f + 0.01f), 5.f, 8.f));
    const Allowance* windows[] = {&session, &weekly, &fable};
    const char* ids[] = {"ClaudeSession", "ClaudeGeneral", "ClaudeFable"};
    std::string percents[3];
    float percent_width = 0;
    for (int i = 0; i < 3; ++i) {
        percents[i] = std::to_string(shown(windows[i]->remaining, true)) + "%";
        percent_width = std::max(percent_width, text_width(percents[i].c_str(), small));
    }
    const std::string names[] = {stale ? "Session *" : "Session", stale ? "Weekly *" : "Weekly"};
    const float label_width = std::max(text_width(names[0].c_str(), 10), text_width(names[1].c_str(), 10));
    // Line one is the session's time of day; line two the sooner of the weekly
    // resets, the hover card listing both.
    std::string resets[2];
    float reset_width = 0;
    const auto reset_size = static_cast<uint16_t>(std::min(9.f, 1600.f / preferences_.appearance.taskbar_text_scale()));
    if (preferences_.appearance.show_resets) {
        if (session.resets_at > 0)
            resets[0] = reset_time(session.resets_at).substr(6);
        const auto soonest = weekly.resets_at <= 0 ? fable.resets_at
                             : fable.resets_at <= 0 ? weekly.resets_at
                                                    : std::min(weekly.resets_at, fable.resets_at);
        resets[1] = reset_time(soonest, true);
        for (const auto& line : resets)
            reset_width = std::max(reset_width, text_width(line.c_str(), reset_size));
    }
    // The bars take all the room the labels, percentages and resets leave.
    // Every bar comes out equally thick, whatever pixel its slot starts on.
    const int bar = static_cast<int>(std::lround(preferences_.appearance.bar_height * pixel_scale_));
    TrackStack stack{{}, bar, static_cast<int>(std::lround(slot * pixel_scale_)) - bar};
    for (const auto* track : ids)
        stack.ids.push_back(id(label(std::string(track) + "Track")).id);
    track_stacks_.push_back(std::move(stack));
    const auto fixed = [](float width, float height) {
        auto cell = column(0, 0);
        cell.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        cell.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
        cell.layout.sizing.height = CLAY_SIZING_FIXED(height);
        if (width > 0)
            cell.layout.sizing.width = CLAY_SIZING_FIXED(width);
        return cell;
    };
    auto row = column(0, bar_gap());
    row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    row.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    CLAY (CLAY_ID("ClaudeOnly"), row) {
        // "Session" beside the first slot, "Weekly" centred on the other two.
        CLAY (CLAY_ID("ClaudeThreeLabels"), column(0, 0)) {
            CLAY_AUTO_ID (fixed(label_width, slot)) {
                text(label(names[0]), 10, {172, 190, 210, 255}, 0, true);
            }
            CLAY_AUTO_ID (fixed(label_width, 2 * slot)) {
                text(label(names[1]), 10, {172, 190, 210, 255}, 0, true);
            }
        }
        // The taskbar row fits its content, so the bars take an explicit width:
        // whatever the labels and the reset column leave.
        const float resets_width = reset_width;
        const float bars_width = std::max(
            0.f, frame_width_ - 2.f * widget_gap(6, 2) - label_width - resets_width - (resets_width > 0 ? 2 : 1) * bar_gap());
        auto bars = column(0, 0);
        bars.layout.sizing.width = CLAY_SIZING_FIXED(bars_width);
        CLAY (CLAY_ID("ClaudeOnlyBars"), bars) {
            for (int i = 0; i < 3; ++i) {
                auto line = fixed(bars_width, slot);
                line.layout.childGap = bar_gap();
                CLAY (id(ids[i]), line) {
                    bar_track(label(std::string(ids[i]) + "Track"), "Claude", i == 2 ? "Fable" : "",
                              shown(windows[i]->remaining, true));
                    CLAY_AUTO_ID (fixed(percent_width, slot)) {
                        text(label(percents[i]), small, {236, 243, 250, 255});
                    }
                }
            }
        }
        if (resets_width > 0) {
            // The two lines read as one block, centred in the column both ways
            // rather than tied to the bars.
            auto resets_column = fixed(resets_width, 3 * slot);
            resets_column.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
            resets_column.layout.childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER};
            CLAY (CLAY_ID("ClaudeResetColumn"), resets_column) {
                for (const auto& line : resets)
                    if (!line.empty())
                        text(label(line), reset_size, {166, 187, 208, 255});
            }
        }
    }
}
void View::column_text(float width, const char* value, uint16_t size, Clay_Color tint, uint16_t left_padding,
                       bool word, bool centred) {
    if (width <= 0 && left_padding == 0) {
        text(value, size, tint, 0, word);
        return;
    }
    auto fixed = column(0, 0);
    fixed.layout.sizing.width = CLAY_SIZING_FIXED((width > 0 ? width : text_width(value, size)) + left_padding);
    fixed.layout.padding.left = left_padding;
    if (centred)
        fixed.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    CLAY_AUTO_ID (fixed) {
        text(value, size, tint, 0, word);
    }
}
// Reset dates for the paired Claude row: one date when both fall on the same
// local day, otherwise weekly then Fable weekly.
static std::string combined_resets(const Allowance& weekly, const Allowance& fable, bool narrow) {
    auto resets = format_reset_time(weekly.resets_at, true, false, false);
    if (weekly.resets_at <= 0 || fable.resets_at <= 0 ||
        format_reset_time(weekly.resets_at, true, true, false) != format_reset_time(fable.resets_at, true, true, false))
        resets += (narrow ? "|" : " / ") + format_reset_time(fable.resets_at, true, false, false);
    return std::string(narrow ? "" : "reset ") + resets;
}
void View::taskbar_allowance(const char* name, const char* title, const Allowance& allowance, bool stale,
                             bool show_reset, bool date_only) {
    const bool claude = std::strcmp(name, "Claude") == 0;
    auto row = column(0, bar_gap());
    row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    row.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    CLAY_AUTO_ID (row) {
        compact_bar(name, label(std::string(title) + (stale ? " *" : "")), shown(allowance.remaining, claude),
                    label(std::to_string(shown(allowance.remaining, claude)) + "%"));
        auto reset = window_reset(allowance, date_only || narrow_widget());
        if (!reset.empty() && !narrow_widget() && std::strcmp(name, "Codex") != 0)
            reset = "reset " + reset;
        if (preferences_.appearance.show_resets && show_reset)
            column_text(widget_columns_.reset, label(reset),
                        9, {166, 187, 208, 255}, static_cast<uint16_t>(6 - bar_gap()), false, true);
    }
}
void View::claude_bars(const AccountUsage& account, const Allowance* session, const Allowance& weekly,
                       const Allowance& fable) {
    const bool narrow = narrow_widget();
    auto row = column(0, bar_gap());
    row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    row.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    CLAY (CLAY_ID("Claude"), row) {
        column_text(widget_columns_.label, account.error.empty() ? "Claude" : "Claude *", 10,
                    {172, 190, 210, 255}, 0, true);
        // The tracks share the row with a 10 pt label; the session, when reported, goes on top.
        struct Track {
            const char* id;
            const Allowance* allowance;
            bool secondary;
        };
        std::vector<Track> rows;
        if (session)
            rows.push_back({"ClaudeSessionTrack", session, false});
        rows.push_back({"ClaudeWeeklyTrack", &weekly, false});
        rows.push_back({"ClaudeFableTrack", &fable, true});
        const auto [track_height, track_gap] = paired_tracks(static_cast<int>(rows.size()));
        TrackStack stack{{}, static_cast<int>(std::lround(track_height * pixel_scale_)),
                         static_cast<int>(std::lround(track_gap * pixel_scale_))};
        for (const auto& entry : rows)
            stack.ids.push_back(id(entry.id).id);
        track_stacks_.push_back(std::move(stack));
        auto tracks = column(0, static_cast<uint16_t>(track_gap));
        CLAY (CLAY_ID("ClaudeTracks"), tracks) {
            for (const auto& entry : rows) {
                Clay_ElementDeclaration track{};
                track.layout.sizing.width = CLAY_SIZING_GROW(0);
                track.layout.sizing.height = CLAY_SIZING_FIXED(track_height);
                track.backgroundColor = widgets_->theme.borderColor;
                track.cornerRadius =
                    CLAY_CORNER_RADIUS(12);
                CLAY (id(entry.id), track) {
                    Clay_ElementDeclaration fill{};
                    fill.layout.sizing.width = CLAY_SIZING_PERCENT(shown(entry.allowance->remaining, true) / 100.f);
                    fill.layout.sizing.height = CLAY_SIZING_GROW(0);
                    fill.backgroundColor = provider_color("Claude", entry.secondary);
                    fill.cornerRadius =
                        CLAY_CORNER_RADIUS(12);
                    CLAY_AUTO_ID (fill) {
                    }
                }
            }
        }
        // One percentage for the row, like Codex's: the session's, or without one
        // the weekly window with the least left.
        const Allowance* headline = session ? session : fable.remaining < weekly.remaining ? &fable : &weekly;
        column_text(widget_columns_.percent, label(std::to_string(shown(headline->remaining, true)) + "%"), 10,
                    {236, 243, 250, 255});
        // With a session the row follows it; otherwise the weekly pair's dates.
        auto session_reset = session ? window_reset(*session, true) : std::string{};
        if (!session_reset.empty() && !narrow)
            session_reset = "reset " + session_reset;
        if (preferences_.appearance.show_resets)
            column_text(widget_columns_.reset,
                        label(!session ? combined_resets(weekly, fable, narrow) : session_reset),
                        9,
                        {166, 187, 208, 255}, static_cast<uint16_t>(6 - bar_gap()), false, true);
    }
}
const char* View::label(std::string value) {
    labels_.push_back(std::move(value));
    return labels_.back().c_str();
}
Clay_Dimensions widget_size(const Appearance& appearance) {
    return {static_cast<float>(appearance.widget_width), static_cast<float>(appearance.widget_height)};
}
Rect place_widget(const Appearance& appearance, Rect panel, const std::vector<Rect>& occupied, float scale) {
    if (scale <= 0) return {};
    // Text is never reduced to fit a gap: the bars give way as the widget narrows
    // toward the smallest allowed width.
    for (int width = appearance.widget_width; width >= preference_limits::widget_width.min; width -= 10) {
        const auto bounds = find_space(panel, occupied, static_cast<int>(std::lround(width * scale)),
                                       static_cast<int>(std::lround(widget_height * scale)),
                                       static_cast<int>(std::ceil(8 * scale)), appearance.position);
        if (!bounds.empty()) return bounds;
    }
    return {};
}
int View::widget_spacing_for(float width) const {
    const auto& a = preferences_.appearance;
    const float range = static_cast<float>(a.widget_width - preference_limits::widget_width.min);
    int spacing = 100;
    if (range > 0)
        spacing = std::clamp(static_cast<int>(std::lround(
                                 100.f * (width - preference_limits::widget_width.min) / range)),
                             0, 100);
    const int scale = a.taskbar_text_scale();
    if (scale > 160 && scale <= 180)
        spacing = std::min(spacing, 100 - 5 * (scale - 160));
    return spacing;
}
Clay_Dimensions hover_size(const Usage& data, int text_scale) {
    const float scale = text_scale / 100.f;
    if (!data.live)
        return {320.f * scale, 250.f * scale};
    const auto font = [scale](int size) { return static_cast<float>(std::lround(size * scale)); };
    // Padding and layout gaps stay fixed; only text grows with the preference.
    float height = 28 + font(11);
    int providers = 0;
    for (const auto* account : {&data.codex, &data.claude}) {
        if (!(account == &data.codex ? data.codex_active() : data.claude_active()))
            continue;
        if (providers++)
            height += 11; // Divider and its additional root gap.
        height += 14 + font(14) + font(12);
        height += account->windows.empty() ? 6 + font(13)
            : (font(14) + font(13) + 19) * static_cast<float>(account->windows.size());
    }
    if (!providers)
        height += 14 + font(14) + font(11);
    return {360.f * scale, height + 4};
}
void View::allowance_row(const char* name, const char* provider, const Allowance& window,
                         std::int64_t now) {
    const bool settings = surface_ == Surface::Settings;
    auto allowance = column(0, settings ? 5 : 3);
    CLAY_AUTO_ID (allowance) {
        auto line = column(0, 4);
        line.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        line.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
        CLAY_AUTO_ID (line) {
            text(window.label.c_str(), settings ? settings_body : 13, {172, 190, 210, 255});
            auto space = column(0, 0);
            CLAY_AUTO_ID (space) {
            }
            text(label(std::to_string(shown(window.remaining, std::strcmp(provider, "Claude") == 0)) + "%"),
                 settings ? 21 : 14, {236, 243, 250, 255});
        }
        auto track = column(0, 0);
        track.layout.sizing.height = CLAY_SIZING_FIXED(7);
        track.backgroundColor = widgets_->theme.borderColor;
        track.cornerRadius = CLAY_CORNER_RADIUS(3);
        CLAY (id(name), track) {
            auto fill = column(0, 0);
            fill.layout.sizing.width = CLAY_SIZING_PERCENT(
                std::clamp(shown(window.remaining, std::strcmp(provider, "Claude") == 0), 0, 100) / 100.f);
            fill.layout.sizing.height = CLAY_SIZING_GROW(0);
            fill.backgroundColor = provider_color(provider, window.label.find("Fable") != std::string::npos);
            fill.cornerRadius = CLAY_CORNER_RADIUS(3);
            CLAY_AUTO_ID (fill) {
            }
        }
        text(label(hover_reset(window.resets_at, now)), settings ? settings_help : 13, {157, 174, 193, 255});
    }
}
float View::hover_height(Usage& data, float width) {
    frame(data, {}, width, 10000);
    return std::ceil(bounds("HoverOverview").height);
}
void View::hover_usage(const Usage& data) {
    auto root = column(14, 10);
    root.layout.sizing.height = CLAY_SIZING_FIT(0);
    root.backgroundColor = background_color();
    CLAY (CLAY_ID("HoverOverview"), root) {
        const auto now = reference_time_ ? reference_time_ : static_cast<std::int64_t>(std::time(nullptr));
        bool first_provider = true;
        for (const auto& entry :
             {std::pair<const char*, const AccountUsage*>{"Codex", &data.codex}, {"Claude", &data.claude}}) {
            const auto& account = *entry.second;
            if (!(entry.second == &data.codex ? data.codex_active() : data.claude_active()))
                continue;
            if (!first_provider) {
                auto divider = column(0, 0);
                divider.layout.sizing.height = CLAY_SIZING_FIXED(1);
                divider.backgroundColor = widgets_->theme.borderColor;
                CLAY_AUTO_ID (divider) {
                }
            }
            first_provider = false;
            auto provider = column(0, 6);
            CLAY (id(label(std::string("Hover") + entry.first)), provider) {
                auto header = column(0, 4);
                header.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
                header.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
                CLAY_AUTO_ID (header) {
                    text(label(std::string(entry.first) +
                               (account.error.empty() ? "" : account.windows.empty() ? " - unavailable" : " - stale")),
                         14, account.error.empty() ? provider_color(entry.first) : Clay_Color{240, 180, 90, 255});
                    auto spacer = column(0, 0);
                    CLAY_AUTO_ID (spacer) {}
                    if (!account.windows.empty())
                        text(label(freshness(account.updated, now)), 11, {157, 174, 193, 255});
                }
                if (!account.plan.empty())
                    text(label("Plan: " + account.plan), 12, {157, 174, 193, 255});
                if (account.unlimited_credits)
                    text(label("Credits: unlimited"), 13, {157, 174, 193, 255});
                else if (!account.credit_balance.empty())
                    text(label("Credits: " + account.credit_balance), 13, {157, 174, 193, 255});
                else if (account.has_credits.has_value())
                    text(label(*account.has_credits ? "Credits available" : "No credits remaining"),
                                 13, {157, 174, 193, 255});
                if (account.available_resets.has_value())
                    text(label(std::to_string(*account.available_resets) +
                                     (*account.available_resets == 1 ? " earned reset available" : " earned resets available")),
                                 13, {157, 174, 193, 255});
                if (account.windows.empty())
                    text(account.error.empty() ? "Connecting..." : "Refresh failed - click widget", 13,
                         {157, 174, 193, 255});
                for (std::size_t i = 0; i < account.windows.size(); ++i) {
                    const auto& window = account.windows[i];
                    allowance_row(label(std::string("Hover") + entry.first + std::to_string(i)),
                                  entry.first, window, now);
                }
            }
        }
        if (!data.codex_active() && !data.claude_active())
            text(!data.codex_enabled && !data.claude_enabled ? "Providers disabled - open settings"
                                                             : "No supported installations detected",
                 14, {157, 174, 193, 255});
    }
}
void View::live_usage(const char* provider, const AccountUsage& account, Frame& result,
                      ClayWidgets_Input input) {
    const bool compact = surface_ == Surface::Widget;
    const bool stale = !account.error.empty();
    auto root = column(compact ? 6 : 16, compact ? 3 : 12);
    root.layout.sizing.height = CLAY_SIZING_GROW(0);
    if (compact)
        root.layout.padding.top = root.layout.padding.bottom = 3;
    else
        root.backgroundColor = background_color();
    CLAY (id(label(std::string("LiveUsage") + provider)), root) {
        const bool claude = std::strcmp(provider, "Claude") == 0;
        text(label(std::string(provider) +
                   (compact ? stale ? "   STALE" : shows_used(claude) ? "   USED" : "   REMAINING" : " usage")),
             compact ? 9 : 18,
             stale      ? Clay_Color{240, 180, 90, 255}
             : hovered_ ? accent_color()
                        : Clay_Color{166, 187, 208, 255});
        if (account.windows.empty()) {
            text(stale ? "Usage unavailable" : label(std::string("Connecting to ") + provider + "..."),
                 compact ? 10 : 14, {236, 243, 250, 255});
        }
        auto windows = column(0, compact ? 10 : 12);
        if (compact)
            windows.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        CLAY_AUTO_ID (windows) {
            for (std::size_t i = 0; i < account.windows.size(); ++i) {
                const auto& window = account.windows[i];
                const auto* name = label(std::string(provider) + "Allowance" + std::to_string(i));
                const auto* percent = label(std::to_string(shown(window.remaining, claude)) + "%");
                if (compact)
                    compact_bar(name, window.label.c_str(), shown(window.remaining, claude), percent);
                else {
                    auto card = column(10, 6);
                    card.backgroundColor = widgets_->theme.surfaceColor;
                    card.cornerRadius =
                        CLAY_CORNER_RADIUS(12);
                    CLAY_AUTO_ID (card) {
                        compact_bar(name, window.label.c_str(), shown(window.remaining, claude), percent);
                        text(label(std::to_string(window.remaining) + "% remaining / " +
                                   std::to_string(100 - window.remaining) + "% used"),
                             11, {157, 174, 193, 255});
                        text(label("Resets: " + date(window.resets_at) + " (local)"), 11,
                             {157, 174, 193, 255});
                    }
                }
            }
        }
        if (!compact) {
            if (stale)
                text("Refresh failed - last reading may be stale", 11, {240, 180, 90, 255});
            if (!account.updated || surface_ == Surface::Details || surface_ == Surface::Settings) {
                if (stale)
                    text(label(account.error.substr(0, 50)), 11, {240, 180, 90, 255});
            }
            if (account.updated)
                text(label("Updated: " + date(account.updated) + " (local)"), 11, {157, 174, 193, 255});
            if (surface_ == Surface::Details || surface_ == Surface::Settings) {
                if (!account.plan.empty())
                    text(label("Plan: " + account.plan), 12, {157, 174, 193, 255});
            }
            if (surface_ == Surface::Details) {
                text("Refreshes every minute. Allowance is not a token count.", 11, {157, 174, 193, 255});
                result.refresh =
                    ClayWidgets_Button(widgets_.get(), id(label(std::string(provider) + "RefreshUsage")),
                                       CLAY_STRING("Refresh now")) ||
                    result.refresh;
                result.close = ClayWidgets_Button(widgets_.get(), id(label(std::string(provider) + "Close")),
                                                  CLAY_STRING("Close")) ||
                               input.keyEscape || result.close;
            }
        }
    }
}
void View::apply_theme() {
    const auto selected_accent = accent_color();
    widgets_->theme.accentColor = selected_accent;
    widgets_->theme.focusRingColor = selected_accent;
    widgets_->theme.accentMutedColor = {selected_accent.r * 0.35f, selected_accent.g * 0.35f,
                                        selected_accent.b * 0.35f, 255};
    widgets_->theme.textColor = text_color({236, 243, 250, 255});
    widgets_->theme.textMutedColor = text_color({157, 174, 193, 255});
    widgets_->theme.surfaceColor = light_theme() ? Clay_Color{250, 250, 250, 255} : Clay_Color{39, 39, 39, 255};
    widgets_->theme.surfaceAltColor = widgets_->theme.surfaceColor;
    widgets_->theme.borderColor = light_theme() ? Clay_Color{205, 205, 205, 255} : Clay_Color{65, 65, 65, 255};
    widgets_->theme.hoverColor = light_theme() ? Clay_Color{230, 230, 230, 255} : Clay_Color{52, 52, 52, 255};
    widgets_->theme.fontBody = widgets_->theme.fontHeading = font_id();
    widgets_->theme.selectionColor = widgets_->theme.hoverColor;
    widgets_->theme.onSelectionColor = widgets_->theme.textColor;
    widgets_->theme.pressedColor = widgets_->theme.borderColor;
    widgets_->theme.onAccentColor = (selected_accent.r * 0.299f + selected_accent.g * 0.587f +
                                     selected_accent.b * 0.114f) > 150.f
                                        ? Clay_Color{20, 20, 20, 255} : Clay_Color{255, 255, 255, 255};
    widgets_->theme.fontSizeBody =
        surface_ == Surface::Settings || surface_ == Surface::Menu ? settings_body : 18;
}

// The window a one-bar provider row shows: its 5 hour session when reported,
// otherwise whichever window has the least left. End when there are none.
static std::vector<Allowance>::const_iterator headline_window(const AccountUsage& account) {
    const auto session = std::find_if(account.windows.begin(), account.windows.end(),
                                      [](const Allowance& window) { return window.label == "5 hour"; });
    if (session != account.windows.end())
        return session;
    return std::min_element(account.windows.begin(), account.windows.end(),
                            [](const Allowance& a, const Allowance& b) { return a.remaining < b.remaining; });
}
// The taskbar's view of the readings: windows whose bars are switched off are
// dropped, and a provider left with none steps aside as if it were disabled.
static Usage taskbar_usage(const Usage& all, const Appearance& a) {
    Usage result = all;
    const auto keep = [](AccountUsage& account, bool& enabled, bool session, bool weekly, bool model) {
        if (account.windows.empty())
            return;
        auto& windows = account.windows;
        windows.erase(std::remove_if(windows.begin(), windows.end(),
                                     [&](const Allowance& window) {
                                         return window.label == "5 hour" ? !session
                                                : window.label == "Weekly" ? !weekly
                                                                           : !model;
                                     }),
                      windows.end());
        if (account.windows.empty())
            enabled = false;
    };
    keep(result.codex, result.codex_enabled, a.codex_session_bar, a.codex_weekly_bar, true);
    keep(result.claude, result.claude_enabled, a.claude_session_bar, a.claude_weekly_bar, a.claude_model_bar);
    return result;
}

void View::live_panel(const Usage& all, Frame& result, ClayWidgets_Input input) {
    if (surface_ == Surface::Widget)
        taskbar_data_ = taskbar_usage(all, preferences_.appearance);
    const Usage& data = surface_ == Surface::Widget ? taskbar_data_ : all;
    if (surface_ == Surface::Widget && (data.codex_active() || data.claude_active())) {
        const bool horizontal = preferences_.appearance.taskbar_text_scale() > stacked_text_limit();
        auto root = column(widget_gap(6, 2), widget_gap(horizontal ? 12 : 2, horizontal ? 2 : 0));
        root.layout.padding.top = root.layout.padding.bottom = widget_gap(2);
        root.layout.sizing.height = CLAY_SIZING_GROW(0);
        root.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
        if (horizontal)
            root.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        // Stacked rows share label and percentage columns so both bars start and
        // end on the same x; side-by-side rows and single providers size their own.
        widget_columns_ = {};
        if (!horizontal && data.codex_active() && data.claude_active()) {
            const bool narrow = narrow_widget();
            for (const auto& entry : {std::pair<const char*, const AccountUsage*>{"Codex", &data.codex},
                                      {"Claude", &data.claude}}) {
                const auto& account = *entry.second;
                if (account.windows.empty())
                    continue;
                const std::string title = std::string(entry.first) + (account.error.empty() ? "" : " *");
                widget_columns_.label = std::max(widget_columns_.label, text_width(title.c_str(), 10));
                const auto lowest = headline_window(account);
                std::string percent = std::to_string(shown(lowest->remaining, entry.second == &data.claude)) + "%";
                // Measured as drawn: a narrow widget drops Claude's "reset " prefix.
                std::string reset = window_reset(*lowest, narrow);
                if (!reset.empty() && !narrow && entry.second != &data.codex)
                    reset = "reset " + reset;
                if (entry.second == &data.claude && lowest->label != "5 hour") {
                    const auto weekly = std::find_if(account.windows.begin(), account.windows.end(),
                                                     [](const auto& w) { return w.label == "Weekly"; });
                    const auto fable = std::find_if(account.windows.begin(), account.windows.end(),
                                                    [](const auto& w) { return w.label == "Fable weekly"; });
                    // Without a session, the paired row's resets are the weekly pair's.
                    if (weekly != account.windows.end() && fable != account.windows.end())
                        reset = combined_resets(*weekly, *fable, narrow);
                }
                widget_columns_.percent = std::max(widget_columns_.percent, text_width(percent.c_str(), 10));
                if (preferences_.appearance.show_resets)
                    widget_columns_.reset = std::max(widget_columns_.reset, text_width(reset.c_str(), 9));
            }
            // The bars start right after the provider names, which read as labels
            // for them, and run up to the percentages and resets: those keep only
            // their own width, so the bars get all the spare room.
        }
        // Stacked providers each take a third-height slot; the slots do the spacing.
        const bool stacked = !horizontal && data.codex_active() && data.claude_active();
        if (stacked)
            root.layout.childGap = 0;
        const auto provider_row = [&](const char* name, const AccountUsage& account) {
            if (&account == &data.claude) {
                const auto find = [&](const char* label) -> const Allowance* {
                    const auto found = std::find_if(account.windows.begin(), account.windows.end(),
                                                    [&](const auto& w) { return w.label == label; });
                    return found != account.windows.end() ? &*found : nullptr;
                };
                const auto* session = find("5 hour");
                const auto* weekly = find("Weekly");
                const auto* fable = find("Fable weekly");
                if (weekly && (fable || session) && !data.codex_active()) {
                    claude_only(account, session, *weekly, fable);
                    return;
                }
                if (weekly && fable) {
                    claude_bars(account, session, *weekly, *fable);
                    return;
                }
            }
            if (&account == &data.codex && !data.claude_active()) {
                const auto session = std::find_if(account.windows.begin(), account.windows.end(),
                    [](const auto& window) { return window.label == "5 hour"; });
                const auto weekly = std::find_if(account.windows.begin(), account.windows.end(),
                    [](const auto& window) { return window.label == "Weekly"; });
                if (session != account.windows.end() && weekly != account.windows.end()) {
                    codex_only_split(account, *session, *weekly);
                    return;
                }
            }
            const auto lowest = headline_window(account);
            if (lowest == account.windows.end())
                text(label(std::string(name) +
                           (account.error.empty() ? ": connecting..." : ": unavailable")),
                     10, {240, 180, 90, 255});
            else if (&account == &data.codex && !data.claude_active() &&
                     preferences_.appearance.show_resets) {
                const int percent = std::min(preferences_.appearance.taskbar_text_scale(), 160);
                const auto now = reference_time_ ? reference_time_ : static_cast<std::int64_t>(std::time(nullptr));
                const auto reset = taskbar_reset(lowest->resets_at, now);
                const auto title = account.error.empty() ? "Codex" : "Codex *";
                // The subtitle spans the bar row: the window's name under the
                // provider name, its reset ending under the percentage. It keeps
                // the other taskbar reset lines' size unless that won't fit.
                constexpr float subtitle_gap = 8;
                int subtitle_percent = percent;
                while (subtitle_percent > 50 && text_width(lowest->label.c_str(), 9, subtitle_percent) + subtitle_gap +
                                                        text_width(reset.c_str(), 9, subtitle_percent) >
                                                    frame_width_ - 12)
                    --subtitle_percent;
                // Keep the bar aligned with the full widget and its hover card.
                // The reset subtitle must not determine the visible row width.
                auto codex = column(0, 0);
                CLAY (CLAY_ID("CodexOnly"), codex) {
                    CLAY_AUTO_ID (thirds_slot(10, percent)) {
                        compact_bar("Codex", title, shown(lowest->remaining, false),
                                    label(std::to_string(shown(lowest->remaining, false)) + "%"), percent);
                    }
                    auto subtitle = thirds_slot(10, percent);
                    subtitle.layout.childGap = static_cast<uint16_t>(subtitle_gap);
                    CLAY (CLAY_ID("CodexReset"), subtitle) {
                        text(lowest->label.c_str(), 9, {166, 187, 208, 255}, subtitle_percent, true);
                        auto space = column(0, 0);
                        CLAY_AUTO_ID (space) {
                        }
                        text(label(reset), 9, {166, 187, 208, 255}, subtitle_percent);
                    }
                }
            } else
                taskbar_allowance(name, name, *lowest, !account.error.empty());
        };
        CLAY (CLAY_ID("Providers"), root) {
            for (const auto& [name, account] : {std::pair<const char*, const AccountUsage*>{"Codex", &data.codex},
                                                {"Claude", &data.claude}}) {
                if (!(account == &data.codex ? data.codex_active() : data.claude_active()))
                    continue;
                if (stacked) {
                    CLAY_AUTO_ID (thirds_slot(10)) {
                        provider_row(name, *account);
                    }
                } else
                    provider_row(name, *account);
            }
        }
    } else if (!data.codex_active() && !data.claude_active()) {
        auto root = column(6, 4);
        CLAY (CLAY_ID("NoProviders"), root) {
            const bool narrow = surface_ == Surface::Widget && narrow_widget();
            text(all.codex_active() || all.claude_active()
                     ? (narrow ? "No bars selected" : "No bars selected - open settings")
                 : !data.codex_enabled && !data.claude_enabled
                     ? (narrow ? "Providers disabled" : "Providers disabled - open settings")
                     : (narrow ? "No providers detected" : "No supported installations detected"),
                 surface_ == Surface::Widget ? 9 : 14, {166, 187, 208, 255});
            if (surface_ == Surface::Details) {
                text("Install Codex or Claude Code, then refresh.", 12, {166, 187, 208, 255});
                result.refresh =
                    ClayWidgets_Button(widgets_.get(), CLAY_ID("Detect"), CLAY_STRING("Refresh now"));
                result.close = ClayWidgets_Button(widgets_.get(), CLAY_ID("Close"), CLAY_STRING("Close")) ||
                               input.keyEscape;
            }
        }
    } else {
        auto row = column(0, 8);
        row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        row.layout.sizing.height = CLAY_SIZING_GROW(0);
        CLAY (CLAY_ID("ProviderColumns"), row) {
            if (data.codex_active())
                live_usage("Codex", data.codex, result, input);
            if (data.claude_active())
                live_usage("Claude", data.claude, result, input);
        }
    }
}

void View::demo_widget(const Usage& data) {
    auto root = column(6, 2);
    root.layout.padding.top = 2;
    root.layout.padding.bottom = 2;
    root.layout.sizing.height = CLAY_SIZING_GROW(0);
    CLAY (CLAY_ID("Widget"), root) {
        if (preferences_.appearance.taskbar_text_scale() <= 160)
            text("DEMO   REMAINING", 9, hovered_ ? accent_color() : Clay_Color{166, 187, 208, 255});
        auto row = column(0, 10);
        row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        CLAY (CLAY_ID("Bars"), row) {
            compact_bar("SessionBar", "S", data.session(), session_);
            compact_bar("WeeklyBar", "W", data.weekly(), weekly_);
        }
    }
}

void View::demo_hover(const Usage& data) {
    auto root = column(16, 12);
    root.layout.sizing.height = CLAY_SIZING_GROW(0);
    root.backgroundColor = background_color();
    CLAY (CLAY_ID("Hover"), root) {
        text("Usage overview", 18, {236, 243, 250, 255});
        text("DEMO DATA  /  No service connected", 11, {157, 174, 193, 255});
        for (int index = 0; index < 2; ++index) {
            auto card = column(10, 6);
            card.backgroundColor = widgets_->theme.surfaceColor;
            card.cornerRadius = CLAY_CORNER_RADIUS(12);
            CLAY_AUTO_ID (card) {
                compact_bar(index == 0 ? "HoverSession" : "HoverWeekly", index == 0 ? "Session" : "Weekly",
                            index == 0 ? data.session() : data.weekly(), index == 0 ? session_ : weekly_);
                auto row = column(0, 8);
                row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
                CLAY_AUTO_ID (row) {
                    text("remaining", 11, {157, 174, 193, 255});
                    Clay_ElementDeclaration spacer{};
                    spacer.layout.sizing.width = CLAY_SIZING_GROW(0);
                    CLAY_AUTO_ID (spacer) {
                    }
                    text((index == 0 ? session_used_ : weekly_used_).c_str(), 11, {157, 174, 193, 255});
                }
            }
        }
        text("Token counts and reset times unavailable", 11, {157, 174, 193, 255});
        text("Click the widget to adjust demo usage", 11, {157, 174, 193, 255});
    }
}

void View::demo_settings(Frame& result, ClayWidgets_Input input) {
    auto panel = column(0, 0);
    panel.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    panel.layout.sizing.height = CLAY_SIZING_GROW(0);
    panel.backgroundColor = background_color();
    CLAY (CLAY_ID("SettingsPanel"), panel) {
        auto root = column(24, 14);
        root.layout.sizing.height = CLAY_SIZING_GROW(0);
        CLAY (CLAY_ID("Settings"), root) {
            text("BarTab", 26, {236, 243, 250, 255});
            text("Appearance", 17, {236, 243, 250, 255});
            auto& a = preferences_.appearance;
            result.changed = setting_slider("TextSizeSlider", "Taskbar text size", a.text_percent,
                                            preference_limits::text_percent, "%") ||
                             result.changed;
            result.changed = setting_slider("HoverTextSizeSlider", "Hover text size", a.hover_text_percent,
                                            preference_limits::text_percent, "%") ||
                             result.changed;
            text("100% default  /  65-200% range", 12, {157, 174, 193, 255});
            text("Larger taskbar text uses more taskbar space.", 11, {157, 174, 193, 255});
            if (ClayWidgets_Button(widgets_.get(), CLAY_ID("ResetTextSize"),
                                   CLAY_STRING("Reset to default"))) {
                set_text_percent(Appearance{}.text_percent);
                set_hover_text_percent(Appearance{}.hover_text_percent);
                result.changed = true;
            }
            Clay_ElementDeclaration space{};
            space.layout.sizing.height = CLAY_SIZING_GROW(0);
            CLAY_AUTO_ID (space) {
            }
            result.save =
                ClayWidgets_Button(widgets_.get(), CLAY_ID("SaveSettings"), CLAY_STRING("Save settings"));
            result.close =
                ClayWidgets_Button(widgets_.get(), CLAY_ID("CancelSettings"), CLAY_STRING("Cancel")) ||
                input.keyEscape;
        }
    }
}

void View::demo_details(Usage& data, Frame& result, ClayWidgets_Input input) {
    auto root = column(24, 18);
    root.layout.sizing.height = CLAY_SIZING_GROW(0);
    root.backgroundColor = background_color();
    CLAY (CLAY_ID("Details"), root) {
        text("Usage remaining", 26, {236, 243, 250, 255});
        text("DEMO DATA  /  No service connected", 12, {157, 174, 193, 255});
        for (int index = 0; index < 2; ++index) {
            auto card = column(16, 12);
            card.backgroundColor = widgets_->theme.surfaceColor;
            card.cornerRadius = CLAY_CORNER_RADIUS(10);
            CLAY (id(index == 0 ? "SessionCard" : "WeeklyCard"), card) {
                auto row = column(0, 8);
                row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
                CLAY_AUTO_ID (row) {
                    text(index == 0 ? "Session" : "Weekly", 17, {236, 243, 250, 255});
                    Clay_ElementDeclaration spacer{};
                    spacer.layout.sizing.width = CLAY_SIZING_GROW(0);
                    CLAY_AUTO_ID (spacer) {
                    }
                    text((index == 0 ? session_ : weekly_).c_str(), 17,
                         color(bar_color(index == 0 ? data.session() : data.weekly())));
                }
                float value = static_cast<float>(index == 0 ? data.session() : data.weekly());
                ClayWidgets_SliderOptions options{};
                options.minValue = 0;
                options.maxValue = 100;
                options.step = 1;
                if (ClayWidgets_Slider(widgets_.get(), id(index == 0 ? "SessionSlider" : "WeeklySlider"),
                                       &value, options)) {
                    if (index == 0)
                        data.set_session(static_cast<int>(std::lround(value)));
                    else
                        data.set_weekly(static_cast<int>(std::lround(value)));
                    result.changed = true;
                }
                text("Adjust demo allowance", 11, {157, 174, 193, 255});
            }
        }
        text("Tab to focus  /  Arrow keys to adjust", 12, {157, 174, 193, 255});
        Clay_ElementDeclaration space{};
        space.layout.sizing.height = CLAY_SIZING_GROW(0);
        CLAY_AUTO_ID (space) {
        }
        result.close =
            ClayWidgets_Button(widgets_.get(), CLAY_ID("Close"), CLAY_STRING("Close")) || input.keyEscape;
    }
}
namespace {
constexpr const char* context_menu_id = "ContextMenu";
} // namespace
// The whole view is the menu: it opens at the origin, and the host sizes its
// window to the panel, so the panel is all that shows.
void View::context_menu(Frame& result) {
    auto* widgets = widgets_.get();
    const auto menu = id(context_menu_id);
    if (menu_opening_) {
        ClayWidgets_OpenContextMenu(widgets, menu, 0, 0);
        menu_opening_ = false;
    }
    Clay_ElementDeclaration root{};
    root.layout.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)};
    CLAY (CLAY_ID("MenuRoot"), root) {
        if (!ClayWidgets_BeginContextMenu(widgets, menu)) {
            result.close = true;
        } else {
            using Choice = Frame::MenuChoice;
            if (ClayWidgets_MenuItem(widgets, id("MenuSettings"), CLAY_STRING("Settings")))
                result.menu = Choice::Settings;
            if (menu_.debug_label &&
                ClayWidgets_MenuItem(widgets, id("MenuDebug"), string(menu_.debug_label)))
                result.menu = Choice::Debug;
            ClayWidgets_MenuSeparator(widgets);
            bool startup = menu_.startup_enabled;
            const bool disabled = !menu_.startup_available && ClayWidgets_BeginDisabled(widgets);
            if (ClayWidgets_MenuCheckItem(widgets, id("MenuStartup"), string(menu_.startup_label), &startup))
                result.menu = Choice::Startup;
            if (disabled)
                ClayWidgets_EndDisabled(widgets);
            ClayWidgets_MenuSeparator(widgets);
            if (menu_.update_label) {
                if (ClayWidgets_MenuItem(widgets, id("MenuUpdate"), string(menu_.update_label)))
                    result.menu = Choice::Update;
                ClayWidgets_MenuSeparator(widgets);
            }
            if (ClayWidgets_MenuItem(widgets, id("MenuQuit"), CLAY_STRING("Quit")))
                result.menu = Choice::Quit;
            ClayWidgets_EndContextMenu(widgets, menu);
        }
    }
}
Clay_BoundingBox View::menu_bounds() {
    Clay_SetCurrentContext(clay_);
    return Clay_GetElementData(
               Clay_GetElementIdWithIndex(CLAY_STRING("ClayWidgetsContextMenuPanel"), id(context_menu_id).id))
        .boundingBox;
}
Frame View::frame(Usage& data, ClayWidgets_Input input, float width, float height) {
    Clay_SetCurrentContext(clay_);
    frame_width_ = width;
    frame_height_ = height;
    labels_.clear();
    track_stacks_.clear();
    if (surface_ == Surface::Widget) {
        widget_width_ = width;
        widget_spacing_ = widget_spacing_for(width);
    }
    apply_theme();
    Frame result;
    const bool dismiss_dropdown = input.keyEscape && widgets_->openComboId != 0;
    ClayWidgets_BeginFrame(widgets_.get(), input, {width, height}, false);
    if (surface_ == Surface::Menu) {
        context_menu(result);
    } else if (data.live) {
        switch (surface_) {
        case Surface::Settings:
            settings_panel(data, result, input);
            break;
        case Surface::Hover:
            hover_usage(data);
            break;
        case Surface::Widget:
        case Surface::Details:
            live_panel(data, result, input);
            break;
        case Surface::Menu:
            break;
        }
    } else {
        session_ = std::to_string(data.session()) + "%";
        weekly_ = std::to_string(data.weekly()) + "%";
        session_used_ = std::to_string(100 - data.session()) + "% used";
        weekly_used_ = std::to_string(100 - data.weekly()) + "% used";
        switch (surface_) {
        case Surface::Widget:
            demo_widget(data);
            break;
        case Surface::Hover:
            demo_hover(data);
            break;
        case Surface::Settings:
            demo_settings(result, input);
            break;
        case Surface::Details:
            demo_details(data, result, input);
            break;
        case Surface::Menu:
            break;
        }
    }
    if (dismiss_dropdown)
        result.close = false;
    result.commands = ClayWidgets_EndFrame(widgets_.get());
    snap_track_stacks(result.commands);
    return result;
}
} // namespace usage::ui
