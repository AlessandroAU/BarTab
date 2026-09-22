#pragma once
#include <filesystem>
#include <string>

namespace usage::windows {
std::filesystem::path executable_directory();
void log(const std::wstring& message);
bool system_light_theme();
} // namespace usage::windows
