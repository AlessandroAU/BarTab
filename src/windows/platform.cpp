#include "windows/platform.hpp"
#include <windows.h>
#include <dwmapi.h>
#include <filesystem>

namespace usage::host {
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
bool animations_enabled() {
    BOOL enabled = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0))
        return true;
    return enabled != FALSE;
}
Color system_accent() {
    DWORD value{};
    BOOL opaque{};
    if (SUCCEEDED(DwmGetColorizationColor(&value, &opaque)))
        return {static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 8),
                static_cast<std::uint8_t>(value)};
    return {0, 120, 212};
}
void background_thread() {
    THREAD_POWER_THROTTLING_STATE state{THREAD_POWER_THROTTLING_CURRENT_VERSION,
                                        THREAD_POWER_THROTTLING_EXECUTION_SPEED,
                                        THREAD_POWER_THROTTLING_EXECUTION_SPEED};
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state));
}
FontFile ui_font(bool bold) {
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
    return {std::move(bytes), 0};
}
std::filesystem::path executable_path() {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return std::filesystem::path(std::wstring(path, length));
}
std::filesystem::path executable_directory() {
    return executable_path().parent_path();
}
// Settings and the log both live in %LOCALAPPDATA%\BarTab.
std::filesystem::path config_directory() {
    wchar_t local[32768]{};
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    if (!length || length >= std::size(local))
        return {};
    const auto directory = std::filesystem::path(local) / L"BarTab";
    // The app was called UsageTracker; its folder moves over once, settings intact.
    static const bool adopted = [&] {
        std::error_code error;
        const auto previous = directory.parent_path() / L"UsageTracker";
        if (!std::filesystem::exists(directory, error) && std::filesystem::is_directory(previous, error))
            std::filesystem::rename(previous, directory, error);
        return true;
    }();
    (void)adopted;
    return directory;
}
std::filesystem::path log_path() {
    const auto directory = config_directory();
    return directory.empty() ? directory : directory / L"bartab.log";
}
} // namespace usage::host

namespace usage::windows {
std::string utf8(const std::wstring& value) {
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr,
                                          0, nullptr, nullptr);
    std::string text(static_cast<std::size_t>(count > 0 ? count : 0), '\0');
    if (count > 0)
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), text.data(), count,
                            nullptr, nullptr);
    return text;
}
std::wstring widen(const std::string& utf8) {
    const int count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring text(static_cast<std::size_t>(count > 0 ? count : 0), L'\0');
    if (count > 0)
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), text.data(), count);
    return text;
}
void log(const std::wstring& message) {
    host::log(utf8(message));
}
} // namespace usage::windows
