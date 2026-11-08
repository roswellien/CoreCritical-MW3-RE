#pragma once
#include "effect_lighting.hpp"
#include "renderer_bridge_config.hpp"
#include "sun_lighting.hpp"

namespace mw34::effects {
struct Settings {
  bool enabled{}, fixtures{true}, fixture_shadows{};
  unsigned fixture_shadow_size{256};
  float fixture_retain_distance{160}, fixture_fade_distance{32};
  unsigned limit{64}, weapon_reserve{16};
  float strength{1}, night_gain{1}, fixture_range_scale{1};
};

inline Settings from_config(const cc_mw3_re::RendererBridgeConfig &cfg,
                            bool sun_enabled) {
  Settings v;
  v.enabled = cfg.effect_lights;
  v.fixtures = cfg.fixture_lights;
  if (v.fixtures && !v.enabled)
    throw std::runtime_error("Fixture lights require effect lights");
  if (v.enabled && cfg.effect_light_catalog.empty())
    throw std::runtime_error("Effect lights require a material catalog");
  v.fixture_shadows = cfg.fixture_shadows;
  v.limit = 64;
  v.strength = 1;
  v.weapon_reserve = 16;
  v.fixture_range_scale = 1;
  v.fixture_shadow_size = 256;
  v.fixture_retain_distance = 160;
  v.fixture_fade_distance = 32;
  v.night_gain = 1;
  if ((v.enabled || v.night_gain != 1) && !sun_enabled)
    throw std::runtime_error("Effect/night lighting requires MW34_SUN=1");
  return v;
}

inline const Settings &settings() {
  static const Settings s =
      from_config(cc_mw3_re::RendererBridgeConfig{}, false);
  return s;
}
} // namespace mw34::effects
