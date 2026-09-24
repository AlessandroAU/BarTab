#pragma once
#include "core/update.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace usage::host {
// The running build's version, from CMake's project version.
update::Version current_version();

// Downloads `url`, which must be HTTPS, into `destination`, replacing it only
// once the whole body has arrived. Empty on success, otherwise why it failed:
// a status other than 200 is "the server answered HTTP <status>".
// Implemented by src/windows/http.cpp (WinHTTP) and src/posix/http.cpp (the
// curl command, which every desktop distribution ships).
std::string download(const std::string& url, const std::filesystem::path& destination,
                     const std::atomic<bool>& stop);

// Replaces `executable` with the verified download `staged`. Windows cannot
// overwrite a running executable but can rename it, so it moves aside first;
// elsewhere the new file is renamed over it. Empty on success, otherwise a
// message for the user. Implemented per platform in src/windows/self_update.cpp
// and src/posix/self_update.cpp.
std::string install_update(const std::filesystem::path& staged, const std::filesystem::path& executable);
// Starts `executable` with --updated, which waits for this instance to exit
// before claiming the single-instance lock. Empty on success.
std::string relaunch(const std::filesystem::path& executable);
// Deletes what an update leaves beside `executable`: the moved-aside old
// executable, and any download that was never installed.
void remove_update_leftovers(const std::filesystem::path& executable);
// Where a download is staged: beside the executable, so installing it is a
// rename on one volume.
std::filesystem::path staged_update(const std::filesystem::path& executable);

// Checks GitHub for a newer release once a day on its own thread, and on
// request downloads and verifies it. The host polls take() for the status and,
// once it is Ready, installs staged() and restarts.
class Updater {
  public:
    using Fetch = std::function<std::string(const std::string& url, const std::filesystem::path& destination,
                                            const std::atomic<bool>& stop)>;
    struct Options {
        update::Version current{current_version()};
        std::filesystem::path executable;
        std::string feed{"https://api.github.com/repos/AlessandroAU/BarTab/releases/latest"};
        std::string asset{update::platform_asset()};
        update::PublicKey key{update::release_key};
        Fetch fetch{download};
        // The first check waits for startup to settle.
        std::chrono::milliseconds first_check{std::chrono::seconds(20)};
        std::chrono::milliseconds interval{std::chrono::hours(24)};
        // After a failed check, sooner than the next day.
        std::chrono::milliseconds retry{std::chrono::hours(2)};
    };
    explicit Updater(Options options);
    ~Updater();
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;
    // `check` is the Check for updates preference; `automatic` downloads a
    // newer release as soon as one is found.
    void set_policy(bool check, bool automatic);
    void check_now();
    // Downloads and verifies the available release; the status becomes Ready.
    void download_update();
    // The status, when it changed since the last call.
    bool take(update::Status& result);
    // The verified executable, once the status is Ready.
    std::filesystem::path staged() const;

  private:
    using Clock = std::chrono::steady_clock;
    void run();
    // False when the feed could not be read, so the next try comes sooner.
    bool check();
    void fetch_update();
    // Callers hold mutex_.
    void publish(update::State state, std::string error = {});
    Options options_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> stop_{false};
    bool check_enabled_{}, automatic_{}, check_requested_{}, download_requested_{}, policy_changed_{};
    bool download_failed_{};
    bool changed_{true};
    update::Status status_;
    std::optional<update::Release> release_;
    std::filesystem::path staged_;
    std::thread worker_;
};
} // namespace usage::host
