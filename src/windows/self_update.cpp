#include "host/updater.hpp"
#include <windows.h>

namespace usage::host {
namespace {
std::filesystem::path previous_executable(const std::filesystem::path& executable) {
    auto previous = executable;
    previous += L".old";
    return previous;
}
std::string with_code(const char* message) {
    return std::string(message) + " (error " + std::to_string(GetLastError()) + ").";
}
} // namespace

std::string install_update(const std::filesystem::path& staged, const std::filesystem::path& executable) {
    const auto previous = previous_executable(executable);
    DeleteFileW(previous.c_str());
    // A running executable cannot be replaced or deleted, but it can be renamed.
    if (!MoveFileExW(executable.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING))
        return with_code("Could not move the running BarTab aside");
    if (!MoveFileExW(staged.c_str(), executable.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto error = with_code("Could not put the new BarTab in place");
        MoveFileExW(previous.c_str(), executable.c_str(), MOVEFILE_REPLACE_EXISTING);
        return error;
    }
    return {};
}

std::string relaunch(const std::filesystem::path& executable) {
    std::wstring command = L"\"" + executable.wstring() + L"\" --updated";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &startup, &info))
        return with_code("Could not start the new BarTab");
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return {};
}

void remove_update_leftovers(const std::filesystem::path& executable) {
    // The previous executable stays locked until its process has exited, which
    // --updated waits for, so this usually succeeds on the first start after.
    DeleteFileW(previous_executable(executable).c_str());
    const auto staged = staged_update(executable);
    DeleteFileW(staged.c_str());
    DeleteFileW((staged.wstring() + L".sig").c_str());
    DeleteFileW((staged.wstring() + L".part").c_str());
    DeleteFileW((staged.wstring() + L".sig.part").c_str());
}
} // namespace usage::host
