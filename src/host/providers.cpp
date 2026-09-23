#include "host/providers.hpp"
#include "host/process_transport.hpp"
#include <exception>

namespace usage::host {
namespace {
AccountUsage read_limits(const std::atomic<bool>& stop, const std::filesystem::path& exe, Service service) {
    ProviderProtocol protocol(service);
    ProcessTransport process(exe, protocol.arguments(), stop);
    process.send(protocol.initialize());
    for (;;) {
        auto reply = protocol.receive(process.read_line());
        for (const auto& message : reply.messages)
            process.send(message);
        if (reply.usage)
            return std::move(*reply.usage);
    }
}
} // namespace
void UsageReader::set_interval(int seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    seconds = preference_limits::provider_interval.clamp(seconds);
    if (interval_seconds_ == seconds)
        return;
    interval_seconds_ = seconds;
    interval_changed_ = true;
    wake_.notify_all();
}
UsageReader::UsageReader(Service service, AccountUsage initial, std::shared_ptr<const MockProviders> mock)
    : service_(service), mock_(std::move(mock)), latest_(std::move(initial)), worker_([this] { run(); }) {}
UsageReader::~UsageReader() {
    stop_ = true;
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}
void UsageReader::refresh() {
    std::lock_guard<std::mutex> lock(mutex_);
    requested_ = true;
    wake_.notify_all();
}
bool UsageReader::take(AccountUsage& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!changed_)
        return false;
    result = latest_;
    changed_ = false;
    return true;
}
AccountUsage UsageReader::read_installed() {
    const auto detection = find_service(service_);
    const auto& exe = detection.path;
    AccountUsage result;
    result.installed = !exe.empty();
    result.error = detection.error;
    if (result.installed) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            latest_.installed = true;
            const auto path = exe.u8string();
            latest_.executable_path.assign(path.begin(), path.end());
            changed_ = true;
        }
        result = read_limits(stop_, exe, service_);
    }
    const auto path = exe.u8string();
    result.executable_path.assign(path.begin(), path.end());
    return result;
}
AccountUsage UsageReader::read_mocked() {
    const auto provider = mock_->provider(service_);
    if (provider.state != MockState::Missing) {
        // As with a real CLI: found first, so a failed read still counts as installed.
        std::lock_guard<std::mutex> lock(mutex_);
        latest_.installed = true;
        latest_.executable_path = mock_path(service_);
    }
    auto result = read_mock(service_, provider, std::time(nullptr));
    if (result.installed)
        result.executable_path = mock_path(service_);
    return result;
}
void UsageReader::run() {
    while (!stop_) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requested_ = false;
        }
        try {
            auto result = mock_ ? read_mocked() : read_installed();
            std::lock_guard<std::mutex> lock(mutex_);
            latest_ = std::move(result);
            changed_ = true;
        } catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock(mutex_);
            // Keep the last valid reading, but never pass it off as fresh data.
            latest_.error = error.what();
            changed_ = true;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stop_ && !requested_) {
            interval_changed_ = false;
            if (!wake_.wait_for(lock, std::chrono::seconds(interval_seconds_),
                                [this] { return stop_ || requested_ || interval_changed_; }))
                break;
        }
    }
}
void ProviderSession::detect() {
    if (demo_)
        return;
    if (mock_) {
        for (const auto service : {Service::Codex, Service::Claude}) {
            auto& account = service == Service::Claude ? usage_.claude : usage_.codex;
            account.installed = mock_->provider(service).state != MockState::Missing;
            account.executable_path = account.installed ? mock_path(service) : std::string{};
            if (!account.installed)
                account.error.clear();
        }
        return;
    }
    detect_service(Service::Codex, usage_.codex);
    detect_service(Service::Claude, usage_.claude);
}
void ProviderSession::apply(const Preferences& preferences) {
    if (demo_)
        return;
    if (usage_.codex_enabled) {
        if (!codex_)
            codex_ = std::make_unique<UsageReader>(Service::Codex, usage_.codex, mock_);
        codex_->set_interval(preferences.codex_interval);
    } else
        codex_.reset();
    if (usage_.claude_enabled) {
        if (!claude_)
            claude_ = std::make_unique<UsageReader>(Service::Claude, usage_.claude, mock_);
        claude_->set_interval(preferences.claude_interval);
    } else
        claude_.reset();
}
void ProviderSession::refresh() {
    if (codex_)
        codex_->refresh();
    if (claude_)
        claude_->refresh();
}
ProviderSession::Update ProviderSession::poll(std::int64_t now) {
    const auto codex_before = usage_.codex;
    const auto claude_before = usage_.claude;
    const bool codex_changed = codex_ && codex_->take(usage_.codex);
    const bool claude_changed = claude_ && claude_->take(usage_.claude);
    Update result;
    result.changed = codex_changed || claude_changed;
    result.reset = (codex_changed && !reset_windows(codex_before, usage_.codex, now).empty()) ||
                   (claude_changed && !reset_windows(claude_before, usage_.claude, now).empty());
    return result;
}
} // namespace usage::host
