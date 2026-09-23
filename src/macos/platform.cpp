// A portable stand-in until the macOS menu-bar host exists: paths follow Apple's
// conventions and the UI font is read straight from the system font files.
// Appearance queries need AppKit and report the defaults for now.
#include "host/platform.hpp"
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mach-o/dyld.h>

namespace usage::host {
namespace {
std::filesystem::path home() {
    const char* value = std::getenv("HOME");
    return value ? value : "";
}
} // namespace
std::filesystem::path executable_path() {
    char buffer[4096];
    std::uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0)
        return {};
    std::error_code error;
    const auto resolved = std::filesystem::canonical(buffer, error);
    return error ? std::filesystem::path(buffer) : resolved;
}
std::filesystem::path executable_directory() {
    return executable_path().parent_path();
}
std::filesystem::path config_directory() {
    const auto base = home();
    return base.empty() ? base : base / "Library/Application Support/UsageTracker";
}
std::filesystem::path log_path() {
    const auto base = home();
    return base.empty() ? base : base / "Library/Logs/UsageTracker/usagetracker.log";
}
bool system_light_theme() {
    return false;
}
bool apps_light_theme() {
    return false;
}
bool animations_enabled() {
    return true;
}
Color system_accent() {
    return {0, 122, 255};
}
FontFile ui_font(bool bold) {
    // Helvetica Neue's collection starts with its regular face; the renderer
    // reads a file's first face, so bold falls back to regular for now.
    (void)bold;
    for (const char* path : {"/System/Library/Fonts/HelveticaNeue.ttc", "/System/Library/Fonts/Helvetica.ttc",
                             "/Library/Fonts/Arial.ttf"}) {
        std::ifstream file(path, std::ios::binary);
        if (file)
            return {{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()}, 0};
    }
    return {};
}
} // namespace usage::host
