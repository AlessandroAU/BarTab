#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace usage::host {
// One hidden process tree and a newline-delimited connection to it. Destroying
// the transport kills the whole tree. The stop flag must outlive this object.
// Implemented by src/windows/process_transport.cpp (a job object) and
// src/posix/process_transport.cpp (a process group).
class ProcessTransport {
  public:
    // `arguments` are UTF-8 and exclude the executable itself.
    ProcessTransport(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
                     const std::atomic<bool>& stop,
                     std::chrono::milliseconds timeout = std::chrono::seconds(20));
    ~ProcessTransport();
    ProcessTransport(const ProcessTransport&) = delete;
    ProcessTransport& operator=(const ProcessTransport&) = delete;
    void send(std::string_view message);
    std::string read_line();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace usage::host
