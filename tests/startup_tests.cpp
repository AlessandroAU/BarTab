#include "host/platform.hpp"
#include "host/startup.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace usage::host;
namespace {
// Toggles startup on, checks what was registered, and off again (twice, since
// turning off an absent entry must also succeed). `registered` checks that the
// platform stored this executable, quoted.
template <typename Registered> bool round_trip(Registered registered) {
    bool ok = !startup_state().enabled && startup_state().error.empty();
    ok = ok && set_startup(true).empty() && startup_state().enabled;
    ok = ok && registered();
    ok = ok && set_startup(false).empty() && !startup_state().enabled;
    return ok && set_startup(false).empty();
}
} // namespace
int main() {
#ifdef _WIN32
    // Sandbox HKCU so the test never touches the real Run key.
    const auto sandbox = L"Software\\BarTabStartupTest-" + std::to_wstring(GetCurrentProcessId());
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sandbox.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
        return 1;
    if (RegOverridePredefKey(HKEY_CURRENT_USER, key) != ERROR_SUCCESS) {
        RegCloseKey(key);
        RegDeleteTreeW(HKEY_CURRENT_USER, sandbox.c_str());
        return 1;
    }
    const auto run = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const auto command = [&](const wchar_t* name) {
        wchar_t value[32768]{};
        DWORD bytes = sizeof(value);
        return RegGetValueW(HKEY_CURRENT_USER, run, name, RRF_RT_REG_SZ, nullptr, value, &bytes) == ERROR_SUCCESS
                   ? std::wstring(value)
                   : std::wstring{};
    };
    bool ok = round_trip([&] { return command(L"BarTab") == L"\"" + executable_path().wstring() + L"\""; });
    // An entry from when the app was called UsageTracker moves to the new name
    // and executable, still switched on.
    const std::wstring previous = L"\"C:\\Tools\\UsageTracker\\UsageTracker.exe\"";
    ok = ok && RegSetKeyValueW(HKEY_CURRENT_USER, run, L"UsageTracker", REG_SZ, previous.c_str(),
                               static_cast<DWORD>((previous.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    ok = ok && startup_state().enabled && command(L"UsageTracker").empty() &&
         command(L"BarTab") == L"\"C:\\Tools\\UsageTracker\\BarTab.exe\"";
    ok = ok && set_startup(false).empty();
    RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    RegCloseKey(key);
    RegDeleteTreeW(HKEY_CURRENT_USER, sandbox.c_str());
#else
    // Point the per-user folders at a scratch directory.
    const auto sandbox =
        std::filesystem::temp_directory_path() /
        ("usage-startup-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(sandbox);
    setenv("HOME", sandbox.c_str(), 1);
    setenv("XDG_CONFIG_HOME", (sandbox / "config").c_str(), 1);
#ifdef __APPLE__
    const auto entry = sandbox / "Library/LaunchAgents/com.bartab.app.plist";
#else
    const auto entry = sandbox / "config/autostart/BarTab.desktop";
#endif
    const auto read = [&] {
        std::ifstream file(entry);
        std::stringstream contents;
        contents << file.rdbuf();
        return contents.str();
    };
    bool ok = round_trip([&] { return read().find(executable_path().string()) != std::string::npos; });
#ifndef __APPLE__
    // A desktop that switched the entry off in place leaves it disabled.
    ok = ok && set_startup(true).empty();
    {
        std::ofstream file(entry, std::ios::app);
        file << "Hidden=true\n";
    }
    ok = ok && !startup_state().enabled && set_startup(false).empty();
    // An entry from when the app was called UsageTracker moves to the new name
    // and executable, still switched on.
    const auto previous = entry.parent_path() / "UsageTracker.desktop";
    {
        std::ofstream file(previous);
        file << "[Desktop Entry]\nType=Application\nName=UsageTracker\n"
                "Exec=\"/opt/UsageTracker/UsageTracker\"\nX-GNOME-Autostart-enabled=true\n";
    }
    ok = ok && startup_state().enabled && !std::filesystem::exists(previous) &&
         read().find("Name=BarTab\n") != std::string::npos &&
         read().find("Exec=\"/opt/UsageTracker/BarTab\"") != std::string::npos;
    ok = ok && set_startup(false).empty();
#endif
    std::error_code ignored;
    std::filesystem::remove_all(sandbox, ignored);
#endif
    std::cout << (ok ? "Startup toggle registers the quoted executable path in an isolated location\n"
                     : "Startup test failed\n");
    return ok ? 0 : 1;
}
