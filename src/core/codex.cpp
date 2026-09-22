#include "core/codex.hpp"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace usage {
AccountUsage parse_codex_limits(std::string_view source) {
    const auto root = nlohmann::json::parse(source);
    const nlohmann::json* bucket = nullptr;
    if (root.contains("rateLimitsByLimitId") && root["rateLimitsByLimitId"].is_object()) {
        const auto& buckets = root["rateLimitsByLimitId"];
        if (buckets.contains("codex") && buckets["codex"].is_object()) bucket = &buckets["codex"];
    }
    if (!bucket && root.contains("rateLimits") && root["rateLimits"].is_object()) {
        const auto& legacy = root["rateLimits"];
        if (!legacy.contains("limitId") || legacy["limitId"].is_null() || legacy["limitId"] == "codex") bucket = &legacy;
    }
    if (!bucket) throw std::runtime_error("No Codex allowance available");
    AccountUsage result;
    if (bucket->contains("planType") && (*bucket)["planType"].is_string()) result.plan = (*bucket)["planType"].get<std::string>();
    for (const char* key : {"primary","secondary"}) {
        if (!bucket->contains(key) || (*bucket)[key].is_null()) continue;
        const auto& window = (*bucket)[key];
        if (!window.contains("usedPercent") || !window["usedPercent"].is_number()) throw std::runtime_error("Invalid allowance percentage");
        const double used = window["usedPercent"].get<double>();
        if (!std::isfinite(used)) throw std::runtime_error("Invalid allowance percentage");
        const int minutes = window.contains("windowDurationMins") && window["windowDurationMins"].is_number_integer() ? window["windowDurationMins"].get<int>() : 0;
        const std::string label = minutes == 10080 ? "Weekly" : minutes > 0 && minutes % 60 == 0 ? std::to_string(minutes/60)+" hour" : minutes > 0 ? std::to_string(minutes)+" min" : key == std::string("primary") ? "Primary" : "Secondary";
        const auto reset = window.contains("resetsAt") && window["resetsAt"].is_number_integer() ? window["resetsAt"].get<std::int64_t>() : 0;
        result.windows.push_back({label,static_cast<int>(std::lround(100-std::clamp(used,0.0,100.0))),reset});
    }
    if (result.windows.empty()) throw std::runtime_error("No allowance windows available");
    result.updated = std::time(nullptr);
    return result;
}
}
