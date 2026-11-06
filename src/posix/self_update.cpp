#include "host/updater.hpp"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <stdexcept>
#include <vector>

extern char** environ;

namespace usage::host {
namespace {
std::string with_errno(const char* message) {
    return std::string(message) + " (" + std::strerror(errno) + ").";
}
} // namespace

std::filesystem::path private_update_directory(const std::filesystem::path& parent) {
    const auto pattern = (parent / "BarTab-update-XXXXXX").string();
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    // mkdtemp creates mode 0700 atomically; no chmod privacy window.
    if (!mkdtemp(name.data()))
        throw std::runtime_error("Could not create a private update directory");
    return std::filesystem::path(name.data());
}

std::string install_update(std::string_view verified_bytes, const std::filesystem::path& executable) {
    std::filesystem::path directory;
    try {
        directory = private_update_directory(std::filesystem::absolute(executable).parent_path());
    } catch (const std::exception&) {
        return "Could not create a private installation directory";
    }
    const auto staged = directory / "binary";
    struct Cleanup {
        const std::filesystem::path& file;
        const std::filesystem::path& directory;
        ~Cleanup() { unlink(file.c_str()); rmdir(directory.c_str()); }
    } cleanup{staged, directory};
    const int file = open(staged.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file < 0)
        return with_errno("Could not create the new BarTab");
    bool written = true;
    while (!verified_bytes.empty()) {
        const auto done = write(file, verified_bytes.data(), verified_bytes.size());
        if (done < 0 && errno == EINTR)
            continue;
        if (done <= 0) {
            written = false;
            break;
        }
        verified_bytes.remove_prefix(static_cast<std::size_t>(done));
    }
    if (fchmod(file, 0755) != 0 || fsync(file) != 0)
        written = false;
    if (close(file) != 0)
        written = false;
    if (!written)
        return "Could not write the new BarTab";
    // The running process keeps its old file open by inode, so replacing the
    // name under it is safe.
    if (std::rename(staged.c_str(), executable.c_str()) != 0)
        return with_errno("Could not put the new BarTab in place");
    return {};
}

std::string relaunch(const std::filesystem::path& executable) {
    const std::string program = executable.string();
    std::string flag = "--updated";
    char* argv[] = {const_cast<char*>(program.c_str()), flag.data(), nullptr};
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    // Its own process group, and default signal handling: this process has
    // SIGPIPE ignored, which exec would otherwise carry over.
    sigset_t defaults, none;
    sigfillset(&defaults);
    sigemptyset(&none);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    posix_spawnattr_setsigmask(&attributes, &none);
    posix_spawnattr_setpgroup(&attributes, 0);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    pid_t process{};
    const int status = posix_spawn(&process, program.c_str(), nullptr, &attributes, argv, environ);
    posix_spawnattr_destroy(&attributes);
    if (status != 0) {
        errno = status;
        return with_errno("Could not start the new BarTab");
    }
    return {};
}

void remove_update_leftovers(const std::filesystem::path& executable) {
    const auto staged = staged_update(executable).string();
    for (const auto& path : {staged, staged + ".sig", staged + ".part", staged + ".sig.part",
                             staged + ".part.status", staged + ".sig.part.status"})
        std::remove(path.c_str());
}
} // namespace usage::host
