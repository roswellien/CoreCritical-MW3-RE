// WP04A unit2: pure readonly native light hierarchy transform.
// Reads node/class data only; never calls node_to_world, light_update or
// zmth::euler, so native flags, caches, RNG, clock and the matrix stack are
// untouched. Native axes/translation become a row-major, column-vector affine
// donor::Mat with an implicit last row {0,0,0,1}.
#pragma once

#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <stdexcept>
#include <vector>

#include "donor_retarget.hpp"
#include "effect_light_ranking.hpp"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zmodel/gmod_light.h"

namespace mw34 {
namespace effects {

// Inline, named namespace (no anonymous namespace) so renderer and tests
// share one definition without ODR mismatch.
namespace native_light_detail {

constexpr int kMaxLightDepth = 64;

inline void require_finite(const float values[12]) {
  for (int i = 0; i < 12; ++i)
    if (!std::isfinite(values[i]))
      throw std::invalid_argument("light transform: non-finite input");
}

// Native stores three row-vector axes followed by translation. Transpose
// those axes into donor's row-major column-vector affine matrix.
inline donor::Mat from_native_3x4(const float m[12]) {
  require_finite(m);
  return donor::Mat{m[0], m[3], m[6], m[9],  m[1], m[4], m[7], m[10],
                    m[2], m[5], m[8], m[11], 0.0,  0.0,  0.0,  1.0};
}

// Exact copy of the native zmth::euler rotation formula (scale == 1, so the
// scale pass is a no-op) written into a 12-float row-major local buffer.
inline void native_euler_local(const float rot[3], const float pos[3],
                               float out[12]) {
  const float sa = std::sin(rot[0]), ca = std::cos(rot[0]);
  const float sb = std::sin(rot[1]), cb = std::cos(rot[1]);
  const float sc = std::sin(rot[2]), cc = std::cos(rot[2]);
  const float cc_cb = cc * cb, sb_sa = sb * sa, sc_cb = sc * cb;
  out[0] = sb_sa * sc + cc_cb;
  out[1] = sc * ca;
  out[2] = sc_cb * sa - cc * sb;
  out[3] = sb_sa * cc - sc_cb;
  out[4] = cc * ca;
  out[5] = cc_cb * sa + sc * sb;
  out[6] = sb * ca;
  out[7] = -sa;
  out[8] = cb * ca;
  out[9] = pos[0];
  out[10] = pos[1];
  out[11] = pos[2];
  require_finite(out);
}

// Bounded readonly field read: memcpy a T from byte offset into node.data.
template <typename T> T read_field(const void *data, size_t offset) {
  T value{};
  std::memcpy(&value, static_cast<const unsigned char *>(data) + offset,
              sizeof(T));
  return value;
}

inline donor::Mat light_local_transform(const zcls::Node &node) {
  const int32_t cls = node.cls();
  if (cls == 2 || cls == 6) {
    // World and LOD anchors carry no local transform.
    return donor::identity();
  }
  const void *data = node.data;
  if (cls == 5) {
    if (data == nullptr)
      throw std::invalid_argument("light transform: class 5 node without data");
    if (read_field<uint32_t>(data, 0) & 8u)
      return donor::identity();
    float m[12];
    std::memcpy(m, static_cast<const unsigned char *>(data) + 0x30, sizeof m);
    return from_native_3x4(m);
  }
  if (cls == 9) {
    if (data == nullptr)
      throw std::invalid_argument("light transform: light node without data");
    float rot[3] = {read_field<float>(data, 0), read_field<float>(data, 4),
                    read_field<float>(data, 8)};
    float pos[3] = {read_field<float>(data, 0xc), read_field<float>(data, 0x10),
                    read_field<float>(data, 0x14)};
    float local[12];
    native_euler_local(rot, pos, local);
    return from_native_3x4(local);
  }
  if (cls == 8) {
    // Animated object: only a live animation (node flags bit 2 and data flags
    // bit 2) carries the per-frame local matrix; otherwise it stays identity.
    if ((node.flags() & 4u) != 0 && data != nullptr &&
        (read_field<uint32_t>(data, 4) & 4u) != 0) {
      float m[12];
      std::memcpy(m, static_cast<const unsigned char *>(data) + 8, sizeof m);
      return from_native_3x4(m);
    }
    return donor::identity();
  }
  throw std::runtime_error("light transform: unsupported node class");
}

} // namespace native_light_detail

inline donor::Mat light_world_transform(const zcls::Node &source) {
  const zcls::Node *node = &source;
  std::set<const zcls::Node *> seen;
  donor::Mat world = donor::identity();
  bool reached_root = false;
  for (int depth = 0; depth < native_light_detail::kMaxLightDepth; ++depth) {
    if (seen.find(node) != seen.end())
      throw std::invalid_argument(
          "light transform: cycle in node parent chain");
    seen.insert(node);
    const int32_t pc = node->parent_count();
    if (pc < 0 || pc > 1)
      throw std::invalid_argument("light transform: node has no single parent");
    const donor::Mat local = native_light_detail::light_local_transform(*node);
    world = donor::product(local, world);
    if (pc == 0) {
      reached_root = true;
      break;
    }
    if (node->parents == nullptr)
      throw std::invalid_argument("light transform: parent pointer missing");
    if (node->parents[0] == nullptr)
      throw std::invalid_argument("light transform: null parent entry");
    node = node->parents[0];
  }
  if (!reached_root)
    throw std::invalid_argument(
        "light transform: depth limit reached without root");
  for (double v : world)
    if (!std::isfinite(v))
      throw std::invalid_argument("light transform: non-finite result");
  return world;
}

// Called only at the producer's BeginScene boundary, after the native camera
// updates its list. The result owns values; no native pointer reaches a worker.
inline std::vector<Candidate>
collect_native_lights(const zmod::LightList &list) {
  if (list.count < 0 || list.count > zmod::LightList::kMax)
    throw std::runtime_error("Local lights: invalid native light count");
  std::vector<Candidate> result;
  for (int i = 0; i < list.count; ++i) {
    const auto &entry = list.list[i];
    if (!entry.node || !entry.data || entry.node->cls() != 9 ||
        entry.node->data != entry.data)
      throw std::runtime_error(
          "Local lights: inconsistent native light source");
    const auto flags =
        native_light_detail::read_field<uint32_t>(entry.data, 0xb0);
    if (!(flags & 2u) || !(flags & 0x200u) || (flags & 8u))
      continue; // Global/directed illumination belongs to the Sun/ambient
                // stage.
    const float radius =
        native_light_detail::read_field<float>(entry.data, 0xb8);
    // native light_create stores diffuse at +0x9c and ambient at +0xa0;
    // a projected light's projected power is their sum (ambient-only lights
    // must still be collected).
    const float diffuse =
        native_light_detail::read_field<float>(entry.data, 0x9c);
    const float ambient =
        native_light_detail::read_field<float>(entry.data, 0xa0);
    std::array<float, 3> color{};
    for (unsigned c = 0; c < 3; ++c) {
      color[c] =
          native_light_detail::read_field<float>(entry.data, 0xa4 + c * 4);
      if (!std::isfinite(color[c]) || color[c] < 0)
        throw std::runtime_error("Local lights: invalid native color");
    }
    if (!std::isfinite(radius) || radius <= 0 || radius > 1e6f)
      throw std::runtime_error("Local lights: invalid native radius/intensity");
    if (!std::isfinite(diffuse) || diffuse < 0 || !std::isfinite(ambient) ||
        ambient < 0)
      throw std::runtime_error("Local lights: invalid native intensity");
    const double power_sum =
        static_cast<double>(diffuse) + static_cast<double>(ambient);
    if (power_sum > static_cast<double>(FLT_MAX))
      throw std::runtime_error("Local lights: native intensity out of range");
    const float power = static_cast<float>(power_sum);
    if (power == 0 || color == std::array<float, 3>{})
      continue;
    const auto world = light_world_transform(*entry.node);
    Candidate light{};
    light.source = static_cast<std::uint64_t>(i) + 1;
    for (unsigned c = 0; c < 3; ++c) {
      const double value = world[c * 4 + 3];
      if (!std::isfinite(value) || std::abs(value) > 1e6)
        throw std::runtime_error("Local lights: native position outside world");
      light.light.start[c] = static_cast<float>(value);
      light.light.end[c] = static_cast<float>(value);
      light.light.color[c] = color[c];
    }
    light.light.start[3] = radius;
    light.light.color[3] = power;
    if (flags & 4u) {
      const std::array<double, 3> direction{-world[2], -world[6], -world[10]};
      const double length =
          std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] +
                    direction[2] * direction[2]);
      if (!std::isfinite(length) || length <= 1e-12)
        throw std::runtime_error("Local lights: degenerate spot direction");
      for (unsigned c = 0; c < 3; ++c)
        light.light.end[c] = static_cast<float>(direction[c] / length);
      light.light.end[3] = 2;
    }
    result.push_back(light);
  }
  return result;
}

} // namespace effects
} // namespace mw34
