#include "windows/discovery.hpp"
#include <algorithm>
#include <cwctype>

namespace usage::windows {
namespace {
bool file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}
std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}
// Search only recognized CLI packages/extensions, without following junctions.
std::filesystem::path bundled(const std::filesystem::path& root, const wchar_t* name) {
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, error),
        end;
    std::filesystem::path newest;
    std::filesystem::file_time_type modified = std::filesystem::file_time_type::min();
    unsigned visited = 0;
    while (!error && it != end && ++visited <= 20000) {
        const auto component = lower(it->path().filename().wstring());
        const bool wrong_arch = component.find(L"arm64") != std::wstring::npos ||
                                component.find(L"aarch64") != std::wstring::npos;
        if (it.depth() >= 7 || wrong_arch || it->is_symlink(error))
            it.disable_recursion_pending();
        error.clear();
        if (lower(it->path().filename().wstring()) == name && file(it->path())) {
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
DiscoveryResult discover_executable(bool claude, const DiscoveryLocations& locations) {
    const wchar_t* name = claude ? L"claude.exe" : L"codex.exe";
    const std::string provider = claude ? "Claude" : "Codex";
    if (!locations.override_path.empty()) {
        if (lower(locations.override_path.extension().wstring()) == L".exe" && file(locations.override_path))
            return {locations.override_path, {}};
        return {{},
                provider + " override is not an existing .exe. Correct or remove USAGETRACKER_" +
                    (claude ? "CLAUDE" : "CODEX") + "."};
    }
    for (const auto& bin : locations.bins)
        if (file(bin / name))
            return {bin / name, {}};
    for (const auto& package : locations.packages) {
        const auto found = bundled(package, name);
        if (!found.empty())
            return {found, {}};
    }
    const std::wstring prefix = claude ? L"anthropic.claude-code-" : L"openai.chatgpt-";
    std::filesystem::path newest;
    auto modified = std::filesystem::file_time_type::min();
    for (const auto& root : locations.extensions) {
        std::error_code error;
        std::filesystem::directory_iterator it(
            root, std::filesystem::directory_options::skip_permission_denied, error),
            end;
        while (!error && it != end) {
            const auto folder = lower(it->path().filename().wstring());
            if (folder.find(prefix) == 0 && folder.find(L"arm64") == std::wstring::npos) {
                const auto found = bundled(it->path(), name);
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
} // namespace usage::windows
