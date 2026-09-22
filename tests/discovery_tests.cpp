#include "windows/discovery.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <stdexcept>
using namespace usage::windows;
int main() {
    const auto root=std::filesystem::temp_directory_path()/("usage-discovery-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto write=[&](const std::filesystem::path& path) { std::filesystem::create_directories(path.parent_path()); std::ofstream(path).put('x'); };
    auto check=[](bool ok) { if (!ok) throw std::runtime_error("Discovery assertion failed"); };
    int status=0;
    try {
        DiscoveryLocations locations;
        locations.bins={root/"missing",root/"bin"};
        locations.extensions={root/"missing-extensions",root/"extensions"};
        check(discover_executable(false,locations).path.empty());
        check(!discover_executable(true,locations).error.empty());
        const auto codex=root/"extensions/openai.chatgpt-2/bin/windows-x86_64/codex.exe";
        const auto claude=root/"extensions/anthropic.claude-code-2/resources/native-binary/claude.exe";
        write(codex);
        check(discover_executable(true,locations).path.empty());
        write(root/"extensions/unrelated.extension-1/claude.exe");
        check(discover_executable(true,locations).path.empty());
        write(claude);
        check(discover_executable(false,locations).path==codex);
        check(discover_executable(true,locations).path==claude);
        const auto older=root/"extensions/anthropic.claude-code-1/resources/claude.exe";
        write(older);
        std::filesystem::last_write_time(older,std::filesystem::last_write_time(claude)-std::chrono::hours(24));
        check(discover_executable(true,locations).path==claude);
        write(root/"bin/codex.cmd");
        check(discover_executable(false,locations).path==codex);
        write(root/"bin/codex.exe");
        check(discover_executable(false,locations).path==root/"bin/codex.exe");
        locations.override_path=root/"not-installed.exe";
        check(discover_executable(false,locations).path.empty());
        check(!discover_executable(false,locations).error.empty());
        locations.override_path=claude;
        check(discover_executable(true,locations).path==claude);
        locations.override_path.clear(); locations.bins.clear(); locations.extensions.clear();
        locations.packages={root/"npm/node_modules/@openai/codex"};
        const auto npm=locations.packages.front()/"vendor/x86_64-pc-windows-msvc/codex/codex.exe";
        write(npm); check(discover_executable(false,locations).path==npm);
        std::filesystem::remove(npm);
        check(discover_executable(false,locations).path.empty());
        check(discover_executable(true,locations).path.empty());
        std::cout << "Discovery: missing providers, extension binaries, PATH priority, overrides, npm, and removal passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; status=1; }
    std::error_code error;
    if (root.is_absolute() && root.parent_path()==std::filesystem::temp_directory_path() && root.filename().string().find("usage-discovery-")==0)
        std::filesystem::remove_all(root,error);
    return status;
}
