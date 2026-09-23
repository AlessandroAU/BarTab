#include "core/provider_protocol.hpp"
#include "core/codex.hpp"
#include "core/claude.hpp"
#include <json.hpp>
#include <stdexcept>

namespace usage {
namespace {
using Json = nlohmann::json;
ProtocolReply codex_response(const Json& message) {
    if (!message.contains("id") || !message["id"].is_number_integer())
        return {};
    const int id = message["id"].get<int>();
    if (id != 1 && id != 2)
        return {};
    if (message.contains("error"))
        throw std::runtime_error("Usage unavailable; check your Codex login");
    if (id == 1) {
        return {{Json{{"method", "initialized"}, {"params", Json::object()}}.dump(),
                 Json{{"id", 2}, {"method", "account/rateLimits/read"}, {"params", Json::object()}}.dump()},
                {}};
    }
    return {{}, parse_codex_limits(message.at("result").dump())};
}
ProtocolReply claude_response(const Json& message) {
    if (message.value("type", std::string{}) != "control_response")
        return {};
    const auto& response = message.at("response");
    const auto id = response.value("request_id", std::string{});
    if (id != "init" && id != "usage")
        return {};
    if (response.value("subtype", std::string{}) != "success")
        throw std::runtime_error("Usage unavailable; check Claude login/version");
    if (id == "init") {
        return {{Json{{"type", "control_request"},
                      {"request_id", "usage"},
                      {"request", {{"subtype", "get_usage"}}}}
                     .dump()},
                {}};
    }
    return {{}, parse_claude_limits(response.at("response").dump())};
}
} // namespace
std::vector<std::string> ProviderProtocol::arguments() const {
    if (service_ == Service::Claude)
        return {"--print",   "--input-format",           "stream-json", "--output-format",    "stream-json",
                "--verbose", "--no-session-persistence", "--safe-mode", "--strict-mcp-config"};
    return {"app-server", "--listen", "stdio://"};
}
std::string ProviderProtocol::initialize() const {
    if (service_ == Service::Claude)
        return Json{
            {"type", "control_request"}, {"request_id", "init"}, {"request", {{"subtype", "initialize"}}}}
            .dump();
    return Json{{"id", 1},
                {"method", "initialize"},
                {"params", {{"clientInfo", {{"name", "usage_tracker"}, {"version", "0.4.0"}}}}}}
        .dump();
}
ProtocolReply ProviderProtocol::receive(std::string_view line) const {
    const auto message = Json::parse(line);
    return service_ == Service::Claude ? claude_response(message) : codex_response(message);
}
} // namespace usage
