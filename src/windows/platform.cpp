#include "windows/platform.hpp"
#include <windows.h>
#include <fstream>

namespace usage::windows {
bool system_light_theme() {
    DWORD light{}, bytes=sizeof(light);
    if (RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes)==ERROR_SUCCESS) return light!=0;
    const auto background=GetSysColor(COLOR_3DFACE);
    return (GetRValue(background)*299+GetGValue(background)*587+GetBValue(background)*114)>128000;
}
std::filesystem::path executable_directory() {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return std::filesystem::path(path).parent_path();
}
void log(const std::wstring& message) {
    wchar_t local[32768]{};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)))) return;
    const auto directory = std::filesystem::path(local) / L"UsageTracker";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::ofstream stream(directory / L"prototype.log", std::ios::app);
    const int count = WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), text.data(), count, nullptr, nullptr);
    stream << now.wYear << '-' << now.wMonth << '-' << now.wDay << ' ' << now.wHour << ':' << now.wMinute << ':' << now.wSecond
           << " [C++] " << text << '\n';
}

} // namespace usage::windows
