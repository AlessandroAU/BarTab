#pragma once
#include "core/usage.hpp"
#include <cstdint>
#include <random>
#include <vector>

namespace usage::ui {
// A burst of paper confetti for celebrating a usage reset. Positions are in
// DIPs with y growing downward, in whatever space the caller emits into; the
// host draws each piece as a rotated rectangle.
struct ConfettiPiece {
    float x{}, y{}, vx{}, vy{};
    float angle{}, spin{};   // Degrees and degrees per second.
    float width{}, height{}; // Before the tumble squashes it.
    float tumble{}, tumble_speed{};
    float sway{}; // Side-to-side drift while falling, DIPs per second.
    float age{}, life{};
    Color color{};
    // Paper tumbling end over end: the visible width swings through zero.
    float visible_width() const;
    // Fully opaque until the last part of its life, then fading out.
    float opacity() const;
};

class Confetti {
  public:
    explicit Confetti(std::uint32_t seed = 0x5eed) : random_(seed) {}
    // Launches `count` pieces upward from the span [left, left + width] at `y`.
    void burst(float left, float width, float y, int count);
    void step(float seconds);
    bool done() const {
        return pieces_.empty();
    }
    const std::vector<ConfettiPiece>& pieces() const {
        return pieces_;
    }

  private:
    std::mt19937 random_;
    std::vector<ConfettiPiece> pieces_;
    float uniform(float low, float high);
};
} // namespace usage::ui
