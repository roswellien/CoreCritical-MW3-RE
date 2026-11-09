#include "frame_renderer.hpp"
#include "frontend/sdl/cpu_interval.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zmodel/gmod_fogcalc.h"
#include "gamez/zutil/ztimer.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "prepared_draw.hpp"
#include "prepared_draw_cache.hpp"
#include "prepared_models.hpp"
#include "render_worker.hpp"
#include "renderer/campaign_sun_settings.hpp"
#include "renderer/effect_light_observations.hpp"
#include "renderer/effect_lighting_stage.inc"
#include "renderer/geometry/reference.hpp"
#include "renderer/material_normals.hpp"
#include "renderer/native_surface_identity.hpp"
#include <filesystem>
#include <fstream>

namespace mw34::materials {
void set_active_surface_key(const SurfaceKey &key);
void clear_active_surface_key();
bool get_active_surface_key(SurfaceKey *out);
} // namespace mw34::materials

#include "frontend/sdl/sdl_child.h"
#include "platform/wsi_surface.hpp"
#include "platform/wsi_win32_dxgi.hpp"
#include "vk_scene.hpp"
#include <SDL3/SDL_vulkan.h>

#undef DrawState // Win32's DrawState macro conflicts with platform::DrawState.

#include <algorithm>
#include <array>
#include <atomic>
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
using ::platform::DrawState;
using ::platform::Primitive;
using ::platform::TLVertex;

// Wall durations on the calling CPU thread, including waits inside the measured
// operation. Disabled intervals do not read a clock or change rendering inputs.
using sdlfe::cpu_profile_requested;
using sdlfe::CpuInterval;

class VisualFloatScope {
public:
  VisualFloatScope() {
    if (std::fegetenv(&saved_) != 0)
      throw std::runtime_error(
          "Cannot preserve the native floating-point environment");
  }
  ~VisualFloatScope() { std::fesetenv(&saved_); }

private:
  std::fenv_t saved_;
};

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
  if (quality.anisotropy != "1" && quality.anisotropy != "2" &&
      quality.anisotropy != "4" && quality.anisotropy != "8" &&
      quality.anisotropy != "16")
    throw std::runtime_error("--cc-af supports 1, 2, 4, 8 or 16");
  result.anisotropy = unsigned(std::stoi(quality.anisotropy));
  return result;
}

mw34::sun::Settings parse_lighting(const sdlfe::CoreCriticalAssets &options) {
  mw34::sun::Settings result;
  if (options.lighting != "off" && options.lighting != "flat" &&
      options.lighting != "normal")
    throw std::runtime_error("--cc-lighting supports off, flat or normal");
  auto number = [](const std::string &text, float low, float high) {
    char *end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end || !std::isfinite(value) || value < low ||
        value > high)
      throw std::runtime_error(
          "Invalid --cc-sun angle (azimuth -360..360, elevation 1..89)");
    return value;
  };
  result.azimuth = number(options.sun_azimuth, -360, 360);
  result.elevation = number(options.sun_elevation, 1, 89);
  result.enabled = options.lighting != "off" || options.geometry != "legacy";
  if (options.lighting == "off" && options.geometry != "legacy") {
    result.strength = 0;
    result.ambient = 1;
    result.ambient_color = {1, 1, 1};
  }
  result.normals = options.lighting == "normal";
  result.normal_strength = result.ground_normal_strength =
      result.normals ? 1.f : 0.f;
  // Native sky/weather/shadow production is not part of prepared-model
  // lighting.
  result.weather_enabled = false;
  return result;
}

vk::Renderer::WindowSurface
sdl_surface(SDL_Window *window,
            const std::shared_ptr<cc_mw3_re::platform::SdlWindowStateTracker>
                &tracker) {
  vk::Renderer::WindowSurface source;
  source.extensions = cc_mw3_re::platform::get_instance_extensions();
  source.create = [window](VkInstance instance) {
    return cc_mw3_re::platform::create_surface(window, instance);
  };
  source.destroy = [](VkInstance instance, VkSurfaceKHR surface) {
    cc_mw3_re::platform::destroy_surface(instance, surface);
  };
  source.is_visible = [tracker](cc_mw3_re::platform::NativeWindowHandle w) {
    if (!w)
      return false;
    if (tracker) {
      if (tracker->is_owner_thread()) {
        tracker->sample_owner();
        return tracker->current_state().visible;
      }
      return tracker->worker_visible();
    }
    return true;
  };
  source.get_drawable_size =
      [tracker](cc_mw3_re::platform::NativeWindowHandle w, long *width,
                long *height) {
        if (!w) {
          if (width)
            *width = 0;
          if (height)
            *height = 0;
          return;
        }
        if (tracker) {
          if (tracker->is_owner_thread()) {
            tracker->sample_owner();
            const auto &state = tracker->current_state();
            if (width)
              *width = state.drawable_width;
            if (height)
              *height = state.drawable_height;
          } else {
            const auto state = tracker->worker_state();
            if (width)
              *width = state.drawable_width;
            if (height)
              *height = state.drawable_height;
          }
          return;
        }
        if (width)
          *width = 0;
        if (height)
          *height = 0;
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
  const ::platform::prepared::Image *replacement{};
  unsigned source_width{}, source_height{};
  std::vector<std::uint32_t> pixels;
  std::string name;
  mw34::materials::SurfaceKey surface_key{};
  std::uint64_t group_load_epoch{0};
};

std::vector<uint32_t> image_pixels(const ::platform::prepared::Image &image,
                                   const uint8_t *source = nullptr,
                                   unsigned source_width = 0,
                                   unsigned source_height = 0) {
  std::vector<uint32_t> pixels(image.pixels.size());
  for (unsigned y = 0; y < image.height; ++y)
    for (unsigned x = 0; x < image.width; ++x) {
      const auto value = image.pixels[std::size_t(y) * image.width + x];
      unsigned alpha = 255;
      if (source) {
        const auto sx = std::min(source_width - 1,
                                 unsigned((uint64_t(x) * 2 + 1) * source_width /
                                          (uint64_t(image.width) * 2)));
        const auto sy = std::min(
            source_height - 1, unsigned((uint64_t(y) * 2 + 1) * source_height /
                                        (uint64_t(image.height) * 2)));
        alpha = source[(std::size_t(sy) * source_width + sx) * 4 + 3];
      }
      pixels[std::size_t(y) * image.width + x] =
          alpha == 0 ? 0
                     : ((value >> 11) * 255 / 31) |
                           (((value >> 5) & 63) * 255 / 63) << 8 |
                           ((value & 31) * 255 / 31) << 16 | alpha << 24;
    }
  return pixels;
}

cc::geometry::Mode parse_geometry(const std::string &mode) {
  if (mode == "legacy")
    return cc::geometry::Mode::legacy;
  if (mode == "partial")
    return cc::geometry::Mode::partial;
  if (mode == "gpu")
    return cc::geometry::Mode::gpu;
  if (mode == "cpu-reference")
    return cc::geometry::Mode::cpu_reference;
  throw std::runtime_error(
      "--cc-geometry supports legacy, partial, gpu or cpu-reference");
}

sdlfe::CoreCriticalAssets
sanitize_prepared_assets(sdlfe::CoreCriticalAssets assets) {
  if (assets.models.empty()) {
    assets.lighting = "off";
    assets.normals.clear();
  }
  return assets;
}

class FrameRenderer final : public VulkanFrameRenderer {
public:
  explicit FrameRenderer(SDL_Window *window, sdlfe::RendererBackend backend,
                         const sdlfe::CoreCriticalQuality &quality,
                         const sdlfe::CoreCriticalAssets &assets)
      : window_(window),
        window_state_tracker_(
            backend == sdlfe::RendererBackend::core_critical_wsi
                ? std::make_shared<cc_mw3_re::platform::SdlWindowStateTracker>(
                      window)
                : nullptr),
        native_window_(
            backend == sdlfe::RendererBackend::core_critical_wsi
                ? cc_mw3_re::platform::get_window_native_handle(window)
                : static_cast<cc_mw3_re::platform::NativeWindowHandle>(
                      cc_mw3_re::platform::get_window_hwnd(window))),
        hwnd_(cc_mw3_re::platform::get_window_hwnd(window)),
        native_wsi_(backend == sdlfe::RendererBackend::core_critical_wsi),
        quality_(parse_quality(quality)), lighting_(parse_lighting(assets)),
        base_lighting_(lighting_), campaign_enabled_(assets.lighting != "off"),
        sun_explicit_(assets.sun_explicit),
        environment_(assets.environment),
        prepared_(sanitize_prepared_assets(assets), assets.materials),
        geometry_mode_(parse_geometry(assets.geometry)),
        geometry_debug_(assets.geometry_debug_normals),
        bridge_config_(configure_bridge(backend, quality, assets)),
        prepared_cache_enabled_(bridge_config_.prepared_cache) {
    if (geometry_debug_ && geometry_mode_ == cc::geometry::Mode::legacy)
      throw std::runtime_error(
          "Geometry normal debug requires an explicit geometry mode");
    if (!native_wsi_ && !hwnd_) {
      throw std::runtime_error(
          "Direct3D presentation requires a Win32 window handle");
    }
    if (backend == sdlfe::RendererBackend::sdl)
      throw std::runtime_error(
          "The Core Critical factory requires a Vulkan backend");
    // Admit catalog paths and headers before native mission callbacks start.
    // World pixel payloads remain lazy; failures here reach the window's normal
    // initialization error boundary instead of unwinding legacy game loaders.
    if (lighting_.normals)
      surface_normals_load();
    local_lights_load();
    initialize_renderer();
    if (assets.render_thread) {
      worker_ = std::make_unique<RenderWorker>(8, 128 * 1024 * 1024, [] {
        // Process only synchronous window messages, without consuming
        // input or reentering the game. DXGI may send one while waiting.
        cc_mw3_re::platform::pump_presentation_messages();
      });
      std::fprintf(stderr,
                   "Core Critical render worker: enabled (8 jobs, 128 MiB)\n");
    }
  }

  bool ok() const override { return true; }
  std::uint64_t presentation_count() const override {
    if (worker_)
      worker_->drain();
    return presented_frames_.load();
  }
  void drain() override {
    flush();
    complete_local_light_packets();
    if (worker_)
      worker_->drain();
  }
  QualityStatistics quality_statistics() const override {
    if (worker_)
      worker_->drain();
    return {retired_quality_.fxaa_passes + renderer_->fxaa_passes,
            retired_quality_.anisotropic_samplers + renderer_->af_samplers,
            retired_quality_.mip_levels + renderer_->mip_levels_uploaded};
  }
  AssetStatistics asset_statistics() const override {
    if (worker_)
      worker_->drain();
    return {prepared_batches_,
            retired_model_draws_ + renderer_->model_draws,
            cpu_clipped_triangles_,
            named_textures_,
            retired_model_uploads_ + renderer_->model_uploads,
            retired_lit_draws_ + renderer_->sun_world_draws +
                renderer_->sun_model_draws + renderer_->sun_tl_draws,
            retired_normal_draws_ + renderer_->normal_draws,
            retired_geometry_buffers_ + renderer_->retired_model_buffers,
            gpu_prepared_vertices_,
            early_prepared_models_,
            prepared_cpu_fallback_batches_,
            surface_normals_.cache.world_bytes,
            surface_normals_.peak_decoded_bytes,
            std::uint64_t(
                std::count_if(surface_normals_.cache.pages.begin(),
                              surface_normals_.cache.pages.end(),
                              [](const auto &page) { return page.loaded; })),
            binding_rejected_mismatch_,
            native_fingerprint_evaluations_};
  }
#include "geometry_frame_adapter.inc"
#include "geometry_reference_adapter.inc"
#include "prepared_gpu_adapter.inc"
#include "renderer/surface_normals_stage.inc"
private:
#include "renderer/local_light_runtime.inc"
public:
  int32_t surface_normal_texture(int normal_index) {
    const auto found = surface_normal_textures_.find(normal_index);
    if (found != surface_normal_textures_.end()) {
      return found->second;
    }
    const auto &norm_img = surface_normal_image(normal_index);
    const auto handle = create_texture(
        int(norm_img.width), int(norm_img.height),
        reinterpret_cast<const uint8_t *>(norm_img.pixels32.data()), 1);
    surface_normal_textures_.emplace(normal_index, handle);
    return handle;
  }

  std::shared_ptr<const ::platform::DeferredMaterialBinding>
  capture_deferred_material_binding(int32_t texture) override {
    if (!lighting_.normals || bridge_config_.native_material_sidecar.empty() ||
        texture < 0 || std::size_t(texture) >= textures_.size())
      return {};
    surface_normals_load(); // Reset clears catalogs; deferred admission can be
                            // first.
    const auto &key = textures_[texture].surface_key;
    auto receipt = std::make_shared<NativeDeferredBinding>();
    receipt->owner = material_binding_owner_;
    receipt->key = textures_[texture].surface_key;
    receipt->epoch = group_load_epoch_;
    receipt->version = textures_[texture].version;
    receipt->texture = texture;
    receipt->surface_parameters = native_surface_parameters(key, texture);
    receipt->normal_index = surface_normal_index(receipt->key, true, texture);
    return receipt;
  }
  void begin_deferred_material_draw(
      std::shared_ptr<const ::platform::DeferredMaterialBinding> receipt)
      override {
    deferred_material_binding_ = std::move(receipt);
  }
  void end_deferred_material_draw() noexcept override {
    deferred_material_binding_.reset();
  }
  void invalidate_material_geometry() noexcept override {
    native_context_.fingerprint_computed = false;
    native_context_.fingerprint.reset();
  }
  bool wants_material_context() const override {
    return lighting_.normals &&
           (!bridge_config_.native_material_sidecar.empty() ||
            bridge_config_.trace_native_bindings);
  }
  bool wants_model_context() const override {
    return !prepared_.empty() || wants_material_context();
  }
  void
  begin_material_draw(const ::platform::MaterialDrawContext &context) override {
    native_context_.material = context;
    native_context_.material_active = true;
  }
  void end_material_draw() noexcept override {
    native_context_.material = {};
    native_context_.material_active = false;
  }
  bool replaces_model_geometry() const override {
    return suppress_model_draws_;
  }
  void end_model_draw() noexcept override {
    suppress_model_draws_ = false;
    native_context_ = {};
  }
  void begin_model_draw(const ::platform::ModelDrawContext &context) override {
    // Visual retargeting/fog must not leak exception flags or rounding state
    // into the native polygon producer that resumes after this callback.
    VisualFloatScope float_scope;
    suppress_model_draws_ = false;
    native_context_ = {};
    if (wants_material_context()) {
      native_context_.node = static_cast<const zcls::Node *>(context.node);
      native_context_.model =
          native_context_.node ? native_context_.node->model : nullptr;
      native_context_.epoch = group_load_epoch_;
    }
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
    return native_wsi_ ? "Core Critical Vulkan/SDL WSI"
                       : "Core Critical Vulkan/DXGI";
  }
  void set_shape(const sdlfe::FrameShape *shape) override { shape_ = shape; }
  // Typed mission-change callback from the native missions object: owns the
  // canonical mission ID and the resolved campaign lighting/policy. Unknown or
  // failed identities clear any prior state (no stale profile).
  void mission_changed(const ::platform::MissionIdentity &identity) override {
    VisualFloatScope fp_guard;
    // Drain packets queued for the previous mission before switching state.
    drain();
    local_lights_clear();
    const auto id = mw34::campaign::canonical_mission_id(identity.world_index,
                                                         identity.number);
    mw34::campaign::ResolvedMissionLighting resolved{};
    if (!id.empty())
      resolved = mw34::campaign::resolve_mission_lighting(id);
    lighting_ = mw34::campaign::mission_sun_settings(
        base_lighting_, resolved, campaign_enabled_, sun_explicit_,
        environment_.get(), id);
    display_hdr_ = window_hdr_enabled();
    if (renderer_)
      renderer_->update_lighting_settings(display_lighting());
    const bool identity_changed =
        identity.world_index != current_mission_identity_.world_index ||
        identity.number != current_mission_identity_.number;
    current_mission_identity_ = identity;
    if (id != current_mission_id_ || identity_changed) {
      current_mission_id_ = id;
      current_mission_lighting_ = resolved;
      if (lighting_.enabled && identity.world_index >= 0 &&
          identity.number > 0 && !resolved.has_profile)
        std::fprintf(stderr,
                     "[MISSION_CHANGED] warning=unknown-mission world_index=%d "
                     "number=%d; configured stock lighting restored\n",
                     identity.world_index, identity.number);
      else if (lighting_.enabled)
        std::fprintf(
            stderr,
            "[MISSION_CHANGED] id=%s world_index=%d number=%d has_profile=%d\n",
            current_mission_id_.c_str(), static_cast<int>(identity.world_index),
            static_cast<int>(identity.number), resolved.has_profile ? 1 : 0);
    } else {
      current_mission_lighting_ = resolved;
    }
  }
  void reset_scene_resources() override {
    // Reset the owned mission identity/result via the callback with the empty
    // identity before the scene resources are reset.
    mission_changed(::platform::MissionIdentity{});
    reset_pending_ = true;
    ++group_load_epoch_;
    for (const auto &[index, handle] : surface_normal_textures_)
      textures_.at(handle) = {};
    surface_normal_textures_.clear();
    surface_normals_ = SurfaceNormalState{};
    deferred_material_binding_.reset();
    native_context_ = {};
    logged_first_normal_trace_ = false;
    if (bridge_config_.trace_native_bindings)
      std::fprintf(stderr,
                   "[SURFACE_EPOCH_RESET] epoch=%llu decoded_bytes=%llu "
                   "normal_handles=%zu\n",
                   (unsigned long long)group_load_epoch_,
                   (unsigned long long)surface_normals_.cache.world_bytes,
                   surface_normal_textures_.size());
  }

  ~FrameRenderer() override {
    try {
      drain();
    } catch (const std::exception &error) {
      std::fprintf(stderr, "Core Critical render worker shutdown: %s\n",
                   error.what());
    }
    local_lights_report();
    if (worker_) {
      worker_->stop();
      const auto statistics = worker_->statistics();
      std::fprintf(
          stderr,
          "Render worker: %zu submitted, %zu completed, %zu peak jobs, "
          "%zu peak bytes, %zu producer waits\n",
          statistics.submitted, statistics.completed, statistics.peak_jobs,
          statistics.peak_bytes, statistics.waits);
      worker_.reset(); // All later summary reads and destruction belong to this
                       // thread.
    }
    if (cpu_profile_) {
      const double milliseconds = 1000.0 / SDL_GetPerformanceFrequency();
      std::fprintf(stderr,
                   "CPU renderer profile (includes warmup): %llu presents, "
                   "%llu GPU sharp draws, "
                   "%llu render calls, render %.4f ms, "
                   "transport %.4f ms\n",
                   static_cast<unsigned long long>(presented_frames_.load()),
                   static_cast<unsigned long long>(gpu_sharp_draws_),
                   static_cast<unsigned long long>(render_calls_),
                   render_ticks_ * milliseconds,
                   transport_ticks_ * milliseconds);
      const auto &models = prepared_.cpu_statistics();
      std::fprintf(stderr,
                   "CPU prepared profile (includes warmup, nested intervals): "
                   "%llu calls, "
                   "selection %.4f ms, retarget %.4f ms, deform %.4f ms, "
                   "emission %.4f ms, TL append %.4f ms\n",
                   static_cast<unsigned long long>(models.calls),
                   models.selection_ticks * milliseconds,
                   models.retarget_ticks * milliseconds,
                   models.deform_ticks * milliseconds,
                   prepared_emit_ticks_ * milliseconds,
                   append_ticks_ * milliseconds);
      if (worker_render_ticks_ || worker_present_ticks_)
        std::fprintf(stderr,
                     "CPU worker profile: render %.4f ms, present %.4f ms\n",
                     worker_render_ticks_ * milliseconds,
                     worker_present_ticks_ * milliseconds);
      std::fprintf(stderr,
                   "CPU prepared vertices: %llu vertices, stage %.4f ms, "
                   "assembly %.4f ms\n",
                   static_cast<unsigned long long>(prepared_cpu_.vertices),
                   prepared_cpu_.stage_ticks * milliseconds,
                   prepared_cpu_.assembly_ticks * milliseconds);
    }
    if (geometry_mode_ != cc::geometry::Mode::legacy)
      std::fprintf(
          stderr,
          "Prepared geometry: %llu GPU vertices, %llu reference CPU vertices, "
          "%llu early models, %llu CPU fallback batches\n",
          (unsigned long long)gpu_prepared_vertices_,
          (unsigned long long)prepared_reference_vertices_,
          (unsigned long long)early_prepared_models_,
          (unsigned long long)prepared_cpu_fallback_batches_);
    if (geometry_mode_ != cc::geometry::Mode::legacy)
      std::fprintf(
          stderr,
          "Geometry handoff: %llu submitted models, %llu CPU vertex "
          "preparations skipped, %llu CPU polygon preparations skipped, %llu "
          "unsupported models, %llu CPU 3D draw calls observed\n",
          (unsigned long long)geometry_statistics_.submitted_models,
          (unsigned long long)geometry_statistics_.skipped_vertices,
          (unsigned long long)geometry_statistics_.skipped_polygons,
          (unsigned long long)geometry_statistics_.unsupported_models,
          (unsigned long long)geometry_statistics_.cpu_draw_calls);
    const auto quality = quality_statistics();
    std::fprintf(
        stderr,
        "%s: %llu draws, %llu resident presents, %llu FXAA passes, %llu AF "
        "samplers, %llu mip "
        "levels\n",
        driver_name(),
        static_cast<unsigned long long>(retired_draws_ + renderer_->draws),
        static_cast<unsigned long long>(retired_presents_ +
                                        renderer_->resident_presents),
        static_cast<unsigned long long>(quality.fxaa_passes),
        static_cast<unsigned long long>(quality.anisotropic_samplers),
        static_cast<unsigned long long>(quality.mip_levels));
    const auto assets = asset_statistics();
    if (lighting_.normals)
      std::fprintf(
          stderr,
          "World normal pages: %llu decoded bytes, %llu peak decoded bytes, "
          "%llu loaded pages (536870912-byte limit)\n",
          (unsigned long long)assets.world_normal_decoded_bytes,
          (unsigned long long)assets.world_normal_peak_decoded_bytes,
          (unsigned long long)assets.world_normal_loaded_pages);
    const auto &cache = prepared_draw_cache_.statistics();
    if (cache.hits || cache.misses || cache.bypasses)
      std::fprintf(stderr,
                   "Prepared staging cache: %llu hits, %llu misses, %llu "
                   "bypasses, %zu bytes\n",
                   (unsigned long long)cache.hits,
                   (unsigned long long)cache.misses,
                   (unsigned long long)cache.bypasses, cache.bytes);
    if (assets.prepared_batches || assets.named_textures)
      std::fprintf(
          stderr,
          "Prepared visuals: %llu batches, %llu GPU model draws, %llu "
          "CPU-clipped "
          "triangles, %llu named texture uploads\n",
          static_cast<unsigned long long>(assets.prepared_batches),
          static_cast<unsigned long long>(assets.gpu_model_draws),
          static_cast<unsigned long long>(assets.cpu_clipped_triangles),
          static_cast<unsigned long long>(assets.named_textures));
    if (lighting_.enabled)
      std::fprintf(
          stderr,
          "Prepared materials: %llu per-pixel draws, %llu normal-map draws\n",
          static_cast<unsigned long long>(assets.lit_draws),
          static_cast<unsigned long long>(assets.normal_draws));
    cc_mw3_re::platform::unregister_window_handle(native_window_);
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

  int32_t create_texture(int width, int height, const uint8_t *rgba,
                         uint8_t) override {
    if (width <= 0 || height <= 0 || unsigned(width) > texture_limit_ ||
        unsigned(height) > texture_limit_) {
      throw std::runtime_error("Invalid mw3-re texture size");
    }
    const auto free =
        std::find_if(textures_.begin(), textures_.end(),
                     [](const Texture &entry) { return entry.pixels.empty(); });
    const auto handle = int32_t(free - textures_.begin());
    if (free == textures_.end()) {
      textures_.emplace_back();
    }
    auto &entry = textures_[handle];
    entry.width = unsigned(width);
    entry.height = unsigned(height);
    mw34::materials::SurfaceKey active_key{};
    if (mw34::materials::get_active_surface_key(&active_key)) {
      entry.surface_key = active_key;
    }
    entry.group_load_epoch = group_load_epoch_;
    update_texture(handle, width, height, rgba);
    return handle;
  }

  void update_texture(int32_t handle, int width, int height,
                      const uint8_t *rgba) override {
    auto &entry = textures_.at(handle);
    // Refresh from the original texel producer on every upload. An arbitrary
    // RGBA update has no authenticated 16-bit source identity and must not keep
    // a normal binding merely because it reuses the same renderer handle.
    mw34::materials::SurfaceKey source_key{};
    mw34::materials::get_active_surface_key(&source_key);
    std::vector<uint32_t> replacement;
    if (entry.replacement) {
      if (!rgba || unsigned(width) != entry.source_width ||
          unsigned(height) != entry.source_height)
        throw std::runtime_error(
            "Named texture reload changed its source dimensions");
      replacement = image_pixels(*entry.replacement, rgba, entry.source_width,
                                 entry.source_height);
      rgba = reinterpret_cast<const uint8_t *>(replacement.data());
      width = int(entry.width);
      height = int(entry.height);
      ++named_textures_;
    }
    if (!rgba || unsigned(width) != entry.width ||
        unsigned(height) != entry.height) {
      throw std::runtime_error("Invalid mw3-re texture upload");
    }
    if (entry.scene_index != no_binding || entry.sharp_index != no_binding) {
      flush();
    }
    entry.pixels.resize(std::size_t(width) * height);
    std::memcpy(entry.pixels.data(), rgba,
                entry.pixels.size() * sizeof(std::uint32_t));
    entry.surface_key = source_key;
    entry.group_load_epoch = group_load_epoch_;
    entry.version = ++texture_version_;
  }

  int32_t create_named_texture(const char *name, int width, int height,
                               const uint8_t *rgba, uint8_t type) override {
    mw34::materials::SurfaceKey active_key{};
    mw34::materials::get_active_surface_key(&active_key);
    const auto *replacement = prepared_.named_texture(name);
    if (!replacement) {
      int32_t handle = create_texture(width, height, rgba, type);
      auto &entry = textures_[handle];
      if (name)
        entry.name = name;
      entry.surface_key = active_key;
      entry.group_load_epoch = group_load_epoch_;
      return handle;
    }
    if (!rgba || width <= 0 || height <= 0)
      throw std::runtime_error("Invalid named texture input");
    const auto pixels =
        image_pixels(*replacement, rgba, unsigned(width), unsigned(height));
    const auto handle =
        create_texture(int(replacement->width), int(replacement->height),
                       reinterpret_cast<const uint8_t *>(pixels.data()), type);
    auto &entry = textures_[handle];
    entry.replacement = replacement;
    entry.source_width = unsigned(width);
    entry.source_height = unsigned(height);
    if (name)
      entry.name = name;
    entry.surface_key = active_key;
    entry.group_load_epoch = group_load_epoch_;
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
    if (color && lighting_.enabled && lighting_.suppress_sky)
      append_clear(true, 0, 0, 0);
    else
      append_clear(color, red, green, blue);
  }

  void set_fog_color(uint8_t red, uint8_t green, uint8_t blue) override {
    fog_ = {red / 255.0f, green / 255.0f, blue / 255.0f};
  }
  void begin_scene() override {
    if (!scene_.draws.empty())
      flush();
    complete_local_light_packets();
    ensure_extent();
    const bool hdr_enabled = window_hdr_enabled();
    if (hdr_enabled != display_hdr_) {
      drain();
      display_hdr_ = hdr_enabled;
      renderer_->update_lighting_settings(display_lighting());
    }
    local_lights_begin();
  }
  void end_scene() override {
    flush();
    complete_local_light_packets();
    local_active_ = false;
  }

  void draw(Primitive primitive, const TLVertex *vertices, int count,
            const DrawState &state) override {
    // Only the explicitly marked 2D compositor may supply CPU vertices in
    // strict mode. Depth-always also occurs in 3D overlays, so it cannot
    // identify a HUD exemption.
    if (count > 0 && !state.sharp &&
        geometry_mode_ != cc::geometry::Mode::legacy) {
      ++geometry_statistics_.cpu_draw_calls;
      if (geometry_mode_ == cc::geometry::Mode::gpu)
        throw std::runtime_error(
            "Strict GPU geometry: CPU 3D draw is not implemented");
    }
    if (suppress_model_draws_)
      return;
    local_lights_observe(vertices, count, state);
    append_draw(primitive, vertices, count, state,
                state.sharp || (state.depth_always &&
                                !authenticated_deferred_world_draw()));
  }

  void clear_depth_rect(int left, int top, int right, int bottom) override {
    if (right <= left || bottom <= top)
      return;
    const float l = float(left), t = float(top), r = float(right),
                b = float(bottom);
    const TLVertex vertices[] = {{l, t, 0, 1, 0, 0xff000000, 0, 0},
                                 {r, t, 0, 1, 0, 0xff000000, 0, 0},
                                 {r, b, 0, 1, 0, 0xff000000, 0, 0},
                                 {l, b, 0, 1, 0, 0xff000000, 0, 0}};
    DrawState state{};
    state.blend = state.depth_write = state.depth_always = true;
    // Preserve the inherited rasterized depth-only operation and its fractional
    // viewport coverage, without treating it as a color/HUD composition
    // boundary.
    append_draw(Primitive::kFan, vertices, 4, state, scene_.hud, true);
  }

private:
  int32_t prepared_normal(const ::platform::prepared::NormalImage &image) {
    const auto found = prepared_normals_.find(&image);
    if (found != prepared_normals_.end())
      return found->second;
    const auto handle = create_texture(
        int(image.width), int(image.height),
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
      vertex.sx =
          (vertex.sx - viewport_[0]) * scene_.width / float(viewport_[2]);
      vertex.sy =
          (vertex.sy - viewport_[1]) * scene_.height / float(viewport_[3]);
      if (vertex.rhw <= 0) {
        vertex.rhw = 1;
      }
      const auto *bytes = reinterpret_cast<const unsigned char *>(&vertex);
      scene_.vertices.insert(scene_.vertices.end(), bytes,
                             bytes + sizeof(TLVertex));
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
    const unsigned drawn =
        unsigned(scene_.vertices.size() / sizeof(TLVertex)) - first;
    if (!drawn) {
      return;
    }
    if (state.wireframe) {
      throw std::runtime_error(
          "Wireframe is unavailable in the Vulkan Training trial");
    }

    vk::Draw draw{};
    draw.native_sky = state.native_sky;
    draw.depth_only = depth_only;
    draw.first = first;
    draw.count = drawn;
    draw.texture =
        state.texture < 0 ? -1 : int(bind_texture(state.texture, state.sharp));
    if (lighting_.enabled && state.texture >= 0) {
      const auto &tex_entry = textures_.at(state.texture);
      const auto key = tex_entry.surface_key;
      const auto epoch = tex_entry.group_load_epoch;
      const auto tex_name = tex_entry.name;
      if (epoch == group_load_epoch_ && key.hash != 0) {
        int norm_idx = lighting_.normals
                           ? deferred_surface_normal_index(key, state.texture)
                           : -1;
        if (norm_idx >= 0) {
          int norm_handle = surface_normal_texture(norm_idx);
          draw.normal_texture = int(bind_texture(norm_handle, false));
        } else {
          draw.normal_texture = -1;
        }
        auto tl = std::make_shared<mw34::sun::Tl>();
        const auto &proj = zmth::projection();
        if (proj.near_z > 0 && proj.fx_near > 0 && proj.fy_near > 0 &&
            viewport_[2] > 0 && viewport_[3] > 0) {
          tl->projection = {
              proj.near_z,
              proj.fx_near * float(scene_.width) / float(viewport_[2]),
              proj.fy_near * float(scene_.height) / float(viewport_[3]),
              (proj.cx - float(viewport_[0])) * float(scene_.width) /
                  float(viewport_[2]),
              (proj.cy - float(viewport_[1])) * float(scene_.height) /
                  float(viewport_[3]),
              proj.far_z > 0 ? proj.far_z : 1000.0f};
          const float *m = zmth::view().camera.m;
          tl->camera_world = {m[0], m[3],  m[6], m[9], m[1], m[4],
                              m[7], m[10], m[2], m[5], m[8], m[11]};
        } else {
          tl->projection = {1.0f,
                            float(scene_.width),
                            float(scene_.height),
                            float(scene_.width) * .5f,
                            float(scene_.height) * .5f,
                            1000.0f};
          tl->camera_world = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        }
        draw.sun_tl = std::move(tl);
        if (lighting_.normals) {
          draw.surface_parameters =
              deferred_surface_parameters(key, state.texture);
          if (draw.surface_parameters[3] > .5f && proj.near_z > 0 &&
              proj.fx_near > 0 && proj.fy_near > 0 && viewport_[2] > 0 &&
              viewport_[3] > 0) {
            const auto &eye = draw.sun_tl->camera_world;
            draw.surface_eye = {eye[3], eye[7], eye[11], 1.f};
          }
        }
        auto norms = std::make_shared<std::vector<std::array<float, 3>>>(
            drawn, std::array<float, 3>{0, 0, 0});
        draw.lit_normals = std::move(norms);
        if (norm_idx >= 0 && !logged_first_normal_trace_) {
          logged_first_normal_trace_ = true;
          std::fprintf(
              stderr,
              "[SURFACE_NORMAL_TRACE] First authenticated surface "
              "normal bound: "
              "name=%s key={w=%u, h=%u, fmt=%u, hash=0x%llx} slot=%d "
              "normal_slot=%d normal_index=%d catalog=%s "
              "uv_transform=[1,0,0,0,1,0] sampler_address=[%d,%d] "
              "proj=[near=%f, fx_near=%f, fy_near=%f, cx=%f, cy=%f] "
              "viewport=[%d,%d,%d,%d] scene=[%u,%u] v0=[sx=%f, sy=%f, "
              "sz=%f, rhw=%f, u=%f, v=%f]\n",
              tex_name.c_str(), key.width, key.height, key.format,
              (unsigned long long)key.hash, draw.texture, draw.normal_texture,
              norm_idx,
              surface_normals_.cache.authored.count(key) ? "authored" : "world",
              state.address_u, state.address_v, proj.near_z, proj.fx_near,
              proj.fy_near, proj.cx, proj.cy, viewport_[0], viewport_[1],
              viewport_[2], viewport_[3], scene_.width, scene_.height,
              vertices[0].sx, vertices[0].sy, vertices[0].sz, vertices[0].rhw,
              vertices[0].tu, vertices[0].tv);
        }
      }
    }
    const bool sharp = state.sharp && state.texture >= 0 &&
                       !textures_.at(state.texture).held_copy;
    draw.sampler = (state.address_u == 3 ? 3U : 1U) << 4 |
                   (state.address_v == 3 ? 3U : 1U) << 8 |
                   (state.sharp && !sharp ? 0U : 3U);
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
    draw.push.mode[2] = draw.texture < 0
                            ? 2 | diffuse
                            : (state.alpha_modulate ? 4 : 2) | texture;
    draw.scissor = {{0, 0}, {scene_.width, scene_.height}};
    if (!scene_.order.empty() && !scene_.order.back().clear) {
      auto &previous = scene_.draws.back();
      if (!previous.model && previous.depth_only == draw.depth_only &&
          previous.native_sky == draw.native_sky &&
          previous.texture == draw.texture &&
          previous.normal_texture == draw.normal_texture &&
          bool(previous.sun_tl) == bool(draw.sun_tl) &&
          (!draw.sun_tl ||
           (previous.sun_tl->camera_world == draw.sun_tl->camera_world &&
            previous.sun_tl->projection == draw.sun_tl->projection)) &&
          bool(previous.lit_normals) == bool(draw.lit_normals) &&
          previous.surface_parameters == draw.surface_parameters &&
          previous.surface_eye == draw.surface_eye &&
          previous.sampler == draw.sampler &&
          previous.key.packed() == draw.key.packed() &&
          std::memcmp(&previous.push, &draw.push, sizeof(draw.push)) == 0) {
        if (previous.lit_normals && draw.lit_normals) {
          auto merged_normals =
              std::make_shared<std::vector<std::array<float, 3>>>(
                  *previous.lit_normals);
          merged_normals->insert(merged_normals->end(),
                                 draw.lit_normals->begin(),
                                 draw.lit_normals->end());
          previous.lit_normals = std::move(merged_normals);
        }
        previous.count += drawn;
        return;
      }
    }
    scene_.order.push_back({false, unsigned(scene_.draws.size())});
    scene_.draws.push_back(std::move(draw));
    if (sharp)
      ++gpu_sharp_draws_;
  }

#include "frame_submission.inc"

  bool read_back(std::vector<uint32_t> *argb, int *width,
                 int *height) override {
    std::vector<uint8_t> bgra;
    unsigned w = 0, h = 0;
    flush();
    complete_local_light_packets();
    const auto capture = [&] { renderer_->capture_fullres_bgra(bgra, w, h); };
    if (worker_)
      worker_->call(capture);
    else
      capture();
    if (bgra.empty()) {
      return false;
    }
    const mw34::display::Samples samples{quality_.samples_x,
                                         quality_.samples_y};
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
              sum += bgra[((y * samples.y + sy) * w + x * samples.x + sx) * 4 +
                          channel];
          const unsigned count = samples.x * samples.y;
          pixel |= ((sum + count / 2) / count) << (channel * 8);
        }
        (*argb)[std::size_t(y) * *width + x] = pixel;
      }
    return true;
  }

  bool hold_frame() override {
    return read_back(&held_, &held_width_, &held_height_);
  }
  bool restore_frame() override {
    ensure_extent();
    if (held_.empty() || unsigned(held_width_) != scene_.width ||
        unsigned(held_height_) != scene_.height) {
      return false;
    }
    const auto rgba = rgba_copy(held_);
    const auto handle =
        create_texture(held_width_, held_height_,
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

#include "frame_presentation.inc"

private:
  int32_t prepared_texture(const ::platform::prepared::Image &image) {
    const auto found = prepared_textures_.find(&image);
    if (found != prepared_textures_.end())
      return found->second;
    const auto pixels = image_pixels(image);
    const auto handle =
        create_texture(int(image.width), int(image.height),
                       reinterpret_cast<const uint8_t *>(pixels.data()), 1);
    prepared_textures_.emplace(&image, handle);
    return handle;
  }

  std::shared_ptr<const vk::ModelGeometry>
  prepared_geometry(const ::platform::prepared::Batch &batch) {
    const auto found = prepared_geometry_.find(&batch);
    if (found != prepared_geometry_.end())
      return found->second;
    auto geometry = std::make_shared<vk::ModelGeometry>();
    geometry->key = 0x4343000000000000ULL + prepared_geometry_.size() + 1;
    geometry->vertices.reserve(batch.vertices.size());
    for (const auto &vertex : batch.vertices)
      geometry->vertices.push_back({vertex[0], vertex[1], vertex[2], vertex[3],
                                    vertex[4], vertex[5], vertex[6],
                                    vertex[7]});
    geometry->hash = 0xcbf29ce484222325ULL;
    const auto *bytes =
        reinterpret_cast<const uint8_t *>(geometry->vertices.data());
    for (size_t i = 0; i < geometry->vertices.size() * sizeof(vk::ModelVertex);
         ++i) {
      geometry->hash ^= bytes[i];
      geometry->hash *= 0x100000001b3ULL;
    }
    prepared_geometry_.emplace(&batch, geometry);
    return geometry;
  }

  void emit_prepared(const PreparedBatchDraw &source,
                     const ::platform::ModelDrawContext &context) {
    CpuInterval interval(cpu_profile_ ? &prepared_emit_ticks_ : nullptr);
    if (geometry_mode_ != cc::geometry::Mode::legacy &&
        source.deformed.empty()) {
      emit_gpu_prepared(source, context, prepared_fog());
      return;
    }
    if (geometry_mode_ == cc::geometry::Mode::gpu)
      throw std::runtime_error(
          "Strict GPU geometry: prepared deformation is not implemented");
    if (geometry_mode_ != cc::geometry::Mode::legacy)
      ++prepared_cpu_fallback_batches_;
    const auto staged = prepared_draw_cache_.get(
        source, context, viewport_, scene_.width, scene_.height,
        prepared_geometry(*source.batch), lighting_.enabled,
        cpu_profile_ ? &prepared_cpu_ : nullptr, prepared_cache_enabled_);
    auto draw = staged->draw;
    apply_surface_material(draw, source, lighting_.normals);
    draw.first = unsigned(scene_.vertices.size() / sizeof(TLVertex));
    draw.texture = int(bind_texture(prepared_texture(*source.texture), false));
    if (source.normal && lighting_.normals)
      draw.normal_texture =
          int(bind_texture(prepared_normal(*source.normal), false));
    scene_.vertices.insert(scene_.vertices.end(), staged->vertices.begin(),
                           staged->vertices.end());
    cpu_clipped_triangles_ += staged->clipped_triangles;
    if (draw.count) {
      scene_.order.push_back({false, unsigned(scene_.draws.size())});
      scene_.draws.push_back(std::move(draw));
      ++prepared_batches_;
    }
  }
  bool window_hdr_enabled() const {
    return SDL_GetBooleanProperty(SDL_GetWindowProperties(window_),
                                  SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false);
  }

  mw34::sun::Settings display_lighting() const {
    return mw34::sun::display_ambient(lighting_, campaign_enabled_, display_hdr_);
  }

  void initialize_renderer() {
    display_hdr_ = window_hdr_enabled();
    auto source = native_wsi_ ? sdl_surface(window_, window_state_tracker_)
                              : vk::Renderer::WindowSurface{};
    renderer_ = std::make_unique<vk::Renderer>(
        vk::Renderer::ColorTarget::rgba8, std::move(source), quality_,
        !prepared_.empty() || geometry_mode_ != cc::geometry::Mode::legacy ||
            lighting_.enabled,
        display_lighting(), geometry_mode_ != cc::geometry::Mode::legacy,
        bridge_config_);
    texture_limit_ = renderer_->max_texture_dimension(native_window_);
    if (!native_wsi_ && !renderer_->dxgi_presenting()) {
      throw std::runtime_error("Core Critical DXGI transport is unavailable");
    }
  }

  void ensure_extent() {
    if (reset_pending_) {
      drain();
      retired_draws_ += renderer_->draws;
      retired_presents_ += renderer_->resident_presents;
      retired_quality_ = quality_statistics();
      retired_model_draws_ += renderer_->model_draws;
      retired_model_uploads_ += renderer_->model_uploads;
      retired_lit_draws_ += renderer_->sun_world_draws +
                            renderer_->sun_model_draws +
                            renderer_->sun_tl_draws;
      retired_normal_draws_ += renderer_->normal_draws;
      retired_geometry_buffers_ += renderer_->retired_model_buffers;
      initialize_renderer();
      submitted_textures_.clear();
      window_bound_ = false;
      resident_width_ = resident_height_ = 0;
      scene_ = {};
      present_window_width_ = present_window_height_ = 0;
      reset_pending_ = false;
    }
    if (scene_.width) {
      return;
    }
    int width = 0, height = 0;
    if (native_wsi_ && window_state_tracker_) {
      const auto state = window_state_tracker_->sample_and_get();
      width = state.drawable_width;
      height = state.drawable_height;
    } else {
      SDL_GetWindowSizeInPixels(window_, &width, &height);
    }
    if (width <= 0 || height <= 0) {
      width = resident_width_ ? int(resident_width_) : 640;
      height = resident_height_ ? int(resident_height_) : 480;
    }
    const auto fit = sdlfe::fit_rect(float(width), float(height),
                                     shape_ ? *shape_ : sdlfe::FrameShape{});
    scene_.width = unsigned(std::max(1.0f, fit.w));
    scene_.height = unsigned(std::max(1.0f, fit.h));
    scene_.has_depth = true;
    if (scene_.width != resident_width_ || scene_.height != resident_height_) {
      if (worker_)
        worker_->drain();
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
    vk::TextureRef ref{key, entry.version, w, h, vk::TexFormat::rgba8888, {},
                       {}};
    const auto current = submitted_textures_.find(ref.key);
    if (current == submitted_textures_.end() ||
        current->second != std::array<std::uint64_t, 3>{ref.hash, w, h}) {
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
      rgba.push_back((pixel & 0xff00ff00U) | ((pixel & 255U) << 16) |
                     ((pixel >> 16) & 255U));
    }
    return rgba;
  }

  void save_capture() {
    std::vector<uint32_t> pixels;
    int width = 0, height = 0;
    if (!read_back(&pixels, &width, &height)) {
      throw std::runtime_error("Core Critical frame capture had no pixels");
    }
    auto *surface = SDL_CreateSurfaceFrom(
        width, height, SDL_PIXELFORMAT_ARGB8888, pixels.data(), width * 4);
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
  std::shared_ptr<cc_mw3_re::platform::SdlWindowStateTracker>
      window_state_tracker_;
  cc_mw3_re::platform::NativeWindowHandle native_window_;
  HWND hwnd_;
  const bool native_wsi_;
  const vk::Renderer::Quality quality_;
  mw34::sun::Settings lighting_;
  bool display_hdr_ = false;
  const mw34::sun::Settings base_lighting_;
  const bool campaign_enabled_, sun_explicit_;
  std::shared_ptr<const ::platform::prepared::EnvironmentSelection>
      environment_;
  // Owned current-mission state: canonical mission ID plus resolved campaign
  // lighting/policy. The profile pointer refers only to the static campaign
  // profile database (no borrowed native pointers).
  ::platform::MissionIdentity current_mission_identity_{};
  std::string current_mission_id_;
  mw34::campaign::ResolvedMissionLighting current_mission_lighting_{};
  PreparedModels prepared_;
  const cc::geometry::Mode geometry_mode_;
  const bool geometry_debug_;
  const RendererBridgeConfig bridge_config_;
  cc::geometry::Statistics geometry_statistics_;
  struct NativeGeometry {
    std::weak_ptr<const cc::geometry::Resource> source;
    std::shared_ptr<const vk::ModelGeometry> geometry;
  };
  std::map<std::uint64_t, NativeGeometry> native_geometry_;
  std::unique_ptr<vk::Renderer> renderer_;
  // Declared after renderer_: join before destroying the backend, including
  // unwinding. Only the producer accesses textures and builds the next scene.
  std::unique_ptr<RenderWorker> worker_;
  std::mutex recycle_mutex_;
  std::vector<vk::Scene> recycled_scenes_;
  std::map<std::uint64_t, std::array<std::uint64_t, 3>> submitted_textures_;
  const sdlfe::FrameShape *shape_{};
  vk::Scene scene_{};
  std::array<int, 4> viewport_{0, 0, 640, 480};
  std::array<float, 3> fog_{};
  std::vector<Texture> textures_;
  std::vector<uint32_t> held_;
  std::string capture_path_;
  std::uint64_t texture_version_{}, retired_draws_{}, retired_presents_{};
  std::atomic<std::uint64_t> presented_frames_{}, resident_draws_{};
  std::uint64_t retired_geometry_buffers_{};
  QualityStatistics retired_quality_{};
  std::map<const ::platform::prepared::Image *, int32_t> prepared_textures_;
  std::map<const ::platform::prepared::NormalImage *, int32_t>
      prepared_normals_;
  std::map<const ::platform::prepared::Batch *,
           std::shared_ptr<const vk::ModelGeometry>>
      prepared_geometry_;
  std::uint64_t group_load_epoch_{1};
  std::map<int, int32_t> surface_normal_textures_;
  bool logged_first_normal_trace_{false};
  std::uint64_t prepared_batches_{}, retired_model_draws_{},
      cpu_clipped_triangles_{}, named_textures_{};
  bool suppress_model_draws_{};
  std::uint64_t retired_model_uploads_{};
  std::uint64_t retired_lit_draws_{}, retired_normal_draws_{};
  unsigned texture_limit_{}, resident_width_{}, resident_height_{};
  int held_width_{}, held_height_{};
  int32_t present_window_width_{}, present_window_height_{};
  bool window_bound_{}, presented_{}, reported_resident_{}, reset_pending_{};
  const bool cpu_profile_ = cpu_profile_requested();
  std::uint64_t render_ticks_{}, transport_ticks_{};
  std::uint64_t worker_render_ticks_{}, worker_present_ticks_{};
  std::uint64_t gpu_sharp_draws_{}, render_calls_{};
  std::uint64_t prepared_emit_ticks_{}, append_ticks_{};
  PreparedDrawCpuStatistics prepared_cpu_;
  std::uint64_t gpu_prepared_vertices_{}, prepared_reference_vertices_{},
      early_prepared_models_{}, prepared_cpu_fallback_batches_{};
  const bool prepared_cache_enabled_;
  PreparedDrawCache prepared_draw_cache_;
};

} // namespace

RendererBridgeConfig configure_bridge(sdlfe::RendererBackend backend,
                                      const sdlfe::CoreCriticalQuality &quality,
                                      const sdlfe::CoreCriticalAssets &assets) {
  auto config = RendererBridgeConfig::from_diagnostic_env();
  if (assets.environment) {
    std::fprintf(stderr,
                 "Environment renderer catalogs: %zu authored, %zu world\n",
                 assets.environment->authored_manifests.size(),
                 assets.environment->world_manifests.size());
    config.environment_authored_manifests =
        assets.environment->authored_manifests;
    config.environment_world_manifests = assets.environment->world_manifests;
  }
  config.dxgi_present = (backend != sdlfe::RendererBackend::core_critical_wsi);
  config.window_mode = "windowed";
  config.world_scale_percent = 100;
  config.remaster_width = 0;
  config.remaster_height = 0;
  config.vsync = true;
  config.temporal_inputs = false;
  config.gpu_models = (assets.geometry != "legacy");
  config.geometry = assets.geometry;
  config.render_thread = assets.render_thread;
  config.geometry_debug_normals = assets.geometry_debug_normals;

  // Quality
  config.antialiasing = quality.antialiasing;
  config.anisotropy = quality.anisotropy;
  if (quality.antialiasing == "fxaa") {
    config.fxaa = true;
    config.samples_x = 1;
    config.samples_y = 1;
  } else if (quality.antialiasing == "ssaa2") {
    config.fxaa = false;
    config.samples_x = 2;
    config.samples_y = 1;
  } else if (quality.antialiasing == "ssaa4") {
    config.fxaa = false;
    config.samples_x = 2;
    config.samples_y = 2;
  } else if (quality.antialiasing != "off") {
    throw std::runtime_error("--cc-aa supports off, fxaa, ssaa2 or ssaa4");
  }
  if (quality.anisotropy != "1" && quality.anisotropy != "2" &&
      quality.anisotropy != "4" && quality.anisotropy != "8" &&
      quality.anisotropy != "16") {
    throw std::runtime_error("--cc-af supports 1, 2, 4, 8 or 16");
  }
  config.af_level = unsigned(std::stoi(quality.anisotropy));

  // Lighting
  config.lighting = assets.lighting;
  if (assets.lighting != "off" && assets.lighting != "flat" &&
      assets.lighting != "normal")
    throw std::runtime_error("--cc-lighting supports off, flat or normal");
  auto number = [](const std::string &text, float low, float high) {
    char *end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end || !std::isfinite(value) || value < low ||
        value > high)
      throw std::runtime_error(
          "Invalid --cc-sun angle (azimuth -360..360, elevation 1..89)");
    return value;
  };
  config.sun_azimuth = number(assets.sun_azimuth, -360.0f, 360.0f);
  config.sun_elevation = number(assets.sun_elevation, 1.0f, 89.0f);
  config.sun = assets.lighting != "off" || assets.geometry != "legacy";
  config.sun_normals = (assets.lighting == "normal");
  config.sun_shadows = false;
  config.normal_maps_enabled = (assets.lighting == "normal");

  // Other defaults
  config.fixture_shadows = false;
  config.fixture_atlas_reuse = true;
  config.fixture_atlas_tracked = false;
  config.fixture_atlas_audit = false;
  config.shadow_batch = false;

  return config;
}

std::unique_ptr<VulkanFrameRenderer>
make_renderer(SDL_Window *window, sdlfe::RendererBackend backend,
              const sdlfe::CoreCriticalQuality &quality,
              const sdlfe::CoreCriticalAssets &assets) {
  return std::make_unique<FrameRenderer>(window, backend, quality, assets);
}

} // namespace cc_mw3_re
