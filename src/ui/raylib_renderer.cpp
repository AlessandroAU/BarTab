#include "ui/raylib_renderer.hpp"
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <map>
#include <iterator>
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <raylib.h>
#include <rlgl.h>
#include <backends/raylib/clay-raylib-renderer.h>
#include <embedded-font.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace usage::ui {
struct Renderer::Impl {
    FontCache fonts;
    std::map<uint16_t,std::vector<unsigned char>> font_data;
    RenderTexture2D widget_texture{}, details_texture{};
    Impl() {
        SetTraceLogLevel(LOG_WARNING);
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(1,1,"UsageTracker rendering context");
        if (!IsWindowReady()) throw std::runtime_error("Could not create the raylib OpenGL context");
        fonts.fallback = GetFontDefault();
        fonts.ttfData = kEmbeddedRobotoTTF;
        fonts.ttfSize = static_cast<int>(kEmbeddedRobotoTTFSize);
        fonts.haveEmbedded = true;
    }
    ~Impl() {
        if (widget_texture.id) UnloadRenderTexture(widget_texture);
        if (details_texture.id) UnloadRenderTexture(details_texture);
        FontCache_Unload(fonts);
        CloseWindow();
    }
};
Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;
bool Renderer::load_font(uint16_t id, const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    if (!file) return false;
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    if (bytes.empty()) return false;
    auto& data=impl_->font_data[id]; data=std::move(bytes);
    return FontCache_Register(impl_->fonts,id,data.data(),static_cast<int>(data.size()));
}
void Renderer::set_scale(float scale) { impl_->fonts.dpiScale = scale; }
Clay_Dimensions Renderer::measure(Clay_StringSlice text, Clay_TextElementConfig* config) {
    return MeasureTextRaylib(text,config,&impl_->fonts);
}
Clay_Dimensions Renderer::measure_callback(Clay_StringSlice text, Clay_TextElementConfig* config, void* user) {
    return static_cast<Renderer*>(user)->measure(text,config);
}
Pixels Renderer::render(Clay_RenderCommandArray commands, int width, int height, float scale, bool hit_background) {
    auto& target = hit_background ? impl_->widget_texture : impl_->details_texture;
    if (!target.id || target.texture.width != width || target.texture.height != height) {
        if (target.id) UnloadRenderTexture(target);
        target = LoadRenderTexture(width,height);
        if (!target.id) throw std::runtime_error("Could not allocate UI render texture");
    }
    impl_->fonts.dpiScale = scale;
    BeginTextureMode(target);
    ClearBackground(::Color{0,0,0,0});
    // Accumulate alpha correctly; RGB output is already premultiplied on black.
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA,RL_ONE_MINUS_SRC_ALPHA,RL_ONE,RL_ONE_MINUS_SRC_ALPHA,RL_FUNC_ADD,RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    rlPushMatrix();
    rlScalef(scale,scale,1);
    RenderClayCommands(commands,impl_->fonts,scale);
    rlPopMatrix();
    EndBlendMode();
    EndTextureMode();
    Image image = LoadImageFromTexture(target.texture);
    if (!image.data) throw std::runtime_error("Could not read UI render texture");
    ImageFlipVertical(&image);
    ImageFormat(&image,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Pixels result{width,height,{}};
    result.data.resize(static_cast<std::size_t>(width) * height);
    const auto* rgba = static_cast<const unsigned char*>(image.data);
    for (std::size_t i = 0; i < result.data.size(); ++i) {
        const auto* p = rgba + i * 4;
        result.data[i] = (static_cast<std::uint32_t>(p[3]) << 24) | (static_cast<std::uint32_t>(p[0]) << 16)
            | (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
        if (hit_background && p[3] == 0) result.data[i] = 0x01000000;
    }
    UnloadImage(image);
    return result;
}
bool Renderer::save_png(const Pixels& pixels, const std::filesystem::path& path, std::uint32_t background) {
    if (pixels.width <= 0 || pixels.height <= 0) return false;
    const auto back_r = static_cast<int>((background >> 16) & 0xff);
    const auto back_g = static_cast<int>((background >> 8) & 0xff);
    const auto back_b = static_cast<int>(background & 0xff);
    std::vector<unsigned char> rgba(static_cast<std::size_t>(pixels.width) * pixels.height * 4);
    for (std::size_t i = 0; i < pixels.data.size(); ++i) {
        const auto value = pixels.data[i];
        // The widget surface marks its transparent hit-test area as 0x01000000.
        const int alpha = static_cast<int>((value >> 24) & 0xff) <= 1 ? 0 : static_cast<int>((value >> 24) & 0xff);
        const int source_r = static_cast<int>((value >> 16) & 0xff);
        const int source_g = static_cast<int>((value >> 8) & 0xff);
        const int source_b = static_cast<int>(value & 0xff);
        // Source RGB is premultiplied, so compositing is source + backdrop*(1-a).
        const auto over = [&](int source, int back) {
            return static_cast<unsigned char>(std::clamp(source + back * (255 - alpha) / 255, 0, 255));
        };
        rgba[i*4+0] = over(alpha ? source_r : 0, back_r);
        rgba[i*4+1] = over(alpha ? source_g : 0, back_g);
        rgba[i*4+2] = over(alpha ? source_b : 0, back_b);
        rgba[i*4+3] = 255;
    }
    Image image{rgba.data(),pixels.width,pixels.height,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    return ExportImage(image,path.string().c_str());
}
} // namespace usage::ui
