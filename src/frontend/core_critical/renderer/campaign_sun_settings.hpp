#pragma once
#include "campaign_lighting.hpp"
#include "platform/prepared_visuals.h"
#include "sun_lighting.hpp"
#include <string>
namespace mw34::campaign {
inline std::string canonical_mission_id(int world, int number) {
  static constexpr const char *worlds[] = {"c1", "c2", "c3", "c4", "c4", "t1"};
  static constexpr int first[] = {1, 1, 1, 1, 4, 1},
                       last[] = {4, 4, 6, 3, 6, 4};
  if (world < 0 || world >= 6 || number < first[world] || number > last[world])
    return {};
  return std::string(worlds[world]) + "m" + std::to_string(number);
}
// Start from immutable configuration on every transition. Unknown/failure/reset
// must never inherit the preceding mission's colour or directional intensity.
inline sun::Settings
mission_sun_settings(const sun::Settings &base,
                     const ResolvedMissionLighting &mission, bool enabled,
                     bool explicit_angles,
                     const platform::prepared::EnvironmentSelection *environment = nullptr,
                     const std::string &mission_id = {}) {
  auto result = base;
  if (!enabled || !base.enabled || !mission.has_profile || !mission.environment)
    return result;
  const auto &profile = *mission.environment;
  if (!explicit_angles) {
    result.azimuth = profile.azimuth;
    result.elevation = profile.elevation;
  }
  result.color = profile.sun_color;
  result.strength = mission.policy.suppress_sun ? 0.f : profile.sun_strength;
  result.ambient_color = profile.ambient_color;
  result.ambient = profile.ambient_strength;
  result.night_world_gain = profile.night_world_gain;
  result.sdr_extra_shadow_lift = mission_id == "c1m2" ? .9f : 0.f;
  result.suppress_sky = mission.policy.suppress_sky;
  result.sky_source = static_cast<float>(mission.policy.sky_source);
  result.sky_tint = profile.sky_tint;
  result.sky_brightness = profile.sky_brightness;
  result.glow_color = profile.sky_glow_color;
  result.glow_strength = profile.sky_glow_strength;
  result.glow_focus = profile.sky_glow_focus;
  result.anti_brightness = profile.sky_grade[0];
  result.anti_saturation = profile.sky_grade[1];
  result.anti_haze = profile.sky_grade[2];
  result.horizon_low = profile.horizon_low;
  result.horizon = profile.horizon_high;
  result.haze_color = profile.haze_color;
  result.haze_manual = true;
  result.sky_active = result.haze_active = result.sky_shading = true;
  if (environment && !mission_id.empty()) {
    const auto found = environment->mission_ambient.find(mission_id);
    if (found != environment->mission_ambient.end()) {
      result.ambient_color = found->second.color;
      result.ambient = found->second.strength;
    }
    const auto found_sky = environment->mission_sky.find(mission_id);
    if (found_sky != environment->mission_sky.end()) {
      result.sky_tint = found_sky->second.tint;
      result.sky_brightness = found_sky->second.brightness;
    }
    const auto found_glow = environment->mission_sky_glow.find(mission_id);
    if (found_glow != environment->mission_sky_glow.end()) {
      result.glow_color = found_glow->second.color;
      result.glow_strength = found_glow->second.strength;
      result.glow_focus = found_glow->second.focus;
    }
    const auto found_horizon = environment->mission_horizon.find(mission_id);
    if (found_horizon != environment->mission_horizon.end() &&
        !result.suppress_sky) {
      result.horizon_low = found_horizon->second.low;
      result.horizon = found_horizon->second.high;
    }
  }
  return result;
}
} // namespace mw34::campaign
