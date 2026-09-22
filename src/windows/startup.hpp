#pragma once
#include <windows.h>

namespace usage::windows {
struct StartupState { bool enabled{}; LSTATUS error{ERROR_SUCCESS}; };
StartupState startup_state();
LSTATUS set_startup(bool enabled);
}
