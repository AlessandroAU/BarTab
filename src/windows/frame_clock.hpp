#pragma once
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace usage::windows {
// Posts `message` to `window` once per display refresh while running, paced by
// DWM composition, so animations step in time with the monitor rather than
// with SetTimer, which rounds 16 ms up to two 15.6 ms ticks (about 32 fps).
// A frame is posted only once the previous one was handled, so a slow render
// drops frames instead of queueing them.
class FrameClock {
  public:
    FrameClock(HWND window, UINT message);
    ~FrameClock();
    FrameClock(const FrameClock&) = delete;
    FrameClock& operator=(const FrameClock&) = delete;
    void start();
    // Frames already posted still arrive; handlers ignore what has settled.
    void stop();
    // Call at the end of handling each frame message.
    void frame_done() {
        pending_ = false;
    }

  private:
    HWND window_;
    UINT message_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool running_{}, quit_{};
    std::atomic<bool> pending_{};
    std::thread thread_;
    void run();
};
} // namespace usage::windows
