#include "host/service_discovery.hpp"
#include <cstdlib>
#include <string>

namespace usage::host {
namespace {
std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}
std::filesystem::path unquote(std::string value) {
    if (value.size() > 1 && value.front() == '"' && value.back() == '"')
        value = value.substr(1, value.size() - 2);
    return value;
}
// npm's platform packages are named for the OS and CPU they carry.
#if defined(__APPLE__)
constexpr const char* npm_os = "darwin";
#else
constexpr const char* npm_os = "linux";
#endif
#if defined(__aarch64__)
constexpr const char* npm_cpu = "arm64";
#else
constexpr const char* npm_cpu = "x64";
#endif
} // namespace
DiscoveryLocations search_locations(Service service) {
    const bool claude = service == Service::Claude;
    DiscoveryLocations locations;
    locations.override_path = unquote(environment(claude ? "BARTAB_CLAUDE" : "BARTAB_CODEX"));
    const auto search = environment("PATH");
    for (std::size_t start = 0; start <= search.size();) {
        auto end = search.find(':', start);
        if (end == std::string::npos)
            end = search.size();
        if (end > start)
            locations.bins.push_back(search.substr(start, end - start));
        start = end + 1;
    }
    const std::filesystem::path home = environment("HOME");
    // An npm prefix keeps global packages in lib/node_modules; recent releases
    // install the native binary as a sibling platform package.
    auto npm = [&](const std::filesystem::path& prefix) {
        if (prefix.empty())
            return;
        const auto scope = prefix / "lib/node_modules" / (claude ? "@anthropic-ai" : "@openai");
        const std::string package = claude ? "claude-code" : "codex";
        locations.packages.push_back(scope / package);
        locations.packages.push_back(scope / (package + "-" + npm_os + "-" + npm_cpu));
    };
    // Every bin folder on PATH may be an npm prefix's bin, as with nvm and Homebrew.
    for (const auto& bin : locations.bins)
        npm(bin.parent_path());
    if (!home.empty()) {
        for (const char* bin :
             {".local/bin", ".claude/local", ".npm-global/bin", ".bun/bin", ".volta/bin", ".cargo/bin"})
            locations.bins.push_back(home / bin);
        npm(home / ".npm-global");
        npm(home / ".local");
        std::error_code error;
        for (std::filesystem::directory_iterator it(home / ".nvm/versions/node", error), end;
             !error && it != end; it.increment(error))
            npm(it->path());
        for (const char* editor :
             {".vscode", ".vscode-insiders", ".vscode-oss", ".vscode-server", ".vscode-server-insiders",
              ".cursor", ".cursor-server", ".windsurf", ".windsurf-server"})
            locations.extensions.push_back(home / editor / "extensions");
        // Flatpak builds of VS Code and VSCodium keep their data under ~/.var.
        locations.extensions.push_back(home / ".var/app/com.visualstudio.code/data/vscode/extensions");
        locations.extensions.push_back(home / ".var/app/com.vscodium.codium/data/codium/extensions");
    }
    for (const char* prefix : {"/usr/local", "/opt/homebrew", "/usr"}) {
        locations.bins.push_back(std::filesystem::path(prefix) / "bin");
        npm(prefix);
    }
    for (const char* variable : {"NPM_CONFIG_PREFIX", "npm_config_prefix", "VOLTA_HOME"})
        npm(environment(variable));
    for (const char* variable : {"VSCODE_EXTENSIONS", "VSCODE_EXTENSIONS_DIR"}) {
        const auto root = environment(variable);
        if (!root.empty())
            locations.extensions.push_back(root);
    }
    const auto portable = environment("VSCODE_PORTABLE");
    if (!portable.empty())
        locations.extensions.push_back(std::filesystem::path(portable) / "extensions");
    return locations;
}
} // namespace usage::host
