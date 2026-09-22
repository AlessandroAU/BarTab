#include "core/settings_edit.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        usage::SettingsEdit edit;
        usage::Preferences saved;
        saved.codex_interval = 45;
        saved.appearance.font = 2;
        check(!edit.cancel(), "Closing without an edit must do nothing");
        edit.begin(saved);
        auto draft = saved;
        draft.appearance.text_percent = 999;
        draft.codex_enabled = false;
        draft.claude_interval = 1;
        auto current = edit.preview(draft);
        check(current.appearance.text_percent == 300 && current.claude_interval == 15,
              "Preview normalizes appearance and provider settings");
        edit.begin(current);
        check(edit.cancel() == saved, "Repeated opening must retain the original rollback snapshot");
        check(!edit.cancel(), "Cancel consumes the rollback snapshot");
        edit.begin(saved);
        current = edit.preview(draft);
        edit.commit();
        check(!edit.cancel(), "Closing after a successful save must not roll back");
        edit.begin(current);
        draft.appearance.text_percent = 100;
        edit.preview(draft);
        check(edit.cancel() == current, "A later edit rolls back to the newly saved preferences");
        std::cout << "Settings preview, commit and cancel passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
