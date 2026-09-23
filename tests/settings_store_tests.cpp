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
        saved.appearance = {180, 120, 270, 73, 5, 37, false, false};
        saved.appearance.twelve_hour_time = true;
        saved.appearance.bold_taskbar = true;
        saved.appearance.bold_settings = true;
        saved.appearance.all_taskbars = true;
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
            file << "[Appearance]\nTextPercent=999\nWidgetWidth=1\nFont=99\nHoverDelay=900\nHoverOpacity=oops\n"
                    "[Providers]\nCodexInterval=1\nClaudeInterval=9999\n";
        }
        const auto loaded = read_settings(invalid);
        check(loaded.appearance.text_percent == 200 && loaded.appearance.widget_width == 100 &&
                  loaded.codex_interval == 15 && loaded.claude_interval == 900,
              "Out-of-range file values are clamped");
        check(loaded.appearance.hover_text_percent == 200,
              "Files saved before the hover text size reuse the taskbar text size");
        check(loaded.appearance.hover_opacity >= 50 && loaded.appearance.hover_opacity <= 100,
              "Malformed numbers produce a valid preference");
        check(loaded.codex_enabled,
              "Missing keys retain defaults");
        const auto legacy = temporary.path / L"legacy.ini";
        {
            std::ofstream file(legacy);
            file << "[Appearance]\nBoldText=1\nBoldHover=0\n";
        }
        const auto migrated = read_settings(legacy).appearance;
        check(migrated.bold_taskbar && migrated.bold_settings,
              "The old shared BoldText switch seeds every surface's bold setting");
        check(!migrated.bold_hover, "A per-surface bold key overrides the old shared switch");
        const auto absolute = temporary.path / L"absolute.ini";
        {
            std::ofstream file(absolute);
            file << "[Appearance]\nTextPercent=150\nHoverTextPercent=130\n";
        }
        auto rescaled = read_settings(absolute).appearance;
        check(rescaled.text_percent == 100 && rescaled.hover_text_percent == 100,
              "Old absolute text sizes at the new base scales load as 100 percent");
        check(rescaled.bold_taskbar && rescaled.bold_hover && !rescaled.bold_settings,
              "Missing bold keys take the defaults");
        {
            std::ofstream file(absolute);
            file << "[Appearance]\nTextPercent=100\nTextSize=120\n";
        }
        rescaled = read_settings(absolute).appearance;
        check(rescaled.text_percent == 120 && rescaled.hover_text_percent == 75,
              "New text size keys win; old ones round to the 5 percent step");
        std::cout << "Settings persistence and failure handling passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
