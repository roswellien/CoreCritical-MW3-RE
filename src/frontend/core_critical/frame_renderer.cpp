#include "frame_renderer.hpp"
#include "prepared_draw_cache.hpp"
#include "frontend/sdl/cpu_interval.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zmodel/gmod_fogcalc.h"
#include "prepared_draw.hpp"
#include "prepared_models.hpp"
#include "renderer/geometry/reference.hpp"

#include "frontend/sdl/sdl_child.h"
#include "vk_scene.hpp"
#include <SDL3/SDL_vulkan.h>

#undef DrawState // Win32's DrawState macro conflicts with platform::DrawState.

#include <algorithm>
#include <array>
#include <cfenv>
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

// Wall durations on the calling CPU thread, including waits inside the measured
// operation. Disabled intervals do not read a clock or change rendering inputs.
using sdlfe::CpuInterval;
using sdlfe::cpu_profile_requested;

class VisualFloatScope {
  public:
    VisualFloatScope() {
        if (std::fegetenv(&saved_) != 0)
            throw std::runtime_error("Cannot preserve the native floating-point environment");
    }
    ~VisualFloatScope() { std::fesetenv(&saved_); }

  private:
    std::fenv_t saved_;
};

void set_option(const char *name, const char *value) {
    if (_putenv_s(name, value) != 0) {
        throw std::runtime_error(std::string("Could not set renderer option ") + name);
    }
}

vk::Renderer::Quality parse_quality(const sdlfe::CoreCriticalQuality &quality) {
    vk::Renderer::Quality result{1, 1, 1, false};
    if (quality.antialiasing == "fxaa")
        result.fxaa = true;
    else if (quality.antialiasing == "ssaa2")
        result.samples_x = 2;
    else if (quality.antialiasing == "ssaa4")
        result.samples_x = result.samples_y = 2;
    else if (quality.antialiasing != "off")
        throw std::runtime_error("--cc-aa supports off, fxaa, ssaa2 or ssaa4");
    if (quality.anisotropy != "1" && quality.anisotropy != "2" && quality.anisotropy != "4" &&
        quality.anisotropy != "8" && quality.anisotropy != "16")
        throw std::runtime_error("--cc-af supports 1, 2, 4, 8 or 16");
    result.anisotropy = unsigned(std::stoi(quality.anisotropy));
    return result;
}

mw34::sun::Settings parse_lighting(const sdlfe::CoreCriticalAssets &options) {
    mw34::sun::Settings result;
    if (options.lighting != "off" && options.lighting != "flat" && options.lighting != "normal")
        throw std::runtime_error("--cc-lighting supports off, flat or normal");
    auto number = [](const std::string &text, float low, float high) {
        char *end = nullptr;
        const float value = std::strtof(text.c_str(), &end);
        if (end == text.c_str() || *end || !std::isfinite(value) || value < low || value > high)
            throw std::runtime_error("Invalid --cc-sun angle (azimuth -360..360, elevation 1..89)");
        return value;
    };
    result.azimuth = number(options.sun_azimuth, -360, 360);
    result.elevation = number(options.sun_elevation, 1, 89);
    result.enabled = options.lighting != "off" || options.geometry != "legacy";
    if (options.lighting == "off" && options.geometry != "legacy") {
        result.strength = 0;
        result.ambient = 1;
        result.ambient_color = {1,1,1};
    }
    result.normals = options.lighting == "normal";
    result.normal_strength = result.ground_normal_strength = result.normals ? 1.f : 0.f;
    // Native sky/weather/shadow production is not part of prepared-model lighting.
    result.weather_enabled = false;
    return result;
}

void configure_bridge(bool native_wsi) {
    set_option("MW34_DXGI_PRESENT", native_wsi ? "0" : "1");
    set_option("MW34_WINDOW_MODE", "windowed");
    set_option("MW34_WORLD_SCALE_PERCENT", "100");
    set_option("MW34_REMASTER_WIDTH", "");
    set_option("MW34_REMASTER_HEIGHT", "");
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
    bool held_copy{};
    const platform::prepared::Image *replacement{};
    unsigned source_width{}, source_height{};
    std::vector<std::uint32_t> pixels;
};

std::vector<uint32_t> image_pixels(const platform::prepared::Image &image,
                                   const uint8_t *source = nullptr, unsigned source_width = 0,
                                   unsigned source_height = 0) {
    std::vector<uint32_t> pixels(image.pixels.size());
    for (unsigned y = 0; y < image.height; ++y)
        for (unsigned x = 0; x < image.width; ++x) {
            const auto value = image.pixels[std::size_t(y) * image.width + x];
            unsigned alpha = 255;
            if (source) {
                const auto sx =
                    std::min(source_width - 1, unsigned((uint64_t(x) * 2 + 1) * source_width /
                                                        (uint64_t(image.width) * 2)));
                const auto sy =
                    std::min(source_height - 1, unsigned((uint64_t(y) * 2 + 1) * source_height /
                                                         (uint64_t(image.height) * 2)));
                alpha = source[(std::size_t(sy) * source_width + sx) * 4 + 3];
            }
            pixels[std::size_t(y) * image.width + x] =
                alpha == 0 ? 0
                           : ((value >> 11) * 255 / 31) | (((value >> 5) & 63) * 255 / 63) << 8 |
                                 ((value & 31) * 255 / 31) << 16 | alpha << 24;
        }
    return pixels;
}

cc::geometry::Mode parse_geometry(const std::string& mode) {
    if (mode == "legacy") return cc::geometry::Mode::legacy;
    if (mode == "partial") return cc::geometry::Mode::partial;
    if (mode == "gpu") return cc::geometry::Mode::gpu;
    if (mode == "cpu-reference") return cc::geometry::Mode::cpu_reference;
    throw std::runtime_error("--cc-geometry supports legacy, partial, gpu or cpu-reference");
}

class FrameRenderer final : public VulkanFrameRenderer {
  public:
    explicit FrameRenderer(SDL_Window *window, sdlfe::RendererBackend backend,
                           const sdlfe::CoreCriticalQuality &quality,
                           const sdlfe::CoreCriticalAssets &assets)
        : window_(window),
          hwnd_(static_cast<HWND>(SDL_GetPointerProperty(
              SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))),
          native_wsi_(backend == sdlfe::RendererBackend::core_critical_wsi),
          quality_(parse_quality(quality)), lighting_(parse_lighting(assets)), prepared_(assets),
          geometry_mode_(parse_geometry(assets.geometry)), geometry_debug_(assets.geometry_debug_normals) {
        if (geometry_debug_ && geometry_mode_ == cc::geometry::Mode::legacy)
            throw std::runtime_error("Geometry normal debug requires an explicit geometry mode");
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
    QualityStatistics quality_statistics() const override {
        return {retired_quality_.fxaa_passes + renderer_->fxaa_passes,
                retired_quality_.anisotropic_samplers + renderer_->af_samplers,
                retired_quality_.mip_levels + renderer_->mip_levels_uploaded};
    }
    AssetStatistics asset_statistics() const override {
        return {prepared_batches_,
                retired_model_draws_ + renderer_->model_draws,
                cpu_clipped_triangles_,
                named_textures_,
                retired_model_uploads_ + renderer_->model_uploads,
                retired_lit_draws_ + renderer_->sun_world_draws + renderer_->sun_tl_draws,
                retired_normal_draws_ + renderer_->normal_draws,
                retired_geometry_buffers_ + renderer_->retired_model_buffers};
    }
#include "geometry_frame_adapter.inc"
#include "geometry_reference_adapter.inc"

    bool wants_model_context() const override { return !prepared_.empty(); }
    bool replaces_model_geometry() const override { return suppress_model_draws_; }
    void end_model_draw() noexcept override { suppress_model_draws_ = false; }
    void begin_model_draw(const platform::ModelDrawContext &context) override {
        // Visual retargeting/fog must not leak exception flags or rounding state
        // into the native polygon producer that resumes after this callback.
        VisualFloatScope float_scope;
        suppress_model_draws_ = false;
        ensure_extent();
        const auto draws = prepared_.draw(context);
        if (!draws.replace)
            return;
        if (scene_.hud && !scene_.draws.empty()) {
            flush();
            ensure_extent();
        }
        scene_.hud = false;
        for (const auto &draw : draws.batches)
            emit_prepared(draw, context);
        suppress_model_draws_ = true;
    }
    const char *driver_name() const override {
        return native_wsi_ ? "Core Critical Vulkan/SDL WSI" : "Core Critical Vulkan/DXGI";
    }
    void set_shape(const sdlfe::FrameShape *shape) override { shape_ = shape; }
    void reset_scene_resources() override { reset_pending_ = true; }

    ~FrameRenderer() override {
        if (cpu_profile_) {
            const double milliseconds = 1000.0 / SDL_GetPerformanceFrequency();
            std::fprintf(stderr,
                         "CPU renderer profile (includes warmup): %llu presents, %llu GPU sharp draws, "
                         "%llu render calls, render %.4f ms, "
                         "transport %.4f ms\n",
                         static_cast<unsigned long long>(presented_frames_),
                         static_cast<unsigned long long>(gpu_sharp_draws_),
                         static_cast<unsigned long long>(render_calls_),
                         render_ticks_ * milliseconds, transport_ticks_ * milliseconds);
            const auto &models = prepared_.cpu_statistics();
            std::fprintf(stderr,
                         "CPU prepared profile (includes warmup, nested intervals): %llu calls, "
                         "selection %.4f ms, retarget %.4f ms, deform %.4f ms, "
                         "emission %.4f ms, TL append %.4f ms\n",
                         static_cast<unsigned long long>(models.calls),
                         models.selection_ticks * milliseconds, models.retarget_ticks * milliseconds,
                         models.deform_ticks * milliseconds, prepared_emit_ticks_ * milliseconds,
                         append_ticks_ * milliseconds);
            std::fprintf(stderr,
                         "CPU prepared vertices: %llu vertices, stage %.4f ms, assembly %.4f ms\n",
                         static_cast<unsigned long long>(prepared_cpu_.vertices),
                         prepared_cpu_.stage_ticks * milliseconds,
                         prepared_cpu_.assembly_ticks * milliseconds);
        }
        if (geometry_mode_ != cc::geometry::Mode::legacy)
            std::fprintf(stderr,"Geometry handoff: %llu submitted models, %llu CPU vertex preparations skipped, %llu CPU polygon preparations skipped, %llu unsupported models, %llu CPU 3D draw calls observed\n",
                (unsigned long long)geometry_statistics_.submitted_models,
                (unsigned long long)geometry_statistics_.skipped_vertices,
                (unsigned long long)geometry_statistics_.skipped_polygons,
                (unsigned long long)geometry_statistics_.unsupported_models,
                (unsigned long long)geometry_statistics_.cpu_draw_calls);
        const auto quality = quality_statistics();
        std::fprintf(
            stderr,
            "%s: %llu draws, %llu resident presents, %llu FXAA passes, %llu AF samplers, %llu mip "
            "levels\n",
            driver_name(), static_cast<unsigned long long>(retired_draws_ + renderer_->draws),
            static_cast<unsigned long long>(retired_presents_ + renderer_->resident_presents),
            static_cast<unsigned long long>(quality.fxaa_passes),
            static_cast<unsigned long long>(quality.anisotropic_samplers),
            static_cast<unsigned long long>(quality.mip_levels));
        const auto assets = asset_statistics();
        const auto &cache = prepared_draw_cache_.statistics();
        if (cache.hits || cache.misses || cache.bypasses)
            std::fprintf(stderr,
                         "Prepared staging cache: %llu hits, %llu misses, %llu bypasses, %zu bytes\n",
                         (unsigned long long)cache.hits, (unsigned long long)cache.misses,
                         (unsigned long long)cache.bypasses, cache.bytes);
        if (assets.prepared_batches || assets.named_textures)
            std::fprintf(stderr,
                         "Prepared visuals: %llu batches, %llu GPU model draws, %llu CPU-clipped "
                         "triangles, %llu named texture uploads\n",
                         static_cast<unsigned long long>(assets.prepared_batches),
                         static_cast<unsigned long long>(assets.gpu_model_draws),
                         static_cast<unsigned long long>(assets.cpu_clipped_triangles),
                         static_cast<unsigned long long>(assets.named_textures));
        if (lighting_.enabled)
            std::fprintf(stderr,
                         "Prepared materials: %llu per-pixel draws, %llu normal-map draws\n",
                         static_cast<unsigned long long>(assets.lit_draws),
                         static_cast<unsigned long long>(assets.normal_draws));
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
        std::vector<uint32_t> replacement;
        if (entry.replacement) {
            if (!rgba || unsigned(width) != entry.source_width ||
                unsigned(height) != entry.source_height)
                throw std::runtime_error("Named texture reload changed its source dimensions");
            replacement =
                image_pixels(*entry.replacement, rgba, entry.source_width, entry.source_height);
            rgba = reinterpret_cast<const uint8_t *>(replacement.data());
            width = int(entry.width);
            height = int(entry.height);
            ++named_textures_;
        }
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

    int32_t create_named_texture(const char *name, int width, int height, const uint8_t *rgba,
                                 uint8_t type) override {
        const auto *replacement = prepared_.named_texture(name);
        if (!replacement)
            return create_texture(width, height, rgba, type);
        if (!rgba || width <= 0 || height <= 0)
            throw std::runtime_error("Invalid named texture input");
        const auto pixels = image_pixels(*replacement, rgba, unsigned(width), unsigned(height));
        const auto handle = create_texture(int(replacement->width), int(replacement->height),
                                           reinterpret_cast<const uint8_t *>(pixels.data()), type);
        auto &entry = textures_[handle];
        entry.replacement = replacement;
        entry.source_width = unsigned(width);
        entry.source_height = unsigned(height);
        ++named_textures_;
        return handle;
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
        // Only the explicitly marked 2D compositor may supply CPU vertices in
        // strict mode. Depth-always also occurs in 3D overlays, so it cannot
        // identify a HUD exemption.
        if (count > 0 && !state.sharp && geometry_mode_ != cc::geometry::Mode::legacy) {
            ++geometry_statistics_.cpu_draw_calls;
            if (geometry_mode_ == cc::geometry::Mode::gpu)
                throw std::runtime_error("Strict GPU geometry: CPU 3D draw is not implemented");
        }
        if (suppress_model_draws_)
            return;
        append_draw(primitive, vertices, count, state, state.sharp || state.depth_always);
    }

    void clear_depth_rect(int left, int top, int right, int bottom) override {
        if (right <= left || bottom <= top)
            return;
        const float l = float(left), t = float(top), r = float(right), b = float(bottom);
        const TLVertex vertices[] = {{l, t, 0, 1, 0, 0xff000000, 0, 0},
                                     {r, t, 0, 1, 0, 0xff000000, 0, 0},
                                     {r, b, 0, 1, 0, 0xff000000, 0, 0},
                                     {l, b, 0, 1, 0, 0xff000000, 0, 0}};
        DrawState state{};
        state.blend = state.depth_write = state.depth_always = true;
        // Preserve the inherited rasterized depth-only operation and its fractional
        // viewport coverage, without treating it as a color/HUD composition boundary.
        append_draw(Primitive::kFan, vertices, 4, state, scene_.hud, true);
    }

  private:
    int32_t prepared_normal(const platform::prepared::NormalImage &image) {
        const auto found = prepared_normals_.find(&image);
        if (found != prepared_normals_.end())
            return found->second;
        const auto handle =
            create_texture(int(image.width), int(image.height),
                           reinterpret_cast<const uint8_t *>(image.pixels.data()), 1);
        prepared_normals_.emplace(&image, handle);
        return handle;
    }

    void append_draw(Primitive primitive, const TLVertex *vertices, int count,
                     const DrawState &state, bool hud, bool depth_only = false) {
        CpuInterval interval(cpu_profile_ ? &append_ticks_ : nullptr);
        if (count <= 0) {
            return;
        }
        ensure_extent();
        if (!scene_.draws.empty() && scene_.hud != hud) {
            flush();
            ensure_extent();
        }
        scene_.hud = hud;
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
        draw.depth_only = depth_only;
        draw.first = first;
        draw.count = drawn;
        draw.texture = state.texture < 0 ? -1 : int(bind_texture(state.texture, state.sharp));
        const bool sharp = state.sharp && state.texture >= 0 && !textures_.at(state.texture).held_copy;
        draw.sampler = (state.address_u == 3 ? 3U : 1U) << 4 |
                       (state.address_v == 3 ? 3U : 1U) << 8 | (state.sharp && !sharp ? 0U : 3U);
        if (!hud)
            draw.sampler |= 0x10000U;
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
        if (sharp)
            draw.push.mode[0] |= 64;
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
            if (!previous.model && previous.depth_only == draw.depth_only &&
                previous.texture == draw.texture && previous.sampler == draw.sampler &&
                previous.key.packed() == draw.key.packed() &&
                std::memcmp(&previous.push, &draw.push, sizeof(draw.push)) == 0) {
                previous.count += drawn;
                return;
            }
        }
        scene_.order.push_back({false, unsigned(scene_.draws.size())});
        scene_.draws.push_back(std::move(draw));
        if (sharp) ++gpu_sharp_draws_;
    }

  public:
    void flush() override {
        if (scene_.order.empty()) {
            return;
        }
        CpuInterval interval(cpu_profile_ ? &render_ticks_ : nullptr);
        if (cpu_profile_)
            ++render_calls_;
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
        // Reuse hot submission storage, like the SDL renderer. Reset every
        // scene value and release its resource references while keeping capacity.
        vk::Scene next{};
        next.vertices.swap(scene_.vertices);
        next.vertices.clear();
        next.draws.swap(scene_.draws);
        next.draws.clear();
        next.textures.swap(scene_.textures);
        next.textures.clear();
        next.order.swap(scene_.order);
        next.order.clear();
        next.clears.swap(scene_.clears);
        next.clears.clear();
        scene_ = std::move(next);
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
        const mw34::display::Samples samples{quality_.samples_x, quality_.samples_y};
        *width = int(w / samples.x);
        *height = int(h / samples.y);
        if (samples.x == 1 && samples.y == 1) {
            argb->resize(std::size_t(w) * h);
            std::memcpy(argb->data(), bgra.data(), bgra.size());
            return true;
        }
        argb->assign(std::size_t(*width) * *height, 0);
        // Match the resident presenter's integer-grid SSAA resolve. The game-facing
        // readback and held frame keep their output dimensions and alpha contract.
        for (int y = 0; y < *height; ++y)
            for (int x = 0; x < *width; ++x) {
                std::uint32_t pixel = 0;
                for (unsigned channel = 0; channel < 4; ++channel) {
                    unsigned sum = 0;
                    for (unsigned sy = 0; sy < samples.y; ++sy)
                        for (unsigned sx = 0; sx < samples.x; ++sx)
                            sum +=
                                bgra[((y * samples.y + sy) * w + x * samples.x + sx) * 4 + channel];
                    const unsigned count = samples.x * samples.y;
                    pixel |= ((sum + count / 2) / count) << (channel * 8);
                }
                (*argb)[std::size_t(y) * *width + x] = pixel;
            }
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
        textures_[handle].held_copy = true;
        const auto old_view = viewport_;
        set_viewport(0, 0, held_width_, held_height_);
        const float w = float(held_width_), h = float(held_height_);
        const TLVertex quad[] = {{0, 0, 0, 1, 0xffffffff, 0, 0, 0},
                                 {w, 0, 0, 1, 0xffffffff, 0, 1, 0},
                                 {w, h, 0, 1, 0xffffffff, 0, 1, 1},
                                 {0, h, 0, 1, 0xffffffff, 0, 0, 1}};
        DrawState copy{};
        copy.sharp = true;
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
        CpuInterval interval(cpu_profile_ ? &transport_ticks_ : nullptr);
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
    int32_t prepared_texture(const platform::prepared::Image &image) {
        const auto found = prepared_textures_.find(&image);
        if (found != prepared_textures_.end())
            return found->second;
        const auto pixels = image_pixels(image);
        const auto handle = create_texture(int(image.width), int(image.height),
                                           reinterpret_cast<const uint8_t *>(pixels.data()), 1);
        prepared_textures_.emplace(&image, handle);
        return handle;
    }

    std::shared_ptr<const vk::ModelGeometry>
    prepared_geometry(const platform::prepared::Batch &batch) {
        const auto found = prepared_geometry_.find(&batch);
        if (found != prepared_geometry_.end())
            return found->second;
        auto geometry = std::make_shared<vk::ModelGeometry>();
        geometry->key = 0x4343000000000000ULL + prepared_geometry_.size() + 1;
        geometry->vertices.reserve(batch.vertices.size());
        for (const auto &vertex : batch.vertices)
            geometry->vertices.push_back({vertex[0], vertex[1], vertex[2], vertex[3], vertex[4],
                                          vertex[5], vertex[6], vertex[7]});
        geometry->hash = 0xcbf29ce484222325ULL;
        const auto *bytes = reinterpret_cast<const uint8_t *>(geometry->vertices.data());
        for (size_t i = 0; i < geometry->vertices.size() * sizeof(vk::ModelVertex); ++i) {
            geometry->hash ^= bytes[i];
            geometry->hash *= 0x100000001b3ULL;
        }
        prepared_geometry_.emplace(&batch, geometry);
        return geometry;
    }

    void emit_prepared(const PreparedBatchDraw &source, const platform::ModelDrawContext &context) {
        CpuInterval interval(cpu_profile_ ? &prepared_emit_ticks_ : nullptr);
        const auto staged = prepared_draw_cache_.get(
            source, context, viewport_, scene_.width, scene_.height,
            prepared_geometry(*source.batch), lighting_.enabled,
            cpu_profile_ ? &prepared_cpu_ : nullptr, prepared_cache_enabled_);
        auto draw = staged->draw;
        draw.first = unsigned(scene_.vertices.size() / sizeof(TLVertex));
        draw.texture = int(bind_texture(prepared_texture(*source.texture), false));
        if (source.normal && lighting_.normals)
            draw.normal_texture = int(bind_texture(prepared_normal(*source.normal), false));
        scene_.vertices.insert(scene_.vertices.end(), staged->vertices.begin(),
                               staged->vertices.end());
        cpu_clipped_triangles_ += staged->clipped_triangles;
        if (draw.count) {
            scene_.order.push_back({false, unsigned(scene_.draws.size())});
            scene_.draws.push_back(std::move(draw));
            ++prepared_batches_;
        }
    }
    void initialize_renderer() {
        auto source = native_wsi_ ? sdl_surface(window_) : vk::Renderer::WindowSurface{};
        renderer_ =
            std::make_unique<vk::Renderer>(vk::Renderer::ColorTarget::rgba8, std::move(source),
                                           quality_, !prepared_.empty() || geometry_mode_ != cc::geometry::Mode::legacy,
                                           lighting_, geometry_mode_ != cc::geometry::Mode::legacy);
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
            retired_quality_ = quality_statistics();
            retired_model_draws_ += renderer_->model_draws;
            retired_model_uploads_ += renderer_->model_uploads;
            retired_lit_draws_ += renderer_->sun_world_draws + renderer_->sun_tl_draws;
            retired_normal_draws_ += renderer_->normal_draws;
            retired_geometry_buffers_ += renderer_->retired_model_buffers;
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
        const auto w = entry.width;
        const auto h = entry.height;
        const auto key = std::uint64_t(handle) * 2 + unsigned(sharp) + 1;
        vk::TextureRef ref{key, entry.version, w, h, vk::TexFormat::rgba8888, {}, {}};
        if (!renderer_->current(ref.key, ref.hash, w, h)) {
            // Held readback is already straight RGBA at the output extent. Its
            // nearest SSAA sampling must preserve alpha-zero RGB as well.
            ref.pixels32 = entry.pixels;
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
    const vk::Renderer::Quality quality_;
    const mw34::sun::Settings lighting_;
    PreparedModels prepared_;
    const cc::geometry::Mode geometry_mode_;
    const bool geometry_debug_;
    cc::geometry::Statistics geometry_statistics_;
    struct NativeGeometry {
        std::weak_ptr<const cc::geometry::Resource> source;
        std::shared_ptr<const vk::ModelGeometry> geometry;
    };
    std::map<std::uint64_t,NativeGeometry> native_geometry_;
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
    std::uint64_t retired_geometry_buffers_{};
    QualityStatistics retired_quality_{};
    std::map<const platform::prepared::Image *, int32_t> prepared_textures_;
    std::map<const platform::prepared::NormalImage *, int32_t> prepared_normals_;
    std::map<const platform::prepared::Batch *, std::shared_ptr<const vk::ModelGeometry>>
        prepared_geometry_;
    std::uint64_t prepared_batches_{}, retired_model_draws_{}, cpu_clipped_triangles_{},
        named_textures_{};
    bool suppress_model_draws_{};
    std::uint64_t retired_model_uploads_{};
    std::uint64_t retired_lit_draws_{}, retired_normal_draws_{};
    unsigned texture_limit_{}, resident_width_{}, resident_height_{};
    int held_width_{}, held_height_{};
    bool window_bound_{}, presented_{}, reported_resident_{}, reset_pending_{};
    const bool cpu_profile_ = cpu_profile_requested();
    std::uint64_t render_ticks_{}, transport_ticks_{};
    std::uint64_t gpu_sharp_draws_{}, render_calls_{};
    std::uint64_t prepared_emit_ticks_{}, append_ticks_{};
    PreparedDrawCpuStatistics prepared_cpu_;
    const bool prepared_cache_enabled_ = [] {
        const auto *value = std::getenv("MW3_CC_PREPARED_CACHE");
        if (!value || !std::strcmp(value, "1")) return true;
        if (!std::strcmp(value, "0")) return false;
        throw std::runtime_error("MW3_CC_PREPARED_CACHE must be 0 or 1");
    }();
    PreparedDrawCache prepared_draw_cache_;
};

} // namespace

std::unique_ptr<VulkanFrameRenderer> make_renderer(SDL_Window *window,
                                                   sdlfe::RendererBackend backend,
                                                   const sdlfe::CoreCriticalQuality &quality,
                                                   const sdlfe::CoreCriticalAssets &assets) {
    return std::make_unique<FrameRenderer>(window, backend, quality, assets);
}

} // namespace cc_mw3_re
