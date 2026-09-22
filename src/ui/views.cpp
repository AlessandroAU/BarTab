#include "ui/views.hpp"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <ctime>

namespace usage::ui {
namespace {
constexpr uint16_t settings_body = 16;
constexpr uint16_t settings_help = 14;
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
std::string date(std::int64_t timestamp) {
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
    std::strftime(buffer, sizeof(buffer), "%d %b %H:%M", &local);
    return std::string(buffer);
}

std::string reset_time(std::int64_t timestamp, bool date_only = false, bool day_key = false) {
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
    std::strftime(buffer, sizeof(buffer), day_key ? "%Y-%m-%d" : date_only ? "%d/%m" : "%d/%m %H:%M", &local);
    return buffer;
}

} // namespace
Clay_Color View::accent_color() const {
    static constexpr Clay_Color colors[] = {{90, 218, 184, 255}, {105, 175, 255, 255}, {193, 153, 255, 255}};
    return colors[preferences_.appearance.accent];
}
Clay_Color View::background_color() const {
    return preferences_.appearance.theme == 0 ? Clay_Color{19, 25, 34, 255} : Clay_Color{35, 40, 49, 255};
}
Clay_Color View::provider_color(const char* name, bool secondary) const {
    if (std::strstr(name, "Claude"))
        return secondary ? Clay_Color{232, 171, 145, 255} : Clay_Color{217, 119, 87, 255};
    if (std::strstr(name, "Codex"))
        return surface_ == Surface::Widget && system_light_ ? Clay_Color{35, 35, 40, 255}
                                                            : Clay_Color{236, 236, 241, 255};
    return accent_color();
}
void View::wrapped_text(const std::string& value, uint16_t size, Clay_Color tint) {
    Clay_TextElementConfig config{};
    config.fontSize = size;
    config.textColor = tint;
    config.wrapMode = CLAY_TEXT_WRAP_WORDS;
    CLAY_TEXT(string(label(value)), config);
}
bool View::setting_slider(const char* name, const char* title, int& value, SettingRange range,
                          const char* unit) {
    text(label(std::string(title) + ": " + std::to_string(value) + unit), settings_body,
         {210, 220, 234, 255});
    float current = static_cast<float>(value);
    ClayWidgets_SliderOptions options{};
    options.minValue = static_cast<float>(range.min);
    options.maxValue = static_cast<float>(range.max);
    options.step = static_cast<float>(range.step);
    if (!ClayWidgets_Slider(widgets_.get(), id(name), &current, options))
        return false;
    value = static_cast<int>(std::lround(current));
    return true;
}
bool View::choice_dropdown(const char* name, const char* title, const char* const* choices, int count,
                           int& value) {
    std::vector<Clay_String> items;
    for (int i = 0; i < count; ++i)
        items.push_back(string(choices[i]));
    int32_t selected = value;
    text(title, settings_body, {210, 220, 234, 255});
    if (!ClayWidgets_Combo(widgets_.get(), id(name), CLAY_STRING(""), items.data(), count, &selected))
        return false;
    value = selected;
    return true;
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
    text("Update interval", settings_body, {210, 220, 234, 255});
    if (!ClayWidgets_Combo(widgets_.get(), id(name), CLAY_STRING(""), items.data(),
                           static_cast<int32_t>(items.size()), &selected))
        return false;
    seconds = values[static_cast<std::size_t>(selected)];
    return true;
}
void View::settings_panel(const Usage& data, Frame& result, ClayWidgets_Input input) {
    auto root = column(16, 14);
    root.layout.sizing.height = CLAY_SIZING_GROW(0);
    root.backgroundColor = background_color();
    CLAY (CLAY_ID("SettingsPanel"), root) {
        auto heading = column(0, 12);
        heading.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        heading.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
        CLAY (CLAY_ID("SettingsHeading"), heading) {
            text("Settings", 24, {236, 243, 250, 255});
            text("Live preview / Cancel restores saved settings", settings_help, {166, 187, 208, 255});
        }
        auto panels = column(0, 16);
        panels.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        panels.layout.sizing.height = CLAY_SIZING_GROW(0);
        CLAY (CLAY_ID("ConfigurationPanels"), panels) {
            auto appearance = column(0, 10);
            appearance.layout.sizing.width = CLAY_SIZING_FIXED(330);
            appearance.layout.sizing.height = CLAY_SIZING_GROW(0);
            CLAY (CLAY_ID("AppearancePanel"), appearance) {
                text("Appearance", 20, {236, 243, 250, 255});
                ClayWidgets_ScrollPanelOptions scroll{};
                scroll.width = CLAY_SIZING_GROW(0);
                scroll.height = CLAY_SIZING_GROW(0);
                scroll.padding = 12;
                scroll.childGap = 10;
                scroll.fadeMargin = 8;
                ClayWidgets_BeginScrollPanel(widgets_.get(), CLAY_ID("AppearanceScroll"), scroll);
                auto& a = preferences_.appearance;
                result.changed = setting_slider("TextSizeSlider", "Text size", a.text_percent,
                                                preference_limits::text_percent, "%") ||
                                 result.changed;
                static const char* fonts[] = {"Roboto", "Segoe UI", "Consolas"};
                result.changed = choice_dropdown("FontChoice", "Font", fonts, 3, a.font) || result.changed;
                static const char* accents[] = {"Accent: Teal", "Accent: Blue", "Accent: Purple"};
                if (ClayWidgets_Button(widgets_.get(), CLAY_ID("AccentChoice"), string(accents[a.accent]))) {
                    a.accent = (a.accent + 1) % 3;
                    result.changed = true;
                }
                static const char* themes[] = {"Midnight", "Slate"};
                result.changed =
                    choice_dropdown("ThemeChoice", "Theme", themes, 2, a.theme) || result.changed;
                result.changed = setting_slider("WidgetWidth", "Widget width", a.widget_width,
                                                preference_limits::widget_width, " px") ||
                                 result.changed;
                result.changed = setting_slider("WidgetPosition", "Taskbar position", a.position,
                                                preference_limits::position, "% from left") ||
                                 result.changed;
                result.changed = setting_slider("BarHeight", "Bar thickness", a.bar_height,
                                                preference_limits::bar_height, " px") ||
                                 result.changed;
                result.changed = setting_slider("CornerRadius", "Corner radius", a.corner_radius,
                                                preference_limits::corner_radius, " px") ||
                                 result.changed;
                result.changed =
                    ClayWidgets_Checkbox(widgets_.get(), CLAY_ID("ShowResets"),
                                         CLAY_STRING("Show taskbar reset labels"), &a.show_resets) ||
                    result.changed;
                result.changed = ClayWidgets_Checkbox(widgets_.get(), CLAY_ID("HoverEnabled"),
                                                      CLAY_STRING("Show hover card"), &a.hover_enabled) ||
                                 result.changed;
                result.changed = setting_slider("HoverWidth", "Hover width", a.hover_width,
                                                preference_limits::hover_width, " px") ||
                                 result.changed;
                result.changed = setting_slider("HoverOpacity", "Hover opacity", a.hover_opacity,
                                                preference_limits::hover_opacity, "%") ||
                                 result.changed;
                result.changed = setting_slider("HoverDelay", "Hover delay", a.hover_delay,
                                                preference_limits::hover_delay, " ms") ||
                                 result.changed;
                wrapped_text("Font and text size apply to the taskbar and hover card. Scroll for more "
                             "appearance controls.",
                             settings_help, {166, 187, 208, 255});
                ClayWidgets_EndScrollPanel(widgets_.get(), CLAY_ID("AppearanceScroll"));
            }
            auto providers = column(0, 10);
            providers.layout.sizing.height = CLAY_SIZING_GROW(0);
            CLAY (CLAY_ID("ProvidersPanel"), providers) {
                text("Providers", 20, {236, 243, 250, 255});
                ClayWidgets_ScrollPanelOptions scroll{};
                scroll.width = CLAY_SIZING_GROW(0);
                scroll.height = CLAY_SIZING_GROW(0);
                scroll.padding = 12;
                scroll.childGap = 12;
                scroll.fadeMargin = 8;
                ClayWidgets_BeginScrollPanel(widgets_.get(), CLAY_ID("ProvidersScroll"), scroll);
                auto cards = column(0, 14);
                cards.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
                CLAY (CLAY_ID("ProviderCards"), cards) {
                    for (int index = 0; index < 2; ++index) {
                        const bool is_codex = index == 0;
                        const auto& account = is_codex ? data.codex : data.claude;
                        auto card = column(12, 10);
                        card.backgroundColor = background_color();
                        card.cornerRadius = CLAY_CORNER_RADIUS(6);
                        CLAY (id(is_codex ? "LiveUsageCodex" : "LiveUsageClaude"), card) {
                            text(is_codex ? "Codex usage" : "Claude usage", 18, {236, 243, 250, 255});
                            auto& enabled =
                                is_codex ? preferences_.codex_enabled : preferences_.claude_enabled;
                            result.changed =
                                ClayWidgets_Checkbox(
                                    widgets_.get(), id(is_codex ? "EnableCodex" : "EnableClaude"),
                                    string(is_codex ? "Enable Codex" : "Enable Claude"), &enabled) ||
                                result.changed;
                            text(account.installed
                                     ? (enabled ? (account.error.empty() ? "Detected / tracking"
                                                                         : "Detected / refresh failed")
                                                : "Detected / disabled")
                                     : "Not detected",
                                 settings_body,
                                 account.installed ? accent_color() : Clay_Color{240, 180, 90, 255});
                            auto& interval =
                                is_codex ? preferences_.codex_interval : preferences_.claude_interval;
                            result.changed =
                                interval_dropdown(is_codex ? "CodexInterval" : "ClaudeInterval", interval) ||
                                result.changed;
                            wrapped_text(account.updated
                                             ? "Last update: " + date(account.updated) + " (local)"
                                             : "Last update: Never",
                                         settings_body, {166, 187, 208, 255});
                            wrapped_text("Plan: " + (account.plan.empty() ? std::string("Not reported")
                                                                          : account.plan),
                                         settings_body, {210, 220, 234, 255});
                            text("Executable", settings_body, {166, 187, 208, 255});
                            if (account.executable_path.empty())
                                text("Not found", settings_body, {166, 187, 208, 255});
                            else {
                                // Preserve every character while wrapping long executable paths.
                                for (std::size_t offset = 0; offset < account.executable_path.size();) {
                                    auto end = std::min(offset + 28, account.executable_path.size());
                                    while (end < account.executable_path.size() &&
                                           (static_cast<unsigned char>(account.executable_path[end]) &
                                            0xc0) == 0x80)
                                        ++end;
                                    text(label(account.executable_path.substr(offset, end - offset)),
                                         settings_body, {166, 187, 208, 255});
                                    offset = end;
                                }
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
                                wrapped_text(window.label, settings_body, {236, 243, 250, 255});
                                compact_bar(label(std::string(is_codex ? "SettingsCodex" : "SettingsClaude") +
                                                  std::to_string(i)),
                                            "", window.remaining,
                                            label(std::to_string(window.remaining) + "%"));
                                wrapped_text(std::to_string(window.remaining) + "% remaining / " +
                                                 std::to_string(100 - window.remaining) + "% used",
                                             settings_body, {166, 187, 208, 255});
                                wrapped_text("Resets: " + date(window.resets_at) + " (local)", settings_body,
                                             {166, 187, 208, 255});
                            }
                        }
                    }
                }
                ClayWidgets_EndScrollPanel(widgets_.get(), CLAY_ID("ProvidersScroll"));
            }
        }
        auto actions = column(0, 12);
        actions.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        actions.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
        CLAY (CLAY_ID("SettingsActions"), actions) {
            if (ClayWidgets_Button(widgets_.get(), CLAY_ID("ResetAppearance"),
                                   CLAY_STRING("Reset appearance to defaults"))) {
                preferences_.appearance = Appearance{};
                result.changed = true;
            }
            result.refresh = ClayWidgets_Button(widgets_.get(), CLAY_ID("RefreshUsage"),
                                                CLAY_STRING("Refresh usage and detection"));
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
void View::text(const char* value, uint16_t size, Clay_Color tint, int text_percent) {
    if (text_percent == 0)
        text_percent = preferences_.appearance.text_percent;
    Clay_TextElementConfig config{};
    config.fontSize = static_cast<uint16_t>(std::lround(
        size * (surface_ == Surface::Widget || surface_ == Surface::Hover ? text_percent / 100.f : 1.f)));
    if (surface_ == Surface::Widget || surface_ == Surface::Hover)
        config.fontId = static_cast<uint16_t>(preferences_.appearance.font);
    config.textColor = surface_ == Surface::Widget && system_light_ ? Clay_Color{35, 40, 48, 255} : tint;
    config.wrapMode = CLAY_TEXT_WRAP_NONE;
    CLAY_TEXT(string(value), config);
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
void View::compact_bar(const char* name, const char* label, int value, std::string_view percent, bool aligned,
                       float percent_width, int text_percent) {
    if (text_percent == 0)
        text_percent = preferences_.appearance.text_percent;
    const uint16_t text_size = surface_ == Surface::Settings ? settings_body : 10;
    auto row = column(0, 0);
    row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    row.layout.childGap = aligned ? 3 : 5;
    row.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    CLAY (id(name), row) {
        if (surface_ == Surface::Hover || aligned) {
            auto name_column = column(0, 0);
            float label_width = 90.f * text_percent / 100.f;
            if (aligned) {
                const char* longest = std::strchr(label, '*') ? "General *" : "General";
                Clay_TextElementConfig config{};
                config.fontId = static_cast<uint16_t>(preferences_.appearance.font);
                config.fontSize = static_cast<uint16_t>(std::lround(text_size * text_percent / 100.f));
                const Clay_StringSlice sample{static_cast<int32_t>(std::strlen(longest)), longest, longest};
                label_width =
                    std::ceil(widgets_->measureText(sample, &config, widgets_->measureTextUserData).width);
            }
            name_column.layout.sizing.width = CLAY_SIZING_FIXED(label_width);
            CLAY_AUTO_ID (name_column) {
                text(label, text_size, {172, 190, 210, 255}, text_percent);
            }
        } else
            text(label, text_size, {172, 190, 210, 255}, text_percent);
        Clay_ElementDeclaration track{};
        track.layout.sizing.width = CLAY_SIZING_GROW(0);
        track.layout.sizing.height =
            CLAY_SIZING_FIXED(static_cast<float>(preferences_.appearance.bar_height));
        track.backgroundColor = widgets_->theme.borderColor;
        track.cornerRadius = CLAY_CORNER_RADIUS(static_cast<float>(preferences_.appearance.corner_radius));
        CLAY (id(this->label(std::string(name) + "Track")), track) {
            Clay_ElementDeclaration fill{};
            fill.layout.sizing.width = CLAY_SIZING_PERCENT(static_cast<float>(value) / 100.f);
            fill.layout.sizing.height = CLAY_SIZING_GROW(0);
            const bool branded = std::strstr(name, "Codex") || std::strstr(name, "Claude");
            fill.backgroundColor =
                branded      ? provider_color(name, std::strstr(name, "Fable") || std::strstr(label, "Fable"))
                : value > 30 ? accent_color()
                             : color(bar_color(value));
            fill.cornerRadius = CLAY_CORNER_RADIUS(static_cast<float>(preferences_.appearance.corner_radius));
            CLAY_AUTO_ID (fill) {
            }
        }
        if (surface_ == Surface::Hover || aligned) {
            auto value_column = column(0, 0);
            value_column.layout.sizing.width =
                CLAY_SIZING_FIXED(aligned ? percent_width : 28.f * text_percent / 100.f);
            value_column.layout.childAlignment.x = aligned ? CLAY_ALIGN_X_LEFT : CLAY_ALIGN_X_RIGHT;
            CLAY_AUTO_ID (value_column) {
                text(percent.data(), text_size, {236, 243, 250, 255}, text_percent);
            }
        } else
            text(percent.data(), text_size, {236, 243, 250, 255}, text_percent);
    }
}
void View::claude_only(const AccountUsage& account, const Allowance& weekly, const Allowance& fable) {
    const auto general_percent = std::to_string(weekly.remaining) + "%";
    const auto fable_percent = std::to_string(fable.remaining) + "%";
    Clay_TextElementConfig config{};
    config.fontId = static_cast<uint16_t>(preferences_.appearance.font);
    config.fontSize = static_cast<uint16_t>(std::lround(10 * preferences_.appearance.text_percent / 100.f));
    const auto measure_percent = [&](const std::string& value) {
        const Clay_StringSlice sample{static_cast<int32_t>(value.size()), value.c_str(), value.c_str()};
        return widgets_->measureText(sample, &config, widgets_->measureTextUserData).width;
    };
    const float percent_width =
        std::ceil(std::max(measure_percent(general_percent), measure_percent(fable_percent)));
    auto row = column(0, 6);
    row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    row.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    CLAY (CLAY_ID("ClaudeOnly"), row) {
        auto bars = column(0, 2);
        if (preferences_.appearance.text_percent > 160) {
            bars.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
            bars.layout.childGap = 12;
        }
        CLAY (CLAY_ID("ClaudeOnlyBars"), bars) {
            compact_bar("ClaudeGeneral", account.error.empty() ? "General" : "General *", weekly.remaining,
                        label(general_percent), true, percent_width);
            compact_bar("ClaudeFable", account.error.empty() ? "Fable" : "Fable *", fable.remaining,
                        label(fable_percent), true, percent_width);
        }
        if (preferences_.appearance.show_resets) {
            const bool shared =
                weekly.resets_at > 0 && fable.resets_at > 0 &&
                reset_time(weekly.resets_at, true, true) == reset_time(fable.resets_at, true, true);
            const bool narrow = preferences_.appearance.widget_width < 208;
            auto first = reset_time(weekly.resets_at, narrow);
            auto second = reset_time(fable.resets_at, narrow);
            if (shared) {
                first = reset_time(weekly.resets_at, true);
                second = reset_time(weekly.resets_at).substr(6);
                const auto other = reset_time(fable.resets_at).substr(6);
                if (second != other)
                    second += "/" + other;
            }
            auto resets = column(0, 2);
            resets.layout.sizing.width = CLAY_SIZING_FIT(0);
            resets.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
            CLAY (CLAY_ID("ClaudeResetColumn"), resets) {
                // Two reset lines must fit the fixed taskbar height at every text scale.
                const auto size =
                    static_cast<uint16_t>(std::min(9.f, 1600.f / preferences_.appearance.text_percent));
                text(label(first), size, {166, 187, 208, 255});
                text(label(second), size, {166, 187, 208, 255});
            }
        }
    }
}
void View::taskbar_allowance(const char* name, const char* title, const Allowance& allowance, bool stale,
                             bool show_reset, bool date_only) {
    auto row = column(0, 8);
    row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    row.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    CLAY_AUTO_ID (row) {
        compact_bar(name, label(std::string(title) + (stale ? " *" : "")), allowance.remaining,
                    label(std::to_string(allowance.remaining) + "%"));
        if (preferences_.appearance.show_resets && show_reset)
            text(label(std::string(std::strcmp(name, "Codex") == 0 ? "" : "reset ") +
                       reset_time(allowance.resets_at,
                                  date_only || preferences_.appearance.widget_width < 208)),
                 9, {166, 187, 208, 255});
    }
}
void View::claude_bars(const AccountUsage& account, const Allowance& weekly, const Allowance& fable) {
    const bool narrow = preferences_.appearance.widget_width < 208;
    auto row = column(0, narrow ? 3 : 5);
    row.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    row.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    CLAY (CLAY_ID("Claude"), row) {
        text(account.error.empty() ? "Claude" : "Claude *", 10, {172, 190, 210, 255});
        auto tracks = column(0, 2);
        CLAY (CLAY_ID("ClaudeTracks"), tracks) {
            for (int i = 0; i < 2; ++i) {
                Clay_ElementDeclaration track{};
                track.layout.sizing.width = CLAY_SIZING_GROW(0);
                track.layout.sizing.height = CLAY_SIZING_FIXED(
                    1.5f * static_cast<float>(std::max(1, (preferences_.appearance.bar_height - 1) / 2)));
                track.backgroundColor = widgets_->theme.borderColor;
                track.cornerRadius =
                    CLAY_CORNER_RADIUS(static_cast<float>(preferences_.appearance.corner_radius));
                CLAY (id(i == 0 ? "ClaudeWeeklyTrack" : "ClaudeFableTrack"), track) {
                    Clay_ElementDeclaration fill{};
                    fill.layout.sizing.width =
                        CLAY_SIZING_PERCENT((i == 0 ? weekly.remaining : fable.remaining) / 100.f);
                    fill.layout.sizing.height = CLAY_SIZING_GROW(0);
                    fill.backgroundColor = provider_color("Claude", i == 1);
                    fill.cornerRadius =
                        CLAY_CORNER_RADIUS(static_cast<float>(preferences_.appearance.corner_radius));
                    CLAY_AUTO_ID (fill) {
                    }
                }
            }
        }
        text(label(std::to_string(weekly.remaining) + (narrow ? "/" : " / ") +
                   std::to_string(fable.remaining) + "%"),
             10, {236, 243, 250, 255});
        if (preferences_.appearance.show_resets) {
            auto resets = reset_time(weekly.resets_at, true);
            if (weekly.resets_at <= 0 || fable.resets_at <= 0 ||
                reset_time(weekly.resets_at, true, true) != reset_time(fable.resets_at, true, true))
                resets += (narrow ? "|" : " / ") + reset_time(fable.resets_at, true);
            text(label(std::string(narrow ? "" : "reset ") + resets), 9, {166, 187, 208, 255});
        }
    }
}
const char* View::label(std::string value) {
    labels_.push_back(std::move(value));
    return labels_.back().c_str();
}
Clay_Dimensions widget_size(const Usage& data, const Appearance& appearance) {
    int rows = 1;
    if (data.live && appearance.text_percent > 160) {
        if (data.codex_active() && data.claude_active())
            rows = 2;
        else if (data.claude_active()) {
            const auto& windows = data.claude.windows;
            const bool weekly = std::any_of(windows.begin(), windows.end(),
                                            [](const auto& w) { return w.label == "Weekly"; });
            const bool fable = std::any_of(windows.begin(), windows.end(),
                                           [](const auto& w) { return w.label == "Fable weekly"; });
            if (weekly && fable)
                rows = 2;
        }
    }
    return {appearance.widget_width * appearance.text_percent / 100.f * rows,
            static_cast<float>(widget_height)};
}
Clay_Dimensions hover_size(const Usage& data, int text_percent) {
    const float scale = text_percent / 100.f;
    if (!data.live)
        return {320.f * scale, 250.f * scale};
    const auto font = [scale](int size) { return static_cast<float>(std::lround(size * scale)); };
    // Padding and layout gaps stay fixed; only text grows with the preference.
    float height = 28 + font(16) + font(10) + 10;
    int providers = 0;
    for (const auto* account : {&data.codex, &data.claude}) {
        if (!(account == &data.codex ? data.codex_active() : data.claude_active()))
            continue;
        ++providers;
        height += 10 + font(12);
        height += account->windows.empty() ? 6 + font(11)
                                           : (2 * font(10) + 9) * static_cast<float>(account->windows.size());
    }
    if (!providers)
        height += 10 + font(12);
    return {360.f * scale, height + 4};
}
void View::hover_usage(const Usage& data) {
    auto root = column(14, 10);
    root.layout.sizing.height = CLAY_SIZING_GROW(0);
    root.backgroundColor = background_color();
    CLAY (CLAY_ID("HoverOverview"), root) {
        auto heading = column(0, 6);
        heading.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        heading.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
        CLAY_AUTO_ID (heading) {
            text("Usage", 16, {236, 243, 250, 255});
            Clay_ElementDeclaration space{};
            space.layout.sizing.width = CLAY_SIZING_GROW(0);
            CLAY_AUTO_ID (space) {
            }
            text("Remaining / resets locally", 10, {157, 174, 193, 255});
        }
        for (const auto& entry :
             {std::pair<const char*, const AccountUsage*>{"Codex", &data.codex}, {"Claude", &data.claude}}) {
            const auto& account = *entry.second;
            if (!(entry.second == &data.codex ? data.codex_active() : data.claude_active()))
                continue;
            auto provider = column(0, 6);
            CLAY (id(label(std::string("Hover") + entry.first)), provider) {
                text(label(std::string(entry.first) + (account.error.empty()     ? ""
                                                       : account.windows.empty() ? " - unavailable"
                                                                                 : " - stale")),
                     12, account.error.empty() ? provider_color(entry.first) : Clay_Color{240, 180, 90, 255});
                if (account.windows.empty())
                    text(account.error.empty() ? "Connecting..." : "Refresh failed. Click for details.", 11,
                         {157, 174, 193, 255});
                for (std::size_t i = 0; i < account.windows.size(); ++i) {
                    const auto& window = account.windows[i];
                    auto allowance = column(0, 3);
                    CLAY_AUTO_ID (allowance) {
                        compact_bar(label(std::string("Hover") + entry.first + std::to_string(i)),
                                    window.label.c_str(), window.remaining,
                                    label(std::to_string(window.remaining) + "%"));
                        text(label(window.resets_at > 0 ? "Resets " + date(window.resets_at)
                                                        : "Reset unavailable"),
                             10, {157, 174, 193, 255});
                    }
                }
            }
        }
        if (!data.codex_active() && !data.claude_active())
            text(!data.codex_enabled && !data.claude_enabled ? "Providers disabled - open settings"
                                                             : "No supported installations detected",
                 12, {157, 174, 193, 255});
        text("Click for settings and usage", 10, {157, 174, 193, 255});
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
        text(label(std::string(provider) + (compact ? stale ? "   STALE" : "   REMAINING" : " usage")),
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
                const auto* percent = label(std::to_string(window.remaining) + "%");
                if (compact)
                    compact_bar(name, window.label.c_str(), window.remaining, percent);
                else {
                    auto card = column(10, 6);
                    card.backgroundColor = widgets_->theme.surfaceColor;
                    card.cornerRadius =
                        CLAY_CORNER_RADIUS(static_cast<float>(preferences_.appearance.corner_radius));
                    CLAY_AUTO_ID (card) {
                        compact_bar(name, window.label.c_str(), window.remaining, percent);
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
    const bool midnight = preferences_.appearance.theme == 0;
    widgets_->theme.accentColor = accent_color();
    widgets_->theme.focusRingColor = accent_color();
    const auto selected_accent = accent_color();
    widgets_->theme.accentMutedColor = {selected_accent.r * 0.35f, selected_accent.g * 0.35f,
                                        selected_accent.b * 0.35f, 255};
    widgets_->theme.surfaceColor = midnight ? Clay_Color{24, 31, 42, 255} : Clay_Color{44, 51, 63, 255};
    widgets_->theme.surfaceAltColor = midnight ? Clay_Color{32, 41, 55, 255} : Clay_Color{55, 65, 80, 255};
    widgets_->theme.borderColor = midnight ? Clay_Color{48, 60, 77, 255} : Clay_Color{76, 88, 105, 255};
    if (surface_ == Surface::Widget && system_light_)
        widgets_->theme.borderColor = {180, 187, 196, 255};
    widgets_->theme.hoverColor = midnight ? Clay_Color{43, 54, 71, 255} : Clay_Color{66, 77, 94, 255};
    widgets_->theme.fontSizeBody = surface_ == Surface::Settings ? settings_body : 18;
}

void View::live_panel(const Usage& data, Frame& result, ClayWidgets_Input input) {
    if (surface_ == Surface::Widget && (data.codex_active() || data.claude_active())) {
        const bool horizontal = preferences_.appearance.text_percent > 160;
        auto root = column(6, horizontal ? 12 : 2);
        root.layout.padding.top = root.layout.padding.bottom = 2;
        root.layout.sizing.height = CLAY_SIZING_GROW(0);
        root.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
        if (horizontal)
            root.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
        CLAY (CLAY_ID("Providers"), root) {
            for (const auto& entry : {std::pair<const char*, const AccountUsage*>{"Codex", &data.codex},
                                      {"Claude", &data.claude}}) {
                const auto& account = *entry.second;
                if (!(entry.second == &data.codex ? data.codex_active() : data.claude_active()))
                    continue;
                if (entry.second == &data.claude) {
                    const auto weekly = std::find_if(account.windows.begin(), account.windows.end(),
                                                     [](const auto& w) { return w.label == "Weekly"; });
                    const auto fable = std::find_if(account.windows.begin(), account.windows.end(),
                                                    [](const auto& w) { return w.label == "Fable weekly"; });
                    if (weekly != account.windows.end() && fable != account.windows.end()) {
                        if (!data.codex_active()) {
                            claude_only(account, *weekly, *fable);
                        } else
                            claude_bars(account, *weekly, *fable);
                        continue;
                    }
                }
                const auto lowest =
                    std::min_element(account.windows.begin(), account.windows.end(),
                                     [](const auto& a, const auto& b) { return a.remaining < b.remaining; });
                if (lowest == account.windows.end())
                    text(label(std::string(entry.first) +
                               (account.error.empty() ? ": connecting..." : ": unavailable")),
                         10, {240, 180, 90, 255});
                else if (entry.second == &data.codex && !data.claude_active() &&
                         preferences_.appearance.show_resets) {
                    auto codex = column(0, 2);
                    CLAY (CLAY_ID("CodexOnly"), codex) {
                        // Keep both lines legible inside the taskbar's fixed height.
                        const int percent = std::min(preferences_.appearance.text_percent, 160);
                        compact_bar("Codex", account.error.empty() ? "Codex" : "Codex *", lowest->remaining,
                                    label(std::to_string(lowest->remaining) + "%"), false, 0, percent);
                        auto reset = column(0, 0);
                        reset.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
                        CLAY (CLAY_ID("CodexReset"), reset) {
                            text(label("reset " + reset_time(lowest->resets_at)), 9, {166, 187, 208, 255},
                                 percent);
                        }
                    }
                } else
                    taskbar_allowance(entry.first, entry.first, *lowest, !account.error.empty());
            }
        }
    } else if (!data.codex_active() && !data.claude_active()) {
        auto root = column(6, 4);
        CLAY (CLAY_ID("NoProviders"), root) {
            const bool narrow = surface_ == Surface::Widget && preferences_.appearance.widget_width < 208;
            text(!data.codex_enabled && !data.claude_enabled
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
        if (preferences_.appearance.text_percent <= 160)
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
            card.cornerRadius = CLAY_CORNER_RADIUS(static_cast<float>(preferences_.appearance.corner_radius));
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
    text_size_label_ = std::to_string(preferences_.appearance.text_percent) + "%";
    auto panel = column(0, 0);
    panel.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    panel.layout.sizing.height = CLAY_SIZING_GROW(0);
    panel.backgroundColor = background_color();
    CLAY (CLAY_ID("SettingsPanel"), panel) {
        auto root = column(24, 18);
        root.layout.sizing.height = CLAY_SIZING_GROW(0);
        CLAY (CLAY_ID("Settings"), root) {
            text("UsageTracker", 26, {236, 243, 250, 255});
            text("Appearance", 17, {236, 243, 250, 255});
            text("Widget and hover text size", 14, {157, 174, 193, 255});
            text(text_size_label_.c_str(), 20, accent_color());
            float value = static_cast<float>(preferences_.appearance.text_percent);
            ClayWidgets_SliderOptions options{};
            options.minValue = static_cast<float>(preference_limits::text_percent.min);
            options.maxValue = static_cast<float>(preference_limits::text_percent.max);
            options.step = static_cast<float>(preference_limits::text_percent.step);
            if (ClayWidgets_Slider(widgets_.get(), CLAY_ID("TextSizeSlider"), &value, options)) {
                set_text_percent(static_cast<int>(std::lround(value)));
                result.changed = true;
            }
            text("150% default  /  100-300% range", 12, {157, 174, 193, 255});
            text("Larger text uses more taskbar space.", 11, {157, 174, 193, 255});
            if (ClayWidgets_Button(widgets_.get(), CLAY_ID("ResetTextSize"),
                                   CLAY_STRING("Reset to default"))) {
                set_text_percent(150);
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
Frame View::frame(Usage& data, ClayWidgets_Input input, float width, float height) {
    Clay_SetCurrentContext(clay_);
    labels_.clear();
    apply_theme();
    Frame result;
    const bool dismiss_dropdown = input.keyEscape && widgets_->openComboId != 0;
    ClayWidgets_BeginFrame(widgets_.get(), input, {width, height}, false);
    if (data.live) {
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
        }
    }
    if (dismiss_dropdown)
        result.close = false;
    result.commands = ClayWidgets_EndFrame(widgets_.get());
    return result;
}
} // namespace usage::ui
