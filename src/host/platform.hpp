#pragma once
#include "core/usage.hpp"
#include <filesystem>
#include <string_view>
#include <vector>

// What a desktop host asks of its operating system. Each platform implements
// these once: src/windows/platform.cpp, src/linux/platform.cpp and
// src/macos/platform.cpp. log() is portable and lives in src/host/log.cpp.
namespace usage::host {
std::filesystem::path executable_path();
std::filesystem::path executable_directory();
// The per-user folder for settings; empty when it cannot be resolved.
std::filesystem::path config_directory();
// The capped diagnostic log; empty when there is nowhere to write it.
std::filesystem::path log_path();
// Appends a timestamped UTF-8 line to log_path(), keeping it under 1 MB.
void log(std::string_view message);
// Light or dark for the shell surface the widget sits on (taskbar, panel).
bool system_light_theme();
// Light or dark for application windows: the hover card and settings.
bool apps_light_theme();
// False when the user asked the system for reduced motion.
bool animations_enabled();
Color system_accent();
// Marks the calling thread as background work the OS may run slowly and on
// efficient cores: Windows' EcoQoS, macOS's utility class. Linux leaves it be.
void background_thread();
// A font file and the face in it to draw with: the face within a collection in
// the low 16 bits, a variable font's named instance above them (fontconfig's
// FC_INDEX encoding, which the renderer takes as is).
struct FontFile {
    std::vector<unsigned char> bytes;
    long face_index{};
};
// The system UI font. `bold` resolves the same family's bold face, or its bold
// instance for a variable font, rather than emboldening the regular one; a
// family without one returns its regular face. Empty when the font cannot be read.
FontFile ui_font(bool bold = false);
} // namespace usage::host
