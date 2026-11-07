#pragma once
#include "campaign_lighting.hpp"
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
                     bool explicit_angles) {
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
  result.suppress_sky = mission.policy.suppress_sky;
  return result;
}
} // namespace mw34::campaign
