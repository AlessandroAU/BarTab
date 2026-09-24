#pragma once
#include "host/service_discovery.hpp"
#include "core/mock_provider.hpp"
#include <atomic>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <ctime>

namespace usage::host {
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

// Away, while nobody can see the result (display off, session locked), the
// readers stop, and catch up as soon as someone can.
enum class PowerMode { Normal, Away };

class UsageReader {
  public:
    // With `mock`, readings come from the mock endpoint instead of the CLI.
    explicit UsageReader(Service service = Service::Codex, AccountUsage initial = {},
                         std::shared_ptr<const MockProviders> mock = nullptr);
    ~UsageReader();
    void refresh();
    void set_interval(int seconds);
    void set_power(PowerMode mode);
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
    bool requested_{true}, changed_{}, schedule_changed_{};
    int interval_seconds_{60};
    PowerMode power_{PowerMode::Normal};
    AccountUsage latest_;
    std::thread worker_;
};

// The provider side of a desktop host: which CLIs are installed, one polling
// reader per enabled provider, and the readings they publish into `usage`.
class ProviderSession {
  public:
    // `demo` keeps the fixed demo data: nothing is detected or polled. With
    // `mock`, the debug build's mock endpoints stand in for the CLIs.
    ProviderSession(Usage& usage, std::shared_ptr<MockProviders> mock, bool demo)
        : usage_(usage), mock_(std::move(mock)), demo_(demo) {}
    // Finds each CLI again, updating whether it is installed and where.
    void detect();
    // Starts or stops each provider's reader to match Usage's enabled flags,
    // and applies the preferences' polling intervals.
    void apply(const Preferences& preferences);
    // Asks every running reader for a fresh reading now.
    void refresh();
    // Applies to the running readers and to any started later.
    void set_power(PowerMode mode);
    struct Update {
        bool changed{};
        // An allowance window reset between two readings: worth a celebration.
        bool reset{};
    };
    // Takes whatever the readers published since the last call.
    Update poll(std::int64_t now = std::time(nullptr));
    const std::shared_ptr<MockProviders>& mock() const {
        return mock_;
    }

  private:
    Usage& usage_;
    std::shared_ptr<MockProviders> mock_;
    bool demo_;
    PowerMode power_{PowerMode::Normal};
    std::unique_ptr<UsageReader> codex_, claude_;
};
} // namespace usage::host
