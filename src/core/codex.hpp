#pragma once
#include "core/usage.hpp"
#include <string_view>

namespace usage {
// Parse the result of account/rateLimits/read, not the JSON-RPC envelope.
AccountUsage parse_codex_limits(std::string_view json);
}
