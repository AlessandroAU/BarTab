#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace usage::windows {
// One hidden process tree and a newline-delimited connection. The stop flag must outlive this object.
class ProcessTransport {
  public:
    ProcessTransport(const std::filesystem::path& executable, const std::wstring& arguments,
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
} // namespace usage::windows
