#include "core/claude.hpp"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace usage {
namespace {
std::int64_t timestamp(const nlohmann::json& value) {
    if (!value.is_string())
        return 0;
    const auto text = value.get<std::string>();
    int year{}, month{}, day{}, hour{}, minute{}, second{};
    if (std::sscanf(text.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6)
        return 0;
    // Claude returns UTC ISO-8601 timestamps, optionally with fractional seconds.
    if (text.back() != 'Z' && text.find("+00:00") == std::string::npos)
        return 0;
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59)
        return 0;
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned y = static_cast<unsigned>(year - era * 400);
    const unsigned m = static_cast<unsigned>(month + (month > 2 ? -3 : 9));
    const unsigned days = y * 365 + y / 4 - y / 100 + (153 * m + 2) / 5 + static_cast<unsigned>(day) - 1;
    return (static_cast<std::int64_t>(era) * 146097 + days - 719468) * 86400 + hour * 3600 + minute * 60 +
           second;
}
} // namespace
AccountUsage parse_claude_limits(std::string_view source) {
    const auto root = nlohmann::json::parse(source);
    if (!root.value("rate_limits_available", false))
        throw std::runtime_error("Claude subscription usage unavailable");
    const auto& limits = root.at("rate_limits");
    AccountUsage result;
    if (root.contains("subscription_type") && root["subscription_type"].is_string())
        result.plan = root["subscription_type"].get<std::string>();
    // Session, weekly and model windows are gathered separately so legacy fallbacks keep the display order.
    std::vector<Allowance> session, weekly, scoped;
    // An idle window may report a null percentage; `idle_used` then stands in for it.
    const auto add = [](std::vector<Allowance>& into, const nlohmann::json& window, const char* key,
                        std::string label, double idle_used = NAN) {
        if (!window.is_object() || !window.contains(key))
            return;
        double used = idle_used;
        if (window[key].is_number())
            used = window[key].get<double>();
        else if (!window[key].is_null())
            return;
        if (!std::isfinite(used))
            return;
        into.push_back({std::move(label), static_cast<int>(std::lround(100 - std::clamp(used, 0.0, 100.0))),
                        window.contains("resets_at") ? timestamp(window["resets_at"]) : 0});
    };
    // The normalized list includes model-specific windows without duplicating legacy fields.
    if (limits.contains("limits") && limits["limits"].is_array()) {
        for (const auto& window : limits["limits"]) {
            const auto kind = window.value("kind", std::string{});
            if (kind == "session")
                add(session, window, "percent", "5 hour", 0);
            else if (kind == "weekly_all")
                add(weekly, window, "percent", "Weekly");
            else if (kind == "weekly_scoped") {
                std::string name = "Model";
                if (window.contains("scope") && window["scope"].is_object() &&
                    window["scope"].contains("model") && window["scope"]["model"].is_object()) {
                    const auto& model = window["scope"]["model"];
                    if (model.contains("display_name") && model["display_name"].is_string())
                        name = model["display_name"].get<std::string>();
                }
                add(scoped, window, "percent", name + " weekly");
            }
        }
    }
    // Some plans leave windows out of the normalized list, so fill each missing one from the legacy fields.
    if (session.empty() && limits.contains("five_hour"))
        add(session, limits["five_hour"], "utilization", "5 hour");
    if (weekly.empty() && limits.contains("seven_day"))
        add(weekly, limits["seven_day"], "utilization", "Weekly");
    if (scoped.empty() && limits.contains("model_scoped") && limits["model_scoped"].is_array())
        for (const auto& window : limits["model_scoped"])
            if (window.is_object())
                add(scoped, window, "utilization",
                    window.value("display_name", std::string("Model")) + " weekly");
    for (auto* group : {&session, &weekly, &scoped})
        result.windows.insert(result.windows.end(), group->begin(), group->end());
    if (result.windows.empty())
        throw std::runtime_error("No Claude allowance windows available");
    result.updated = std::time(nullptr);
    return result;
}
} // namespace usage
