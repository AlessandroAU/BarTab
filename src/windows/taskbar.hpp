#pragma once
#include "core/usage.hpp"
#include <windows.h>
#include <chrono>
#include <memory>
#include <string>

namespace usage::windows {
inline constexpr wchar_t widget_class[] = L"BarTab.Widget.Cpp";
struct Snapshot {
    HWND taskbar{};
    Rect bounds{};
    unsigned dpi{96};
    std::vector<Rect> occupied;
    std::wstring error{L"Waiting for taskbar layout."};
    std::chrono::steady_clock::time_point captured{};
};
// The primary taskbar, and each other monitor's when asked for.
struct Taskbars {
    Snapshot primary;
    std::vector<Snapshot> secondary;
};
// The reader re-reads a layout that holds still less and less often, up to
// this gap, so a snapshot older than `taskbar_stale_after` means the worker is
// stuck rather than idle.
inline constexpr auto taskbar_slowest_read = std::chrono::seconds(8);
inline constexpr auto taskbar_stale_after = taskbar_slowest_read + std::chrono::seconds(5);

// The worker owns all COM objects. Only plain data crosses to the UI thread.
class TaskbarReader {
  public:
    TaskbarReader();
    ~TaskbarReader();
    TaskbarReader(const TaskbarReader&) = delete;
    TaskbarReader& operator=(const TaskbarReader&) = delete;
    Taskbars latest() const;
    // Whether to read the other monitors' taskbars too; each one costs a UI
    // Automation walk per read, so only while a widget is wanted there.
    void set_secondary(bool value);
    // Something that may move taskbar buttons happened, such as a window
    // opening: read shortly, and again once the taskbar has animated.
    void poke();
    // Stops reading while nobody can see the widget, and reads at once on return.
    void set_paused(bool value);

  private:
    struct State;
    std::shared_ptr<State> state_;
};
Rect window_rect(HWND window);
} // namespace usage::windows
