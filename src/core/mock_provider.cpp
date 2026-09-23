#include "core/mock_provider.hpp"
#include <json.hpp>
#include <cstdio>
#include <deque>
#include <stdexcept>

namespace usage {
namespace {
using Json = nlohmann::json;

// Claude's reset times are UTC ISO-8601; days-to-civil keeps this free of the
// C library's non-reentrant gmtime, since both readers run on their own threads.
std::string iso_time(std::int64_t seconds) {
    std::int64_t days = seconds / 86400, rest = seconds % 86400;
    if (rest < 0)
        rest += 86400, --days;
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const auto day_of_era = static_cast<unsigned>(days - era * 146097);
    const unsigned year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const unsigned day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const unsigned shifted_month = (5 * day_of_year + 2) / 153;
    const unsigned day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
    const unsigned month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
    const auto year = static_cast<std::int64_t>(year_of_era) + era * 400 + (month <= 2);
    char text[32];
    std::snprintf(text, sizeof(text), "%04lld-%02u-%02uT%02d:%02d:%02dZ", static_cast<long long>(year), month, day,
                  static_cast<int>(rest / 3600), static_cast<int>(rest / 60 % 60), static_cast<int>(rest % 60));
    return text;
}

Json codex_window(const MockAllowance& allowance, int minutes, std::int64_t now) {
    return {{"usedPercent", allowance.used_percent},
            {"windowDurationMins", minutes},
            {"resetsAt", now + std::int64_t{allowance.resets_in_minutes} * 60}};
}

std::vector<std::string> codex_answer(const MockProvider& provider, const Json& request, std::int64_t now) {
    const auto method = request.value("method", std::string{});
    if (!request.contains("id"))
        return {}; // Notifications such as "initialized" get no reply.
    const auto& id = request["id"];
    if (method == "initialize")
        return {Json{{"id", id}, {"result", {{"userAgent", "codex-mock"}}}}.dump(),
                // Unsolicited traffic the client must skip over.
                Json{{"method", "account/updated"}, {"params", Json::object()}}.dump()};
    if (method != "account/rateLimits/read")
        return {Json{{"id", id}, {"error", {{"code", -32601}, {"message", "Method not found"}}}}.dump()};
    if (provider.state == MockState::LoginError)
        return {Json{{"id", id}, {"error", {{"code", -32600}, {"message", "Not logged in"}}}}.dump()};
    if (provider.state == MockState::Malformed)
        return {Json{{"id", id}, {"result", {{"rateLimits", {{"primary", {{"usedPercent", "lots"}}}}}}}}.dump()};
    // The server reports whichever windows apply as primary then secondary.
    std::vector<Json> windows;
    if (provider.session.present)
        windows.push_back(codex_window(provider.session, 300, now));
    if (provider.weekly.present)
        windows.push_back(codex_window(provider.weekly, 10080, now));
    Json bucket{{"limitId", "codex"},
                {"planType", provider.plan.empty() ? Json(nullptr) : Json(provider.plan)},
                {"primary", windows.size() > 0 ? windows[0] : Json(nullptr)},
                {"secondary", windows.size() > 1 ? windows[1] : Json(nullptr)}};
    if (!provider.credit_balance.empty())
        bucket["credits"] = {{"hasCredits", true}, {"unlimited", false}, {"balance", provider.credit_balance}};
    Json result{{"rateLimits", bucket}, {"rateLimitsByLimitId", {{"codex", bucket}}}};
    if (provider.available_resets >= 0)
        result["rateLimitResetCredits"] = {{"availableCount", provider.available_resets}};
    return {Json{{"id", id}, {"result", result}}.dump()};
}

Json claude_success(const std::string& id, Json response) {
    return {{"type", "control_response"},
            {"response", {{"subtype", "success"}, {"request_id", id}, {"response", std::move(response)}}}};
}

std::vector<std::string> claude_answer(const MockProvider& provider, const Json& request, std::int64_t now) {
    if (request.value("type", std::string{}) != "control_request")
        return {};
    const auto id = request.value("request_id", std::string{});
    const auto subtype = request.at("request").value("subtype", std::string{});
    if (subtype == "initialize")
        return {Json{{"type", "system"}, {"subtype", "init"}}.dump(),
                claude_success(id, {{"commands", Json::array()}}).dump()};
    if (subtype != "get_usage" || provider.state == MockState::LoginError)
        return {Json{{"type", "control_response"},
                     {"response", {{"subtype", "error"}, {"request_id", id}, {"error", "Not logged in"}}}}
                    .dump()};
    if (provider.state == MockState::Malformed)
        return {claude_success(id, {{"rate_limits_available", false}}).dump()};
    Json limits = Json::array();
    const auto add = [&](const MockAllowance& allowance, const char* kind, Json extra = {}) {
        if (!allowance.present)
            return;
        Json window{{"kind", kind},
                    {"percent", allowance.used_percent},
                    {"resets_at", iso_time(now + std::int64_t{allowance.resets_in_minutes} * 60)}};
        if (!extra.is_null())
            window.update(extra);
        limits.push_back(std::move(window));
    };
    add(provider.session, "session");
    add(provider.weekly, "weekly_all");
    add(provider.model, "weekly_scoped", {{"scope", {{"model", {{"display_name", provider.model_name}}}}}});
    Json usage{{"rate_limits_available", true}, {"rate_limits", {{"limits", limits}}}};
    if (!provider.plan.empty())
        usage["subscription_type"] = provider.plan;
    return {claude_success(id, usage).dump()};
}

MockAllowance used(int percent, int resets_in_minutes) {
    return {true, percent, resets_in_minutes};
}
constexpr MockAllowance absent{false};
constexpr int hours = 60, days = 24 * 60;

MockProvider codex(MockAllowance session, MockAllowance weekly) {
    MockProvider result;
    result.plan = "pro";
    result.session = session;
    result.weekly = weekly;
    result.credit_balance = "12.5";
    result.available_resets = 2;
    return result;
}
MockProvider claude(MockAllowance session, MockAllowance weekly, MockAllowance model) {
    MockProvider result;
    result.plan = "max";
    result.session = session;
    result.weekly = weekly;
    result.model = model;
    return result;
}
MockProvider with_state(MockProvider provider, MockState state) {
    provider.state = state;
    return provider;
}
} // namespace

const char* mock_state_name(MockState state) {
    switch (state) {
    case MockState::Ready: return "Ready";
    case MockState::Missing: return "Not installed";
    case MockState::LoginError: return "Login error";
    case MockState::Connecting: return "Connecting";
    case MockState::Malformed: return "Malformed reply";
    }
    return "";
}

const std::vector<MockPreset>& mock_presets() {
    static const auto presets = [] {
        const auto codex_both = codex(used(38, 2 * hours + 14), used(19, 4 * days + 3 * hours));
        const auto claude_all = claude(used(3, 4 * hours), used(56, 3 * days), used(77, 5 * days));
        const auto missing = with_state({}, MockState::Missing);
        return std::vector<MockPreset>{
            {"Codex + Claude", {codex_both, claude_all}},
            {"Codex + Claude (no model window)", {codex_both, claude(used(3, 4 * hours), used(56, 3 * days), absent)}},
            {"Codex only: 5 hour + weekly", {codex_both, missing}},
            {"Codex only: weekly", {codex(absent, used(19, 4 * days)), missing}},
            {"Codex only: 5 hour", {codex(used(38, 2 * hours), absent), missing}},
            {"Claude only: weekly + model", {missing, claude_all}},
            {"Claude only: 5 hour + weekly", {missing, claude(used(3, 4 * hours), used(56, 3 * days), absent)}},
            {"Near the limits", {codex(used(97, 40), used(91, 26 * hours)),
                                 claude(used(99, 12), used(95, 20 * hours), used(100, 2 * days))}},
            {"Codex login error", {with_state(codex_both, MockState::LoginError), claude_all}},
            {"Claude malformed reply", {codex_both, with_state(claude_all, MockState::Malformed)}},
            {"Both connecting", {with_state(codex_both, MockState::Connecting),
                                 with_state(claude_all, MockState::Connecting)}},
            {"Nothing installed", {missing, missing}},
        };
    }();
    return presets;
}

std::vector<std::string> MockEndpoint::answer(std::string_view request) const {
    const auto message = Json::parse(request);
    return service_ == Service::Claude ? claude_answer(provider_, message, now_)
                                       : codex_answer(provider_, message, now_);
}

AccountUsage read_mock(Service service, const MockProvider& provider, std::int64_t now) {
    AccountUsage result;
    result.installed = provider.state != MockState::Missing;
    if (provider.state == MockState::Missing || provider.state == MockState::Connecting)
        return result;
    ProviderProtocol protocol(service);
    const MockEndpoint endpoint(service, provider, now);
    std::deque<std::string> outgoing{protocol.initialize()};
    while (!outgoing.empty()) {
        const auto request = std::move(outgoing.front());
        outgoing.pop_front();
        for (const auto& line : endpoint.answer(request)) {
            auto reply = protocol.receive(line);
            if (reply.usage)
                return std::move(*reply.usage);
            outgoing.insert(outgoing.end(), reply.messages.begin(), reply.messages.end());
        }
    }
    throw std::runtime_error("The mock provider stopped answering");
}
} // namespace usage
