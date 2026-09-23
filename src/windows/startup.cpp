#include "host/startup.hpp"
#include "windows/platform.hpp"
#include <windows.h>
#include <string>

namespace usage::host {
namespace {
constexpr wchar_t key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t name[] = L"UsageTracker";
std::string describe(LSTATUS status) {
    wchar_t detail[512]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   static_cast<DWORD>(status), 0, detail, static_cast<DWORD>(std::size(detail)), nullptr);
    return windows::utf8(detail) + "Windows error: " + std::to_string(status);
}
LSTATUS register_executable() {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (!length)
        return static_cast<LSTATUS>(GetLastError());
    if (length >= std::size(path))
        return ERROR_INSUFFICIENT_BUFFER;
    const std::wstring command = L"\"" + std::wstring(path, length) + L"\"";
    // The Run key has a 260-character command limit, including the quotes.
    if (command.size() > 260)
        return ERROR_FILENAME_EXCED_RANGE;
    HKEY opened{};
    const auto status =
        RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &opened, nullptr);
    if (status != ERROR_SUCCESS)
        return status;
    const auto written =
        RegSetValueExW(opened, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(opened);
    return written;
}
} // namespace
StartupState startup_state() {
    DWORD bytes{};
    const auto status = RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
        return {};
    if (status != ERROR_SUCCESS)
        return {false, describe(status)};
    return {bytes > sizeof(wchar_t), {}};
}
std::string set_startup(bool enabled) {
    LSTATUS status = ERROR_SUCCESS;
    if (!enabled) {
        status = RegDeleteKeyValueW(HKEY_CURRENT_USER, key, name);
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
            status = ERROR_SUCCESS;
    } else
        status = register_executable();
    return status == ERROR_SUCCESS ? std::string{} : describe(status);
}
} // namespace usage::host
