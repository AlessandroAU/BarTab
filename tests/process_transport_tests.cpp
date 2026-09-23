#include "host/platform.hpp"
#include "host/process_transport.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

namespace {
using usage::host::ProcessTransport;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void fails(ProcessTransport& process, const char* expected) {
    std::string error;
    try {
        process.read_line();
    } catch (const std::exception& failure) {
        error = failure.what();
    }
    check(error.find(expected) != std::string::npos, "Transport failure did not match expected error");
}
unsigned long process_id() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}
// Whether the helper `pid` exits within two seconds of the transport going.
bool terminates(unsigned long pid) {
#ifdef _WIN32
    HANDLE helper = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!helper)
        return true;
    const bool ended = WaitForSingleObject(helper, 2000) == WAIT_OBJECT_0;
    CloseHandle(helper);
    return ended;
#else
    for (int i = 0; i < 200; ++i) {
        if (kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
#endif
}
int child(const std::string& mode) {
    if (mode == "echo") {
        std::string input;
        std::getline(std::cin, input);
        std::cout << input.substr(0, 2) << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::cout << input.substr(2) << "\nsecond\n" << std::flush;
    } else if (mode == "large") {
        std::cout << std::string(1024 * 1024 + 4096, 'x') << std::flush;
    } else if (mode == "exit") {
        return 0;
    } else {
        std::cout << process_id() << '\n' << std::flush;
    }
    std::this_thread::sleep_for(std::chrono::seconds(10));
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--child") {
        if (std::string(argv[2]) == "argv") {
            // Echo the arguments back, one per line, to prove they survive quoting.
            for (int i = 3; i < argc; ++i)
                std::cout << '[' << argv[i] << "]\n";
            std::cout << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(10));
            return 0;
        }
        return child(argv[2]);
    }
    try {
        const auto path = usage::host::executable_path();
        std::atomic<bool> stop{false};
        {
            ProcessTransport process(path, {"--child", "echo"}, stop);
            process.send("hello");
            check(process.read_line() == "hello", "Fragmented output is assembled into one line");
            check(process.read_line() == "second", "Buffered subsequent lines are preserved");
        }
        {
            ProcessTransport process(path, {"--child", "argv", "two words", "quote\"d", "trailing\\", ""},
                                     stop);
            check(process.read_line() == "[two words]", "An argument with a space arrives whole");
            check(process.read_line() == "[quote\"d]", "An embedded quote survives");
            check(process.read_line() == "[trailing\\]", "A trailing backslash survives");
            check(process.read_line() == "[]", "An empty argument survives");
        }
        {
            ProcessTransport process(path, {"--child", "large"}, stop);
            fails(process, "too large");
        }
        {
            ProcessTransport process(path, {"--child", "exit"}, stop);
            fails(process, "Usage");
        }
        {
            ProcessTransport process(path, {"--child", "wait"}, stop, std::chrono::milliseconds(200));
            process.read_line();
            fails(process, "timed out");
        }
        unsigned long helper{};
        {
            ProcessTransport process(path, {"--child", "wait"}, stop);
            helper = std::stoul(process.read_line());
            stop = true;
            fails(process, "timed out"); // Preserve the existing cancellation error.
        }
        check(terminates(helper), "Transport destruction terminates helper");
        bool failed = false;
        try {
            ProcessTransport process(path.parent_path() / "missing-helper.exe", {}, stop);
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "Process creation failure propagates");
        std::cout << "Process framing, quoting, timeout, cancellation and cleanup passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
