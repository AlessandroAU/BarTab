#include "host/settings_store.hpp"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace usage::host {
namespace {
struct AppearanceField {
    const char* name;
    int Appearance::*member;
};
constexpr AppearanceField appearance_fields[] = {
    {"TextSize", &Appearance::text_percent},     {"HoverTextSize", &Appearance::hover_text_percent},
    {"WidgetWidth", &Appearance::widget_width},  {"HoverOpacity", &Appearance::hover_opacity},
    {"BarHeight", &Appearance::bar_height},      {"Position", &Appearance::position},
    {"WidgetHeight", &Appearance::widget_height}, {"WidgetOpacity", &Appearance::widget_opacity}};
struct SwitchField {
    const char* name;
    bool Appearance::*member;
};
// Which taskbar bars show, and what each provider's measure.
constexpr SwitchField bar_fields[] = {
    {"CodexSession", &Appearance::codex_session_bar},   {"CodexWeekly", &Appearance::codex_weekly_bar},
    {"ClaudeSession", &Appearance::claude_session_bar}, {"ClaudeWeekly", &Appearance::claude_weekly_bar},
    {"ClaudeModel", &Appearance::claude_model_bar},     {"CodexShowUsed", &Appearance::codex_show_used},
    {"ClaudeShowUsed", &Appearance::claude_show_used}};
std::string lower(std::string value) {
    for (auto& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos)
        return {};
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}
// The INI subset the app writes, read the way GetPrivateProfileInt reads it:
// section and key names ignore case, the first occurrence of a key wins, and a
// value is its leading integer, or 0 when it has none.
class IniFile {
  public:
    explicit IniFile(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        std::string line, section;
        bool first = true;
        while (std::getline(file, line)) {
            if (first && line.rfind("\xEF\xBB\xBF", 0) == 0)
                line.erase(0, 3);
            first = false;
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#')
                continue;
            if (line.front() == '[') {
                const auto close = line.find(']');
                section =
                    lower(trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
                continue;
            }
            const auto equals = line.find('=');
            if (equals != std::string::npos)
                values_.emplace(section + '\n' + lower(trim(line.substr(0, equals))),
                                trim(line.substr(equals + 1)));
        }
    }
    int read(const char* section, const char* key, int fallback) const {
        const auto found = values_.find(lower(section) + '\n' + lower(key));
        if (found == values_.end())
            return fallback;
        return static_cast<int>(std::strtol(found->second.c_str(), nullptr, 10));
    }

  private:
    std::map<std::string, std::string> values_;
};
// Replaces `path` with the finished temporary file in one step, so a reader
// never sees a half-written file.
bool replace_file(const std::filesystem::path& temporary, const std::filesystem::path& path) {
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) !=
           0;
#else
    // Flush the data before the rename publishes it.
    const int descriptor = open(temporary.c_str(), O_RDONLY);
    if (descriptor >= 0) {
        fsync(descriptor);
        close(descriptor);
    }
    return std::rename(temporary.c_str(), path.c_str()) == 0;
#endif
}
// TextPercent and HoverTextPercent were absolute scales; the sizes that replace
// them are relative to each surface's base, so 150 and 130 both become 100.
// Files from before the hover size had only TextPercent, which both reuse.
int rescaled(int old_percent, int base) {
    const int step = preference_limits::text_percent.step;
    return (old_percent * 100 / base + step / 2) / step * step;
}
} // namespace
Preferences read_settings(const std::filesystem::path& path) {
    Preferences value;
    if (path.empty())
        return value;
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error)
        return value;
    const IniFile file(absolute);
    auto read = [&](const char* section, const char* key, int fallback) {
        return file.read(section, key, fallback);
    };
    const int old_text = read("Appearance", "TextPercent", 0);
    if (old_text > 0) {
        value.appearance.text_percent = rescaled(old_text, taskbar_text_base);
        value.appearance.hover_text_percent =
            rescaled(read("Appearance", "HoverTextPercent", old_text), hover_text_base);
    }
    for (const auto& field : appearance_fields)
        value.appearance.*(field.member) = read("Appearance", field.name, value.appearance.*(field.member));
    value.appearance.show_resets = read("Appearance", "ShowResets", value.appearance.show_resets) != 0;
    value.appearance.hover_enabled = read("Appearance", "HoverEnabled", value.appearance.hover_enabled) != 0;
    value.appearance.twelve_hour_time = read("Appearance", "TwelveHourTime", 0) != 0;
    value.appearance.all_taskbars = read("Appearance", "AllTaskbars", 0) != 0;
    // BoldText was one switch for every surface; it seeds each of its successors.
    const int bold = read("Appearance", "BoldText", -1);
    auto& a = value.appearance;
    a.bold_taskbar = read("Appearance", "BoldTaskbar", bold < 0 ? a.bold_taskbar : bold) != 0;
    a.bold_hover = read("Appearance", "BoldHover", bold < 0 ? a.bold_hover : bold) != 0;
    a.bold_settings = read("Appearance", "BoldSettings", bold < 0 ? a.bold_settings : bold) != 0;
    for (const auto& field : bar_fields)
        a.*(field.member) = read("Bars", field.name, a.*(field.member)) != 0;
    value.codex_enabled = read("Providers", "Codex", value.codex_enabled) != 0;
    value.claude_enabled = read("Providers", "Claude", value.claude_enabled) != 0;
    value.codex_interval = read("Providers", "CodexInterval", value.codex_interval);
    value.claude_interval = read("Providers", "ClaudeInterval", value.claude_interval);
    value.normalize();
    return value;
}

bool write_settings(const std::filesystem::path& path, Preferences value) {
    value.normalize();
    std::error_code error;
    if (!path.empty() && !path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), error);
    auto temporary = path;
    temporary += ".tmp";
    bool saved = false;
    if (!path.empty() && !error) {
        std::ofstream file(temporary, std::ios::trunc);
        file << "[Appearance]\n";
        for (const auto& field : appearance_fields)
            file << field.name << '=' << value.appearance.*(field.member) << '\n';
        file << "ShowResets=" << value.appearance.show_resets
             << "\nBoldTaskbar=" << value.appearance.bold_taskbar
             << "\nBoldHover=" << value.appearance.bold_hover
             << "\nBoldSettings=" << value.appearance.bold_settings
             << "\nTwelveHourTime=" << value.appearance.twelve_hour_time
             << "\nHoverEnabled=" << value.appearance.hover_enabled
             << "\nAllTaskbars=" << value.appearance.all_taskbars << "\n[Bars]\n";
        for (const auto& field : bar_fields)
            file << field.name << '=' << value.appearance.*(field.member) << '\n';
        file << "[Providers]\nCodex=" << value.codex_enabled << "\nClaude=" << value.claude_enabled
             << "\nCodexInterval=" << value.codex_interval << "\nClaudeInterval=" << value.claude_interval
             << '\n';
        file.close();
        saved = file.good() && replace_file(temporary, path);
    }
    if (!saved && !path.empty()) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    return saved;
}
} // namespace usage::host
