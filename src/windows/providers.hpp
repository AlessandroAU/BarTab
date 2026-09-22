#pragma once
#include "windows/service_discovery.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace usage::windows {
class UsageReader {
  public:
    explicit UsageReader(Service service = Service::Codex, AccountUsage initial = {});
    ~UsageReader();
    void refresh();
    void set_interval(int seconds);
    bool take(AccountUsage& result);

  private:
    void run();
    Service service_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> stop_{false};
    bool requested_{true}, changed_{}, interval_changed_{};
    int interval_seconds_{60};
    AccountUsage latest_;
    std::thread worker_;
};
} // namespace usage::windows
