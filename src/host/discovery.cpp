#include "host/discovery.hpp"
#include <algorithm>
#include <cstring>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace usage::host {
namespace {
using native = std::filesystem::path::string_type;
// Folder names that mark a bundle for another OS or CPU. Packages and editor
// extensions often carry every platform's binary side by side.
constexpr const char* foreign_markers[] = {
#if defined(__aarch64__) || defined(_M_ARM64)
    "x64",   "x86_64", "amd64",
#else
    "arm64",  "aarch64",
#endif
#if defined(__APPLE__)
    "linux", "win32",  "windows",
#elif !defined(_WIN32)
    "darwin", "apple",   "macos", "win32", "windows",
#endif
};
// ASCII-only lowering is enough to compare against the ASCII markers, and never
// fails on a name in an unexpected encoding.
native lower(native value) {
    for (auto& c : value)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<native::value_type>(c - 'A' + 'a');
    return value;
}
bool contains(const native& haystack, const char* needle) {
    return haystack.find(native(needle, needle + std::strlen(needle))) != native::npos;
}
bool foreign(const native& lowered) {
    return std::any_of(std::begin(foreign_markers), std::end(foreign_markers),
                       [&](const char* marker) { return contains(lowered, marker); });
}
bool launchable(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        return false;
#ifdef _WIN32
    return true;
#else
    return access(path.c_str(), X_OK) == 0;
#endif
}
// Search only recognized CLI packages/extensions, without following links.
std::filesystem::path bundled(const std::filesystem::path& root, const native& name) {
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, error),
        end;
    std::filesystem::path newest;
    std::filesystem::file_time_type modified = std::filesystem::file_time_type::min();
    unsigned visited = 0;
    while (!error && it != end && ++visited <= 20000) {
        const auto component = lower(it->path().filename().native());
        if (it.depth() >= 7 || foreign(component) || it->is_symlink(error))
            it.disable_recursion_pending();
        error.clear();
        if (component == name && launchable(it->path())) {
            const auto time = std::filesystem::last_write_time(it->path(), error);
            if (!error && (newest.empty() || time > modified)) {
                newest = it->path();
                modified = time;
            }
        }
        error.clear();
        it.increment(error);
    }
    return newest;
}
} // namespace
std::filesystem::path executable_name(bool claude) {
#ifdef _WIN32
    return claude ? "claude.exe" : "codex.exe";
#else
    return claude ? "claude" : "codex";
#endif
}
DiscoveryResult discover_executable(bool claude, const DiscoveryLocations& locations) {
    const auto name = executable_name(claude);
    const auto lowered_name = lower(name.native());
    const std::string provider = claude ? "Claude" : "Codex";
    const std::string variable = std::string("BARTAB_") + (claude ? "CLAUDE" : "CODEX");
    if (!locations.override_path.empty()) {
#ifdef _WIN32
        if (lower(locations.override_path.extension().native()) == L".exe" &&
            launchable(locations.override_path))
            return {locations.override_path, {}};
        return {{}, provider + " override is not an existing .exe. Correct or remove " + variable + "."};
#else
        if (launchable(locations.override_path))
            return {locations.override_path, {}};
        return {{}, provider + " override is not an executable file. Correct or remove " + variable + "."};
#endif
    }
    for (const auto& bin : locations.bins)
        if (launchable(bin / name))
            return {bin / name, {}};
    for (const auto& package : locations.packages) {
        const auto found = bundled(package, lowered_name);
        if (!found.empty())
            return {found, {}};
    }
    const char* prefix = claude ? "anthropic.claude-code-" : "openai.chatgpt-";
    const native wanted(prefix, prefix + std::strlen(prefix));
    std::filesystem::path newest;
    auto modified = std::filesystem::file_time_type::min();
    for (const auto& root : locations.extensions) {
        std::error_code error;
        std::filesystem::directory_iterator it(
            root, std::filesystem::directory_options::skip_permission_denied, error),
            end;
        while (!error && it != end) {
            const auto folder = lower(it->path().filename().native());
            if (folder.find(wanted) == 0 && !foreign(folder)) {
                const auto found = bundled(it->path(), lowered_name);
                if (!found.empty()) {
                    const auto time = std::filesystem::last_write_time(found, error);
                    if (!error && (newest.empty() || time > modified)) {
                        newest = found;
                        modified = time;
                    }
                }
            }
            error.clear();
            it.increment(error);
        }
    }
    if (!newest.empty())
        return {newest, {}};
    return {{},
            provider + " not found. Install its CLI or VS Code extension, then Refresh usage and detection."};
}
} // namespace usage::host
