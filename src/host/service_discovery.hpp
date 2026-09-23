#pragma once
#include "core/provider_protocol.hpp"
#include "host/discovery.hpp"

namespace usage::host {
// Where this platform installs each CLI, most specific first. Implemented by
// src/windows/locations.cpp and src/posix/locations.cpp.
DiscoveryLocations search_locations(Service service);
DiscoveryResult find_service(Service service);
void detect_service(Service service, AccountUsage& result);
} // namespace usage::host
