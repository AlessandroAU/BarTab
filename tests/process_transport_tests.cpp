#include "windows/process_transport.hpp"
#include <windows.h>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void fails(usage::windows::ProcessTransport& process, const char* expected) {
    std::string error;
    try {
        process.read_line();
    } catch (const std::exception& failure) {
        error = failure.what();
    }
    check(error.find(expected) != std::string::npos, "Transport failure did not match expected error");
}
int child(const std::string& mode) {
    if (mode == "echo") {
        std::string input;
        std::getline(std::cin, input);
        std::cout << input.substr(0, 2) << std::flush;
        Sleep(10);
        std::cout << input.substr(2) << "\nsecond\n" << std::flush;
    } else if (mode == "large") {
        std::cout << std::string(1024 * 1024 + 4096, 'x') << std::flush;
    } else if (mode == "exit") {
        return 0;
    } else {
        std::cout << GetCurrentProcessId() << '\n' << std::flush;
    }
    Sleep(10000);
    return 0;
}
struct ProcessHandle {
    HANDLE value{};
    ~ProcessHandle() {
        if (value)
            CloseHandle(value);
    }
};
} // namespace
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--child")
        return child(argv[2]);
    try {
        wchar_t path[32768]{};
        check(GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path))) != 0,
              "Find test executable");
        using usage::windows::ProcessTransport;
        std::atomic<bool> stop{false};
        {
            ProcessTransport process(path, L" --child echo", stop);
            process.send("hello");
            check(process.read_line() == "hello", "Fragmented output is assembled into one line");
            check(process.read_line() == "second", "Buffered subsequent lines are preserved");
        }
        {
            ProcessTransport process(path, L" --child large", stop);
            fails(process, "too large");
        }
        {
            ProcessTransport process(path, L" --child exit", stop);
            fails(process, "Usage");
        }
        {
            ProcessTransport process(path, L" --child wait", stop, std::chrono::milliseconds(200));
            process.read_line();
            fails(process, "timed out");
        }
        ProcessHandle helper;
        {
            ProcessTransport process(path, L" --child wait", stop);
            const auto pid = static_cast<DWORD>(std::stoul(process.read_line()));
            helper.value = OpenProcess(SYNCHRONIZE, FALSE, pid);
            check(helper.value != nullptr, "Observe child lifetime");
            stop = true;
            fails(process, "timed out"); // Preserve the existing cancellation error.
        }
        check(WaitForSingleObject(helper.value, 2000) == WAIT_OBJECT_0,
              "Transport destruction terminates helper");
        bool failed = false;
        try {
            ProcessTransport process(std::filesystem::path(path) / L"missing.exe", L"", stop);
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "Process creation failure propagates");
        std::cout << "Process framing, timeout, cancellation and cleanup passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
