#include "windows/app.hpp"
#include "windows/platform.hpp"
#include <algorithm>
#include <fstream>

namespace usage::windows {
namespace {
struct AppearanceField { const wchar_t* key; const char* name; int Appearance::* member; };
constexpr AppearanceField appearance_fields[] = {
    {L"TextPercent","TextPercent",&Appearance::text_percent}, {L"Font","Font",&Appearance::font},
    {L"Accent","Accent",&Appearance::accent}, {L"Theme","Theme",&Appearance::theme},
    {L"WidgetWidth","WidgetWidth",&Appearance::widget_width}, {L"HoverWidth","HoverWidth",&Appearance::hover_width},
    {L"HoverOpacity","HoverOpacity",&Appearance::hover_opacity}, {L"HoverDelay","HoverDelay",&Appearance::hover_delay},
    {L"BarHeight","BarHeight",&Appearance::bar_height}, {L"CornerRadius","CornerRadius",&Appearance::corner_radius}
};
}
void App::load_settings() {
    wchar_t local[32768]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA",local,static_cast<DWORD>(std::size(local)))) {
        settings_path_ = std::filesystem::path(local) / L"UsageTracker" / L"settings.ini";
        auto read = [&](const wchar_t* section, const wchar_t* key, int fallback) {
            return static_cast<int>(GetPrivateProfileIntW(section,key,fallback,settings_path_.c_str()));
        };
        for (const auto& field : appearance_fields) preferences_.appearance.*(field.member)=read(L"Appearance",field.key,preferences_.appearance.*(field.member));
        preferences_.appearance.show_resets=read(L"Appearance",L"ShowResets",1)!=0;
        preferences_.appearance.hover_enabled=read(L"Appearance",L"HoverEnabled",1)!=0;
        preferences_.codex_enabled=read(L"Providers",L"Codex",1)!=0;
        preferences_.claude_enabled=read(L"Providers",L"Claude",1)!=0;
        preferences_.codex_interval=read(L"Providers",L"CodexInterval",60);
        preferences_.claude_interval=read(L"Providers",L"ClaudeInterval",60);
    }
    if (smoke_) {
        settings_path_ = executable_directory() / L"smoke-settings.ini";
        preferences_ = Preferences{};
    }
    preferences_.normalize();
    usage_.codex_enabled=preferences_.codex_enabled;
    usage_.claude_enabled=preferences_.claude_enabled;
    apply_text_size();
}

bool App::save_settings() {
    std::error_code error;
    if (!settings_path_.empty()) std::filesystem::create_directories(settings_path_.parent_path(),error);
    auto temporary=settings_path_; temporary+=L".tmp";
    const auto& value=details_view_.preferences();
    bool saved=false;
    if (!settings_path_.empty() && !error) {
        std::ofstream file(temporary,std::ios::trunc);
        file << "[Appearance]\n";
        for (const auto& field : appearance_fields) file << field.name << '=' << value.appearance.*(field.member) << '\n';
        file << "ShowResets=" << value.appearance.show_resets << "\nHoverEnabled=" << value.appearance.hover_enabled
             << "\n[Providers]\nCodex=" << value.codex_enabled << "\nClaude=" << value.claude_enabled
             << "\nCodexInterval=" << value.codex_interval << "\nClaudeInterval=" << value.claude_interval << '\n';
        file.close();
        saved=file.good() && MoveFileExW(temporary.c_str(),settings_path_.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    if (!saved) {
        MessageBoxW(popup_,L"Could not save your preferences. Check that your local app data folder is writable.",L"UsageTracker settings",MB_OK | MB_ICONERROR);
        return false;
    }
    apply_preferences(value);
    settings_preview_=false;
    return true;
}

void App::detect_providers() {
    if (demo_mode_) return;
    detect_service(Service::Codex,usage_.account);
    detect_service(Service::Claude,usage_.claude);
}
void App::apply_providers() {
    if (demo_mode_) return;
    if (usage_.codex_enabled) {
        if (!codex_) codex_=std::make_unique<UsageReader>(Service::Codex,usage_.account);
        codex_->set_interval(preferences_.codex_interval);
    } else codex_.reset();
    if (usage_.claude_enabled) {
        if (!claude_) claude_=std::make_unique<UsageReader>(Service::Claude,usage_.claude);
        claude_->set_interval(preferences_.claude_interval);
    } else claude_.reset();
}
void App::apply_preferences(Preferences value) {
    value.normalize();
    if (preferences_==value) return;
    preferences_=value;
    usage_.codex_enabled=value.codex_enabled;
    usage_.claude_enabled=value.claude_enabled;
    apply_text_size();
    apply_providers();
    if (!value.appearance.hover_enabled) hide_hover();
    tick();
    update_usage();
}
void App::apply_text_size() {
    widget_view_.set_preferences(preferences_);
    hover_view_.set_preferences(preferences_);
}
} // namespace usage::windows
