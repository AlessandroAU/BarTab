#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <ctime>
#include "core/preferences.hpp"

namespace usage {
struct Rect {
    int x{}, y{}, width{}, height{};
    int right() const {
        return x + width;
    }
    int bottom() const {
        return y + height;
    }
    bool empty() const {
        return width <= 0 || height <= 0;
    }
    bool intersects(const Rect& other) const;
    bool operator==(const Rect& other) const;
};

// `position` is where along the taskbar the widget prefers to sit, as a
// percentage from the left edge: 0 hard left, 50 centred, 100 hard right. The
// widget lands in whichever free gap gets closest to that point, so a preference
// can never place it underneath a taskbar button.
Rect find_space(Rect panel, const std::vector<Rect>& occupied, int width, int height, int gap,
                int position = 100);

struct Allowance {
    std::string label;
    int remaining{};
    std::int64_t resets_at{};
};
struct AccountUsage {
    bool installed{true};
    std::vector<Allowance> windows;
    std::string plan;
    std::string executable_path;
    std::string error;
    std::time_t updated{};
};

class Usage {
  public:
    Usage() {
        claude.installed = false;
    }
    bool live{};
    bool codex_enabled{true}, claude_enabled{true};
    bool codex_active() const {
        return codex_enabled && codex.installed;
    }
    bool claude_active() const {
        return claude_enabled && claude.installed;
    }
    AccountUsage codex;
    AccountUsage claude;
    int session() const {
        return session_;
    }
    int weekly() const {
        return weekly_;
    }
    void set_session(int value);
    void set_weekly(int value);

  private:
    int session_{62};
    int weekly_{81};
};

struct Color {
    std::uint8_t r, g, b;
    bool operator==(Color other) const {
        return r == other.r && g == other.g && b == other.b;
    }
};
inline constexpr Color accent{90, 218, 184};
Color bar_color(int value);

inline constexpr int widget_width = 208, widget_height = 38;
} // namespace usage
