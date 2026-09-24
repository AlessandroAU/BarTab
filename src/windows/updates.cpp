#include "windows/app.hpp"
#include "windows/platform.hpp"

namespace usage::windows {

void App::start_updater() {
    // The debug build is not a release asset, and smoke tests stay offline.
    if (smoke_ || mock_)
        return;
    host::Updater::Options options;
    options.executable = host::executable_path();
    updater_ = std::make_unique<host::Updater>(std::move(options));
    updater_->set_policy(preferences_.check_updates, preferences_.install_updates);
}

void App::simulate_update(host::SimulatedRelease release) {
    auto options = host::simulated_update(host::executable_path(), release);
    if (!options)
        return;
    // Stops the previous updater, cancelling whatever it was fetching.
    updater_.reset();
    install_requested_ = false;
    // Every simulated release is announced, even one already seen.
    notified_version_.clear();
    updater_ = std::make_unique<host::Updater>(std::move(*options));
    updater_->set_policy(preferences_.check_updates, preferences_.install_updates);
    updater_->check_now();
}

void App::poll_updates() {
    if (!updater_)
        return;
    using update::State;
    if (updater_->take(update_status_)) {
        details_view_.set_update_status(update_status_);
        if (popup_ && IsWindowVisible(popup_) && settings_mode_)
            render_details(details_pointer_);
        // Once per version, and not when it installs itself anyway.
        if (update_status_.state == State::Available && update_status_.latest != notified_version_ &&
            !preferences_.install_updates) {
            notified_version_ = update_status_.latest;
            notify_update();
        }
    }
    // An automatic install waits until nothing is open, so it never restarts
    // the app under the user's pointer.
    const bool idle = !(popup_ && IsWindowVisible(popup_)) && !menu_open_;
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
    const auto executable = host::executable_path();
    const auto staged = updater_->staged();
    if (staged.empty())
        return;
    auto error = host::install_update(staged, executable);
    if (!error.empty()) {
        log(L"Could not install the update: " + widen(error));
        const auto message = L"Could not install the update.\n\n" + widen(error);
        MessageBoxW(controller_, message.c_str(), L"BarTab", MB_OK | MB_ICONERROR);
        return;
    }
    error = host::relaunch(executable);
    if (!error.empty()) {
        // The new version is in place; it runs from the next start.
        log(L"Installed version " + widen(update_status_.latest) + L" but could not restart: " + widen(error));
        const auto message = L"BarTab was updated but could not restart itself. Start it again to use version " +
                             widen(update_status_.latest) + L".\n\n" + widen(error);
        MessageBoxW(controller_, message.c_str(), L"BarTab", MB_OK | MB_ICONWARNING);
        return;
    }
    log(L"Installed version " + widen(update_status_.latest) + L"; restarting.");
    PostQuitMessage(0);
}

// A notification from the tray icon; clicking it opens the Updates settings.
void App::notify_update() {
    auto balloon = tray_;
    balloon.uFlags = NIF_INFO;
    balloon.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s(balloon.szInfoTitle, L"BarTab update available", _TRUNCATE);
    const auto text = L"Version " + widen(update_status_.latest) + L" is available. Click to install it from settings.";
    wcsncpy_s(balloon.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &balloon);
}

void App::open_update_settings() {
    details_view_.set_settings_page(ui::SettingsPage::General);
    open_details(true);
}

} // namespace usage::windows
