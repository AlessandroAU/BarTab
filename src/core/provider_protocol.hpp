#pragma once
#include "core/usage.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace usage {
enum class Service { Codex, Claude };
struct ProtocolReply {
    std::vector<std::string> messages;
    std::optional<AccountUsage> usage;
};
// Consumes complete JSON lines. Framing, process lifetime and deadlines belong to the transport.
class ProviderProtocol {
  public:
    explicit ProviderProtocol(Service service) : service_(service) {}
    // Command-line arguments after the executable, UTF-8.
    std::vector<std::string> arguments() const;
    std::string initialize() const;
    ProtocolReply receive(std::string_view line) const;

  private:
    Service service_;
};
} // namespace usage
