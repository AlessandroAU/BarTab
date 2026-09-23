#include "host/service_discovery.hpp"
#include <exception>

namespace usage::host {
DiscoveryResult find_service(Service service) {
    return discover_executable(service == Service::Claude, search_locations(service));
}
void detect_service(Service service, AccountUsage& result) {
    try {
        const auto detection = find_service(service);
        const auto& path = detection.path;
        if (path.empty() || !result.installed)
            result.error = detection.error;
        result.installed = !path.empty();
        const auto utf8 = path.u8string();
        result.executable_path.assign(utf8.begin(), utf8.end());
    } catch (const std::exception& error) {
        result.installed = false;
        result.executable_path.clear();
        result.error = error.what();
    }
}
} // namespace usage::host
