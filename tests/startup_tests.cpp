#include "windows/startup.hpp"
#include <iostream>
#include <string>

int main() {
    const auto sandbox = L"Software\\UsageTrackerStartupTest-" + std::to_wstring(GetCurrentProcessId());
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sandbox.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
        return 1;
    if (RegOverridePredefKey(HKEY_CURRENT_USER, key) != ERROR_SUCCESS) {
        RegCloseKey(key);
        RegDeleteTreeW(HKEY_CURRENT_USER, sandbox.c_str());
        return 1;
    }
    using namespace usage::windows;
    bool ok = !startup_state().enabled && startup_state().error == ERROR_SUCCESS;
    ok = ok && set_startup(true) == ERROR_SUCCESS && startup_state().enabled;
    wchar_t command[32768]{};
    DWORD bytes = sizeof(command);
    ok = ok && RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                            L"UsageTracker", RRF_RT_REG_SZ, nullptr, command, &bytes) == ERROR_SUCCESS;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    ok = ok && std::wstring(command) == L"\"" + std::wstring(executable) + L"\"";
    ok = ok && set_startup(false) == ERROR_SUCCESS && !startup_state().enabled;
    ok = ok && set_startup(false) == ERROR_SUCCESS;
    RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    RegCloseKey(key);
    RegDeleteTreeW(HKEY_CURRENT_USER, sandbox.c_str());
    std::cout << (ok ? "Startup toggle and quoted executable path passed in isolated registry key\n"
                     : "Startup test failed\n");
    return ok ? 0 : 1;
}
