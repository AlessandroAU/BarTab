#pragma once
#include "core/usage.hpp"
#include <optional>
#include <string_view>

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
    std::wstring arguments() const;
    std::string initialize() const;
    ProtocolReply receive(std::string_view line) const;

  private:
    Service service_;
};
} // namespace usage
