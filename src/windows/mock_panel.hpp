#pragma once
#include "host/providers.hpp"
#include "host/simulated_update.hpp"
#include <windows.h>
#include <functional>
#include <vector>

namespace usage::windows {
// The debug build's control window: picks a preset scenario or edits each
// mocked provider's state and allowances, applying every change immediately,
// and offers a simulated release to update to.
class MockPanel {
  public:
    // `celebrate` plays the reset animation when a simulated reset would not
    // trigger it on its own. `simulate_update` checks a fake release server
    // that answers with the chosen release.
    MockPanel(std::shared_ptr<host::MockProviders> providers, std::function<void()> changed,
              std::function<void()> celebrate, std::function<void(host::SimulatedRelease)> simulate_update);
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
        HWND state{}, plan{}, model_name{}, session_shape{}, credits{}, earned_resets{};
        AllowanceControls allowances[3];
    };
    std::shared_ptr<host::MockProviders> providers_;
    std::function<void()> changed_, celebrate_;
    std::function<void(host::SimulatedRelease)> simulate_update_;
    HWND window_{}, preset_{}, release_{};
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
    // Rolls every window of one provider over: nothing used, a full period to go.
    void simulate_reset(int index);
};
} // namespace usage::windows
