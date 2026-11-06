#pragma once
#include <windows.h>
#include <wtsapi32.h>

namespace usage::windows {
// Display power is meaningful for the console. For RDP, connection and lock
// state determine whether the user can see the app, independently of monitors.
struct SessionState {
    bool display_on{true}, connected{true}, locked{}, remote{};

    const wchar_t* pause_reason() const {
        if (!connected) return L"session disconnected";
        if (locked) return L"session locked";
        if (!remote && !display_on) return L"session display off";
        return nullptr;
    }
    bool away() const { return pause_reason() != nullptr; }

    // Apply the event after querying current state: the event is authoritative
    // if a query failed or returned a state from just before this transition.
    // A connect alone does not imply unlock; the queried lock state is retained.
    bool change(UINT event) {
        switch (event) {
        case WTS_REMOTE_CONNECT:
        case WTS_CONSOLE_CONNECT:
            connected = true;
            remote = event == WTS_REMOTE_CONNECT;
            display_on = true;
            break;
        case WTS_REMOTE_DISCONNECT:
        case WTS_CONSOLE_DISCONNECT:
            connected = false;
            break;
        case WTS_SESSION_LOCK:
            locked = true;
            break;
        case WTS_SESSION_UNLOCK:
            locked = false;
            connected = true;
            display_on = true;
            break;
        case WTS_SESSION_REMOTE_CONTROL:
            break; // The current state was queried by the caller.
        default:
            return false;
        }
        return true;
    }
};
} // namespace usage::windows
