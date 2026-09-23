#include "windows/taskbar.hpp"
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <wrl/client.h>
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
} // namespace

struct TaskbarReader::State {
    std::mutex mutex;
    std::condition_variable wake;
    bool stop{};
    bool secondary{};
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
                state->snapshot = std::move(snapshot);
                if (state->wake.wait_for(lock, std::chrono::seconds(1), [&] { return state->stop; }))
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
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->secondary = value;
}
} // namespace usage::windows
