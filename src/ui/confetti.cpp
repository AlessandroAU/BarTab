#include "ui/confetti.hpp"
#include <algorithm>
#include <cmath>

namespace usage::ui {
namespace {
constexpr float gravity = 900.f; // DIPs per second squared.
constexpr float drag = 1.6f;     // Fraction of velocity shed per second, roughly.
constexpr float fade = 0.45f;    // Seconds of fade at the end of each life.
// Paper falls slowly: past this speed air resistance wins over gravity.
constexpr float terminal_speed = 260.f;
// The fan of launch directions either side of straight up, in degrees.
constexpr float spread_angle = 50.f;
constexpr Color palette[] = {{255, 94, 98},  {255, 196, 61}, {90, 218, 184}, {77, 166, 255},
                             {178, 120, 255}, {255, 138, 205}, {255, 255, 255}};
} // namespace

float ConfettiPiece::visible_width() const {
    return width * std::max(0.15f, std::abs(std::cos(tumble)));
}

float ConfettiPiece::opacity() const {
    return std::clamp((life - age) / fade, 0.f, 1.f);
}

float Confetti::uniform(float low, float high) {
    return std::uniform_real_distribution<float>(low, high)(random_);
}

void Confetti::burst(float left, float width, float y, int count) {
    for (int i = 0; i < count; ++i) {
        ConfettiPiece piece;
        // Launch from around the middle of the span in a fan of directions,
        // each at its own speed, so the burst spreads unevenly.
        piece.x = left + width / 2 + uniform(-0.2f, 0.2f) * width;
        piece.y = y + uniform(-2.f, 2.f);
        const float direction = uniform(-spread_angle, spread_angle) * 3.14159265f / 180.f;
        const float speed = uniform(360.f, 680.f);
        piece.vx = std::sin(direction) * speed;
        piece.vy = -std::cos(direction) * speed;
        piece.sway = uniform(20.f, 70.f);
        piece.angle = uniform(0.f, 360.f);
        piece.spin = uniform(-540.f, 540.f);
        piece.width = uniform(4.f, 7.f);
        piece.height = uniform(7.f, 11.f);
        piece.tumble = uniform(0.f, 6.2832f);
        piece.tumble_speed = uniform(6.f, 14.f);
        piece.life = uniform(1.5f, 2.3f);
        piece.color = palette[std::uniform_int_distribution<std::size_t>(0, std::size(palette) - 1)(random_)];
        pieces_.push_back(piece);
    }
}

void Confetti::step(float seconds) {
    seconds = std::clamp(seconds, 0.f, 0.05f);
    const float keep = std::exp(-drag * seconds);
    for (auto& piece : pieces_) {
        piece.age += seconds;
        piece.vy = std::min(terminal_speed, piece.vy + gravity * seconds);
        piece.vx *= keep;
        // Falling paper sways side to side with its tumble.
        piece.x += (piece.vx + (piece.vy > 0 ? std::sin(piece.tumble) * piece.sway : 0.f)) * seconds;
        piece.y += piece.vy * seconds;
        piece.angle += piece.spin * seconds;
        piece.tumble += piece.tumble_speed * seconds;
    }
    pieces_.erase(std::remove_if(pieces_.begin(), pieces_.end(),
                                 [](const ConfettiPiece& piece) { return piece.age >= piece.life; }),
                  pieces_.end());
}
} // namespace usage::ui
