#include "core/update.hpp"
#include <json.hpp>
#include <monocypher-ed25519.h>

namespace usage::update {
namespace {
std::optional<int> number(std::string_view& text) {
    if (text.empty() || text.front() < '0' || text.front() > '9')
        return std::nullopt;
    int value = 0;
    while (!text.empty() && text.front() >= '0' && text.front() <= '9') {
        if (value > 99999)
            return std::nullopt;
        value = value * 10 + (text.front() - '0');
        text.remove_prefix(1);
    }
    return value;
}
bool dot(std::string_view& text) {
    if (text.empty() || text.front() != '.')
        return false;
    text.remove_prefix(1);
    return true;
}
} // namespace

std::string Version::text() const {
    return std::to_string(major) + '.' + std::to_string(minor) + '.' + std::to_string(patch);
}

std::optional<Version> parse_version(std::string_view text) {
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
        text.remove_prefix(1);
    const auto major = number(text);
    if (!major || !dot(text))
        return std::nullopt;
    const auto minor = number(text);
    if (!minor || !dot(text))
        return std::nullopt;
    const auto patch = number(text);
    if (!patch || !text.empty())
        return std::nullopt;
    return Version{*major, *minor, *patch};
}

std::string_view platform_asset() {
#if defined(_WIN32) && defined(_M_X64)
    return "BarTab-windows-x64.exe";
#elif defined(__linux__) && defined(__x86_64__)
    return "BarTab-linux-x64";
#else
    return {};
#endif
}

std::optional<Release> parse_release(std::string_view json, std::string_view asset) {
    const auto reply = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
    if (!reply.is_object() || !reply.contains("tag_name") || !reply["tag_name"].is_string())
        return std::nullopt;
    const auto version = parse_version(reply["tag_name"].get<std::string>());
    if (!version || asset.empty())
        return std::nullopt;
    Release release;
    release.version = *version;
    if (reply.contains("html_url") && reply["html_url"].is_string())
        release.page_url = reply["html_url"].get<std::string>();
    const auto signature = std::string(asset) + ".sig";
    if (reply.contains("assets") && reply["assets"].is_array())
        for (const auto& entry : reply["assets"]) {
            if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string() ||
                !entry.contains("browser_download_url") || !entry["browser_download_url"].is_string())
                continue;
            const auto name = entry["name"].get<std::string>();
            const auto url = entry["browser_download_url"].get<std::string>();
            if (name == asset)
                release.binary_url = url;
            else if (name == signature)
                release.signature_url = url;
        }
    if (release.binary_url.empty() || release.signature_url.empty())
        return std::nullopt;
    return release;
}

std::string signed_message(std::string_view asset, const Version& version, std::string_view file) {
    std::uint8_t hash[64];
    crypto_sha512(hash, reinterpret_cast<const std::uint8_t*>(file.data()), file.size());
    static constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (const auto byte : hash) {
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 15]);
    }
    return "BarTab update v1\nasset=" + std::string(asset) + "\nversion=" + version.text() + "\nsha512=" + hex +
           "\n";
}

// Generated with `openssl genpkey -algorithm ed25519`; the public half is the
// last 32 bytes of `openssl pkey -pubout -outform DER`.
const PublicKey release_key{0xe4, 0xba, 0x2f, 0xcb, 0xba, 0xd3, 0xe9, 0xdf, 0x64, 0x89, 0x27,
                            0xa2, 0xd0, 0x7d, 0x86, 0x68, 0x83, 0x56, 0xdd, 0x39, 0x87, 0x7c,
                            0x44, 0xf1, 0x0b, 0x88, 0x92, 0x95, 0x19, 0x8d, 0x2a, 0xf8};

bool verify(std::string_view message, std::string_view signature, const PublicKey& key) {
    if (signature.size() != 64)
        return false;
    return crypto_ed25519_check(reinterpret_cast<const std::uint8_t*>(signature.data()), key.data(),
                                reinterpret_cast<const std::uint8_t*>(message.data()), message.size()) == 0;
}
} // namespace usage::update
