#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace usage::host {
struct DiscoveryLocations {
    std::filesystem::path override_path;
    std::vector<std::filesystem::path> bins, packages, extensions;
};
struct DiscoveryResult {
    std::filesystem::path path;
    std::string error;
};
// The provider CLI's file name on this platform: codex.exe / claude.exe on
// Windows, codex / claude elsewhere.
std::filesystem::path executable_name(bool claude);
// Searches the override, then each bin folder, then npm packages, then editor
// extensions. Bundles built for another OS or CPU are skipped, and outside
// Windows a match must be executable.
DiscoveryResult discover_executable(bool claude, const DiscoveryLocations& locations);
} // namespace usage::host
