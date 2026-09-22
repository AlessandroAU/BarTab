#include "windows/settings_store.hpp"
#include <windows.h>
#include <fstream>

namespace usage::windows {
namespace {
struct AppearanceField {
    const wchar_t* key;
    const char* name;
    int Appearance::*member;
    // Missing keys copy this already-loaded member instead of the compiled default.
    int Appearance::*fallback{};
};
constexpr AppearanceField appearance_fields[] = {
    {L"TextPercent", "TextPercent", &Appearance::text_percent},
    {L"HoverTextPercent", "HoverTextPercent", &Appearance::hover_text_percent, &Appearance::text_percent},
    {L"WidgetWidth", "WidgetWidth", &Appearance::widget_width},
    {L"HoverOpacity", "HoverOpacity", &Appearance::hover_opacity},
    {L"HoverDelay", "HoverDelay", &Appearance::hover_delay},
    {L"BarHeight", "BarHeight", &Appearance::bar_height},
    {L"Position", "Position", &Appearance::position}};
} // namespace
Preferences read_settings(const std::filesystem::path& path) {
    Preferences value;
    if (path.empty())
        return value;
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error)
        return value;
    auto read = [&](const wchar_t* section, const wchar_t* key, int fallback) {
        return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, absolute.c_str()));
    };
    for (const auto& field : appearance_fields)
        value.appearance.*(field.member) =
            read(L"Appearance", field.key, value.appearance.*(field.fallback ? field.fallback : field.member));
    value.appearance.show_resets = read(L"Appearance", L"ShowResets", value.appearance.show_resets) != 0;
    value.appearance.hover_enabled =
        read(L"Appearance", L"HoverEnabled", value.appearance.hover_enabled) != 0;
    value.appearance.twelve_hour_time = read(L"Appearance", L"TwelveHourTime", 0) != 0;
    value.codex_enabled = read(L"Providers", L"Codex", value.codex_enabled) != 0;
    value.claude_enabled = read(L"Providers", L"Claude", value.claude_enabled) != 0;
    value.codex_interval = read(L"Providers", L"CodexInterval", value.codex_interval);
    value.claude_interval = read(L"Providers", L"ClaudeInterval", value.claude_interval);
    value.normalize();
    return value;
}

bool write_settings(const std::filesystem::path& path, Preferences value) {
    value.normalize();
    std::error_code error;
    if (!path.empty() && !path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), error);
    auto temporary = path;
    temporary += L".tmp";
    bool saved = false;
    if (!path.empty() && !error) {
        std::ofstream file(temporary, std::ios::trunc);
        file << "[Appearance]\n";
        for (const auto& field : appearance_fields)
            file << field.name << '=' << value.appearance.*(field.member) << '\n';
        file << "ShowResets=" << value.appearance.show_resets
             << "\nTwelveHourTime=" << value.appearance.twelve_hour_time
             << "\nHoverEnabled=" << value.appearance.hover_enabled
             << "\n[Providers]\nCodex=" << value.codex_enabled << "\nClaude=" << value.claude_enabled
             << "\nCodexInterval=" << value.codex_interval << "\nClaudeInterval=" << value.claude_interval
             << '\n';
        file.close();
        saved = file.good() && MoveFileExW(temporary.c_str(), path.c_str(),
                                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    if (!saved && !path.empty()) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    return saved;
}
} // namespace usage::windows
