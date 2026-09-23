#pragma once
#include "host/platform.hpp"
#include <string>

namespace usage::windows {
using host::animations_enabled;
using host::apps_light_theme;
using host::executable_directory;
using host::system_accent;
using host::system_light_theme;
using host::ui_font;
// The Win32 host builds its messages as UTF-16; this converts and appends them
// to the shared log.
void log(const std::wstring& message);
std::string utf8(const std::wstring& value);
std::wstring widen(const std::string& utf8);
} // namespace usage::windows
