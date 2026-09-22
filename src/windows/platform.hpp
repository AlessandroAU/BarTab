#pragma once
#include "core/usage.hpp"
#include <filesystem>
#include <string>

namespace usage::windows {
std::filesystem::path executable_directory();
void log(const std::wstring& message);
bool system_light_theme();
bool apps_light_theme();
// The "Animation effects" accessibility switch; false asks for reduced motion.
bool client_animations_enabled();
Color windows_accent();
std::vector<unsigned char> windows_ui_font();
} // namespace usage::windows
