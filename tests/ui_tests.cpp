#include "ui/views.hpp"
#include <cmath>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool value, const char* message) {
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
        if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
            const auto& text = command.renderData.text.stringContents;
            if (std::string(text.chars, static_cast<std::size_t>(text.length)) == expected)
                return true;
        }
    }
    return false;
}
} // namespace
int main() {
    try {
        {
            usage::ui::View split(usage::ui::Surface::Widget, measure, nullptr);
            usage::Usage account;
            account.live = true;
            account.codex.windows = {{"Weekly", 51, 1790583271}, {"5 hour", 76, 1790008040}};
            split.set_reference_time(1790000000);
            auto frame = split.frame(account, neutral(), 400, 38);
            check(contains(frame.commands, "5h") && contains(frame.commands, "Week") &&
                      contains(frame.commands, "76%") && contains(frame.commands, "51%") &&
                      contains(frame.commands, "Resets in 2h 14m"), "Codex split labels both reported windows and resets");
            frame = split.frame(account, neutral(), 150, 38);
            check(split.bounds("Codex5hTrack").width > 0 && split.bounds("CodexWeeklyTrack").width > 0 &&
                      !contains(frame.commands, "Resets in 2h 14m"), "Narrow Codex split keeps both bars and omits resets");
            account.codex.windows.pop_back();
            frame = split.frame(account, neutral(), 208, 38);
            check(split.bounds("CodexSplit").width == 0 && split.bounds("CodexTrack").width > 0,
                  "Weekly-only accounts retain a single Codex bar");
            for (const float width : {225.f, 300.f, 400.f}) {
                split.frame(account, neutral(), width, 38);
                const auto row = split.bounds("CodexOnly");
                const auto providers = split.bounds("Providers");
                check(std::abs(row.x - 6.f) < 0.5f &&
                          std::abs(row.width - (providers.width - 12.f)) < 0.5f,
                      "Codex-only content fills the widget width inside its padding");
            }
            account.codex.plan = "prolite";
            account.codex.credit_balance = "12.5";
            account.codex.available_resets = 2;
            usage::ui::View card(usage::ui::Surface::Hover, measure, nullptr);
            account.claude_enabled = false;
            for (const float width : {150.f, 208.f, 300.f, 480.f}) {
                const auto measured_height = card.hover_height(account, width);
                card.frame(account, neutral(), width, measured_height);
                check(std::abs(card.bounds("HoverOverview").width - width) < 0.5f,
                      "Codex-only hover fills the taskbar widget width");
            }
            auto height = card.hover_height(account, 300);
            frame = card.frame(account, neutral(), 300, height);
            check(contains(frame.commands, "Plan: prolite") && contains(frame.commands, "Credits: 12.5") &&
                      contains(frame.commands, "2 earned resets available"), "Hover shows reported account details");
            account.codex.credit_balance.clear();
            account.codex.available_resets.reset();
            frame = card.frame(account, neutral(), 300, height);
            check(!contains(frame.commands, "Credits: 12.5") && !contains(frame.commands, "0 earned resets available"),
                  "Hover omits missing account details");
            usage::Preferences clock_preferences;
            clock_preferences.appearance.hover_text_percent = 100;
            clock_preferences.appearance.twelve_hour_time = true;
            card.set_preferences(clock_preferences);
            std::tm local{};
            local.tm_year = 126; local.tm_mon = 8; local.tm_mday = 28;
            local.tm_hour = 0; local.tm_min = 14; local.tm_isdst = -1;
            auto midnight = std::mktime(&local);
            account.codex.windows = {{"Weekly", 51, midnight}};
            card.set_reference_time(midnight - 2 * 86400);
            frame = card.frame(account, neutral(), 400, 400);
            check(contains(frame.commands, "Resets Mon, 12:14 AM"), "12-hour clock formats midnight");
            account.codex.windows[0].resets_at = midnight + 12 * 3600;
            frame = card.frame(account, neutral(), 400, 400);
            check(contains(frame.commands, "Resets Mon, 12:14 PM"), "12-hour clock formats noon");
            clock_preferences.appearance.twelve_hour_time = false;
            card.set_preferences(clock_preferences);
            frame = card.frame(account, neutral(), 400, 400);
            check(contains(frame.commands, "Resets Mon, 12:14"), "24-hour clock omits AM/PM");
            std::cout << "PASS: Codex single and split window layouts\n";
        }
        usage::Usage data;
        usage::ui::View details(usage::ui::Surface::Details, measure, nullptr);
        usage::ui::View widget(usage::ui::Surface::Widget, measure, nullptr);
        usage::ui::View hover(usage::ui::Surface::Hover, measure, nullptr);
        {
            usage::Usage pair;
            pair.live = true;
            pair.codex.installed = pair.claude.installed = true;
            usage::Appearance appearance;
            const usage::Rect panel{0, 0, 1000, 48};
            const std::vector<usage::Rect> occupied{{0, 0, 650, 48}, {950, 0, 50, 48}};
            for (const int percent : {150, 160, 170, 200, 300}) {
                appearance.text_percent = percent;
                const auto placed = usage::ui::place_widget(appearance, panel, occupied, 1.f);
                check(!placed.empty(), "Increasing taskbar text never hides a widget that still fits");
                check(placed.width == appearance.widget_width, "Taskbar text never changes the widget width");
                for (const auto& block : occupied)
                    check(!placed.intersects(block), "Fitted taskbar widget avoids buttons");
            }
            appearance.text_percent = 180;
            pair.codex.windows = {{"Weekly", 81, 1790583271}};
            pair.claude.windows = {{"Weekly", 44, 1790583271}, {"Fable weekly", 23, 1790583271}};
            // A 300 px widget meeting a 270 px gap narrows; the view then tightens its
            // spacing and the bars give way, while the text keeps its size.
            appearance.widget_width = 300;
            const std::vector<usage::Rect> tight_occupied{{0, 0, 680, 48}, {950, 0, 50, 48}};
            const auto tight = usage::ui::place_widget(appearance, panel, tight_occupied, 1.f);
            check(!tight.empty() && tight.width < 300 && tight.width <= 254 && tight.width >= 240,
                  "Tight gaps narrow the bars, never the text");
            usage::Preferences fitted;
            fitted.appearance = appearance;
            widget.set_preferences(fitted);
            auto compact = widget.frame(pair, neutral(), static_cast<float>(tight.width), 38);
            for (int i = 0; i < compact.commands.length; ++i) {
                const auto& command = compact.commands.internalArray[i];
                if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
                    check(command.boundingBox.x >= 0 && command.boundingBox.y >= 0 &&
                              command.boundingBox.x + command.boundingBox.width <= tight.width &&
                              command.boundingBox.y + command.boundingBox.height <= 38,
                          "Adaptive spacing keeps the larger taskbar text inside the widget");
                    check(command.renderData.text.fontSize >= 16, "Narrowed widget keeps the requested text size");
                }
            }
            check(widget.bounds("Codex").y < widget.bounds("Claude").y &&
                      widget.bounds("Codex").x == widget.bounds("Claude").x,
                  "Large text stays stacked once padding is gone");
            const float narrowed_track = widget.bounds("CodexTrack").width;
            widget.frame(pair, neutral(), 300, 38);
            check(narrowed_track > 0 && narrowed_track < widget.bounds("CodexTrack").width,
                  "Narrowed widget shrinks its bars");
            appearance.text_percent = 300;
            const auto spacious = usage::ui::place_widget(appearance, panel, {}, 1.f);
            check(spacious.width == appearance.widget_width, "Full width returns when space returns");
            check(usage::ui::place_widget(appearance, panel, {panel}, 1.f).empty(),
                  "Completely full taskbar still hides widget safely");
        }
        usage::Preferences baseline;
        baseline.appearance.text_percent = 100;
        baseline.appearance.hover_text_percent = 100;
        baseline.appearance.widget_width = 208;
        widget.set_preferences(baseline);
        hover.set_preferences(baseline);
        usage::Usage live;
        live.live = true;
        auto loading = widget.frame(live, neutral(), 208, 38);
        check(contains(loading.commands, "Codex: connecting..."), "No invented allowance while loading");
        live.codex.windows.push_back({"Weekly", 85, 1790583271});
        auto actual = widget.frame(live, neutral(), 208, 38);
        check(contains(actual.commands, "Codex") && contains(actual.commands, "85%") &&
                  !contains(actual.commands, "62%"),
              "Single primary weekly allowance displayed");
        {
            auto single = live;
            widget.set_reference_time(1790000000);
            single.codex.windows[0].resets_at = 1790008040;
            auto reset_frame = widget.frame(single, neutral(), 208, 38);
            check(contains(reset_frame.commands, "Weekly, Resets in 2h 14m"),
                  "Codex-only reset line uses the same descriptive wording as the hover card");
            single.codex.windows[0].resets_at = 0;
            reset_frame = widget.frame(single, neutral(), 208, 38);
            check(contains(reset_frame.commands, "Weekly, Reset unavailable"), "Codex-only unknown reset is explicit");
            widget.set_reference_time(0);
        }
        live.codex.error = "Connection failed";
        actual = widget.frame(live, neutral(), 208, 38);
        check(contains(actual.commands, "Codex *"), "Failed refresh marks cached data stale");
        for (bool light : {true, false}) {
            widget.set_system_light(light);
            actual = widget.frame(live, neutral(), 208, 38);
            bool saw_text = false;
            for (int i = 0; i < actual.commands.length; ++i) {
                const auto& command = actual.commands.internalArray[i];
                if (command.commandType != CLAY_RENDER_COMMAND_TYPE_TEXT)
                    continue;
                const auto tint = command.renderData.text.textColor;
                check(light ? tint.r < 100 && tint.g < 100 && tint.b < 100 : tint.r > 100,
                      "Taskbar labels and reset text follow the system theme in both directions");
                saw_text = true;
            }
            check(saw_text, "Theme test renders taskbar text");
        }
        live.claude.installed = true;
        live.claude.windows = {{"5 hour", 97, 0}, {"Weekly", 44, 0}, {"Fable weekly", 23, 0}};
        actual = widget.frame(live, neutral(), 208, 38);
        check(contains(actual.commands, "Codex *") && contains(actual.commands, "Claude") &&
                  contains(actual.commands, "85%") && contains(actual.commands, "44 / 23%"),
              "Claude shows overall and Fable percentages separately");
        auto resets = live;
        std::tm day{};
        day.tm_year = 126;
        day.tm_mon = 8;
        day.tm_mday = 25;
        day.tm_hour = 12;
        day.tm_isdst = -1;
        resets.claude.windows[1].resets_at = std::mktime(&day);
        resets.claude.windows[2].resets_at = resets.claude.windows[1].resets_at + 3600;
        actual = widget.frame(resets, neutral(), 208, 38);
        check(contains(actual.commands, "reset 25/09"),
              "Claude merges resets on the same local day despite different times");
        resets.codex_enabled = false;
        for (int percent : {100, 150, 300}) {
            widget.set_text_percent(percent);
            // A widget the user has sized for this text scale.
            const Clay_Dimensions size{208.f * percent / 100.f, 38.f};
            actual = widget.frame(resets, neutral(), size.width, size.height);
            int reset_labels = 0;
            for (int i = 0; i < actual.commands.length; ++i) {
                const auto& command = actual.commands.internalArray[i];
                if (command.commandType != CLAY_RENDER_COMMAND_TYPE_TEXT)
                    continue;
                const auto& value = command.renderData.text.stringContents;
                if (std::string(value.chars, static_cast<std::size_t>(value.length)).find("reset ") == 0)
                    ++reset_labels;
            }
            check(reset_labels == 0 && contains(actual.commands, "25/09") &&
                      contains(actual.commands, "12:00/13:00"),
                  "Claude-only reset column shares the date and preserves both times");
            check(std::abs(widget.bounds("ClaudeGeneralTrack").width -
                           widget.bounds("ClaudeFableTrack").width) < 0.1f,
                  "Claude-only tracks have equal widths");
        }
        widget.set_preferences(baseline);
        resets.codex_enabled = true;
        {
            // Combined mode stacks two Claude tracks nearly as thick as a full bar.
            usage::Preferences combined = baseline;
            combined.appearance.text_percent = 150;
            widget.set_preferences(combined);
            const auto size = usage::ui::widget_size(combined.appearance);
            widget.frame(resets, neutral(), size.width, size.height);
            const auto weekly = widget.bounds("ClaudeWeeklyTrack"), fable = widget.bounds("ClaudeFableTrack");
            const auto row = widget.bounds("Claude");
            check(weekly.height >= 6 && fable.height == weekly.height,
                  "Combined Claude tracks are one pixel thinner than the bar");
            check(fable.y + fable.height <= row.y + row.height + 0.5f && weekly.y >= row.y - 0.5f,
                  "Combined Claude tracks stay within their row");
            const auto codex_track = widget.bounds("CodexTrack");
            check(std::abs(codex_track.x - weekly.x) < 0.5f &&
                      std::abs(codex_track.x + codex_track.width - (weekly.x + weekly.width)) < 0.5f,
                  "Combined rows start and end their bars on the same x");

            combined.appearance.text_percent = 100;
            widget.set_preferences(combined);
            widget.frame(resets, neutral(), 208, 38);
            check(widget.bounds("ClaudeWeeklyTrack").height <= 4.5f,
                  "Small labels cap the combined track thickness");
            widget.set_preferences(baseline);
        }
        resets.claude.windows[2].resets_at += 86400;
        actual = widget.frame(resets, neutral(), 208, 38);
        check(contains(actual.commands, "reset 25/09 / 26/09"), "Claude retains different reset dates");
        for (const float width : {150.f, 225.f, 480.f}) {
            const float height = hover.hover_height(live, width);
            actual = hover.frame(live, neutral(), width, height);
            for (int i = 0; i < actual.commands.length; ++i) {
                const auto& command = actual.commands.internalArray[i];
                if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT)
                    check(command.boundingBox.x >= 0 && command.boundingBox.x + command.boundingBox.width <= width + 1 &&
                              command.boundingBox.y + command.boundingBox.height <= height + 1,
                          "Hover wraps within the matched widget width and measured height");
            }
        }
        hover.set_reference_time(1790000000);
        live.codex.updated = 1789999280;
        live.codex.windows[0].resets_at = 1790008040;
        for (const int percent : {100, 140}) {
            hover.set_hover_text_percent(percent);
            const auto size = usage::ui::hover_size(live, percent);
            actual = hover.frame(live, neutral(), size.width, size.height);
            check(contains(actual.commands, "Codex - stale") && contains(actual.commands, "Claude") &&
                      contains(actual.commands, "Fable weekly"),
                  "Combined hover preserves providers, stale state and model windows");
            check(contains(actual.commands, "Resets in 2h 14m") &&
                      contains(actual.commands, "Updated 12m ago") &&
                      !contains(actual.commands, "Usage Remaining"),
                  "Hover explains remaining allowance, relative resets and freshness");
            const auto codex = hover.bounds("HoverCodex"), claude = hover.bounds("HoverClaude");
            check(codex.x == claude.x && claude.y >= codex.y + codex.height,
                  "Providers stack in one compact panel");
            check(size.width <= 504 && size.height < 540, "Combined hover stays compact at both text sizes");
            for (int i = 0; i < actual.commands.length; ++i) {
                const auto& command = actual.commands.internalArray[i];
                if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
                    check(command.boundingBox.x >= 0 &&
                              command.boundingBox.x + command.boundingBox.width <= size.width,
                          "Hover text fits horizontally");
                    check(command.boundingBox.y >= 0 &&
                              command.boundingBox.y + command.boundingBox.height <= size.height,
                          "Hover text fits vertically");
                }
            }
        }
        hover.set_hover_text_percent(100);
        // Saved provider choices control all surfaces, even with retained readings.
        auto selected = live;
        selected.claude.windows[1].resets_at = 1790583271;
        selected.claude.windows[2].resets_at = 1790669671;
        for (const bool codex : {false, true})
            for (const bool claude : {false, true}) {
                selected.codex_enabled = codex;
                selected.claude_enabled = claude;
                for (const int percent : {100, 140}) {
                    widget.set_text_percent(percent);
                    const float width = usage::widget_width * percent / 100.f;
                    actual = widget.frame(selected, neutral(), width, 38);
                    check(contains(actual.commands, "Codex *") == codex,
                          "Codex visibility follows preference");
                    if (claude && !codex) {
                        check(contains(actual.commands, "General") && contains(actual.commands, "Fable"),
                              "Claude-only labels both allowances");
                        check(contains(actual.commands, "44%") && contains(actual.commands, "23%"),
                              "Claude-only rows include separate percentages");
                        check(widget.bounds("ClaudeGeneral").y < widget.bounds("ClaudeFable").y,
                              "General and Fable have separate taskbar rows");
                    }
                    if (!codex && !claude)
                        check(contains(actual.commands, "Providers disabled - open settings"),
                              "All-disabled state stays actionable");
                    for (int i = 0; i < actual.commands.length; ++i) {
                        const auto& command = actual.commands.internalArray[i];
                        if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
                            check(command.boundingBox.x >= 0 &&
                                      command.boundingBox.x + command.boundingBox.width <= width,
                                  "Taskbar labels fit enabled-provider layout");
                            check(command.boundingBox.y >= 0 &&
                                      command.boundingBox.y + command.boundingBox.height <= 38,
                                  "Taskbar rows fit at all text sizes");
                        }
                    }
                }
                const auto size = usage::ui::hover_size(selected, 100);
                actual = hover.frame(selected, neutral(), size.width, size.height);
                check(contains(actual.commands, "Codex - stale") == codex &&
                          contains(actual.commands, "Claude") == claude,
                      "Disabled providers disappear from hover");
            }
        auto narrow_usage = live;
        narrow_usage.codex.windows = {{"Weekly", 100, 1790583271}};
        narrow_usage.claude.windows = {{"Weekly", 100, 1790583271}, {"Fable weekly", 100, 1790669671}};
        narrow_usage.claude.error = "Offline";
        for (const int percent : {100, 140}) {
            usage::Preferences narrow;
            narrow.appearance.widget_width = 160;
            narrow.appearance.text_percent = percent;
            narrow.appearance.hover_text_percent = percent;
            widget.set_preferences(narrow);
            hover.set_preferences(narrow);
            for (const bool codex : {false, true}) {
                narrow_usage.codex_enabled = codex;
                const float width = 160 * percent / 100.f;
                actual = widget.frame(narrow_usage, neutral(), width, 38);
                for (int i = 0; i < actual.commands.length; ++i) {
                    const auto& command = actual.commands.internalArray[i];
                    if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT)
                        check(command.boundingBox.x >= 0 &&
                                  command.boundingBox.x + command.boundingBox.width <= width,
                              "Small widget fits percentages, stale labels and reset dates");
                }
            }
            const auto height = usage::ui::hover_size(narrow_usage, percent).height;
            const float width = 240 * percent / 100.f;
            actual = hover.frame(narrow_usage, neutral(), width, height);
            for (int i = 0; i < actual.commands.length; ++i) {
                const auto& command = actual.commands.internalArray[i];
                if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT)
                    check(command.boundingBox.x >= 0 &&
                              command.boundingBox.x + command.boundingBox.width <= width,
                          "Small hover card keeps labels readable");
            }
        }
        for (const int percent : {100, 150, 160, 170, 200, 300}) {
            usage::Preferences sized;
            sized.appearance.text_percent = percent;
            sized.appearance.hover_text_percent = percent;
            sized.appearance.widget_width = 150;
            widget.set_preferences(sized);
            hover.set_preferences(sized);
            for (const bool codex : {false, true}) {
                narrow_usage.codex_enabled = codex;
                // A widget the user has sized generously for this text scale; side-by-side
                // rows beyond 180% need roughly twice the room of stacked ones.
                const Clay_Dimensions size{(percent > 180 ? 400.f : 150.f) * percent / 100.f, 38.f};
                actual = widget.frame(narrow_usage, neutral(), size.width, size.height);
                // Padding gives way first, so rows stack up to 180% and go side by side beyond.
                if (!codex && percent > 180)
                    check(widget.bounds("ClaudeGeneral").x < widget.bounds("ClaudeFable").x,
                          "Large taskbar text uses a horizontal layout");
                if (!codex && percent > 160 && percent <= 180)
                    check(widget.bounds("ClaudeGeneral").y < widget.bounds("ClaudeFable").y,
                          "Text up to 180% stays stacked by tightening padding");
                for (int i = 0; i < actual.commands.length; ++i) {
                    const auto& command = actual.commands.internalArray[i];
                    if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
                        check(command.boundingBox.x >= 0 &&
                                  command.boundingBox.x + command.boundingBox.width <= size.width,
                              "Large taskbar text fits horizontally");
                        check(command.boundingBox.y >= 0 &&
                                  command.boundingBox.y + command.boundingBox.height <= size.height,
                              "Large taskbar text fits vertically");
                    }
                }
            }
            const auto size = usage::ui::hover_size(narrow_usage, percent);
            actual = hover.frame(narrow_usage, neutral(), size.width, size.height);
            for (int i = 0; i < actual.commands.length; ++i) {
                const auto& command = actual.commands.internalArray[i];
                if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT)
                    check(command.boundingBox.y + command.boundingBox.height <= size.height,
                          "Hover accommodates the entire text-size range");
            }
        }
        {
            auto edge = live;
            edge.codex.windows = {{"5 hour", 0, 1789999999}, {"Weekly", 100, 0}};
            const auto size = usage::ui::hover_size(edge, hover.hover_text_percent());
            actual = hover.frame(edge, neutral(), size.width, size.height);
            check(contains(actual.commands, "Reset due - awaiting update") &&
                      contains(actual.commands, "Reset unavailable"),
                  "Hover distinguishes overdue and unknown resets");
        }
        {
            // The hover card follows its own text size, not the taskbar's.
            usage::Preferences split = baseline;
            split.appearance.text_percent = 100;
            split.appearance.hover_text_percent = 200;
            hover.set_preferences(split);
            const auto size = usage::ui::hover_size(live, 200);
            actual = hover.frame(live, neutral(), size.width, size.height);
            uint16_t largest = 0;
            for (int i = 0; i < actual.commands.length; ++i) {
                const auto& command = actual.commands.internalArray[i];
                if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT)
                    largest = std::max(largest, command.renderData.text.fontSize);
            }
            check(largest == 28, "Hover text scales with the hover text size");
            const float tall = hover.hover_height(live, 360);
            hover.set_preferences(baseline);
            check(tall > hover.hover_height(live, 360), "Hover height follows the hover text size");
        }
        widget.set_preferences(baseline);
        hover.set_preferences(baseline);
        live.codex.installed = false;
        actual = widget.frame(live, neutral(), 208, 38);
        check(!contains(actual.commands, "Codex *") && contains(actual.commands, "General") &&
                  contains(actual.commands, "Fable"),
              "Claude alone uses separate General and Fable rows");
        live.claude.installed = false;
        actual = widget.frame(live, neutral(), 208, 38);
        check(contains(actual.commands, "No supported installations detected"),
              "No-installation state has no fabricated usage");
        auto empty_size = usage::ui::hover_size(live, 100);
        actual = hover.frame(live, neutral(), empty_size.width, empty_size.height);
        check(contains(actual.commands, "No supported installations detected"),
              "Combined hover handles no providers");
        live.claude.installed = true;
        live.claude.windows.clear();
        actual = hover.frame(live, neutral(), 360, 138);
        check(contains(actual.commands, "Connecting..."), "Combined hover handles connecting provider");
        live.claude.error = "Offline";
        actual = hover.frame(live, neutral(), 360, 138);
        check(contains(actual.commands, "Claude - unavailable"),
              "Combined hover handles unavailable provider");
        live.claude.error.clear();
        live.claude.installed = false;
        live.claude.windows = {{"5 hour", 97, 0}, {"Weekly", 44, 0}, {"Fable weekly", 23, 0}};
        live.codex.installed = true;
        actual = details.frame(live, neutral(), 400, 470);
        check(details.bounds("SessionSlider").width == 0, "Live allowance cannot be edited");
        usage::ui::View settings(usage::ui::Surface::Settings, measure, nullptr);
        check(!settings.animations(), "Views start with animations off for deterministic layout");
        settings.set_animations(true);
        check(settings.animations(), "Hosts can enable eased motion");
        settings.set_animations(false);
        settings.frame(data, neutral(), 400, 470);
        auto settings_input = neutral();
        settings_input.keyTab = true;
        settings.frame(data, settings_input, 400, 470);
        settings_input = neutral();
        settings_input.keyEnd = true;
        settings.frame(data, settings_input, 400, 470);
        check(settings.text_percent() == 300 && settings.hover_text_percent() == 150,
              "Settings keyboard selects largest taskbar text without touching the hover size");
        settings_input = neutral();
        settings_input.keyTab = true;
        settings.frame(data, settings_input, 400, 470);
        settings_input = neutral();
        settings_input.keyHome = true;
        settings.frame(data, settings_input, 400, 470);
        check(settings.hover_text_percent() == 100 && settings.text_percent() == 300,
              "Hover text size has its own slider");
        settings_input = neutral();
        settings_input.keyTab = true;
        settings_input.shiftDown = true;
        settings.frame(data, settings_input, 400, 470);
        settings_input = neutral();
        settings_input.keyEscape = true;
        check(settings.frame(data, settings_input, 400, 470).close, "Escape cancels settings");
        const auto save_bounds = settings.bounds("SaveSettings");
        check(save_bounds.height > 0 && save_bounds.y + save_bounds.height <= 470,
              "Save fits settings window");
        live.claude.installed = true;
        const auto click_in = [&](usage::ui::View& view, usage::Usage& state, const char* name) {
            const auto bounds = view.bounds(name);
            auto pointer = neutral();
            pointer.mouseX = bounds.x + bounds.width / 2;
            pointer.mouseY = bounds.y + bounds.height / 2;
            view.frame(state, pointer, usage::ui::settings_width, usage::ui::settings_height);
            pointer.pointerDown = pointer.pointerPressed = true;
            view.frame(state, pointer, usage::ui::settings_width, usage::ui::settings_height);
            pointer.pointerDown = pointer.pointerPressed = false;
            pointer.pointerReleased = true;
            return view.frame(state, pointer, usage::ui::settings_width, usage::ui::settings_height);
        };
        actual = settings.frame(live, neutral(), usage::ui::settings_width, usage::ui::settings_height);
        check(settings.settings_page() == usage::ui::SettingsPage::Taskbar &&
                  settings.bounds("TextSizeSlider").width > 0 && settings.bounds("BoldTaskbar").width > 0 &&
                  settings.bounds("HoverTextSizeSlider").width == 0 && settings.bounds("EnableCodex").width == 0,
              "Settings open on the taskbar page and show only its controls");
        const auto nav = settings.bounds("SettingsNav"), page = settings.bounds("SettingsPage");
        check(nav.width > 0 && nav.x + nav.width <= page.x, "The page list sits beside the page");
        click_in(settings, live, "NavHover");
        check(settings.settings_page() == usage::ui::SettingsPage::Hover &&
                  settings.bounds("HoverTextSizeSlider").width > 0 && settings.bounds("BoldHover").width > 0 &&
                  settings.bounds("TextSizeSlider").width == 0 &&
                  settings.bounds("HoverTextSizeSlider").y > settings.bounds("HoverEnabled").y,
              "The sidebar switches to the hover card page, with its own text size");
        click_in(settings, live, "NavGeneral");
        check(settings.bounds("BoldSettings").width > 0 && settings.bounds("TimeFormat").width > 0,
              "The general page holds the settings window's weight and the time format");
        {
            using usage::ui::SettingsPage;
            const struct {
                SettingsPage page;
                const char* scroll;
                const char* last;
            } pages[] = {{SettingsPage::Taskbar, "TaskbarScroll", "BarHeight"},
                         {SettingsPage::Hover, "HoverScroll", "HoverOpacity"},
                         {SettingsPage::General, "GeneralScroll", "TimeFormat"}};
            for (const auto& entry : pages) {
                settings.set_settings_page(entry.page);
                settings.frame(live, neutral(), usage::ui::settings_width, usage::ui::settings_height);
                const auto scroll = settings.bounds(entry.scroll), last = settings.bounds(entry.last);
                check(last.height > 0 && last.y + last.height <= scroll.y + scroll.height,
                      "Every appearance page fits the default window without scrolling");
            }
        }
        settings.set_settings_page(usage::ui::SettingsPage::Providers);
        actual = settings.frame(live, neutral(), usage::ui::settings_width, usage::ui::settings_height);
        check(contains(actual.commands, "Codex usage") && contains(actual.commands, "Claude usage"),
              "The providers page shows both providers");
        check(contains(actual.commands, "Last error: Connection failed"), "Provider error remains readable");
        for (int i = 0; i < actual.commands.length; ++i) {
            const auto& command = actual.commands.internalArray[i];
            if (command.commandType != CLAY_RENDER_COMMAND_TYPE_TEXT)
                continue;
            const auto text = command.renderData.text.stringContents;
            const auto value = std::string(text.chars, static_cast<std::size_t>(text.length));
            if (value == "Enable Codex" || value == "Roboto" || value == "Update every" ||
                value == "1 minute" || value == "Last error: Connection failed" ||
                value == "Plan: Not reported")
                check(command.renderData.text.fontSize == 18,
                      "Settings labels, metadata and controls share one body size");
            check(command.renderData.text.fontSize >= 14, "Settings text never uses tiny taskbar type");
        }
        const auto providers_scroll = settings.bounds("ProvidersScroll");
        auto provider_wheel = neutral();
        provider_wheel.mouseX = providers_scroll.x + 30;
        provider_wheel.mouseY = providers_scroll.y + 100;
        settings.frame(live, provider_wheel, usage::ui::settings_width, usage::ui::settings_height);
        provider_wheel.scrollY = -20;
        actual = settings.frame(live, provider_wheel, usage::ui::settings_width, usage::ui::settings_height);
        check(contains(actual.commands, "Fable weekly"),
              "All provider limits remain accessible by scrolling");
        provider_wheel.scrollY = 20;
        settings.frame(live, provider_wheel, usage::ui::settings_width, usage::ui::settings_height);

        check(settings.bounds("RefreshUsage").height > 0 &&
                  settings.bounds("CodexRefreshUsage").height == 0 &&
                  settings.bounds("ClaudeClose").height == 0,
              "Unified panel has shared actions");
        check(settings.bounds("SessionSlider").width == 0, "Unified live usage is read only");
        const auto toggle = settings.bounds("EnableCodex");
        auto toggle_input = neutral();
        toggle_input.mouseX = toggle.x + toggle.width / 2;
        toggle_input.mouseY = toggle.y + toggle.height / 2;
        settings.frame(live, toggle_input, usage::ui::settings_width, usage::ui::settings_height);
        toggle_input.pointerDown = toggle_input.pointerPressed = true;
        settings.frame(live, toggle_input, usage::ui::settings_width, usage::ui::settings_height);
        toggle_input.pointerDown = toggle_input.pointerPressed = false;
        toggle_input.pointerReleased = true;
        settings.frame(live, toggle_input, usage::ui::settings_width, usage::ui::settings_height);
        check(!settings.codex_enabled() && settings.claude_enabled() && live.codex_enabled,
              "Provider toggles edit a draft until save");
        const auto save_live = settings.bounds("SaveSettings"),
                   refresh_live = settings.bounds("RefreshUsage");
        check(save_live.y + save_live.height <= usage::ui::settings_height && refresh_live.y + refresh_live.height <= usage::ui::settings_height,
              "Provider controls and actions fit settings");
        settings.set_providers(true, true);

        const auto refresh = settings.bounds("RefreshUsage");
        auto click = neutral();
        click.mouseX = refresh.x + refresh.width / 2;
        click.mouseY = refresh.y + refresh.height / 2;
        settings.frame(live, click, usage::ui::settings_width, usage::ui::settings_height);
        click.pointerDown = click.pointerPressed = true;
        settings.frame(live, click, usage::ui::settings_width, usage::ui::settings_height);
        click.pointerDown = click.pointerPressed = false;
        click.pointerReleased = true;
        check(settings.frame(live, click, usage::ui::settings_width, usage::ui::settings_height).refresh, "Shared refresh requests updated usage");
        check(settings.text_percent() == 300, "Usage refresh preserves pending appearance edits");
        live.codex.installed = live.claude.installed = false;
        actual = settings.frame(live, neutral(), usage::ui::settings_width, usage::ui::settings_height);
        check(contains(actual.commands, "Not detected \xC2\xB7 Not updated yet") && settings.bounds("SaveSettings").height > 0,
              "Settings remain available without providers");
        const auto click_config = [&](const char* name) { return click_in(settings, live, name); };
        check(!contains(settings.frame(live, neutral(), usage::ui::settings_width, usage::ui::settings_height).commands, "Executable"),
              "Connection details start collapsed");
        auto connection_frame = click_config("CodexConnection");
        check(contains(connection_frame.commands, "Executable"), "Connection details expand on click");
        connection_frame = click_config("CodexConnection");
        check(!contains(connection_frame.commands, "Executable"), "Connection details collapse on click");
        check(settings.bounds("FontChoice").width == 0 && settings.bounds("ThemeChoice").width == 0 &&
                  settings.bounds("AccentChoice").width == 0, "Windows appearance needs no manual selectors");
        const auto background = [&]() {
            auto frame = settings.frame(live, neutral(), usage::ui::settings_width, usage::ui::settings_height);
            for (int i = 0; i < frame.commands.length; ++i) {
                const auto& command = frame.commands.internalArray[i];
                if (command.commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE &&
                    command.id == Clay_GetElementId(CLAY_STRING("SettingsPanel")).id)
                    return command.renderData.rectangle.backgroundColor.r;
            }
            return -1.f;
        };
        check(background() == 32, "Windows dark mode paints a neutral settings background");
        settings.set_system_light(true);
        check(background() == 243, "Windows light mode repaints settings");
        settings.set_system_light(false);
        check(background() == 32, "Windows dark mode restores the dark palette");
        click_config("CodexInterval");
        auto interval_key = neutral();
        interval_key.keyEnd = true;
        settings.frame(live, interval_key, usage::ui::settings_width, usage::ui::settings_height);
        interval_key = neutral();
        interval_key.keyEnter = true;
        settings.frame(live, interval_key, usage::ui::settings_width, usage::ui::settings_height);
        check(settings.preferences().codex_interval == 900 && settings.preferences().claude_interval == 60,
              "Provider intervals are independent");
        click_config("ClaudeInterval");
        interval_key = neutral();
        interval_key.keyEscape = true;
        check(!settings.frame(live, interval_key, usage::ui::settings_width, usage::ui::settings_height).close &&
                  settings.preferences().claude_interval == 60,
              "Escape dismisses the dropdown without closing Settings");
        const auto footer = settings.bounds("SaveSettings");
        const auto cancel = settings.bounds("CancelSettings");
        check(cancel.y == footer.y && cancel.y + cancel.height <= usage::ui::settings_height,
              "Save and Cancel share the footer");
        check(settings.bounds("RefreshUsage").y < settings.bounds("ProvidersScroll").y,
              "Refresh sits above usage readings");

        click_config("ResetAppearance");
        check(settings.preferences().appearance == usage::Appearance{} &&
                  settings.preferences().codex_interval == 900,
              "Reset restores every appearance default without changing providers");
        auto custom = settings.preferences();
        custom.appearance.show_resets = false;
        widget.set_preferences(custom);
        live.codex.installed = true;
        actual = widget.frame(live, neutral(), 208, 38);
        for (int i = 0; i < actual.commands.length; ++i) {
            const auto& command = actual.commands.internalArray[i];
            if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
                check(command.renderData.text.fontId == usage::ui::regular_font, "Windows font reaches taskbar text");
                const auto value = command.renderData.text.stringContents;
                check(std::string(value.chars, static_cast<std::size_t>(value.length)).find("reset ") ==
                          std::string::npos,
                      "Reset label visibility is configurable");
            }
        }
        {
            const auto uses = [](const usage::ui::Frame& frame, uint16_t font) {
                for (int i = 0; i < frame.commands.length; ++i) {
                    const auto& command = frame.commands.internalArray[i];
                    if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT &&
                        command.renderData.text.fontId == font)
                        return true;
                }
                return false;
            };
            auto bold = usage::Preferences{};
            bold.appearance.bold_taskbar = true;
            widget.set_preferences(bold);
            hover.set_preferences(bold);
            check(uses(widget.frame(live, neutral(), 208, 38), usage::ui::bold_font) &&
                      !uses(widget.frame(live, neutral(), 208, 38), usage::ui::regular_font) &&
                      !uses(hover.frame(live, neutral(), 320, 250), usage::ui::bold_font),
                  "Bold taskbar text leaves the hover card regular");
            bold.appearance.bold_taskbar = false;
            bold.appearance.bold_hover = true;
            widget.set_preferences(bold);
            hover.set_preferences(bold);
            check(uses(hover.frame(live, neutral(), 320, 250), usage::ui::bold_font) &&
                      !uses(widget.frame(live, neutral(), 208, 38), usage::ui::bold_font),
                  "The hover card has its own bold switch");
            bold.appearance.bold_hover = false;
            bold.appearance.bold_settings = true;
            settings.set_preferences(bold);
            settings.set_settings_page(usage::ui::SettingsPage::Taskbar);
            check(!uses(settings.frame(live, neutral(), usage::ui::settings_width, usage::ui::settings_height), usage::ui::regular_font),
                  "The settings window's bold switch reaches every settings label");
            hover.set_preferences(baseline);
            settings.set_preferences(usage::Preferences{});
        }
        widget.set_preferences(usage::Preferences{});
        widget.set_text_percent(140);
        auto large = widget.frame(data, neutral(), 292, 38);
        bool large_text = false;
        for (int i = 0; i < large.commands.length; ++i) {
            const auto& command = large.commands.internalArray[i];
            if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
                large_text = large_text || command.renderData.text.fontSize == 14;
                check(command.boundingBox.y + command.boundingBox.height <= 38, "Large taskbar text fits");
            }
        }
        check(large_text, "Text preference increases rendered font size");
        widget.set_text_percent(100);
        auto overview = hover.frame(data, neutral(), 320, 250);
        check(contains(overview.commands, "38% used") && contains(overview.commands, "19% used"),
              "Hover shows used allowances");
        check(contains(overview.commands, "Token counts and reset times unavailable"),
              "Hover does not invent provider data");
        auto frame = details.frame(data, neutral(), 400, 470);
        check(contains(frame.commands, "62%") && contains(frame.commands, "81%"), "Details show live values");
        const auto slider = details.bounds("SessionSlider");
        const auto close = details.bounds("Close");
        check(slider.width > 200 && slider.height > 0, "Session slider has usable bounds");
        check(close.y >= 0 && close.y + close.height <= 470, "Close button fits popup");
        auto input = neutral();
        input.mouseX = slider.x + slider.width * 0.25f;
        input.mouseY = slider.y + slider.height / 2;
        details.frame(data, input, 400, 470);
        input.pointerDown = input.pointerPressed = true;
        check(details.frame(data, input, 400, 470).changed && data.session() == 25,
              "Pointer updates session slider");
        input.pointerPressed = false;
        input.mouseX = slider.x + slider.width + 100;
        details.frame(data, input, 400, 470);
        check(data.session() == 100, "Captured drag clamps outside track");
        input.pointerDown = false;
        input.pointerReleased = true;
        details.frame(data, input, 400, 470);
        // Rendering another surface must not steal popup focus/context.
        frame = widget.frame(data, neutral(), 208, 38);
        check(contains(frame.commands, "100%"), "Taskbar view receives changed usage");
        input = neutral();
        input.keyLeft = true;
        details.frame(data, input, 400, 470);
        check(data.session() == 99, "Keyboard focus survives rendering other view");
        input = neutral();
        input.keyTab = true;
        details.frame(data, input, 400, 470);
        input = neutral();
        input.keyHome = true;
        details.frame(data, input, 400, 470);
        check(data.weekly() == 0, "Tab focuses weekly and Home reaches zero");
        input = neutral();
        input.keyEnd = true;
        details.frame(data, input, 400, 470);
        check(data.weekly() == 100, "End reaches full allowance");
        input = neutral();
        input.keyTab = true;
        details.frame(data, input, 400, 470);
        input = neutral();
        input.keyEnter = true;
        check(details.frame(data, input, 400, 470).close, "Keyboard activates Close");
        details.reset_focus();
        input = neutral();
        input.keyEscape = true;
        check(details.frame(data, input, 400, 470).close, "Escape closes details");
        for (const int value : {0, 15, 30, 100}) {
            data.set_session(value);
            data.set_weekly(value);
            frame = widget.frame(data, neutral(), 208, 38);
            check(contains(frame.commands, std::to_string(value) + "%"),
                  "Boundary percentage visible in widget");
            frame = hover.frame(data, neutral(), 320, 250);
            check(contains(frame.commands, std::to_string(100 - value) + "% used"),
                  "Hover updates at allowance boundaries");
            for (int i = 0; i < frame.commands.length; ++i) {
                const auto& command = frame.commands.internalArray[i];
                if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT)
                    check(command.boundingBox.y + command.boundingBox.height <= 250, "Hover text fits card");
            }
        }
        std::cout << "PASS: Clay layouts, pointer drag, focus isolation, keyboard navigation, Close and "
                     "allowance limits.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
