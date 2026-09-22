#pragma once
#include "core/provider_protocol.hpp"
#include "windows/discovery.hpp"

namespace usage::windows {
DiscoveryResult find_service(Service service);
void detect_service(Service service, AccountUsage& result);
} // namespace usage::windows
