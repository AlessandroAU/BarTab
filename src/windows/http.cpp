#include "host/updater.hpp"
#include "windows/platform.hpp"
#include <windows.h>
#include <winhttp.h>
#include <fstream>

namespace usage::host {
namespace {
struct Internet {
    HINTERNET value{};
    explicit Internet(HINTERNET handle) : value(handle) {}
    ~Internet() {
        if (value)
            WinHttpCloseHandle(value);
    }
    Internet(const Internet&) = delete;
    Internet& operator=(const Internet&) = delete;
};
std::string network_error(DWORD code) {
    switch (code) {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return "the server could not be found; check the network connection";
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR:
        return "could not connect to the server";
    case ERROR_WINHTTP_TIMEOUT:
        return "the server took too long to answer";
    case ERROR_WINHTTP_SECURE_FAILURE:
        return "the server's certificate was not accepted";
    default:
        return "network error " + std::to_string(code);
    }
}
} // namespace

std::string download(const std::string& url, const std::filesystem::path& destination,
                     const std::atomic<bool>& stop) {
    const auto wide = windows::widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
        return "only HTTPS addresses are allowed";
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    // The query string follows the path directly in the cracked URL.
    const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength + parts.dwExtraInfoLength);
    const auto agent = L"BarTab/" + windows::widen(current_version().text());
    // Follows the system's proxy settings, as a browser would.
    Internet session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value)
        return network_error(GetLastError());
    // Bounded, so stopping mid-download never waits long on a stalled read.
    WinHttpSetTimeouts(session.value, 15000, 15000, 15000, 15000);
    Internet connection(WinHttpConnect(session.value, host.c_str(), parts.nPort, 0));
    if (!connection.value)
        return network_error(GetLastError());
    // Redirects are followed, but never from HTTPS down to HTTP (WinHTTP's default).
    Internet request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.value)
        return network_error(GetLastError());
    if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr))
        return network_error(GetLastError());
    DWORD status{}, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
        return network_error(GetLastError());
    if (status != 200)
        return "the server answered HTTP " + std::to_string(status);
    auto partial = destination;
    partial += L".part";
    std::string error;
    {
        std::ofstream file(partial, std::ios::binary | std::ios::trunc);
        if (!file)
            return "could not write " + windows::utf8(partial.wstring());
        std::uint64_t total = 0;
        char buffer[64 * 1024];
        for (;;) {
            if (stop) {
                error = "stopped";
                break;
            }
            DWORD read{};
            if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &read)) {
                error = network_error(GetLastError());
                break;
            }
            if (read == 0)
                break;
            total += read;
            if (total > 64 * 1024 * 1024) {
                error = "the download was larger than expected";
                break;
            }
            file.write(buffer, read);
        }
        file.close();
        if (error.empty() && !file.good())
            error = "could not write " + windows::utf8(partial.wstring());
    }
    if (error.empty() &&
        !MoveFileExW(partial.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        error = "could not write " + windows::utf8(destination.wstring());
    if (!error.empty())
        DeleteFileW(partial.c_str());
    return error;
}
} // namespace usage::host
