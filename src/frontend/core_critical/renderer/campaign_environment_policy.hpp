#pragma once

#include <string_view>

namespace mw34::campaign {

// Presentation policy follows the approved 20 campaign + 4 Training mission
// schedule. Sealed maps are protected by code identity so profile data or
// environment packs cannot reopen subterranean interior installations.
enum class SkySource : unsigned {
  OriginalClouds = 1,
  ClearDay = 2,
  Twilight = 3,
  ClearNight = 4,
  Interior = 5,
  DustHaze = 6
};

inline bool sealed_mission(std::string_view id) noexcept {
  return id == "c2m2" || id == "c2m4" || id == "c3m5";
}

inline bool sealed_mission(const char *id) noexcept {
  return id ? sealed_mission(std::string_view(id)) : false;
}

inline SkySource mission_sky_source(std::string_view id,
                                    bool interior = false) noexcept {
  if (interior || sealed_mission(id)) {
    return SkySource::Interior;
  }
  if (id == "t1m2" || id == "c2m1" || id == "c3m1" || id == "c3m2" ||
      id == "c4m6") {
    return SkySource::ClearDay;
  }
  if (id == "t1m1" || id == "t1m4" || id == "c1m1" || id == "c3m3" ||
      id == "c3m4" || id == "c4m3" || id == "c4m4" || id == "c4m5") {
    return SkySource::Twilight;
  }
  if (id == "c3m6") {
    return SkySource::ClearNight;
  }
  if (id == "c2m3") {
    return SkySource::DustHaze;
  }
  return SkySource::OriginalClouds;
}

inline SkySource mission_sky_source(const char *id,
                                    bool interior = false) noexcept {
  return mission_sky_source(id ? std::string_view(id) : std::string_view{},
                            interior);
}

struct EnvironmentPolicy {
  bool inside{false};
  bool suppress_sun{false};
  bool suppress_sky{false};
  SkySource sky_source{SkySource::OriginalClouds};
};

inline EnvironmentPolicy
resolve_environment_policy(std::string_view id,
                           bool profile_interior = false) noexcept {
  EnvironmentPolicy policy{};
  const bool sealed = sealed_mission(id);
  if (sealed || profile_interior) {
    policy.inside = true;
    policy.suppress_sun = true;
    policy.suppress_sky = true;
    policy.sky_source = SkySource::Interior;
    return policy;
  }
  policy.inside = false;
  policy.suppress_sun = false;
  policy.suppress_sky = false;
  policy.sky_source = mission_sky_source(id, false);
  return policy;
}

inline EnvironmentPolicy
resolve_environment_policy(const char *id,
                           bool profile_interior = false) noexcept {
  return resolve_environment_policy(
      id ? std::string_view(id) : std::string_view{}, profile_interior);
}

} // namespace mw34::campaign
