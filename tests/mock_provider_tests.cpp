#include "core/mock_provider.hpp"
#include "ui/views.hpp"
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>

namespace {
using namespace usage;
constexpr std::int64_t now = 1790000000;

void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
Clay_Dimensions measure(Clay_StringSlice text, Clay_TextElementConfig* config, void*) {
    return {static_cast<float>(text.length) * config->fontSize * 0.5f, static_cast<float>(config->fontSize)};
}
ClayWidgets_Input neutral() {
    ClayWidgets_Input result{};
    result.mouseX = result.mouseY = -100;
    return result;
}
bool contains(Clay_RenderCommandArray commands, const std::string& expected) {
    for (int i = 0; i < commands.length; ++i) {
        const auto& command = commands.internalArray[i];
        if (command.commandType != CLAY_RENDER_COMMAND_TYPE_TEXT)
            continue;
        const auto& text = command.renderData.text.stringContents;
        if (std::string(text.chars, static_cast<std::size_t>(text.length)) == expected)
            return true;
    }
    return false;
}

// What the app holds after a first reading of this provider: the reply
// parsed through the real protocol, or an installed account carrying the error.
AccountUsage first_reading(Service service, const MockProvider& provider, const std::string& context) {
    const bool fails = provider.state == MockState::LoginError || provider.state == MockState::Malformed;
    try {
        auto result = read_mock(service, provider, now);
        check(!fails, context + ": a failing endpoint must be rejected");
        return result;
    } catch (const std::exception& error) {
        check(fails, context + ": unexpected failure: " + error.what());
        AccountUsage result;
        result.error = error.what();
        return result;
    }
}

void check_reading(const AccountUsage& account, const MockProvider& provider, bool claude,
                   const std::string& context) {
    check(account.installed == (provider.state != MockState::Missing), context + ": installed flag");
    if (provider.state != MockState::Ready) {
        check(account.windows.empty(), context + ": no windows without a usable reply");
        return;
    }
    check(account.plan == provider.plan, context + ": plan");
    struct Expected {
        const MockAllowance* allowance;
        std::string label;
    };
    std::vector<Expected> expected;
    if (provider.session.present)
        expected.push_back({&provider.session, "5 hour"});
    if (provider.weekly.present)
        expected.push_back({&provider.weekly, "Weekly"});
    if (claude && provider.model.present)
        expected.push_back({&provider.model, provider.model_name + " weekly"});
    check(account.windows.size() == expected.size(), context + ": window count");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& window = account.windows[i];
        check(window.label == expected[i].label, context + ": label " + window.label);
        check(window.remaining == 100 - expected[i].allowance->used_percent, context + ": remaining " + window.label);
        check(window.resets_at == now + expected[i].allowance->resets_in_minutes * 60,
              context + ": reset time " + window.label);
    }
    if (!claude) {
        check(account.credit_balance == provider.credit_balance, context + ": credits");
        check(account.available_resets.value_or(-1) == provider.available_resets, context + ": earned resets");
    }
}

// The taskbar element each preset's widget must lay out with; every preset
// needs an entry, so a new one can't slip in untested.
const std::map<std::string, const char*> widget_layouts{
    {"Codex + Claude", "Claude"},
    {"Codex + Claude (no model window)", "Providers"},
    {"Codex only: 5 hour + weekly", "CodexSplit"},
    {"Codex only: weekly", "CodexOnly"},
    {"Codex only: 5 hour", "CodexOnly"},
    {"Claude only: weekly + model", "ClaudeOnly"},
    {"Claude only: 5 hour + weekly", "Providers"},
    {"Near the limits", "Claude"},
    {"Codex login error", "Claude"},
    {"Claude malformed reply", "Providers"},
    {"Both connecting", "Providers"},
    {"Nothing installed", "NoProviders"},
};

void render(const MockPreset& preset, Usage& data) {
    const std::string name = preset.name;
    const auto layout = widget_layouts.find(name);
    check(layout != widget_layouts.end(), name + ": no expected widget layout");
    ui::View widget(ui::Surface::Widget, measure, nullptr);
    widget.set_reference_time(now);
    auto frame = widget.frame(data, neutral(), 300, 38);
    check(frame.commands.length > 0, name + ": widget renders");
    check(widget.bounds(layout->second).width > 0, name + ": widget lays out as " + layout->second);
    for (const auto& [provider, account, active] :
         {std::tuple{"Codex", &data.codex, data.codex_active()}, std::tuple{"Claude", &data.claude, data.claude_active()}}) {
        if (active && account->windows.empty())
            check(contains(frame.commands, std::string(provider) +
                                               (account->error.empty() ? ": connecting..." : ": unavailable")),
                  name + ": widget says why " + provider + " has no bars");
    }
    ui::View hover(ui::Surface::Hover, measure, nullptr);
    hover.set_reference_time(now);
    for (const float width : {150.f, 208.f, 300.f}) {
        const float height = hover.hover_height(data, width);
        check(height > 0, name + ": hover card has height");
        check(hover.frame(data, neutral(), width, height).commands.length > 0, name + ": hover card renders");
    }
    for (const auto surface : {ui::Surface::Details, ui::Surface::Settings}) {
        ui::View popup(surface, measure, nullptr);
        popup.set_reference_time(now);
        check(popup.frame(data, neutral(), ui::settings_width, ui::settings_height).commands.length > 0,
              name + ": popup renders");
    }
    if (data.codex_active() && data.claude_active()) {
        // Settings keeps both providers available even when one is switched off.
        data.claude_enabled = false;
        check(widget.frame(data, neutral(), 300, 38).commands.length > 0, name + ": Claude disabled renders");
        data.claude_enabled = true;
    }
}
} // namespace

int main() {
    try {
        check(!mock_presets().empty() && std::strcmp(mock_presets().front().name, "Codex + Claude") == 0,
              "The debug build starts with both providers");
        check(widget_layouts.size() == mock_presets().size(), "Every widget layout belongs to a preset");
        for (const auto& preset : mock_presets()) {
            const std::string name = preset.name;
            Usage data;
            data.live = true;
            data.codex = first_reading(Service::Codex, preset.scenario.codex, name + " Codex");
            data.claude = first_reading(Service::Claude, preset.scenario.claude, name + " Claude");
            check_reading(data.codex, preset.scenario.codex, false, name + " Codex");
            check_reading(data.claude, preset.scenario.claude, true, name + " Claude");
            render(preset, data);
        }
        // Claude's ISO reset times survive month, year and leap-day boundaries.
        MockProvider claude;
        claude.plan = "max";
        claude.session = {true, 10, 0};
        claude.weekly = {false};
        for (const std::int64_t instant : {951782400LL /* 2000-02-29 */, 1767225599LL /* 2025-12-31 23:59:59 */,
                                           4102444800LL /* 2100-01-01 */}) {
            const auto account = read_mock(Service::Claude, claude, instant);
            check(account.windows.size() == 1 && account.windows[0].resets_at == instant,
                  "Claude reset time round trip at " + std::to_string(instant));
        }
        std::cout << "Every mock provider scenario parses and renders\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
