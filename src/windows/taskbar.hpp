#pragma once
#include "core/usage.hpp"
#include <windows.h>
#include <chrono>
#include <memory>
#include <string>

namespace usage::windows {
inline constexpr wchar_t widget_class[] = L"UsageTracker.Widget.Cpp";
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

// The worker owns all COM objects. Only plain data crosses to the UI thread.
class TaskbarReader {
  public:
    TaskbarReader();
    ~TaskbarReader();
    TaskbarReader(const TaskbarReader&) = delete;
    TaskbarReader& operator=(const TaskbarReader&) = delete;
    Taskbars latest() const;
    // Whether to read the other monitors' taskbars too; each one costs a UI
    // Automation walk per second, so only while a widget is wanted there.
    void set_secondary(bool value);

  private:
    struct State;
    std::shared_ptr<State> state_;
};
Rect window_rect(HWND window);
} // namespace usage::windows
