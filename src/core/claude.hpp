#pragma once
#include "core/usage.hpp"
#include <string_view>
namespace usage {
AccountUsage parse_claude_limits(std::string_view json);
}
