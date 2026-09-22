#include "core/codex.hpp"
#include <iostream>
#include <stdexcept>
void check(bool value) {
    if (!value)
        throw std::runtime_error("Codex parsing check failed");
}
int main() {
    try {
        auto result = usage::parse_codex_limits(
            R"({"rateLimitsByLimitId":{"codex":{"primary":{"usedPercent":15,"windowDurationMins":10080,"resetsAt":1790583271},"secondary":null}},"rateLimits":{"primary":{"usedPercent":99}}})");
        check(result.windows.size() == 1 && result.windows[0].label == "Weekly" &&
              result.windows[0].remaining == 85 && result.windows[0].resets_at == 1790583271);
        result = usage::parse_codex_limits(
            R"({"rateLimits":{"primary":{"usedPercent":125,"windowDurationMins":300},"secondary":{"usedPercent":-5,"windowDurationMins":10080}}})");
        check(result.windows.size() == 2 && result.windows[0].remaining == 0 &&
              result.windows[1].remaining == 100 && result.windows[0].label == "5 hour");
        check(result.credit_balance.empty() && !result.available_resets && !result.has_credits);
        result = usage::parse_codex_limits(
            R"({"rateLimits":{"primary":{"usedPercent":20},"credits":{"balance":"12.5","unlimited":false,"hasCredits":true}},"rateLimitResetCredits":{"availableCount":2,"credits":[]}})");
        check(result.credit_balance == "12.5" && result.has_credits == true && result.available_resets == 2);
        result = usage::parse_codex_limits(
            R"({"rateLimits":{"primary":{"usedPercent":20},"credits":{"balance":null,"unlimited":true}},"rateLimitResetCredits":{"availableCount":0}})");
        check(result.unlimited_credits && result.credit_balance.empty() && result.available_resets == 0);
        result = usage::parse_codex_limits(
            R"({"rateLimits":{"primary":{"usedPercent":20},"credits":{"balance":[],"unlimited":"yes","hasCredits":null}},"rateLimitResetCredits":{"availableCount":-1}})");
        check(!result.unlimited_credits && !result.has_credits && !result.available_resets);
        for (const char* invalid :
             {"{}", R"({"rateLimits":{"primary":null}})",
              R"({"rateLimits":{"primary":{"usedPercent":"15"}}})",
              R"({"rateLimits":{"limitId":"other-model","primary":{"usedPercent":15}}})"}) {
            bool rejected = false;
            try {
                usage::parse_codex_limits(invalid);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected);
        }
        std::cout << "PASS: Codex buckets, optional windows, durations, clamping and invalid data\n";
    } catch (const std::exception& error) {
        std::cerr << error.what();
        return 1;
    }
}
