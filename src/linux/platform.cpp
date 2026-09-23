#include "host/platform.hpp"
#include <fontconfig/fontconfig.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#ifdef USAGETRACKER_HAVE_GIO
#include <gio/gio.h>
#endif

namespace usage::host {
namespace {
std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}
// $XDG_<kind>_HOME, or its default under the home folder.
std::filesystem::path xdg_directory(const char* variable, const char* fallback) {
    const auto value = environment(variable);
    if (!value.empty() && std::filesystem::path(value).is_absolute())
        return value;
    const auto home = environment("HOME");
    return home.empty() ? std::filesystem::path{} : std::filesystem::path(home) / fallback;
}

// What the desktop reports about its appearance, read at most every two
// seconds: hosts ask on every tick, and each answer is a D-Bus round trip.
struct Desktop {
    bool light{};
    Color accent{53, 132, 228};
    bool animations{true};
    std::string font_family;
};
#ifdef USAGETRACKER_HAVE_GIO
// One key from the XDG desktop portal's Settings interface, which GNOME, KDE
// and other desktops running xdg-desktop-portal all serve.
GVariant* portal_setting(GDBusConnection* bus, const char* space, const char* key) {
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        bus, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings", "ReadOne", g_variant_new("(ss)", space, key),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 250, nullptr, &error);
    if (!reply) {
        g_clear_error(&error);
        return nullptr;
    }
    GVariant* value = nullptr;
    g_variant_get(reply, "(v)", &value);
    g_variant_unref(reply);
    return value;
}
// A GSettings object for `schema` when the desktop installs it; GNOME's own
// settings are the fallback for what the portal does not carry.
GSettings* settings_for(const char* schema) {
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    if (!source)
        return nullptr;
    GSettingsSchema* found = g_settings_schema_source_lookup(source, schema, TRUE);
    if (!found)
        return nullptr;
    g_settings_schema_unref(found);
    return g_settings_new(schema);
}
#endif
Desktop read_desktop() {
    Desktop result;
#ifdef USAGETRACKER_HAVE_GIO
    static GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    static GSettings* interface = settings_for("org.gnome.desktop.interface");
    std::optional<std::uint32_t> scheme;
    if (bus) {
        // color-scheme: 0 no preference, 1 prefer dark, 2 prefer light.
        if (GVariant* value = portal_setting(bus, "org.freedesktop.appearance", "color-scheme")) {
            if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32))
                scheme = g_variant_get_uint32(value);
            g_variant_unref(value);
        }
        // accent-color: sRGB components in 0..1; out of range means unset.
        if (GVariant* value = portal_setting(bus, "org.freedesktop.appearance", "accent-color")) {
            double r = -1, g = -1, b = -1;
            if (g_variant_is_of_type(value, G_VARIANT_TYPE("(ddd)")))
                g_variant_get(value, "(ddd)", &r, &g, &b);
            if (r >= 0 && r <= 1 && g >= 0 && g <= 1 && b >= 0 && b <= 1)
                result.accent = {static_cast<std::uint8_t>(r * 255 + 0.5),
                                 static_cast<std::uint8_t>(g * 255 + 0.5),
                                 static_cast<std::uint8_t>(b * 255 + 0.5)};
            g_variant_unref(value);
        }
    }
    if (interface) {
        if (!scheme || *scheme == 0) {
            gchar* value = g_settings_get_string(interface, "color-scheme");
            scheme = std::string(value) == "prefer-dark" ? 1u : 2u;
            g_free(value);
        }
        result.animations = g_settings_get_boolean(interface, "enable-animations");
        gchar* font = g_settings_get_string(interface, "font-name");
        // "Cantarell 11": the family, without the trailing point size.
        std::string name = font;
        g_free(font);
        const auto space = name.find_last_of(' ');
        if (space != std::string::npos &&
            name.find_first_not_of("0123456789.", space + 1) == std::string::npos)
            name.resize(space);
        result.font_family = name;
    }
    // "No preference" draws light, as desktops do; no answer at all keeps the
    // dark theme the layouts were designed on.
    result.light = scheme.value_or(1) != 1;
#endif
    if (environment("GTK_THEME").find(":dark") != std::string::npos)
        result.light = false;
    return result;
}
Desktop desktop() {
    static std::mutex mutex;
    static Desktop cached;
    static std::chrono::steady_clock::time_point read{};
    std::lock_guard<std::mutex> lock(mutex);
    const auto now = std::chrono::steady_clock::now();
    if (read == std::chrono::steady_clock::time_point{} || now - read >= std::chrono::seconds(2)) {
        cached = read_desktop();
        read = now;
    }
    return cached;
}
std::vector<unsigned char> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
} // namespace

std::filesystem::path executable_path() {
    std::error_code error;
    return std::filesystem::read_symlink("/proc/self/exe", error);
}
std::filesystem::path executable_directory() {
    return executable_path().parent_path();
}
std::filesystem::path config_directory() {
    const auto base = xdg_directory("XDG_CONFIG_HOME", ".config");
    return base.empty() ? base : base / "UsageTracker";
}
std::filesystem::path log_path() {
    const auto base = xdg_directory("XDG_STATE_HOME", ".local/state");
    return base.empty() ? base : base / "UsageTracker" / "usagetracker.log";
}
// The panel and application windows share one color scheme on Linux desktops.
bool system_light_theme() {
    return desktop().light;
}
bool apps_light_theme() {
    return desktop().light;
}
bool animations_enabled() {
    return desktop().animations;
}
Color system_accent() {
    return desktop().accent;
}
FontFile ui_font(bool bold) {
    if (!FcInit())
        return {};
    const auto family = desktop().font_family;
    FcPattern* pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FAMILY,
                       reinterpret_cast<const FcChar8*>(family.empty() ? "sans-serif" : family.c_str()));
    FcPatternAddInteger(pattern, FC_WEIGHT, bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result{};
    FcPattern* match = FcFontMatch(nullptr, pattern, &result);
    FcPatternDestroy(pattern);
    FontFile font;
    if (match) {
        FcChar8* file = nullptr;
        int index = 0, weight = FC_WEIGHT_REGULAR;
        FcPatternGetInteger(match, FC_INDEX, 0, &index);
        FcPatternGetInteger(match, FC_WEIGHT, 0, &weight);
        // The index can name a variable font's instance, as GNOME's Cantarell
        // and Adwaita Sans ship their bold; the renderer opens it directly. A
        // family with no bold at all keeps the regular face: better unchanged
        // than smeared, as on Windows.
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch &&
            (!bold || weight >= FC_WEIGHT_DEMIBOLD)) {
            font.bytes = read_file(reinterpret_cast<const char*>(file));
            font.face_index = index;
        }
        FcPatternDestroy(match);
    }
    if (font.bytes.empty() && bold)
        return ui_font(false);
    return font;
}
} // namespace usage::host
