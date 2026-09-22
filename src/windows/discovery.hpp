#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace usage::windows {
struct DiscoveryLocations {
    std::filesystem::path override_path;
    std::vector<std::filesystem::path> bins, packages, extensions;
};
struct DiscoveryResult { std::filesystem::path path; std::string error; };
DiscoveryResult discover_executable(bool claude, const DiscoveryLocations& locations);
}
