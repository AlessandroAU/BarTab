#include "host/startup.hpp"
#include "host/platform.hpp"
#include <cstdlib>
#include <fstream>

namespace usage::host {
namespace {
// A per-user LaunchAgent, which launchd runs at login.
std::filesystem::path agent_path() {
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return {};
    return std::filesystem::path(home) / "Library/LaunchAgents/com.bartab.app.plist";
}
std::string escaped(const std::string& value) {
    std::string result;
    for (const char c : value) {
        if (c == '&')
            result += "&amp;";
        else if (c == '<')
            result += "&lt;";
        else if (c == '>')
            result += "&gt;";
        else
            result += c;
    }
    return result;
}
} // namespace
StartupState startup_state() {
    const auto path = agent_path();
    if (path.empty())
        return {false, "No home folder to keep a LaunchAgent in."};
    std::error_code error;
    return {std::filesystem::exists(path, error), {}};
}
std::string set_startup(bool enabled) {
    const auto path = agent_path();
    if (path.empty())
        return "No home folder to keep a LaunchAgent in.";
    std::error_code error;
    if (!enabled) {
        std::filesystem::remove(path, error);
        return error ? "Could not remove " + path.string() + ": " + error.message() : std::string{};
    }
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::trunc);
    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
            "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\">\n<dict>\n"
            "  <key>Label</key><string>com.bartab.app</string>\n"
            "  <key>ProgramArguments</key><array><string>"
         << escaped(executable_path().string())
         << "</string></array>\n"
            "  <key>RunAtLoad</key><true/>\n"
            "</dict>\n</plist>\n";
    return file.good() ? std::string{} : "Could not write " + path.string() + ".";
}
} // namespace usage::host
