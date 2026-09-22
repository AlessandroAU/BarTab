#pragma once
#include "core/usage.hpp"
#include <filesystem>
#include <string>

namespace usage::windows {
std::filesystem::path executable_directory();
void log(const std::wstring& message);
bool system_light_theme();
bool apps_light_theme();
Color windows_accent();
std::vector<unsigned char> windows_ui_font();
} // namespace usage::windows
