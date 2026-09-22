#include "windows/providers.hpp"
#include "windows/process_transport.hpp"
#include <exception>

namespace usage::windows {
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
UsageReader::UsageReader(Service service, AccountUsage initial)
    : service_(service), latest_(std::move(initial)), worker_([this] { run(); }) {}
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
void UsageReader::run() {
    while (!stop_) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requested_ = false;
        }
        try {
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
} // namespace usage::windows
