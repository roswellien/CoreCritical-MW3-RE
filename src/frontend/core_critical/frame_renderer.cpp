#include "frame_renderer.hpp"

#include "frontend/sdl/sdl_child.h"
#include "vk_scene.hpp"
#include <SDL3/SDL_vulkan.h>

#undef DrawState // Win32's DrawState macro conflicts with platform::DrawState.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cc_mw3_re {
namespace {

namespace vk = mw34::vkscene;
using platform::DrawState;
using platform::Primitive;
using platform::TLVertex;

void set_option(const char *name, const char *value) {
    if (_putenv_s(name, value) != 0) {
        throw std::runtime_error(std::string("Could not set renderer option ") + name);
    }
}

void configure_bridge(bool native_wsi) {
    set_option("MW34_DXGI_PRESENT", native_wsi ? "0" : "1");
    set_option("MW34_WINDOW_MODE", "windowed");
    set_option("MW34_WORLD_SCALE_PERCENT", "100");
    set_option("MW34_REMASTER_WIDTH", "");
    set_option("MW34_REMASTER_HEIGHT", "");
    set_option("MW34_REMASTER_AA", "1");
    set_option("MW34_REMASTER_AF", "1");
    set_option("MW34_TEMPORAL_INPUTS", "0");
    set_option("MW34_GPU_MODELS", "0");
    set_option("MW34_SUN", "0");
    set_option("MW34_SUN_NORMALS", "0");
    set_option("MW34_SUN_SHADOWS", "0");
    set_option("MW34_EFFECT_LIGHTS", "0");
    set_option("MW34_FIXTURE_SHADOWS", "0");
}

vk::Renderer::WindowSurface sdl_surface(SDL_Window *window) {
    if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_VULKAN))
        throw std::runtime_error("SDL Vulkan surface: window requires SDL_WINDOW_VULKAN");
    Uint32 count = 0;
    const auto *extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!extensions)
        throw std::runtime_error(std::string("SDL Vulkan extensions: ") + SDL_GetError());
    vk::Renderer::WindowSurface source;
    source.extensions.assign(extensions, extensions + count);
    source.create = [window](VkInstance instance) {
        VkSurfaceKHR surface{};
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
            throw std::runtime_error(std::string("SDL Vulkan surface: ") + SDL_GetError());
        return surface;
    };
    source.destroy = [](VkInstance instance, VkSurfaceKHR surface) {
        SDL_Vulkan_DestroySurface(instance, surface, nullptr);
    };
    return source;
}

constexpr unsigned no_binding = std::numeric_limits<unsigned>::max();

struct Texture {
    unsigned width{}, height{};
    std::uint64_t version{};
    unsigned scene_index = no_binding;
    unsigned sharp_index = no_binding;
    std::vector<std::uint32_t> pixels;
};

std::vector<std::uint32_t> sharp_layer(const Texture &texture, unsigned width, unsigned height) {
    // SDL samples premultiplied pixels with a narrowed bilinear footprint, then
    // unpremultiplies. Pre-filter just this 2D layer at its actual raster size.
    auto coordinate = [](unsigned at, unsigned input, unsigned output) {
        const float scale = std::max(float(output) / float(input), 1.0f);
        const float texel = (float(at) + .5f) * float(input) / float(output);
        const float centre = std::floor(texel) + .5f;
        const float delta = texel - centre;
        const float radius = .5f - .5f / scale;
        return centre + (delta - std::clamp(delta, -radius, radius)) * scale - .5f;
    };
    std::vector<std::uint32_t> result(std::size_t(width) * height);
    auto pixel = [&](int x, int y) {
        x = std::clamp(x, 0, int(texture.width) - 1);
        y = std::clamp(y, 0, int(texture.height) - 1);
        return texture.pixels[std::size_t(y) * texture.width + x];
    };
    for (unsigned y = 0; y < height; ++y) {
        const float sy = coordinate(y, texture.height, height);
        const int top = int(std::floor(sy));
        for (unsigned x = 0; x < width; ++x) {
            const float sx = coordinate(x, texture.width, width);
            const int left = int(std::floor(sx));
            const float fx = sx - float(left), fy = sy - float(top);
            const auto a = pixel(left, top), b = pixel(left + 1, top);
            const auto c = pixel(left, top + 1), d = pixel(left + 1, top + 1);
            const auto channel = [&](unsigned shift) {
                const auto value = [&](std::uint32_t p) { return float((p >> shift) & 255); };
                return std::lerp(std::lerp(value(a), value(b), fx),
                                 std::lerp(value(c), value(d), fx), fy);
            };
            const float alpha = channel(24);
            std::uint32_t rgba = std::uint32_t(std::lround(alpha)) << 24;
            if (alpha > 0) {
                for (unsigned shift : {0U, 8U, 16U}) {
                    rgba |=
                        std::uint32_t(std::lround(std::min(255.f, channel(shift) * 255.f / alpha)))
                        << shift;
                }
            }
            result[std::size_t(y) * width + x] = rgba;
        }
    }
    return result;
}

class FrameRenderer final : public VulkanFrameRenderer {
  public:
    explicit FrameRenderer(SDL_Window *window, sdlfe::RendererBackend backend)
        : window_(window),
          hwnd_(static_cast<HWND>(SDL_GetPointerProperty(
              SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))),
          native_wsi_(backend == sdlfe::RendererBackend::core_critical_wsi) {
        if (!hwnd_) {
            throw std::runtime_error("SDL window has no Win32 handle");
        }
        if (backend == sdlfe::RendererBackend::sdl)
            throw std::runtime_error("The Core Critical factory requires a Vulkan backend");
        configure_bridge(native_wsi_);
        initialize_renderer();
    }

    bool ok() const override { return true; }
    std::uint64_t presentation_count() const override { return presented_frames_; }
    const char *driver_name() const override {
        return native_wsi_ ? "Core Critical Vulkan/SDL WSI" : "Core Critical Vulkan/DXGI";
    }
    void set_shape(const sdlfe::FrameShape *shape) override { shape_ = shape; }
    void reset_scene_resources() override { reset_pending_ = true; }

    ~FrameRenderer() override {
        std::fprintf(
            stderr, "%s: %llu draws, %llu resident presents\n", driver_name(),
            static_cast<unsigned long long>(retired_draws_ + renderer_->draws),
            static_cast<unsigned long long>(retired_presents_ + renderer_->resident_presents));
    }

    void capture_next(const char *path) override { capture_path_ = path; }
    void viewport(int *x, int *y, int *width, int *height) const override {
        *x = viewport_[0];
        *y = viewport_[1];
        *width = viewport_[2];
        *height = viewport_[3];
    }
    void set_viewport(int x, int y, int width, int height) override {
        if (width < 1 || height < 1) {
            throw std::runtime_error("Invalid mw3-re viewport");
        }
        viewport_ = {x, y, width, height};
    }

    int32_t create_texture(int width, int height, const uint8_t *rgba, uint8_t) override {
        if (width <= 0 || height <= 0 || unsigned(width) > texture_limit_ ||
            unsigned(height) > texture_limit_) {
            throw std::runtime_error("Invalid mw3-re texture size");
        }
        const auto free = std::find_if(textures_.begin(), textures_.end(),
                                       [](const Texture &entry) { return entry.pixels.empty(); });
        const auto handle = int32_t(free - textures_.begin());
        if (free == textures_.end()) {
            textures_.emplace_back();
        }
        auto &entry = textures_[handle];
        entry.width = unsigned(width);
        entry.height = unsigned(height);
        update_texture(handle, width, height, rgba);
        return handle;
    }

    void update_texture(int32_t handle, int width, int height, const uint8_t *rgba) override {
        auto &entry = textures_.at(handle);
        if (!rgba || unsigned(width) != entry.width || unsigned(height) != entry.height) {
            throw std::runtime_error("Invalid mw3-re texture upload");
        }
        if (entry.scene_index != no_binding || entry.sharp_index != no_binding) {
            flush();
        }
        entry.pixels.resize(std::size_t(width) * height);
        std::memcpy(entry.pixels.data(), rgba, entry.pixels.size() * sizeof(std::uint32_t));
        entry.version = ++texture_version_;
    }

    void release_texture(int32_t handle) override {
        auto &entry = textures_.at(handle);
        if (entry.scene_index != no_binding || entry.sharp_index != no_binding) {
            flush();
        }
        entry = {};
    }

    void clear(bool color, uint8_t red, uint8_t green, uint8_t blue) override {
        ensure_extent();
        append_clear(color, red, green, blue);
    }

    void set_fog_color(uint8_t red, uint8_t green, uint8_t blue) override {
        fog_ = {red / 255.0f, green / 255.0f, blue / 255.0f};
    }
    void begin_scene() override { ensure_extent(); }
    void end_scene() override { flush(); }

    void draw(Primitive primitive, const TLVertex *vertices, int count,
              const DrawState &state) override {
        if (count <= 0) {
            return;
        }
        ensure_extent();
        const unsigned first = unsigned(scene_.vertices.size() / sizeof(TLVertex));
        const auto add = [&](TLVertex vertex) {
            vertex.sx = (vertex.sx - viewport_[0]) * scene_.width / float(viewport_[2]);
            vertex.sy = (vertex.sy - viewport_[1]) * scene_.height / float(viewport_[3]);
            if (vertex.rhw <= 0) {
                vertex.rhw = 1;
            }
            const auto *bytes = reinterpret_cast<const unsigned char *>(&vertex);
            scene_.vertices.insert(scene_.vertices.end(), bytes, bytes + sizeof(TLVertex));
        };
        if (primitive == Primitive::kFan) {
            for (int i = 1; i + 1 < count; ++i) {
                add(vertices[0]);
                add(vertices[i]);
                add(vertices[i + 1]);
            }
        } else if (primitive == Primitive::kStrip) {
            for (int i = 0; i + 2 < count; ++i) {
                add(vertices[i + (i & 1)]);
                add(vertices[i + 1 - (i & 1)]);
                add(vertices[i + 2]);
            }
        } else if (primitive == Primitive::kPoints) {
            for (int i = 0; i < count; ++i) {
                TLVertex a = vertices[i], b = a, c = a, d = a;
                b.sx += 1;
                c.sy += 1;
                d.sx += 1;
                d.sy += 1;
                add(a);
                add(b);
                add(c);
                add(b);
                add(d);
                add(c);
            }
        } else {
            throw std::runtime_error("Unsupported mw3-re draw primitive");
        }
        const unsigned drawn = unsigned(scene_.vertices.size() / sizeof(TLVertex)) - first;
        if (!drawn) {
            return;
        }
        if (state.wireframe) {
            throw std::runtime_error("Wireframe is unavailable in the Vulkan Training trial");
        }

        vk::Draw draw{};
        draw.first = first;
        draw.count = drawn;
        draw.texture = state.texture < 0 ? -1 : int(bind_texture(state.texture, state.sharp));
        draw.sampler = (state.address_u == 3 ? 3U : 1U) << 4 |
                       (state.address_v == 3 ? 3U : 1U) << 8 | (state.sharp ? 0U : 3U);
        draw.key = {uint8_t(state.blend || state.multiply),
                    uint8_t(state.multiply ? 1 : 5),
                    uint8_t(state.multiply ? 3 : 6),
                    1,
                    uint8_t(state.depth_always ? 8 : 7),
                    uint8_t(state.depth_write),
                    1,
                    0};
        draw.push.target[0] = float(scene_.width);
        draw.push.target[1] = float(scene_.height);
        std::copy(fog_.begin(), fog_.end(), draw.push.fog);
        draw.push.mode[0] = 2 | (draw.texture >= 0 ? 4 : 0) | (state.fog ? 16 : 0);
        constexpr unsigned texture = 2U << 8;
        constexpr unsigned diffuse = 0;
        draw.push.mode[1] = draw.texture < 0     ? 2 | diffuse
                            : state.color_select ? 2 | texture
                                                 : 4 | texture;
        draw.push.mode[2] =
            draw.texture < 0 ? 2 | diffuse : (state.alpha_modulate ? 4 : 2) | texture;
        draw.scissor = {{0, 0}, {scene_.width, scene_.height}};
        if (!scene_.order.empty() && !scene_.order.back().clear) {
            auto &previous = scene_.draws.back();
            if (previous.texture == draw.texture && previous.sampler == draw.sampler &&
                previous.key.packed() == draw.key.packed() &&
                std::memcmp(&previous.push, &draw.push, sizeof(draw.push)) == 0) {
                previous.count += drawn;
                return;
            }
        }
        scene_.order.push_back({false, unsigned(scene_.draws.size())});
        scene_.draws.push_back(std::move(draw));
    }

    void flush() override {
        if (scene_.order.empty()) {
            return;
        }
        const bool initialize =
            scene_.width != resident_width_ || scene_.height != resident_height_;
        if (initialize) {
            const auto size = std::size_t(scene_.width) * scene_.height;
            scene_.color.assign(size, 0);
            scene_.depth.assign(size, 0);
        }
        renderer_->render(scene_, initialize, false);
        resident_width_ = scene_.width;
        resident_height_ = scene_.height;
        scene_ = {};
        for (auto &entry : textures_) {
            entry.scene_index = entry.sharp_index = no_binding;
        }
    }

    bool read_back(std::vector<uint32_t> *argb, int *width, int *height) override {
        std::vector<uint8_t> bgra;
        unsigned w = 0, h = 0;
        flush();
        renderer_->capture_fullres_bgra(bgra, w, h);
        if (bgra.empty()) {
            return false;
        }
        *width = int(w);
        *height = int(h);
        argb->resize(std::size_t(w) * h);
        std::memcpy(argb->data(), bgra.data(), bgra.size());
        return true;
    }

    bool hold_frame() override { return read_back(&held_, &held_width_, &held_height_); }
    bool restore_frame() override {
        ensure_extent();
        if (held_.empty() || unsigned(held_width_) != scene_.width ||
            unsigned(held_height_) != scene_.height) {
            return false;
        }
        const auto rgba = rgba_copy(held_);
        const auto handle = create_texture(held_width_, held_height_,
                                           reinterpret_cast<const uint8_t *>(rgba.data()), 1);
        const auto old_view = viewport_;
        set_viewport(0, 0, held_width_, held_height_);
        const float w = float(held_width_), h = float(held_height_);
        const TLVertex quad[] = {{0, 0, 0, 1, 0xffffffff, 0, 0, 0},
                                 {w, 0, 0, 1, 0xffffffff, 0, 1, 0},
                                 {w, h, 0, 1, 0xffffffff, 0, 1, 1},
                                 {0, h, 0, 1, 0xffffffff, 0, 0, 1}};
        DrawState copy{};
        copy.texture = handle;
        copy.address_u = copy.address_v = 3;
        copy.depth_always = true;
        copy.depth_write = false;
        draw(Primitive::kFan, quad, 4, copy);
        release_texture(handle);
        viewport_ = old_view;
        return true;
    }
    void drop_frame() override { held_.clear(); }

    void present() override {
        flush();
        if (!resident_width_) {
            return;
        }
        if (!capture_path_.empty()) {
            save_capture();
            return;
        }
        if ((SDL_GetWindowFlags(window_) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) != 0) {
            return;
        }
        const auto before = renderer_->presents;
        if (!window_bound_) {
            std::vector<uint32_t> argb;
            int width = 0, height = 0;
            if (!read_back(&argb, &width, &height)) {
                throw std::runtime_error("First Vulkan frame could not be read back");
            }
            const auto rgba = rgba_copy(argb);
            if (!renderer_->present(hwnd_, reinterpret_cast<const uint8_t *>(rgba.data()), width,
                                    height, width * 4, false, true)) {
                throw std::runtime_error(
                    "Could not bind the selected Core Critical Vulkan presenter");
            }
            window_bound_ = renderer_->presents != before;
        } else if (!renderer_->present_resident(hwnd_, resident_width_, resident_height_)) {
            throw std::runtime_error("Core Critical resident present failed");
        }
        if (renderer_->presents != before) {
            presented_frames_ += renderer_->presents - before;
            if (!presented_) {
                std::fprintf(stderr, "%s first frame: %s (upload)\n", driver_name(),
                             renderer_->device_name.c_str());
                sdlfe::mark_ready();
            } else if (!reported_resident_ && renderer_->resident_presents != 0) {
                std::fprintf(stderr, "%s resident frame: %llu Vulkan draws\n", driver_name(),
                             static_cast<unsigned long long>(renderer_->draws));
                reported_resident_ = true;
            }
            presented_ = true;
        }
    }

  private:
    void initialize_renderer() {
        auto source = native_wsi_ ? sdl_surface(window_) : vk::Renderer::WindowSurface{};
        renderer_ =
            std::make_unique<vk::Renderer>(vk::Renderer::ColorTarget::rgba8, std::move(source));
        texture_limit_ = renderer_->max_texture_dimension(hwnd_);
        if (!native_wsi_ && !renderer_->dxgi_presenting()) {
            throw std::runtime_error("Core Critical DXGI transport is unavailable");
        }
    }

    void ensure_extent() {
        if (reset_pending_) {
            flush();
            retired_draws_ += renderer_->draws;
            retired_presents_ += renderer_->resident_presents;
            initialize_renderer();
            window_bound_ = false;
            resident_width_ = resident_height_ = 0;
            scene_ = {};
            reset_pending_ = false;
        }
        if (scene_.width) {
            return;
        }
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        if (width <= 0 || height <= 0) {
            width = resident_width_ ? int(resident_width_) : 640;
            height = resident_height_ ? int(resident_height_) : 480;
        }
        const auto fit =
            sdlfe::fit_rect(float(width), float(height), shape_ ? *shape_ : sdlfe::FrameShape{});
        scene_.width = unsigned(std::max(1.0f, fit.w));
        scene_.height = unsigned(std::max(1.0f, fit.h));
        scene_.has_depth = true;
        if (scene_.width != resident_width_ || scene_.height != resident_height_) {
            append_clear(true, 0, 0, 0);
        }
    }

    void append_clear(bool color, uint8_t red, uint8_t green, uint8_t blue) {
        vk::Clear clear{color ? 3U : 2U,
                        {red / 255.f, green / 255.f, blue / 255.f, 1},
                        0,
                        {{{0, 0}, {scene_.width, scene_.height}}}};
        scene_.order.push_back({true, unsigned(scene_.clears.size())});
        scene_.clears.push_back(std::move(clear));
    }

    unsigned bind_texture(int handle, bool sharp) {
        auto &entry = textures_.at(handle);
        if (entry.pixels.empty()) {
            throw std::runtime_error("mw3-re draw references a released texture");
        }
        unsigned &index = sharp ? entry.sharp_index : entry.scene_index;
        if (index != no_binding) {
            return index;
        }
        const auto w = sharp ? scene_.width : entry.width;
        const auto h = sharp ? scene_.height : entry.height;
        const auto key = std::uint64_t(handle) * 2 + unsigned(sharp) + 1;
        vk::TextureRef ref{key, entry.version, w, h, vk::TexFormat::rgba8888, {}, {}};
        if (!renderer_->current(ref.key, ref.hash, w, h)) {
            ref.pixels32 = sharp ? sharp_layer(entry, w, h) : entry.pixels;
        }
        index = unsigned(scene_.textures.size());
        scene_.textures.push_back(std::move(ref));
        return index;
    }

    static std::vector<uint32_t> rgba_copy(const std::vector<uint32_t> &argb) {
        std::vector<uint32_t> rgba;
        rgba.reserve(argb.size());
        for (const auto pixel : argb) {
            rgba.push_back((pixel & 0xff00ff00U) | ((pixel & 255U) << 16) | ((pixel >> 16) & 255U));
        }
        return rgba;
    }

    void save_capture() {
        std::vector<uint32_t> pixels;
        int width = 0, height = 0;
        if (!read_back(&pixels, &width, &height)) {
            throw std::runtime_error("Core Critical frame capture had no pixels");
        }
        auto *surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_ARGB8888,
                                              pixels.data(), width * 4);
        if (!surface) {
            throw std::runtime_error(SDL_GetError());
        }
        const bool saved = SDL_SaveBMP(surface, capture_path_.c_str());
        SDL_DestroySurface(surface);
        if (!saved) {
            throw std::runtime_error(SDL_GetError());
        }
        capture_path_.clear();
    }

    SDL_Window *window_;
    HWND hwnd_;
    const bool native_wsi_;
    std::unique_ptr<vk::Renderer> renderer_;
    const sdlfe::FrameShape *shape_{};
    vk::Scene scene_{};
    std::array<int, 4> viewport_{0, 0, 640, 480};
    std::array<float, 3> fog_{};
    std::vector<Texture> textures_;
    std::vector<uint32_t> held_;
    std::string capture_path_;
    std::uint64_t texture_version_{}, retired_draws_{}, retired_presents_{};
    std::uint64_t presented_frames_{};
    unsigned texture_limit_{}, resident_width_{}, resident_height_{};
    int held_width_{}, held_height_{};
    bool window_bound_{}, presented_{}, reported_resident_{}, reset_pending_{};
};

} // namespace

std::unique_ptr<VulkanFrameRenderer> make_renderer(SDL_Window *window,
                                                   sdlfe::RendererBackend backend) {
    return std::make_unique<FrameRenderer>(window, backend);
}

} // namespace cc_mw3_re
