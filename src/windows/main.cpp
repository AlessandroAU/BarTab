#include "windows/app.hpp"
#include "windows/platform.hpp"
#include <exception>
#ifdef USAGETRACKER_MOCK
#include "windows/mock_panel.hpp"
// The debug build runs beside an installed copy, so it takes its own mutex.
constexpr wchar_t instance_mutex[] = L"Local\\UsageTracker.Debug";
#else
constexpr wchar_t instance_mutex[] = L"Local\\UsageTracker.Prototype";
#endif

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, instance_mutex);
    if (!mutex)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 2;
    }
    int argument_count{};
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    bool smoke = false;
    bool live_test = false;
    for (int i = 1; i < argument_count; ++i)
        if (std::wstring(arguments[i]) == L"--smoke-test")
            smoke = true;
    for (int i = 1; i < argument_count; ++i)
        if (std::wstring(arguments[i]) == L"--live-smoke-test")
            live_test = true;
    LocalFree(arguments);
    int result = 1;
    try {
#ifdef USAGETRACKER_MOCK
        using namespace usage::windows;
        // Smoke tests keep their fixed demo data; the panel drives everything else.
        auto mock = smoke ? nullptr : std::make_shared<MockProviders>(usage::mock_presets().front().scenario);
        App app(smoke, live_test, mock);
        std::unique_ptr<MockPanel> panel;
        if (mock) {
            panel = std::make_unique<MockPanel>(mock, [&app] { app.mock_changed(); }, [&app] { app.celebrate(); });
            app.set_mock_panel([&panel] { panel->show(); });
            panel->show();
        }
        result = app.run();
#else
        usage::windows::App app(smoke, live_test);
        result = app.run();
#endif
    } catch (const std::exception& error) {
        const std::string text = error.what();
        usage::windows::log(L"Fatal error: " + std::wstring(text.begin(), text.end()));
    }
    CloseHandle(mutex);
    return result;
}
