#pragma once
// WP04A bounded unit3: deterministic per-frame ranking of Candidate effect
// lights into the effect_lighting.hpp Uniform ABI. Weapons are ranked first
// and fixtures fill the remaining slots; params.y carries the weapon prefix
// count that the fixture shadow consumer (reference fixture_slots) reads.
// No game state, RNG or clock is touched and the Uniform ABI is unchanged.
#include "effect_lighting.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <vector>

namespace mw34::effects {

struct Candidate {
  Light light{};
  std::array<float, 4> cone{};
  std::uint64_t source{};
  bool weapon{};
};

inline Uniform rank_frame_lights(const std::vector<Candidate> &sources,
                                 const Point &eye, unsigned limit = capacity,
                                 unsigned weapon_reserve = 16) {
  if (limit < 1 || limit > capacity)
    throw std::runtime_error("rank_frame_lights: limit outside 1..64");
  if (weapon_reserve > limit)
    throw std::runtime_error("rank_frame_lights: weapon reserve exceeds limit");
  if (sources.size() > 384)
    throw std::runtime_error("rank_frame_lights: too many candidate sources");
  for (const float v : eye)
    if (!std::isfinite(v) || std::fabs(v) > 1e6f)
      throw std::runtime_error("rank_frame_lights: eye outside [-1e6, 1e6]");
  const auto finite4 = [](const std::array<float, 4> &v) {
    for (const float c : v)
      if (!std::isfinite(c))
        return false;
    return true;
  };
  std::set<std::pair<bool, std::uint64_t>> ids;
  for (const Candidate &c : sources) {
    if (!finite4(c.light.start) || !finite4(c.light.end) ||
        !finite4(c.light.color) || !finite4(c.cone))
      throw std::runtime_error("rank_frame_lights: non-finite candidate");
    if (c.source == 0 || !ids.insert({c.weapon, c.source}).second)
      throw std::runtime_error(
          "rank_frame_lights: zero or duplicate source id");
    if (c.light.start[3] <= 0 || c.light.color[3] < 0 || c.light.color[0] < 0 ||
        c.light.color[1] < 0 || c.light.color[2] < 0)
      throw std::runtime_error(
          "rank_frame_lights: negative or zero radius/intensity");
    const float kind = c.light.end[3];
    if (kind == 0) {
      // end.xyz must equal start.xyz exactly, component for component.
      if (c.light.end[0] != c.light.start[0] ||
          c.light.end[1] != c.light.start[1] ||
          c.light.end[2] != c.light.start[2])
        throw std::runtime_error(
            "rank_frame_lights: point end must equal start");
    } else if (kind == 2) {
      // Spot direction length: sum of squared end components, not a
      // start/end distance.
      const double len2 = static_cast<double>(c.light.end[0]) * c.light.end[0] +
                          static_cast<double>(c.light.end[1]) * c.light.end[1] +
                          static_cast<double>(c.light.end[2]) * c.light.end[2];
      if (std::fabs(len2 - 1) > 1e-3)
        throw std::runtime_error(
            "rank_frame_lights: spot direction must be unit length");
    } else if (kind != 1)
      throw std::runtime_error(
          "rank_frame_lights: end.w must be point/capsule/spot");
  }
  // Zero-intensity lights are dropped only after the validation above.
  struct Entry {
    Candidate value;
    double score;
  };
  std::vector<Entry> weapons;
  std::vector<Entry> fixtures;
  for (const Candidate &c : sources)
    if (c.light.color[3] > 0)
      (c.weapon ? weapons : fixtures).push_back({c, 0});
  auto score = [](const Candidate &c, const Point &eye) {
    const auto &l = c.light;
    double cx = 0, cy = 0, cz = 0;
    if (l.end[3] == 1) {
      // Capsule projection in double; cast every float BEFORE any
      // subtraction or multiplication so large finite floats cannot
      // overflow to inf/NaN.
      const double dx =
          static_cast<double>(l.end[0]) - static_cast<double>(l.start[0]);
      const double dy =
          static_cast<double>(l.end[1]) - static_cast<double>(l.start[1]);
      const double dz =
          static_cast<double>(l.end[2]) - static_cast<double>(l.start[2]);
      const double len2 = dx * dx + dy * dy + dz * dz;
      double t = 0;
      if (len2 > 0) {
        const double s =
            (static_cast<double>(eye[0]) - static_cast<double>(l.start[0])) *
                dx +
            (static_cast<double>(eye[1]) - static_cast<double>(l.start[1])) *
                dy +
            (static_cast<double>(eye[2]) - static_cast<double>(l.start[2])) *
                dz;
        t = std::clamp(s / len2, 0.0, 1.0);
      }
      cx = static_cast<double>(l.start[0]) + t * dx;
      cy = static_cast<double>(l.start[1]) + t * dy;
      cz = static_cast<double>(l.start[2]) + t * dz;
    } else {
      cx = l.start[0];
      cy = l.start[1];
      cz = l.start[2];
    }
    const double ex = static_cast<double>(eye[0]) - cx;
    const double ey = static_cast<double>(eye[1]) - cy;
    const double ez = static_cast<double>(eye[2]) - cz;
    const double radius = l.start[3];
    const double r2 = radius * radius;
    const double value = static_cast<double>(l.color[3]) * r2 /
                         (r2 + ex * ex + ey * ey + ez * ez);
    if (!std::isfinite(value))
      throw std::runtime_error("rank_frame_lights: non-finite score");
    return value;
  };
  for (Entry &e : weapons)
    e.score = score(e.value, eye);
  for (Entry &e : fixtures)
    e.score = score(e.value, eye);
  const auto ordered = [](const Entry &a, const Entry &b) {
    if (a.score != b.score)
      return a.score > b.score;
    return a.value.source < b.value.source;
  };
  std::sort(weapons.begin(), weapons.end(), ordered);
  std::sort(fixtures.begin(), fixtures.end(), ordered);
  const size_t weaponCount =
      std::min(weapons.size(), static_cast<size_t>(limit));
  const size_t fixtureCount =
      std::min({fixtures.size(), static_cast<size_t>(limit - weaponCount),
                static_cast<size_t>(limit - weapon_reserve)});
  Uniform out{};
  const size_t total = weaponCount + fixtureCount;
  for (size_t i = 0; i < weaponCount; ++i) {
    out.lights[i] = weapons[i].value.light;
    out.cones[i] = weapons[i].value.cone;
  }
  for (size_t i = 0; i < fixtureCount; ++i) {
    out.lights[weaponCount + i] = fixtures[i].value.light;
    out.cones[weaponCount + i] = fixtures[i].value.cone;
  }
  out.params = {static_cast<float>(total), static_cast<float>(weaponCount),
                static_cast<float>(fixtureCount), 0};
  return out;
}

} // namespace mw34::effects
