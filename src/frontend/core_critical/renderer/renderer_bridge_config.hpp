#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace cc_mw3_re {

struct RendererBridgeConfig {
  // Platform / Presentation
  bool dxgi_present{true};
  std::string window_mode{"windowed"};
  bool vsync{true};
  std::string present_mode;

  // Output / Resolution
  unsigned world_scale_percent{100};
  unsigned remaster_width{0};
  unsigned remaster_height{0};

  // Quality / AA / AF
  std::string antialiasing{"off"};
  std::string anisotropy{"1"};
  unsigned samples_x{1};
  unsigned samples_y{1};
  bool fxaa{false};
  unsigned af_level{1};

  // Geometry / Model / Threading
  std::string geometry{"legacy"};
  bool render_thread{false};
  bool geometry_debug_normals{false};
  bool gpu_models{false};
  bool normal_maps_enabled{false};
  bool prepared_cache{true};

  // Lighting / Sun
  std::string lighting{"off"};
  bool sun{false};
  bool sun_normals{false};
  bool sun_shadows{false};
  float sun_azimuth{45.0f};
  float sun_elevation{35.0f};

  // Fixtures / Local Lights / Shadow Atlas
  bool effect_lights{false};
  std::string effect_light_catalog;
  bool fixture_lights{false};
  bool fixture_shadows{false};
  bool fixture_atlas_reuse{true};
  bool fixture_atlas_tracked{false};
  bool fixture_atlas_audit{false};
  bool shadow_batch{false};

  // Optional normal catalogs are captured once per renderer. Runtime lookups
  // must not observe another renderer's later environment changes.
  std::string surface_normal_catalog;
  std::string world_normal_manifest;
  std::vector<std::string> environment_authored_manifests,
      environment_world_manifests;
  std::string native_material_sidecar;
  std::string native_material_asset_id;
  bool trace_native_bindings{false};

  // Surface Detail
  std::optional<double> detail_fixed_time{std::nullopt};

  // Temporal
  bool temporal_inputs{false};

  // Diagnostics / Debug (read-only diagnostic env overrides)
  bool vk_validation{false};
  bool seh_log{false};
  bool gpu_profile{false};
  bool cpu_profile{false};
  bool buffer_reserve{true};
  bool buffer_growth_trace{false};
  bool tl_combine{true};
  std::string texture_staging;
  std::string sky_debug;
  std::string sun_shadow_audit;

  static RendererBridgeConfig from_diagnostic_env() {
    RendererBridgeConfig cfg;
    auto read_env_bool = [](const char *name, bool fallback) {
      const char *value = std::getenv(name);
      if (!value)
        return fallback;
      if (std::strcmp(value, "0") == 0)
        return false;
      if (std::strcmp(value, "1") == 0)
        return true;
      throw std::runtime_error(
          std::string("Invalid boolean environment value: ") + name);
    };
    cfg.effect_lights = read_env_bool("MW34_EFFECT_LIGHTS", false);
    cfg.fixture_lights =
        read_env_bool("MW34_FIXTURE_LIGHTS", cfg.effect_lights);
    if (cfg.fixture_lights && !cfg.effect_lights)
      throw std::runtime_error(
          "MW34_FIXTURE_LIGHTS requires MW34_EFFECT_LIGHTS");
    if (const char *path = std::getenv("MW34_EFFECT_LIGHT_CATALOG"))
      cfg.effect_light_catalog = path;
    if (const char *path = std::getenv("MW34_NATIVE_MATERIALS"))
      cfg.native_material_sidecar = path;
    if (const char *id = std::getenv("MW34_NATIVE_MATERIAL_ID"))
      cfg.native_material_asset_id = id;
    if (const char *value = std::getenv("MW34_NATIVE_BINDING_TRACE"))
      cfg.trace_native_bindings = std::strcmp(value, "1") == 0;
    if (const char *path = std::getenv("MW34_SURFACE_NORMALS"))
      cfg.surface_normal_catalog = path;
    if (const char *path = std::getenv("MW34_WORLD_NORMALS"))
      cfg.world_normal_manifest = path;
    if (const char *v = std::getenv("MW34_VK_VALIDATION")) {
      cfg.vk_validation = (std::strcmp(v, "1") == 0);
    }
    if (const char *v = std::getenv("MW34_SEH_LOG")) {
      cfg.seh_log = (v != nullptr && *v != '0');
    }
    if (const char *v = std::getenv("MW3_CC_GPU_PROFILE")) {
      cfg.gpu_profile = (std::strcmp(v, "1") == 0);
    }
    if (const char *v = std::getenv("MW3_CC_CPU_PROFILE")) {
      cfg.cpu_profile = (std::strcmp(v, "1") == 0);
    }
    if (const char *v = std::getenv("MW3_CC_PREPARED_CACHE")) {
      if (std::strcmp(v, "0") == 0)
        cfg.prepared_cache = false;
      else if (std::strcmp(v, "1") == 0)
        cfg.prepared_cache = true;
    }
    if (const char *v = std::getenv("MW34_WORLD_SCALE_PERCENT")) {
      if (*v) {
        unsigned res = 0;
        for (const char *c = v; *c; ++c) {
          if (*c < '0' || *c > '9' || res > 100)
            break;
          res = res * 10 + unsigned(*c - '0');
        }
        if (res >= 50 && res <= 100)
          cfg.world_scale_percent = res;
      }
    }
    if (const char *v = std::getenv("MW34_DETAIL_FIXED_TIME")) {
      char *end = nullptr;
      const double val = std::strtod(v, &end);
      if (end != v && !*end)
        cfg.detail_fixed_time = val;
    }
    if (const char *v = std::getenv("MW34_TEMPORAL_INPUTS")) {
      if (std::strcmp(v, "1") == 0)
        cfg.temporal_inputs = true;
      else if (std::strcmp(v, "0") == 0)
        cfg.temporal_inputs = false;
    }
    if (const char *v = std::getenv("MW34_BUFFER_RESERVE")) {
      if (std::strcmp(v, "0") == 0)
        cfg.buffer_reserve = false;
      else if (std::strcmp(v, "1") == 0)
        cfg.buffer_reserve = true;
    }
    if (std::getenv("MW34_BUFFER_GROWTH_TRACE")) {
      cfg.buffer_growth_trace = true;
    }
    if (const char *v = std::getenv("MW34_TL_COMBINE")) {
      if (std::strcmp(v, "0") == 0)
        cfg.tl_combine = false;
      else if (std::strcmp(v, "1") == 0)
        cfg.tl_combine = true;
    }
    if (const char *v = std::getenv("MW34_TEXTURE_STAGING")) {
      cfg.texture_staging = v;
    }
    if (const char *v = std::getenv("MW34_SKY_DEBUG")) {
      cfg.sky_debug = v;
    }
    if (const char *v = std::getenv("MW34_SUN_SHADOW_AUDIT")) {
      cfg.sun_shadow_audit = v;
    }
    return cfg;
  }
};

} // namespace cc_mw3_re
