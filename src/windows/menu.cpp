#include "windows/app.hpp"
#include "host/startup.hpp"
#include "windows/platform.hpp"

namespace usage::windows {

// Opens the native context menu at the pointer, from the widget or the tray icon.
void App::show_menu() {
    using Choice = ui::Frame::MenuChoice;
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    const auto startup = host::startup_state();
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(Choice::Settings), L"Settings");
    if (open_mock_panel_)
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(Choice::Debug), L"Mock providers...");
    AppendMenuW(menu,
                MF_STRING | (startup.enabled ? MF_CHECKED : MF_UNCHECKED) |
                    (startup.error.empty() ? 0 : MF_GRAYED),
                static_cast<UINT_PTR>(Choice::Startup), L"Start at boot");
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(Choice::Quit), L"Quit");
    // The card, even a pinned settings preview, stays hidden while the menu is open.
    menu_open_ = true;
    hide_hover(true);
    POINT position{};
    GetCursorPos(&position);
    // Foreground first, and a message after, or the menu does not close on an outside click.
    SetForegroundWindow(controller_);
    const auto selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, position.x, position.y, 0,
                                         controller_, nullptr);
    DestroyMenu(menu);
    PostMessageW(controller_, WM_NULL, 0, 0);
    menu_open_ = false;
    if (hover_pinned_)
        show_hover();
    choose_menu(static_cast<Choice>(selected));
}

void App::choose_menu(ui::Frame::MenuChoice choice) {
    using Choice = ui::Frame::MenuChoice;
    switch (choice) {
    case Choice::Settings:
        open_details(true);
        break;
    case Choice::Debug:
        if (open_mock_panel_)
            open_mock_panel_();
        break;
    case Choice::Startup: {
        const auto error = host::set_startup(!host::startup_state().enabled);
        if (!error.empty()) {
            const auto message = L"Could not change Start at boot.\n\n" + widen(error);
            MessageBoxW(controller_, message.c_str(), L"BarTab", MB_OK | MB_ICONERROR);
        }
        break;
    }
    case Choice::Quit:
        PostQuitMessage(0);
        break;
    case Choice::None:
        break;
    }
}

} // namespace usage::windows
