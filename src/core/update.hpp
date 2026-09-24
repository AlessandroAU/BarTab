#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Self-update, the portable half: which release is newest, and whether a
// download is one the release workflow signed. The host fetches and installs.
namespace usage::update {
struct Version {
    int major{}, minor{}, patch{};
    bool operator<(const Version& other) const {
        if (major != other.major)
            return major < other.major;
        if (minor != other.minor)
            return minor < other.minor;
        return patch < other.patch;
    }
    bool operator==(const Version& other) const {
        return major == other.major && minor == other.minor && patch == other.patch;
    }
    std::string text() const;
};
// "1.2.3" or "v1.2.3"; nothing else, so pre-release tags never count as newer.
std::optional<Version> parse_version(std::string_view text);

// This platform's release asset: the bare executable, installed in place of the
// running one. Empty where releases ship none.
std::string_view platform_asset();

struct Release {
    Version version;
    // The release's GitHub page, for the notes.
    std::string page_url;
    // The executable and its detached signature.
    std::string binary_url, signature_url;
};
// Reads GitHub's releases/latest reply. Nothing when it names no version, or
// lacks `asset` or its ".sig".
std::optional<Release> parse_release(std::string_view json, std::string_view asset);

// What the release workflow signs for each asset: its name, the version it
// claims and its SHA-512. Binding the name and version means a genuine older
// build cannot be passed off as a newer one, or one platform's as another's.
std::string signed_message(std::string_view asset, const Version& version, std::string_view file);
using PublicKey = std::array<std::uint8_t, 32>;
// The Ed25519 key release builds are signed with. Its private half is the
// BARTAB_SIGNING_KEY secret in GitHub Actions.
extern const PublicKey release_key;
// An Ed25519 signature: 64 bytes, as `openssl pkeyutl -sign -rawin` writes it.
bool verify(std::string_view message, std::string_view signature, const PublicKey& key);

enum class State {
    // This build does not update itself (the debug build, smoke tests).
    Unavailable,
    // Checking is switched off in settings.
    Off,
    // Waiting for the first check.
    Idle,
    Checking,
    UpToDate,
    Available,
    Downloading,
    // Downloaded and verified; installing restarts the app.
    Ready,
    Failed,
};
struct Status {
    State state{State::Unavailable};
    std::string current;
    // The newer version, while one is available, downloading or ready.
    std::string latest;
    std::string page_url;
    // Why the last check or download failed.
    std::string error;
    bool operator==(const Status& other) const {
        return state == other.state && current == other.current && latest == other.latest &&
               page_url == other.page_url && error == other.error;
    }
};
} // namespace usage::update
