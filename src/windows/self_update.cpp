#include "host/updater.hpp"
#include <windows.h>
#include <sddl.h>
#include <random>
#include <stdexcept>
#include <vector>
#include <algorithm>

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

std::filesystem::path private_update_directory(const std::filesystem::path& parent) {
    HANDLE token{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        throw std::runtime_error("Could not identify the update owner");
    DWORD size{};
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> data(size);
    const bool obtained = GetTokenInformation(token, TokenUser, data.data(), size, &size) != FALSE;
    CloseHandle(token);
    if (!obtained)
        throw std::runtime_error("Could not identify the update owner");
    LPWSTR sid{};
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid))
        throw std::runtime_error("Could not identify the update owner");
    const std::wstring acl = L"D:P(A;OICI;FA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor{};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1,
                                                               &descriptor, nullptr))
        throw std::runtime_error("Could not protect the update directory");
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    std::filesystem::path result;
    // The name avoids collisions; CreateDirectory and its explicit protected
    // DACL establish privacy atomically, even in a shared parent directory.
    try {
        std::random_device random;
        for (int attempt = 0; attempt < 32; ++attempt) {
            const auto candidate = parent / (L"BarTab-update-" + std::to_wstring(random()) + L"-" +
                                              std::to_wstring(random()));
            if (CreateDirectoryW(candidate.c_str(), &security)) {
                result = candidate;
                break;
            }
            if (GetLastError() != ERROR_ALREADY_EXISTS)
                break;
        }
    } catch (...) {
        LocalFree(descriptor);
        throw;
    }
    LocalFree(descriptor);
    if (result.empty())
        throw std::runtime_error("Could not create a private update directory");
    return result;
}

std::string install_update(std::string_view verified_bytes, const std::filesystem::path& executable) {
    std::filesystem::path directory;
    try {
        directory = private_update_directory(std::filesystem::absolute(executable).parent_path());
    } catch (const std::exception&) {
        return "Could not create a private installation directory";
    }
    const auto staged = directory / L"binary";
    struct Cleanup {
        const std::filesystem::path& file;
        const std::filesystem::path& directory;
        ~Cleanup() { DeleteFileW(file.c_str()); RemoveDirectoryW(directory.c_str()); }
    } cleanup{staged, directory};
    HANDLE file = CreateFileW(staged.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return with_code("Could not create the new BarTab");
    bool written = true;
    while (!verified_bytes.empty()) {
        const DWORD count = static_cast<DWORD>((std::min)(verified_bytes.size(), std::size_t{64 * 1024}));
        DWORD done{};
        if (!WriteFile(file, verified_bytes.data(), count, &done, nullptr) || done != count) {
            written = false;
            break;
        }
        verified_bytes.remove_prefix(done);
    }
    if (!FlushFileBuffers(file))
        written = false;
    CloseHandle(file);
    if (!written)
        return "Could not write the new BarTab";
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
