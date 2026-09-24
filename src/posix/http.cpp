#include "host/updater.hpp"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace usage::host {
namespace {
std::string curl_error(int code) {
    switch (code) {
    case 6:
        return "the server could not be found; check the network connection";
    case 7:
        return "could not connect to the server";
    case 23:
        return "could not write the download";
    case 28:
        return "the server took too long to answer";
    case 35:
    case 60:
        return "the server's certificate was not accepted";
    case 63:
        return "the download was larger than expected";
    case 127:
        return "the curl command is not installed";
    default:
        return "curl failed with exit code " + std::to_string(code);
    }
}
} // namespace

std::string download(const std::string& url, const std::filesystem::path& destination,
                     const std::atomic<bool>& stop) {
    if (url.rfind("https://", 0) != 0)
        return "only HTTPS addresses are allowed";
    auto partial = destination;
    partial += ".part";
    const std::string output = partial.string();
    // curl writes the HTTP status here, rather than failing on anything but 200.
    const std::string status_path = output + ".status";
    const std::string agent = "BarTab/" + current_version().text();
    // HTTPS only, redirects included; bounded in time and size.
    std::vector<std::string> arguments{"curl",           "--silent",      "--location",
                                       "--proto",        "=https",        "--proto-redir",
                                       "=https",         "--connect-timeout", "20",
                                       "--max-time",     "600",           "--max-filesize",
                                       "67108864",       "--user-agent",  agent,
                                       "--write-out",    "%{http_code}",  "--output",
                                       output,           "--",            url};
    std::vector<char*> argv;
    for (auto& argument : arguments)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, status_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                                     0600);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    sigset_t defaults, none;
    sigfillset(&defaults);
    sigemptyset(&none);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    posix_spawnattr_setsigmask(&attributes, &none);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    pid_t process{};
    const int spawned = posix_spawnp(&process, "curl", &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (spawned != 0)
        return curl_error(127);
    int status{};
    for (;;) {
        const auto done = waitpid(process, &status, WNOHANG);
        if (done == process)
            break;
        if (done < 0 && errno != EINTR)
            return "lost track of curl";
        if (stop) {
            kill(process, SIGKILL);
            while (waitpid(process, nullptr, 0) < 0 && errno == EINTR) {
            }
            std::remove(output.c_str());
            std::remove(status_path.c_str());
            return "stopped";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    int http = 0;
    if (FILE* file = std::fopen(status_path.c_str(), "r")) {
        if (std::fscanf(file, "%d", &http) != 1)
            http = 0;
        std::fclose(file);
    }
    std::remove(status_path.c_str());
    if (code != 0 || http != 200) {
        std::remove(output.c_str());
        if (code < 0)
            return "curl stopped unexpectedly";
        return code != 0 ? curl_error(code) : "the server answered HTTP " + std::to_string(http);
    }
    if (std::rename(output.c_str(), destination.c_str()) != 0) {
        std::remove(output.c_str());
        return "could not write " + destination.string();
    }
    return {};
}
} // namespace usage::host
