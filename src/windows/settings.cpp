#include "windows/app.hpp"
#include "windows/platform.hpp"
#include "windows/settings_store.hpp"

namespace usage::windows {
void App::load_settings() {
    if (smoke_) {
        settings_path_ = executable_directory() / L"smoke-settings.ini";
        preferences_ = Preferences{};
    } else {
        wchar_t local[32768]{};
        const auto length =
            GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
        if (length > 0 && length < std::size(local))
            settings_path_ = std::filesystem::path(local) / L"UsageTracker" / L"settings.ini";
        preferences_ = read_settings(settings_path_);
    }
    usage_.codex_enabled = preferences_.codex_enabled;
    usage_.claude_enabled = preferences_.claude_enabled;
    apply_view_preferences();
}

void App::begin_settings_preview() {
    settings_edit_.begin(preferences_);
    details_view_.set_preferences(preferences_);
}

void App::preview_settings(Preferences value) {
    apply_preferences(settings_edit_.preview(value));
}

void App::cancel_settings_preview() {
    if (const auto original = settings_edit_.cancel())
        apply_preferences(*original);
}

bool App::save_settings(Preferences value) {
    value.normalize();
    if (!write_settings(settings_path_, value)) {
        MessageBoxW(popup_,
                    L"Could not save your preferences. Check that your local app data folder is writable.",
                    L"UsageTracker settings", MB_OK | MB_ICONERROR);
        return false;
    }
    apply_preferences(value);
    settings_edit_.commit();
    return true;
}

void App::apply_preferences(Preferences value) {
    value.normalize();
    if (preferences_ == value)
        return;
    preferences_ = value;
    usage_.codex_enabled = value.codex_enabled;
    usage_.claude_enabled = value.claude_enabled;
    apply_view_preferences();
    apply_providers();
    if (!value.appearance.hover_enabled)
        hide_hover(true);
    tick();
    update_usage();
    if (hover_pinned_ && !IsWindowVisible(hover_))
        show_hover();
}
void App::apply_view_preferences() {
    widget_view_.set_preferences(preferences_);
    hover_view_.set_preferences(preferences_);
}
} // namespace usage::windows
