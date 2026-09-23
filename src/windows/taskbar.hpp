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

// The worker owns all COM objects. Only plain data crosses to the UI thread.
class TaskbarReader {
  public:
    TaskbarReader();
    ~TaskbarReader();
    TaskbarReader(const TaskbarReader&) = delete;
    TaskbarReader& operator=(const TaskbarReader&) = delete;
    Snapshot latest() const;

  private:
    struct State;
    std::shared_ptr<State> state_;
};
Rect window_rect(HWND window);
} // namespace usage::windows
