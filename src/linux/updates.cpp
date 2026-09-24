#include "linux/app.hpp"
#include "host/platform.hpp"
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace usage::linux_host {
namespace {
// A desktop notification through notify-send, where it is installed. Best
// effort: without it the menu and settings still offer the update.
void notify(const std::string& title, const std::string& body) {
    std::string program = "notify-send", name = "--app-name=BarTab", heading = title, text = body;
    char* argv[] = {program.data(), name.data(), heading.data(), text.data(), nullptr};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    sigset_t defaults, none;
    sigfillset(&defaults);
    sigemptyset(&none);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    posix_spawnattr_setsigmask(&attributes, &none);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    pid_t process{};
    const int status = posix_spawnp(&process, program.c_str(), &actions, &attributes, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (status != 0) {
        host::log("Could not show a notification: notify-send is not installed.");
        return;
    }
    // Reaped off the UI thread; it exits as soon as the notification is sent.
    std::thread([process] {
        while (waitpid(process, nullptr, 0) < 0 && errno == EINTR) {
        }
    }).detach();
}
} // namespace

void App::start_updater() {
    // The debug build is not a release asset, and smoke tests stay offline.
    if (smoke_ || mock_)
        return;
    executable_ = host::executable_path();
    host::Updater::Options options;
    options.executable = executable_;
    updater_ = std::make_unique<host::Updater>(std::move(options));
    updater_->set_policy(preferences_.check_updates, preferences_.install_updates);
}

void App::poll_updates() {
    if (!updater_)
        return;
    using update::State;
    if (updater_->take(update_status_)) {
        details_view_.set_update_status(update_status_);
        if (popup_ && x_.visible(popup_) && settings_mode_)
            render_details(details_pointer_);
        // Once per version, and not when it installs itself anyway.
        if (update_status_.state == State::Available && update_status_.latest != notified_version_ &&
            !preferences_.install_updates) {
            notified_version_ = update_status_.latest;
            notify("BarTab update available", "Version " + update_status_.latest +
                                                  " is available. Right-click the widget to install it.");
        }
    }
    // An automatic install waits until nothing is open, so it never restarts
    // the app under the user's pointer.
    const bool idle = !(popup_ && x_.visible(popup_)) && !(menu_ && x_.visible(menu_));
    if (update_status_.state == State::Ready && (install_requested_ || (preferences_.install_updates && idle)))
        apply_update();
}

void App::install_update() {
    if (!updater_)
        return;
    install_requested_ = true;
    if (update_status_.state == update::State::Ready)
        apply_update();
    else
        updater_->download_update();
}

void App::apply_update() {
    install_requested_ = false;
    const auto staged = updater_->staged();
    if (staged.empty())
        return;
    auto error = host::install_update(staged, executable_);
    if (!error.empty()) {
        host::log("Could not install the update: " + error);
        notify("BarTab could not update", error);
        return;
    }
    error = host::relaunch(executable_);
    if (!error.empty()) {
        // The new version is in place; it runs from the next start.
        host::log("Installed version " + update_status_.latest + " but could not restart: " + error);
        notify("BarTab was updated", "Start it again to use version " + update_status_.latest + ".");
        return;
    }
    host::log("Installed version " + update_status_.latest + "; restarting.");
    running_ = false;
}
} // namespace usage::linux_host
