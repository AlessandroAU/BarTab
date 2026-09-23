#include "host/startup.hpp"
#include "host/platform.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace usage::host {
namespace {
// An XDG autostart entry, which GNOME, KDE, XFCE and most other desktops run at
// login: $XDG_CONFIG_HOME/autostart/UsageTracker.desktop.
std::filesystem::path entry_path() {
    std::filesystem::path base;
    if (const char* config = std::getenv("XDG_CONFIG_HOME");
        config && std::filesystem::path(config).is_absolute())
        base = config;
    else if (const char* home = std::getenv("HOME"); home && *home)
        base = std::filesystem::path(home) / ".config";
    return base.empty() ? base : base / "autostart" / "UsageTracker.desktop";
}
// Desktop-entry quoting: the path in double quotes with ", `, $ and \ escaped,
// then the file format's own escaping on top, which doubles every backslash
// again, and %, the field-code marker, doubled.
std::string quoted(const std::string& value) {
    std::string argument = "\"";
    for (const char c : value) {
        if (c == '"' || c == '`' || c == '$' || c == '\\')
            argument += '\\';
        argument += c;
    }
    argument += '"';
    std::string result;
    for (const char c : argument) {
        if (c == '\\')
            result += "\\\\";
        else if (c == '%')
            result += "%%";
        else
            result += c;
    }
    return result;
}
} // namespace
StartupState startup_state() {
    const auto path = entry_path();
    if (path.empty())
        return {false, "No home folder to keep an autostart entry in."};
    std::ifstream file(path);
    if (!file)
        return {};
    std::stringstream contents;
    contents << file.rdbuf();
    const auto text = contents.str();
    // Desktops switch an entry off in place rather than deleting it.
    const bool disabled = text.find("\nHidden=true") != std::string::npos ||
                          text.find("\nX-GNOME-Autostart-enabled=false") != std::string::npos;
    return {!disabled, {}};
}
std::string set_startup(bool enabled) {
    const auto path = entry_path();
    if (path.empty())
        return "No home folder to keep an autostart entry in.";
    std::error_code error;
    if (!enabled) {
        std::filesystem::remove(path, error);
        return error ? "Could not remove " + path.string() + ": " + error.message() : std::string{};
    }
    const auto executable = executable_path();
    if (executable.empty())
        return "Could not find this program's location.";
    std::filesystem::create_directories(path.parent_path(), error);
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        file << "[Desktop Entry]\nType=Application\nName=UsageTracker\n"
                "Comment=Codex and Claude usage widget\nExec="
             << quoted(executable.string()) << "\nTerminal=false\nX-GNOME-Autostart-enabled=true\n";
        if (!file.good())
            return "Could not write " + temporary.string() + ".";
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return "Could not write " + path.string() + ".";
    }
    return {};
}
} // namespace usage::host
