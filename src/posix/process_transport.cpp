#include "host/process_transport.hpp"
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace usage::host {
namespace {
struct Descriptor {
    int value{-1};
    ~Descriptor() {
        reset();
    }
    Descriptor() = default;
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    void reset() {
        if (value >= 0)
            close(value);
        value = -1;
    }
};
// Both ends close on exec, so a helper started by another thread never
// inherits them; the child's copies come from the spawn's dup2 actions.
void make_pipe(Descriptor& read_end, Descriptor& write_end) {
    int ends[2];
#ifdef __linux__
    if (pipe2(ends, O_CLOEXEC) != 0)
        throw std::runtime_error("Could not create usage pipes");
#else
    if (pipe(ends) != 0)
        throw std::runtime_error("Could not create usage pipes");
    fcntl(ends[0], F_SETFD, FD_CLOEXEC);
    fcntl(ends[1], F_SETFD, FD_CLOEXEC);
#endif
    read_end.value = ends[0];
    write_end.value = ends[1];
}
} // namespace
struct ProcessTransport::Impl {
    Descriptor input_write, output_read;
    pid_t process{-1};
    bool reaped{};
    const std::atomic<bool>& stop;
    std::chrono::steady_clock::time_point deadline;
    std::string buffer;
    Impl(const std::filesystem::path& exe, const std::vector<std::string>& arguments,
         const std::atomic<bool>& stopped, std::chrono::milliseconds timeout)
        : stop(stopped) {
        // A helper that exits mid-write must fail send(), not kill the host.
        static std::once_flag ignore_sigpipe;
        std::call_once(ignore_sigpipe, [] { std::signal(SIGPIPE, SIG_IGN); });
        // Without pipe2 the descriptors are briefly inheritable; serialize
        // spawns so no helper can pick up another's pipes.
        static std::mutex spawn_mutex;
        std::lock_guard<std::mutex> spawn_lock(spawn_mutex);
        Descriptor input_read, output_write;
        make_pipe(input_read, input_write);
        make_pipe(output_read, output_write);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, input_read.value, STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, output_write.value, STDOUT_FILENO);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawnattr_t attributes;
        posix_spawnattr_init(&attributes);
        // Its own process group, so the destructor can kill the whole tree; and
        // default signal handling, so the helper does not inherit SIGPIPE ignored.
        sigset_t defaults, none;
        sigfillset(&defaults);
        sigemptyset(&none);
        posix_spawnattr_setsigdefault(&attributes, &defaults);
        posix_spawnattr_setsigmask(&attributes, &none);
        posix_spawnattr_setpgroup(&attributes, 0);
        posix_spawnattr_setflags(&attributes,
                                 POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
        const std::string program = exe.string();
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(program.c_str()));
        for (const auto& argument : arguments)
            argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        const int status =
            posix_spawn(&process, program.c_str(), &actions, &attributes, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
        if (status != 0) {
            process = -1;
            throw std::runtime_error("Could not start usage helper");
        }
        deadline = std::chrono::steady_clock::now() + timeout;
    }
    ~Impl() {
        input_write.reset();
        output_read.reset();
        if (process <= 0)
            return;
        kill(-process, SIGKILL);
        if (!reaped)
            while (waitpid(process, nullptr, 0) < 0 && errno == EINTR) {
            }
    }
    bool exited() {
        if (reaped)
            return true;
        int status{};
        reaped = waitpid(process, &status, WNOHANG) == process;
        return reaped;
    }
};
ProcessTransport::ProcessTransport(const std::filesystem::path& executable,
                                   const std::vector<std::string>& arguments, const std::atomic<bool>& stop,
                                   std::chrono::milliseconds timeout)
    : impl_(std::make_unique<Impl>(executable, arguments, stop, timeout)) {}
ProcessTransport::~ProcessTransport() = default;
void ProcessTransport::send(std::string_view message) {
    const auto line = std::string(message) + "\n";
    std::size_t sent = 0;
    while (sent < line.size()) {
        const auto written = write(impl_->input_write.value, line.data() + sent, line.size() - sent);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            throw std::runtime_error("Usage connection closed");
        sent += static_cast<std::size_t>(written);
    }
}
std::string ProcessTransport::read_line() {
    auto& state = *impl_;
    while (!state.stop && std::chrono::steady_clock::now() < state.deadline) {
        const auto newline = state.buffer.find('\n');
        if (newline != std::string::npos) {
            auto line = state.buffer.substr(0, newline);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            state.buffer.erase(0, newline + 1);
            return line;
        }
        pollfd readable{state.output_read.value, POLLIN, 0};
        const int ready = poll(&readable, 1, 50);
        if (ready < 0 && errno != EINTR)
            throw std::runtime_error("Usage connection closed");
        if (ready <= 0) {
            // A grandchild may still hold the pipe open, so EOF alone is not
            // enough to notice the helper has gone.
            if (state.exited())
                throw std::runtime_error("Usage helper exited before replying");
            continue;
        }
        char bytes[4096];
        const auto count = read(state.output_read.value, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            throw std::runtime_error("Usage connection closed");
        if (count == 0)
            throw std::runtime_error("Usage helper exited before replying");
        state.buffer.append(bytes, static_cast<std::size_t>(count));
        if (state.buffer.size() > 1024 * 1024)
            throw std::runtime_error("Usage reply too large");
    }
    throw std::runtime_error("Usage request timed out");
}
} // namespace usage::host
