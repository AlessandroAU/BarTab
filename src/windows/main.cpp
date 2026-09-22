#include "windows/app.hpp"
#include "windows/platform.hpp"
#include <exception>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr,FALSE,L"Local\\UsageTracker.Prototype");
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mutex); return 2; }
    int argument_count{};
    auto arguments = CommandLineToArgvW(GetCommandLineW(),&argument_count);
    bool smoke = false;
    bool live_test = false;
    for (int i = 1; i < argument_count; ++i) if (std::wstring(arguments[i]) == L"--smoke-test") smoke = true;
    for (int i = 1; i < argument_count; ++i) if (std::wstring(arguments[i]) == L"--live-smoke-test") live_test = true;
    LocalFree(arguments);
    int result = 1;
    try { usage::windows::App app(smoke,live_test); result = app.run(); }
    catch (const std::exception& error) {
        const std::string text = error.what();
        usage::windows::log(L"Fatal error: " + std::wstring(text.begin(),text.end()));
    }
    CloseHandle(mutex);
    return result;
}
