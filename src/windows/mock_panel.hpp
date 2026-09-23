#pragma once
#include "windows/providers.hpp"
#include <windows.h>
#include <functional>
#include <vector>

namespace usage::windows {
// The debug build's control window: picks a preset scenario or edits each
// mocked provider's state and allowances, applying every change immediately.
class MockPanel {
  public:
    MockPanel(std::shared_ptr<MockProviders> providers, std::function<void()> changed);
    ~MockPanel();
    MockPanel(const MockPanel&) = delete;
    MockPanel& operator=(const MockPanel&) = delete;
    void show();

  private:
    struct Placed {
        HWND window;
        RECT bounds; // In DIPs.
    };
    // One allowance row: whether the window is reported, how much of it is
    // used, and how long until it resets.
    struct AllowanceControls {
        HWND present{}, used{}, used_label{}, resets{}, resets_label{};
    };
    struct ProviderControls {
        HWND state{}, plan{}, model_name{}, credits{}, earned_resets{};
        AllowanceControls allowances[3];
    };
    std::shared_ptr<MockProviders> providers_;
    std::function<void()> changed_;
    HWND window_{}, preset_{};
    HFONT font_{};
    UINT dpi_{96};
    bool syncing_{};
    std::vector<Placed> placed_;
    ProviderControls controls_[2];

    static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l);
    HWND add(const wchar_t* type, const wchar_t* text, DWORD style, RECT bounds, int id = 0);
    void build();
    void build_provider(int index, int left);
    void layout();
    void show_scenario(const MockScenario& scenario);
    MockScenario read_scenario() const;
    void update_labels();
    void apply(bool from_preset);
};
} // namespace usage::windows
