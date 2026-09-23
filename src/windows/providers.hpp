#pragma once
#include "windows/service_discovery.hpp"
#include "core/mock_provider.hpp"
#include <atomic>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace usage::windows {
// The debug build's stand-in for the installed CLIs. The control panel edits
// it on the UI thread while the readers read it from theirs.
class MockProviders {
  public:
    explicit MockProviders(MockScenario scenario) : scenario_(std::move(scenario)) {}
    MockScenario scenario() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return scenario_;
    }
    MockProvider provider(Service service) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return service == Service::Claude ? scenario_.claude : scenario_.codex;
    }
    void set(MockScenario value) {
        std::lock_guard<std::mutex> lock(mutex_);
        scenario_ = std::move(value);
    }

  private:
    mutable std::mutex mutex_;
    MockScenario scenario_;
};
// Where a mocked provider claims its executable lives.
inline std::string mock_path(Service service) {
    return service == Service::Claude ? "mock://claude" : "mock://codex";
}

class UsageReader {
  public:
    // With `mock`, readings come from the mock endpoint instead of the CLI.
    explicit UsageReader(Service service = Service::Codex, AccountUsage initial = {},
                         std::shared_ptr<const MockProviders> mock = nullptr);
    ~UsageReader();
    void refresh();
    void set_interval(int seconds);
    bool take(AccountUsage& result);

  private:
    void run();
    AccountUsage read_installed();
    AccountUsage read_mocked();
    Service service_;
    std::shared_ptr<const MockProviders> mock_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> stop_{false};
    bool requested_{true}, changed_{}, interval_changed_{};
    int interval_seconds_{60};
    AccountUsage latest_;
    std::thread worker_;
};
} // namespace usage::windows
