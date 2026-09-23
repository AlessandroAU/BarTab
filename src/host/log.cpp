#include "host/platform.hpp"
#include <ctime>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>

namespace usage::host {
void log(std::string_view message) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    const auto path = log_path();
    if (path.empty())
        return;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    // The log is capped at 1 MB: past that, drop the oldest half, cutting at a
    // line boundary, so the newest history always survives.
    constexpr std::uintmax_t log_limit = 1024 * 1024;
    const auto size = std::filesystem::file_size(path, error);
    if (!error && size >= log_limit) {
        std::string contents;
        {
            std::ifstream old(path, std::ios::binary);
            contents.assign(std::istreambuf_iterator<char>(old), std::istreambuf_iterator<char>());
        }
        const auto cut = contents.find('\n', contents.size() - log_limit / 2);
        auto trimmed = path;
        trimmed += ".tmp";
        {
            std::ofstream kept(trimmed, std::ios::binary | std::ios::trunc);
            if (cut != std::string::npos)
                kept.write(contents.data() + cut + 1,
                           static_cast<std::streamsize>(contents.size() - cut - 1));
        }
        std::filesystem::rename(trimmed, path, error);
        if (error)
            std::filesystem::remove(trimmed, error);
    }
    const auto now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ofstream stream(path, std::ios::app);
    stream << local.tm_year + 1900 << '-' << local.tm_mon + 1 << '-' << local.tm_mday << ' ' << local.tm_hour
           << ':' << local.tm_min << ':' << local.tm_sec << " [C++] " << message << '\n';
}
} // namespace usage::host
