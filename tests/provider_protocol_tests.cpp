#include "core/provider_protocol.hpp"
#include <json.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void ignored(const usage::ProviderProtocol& protocol, const char* line) {
    const auto reply = protocol.receive(line);
    check(reply.messages.empty() && !reply.usage, "Unrelated messages must not advance the exchange");
}
void rejected(const usage::ProviderProtocol& protocol, const char* line) {
    bool failed = false;
    try {
        protocol.receive(line);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Invalid responses must fail the request");
}
void codex_exchange() {
    usage::ProviderProtocol protocol(usage::Service::Codex);
    const auto initialize = Json::parse(protocol.initialize());
    check(initialize.at("id") == 1 && initialize.at("method") == "initialize", "Codex initializes first");
    check(protocol.arguments() == std::vector<std::string>{"app-server", "--listen", "stdio://"},
          "Codex uses the stdio server");
    ignored(protocol, R"({"method":"notification"})");
    ignored(protocol, R"({"id":"1","result":{}})");
    ignored(protocol, R"({"id":99,"error":{}})");
    const auto handshake = protocol.receive(R"({"id":1,"result":{}})");
    check(handshake.messages.size() == 2 && !handshake.usage,
          "Codex handshake sends notification then request");
    check(Json::parse(handshake.messages[0]).at("method") == "initialized", "Codex initialized notification");
    const auto request = Json::parse(handshake.messages[1]);
    check(request.at("id") == 2 && request.at("method") == "account/rateLimits/read", "Codex usage request");
    const auto result = protocol.receive(
        R"({"id":2,"result":{"rateLimits":{"planType":"pro","primary":{"usedPercent":25,"windowDurationMins":300,"resetsAt":12345}}}})");
    check(result.messages.empty() && result.usage && result.usage->plan == "pro" &&
              result.usage->windows.size() == 1 && result.usage->windows[0].remaining == 75 &&
              result.usage->windows[0].resets_at == 12345,
          "Codex usage response is parsed");
    rejected(protocol, R"({"id":1,"error":{"message":"login"}})");
    rejected(protocol, R"({"id":2,"error":{}})");
    rejected(protocol, R"({"id":2})");
    rejected(protocol, R"({"id":2,"result":{}})");
    rejected(protocol, "not json");
}
void claude_exchange() {
    usage::ProviderProtocol protocol(usage::Service::Claude);
    const auto initialize = Json::parse(protocol.initialize());
    check(initialize.at("type") == "control_request" && initialize.at("request_id") == "init" &&
              initialize.at("request").at("subtype") == "initialize",
          "Claude initializes first");
    const auto arguments = protocol.arguments();
    const auto has = [&](const char* flag) {
        return std::find(arguments.begin(), arguments.end(), flag) != arguments.end();
    };
    check(has("--safe-mode") && has("--strict-mcp-config") && has("--no-session-persistence"),
          "Claude keeps safe launch options");
    ignored(protocol, R"({"type":"system"})");
    ignored(protocol, R"({"type":"control_response","response":{"request_id":"other","subtype":"error"}})");
    const auto handshake = protocol.receive(
        R"({"type":"control_response","response":{"request_id":"init","subtype":"success","response":{}}})");
    check(handshake.messages.size() == 1 && !handshake.usage, "Claude handshake sends one usage request");
    const auto request = Json::parse(handshake.messages[0]);
    check(request.at("request_id") == "usage" && request.at("request").at("subtype") == "get_usage",
          "Claude usage request");
    const auto result = protocol.receive(
        R"({"type":"control_response","response":{"request_id":"usage","subtype":"success","response":{"rate_limits_available":true,"subscription_type":"max","rate_limits":{"five_hour":{"utilization":20}}}}})");
    check(result.messages.empty() && result.usage && result.usage->plan == "max" &&
              result.usage->windows.size() == 1 && result.usage->windows[0].remaining == 80,
          "Claude usage response is parsed");
    rejected(protocol, R"({"type":"control_response","response":{"request_id":"init","subtype":"error"}})");
    rejected(protocol, R"({"type":"control_response","response":{"request_id":"usage","subtype":"error"}})");
    rejected(protocol, R"({"type":"control_response"})");
    rejected(
        protocol,
        R"({"type":"control_response","response":{"request_id":"usage","subtype":"success","response":{}}})");
    rejected(protocol, "{");
}
} // namespace
int main() {
    try {
        codex_exchange();
        claude_exchange();
        std::cout << "Provider protocol exchanges passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
