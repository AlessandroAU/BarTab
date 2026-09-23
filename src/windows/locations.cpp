#include "host/service_discovery.hpp"
#include <windows.h>

namespace usage::host {
namespace {
std::wstring environment(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size)
        return {};
    std::wstring value(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), size);
    if (!written || written >= size)
        return {};
    value.resize(written);
    return value;
}
std::filesystem::path unquote(std::wstring value) {
    if (value.size() > 1 && value.front() == L'"' && value.back() == L'"')
        value = value.substr(1, value.size() - 2);
    return value;
}
} // namespace
DiscoveryLocations search_locations(Service service) {
    const bool claude = service == Service::Claude;
    DiscoveryLocations locations;
    locations.override_path = unquote(environment(claude ? L"BARTAB_CLAUDE" : L"BARTAB_CODEX"));
    const auto search = environment(L"PATH");
    for (std::size_t start = 0; start < search.size();) {
        auto end = search.find(L';', start);
        if (end == std::wstring::npos)
            end = search.size();
        const auto part = unquote(search.substr(start, end - start));
        if (!part.empty())
            locations.bins.push_back(part);
        start = end + 1;
    }
    auto npm = [&](const std::filesystem::path& prefix) {
        if (prefix.empty())
            return;
        const auto modules = prefix / L"node_modules";
        locations.packages.push_back(modules / (claude ? L"@anthropic-ai" : L"@openai") /
                                     (claude ? L"claude-code" : L"codex"));
        // Recent npm releases install platform binaries as sibling packages.
        locations.packages.push_back(modules / (claude ? L"@anthropic-ai" : L"@openai") /
                                     (claude ? L"claude-code-win32-x64" : L"codex-win32-x64"));
    };
    for (const auto& bin : locations.bins) {
        npm(bin);
        // Portable VS Code/Insiders installations expose their bin folder on PATH.
        locations.extensions.push_back(bin.parent_path() / L"data/extensions");
    }
    const auto home = environment(L"USERPROFILE"), local = environment(L"LOCALAPPDATA"),
               roaming = environment(L"APPDATA");
    if (!home.empty()) {
        const std::filesystem::path root(home);
        for (const auto* bin : {L".local/bin", L".cargo/bin", L"scoop/shims"})
            locations.bins.push_back(root / bin);
        for (const auto* editor : {L".vscode", L".vscode-insiders", L".vscode-oss", L".cursor", L".windsurf"})
            locations.extensions.push_back(root / editor / L"extensions");
        locations.packages.push_back(root / L"scoop/apps" / (claude ? L"claude-code" : L"codex") /
                                     L"current");
    }
    if (!roaming.empty())
        npm(std::filesystem::path(roaming) / L"npm");
    if (!local.empty()) {
        npm(std::filesystem::path(local) / L"npm");
        locations.bins.push_back(std::filesystem::path(local) / L"Microsoft/WinGet/Links");
    }
    for (const auto* variable : {L"NPM_CONFIG_PREFIX", L"VOLTA_HOME", L"NVM_SYMLINK"})
        npm(environment(variable));
    for (const auto* variable : {L"VSCODE_EXTENSIONS", L"VSCODE_EXTENSIONS_DIR"}) {
        const auto root = environment(variable);
        if (!root.empty())
            locations.extensions.push_back(root);
    }
    const auto portable = environment(L"VSCODE_PORTABLE");
    if (!portable.empty())
        locations.extensions.push_back(std::filesystem::path(portable) / L"extensions");
    return locations;
}
} // namespace usage::host
