#pragma once
#include "core/provider_protocol.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace usage {
// A fake provider for the debug build and tests. It speaks each CLI's wire
// protocol, so a mocked reading still goes through ProviderProtocol and the
// real parsers rather than being poked into AccountUsage directly.
enum class MockState {
    Ready,      // Answers with the configured allowances.
    Missing,    // The CLI is not installed.
    LoginError, // The CLI runs but refuses the usage request.
    Connecting, // Installed, first reading still pending.
    Malformed,  // Answers the usage request with a reply the parser rejects.
};
struct MockAllowance {
    bool present{true};
    int used_percent{};
    int resets_in_minutes{};
};
struct MockProvider {
    MockState state{MockState::Ready};
    std::string plan;
    MockAllowance session, weekly;
    // Claude only: a model-scoped weekly window, "<model> weekly".
    MockAllowance model{false};
    std::string model_name{"Fable"};
    // Codex only: account credits and earned rate-limit resets; empty or
    // negative leaves them out of the reply.
    std::string credit_balance;
    int available_resets{-1};
};
struct MockScenario {
    MockProvider codex, claude;
};
struct MockPreset {
    const char* name;
    MockScenario scenario;
};
// Every provider combination the UI lays out differently, first one default.
const std::vector<MockPreset>& mock_presets();
const char* mock_state_name(MockState state);

// Answers one request line the way the provider would, returning the reply
// lines in order. `now` anchors the reset times.
class MockEndpoint {
  public:
    MockEndpoint(Service service, MockProvider provider, std::int64_t now)
        : service_(service), provider_(std::move(provider)), now_(now) {}
    std::vector<std::string> answer(std::string_view request) const;

  private:
    Service service_;
    MockProvider provider_;
    std::int64_t now_;
};
// Runs the whole exchange between ProviderProtocol and a MockEndpoint, as the
// reader does with a real process. Throws what the protocol throws. A
// Connecting provider returns an installed account with no windows yet.
AccountUsage read_mock(Service service, const MockProvider& provider, std::int64_t now);
} // namespace usage
