#include "windows/session_state.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void test_session_state() {
    using usage::windows::SessionState;
    // Also covers launching into a queried session before any WTS event.
    for (bool remote : {false, true})
        for (bool connected : {false, true})
            for (bool locked : {false, true})
                for (bool display : {false, true}) {
                    SessionState state{display, connected, locked, remote};
                    const bool visible = connected && !locked && (remote || display);
                    check(state.away() != visible, "Polling must follow session visibility");
                    check((state.pause_reason() == nullptr) == visible, "Every pause must have a reason");
                }

    SessionState state;
    state.display_on = false;
    check(state.away(), "Console display off pauses polling");
    check(state.change(WTS_CONSOLE_DISCONNECT) && state.away(), "Console disconnect pauses");
    check(state.change(WTS_REMOTE_CONNECT) && !state.away(), "RDP connect resumes from a dark console");
    state.display_on = false;
    check(!state.away(), "A late display-off notification must not suspend connected RDP");
    state.change(WTS_SESSION_LOCK);
    check(state.away() && std::wstring(state.pause_reason()) == L"session locked", "RDP lock still pauses");
    state.change(WTS_REMOTE_DISCONNECT);
    check(std::wstring(state.pause_reason()) == L"session disconnected", "Disconnect reason is recorded even if already paused");
    state.change(WTS_REMOTE_CONNECT);
    check(state.away(), "Connecting does not unlock a locked session");
    state.change(WTS_SESSION_UNLOCK);
    check(!state.away(), "Unlock resumes the connected session");
    state.change(WTS_REMOTE_DISCONNECT);
    state.change(WTS_CONSOLE_CONNECT);
    check(!state.remote && !state.away(), "Return to console clears obsolete display-off state");
    state.display_on = false;
    check(state.away(), "Console display power saving works after RDP");
    state.change(WTS_SESSION_UNLOCK);
    check(!state.away(), "Console unlock clears an old display-off report");
    check(!state.change(0xffffffff), "Unknown session events are ignored");
}
} // namespace

int main() {
    try {
        test_session_state();
        std::cout << "Session power and reconnect checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
