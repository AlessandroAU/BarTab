#include "windows/mock_panel.hpp"
#include <commctrl.h>
#include <cwchar>
#include <stdexcept>
#include <string>

// Themed controls for the panel; only the debug executable links this file.
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace usage::windows {
namespace {
constexpr wchar_t panel_class[] = L"UsageTracker.MockPanel.Cpp";
constexpr int preset_id = 900, reset_button_id = 910;
constexpr int state_field = 1, plan_field = 2, model_name_field = 3, credits_field = 4, earned_field = 5;
constexpr int present_field = 10, used_field = 11, resets_field = 12;
constexpr MockState states[] = {MockState::Ready, MockState::Missing, MockState::LoginError, MockState::Connecting,
                                MockState::Malformed};
// Layout, in DIPs.
constexpr int margin = 16, column_width = 330, column_gap = 20;
constexpr int client_width = margin * 2 + column_width * 2 + column_gap, client_height = 530;
constexpr int allowance_top = 144, allowance_step = 86;

int control_id(int provider, int field, int allowance = 0) {
    return 1000 + provider * 100 + field + allowance * 10;
}
std::wstring widen(const std::string& value) {
    return {value.begin(), value.end()};
}
std::string narrow(const std::wstring& value) {
    std::string result;
    for (const auto character : value)
        result += character < 0x80 ? static_cast<char>(character) : '?';
    return result;
}
std::wstring window_text(HWND window) {
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    text.resize(static_cast<std::size_t>(GetWindowTextW(window, text.data(), static_cast<int>(text.size()))));
    return text;
}
int window_number(HWND window, int fallback) {
    const auto text = window_text(window);
    return text.empty() ? fallback : static_cast<int>(std::wcstol(text.c_str(), nullptr, 10));
}
void set_text(HWND window, const std::wstring& text) {
    if (window && window_text(window) != text)
        SetWindowTextW(window, text.c_str());
}
std::wstring duration(int minutes) {
    const int days = minutes / 1440, hours = minutes / 60 % 24, rest = minutes % 60;
    std::wstring result = L"min";
    if (minutes >= 60) {
        result += L" (";
        if (days)
            result += std::to_wstring(days) + L"d ";
        if (hours || !days)
            result += std::to_wstring(hours) + L"h ";
        if (rest && !days)
            result += std::to_wstring(rest) + L"m ";
        result.back() = L')';
    }
    return result;
}
} // namespace

MockPanel::MockPanel(std::shared_ptr<host::MockProviders> providers, std::function<void()> changed,
                     std::function<void()> celebrate)
    : providers_(std::move(providers)), changed_(std::move(changed)), celebrate_(std::move(celebrate)) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = proc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    cls.lpszClassName = panel_class;
    if (!RegisterClassExW(&cls))
        throw std::runtime_error("Could not register the mock panel class");
    window_ = CreateWindowExW(0, panel_class, L"UsageTracker - Mock providers",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                              CW_USEDEFAULT, 100, 100, nullptr, nullptr, cls.hInstance, this);
    if (!window_)
        throw std::runtime_error("Could not create the mock panel");
    dpi_ = GetDpiForWindow(window_);
    build();
    layout();
    show_scenario(providers_->scenario());
}

MockPanel::~MockPanel() {
    if (window_)
        DestroyWindow(window_);
    if (font_)
        DeleteObject(font_);
}

void MockPanel::show() {
    ShowWindow(window_, IsIconic(window_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(window_);
}

LRESULT CALLBACK MockPanel::proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCCREATE) {
        auto* created = reinterpret_cast<CREATESTRUCTW*>(l);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created->lpCreateParams));
    }
    auto* panel = reinterpret_cast<MockPanel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!panel)
        return DefWindowProcW(window, message, w, l);
    switch (message) {
    case WM_COMMAND: {
        const int id = LOWORD(w), code = HIWORD(w);
        if ((id == reset_button_id || id == reset_button_id + 1) && code == BN_CLICKED) {
            panel->simulate_reset(id - reset_button_id);
        } else if (id == preset_id && code == CBN_SELCHANGE) {
            const auto index = SendMessageW(panel->preset_, CB_GETCURSEL, 0, 0);
            if (index >= 0 && static_cast<std::size_t>(index) < mock_presets().size()) {
                panel->show_scenario(mock_presets()[static_cast<std::size_t>(index)].scenario);
                panel->apply(true);
            }
        } else if (id >= 1000 && (code == CBN_SELCHANGE || code == EN_CHANGE || code == BN_CLICKED))
            panel->apply(false);
        return 0;
    }
    case WM_HSCROLL:
        if (l)
            panel->apply(false);
        return 0;
    case WM_DPICHANGED: {
        panel->dpi_ = HIWORD(w);
        const auto* suggested = reinterpret_cast<const RECT*>(l);
        SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        panel->layout();
        return 0;
    }
    case WM_CLOSE:
        // The tray menu's "Mock providers..." brings it back.
        ShowWindow(window, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

HWND MockPanel::add(const wchar_t* type, const wchar_t* text, DWORD style, RECT bounds, int id) {
    const bool edit = std::wcscmp(type, WC_EDITW) == 0;
    HWND control = CreateWindowExW(edit ? WS_EX_CLIENTEDGE : 0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0,
                                   0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    if (!control)
        throw std::runtime_error("Could not create a mock panel control");
    placed_.push_back({control, bounds});
    return control;
}

void MockPanel::build() {
    add(WC_STATICW, L"Scenario", SS_LEFT, {margin, 19, margin + 70, 39});
    preset_ = add(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                  {margin + 80, 16, client_width - margin, 316}, preset_id);
    for (const auto& preset : mock_presets())
        SendMessageW(preset_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(preset.name).c_str()));
    SendMessageW(preset_, CB_SETCURSEL, 0, 0);
    build_provider(0, margin);
    build_provider(1, margin + column_width + column_gap);
    add(WC_STATICW,
        L"Changes apply immediately. Reset times count from each reading, so a refresh moves them forward.",
        SS_LEFT, {margin, client_height - 34, client_width - margin, client_height - 14});
}

void MockPanel::build_provider(int index, int left) {
    auto& controls = controls_[index];
    const bool claude = index == 1;
    const int right = left + column_width;
    add(WC_BUTTONW, claude ? L"Claude" : L"Codex", BS_GROUPBOX, {left, 52, right, client_height - 46});
    add(WC_BUTTONW, L"Simulate reset", BS_PUSHBUTTON | WS_TABSTOP,
        {left + 12, client_height - 88, left + 140, client_height - 60}, reset_button_id + index);
    add(WC_STATICW, L"State", SS_LEFT, {left + 12, 83, left + 88, 103});
    controls.state = add(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                         {left + 90, 80, right - 12, 260}, control_id(index, state_field));
    for (const auto state : states)
        SendMessageW(controls.state, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(mock_state_name(state)).c_str()));
    add(WC_STATICW, L"Plan", SS_LEFT, {left + 12, 113, left + 88, 133});
    controls.plan = add(WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, {left + 90, 110, right - 12, 133},
                        control_id(index, plan_field));
    const wchar_t* names[] = {L"5 hour", L"Weekly", L"Model weekly"};
    const int rows = claude ? 3 : 2;
    for (int a = 0; a < rows; ++a) {
        auto& allowance = controls.allowances[a];
        const int y = allowance_top + a * allowance_step;
        allowance.present = add(WC_BUTTONW, names[a], BS_AUTOCHECKBOX | WS_TABSTOP,
                                {left + 12, y, left + 180, y + 20}, control_id(index, present_field, a));
        allowance.used_label = add(WC_STATICW, L"", SS_RIGHT, {right - 110, y + 2, right - 12, y + 20});
        allowance.used = add(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP,
                             {left + 6, y + 22, right - 6, y + 50}, control_id(index, used_field, a));
        SendMessageW(allowance.used, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(allowance.used, TBM_SETTICFREQ, 10, 0);
        SendMessageW(allowance.used, TBM_SETPAGESIZE, 0, 10);
        add(WC_STATICW, L"Resets in", SS_LEFT, {left + 12, y + 57, left + 76, y + 77});
        allowance.resets = add(WC_EDITW, L"", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
                               {left + 78, y + 54, left + 146, y + 77}, control_id(index, resets_field, a));
        allowance.resets_label = add(WC_STATICW, L"", SS_LEFT, {left + 152, y + 57, right - 12, y + 77});
    }
    const int y = allowance_top + rows * allowance_step;
    if (claude) {
        add(WC_STATICW, L"Model", SS_LEFT, {left + 12, y + 3, left + 88, y + 23});
        controls.model_name = add(WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, {left + 90, y, right - 12, y + 23},
                                  control_id(index, model_name_field));
        return;
    }
    add(WC_STATICW, L"Credits", SS_LEFT, {left + 12, y + 3, left + 88, y + 23});
    controls.credits = add(WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, {left + 90, y, right - 12, y + 23},
                           control_id(index, credits_field));
    add(WC_STATICW, L"Earned resets", SS_LEFT, {left + 12, y + 33, left + 100, y + 53});
    controls.earned_resets = add(WC_EDITW, L"", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
                                 {left + 102, y + 30, left + 170, y + 53}, control_id(index, earned_field));
    add(WC_STATICW, L"blank leaves them out", SS_LEFT, {left + 176, y + 33, right - 12, y + 53});
}

// Positions every control for the current DPI and gives them a matching font.
void MockPanel::layout() {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi_)) {
        if (font_)
            DeleteObject(font_);
        font_ = CreateFontIndirectW(&metrics.lfMessageFont);
    }
    const auto scale = [this](LONG value) { return MulDiv(value, static_cast<int>(dpi_), 96); };
    for (const auto& control : placed_) {
        SetWindowPos(control.window, nullptr, scale(control.bounds.left), scale(control.bounds.top),
                     scale(control.bounds.right - control.bounds.left),
                     scale(control.bounds.bottom - control.bounds.top), SWP_NOZORDER | SWP_NOACTIVATE);
        SendMessageW(control.window, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    RECT frame{0, 0, scale(client_width), scale(client_height)};
    const auto style = static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE));
    AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpi_);
    SetWindowPos(window_, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void MockPanel::show_scenario(const MockScenario& scenario) {
    syncing_ = true;
    for (int index = 0; index < 2; ++index) {
        const auto& provider = index ? scenario.claude : scenario.codex;
        auto& controls = controls_[index];
        for (int s = 0; s < static_cast<int>(std::size(states)); ++s)
            if (states[s] == provider.state)
                SendMessageW(controls.state, CB_SETCURSEL, static_cast<WPARAM>(s), 0);
        set_text(controls.plan, widen(provider.plan));
        set_text(controls.model_name, widen(provider.model_name));
        set_text(controls.credits, widen(provider.credit_balance));
        set_text(controls.earned_resets,
                 provider.available_resets >= 0 ? std::to_wstring(provider.available_resets) : L"");
        const MockAllowance* allowances[] = {&provider.session, &provider.weekly, &provider.model};
        for (int a = 0; a < 3; ++a) {
            auto& allowance = controls.allowances[a];
            if (!allowance.present)
                continue;
            SendMessageW(allowance.present, BM_SETCHECK, allowances[a]->present ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(allowance.used, TBM_SETPOS, TRUE, allowances[a]->used_percent);
            set_text(allowance.resets, std::to_wstring(allowances[a]->resets_in_minutes));
        }
    }
    syncing_ = false;
    update_labels();
}

MockScenario MockPanel::read_scenario() const {
    auto scenario = providers_->scenario();
    for (int index = 0; index < 2; ++index) {
        auto& provider = index ? scenario.claude : scenario.codex;
        const auto& controls = controls_[index];
        const auto state = SendMessageW(controls.state, CB_GETCURSEL, 0, 0);
        if (state >= 0 && state < static_cast<LRESULT>(std::size(states)))
            provider.state = states[state];
        provider.plan = narrow(window_text(controls.plan));
        if (controls.model_name)
            provider.model_name = narrow(window_text(controls.model_name));
        if (controls.credits) {
            provider.credit_balance = narrow(window_text(controls.credits));
            provider.available_resets = window_number(controls.earned_resets, -1);
        }
        MockAllowance* allowances[] = {&provider.session, &provider.weekly, &provider.model};
        for (int a = 0; a < 3; ++a) {
            const auto& allowance = controls.allowances[a];
            if (!allowance.present)
                continue;
            allowances[a]->present = SendMessageW(allowance.present, BM_GETCHECK, 0, 0) == BST_CHECKED;
            allowances[a]->used_percent = static_cast<int>(SendMessageW(allowance.used, TBM_GETPOS, 0, 0));
            allowances[a]->resets_in_minutes = window_number(allowance.resets, 0);
        }
    }
    return scenario;
}

// Refreshes the read-outs beside the sliders and greys out what a provider's
// state or a missing window makes irrelevant.
void MockPanel::update_labels() {
    const auto scenario = read_scenario();
    for (int index = 0; index < 2; ++index) {
        const auto& provider = index ? scenario.claude : scenario.codex;
        const auto& controls = controls_[index];
        const bool answers = provider.state == MockState::Ready;
        EnableWindow(controls.plan, answers);
        if (controls.model_name)
            EnableWindow(controls.model_name, answers && provider.model.present);
        if (controls.credits) {
            EnableWindow(controls.credits, answers);
            EnableWindow(controls.earned_resets, answers);
        }
        const MockAllowance* allowances[] = {&provider.session, &provider.weekly, &provider.model};
        for (int a = 0; a < 3; ++a) {
            const auto& allowance = controls.allowances[a];
            if (!allowance.present)
                continue;
            const bool active = answers && allowances[a]->present;
            EnableWindow(allowance.present, answers);
            for (HWND control : {allowance.used, allowance.used_label, allowance.resets, allowance.resets_label})
                EnableWindow(control, active);
            set_text(allowance.used_label, std::to_wstring(allowances[a]->used_percent) + L"% used");
            set_text(allowance.resets_label, duration(allowances[a]->resets_in_minutes));
        }
    }
}

void MockPanel::apply(bool from_preset) {
    if (syncing_)
        return;
    // A hand edit no longer matches the preset it started from.
    if (!from_preset)
        SendMessageW(preset_, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    update_labels();
    providers_->set(read_scenario());
    changed_();
}
// A window with usage to give back resets through the app's own detection on
// the next reading; one already empty would not, so the button celebrates
// directly and can be pressed again and again.
void MockPanel::simulate_reset(int index) {
    const auto scenario = providers_->scenario();
    const auto& provider = index ? scenario.claude : scenario.codex;
    bool detected = false;
    if (provider.state == MockState::Ready)
        for (const auto* allowance : {&provider.session, &provider.weekly, &provider.model})
            detected = detected || (allowance->present && allowance->used_percent > 0);
    syncing_ = true;
    for (int a = 0; a < 3; ++a) {
        const auto& allowance = controls_[index].allowances[a];
        if (!allowance.present)
            continue;
        SendMessageW(allowance.used, TBM_SETPOS, TRUE, 0);
        set_text(allowance.resets, a == 0 ? L"300" : L"10080");
    }
    syncing_ = false;
    apply(false);
    if (!detected)
        celebrate_();
}
} // namespace usage::windows
