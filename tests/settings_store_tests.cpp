#include "windows/settings_store.hpp"
#include "core/settings_edit.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct TemporaryDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 (L"usage-settings-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                  std::to_wstring(GetTickCount64()));
    TemporaryDirectory() {
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
struct FileLock {
    HANDLE handle;
    ~FileLock() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
};
} // namespace
int main() {
    try {
        TemporaryDirectory temporary;
        const auto path = temporary.path / L"nested/settings.ini";
        using namespace usage;
        using namespace usage::windows;
        check(read_settings(path) == Preferences{}, "Missing settings use defaults");
        check(read_settings({}) == Preferences{}, "Empty load path uses defaults");
        check(!write_settings({}, {}), "Empty save path fails");
        Preferences saved;
        saved.appearance = {230, 120, 270, 73, 350, 5, 4, 37, false, false};
        saved.codex_enabled = false;
        saved.claude_enabled = false;
        saved.codex_interval = 45;
        saved.claude_interval = 120;
        check(write_settings(path, saved), "Save creates parent directories");
        check(read_settings(path) == saved,
              "Every preference survives a round trip, including custom intervals");
        auto replacement = saved;
        replacement.appearance.text_percent = 999;
        replacement.claude_interval = 1;
        check(write_settings(path, replacement), "Save replaces an existing file");
        replacement.normalize();
        check(read_settings(path) == replacement, "Saved values are normalized");
        check(!std::filesystem::exists(path.wstring() + L".tmp"), "Successful save leaves no temporary file");
        {
            SettingsEdit edit;
            edit.begin(replacement);
            auto draft = replacement;
            draft.codex_enabled = true;
            FileLock lock{
                CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)};
            check(lock.handle != INVALID_HANDLE_VALUE, "Lock the destination for failure testing");
            check(!write_settings(path, draft), "A locked destination reports save failure");
            check(read_settings(path) == replacement, "Failed save preserves the previous file");
            check(edit.cancel() == replacement, "Failed save leaves the edit cancellable");
            check(!std::filesystem::exists(path.wstring() + L".tmp"),
                  "Failed save cleans its temporary file");
        }
        const auto invalid = temporary.path / L"invalid.ini";
        {
            std::ofstream file(invalid);
            file << "[Appearance]\nTextPercent=999\nWidgetWidth=1\nFont=99\nHoverOpacity=oops\n"
                    "[Providers]\nCodexInterval=1\nClaudeInterval=9999\n";
        }
        const auto loaded = read_settings(invalid);
        check(loaded.appearance.text_percent == 300 && loaded.appearance.widget_width == 100 &&
                  loaded.codex_interval == 15 && loaded.claude_interval == 900,
              "Out-of-range file values are clamped");
        check(loaded.appearance.hover_text_percent == 300,
              "Files saved before the hover text size reuse the taskbar text size");
        check(loaded.appearance.hover_opacity >= 50 && loaded.appearance.hover_opacity <= 100,
              "Malformed numbers produce a valid preference");
        check(loaded.codex_enabled,
              "Missing keys retain defaults");
        std::cout << "Settings persistence and failure handling passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
