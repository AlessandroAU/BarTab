#include "host/simulated_update.hpp"
#include "host/updater.hpp"
#include <monocypher-ed25519.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <thread>

namespace {
using namespace usage;
using update::State;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes;
}
std::string bytes(std::initializer_list<unsigned> values) {
    std::string result;
    for (const auto value : values)
        result.push_back(static_cast<char>(value));
    return result;
}

void version_tests() {
    check(update::parse_version("v1.2.3") == update::Version{1, 2, 3}, "A tag parses");
    check(update::parse_version("0.10.0") == update::Version{0, 10, 0}, "A bare version parses");
    for (const auto* bad : {"", "v", "1.2", "1.2.3.4", "1.2.3-beta", "v1.2.x", " 1.2.3", "1..3"})
        check(!update::parse_version(bad), "Only plain X.Y.Z versions parse");
    check(update::Version{0, 9, 9} < update::Version{0, 10, 0}, "Versions compare numerically");
    check(!(update::Version{1, 0, 0} < update::Version{1, 0, 0}), "Equal versions are not newer");
    check(update::Version{1, 2, 3}.text() == "1.2.3", "Versions print without the v");
    std::cout << "PASS: versions\n";
}

constexpr const char* feed_json = R"({
  "tag_name": "v1.2.3",
  "html_url": "https://github.com/AlessandroAU/BarTab/releases/tag/v1.2.3",
  "assets": [
    {"name": "BarTab-windows-x64.zip", "browser_download_url": "https://example.test/win.zip"},
    {"name": "BarTab-linux-x64", "browser_download_url": "https://example.test/linux"},
    {"name": "BarTab-linux-x64.sig", "browser_download_url": "https://example.test/linux.sig"}
  ]
})";

void release_tests() {
    const auto release = update::parse_release(feed_json, "BarTab-linux-x64");
    check(release && release->version == update::Version{1, 2, 3}, "The release's version is its tag");
    check(release->binary_url == "https://example.test/linux" &&
              release->signature_url == "https://example.test/linux.sig",
          "The asset and its signature are found by name");
    check(release->page_url.find("/releases/tag/v1.2.3") != std::string::npos, "The release page is kept");
    check(!update::parse_release(feed_json, "BarTab-windows-x64.exe"),
          "A release without this platform's executable and signature offers nothing");
    check(!update::parse_release("not json", "BarTab-linux-x64"), "A garbled reply offers nothing");
    check(!update::parse_release(R"({"tag_name": "nightly", "assets": []})", "BarTab-linux-x64"),
          "A tag that is not a version offers nothing");
    std::cout << "PASS: release feed\n";
}

// Signed by `openssl pkeyutl -sign -rawin` with a throwaway key, over the
// message the release workflow builds with printf and sha512sum, so this
// proves the workflow's signatures verify here.
void signature_tests() {
    const update::PublicKey key{0x1e, 0x1a, 0x3d, 0x20, 0x70, 0x63, 0xf7, 0x04, 0xcf, 0x45, 0xd0,
                                0xd3, 0x4d, 0xae, 0x86, 0x35, 0x44, 0x10, 0xce, 0xd0, 0xf0, 0xd8,
                                0x06, 0x16, 0x89, 0x1d, 0x04, 0xf5, 0x6a, 0xc5, 0xf6, 0x3b};
    const auto signature =
        bytes({0xb6, 0x95, 0xb1, 0x34, 0x7a, 0x1b, 0x1e, 0x80, 0x11, 0x42, 0x29, 0xd8, 0x69, 0x40, 0xd8, 0xa0,
               0x16, 0xbe, 0x42, 0xae, 0xed, 0x9c, 0x2c, 0x87, 0xbf, 0x76, 0x9f, 0x9c, 0x4c, 0xda, 0x97, 0xe5,
               0xde, 0x90, 0xb1, 0xe2, 0xef, 0x82, 0xf8, 0x8a, 0xdd, 0xe7, 0x14, 0xfd, 0x73, 0x9d, 0xa9, 0xfd,
               0xa1, 0x9f, 0xcf, 0xbc, 0xe0, 0xcc, 0xa0, 0xeb, 0xf3, 0x74, 0xa6, 0x79, 0xaf, 0x2a, 0xda, 0x01});
    const std::string file = "fake binary\n";
    const update::Version version{1, 2, 3};
    const auto message = update::signed_message("BarTab-linux-x64", version, file);
    check(message.rfind("BarTab update v1\nasset=BarTab-linux-x64\nversion=1.2.3\nsha512=", 0) == 0 &&
              message.size() == 61 + 128 + 1,
          "The signed message names the asset, the version and the file's hash");
    check(update::verify(message, signature, key), "An OpenSSL signature verifies");
    check(!update::verify(update::signed_message("BarTab-linux-x64", version, "fake binarY\n"), signature, key),
          "A changed file does not verify");
    check(!update::verify(update::signed_message("BarTab-linux-x64", {1, 2, 4}, file), signature, key),
          "The same file claiming another version does not verify");
    check(!update::verify(update::signed_message("BarTab-windows-x64.exe", version, file), signature, key),
          "The same file under another asset name does not verify");
    check(!update::verify(message, signature, update::release_key), "Another key does not verify");
    check(!update::verify(message, signature.substr(0, 63), key), "A truncated signature does not verify");
    std::cout << "PASS: signatures\n";
}

// A release server in memory, and a signing key that stands in for the
// release workflow's.
struct FakeRelease {
    std::uint8_t secret[64]{};
    update::PublicKey key{};
    std::map<std::string, std::string> files;
    bool offline{};
    FakeRelease() {
        std::uint8_t seed[32];
        for (int i = 0; i < 32; ++i)
            seed[i] = static_cast<std::uint8_t>(i * 7 + 1);
        crypto_ed25519_key_pair(secret, key.data(), seed);
    }
    void publish(const char* tag, const std::string& binary, const std::string& asset = "BarTab-test") {
        const auto version = *update::parse_version(tag);
        const auto message = update::signed_message(asset, version, binary);
        std::string signature(64, '\0');
        crypto_ed25519_sign(reinterpret_cast<std::uint8_t*>(signature.data()), secret,
                            reinterpret_cast<const std::uint8_t*>(message.data()), message.size());
        files["https://fake.test/feed"] = std::string(R"({"tag_name": ")") + tag +
                                          R"(", "html_url": "https://fake.test/page", "assets": [
            {"name": "BarTab-test", "browser_download_url": "https://fake.test/binary"},
            {"name": "BarTab-test.sig", "browser_download_url": "https://fake.test/binary.sig"}]})";
        files["https://fake.test/binary"] = binary;
        files["https://fake.test/binary.sig"] = signature;
    }
    host::Updater::Fetch fetch() {
        return [this](const std::string& url, const std::filesystem::path& destination,
                      const std::atomic<bool>&) -> std::string {
            if (offline)
                return "offline";
            const auto found = files.find(url);
            if (found == files.end())
                return "the server answered HTTP 404";
            write(destination, found->second);
            return {};
        };
    }
};

// Polls until the updater settles on `state`, as a host's timer does.
update::Status wait_for(host::Updater& updater, State state) {
    update::Status status;
    for (int i = 0; i < 250; ++i) {
        updater.take(status);
        if (status.state == state)
            return status;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    throw std::runtime_error("The updater never reached the expected state");
}

void updater_tests(const std::filesystem::path& folder) {
    FakeRelease server;
    const auto executable = folder / "BarTab-test";
    write(executable, "running build");
    auto options = [&] {
        host::Updater::Options value;
        value.current = {1, 0, 0};
        value.executable = executable;
        value.feed = "https://fake.test/feed";
        value.asset = "BarTab-test";
        value.key = server.key;
        value.fetch = server.fetch();
        value.first_check = std::chrono::milliseconds(0);
        return value;
    };

    // GitHub's answer before the first release.
    {
        host::Updater updater(options());
        updater.set_policy(true, false);
        const auto status = wait_for(updater, State::UpToDate);
        check(status.error.empty(), "No release yet is not an error");
    }
    std::cout << "PASS: no release yet\n";

    server.publish("v1.0.0", "same build");
    {
        host::Updater updater(options());
        update::Status status;
        updater.take(status);
        check(status.state == State::Off && status.current == "1.0.0", "Checking waits for the preference");
        updater.set_policy(true, false);
        wait_for(updater, State::UpToDate);
    }
    std::cout << "PASS: no newer release\n";

    server.publish("v1.1.0", "new build");
    {
        host::Updater updater(options());
        updater.set_policy(true, false);
        const auto status = wait_for(updater, State::Available);
        check(status.latest == "1.1.0" && status.page_url == "https://fake.test/page",
              "A newer release is offered with its page");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        check(updater.staged().empty() && !std::filesystem::exists(host::staged_update(executable)),
              "Nothing downloads until asked");
        updater.download_update();
        wait_for(updater, State::Ready);
        check(read(updater.staged()) == "new build", "The verified download is staged beside the executable");
        updater.check_now();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        update::Status after;
        check(!updater.take(after) && !updater.staged().empty(), "A ready download is not checked away");
    }
    host::remove_update_leftovers(executable);
    std::cout << "PASS: download on request\n";

    {
        host::Updater updater(options());
        updater.set_policy(true, true);
        wait_for(updater, State::Ready);
    }
    host::remove_update_leftovers(executable);
    std::cout << "PASS: automatic download\n";

    server.files["https://fake.test/binary"] = "tampered build";
    {
        host::Updater updater(options());
        updater.set_policy(true, true);
        const auto failed = [&] {
            update::Status value;
            for (int i = 0; i < 250; ++i) {
                if (updater.take(value) && value.state == State::Available && !value.error.empty())
                    return value;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            throw std::runtime_error("The tampered download was never rejected");
        }();
        check(failed.error.find("signature") != std::string::npos, "A tampered download is rejected");
        check(!std::filesystem::exists(host::staged_update(executable)), "A rejected download is deleted");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        update::Status again;
        check(!updater.take(again), "An automatic download is not retried in a loop");
    }
    std::cout << "PASS: tampered download\n";

    server.offline = true;
    {
        host::Updater updater(options());
        updater.set_policy(true, false);
        const auto status = wait_for(updater, State::Failed);
        check(status.error.find("offline") != std::string::npos, "A failed check says why");
        updater.set_policy(false, false);
        wait_for(updater, State::Off);
    }
    std::cout << "PASS: offline\n";
}

// The debug build's fake release server, through the real updater.
void simulation_tests(const std::filesystem::path& folder) {
    const auto executable = folder / "BarTab-simulated";
    write(executable, "running build");
    using host::SimulatedRelease;
    const auto options = [&](SimulatedRelease release) {
        auto value = host::simulated_update(executable, release);
        check(value.has_value(), "The simulation reads the running executable");
        value->first_check = std::chrono::milliseconds(0);
        return std::move(*value);
    };
    auto newer = host::current_version();
    ++newer.minor;
    newer.patch = 0;
    {
        host::Updater updater(options(SimulatedRelease::Newer));
        updater.set_policy(true, false);
        check(wait_for(updater, State::Available).latest == newer.text(), "The simulated release is newer");
        updater.download_update();
        wait_for(updater, State::Ready);
        check(read(updater.staged()) == "running build", "The simulated release is the running executable");
    }
    host::remove_update_leftovers(executable);
    {
        host::Updater updater(options(SimulatedRelease::Latest));
        updater.set_policy(true, false);
        wait_for(updater, State::UpToDate);
    }
    {
        host::Updater updater(options(SimulatedRelease::Offline));
        updater.set_policy(true, false);
        check(!wait_for(updater, State::Failed).error.empty(), "An offline simulation fails its check");
    }
    {
        host::Updater updater(options(SimulatedRelease::Tampered));
        updater.set_policy(true, false);
        wait_for(updater, State::Available);
        updater.download_update();
        update::Status status;
        for (int i = 0; i < 250 && status.error.empty(); ++i) {
            updater.take(status);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        check(status.state == State::Available && status.error.find("signature") != std::string::npos,
              "A tampered simulated release is rejected");
    }
    std::cout << "PASS: simulated updates\n";
}

void install_tests(const std::filesystem::path& folder) {
    const auto executable = folder / "BarTab-install";
    const auto staged = host::staged_update(executable);
    write(executable, "old");
    write(staged, "new");
    check(host::install_update(staged, executable).empty(), "Installing succeeds");
    check(read(executable) == "new" && !std::filesystem::exists(staged), "The new build takes the old one's place");
    host::remove_update_leftovers(executable);
    for (const auto& entry : std::filesystem::directory_iterator(folder))
        check(entry.path().filename().string().rfind("BarTab-install", 0) != 0 || entry.path() == executable,
              "Nothing is left beside the executable");
    std::cout << "PASS: install\n";
}
} // namespace

int main() {
    const auto folder = std::filesystem::temp_directory_path() /
                        ("bartab-update-test-" + std::to_string(std::chrono::steady_clock::now()
                                                                    .time_since_epoch()
                                                                    .count()));
    std::filesystem::create_directories(folder);
    int result = 0;
    try {
        version_tests();
        release_tests();
        signature_tests();
        updater_tests(folder);
        simulation_tests(folder);
        install_tests(folder);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
    return result;
}
