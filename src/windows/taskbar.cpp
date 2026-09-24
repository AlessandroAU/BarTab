#include "windows/taskbar.hpp"
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace usage::windows {
using Microsoft::WRL::ComPtr;
Rect window_rect(HWND window) {
    RECT rect{};
    if (!GetWindowRect(window, &rect))
        return {};
    return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
}

namespace {
// The primary taskbar always lists buttons once its layout is ready, so an
// empty walk means "not yet"; another monitor's may show none at all.
Snapshot read_snapshot(IUIAutomation* automation, HWND taskbar, bool buttons_expected) {
    Snapshot result;
    result.taskbar = taskbar;
    result.bounds = window_rect(result.taskbar);
    if (!result.taskbar || result.bounds.empty())
        return result;
    result.dpi = GetDpiForWindow(result.taskbar);
    if (!result.dpi)
        result.dpi = 96;
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(result.taskbar, &root)))
        return result;
    VARIANT buttonType{};
    buttonType.vt = VT_I4;
    buttonType.lVal = UIA_ButtonControlTypeId;
    ComPtr<IUIAutomationCondition> condition;
    if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, buttonType, &condition)))
        return result;
    ComPtr<IUIAutomationElementArray> buttons;
    if (FAILED(root->FindAll(TreeScope_Descendants, condition.Get(), &buttons)))
        return result;
    int count{};
    if (FAILED(buttons->get_Length(&count)))
        return result;
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> element;
        RECT rect{};
        BOOL offscreen{};
        int process{};
        if (FAILED(buttons->GetElement(i, &element)) || FAILED(element->get_CurrentProcessId(&process)) ||
            FAILED(element->get_CurrentIsOffscreen(&offscreen)) ||
            FAILED(element->get_CurrentBoundingRectangle(&rect))) {
            result.error = L"Taskbar layout changed; retrying.";
            return result;
        }
        if (static_cast<DWORD>(process) == GetCurrentProcessId() || offscreen)
            continue;
        Rect occupied{rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
        if (!occupied.empty())
            result.occupied.push_back(occupied);
    }
    // Another instance's widget, such as the debug build's beside the installed
    // app, is a plain child window that UI Automation does not list as a button.
    for (HWND other = nullptr; (other = FindWindowExW(result.taskbar, other, widget_class, nullptr));) {
        DWORD process{};
        GetWindowThreadProcessId(other, &process);
        const auto rect = window_rect(other);
        if (process != GetCurrentProcessId() && IsWindowVisible(other) && !rect.empty())
            result.occupied.push_back(rect);
    }
    if (!result.occupied.empty() || !buttons_expected)
        result.error.clear();
    return result;
}

// Whether two reads found the same layout; only then may reading slow down.
bool same_layout(const Snapshot& a, const Snapshot& b) {
    return a.taskbar == b.taskbar && a.bounds == b.bounds && a.dpi == b.dpi && a.occupied == b.occupied &&
           a.error == b.error;
}
bool same_layout(const Taskbars& a, const Taskbars& b) {
    return same_layout(a.primary, b.primary) &&
           std::equal(a.secondary.begin(), a.secondary.end(), b.secondary.begin(), b.secondary.end(),
                      [](const Snapshot& x, const Snapshot& y) { return same_layout(x, y); });
}
// Each walk is a few dozen cross-process calls into Explorer, so a layout that
// holds still is read less and less often. A poke reads after a short pause,
// which folds a burst of pokes into one read, and once more when the taskbar's
// own button animation has settled. Pokes are frequent (the shell reports
// every top-level window, most of which never reach the taskbar), so each one
// must stay cheap.
constexpr auto quickest_read = std::chrono::seconds(1);
constexpr auto poke_delay = std::chrono::milliseconds(250);
constexpr auto settled_read = std::chrono::seconds(1);
} // namespace

struct TaskbarReader::State {
    std::mutex mutex;
    std::condition_variable wake;
    bool stop{};
    bool secondary{};
    bool poked{}, paused{};
    Taskbars snapshot;
};
TaskbarReader::TaskbarReader() : state_(std::make_shared<State>()) {
    // Detached with shared ownership: a blocked third-party UIA provider must not
    // prevent Quit. The worker never references the App or posts to its HWNDs.
    std::thread([state = state_] {
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initialized))
            return;
        {
            ComPtr<IUIAutomation> automation;
            std::chrono::steady_clock::duration gap = quickest_read;
            bool settling{};
            while (true) {
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->stop)
                        break;
                }
                bool secondary{};
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    secondary = state->secondary;
                }
                Taskbars snapshot;
                if (!automation)
                    CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&automation));
                if (automation) {
                    snapshot.primary = read_snapshot(automation.Get(), FindWindowW(L"Shell_TrayWnd", nullptr), true);
                    // Windows gives every other monitor's taskbar this class.
                    for (HWND other = nullptr;
                         secondary && (other = FindWindowExW(nullptr, other, L"Shell_SecondaryTrayWnd", nullptr));)
                        if (IsWindowVisible(other))
                            snapshot.secondary.push_back(read_snapshot(automation.Get(), other, false));
                } else
                    snapshot.primary.error = L"UI Automation is unavailable; retrying.";
                const auto now = std::chrono::steady_clock::now();
                snapshot.primary.captured = now;
                for (auto& other : snapshot.secondary)
                    other.captured = now;
                std::unique_lock<std::mutex> lock(state->mutex);
                // An unreadable or moving layout keeps the quick pace.
                gap = snapshot.primary.error.empty() && same_layout(snapshot, state->snapshot)
                          ? std::min<std::chrono::steady_clock::duration>(gap * 2, taskbar_slowest_read)
                          : quickest_read;
                state->snapshot = std::move(snapshot);
                const auto wait = settling ? std::chrono::steady_clock::duration(settled_read) : gap;
                settling = false;
                const auto woken = [&] { return state->stop || state->poked; };
                if (state->paused)
                    state->wake.wait(lock, woken);
                else
                    state->wake.wait_for(lock, wait, woken);
                if (state->poked) {
                    state->wake.wait_for(lock, poke_delay, [&] { return state->stop; });
                    state->poked = false;
                    settling = true;
                }
                if (state->stop)
                    break;
            }
        }
        CoUninitialize();
    }).detach();
}
TaskbarReader::~TaskbarReader() {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->stop = true;
    }
    state_->wake.notify_all();
}
Taskbars TaskbarReader::latest() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->snapshot;
}
void TaskbarReader::set_secondary(bool value) {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->secondary == value)
            return;
        state_->secondary = value;
    }
    poke();
}
void TaskbarReader::set_paused(bool value) {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->paused == value)
            return;
        state_->paused = value;
    }
    if (!value)
        poke();
}
void TaskbarReader::poke() {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->poked = true;
    }
    state_->wake.notify_all();
}
} // namespace usage::windows
