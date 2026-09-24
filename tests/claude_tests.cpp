#include "core/claude.hpp"
#include <iostream>
#include <stdexcept>
void check(bool value) {
    if (!value)
        throw std::runtime_error("Claude parsing check failed");
}
int main() {
    try {
        auto result = usage::parse_claude_limits(
            R"({"subscription_type":"max","rate_limits_available":true,"rate_limits":{"limits":[{"kind":"session","percent":3,"resets_at":"2026-09-21T17:49:59.966184+00:00"},{"kind":"weekly_all","percent":56},{"kind":"weekly_scoped","percent":77,"scope":{"model":{"display_name":"Fable"}}}],"five_hour":{"utilization":99}}})");
        check(result.windows.size() == 3 && result.windows[0].remaining == 97 &&
              result.windows[1].remaining == 44 && result.windows[2].remaining == 23);
        check(result.windows[0].resets_at == 1790012999 && result.windows[2].label == "Fable weekly" &&
              result.plan == "max");
        result = usage::parse_claude_limits(
            R"({"rate_limits_available":true,"rate_limits":{"five_hour":{"utilization":200},"seven_day":null,"model_scoped":[{"display_name":"Sonnet","utilization":-5}]}})");
        check(result.windows.size() == 2 && result.windows[0].remaining == 0 &&
              result.windows[1].remaining == 100 && result.windows[0].resets_at == 0);
        // Normalized list without a session entry still picks up the legacy five_hour window, first.
        result = usage::parse_claude_limits(
            R"({"rate_limits_available":true,"rate_limits":{"limits":[{"kind":"weekly_all","percent":40}],"five_hour":{"utilization":25},"seven_day":{"utilization":90}}})");
        check(result.windows.size() == 2 && result.windows[0].label == "5 hour" &&
              result.windows[0].remaining == 75 && result.windows[1].label == "Weekly" &&
              result.windows[1].remaining == 60);
        // An idle session reports a null percentage, which means nothing used yet.
        result = usage::parse_claude_limits(
            R"({"rate_limits_available":true,"rate_limits":{"limits":[{"kind":"session","percent":null,"resets_at":null},{"kind":"weekly_all","percent":10}],"five_hour":null}})");
        check(result.windows.size() == 2 && result.windows[0].label == "5 hour" &&
              result.windows[0].remaining == 100 && result.windows[0].resets_at == 0);
        for (const char* invalid :
             {"{}", R"({"rate_limits_available":false})",
              R"({"rate_limits_available":true,"rate_limits":{"five_hour":{"utilization":"wrong"}}})"}) {
            bool rejected = false;
            try {
                usage::parse_claude_limits(invalid);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected);
        }
        std::cout << "PASS: Claude normalized and legacy windows, scoped limits, UTC resets, invalid data\n";
    } catch (const std::exception& error) {
        std::cerr << error.what();
        return 1;
    }
}
