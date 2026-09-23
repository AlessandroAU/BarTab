#include "windows/frame_clock.hpp"
#include <dwmapi.h>

namespace usage::windows {
FrameClock::FrameClock(HWND window, UINT message) : window_(window), message_(message) {
    thread_ = std::thread([this] { run(); });
}

FrameClock::~FrameClock() {
    {
        std::lock_guard lock(mutex_);
        quit_ = true;
    }
    wake_.notify_one();
    thread_.join();
}

void FrameClock::start() {
    {
        std::lock_guard lock(mutex_);
        if (running_)
            return;
        running_ = true;
    }
    wake_.notify_one();
}

void FrameClock::stop() {
    std::lock_guard lock(mutex_);
    running_ = false;
}

void FrameClock::run() {
    std::unique_lock lock(mutex_);
    while (true) {
        wake_.wait(lock, [this] { return running_ || quit_; });
        if (quit_)
            return;
        lock.unlock();
        // Returns after the next composition pass: once per refresh.
        if (FAILED(DwmFlush()))
            Sleep(16);
        if (!pending_.exchange(true) && !PostMessageW(window_, message_, 0, 0))
            pending_ = false;
        lock.lock();
    }
}
} // namespace usage::windows
