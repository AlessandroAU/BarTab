#include "host/providers.hpp"
#include "ui/pixels.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace usage;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
// Polls until the session publishes something, as a host's timer does.
host::ProviderSession::Update wait(host::ProviderSession& session) {
    for (int i = 0; i < 250; ++i) {
        const auto update = session.poll();
        if (update.changed)
            return update;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return {};
}
const MockScenario& preset(const char* name) {
    for (const auto& entry : mock_presets())
        if (std::string(entry.name) == name)
            return entry.scenario;
    throw std::runtime_error("Missing mock preset");
}
void session_tests() {
    auto mock = std::make_shared<host::MockProviders>(preset("Codex only: 5 hour + weekly"));
    Usage usage;
    usage.live = true;
    host::ProviderSession session(usage, mock, false);
    session.detect();
    check(usage.codex.installed && !usage.claude.installed,
          "Detection follows the mock's installed providers");
    check(usage.codex.executable_path == host::mock_path(Service::Codex),
          "A mocked provider names its endpoint");
    Preferences preferences;
    usage.codex_enabled = true;
    usage.claude_enabled = false;
    session.apply(preferences);
    check(wait(session).changed && usage.codex.windows.size() == 2 && usage.codex.error.empty(),
          "An enabled provider's reader publishes a reading");
    check(!session.poll().changed, "A reading is taken once");

    mock->set(preset("Near the limits"));
    session.refresh();
    check(wait(session).changed && usage.codex.windows.front().remaining < 10, "Refresh reads again at once");
    mock->set(preset("Codex only: 5 hour + weekly"));
    session.refresh();
    const auto recovered = wait(session);
    check(recovered.changed && recovered.reset,
          "Allowance growing back past its reset time counts as a reset");

    usage.codex_enabled = false;
    session.apply(preferences);
    session.refresh();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    check(!session.poll().changed, "A disabled provider stops polling");

    Usage demo;
    host::ProviderSession fixed(demo, mock, true);
    const auto before = demo.codex.installed;
    fixed.detect();
    fixed.apply(preferences);
    check(demo.codex.installed == before && !fixed.poll().changed, "Demo data is never detected or polled");
    std::cout << "PASS: provider session\n";
}
ui::Pixels solid(int width, int height, std::uint32_t value) {
    return {width, height, std::vector<std::uint32_t>(static_cast<std::size_t>(width) * height, value)};
}
std::uint32_t at(const ui::Pixels& pixels, int x, int y) {
    return pixels.data[static_cast<std::size_t>(y) * pixels.width + x];
}
void pixel_tests() {
    auto card = solid(40, 30, 0xffffffffu);
    ui::round_corners(card, 8);
    check(at(card, 0, 0) == 0 && at(card, 39, 29) == 0, "Rounded corners clear the corner pixels");
    check(at(card, 20, 15) == 0xffffffffu && at(card, 0, 15) == 0xffffffffu, "Edges and middle stay opaque");
    const auto edge = at(card, 2, 2) >> 24;
    check(edge > 0 && edge < 255, "The curve is antialiased");
    auto clipped = solid(10, 10, 0xffffffffu);
    ui::round_corners(clipped, 0, 3, 7);
    check(at(clipped, 5, 2) == 0 && at(clipped, 5, 3) != 0 && at(clipped, 5, 7) == 0,
          "Rows outside the span clear");

    auto faded = solid(2, 2, 0xff804020u);
    ui::fade(faded, 0.5f);
    check(at(faded, 0, 0) == 0x80402010u, "Fading scales premultiplied channels together");

    auto text = solid(2, 1, 0);
    text.data[1] = 0xffffffffu;
    ui::fill_behind(text, {0x20, 0x20, 0x20}, 255);
    check(at(text, 0, 0) == 0xff202020u && at(text, 1, 0) == 0xffffffffu,
          "The backdrop shows only where uncovered");

    auto rows = solid(1, 4, 0);
    rows.data = {1, 2, 3, 4};
    ui::shift_rows(rows, 1);
    check(rows.data == std::vector<std::uint32_t>{0, 1, 2, 3}, "Shifting down clears the top row");
    ui::shift_rows(rows, -2);
    check(rows.data == std::vector<std::uint32_t>{2, 3, 0, 0}, "Shifting up clears the bottom rows");
    std::cout << "PASS: pixel effects\n";
}
} // namespace
int main() {
    try {
        session_tests();
        pixel_tests();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
