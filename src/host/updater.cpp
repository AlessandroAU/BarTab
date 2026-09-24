#include "host/updater.hpp"
#include "host/platform.hpp"
#include <fstream>
#include <iterator>

#ifndef BARTAB_VERSION
#error "BARTAB_VERSION must be defined by the build"
#endif

namespace usage::host {
namespace {
// Nothing BarTab publishes comes near this; a larger download is not ours.
constexpr std::uintmax_t largest_download = 64 * 1024 * 1024;

// The whole file, or nothing when it is missing or larger than `limit`.
std::optional<std::string> read_file(const std::filesystem::path& path, std::uintmax_t limit) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > limit)
        return std::nullopt;
    std::ifstream file(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!file.good() && !file.eof())
        return std::nullopt;
    return bytes;
}
void remove_file(const std::filesystem::path& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}
} // namespace

update::Version current_version() {
    static const auto version = update::parse_version(BARTAB_VERSION).value_or(update::Version{});
    return version;
}

std::filesystem::path staged_update(const std::filesystem::path& executable) {
    auto staged = executable;
    staged += ".new";
    return staged;
}

Updater::Updater(Options options) : options_(std::move(options)) {
    status_.state = update::State::Off;
    status_.current = options_.current.text();
    worker_ = std::thread([this] { run(); });
}

Updater::~Updater() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

void Updater::set_policy(bool check, bool automatic) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (check_enabled_ == check && automatic_ == automatic)
        return;
    check_enabled_ = check;
    automatic_ = automatic;
    // Switching checks off forgets a stale answer, but keeps an update that is
    // already known, so it can still be installed.
    using update::State;
    if (!check && (status_.state == State::Idle || status_.state == State::UpToDate ||
                   status_.state == State::Failed))
        publish(State::Off);
    else if (check && status_.state == State::Off)
        publish(State::Idle);
    policy_changed_ = true;
    wake_.notify_all();
}

void Updater::check_now() {
    std::lock_guard<std::mutex> lock(mutex_);
    check_requested_ = true;
    wake_.notify_all();
}

void Updater::download_update() {
    std::lock_guard<std::mutex> lock(mutex_);
    download_requested_ = true;
    wake_.notify_all();
}

bool Updater::take(update::Status& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!changed_)
        return false;
    result = status_;
    changed_ = false;
    return true;
}

std::filesystem::path Updater::staged() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.state == update::State::Ready ? staged_ : std::filesystem::path{};
}

void Updater::publish(update::State state, std::string error) {
    status_.state = state;
    status_.error = std::move(error);
    const bool newer = state == update::State::Available || state == update::State::Downloading ||
                       state == update::State::Ready;
    status_.latest = newer && release_ ? release_->version.text() : std::string{};
    status_.page_url = newer && release_ ? release_->page_url : std::string{};
    changed_ = true;
}

void Updater::run() {
    if (!options_.executable.empty())
        remove_update_leftovers(options_.executable);
    auto next_check = Clock::now() + options_.first_check;
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
        using update::State;
        // A verified download waits for the host to install it; checking again
        // would only replace it with the same answer.
        if (status_.state == State::Ready)
            check_requested_ = false;
        else if (check_requested_ || (check_enabled_ && Clock::now() >= next_check)) {
            check_requested_ = false;
            lock.unlock();
            const bool answered = check();
            lock.lock();
            next_check = Clock::now() + (answered ? options_.interval : options_.retry);
            continue;
        }
        // Automatic downloads try once per release; installing by hand retries.
        if (status_.state == State::Available && (download_requested_ || (automatic_ && !download_failed_))) {
            download_requested_ = false;
            lock.unlock();
            fetch_update();
            lock.lock();
            continue;
        }
        download_requested_ = false;
        policy_changed_ = false;
        const auto woken = [this] { return stop_ || check_requested_ || download_requested_ || policy_changed_; };
        if (check_enabled_)
            wake_.wait_until(lock, next_check, woken);
        else
            wake_.wait(lock, woken);
    }
}

bool Updater::check() {
    using update::State;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        publish(State::Checking);
    }
    std::error_code ignored;
    const auto feed = std::filesystem::temp_directory_path(ignored) / "BarTab-release.json";
    auto error = options_.fetch(options_.feed, feed, stop_);
    const auto json = error.empty() ? read_file(feed, 4 * 1024 * 1024) : std::nullopt;
    remove_file(feed);
    if (error.empty() && !json)
        error = "the release information could not be read";
    std::lock_guard<std::mutex> lock(mutex_);
    // GitHub answers 404 until the first release is published.
    if (error == "the server answered HTTP 404") {
        release_.reset();
        publish(State::UpToDate);
        return true;
    }
    if (!error.empty()) {
        log("Update check failed: " + error);
        publish(release_ ? State::Available : State::Failed, "Couldn't check for updates: " + error);
        return false;
    }
    const auto release = update::parse_release(*json, options_.asset);
    if (!release || !(options_.current < release->version)) {
        // A release without a signed download for this system cannot be
        // installed; only a newer one that has one counts.
        release_.reset();
        publish(State::UpToDate);
        return true;
    }
    if (!release_ || !(release_->version == release->version)) {
        download_failed_ = false;
        log("Update available: " + release->version.text() + " (running " + options_.current.text() + ")");
    }
    release_ = release;
    publish(State::Available);
    return true;
}

void Updater::fetch_update() {
    using update::State;
    update::Release release;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!release_)
            return;
        release = *release_;
        publish(State::Downloading);
    }
    const auto staged = staged_update(options_.executable);
    auto signature_path = staged;
    signature_path += ".sig";
    auto error = options_.fetch(release.binary_url, staged, stop_);
    if (error.empty())
        error = options_.fetch(release.signature_url, signature_path, stop_);
    std::optional<std::string> binary, signature;
    if (error.empty()) {
        binary = read_file(staged, largest_download);
        signature = read_file(signature_path, 1024);
        if (!binary || !signature)
            error = "the download could not be read";
    }
    remove_file(signature_path);
    bool verified = false;
    if (error.empty()) {
        verified = update::verify(update::signed_message(options_.asset, release.version, *binary), *signature,
                                  options_.key);
        if (!verified)
            error = "its signature did not match, so it was not installed";
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!verified) {
        remove_file(staged);
        download_failed_ = true;
        log("Update download failed: " + error);
        // Still available: installing again downloads afresh.
        publish(State::Available, "Couldn't download the update: " + error);
        return;
    }
    log("Update " + release.version.text() + " downloaded and verified.");
    staged_ = staged;
    publish(State::Ready);
}
} // namespace usage::host
