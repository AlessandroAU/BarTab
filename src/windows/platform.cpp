#include "windows/platform.hpp"
#include <windows.h>
#include <dwmapi.h>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace usage::windows {
bool system_light_theme() {
    DWORD light{}, bytes = sizeof(light);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes) == ERROR_SUCCESS)
        return light != 0;
    const auto background = GetSysColor(COLOR_3DFACE);
    return (GetRValue(background) * 299 + GetGValue(background) * 587 + GetBValue(background) * 114) > 128000;
}
bool apps_light_theme() {
    DWORD light{}, bytes = sizeof(light);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes) == ERROR_SUCCESS)
        return light != 0;
    return system_light_theme();
}
bool client_animations_enabled() {
    BOOL enabled = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0))
        return true;
    return enabled != FALSE;
}
Color windows_accent() {
    DWORD value{};
    BOOL opaque{};
    if (SUCCEEDED(DwmGetColorizationColor(&value, &opaque)))
        return {static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 8),
                static_cast<std::uint8_t>(value)};
    return {0, 120, 212};
}
std::vector<unsigned char> windows_ui_font(bool bold) {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
        return {};
    if (bold) {
        // Ask the font mapper for another weight in the same family. GetFontData
        // below returns whatever physical face it lands on, so a family shipping
        // a real bold (Segoe UI does) yields that face's own outlines. One that
        // does not falls back to its regular file, because GDI would only
        // embolden such a family while rasterizing - and nothing here rasterizes
        // through GDI, so the caller simply sees no change.
        metrics.lfMessageFont.lfWeight = FW_BOLD;
    }
    const auto font = CreateFontIndirectW(&metrics.lfMessageFont);
    if (!font) return {};
    const auto dc = CreateCompatibleDC(nullptr);
    if (!dc) {
        DeleteObject(font);
        return {};
    }
    const auto previous = SelectObject(dc, font);
    const auto size = GetFontData(dc, 0, 0, nullptr, 0);
    std::vector<unsigned char> bytes;
    if (size != GDI_ERROR && size > 0 && size <= 32 * 1024 * 1024) {
        bytes.resize(size);
        if (GetFontData(dc, 0, 0, bytes.data(), size) == GDI_ERROR)
            bytes.clear();
    }
    SelectObject(dc, previous);
    DeleteDC(dc);
    DeleteObject(font);
    return bytes;
}
std::filesystem::path executable_directory() {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return std::filesystem::path(path).parent_path();
}
void log(const std::wstring& message) {
    wchar_t local[32768]{};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local))))
        return;
    const auto directory = std::filesystem::path(local) / L"UsageTracker";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const auto path = directory / L"prototype.log";
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
        trimmed += L".tmp";
        {
            std::ofstream kept(trimmed, std::ios::binary | std::ios::trunc);
            if (cut != std::string::npos)
                kept.write(contents.data() + cut + 1, static_cast<std::streamsize>(contents.size() - cut - 1));
        }
        if (!MoveFileExW(trimmed.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
            std::filesystem::remove(trimmed, error);
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::ofstream stream(path, std::ios::app);
    const int count = WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), text.data(), count,
                        nullptr, nullptr);
    stream << now.wYear << '-' << now.wMonth << '-' << now.wDay << ' ' << now.wHour << ':' << now.wMinute
           << ':' << now.wSecond << " [C++] " << text << '\n';
}

} // namespace usage::windows
