#include "host/updater.hpp"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>

extern char** environ;

namespace usage::host {
namespace {
std::string with_errno(const char* message) {
    return std::string(message) + " (" + std::strerror(errno) + ").";
}
} // namespace

std::string install_update(const std::filesystem::path& staged, const std::filesystem::path& executable) {
    if (chmod(staged.c_str(), 0755) != 0)
        return with_errno("Could not make the new BarTab executable");
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
