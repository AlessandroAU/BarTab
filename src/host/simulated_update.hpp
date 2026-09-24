#pragma once
#include "host/updater.hpp"
#include <optional>

namespace usage::host {
// What the debug build's fake release server answers.
enum class SimulatedRelease {
    // One minor version ahead. Its executable is a copy of the running one,
    // signed with a key made for this run, so installing it swaps and
    // restarts for real.
    Newer,
    // The running version, so there is nothing to install.
    Latest,
    // Every request fails, as without a connection.
    Offline,
    // Newer, but its signature does not match its executable.
    Tampered,
};
// Updater options that talk to a fake release server in memory instead of
// GitHub, for trying an update end to end without publishing a release. Each
// request takes a moment so Checking and Downloading can be seen. Nothing when
// the running executable cannot be read.
std::optional<Updater::Options> simulated_update(const std::filesystem::path& executable, SimulatedRelease release);
} // namespace usage::host
