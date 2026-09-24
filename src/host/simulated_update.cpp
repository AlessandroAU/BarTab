#include "host/simulated_update.hpp"
#include "host/platform.hpp"
#include <monocypher-ed25519.h>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <thread>

namespace usage::host {
namespace {
constexpr const char* feed_url = "https://simulated.test/releases/latest";
constexpr const char* binary_url = "https://simulated.test/BarTab-simulated";
constexpr const char* signature_url = "https://simulated.test/BarTab-simulated.sig";
constexpr const char* asset = "BarTab-simulated";

// Waits `duration` unless the updater is stopping. False when it is.
bool pause(std::chrono::milliseconds duration, const std::atomic<bool>& stop) {
    for (auto waited = std::chrono::milliseconds(0); waited < duration; waited += std::chrono::milliseconds(50)) {
        if (stop)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return !stop;
}
} // namespace

std::optional<Updater::Options> simulated_update(const std::filesystem::path& executable, SimulatedRelease release) {
    std::ifstream file(executable, std::ios::binary);
    std::string binary((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (binary.empty()) {
        log("Simulated update: could not read the running executable.");
        return std::nullopt;
    }

    // A key for this run only; the options trust it instead of the release key.
    std::uint8_t seed[32], secret[64];
    std::random_device random;
    for (auto& byte : seed)
        byte = static_cast<std::uint8_t>(random());
    Updater::Options options;
    crypto_ed25519_key_pair(secret, options.key.data(), seed);

    auto version = options.current;
    if (release != SimulatedRelease::Latest) {
        ++version.minor;
        version.patch = 0;
    }
    const auto signed_file = release == SimulatedRelease::Tampered ? binary + '!' : binary;
    const auto message = update::signed_message(asset, version, signed_file);
    std::string signature(64, '\0');
    crypto_ed25519_sign(reinterpret_cast<std::uint8_t*>(signature.data()), secret,
                        reinterpret_cast<const std::uint8_t*>(message.data()), message.size());
    crypto_wipe(secret, sizeof(secret));

    auto files = std::make_shared<std::map<std::string, std::string>>();
    (*files)[feed_url] = R"({"tag_name": "v)" + version.text() +
                         R"(", "html_url": "https://github.com/AlessandroAU/BarTab/releases", "assets": [
        {"name": "BarTab-simulated", "browser_download_url": ")" + std::string(binary_url) + R"("},
        {"name": "BarTab-simulated.sig", "browser_download_url": ")" + std::string(signature_url) + R"("}]})";
    (*files)[binary_url] = std::move(binary);
    (*files)[signature_url] = std::move(signature);
    const bool offline = release == SimulatedRelease::Offline;

    options.executable = executable;
    options.feed = feed_url;
    options.asset = asset;
    options.fetch = [files, offline](const std::string& url, const std::filesystem::path& destination,
                                     const std::atomic<bool>& stop) -> std::string {
        if (!pause(std::chrono::milliseconds(url == binary_url ? 2500 : 800), stop))
            return "the download was cancelled";
        if (offline)
            return "could not reach the server (simulated)";
        const auto found = files->find(url);
        if (found == files->end())
            return "the server answered HTTP 404";
        std::ofstream(destination, std::ios::binary | std::ios::trunc) << found->second;
        return {};
    };
    log(offline ? std::string("Simulating updates while offline.")
                : "Simulating updates: the fake release is " + version.text() +
                      (release == SimulatedRelease::Tampered ? ", with a bad signature." : "."));
    return options;
}
} // namespace usage::host
