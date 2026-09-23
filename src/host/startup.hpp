#pragma once
#include <string>

namespace usage::host {
// Whether this executable starts when the user logs in. Implemented by
// src/windows/startup.cpp (the Run registry key), src/linux/startup.cpp (an XDG
// autostart entry) and src/macos/startup.cpp (a LaunchAgent).
struct StartupState {
    bool enabled{};
    // Why the state could not be read; the toggle is unavailable while set.
    std::string error;
};
StartupState startup_state();
// Registers or removes the current executable. Empty on success, otherwise a
// message for the user.
std::string set_startup(bool enabled);
} // namespace usage::host
