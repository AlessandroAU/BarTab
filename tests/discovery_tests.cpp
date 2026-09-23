#include "host/discovery.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace usage::host;
int main() {
    const auto root =
        std::filesystem::temp_directory_path() /
        ("usage-discovery-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    // Creates a stand-in binary; outside Windows only an executable one counts.
    auto write = [&](const std::filesystem::path& path, bool executable = true) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path).put('x');
#ifndef _WIN32
        if (executable)
            std::filesystem::permissions(path, std::filesystem::perms::owner_exec,
                                         std::filesystem::perm_options::add);
#else
        (void)executable;
#endif
    };
    auto check = [](bool ok, const char* message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    const auto codex_name = executable_name(false), claude_name = executable_name(true);
    int status = 0;
    try {
        DiscoveryLocations locations;
        locations.bins = {root / "missing", root / "bin"};
        locations.extensions = {root / "missing-extensions", root / "extensions"};
        check(discover_executable(false, locations).path.empty(), "Nothing installed finds nothing");
        check(!discover_executable(true, locations).error.empty(), "A missing provider explains itself");
        const auto codex = root / "extensions/openai.chatgpt-2/bin/native" / codex_name;
        const auto claude = root / "extensions/anthropic.claude-code-2/resources/native-binary" / claude_name;
        write(codex);
        check(discover_executable(true, locations).path.empty(),
              "One provider's extension is not the other's");
        write(root / "extensions/unrelated.extension-1" / claude_name);
        check(discover_executable(true, locations).path.empty(), "Unrelated extensions are ignored");
        write(claude);
        check(discover_executable(false, locations).path == codex, "Codex is found in its extension");
        check(discover_executable(true, locations).path == claude, "Claude is found in its extension");
        const auto older = root / "extensions/anthropic.claude-code-1/resources" / claude_name;
        write(older);
        std::filesystem::last_write_time(older,
                                         std::filesystem::last_write_time(claude) - std::chrono::hours(24));
        check(discover_executable(true, locations).path == claude, "The newest extension wins");
        write(root / "bin/codex.cmd");
        check(discover_executable(false, locations).path == codex,
              "Script shims with other names are ignored");
        write(root / "bin" / codex_name);
        check(discover_executable(false, locations).path == root / "bin" / codex_name,
              "PATH wins over extensions");
        locations.override_path = root / "not-installed" / codex_name;
        check(discover_executable(false, locations).path.empty(), "A missing override finds nothing");
        check(!discover_executable(false, locations).error.empty(), "A missing override explains itself");
        locations.override_path = claude;
        check(discover_executable(true, locations).path == claude, "A valid override is used");
        locations.override_path.clear();
        locations.bins.clear();
        locations.extensions.clear();
        locations.packages = {root / "npm/node_modules/@openai/codex"};
        const auto npm = locations.packages.front() / "vendor/native/codex" / codex_name;
        write(npm);
        check(discover_executable(false, locations).path == npm, "npm package binaries are found");
        std::filesystem::remove(npm);
        check(discover_executable(false, locations).path.empty(), "A removed binary is no longer found");
        check(discover_executable(true, locations).path.empty(), "Claude is not in Codex's package");
#ifndef _WIN32
        write(root / "npm/node_modules/@openai/codex/vendor/aarch64-apple-darwin/codex" / codex_name);
        write(root / "npm/node_modules/@openai/codex/vendor/x86_64-pc-windows-msvc/codex" / codex_name);
#if !defined(__APPLE__)
        check(discover_executable(false, locations).path.empty(), "Another OS's bundled binary is ignored");
#endif
        locations.bins = {root / "plain"};
        write(root / "plain" / codex_name, false);
        check(discover_executable(false, locations).path.empty(),
              "A file without execute permission is ignored");
        locations.override_path = root / "plain" / codex_name;
        check(!discover_executable(false, locations).error.empty(), "A non-executable override is rejected");
#endif
        std::cout << "Discovery: missing providers, extension binaries, PATH priority, overrides, npm, and "
                     "removal passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        status = 1;
    }
    std::error_code error;
    if (root.is_absolute() && root.parent_path() == std::filesystem::temp_directory_path() &&
        root.filename().string().find("usage-discovery-") == 0)
        std::filesystem::remove_all(root, error);
    return status;
}
