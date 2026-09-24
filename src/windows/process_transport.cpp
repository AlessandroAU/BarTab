#include "host/process_transport.hpp"
#include <windows.h>
#include <mutex>
#include <stdexcept>

namespace usage::host {
namespace {
struct Handle {
    HANDLE value{};
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
std::wstring widen(const std::string& value) {
    const int count =
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(count > 0 ? count : 0), L'\0');
    if (count > 0)
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}
// Quotes one argument so the child's C runtime parses it back unchanged:
// backslashes are literal except before a quote, where they double.
std::wstring quote(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        return argument;
    std::wstring result = L"\"";
    for (auto it = argument.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != argument.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == argument.end()) {
            result.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"')
            result.append(backslashes * 2 + 1, L'\\');
        else
            result.append(backslashes, L'\\');
        result.push_back(*it);
    }
    result.push_back(L'"');
    return result;
}
} // namespace
struct ProcessTransport::Impl {
    Handle input_read, input_write, output_read, output_write, null_output, process, thread, job;
    const std::atomic<bool>& stop;
    std::chrono::steady_clock::time_point deadline;
    std::string buffer;
    Impl(const std::filesystem::path& exe, const std::vector<std::string>& arguments,
         const std::atomic<bool>& stopped, std::chrono::milliseconds timeout)
        : stop(stopped) {
        static std::mutex spawn_mutex;
        std::unique_lock<std::mutex> spawn_lock(spawn_mutex);
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        if (!CreatePipe(&input_read.value, &input_write.value, &security, 0) ||
            !CreatePipe(&output_read.value, &output_write.value, &security, 0))
            throw std::runtime_error("Could not create usage pipes");
        SetHandleInformation(input_write.value, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(output_read.value, HANDLE_FLAG_INHERIT, 0);
        null_output.value = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                        OPEN_EXISTING, 0, nullptr);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input_read.value;
        startup.hStdOutput = output_write.value;
        startup.hStdError = null_output.value;
        PROCESS_INFORMATION info{};
        std::wstring command = L"\"" + exe.wstring() + L"\"";
        for (const auto& argument : arguments)
            command += L" " + quote(widen(argument));
        job.value = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        // The CLI and everything it starts run below normal priority: a usage
        // check is never more urgent than what the user is doing.
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PRIORITY_CLASS;
        limits.BasicLimitInformation.PriorityClass = BELOW_NORMAL_PRIORITY_CLASS;
        if (!job.value ||
            !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            throw std::runtime_error("Could not manage usage process");
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &info))
            throw std::runtime_error("Could not start usage helper");
        process.value = info.hProcess;
        thread.value = info.hThread;
        if (!AssignProcessToJobObject(job.value, process.value)) {
            TerminateProcess(process.value, 1);
            throw std::runtime_error("Could not manage usage process");
        }
        // EcoQoS before it runs: efficient cores and low clocks for the whole check.
        PROCESS_POWER_THROTTLING_STATE throttling{PROCESS_POWER_THROTTLING_CURRENT_VERSION,
                                                  PROCESS_POWER_THROTTLING_EXECUTION_SPEED,
                                                  PROCESS_POWER_THROTTLING_EXECUTION_SPEED};
        SetProcessInformation(process.value, ProcessPowerThrottling, &throttling, sizeof(throttling));
        ResumeThread(thread.value);
        CloseHandle(input_read.value);
        input_read.value = nullptr;
        CloseHandle(output_write.value);
        output_write.value = nullptr;
        spawn_lock.unlock();
        deadline = std::chrono::steady_clock::now() + timeout;
    }
};
ProcessTransport::ProcessTransport(const std::filesystem::path& executable,
                                   const std::vector<std::string>& arguments, const std::atomic<bool>& stop,
                                   std::chrono::milliseconds timeout)
    : impl_(std::make_unique<Impl>(executable, arguments, stop, timeout)) {}
ProcessTransport::~ProcessTransport() = default;
void ProcessTransport::send(std::string_view message) {
    const auto line = std::string(message) + "\n";
    DWORD written{};
    if (!WriteFile(impl_->input_write.value, line.data(), static_cast<DWORD>(line.size()), &written,
                   nullptr) ||
        written != line.size())
        throw std::runtime_error("Usage connection closed");
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
        DWORD available{};
        if (!PeekNamedPipe(state.output_read.value, nullptr, 0, nullptr, &available, nullptr))
            throw std::runtime_error("Usage connection closed");
        if (!available) {
            if (WaitForSingleObject(state.process.value, 50) == WAIT_OBJECT_0)
                throw std::runtime_error("Usage helper exited before replying");
            continue;
        }
        char bytes[4096];
        DWORD read{};
        if (!ReadFile(state.output_read.value, bytes, sizeof(bytes), &read, nullptr))
            throw std::runtime_error("Usage connection closed");
        state.buffer.append(bytes, read);
        if (state.buffer.size() > 1024 * 1024)
            throw std::runtime_error("Usage reply too large");
    }
    throw std::runtime_error("Usage request timed out");
}
} // namespace usage::host
