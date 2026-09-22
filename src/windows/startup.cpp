#include "windows/startup.hpp"
#include <string>

namespace usage::windows {
namespace {
constexpr wchar_t key[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t name[]=L"UsageTracker";
}
StartupState startup_state() {
    DWORD bytes{};
    const auto status=RegGetValueW(HKEY_CURRENT_USER,key,name,RRF_RT_REG_SZ,nullptr,nullptr,&bytes);
    if (status==ERROR_FILE_NOT_FOUND || status==ERROR_PATH_NOT_FOUND) return {};
    return {status==ERROR_SUCCESS && bytes>sizeof(wchar_t),status};
}
LSTATUS set_startup(bool enabled) {
    if (!enabled) {
        const auto status=RegDeleteKeyValueW(HKEY_CURRENT_USER,key,name);
        return status==ERROR_FILE_NOT_FOUND || status==ERROR_PATH_NOT_FOUND ? ERROR_SUCCESS : status;
    }
    wchar_t path[32768]{};
    const DWORD length=GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));
    if (!length) return static_cast<LSTATUS>(GetLastError());
    if (length>=std::size(path)) return ERROR_INSUFFICIENT_BUFFER;
    const std::wstring command=L"\""+std::wstring(path,length)+L"\"";
    // The Run key has a 260-character command limit, including the quotes.
    if (command.size()>260) return ERROR_FILENAME_EXCED_RANGE;
    HKEY opened{};
    const auto status=RegCreateKeyExW(HKEY_CURRENT_USER,key,0,nullptr,0,KEY_SET_VALUE,nullptr,&opened,nullptr);
    if (status!=ERROR_SUCCESS) return status;
    const auto written=RegSetValueExW(opened,name,0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
    RegCloseKey(opened);
    return written;
}
}
