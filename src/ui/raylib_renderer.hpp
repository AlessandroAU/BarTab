#pragma once
#include "ui/views.hpp"
#include <cstdint>
#include <memory>
#include <vector>
#include <filesystem>

namespace usage::ui {
struct Pixels {
    int width{}, height{};
    // Top-down, premultiplied BGRA for the Windows compositor.
    std::vector<std::uint32_t> data;
};
class Renderer {
  public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Clay_Dimensions measure(Clay_StringSlice, Clay_TextElementConfig*);
    // Call before measuring or rendering a view: the DPI scale picks the atlas
    // pixel size and the view's text gamma (View::text_gamma) picks its curve.
    // One call for both so a surface cannot render with the other's atlas.
    void set_surface(float scale, float text_gamma);
    bool load_font_data(uint16_t id, std::vector<unsigned char> bytes);
    bool load_font(uint16_t id, const std::filesystem::path& path);
    Pixels render(Clay_RenderCommandArray, int width, int height, float scale, bool hit_background);
    // Composites the premultiplied result over an opaque 0xRRGGBB backdrop and
    // writes a PNG. Used by the screenshot tool to regenerate the README images.
    static bool save_png(const Pixels& pixels, const std::filesystem::path& path, std::uint32_t background);
    static Clay_Dimensions measure_callback(Clay_StringSlice text, Clay_TextElementConfig* config,
                                            void* user);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace usage::ui
