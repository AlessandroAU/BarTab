#pragma once
#include "core/preferences.hpp"
#include <filesystem>

namespace usage::windows {
// Missing files/keys use defaults. Values are normalized after loading.
Preferences read_settings(const std::filesystem::path& path);
// Replace the destination only after the complete temporary file is written.
bool write_settings(const std::filesystem::path& path, Preferences value);
} // namespace usage::windows
