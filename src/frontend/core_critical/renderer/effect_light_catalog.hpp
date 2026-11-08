#pragma once
// MWFX1 effect-light material catalog reader. Ported verbatim from the MWFX1
// file contract: all integers and float bit patterns are read through the
// portable little-endian readers below, so the on-disk layout does not depend
// on the host endianness. The Light, Uniform and reconstruct ABIs live in
// effect_lighting.hpp and are not duplicated here.
#include "material_normals.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <istream>
#include <limits>
#include <map>
#include <stdexcept>

namespace mw34::effects {

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "MWFX1 requires IEEE-754 binary32 floats");

struct Emitter {
  unsigned kind{};
  float radius{}, intensity{}, lifetime{};
  std::array<float, 3> color{};
};
using Catalog = std::map<materials::SurfaceKey, Emitter>;

inline Catalog read_catalog(std::istream &f) {
  auto fail = [] {
    throw std::runtime_error("Invalid effect-light material catalog");
  };
  auto bytes = [&](void *p, size_t n) {
    if (!f.read(static_cast<char *>(p), std::streamsize(n)))
      fail();
  };
  // Read little-endian integers byte by byte so the file stays little-endian
  // regardless of the host's load width or alignment.
  auto u32 = [&] {
    unsigned char b[4];
    bytes(b, 4);
    return static_cast<std::uint32_t>(b[0]) |
           static_cast<std::uint32_t>(b[1]) << 8 |
           static_cast<std::uint32_t>(b[2]) << 16 |
           static_cast<std::uint32_t>(b[3]) << 24;
  };
  auto u64 = [&] {
    unsigned char b[8];
    bytes(b, 8);
    unsigned long long v = 0;
    for (int i = 7; i >= 0; --i)
      v = v << 8 | b[i];
    return static_cast<std::uint64_t>(v);
  };
  char magic[8];
  bytes(magic, 8);
  if (std::memcmp(magic, "MWFX1\0\0\0", 8))
    fail();
  unsigned count = u32();
  if (!count || count > 8192)
    fail();
  Catalog out;
  for (unsigned i = 0; i < count; ++i) {
    materials::SurfaceKey k;
    Emitter e;
    k.width = u32();
    k.height = u32();
    k.format = u32();
    e.kind = u32();
    k.hash = u64();
    auto scalar = [&](float &v) {
      const std::uint32_t bits = u32();
      std::memcpy(&v, &bits, sizeof(v));
    };
    scalar(e.radius);
    scalar(e.intensity);
    scalar(e.lifetime);
    for (auto &c : e.color)
      scalar(c);
    if (!k.width || !k.height || k.width > 4096 || k.height > 4096 ||
        k.format < 1 || k.format > 3 || e.kind < 1 || e.kind > 6)
      fail();
    auto valid = [](float v, float a, float b) {
      return std::isfinite(v) && v >= a && v <= b;
    };
    if (!valid(e.radius, .25f, 128) || !valid(e.intensity, 0, 8) ||
        !valid(e.lifetime, .01f, 1))
      fail();
    for (float c : e.color)
      if (!valid(c, 0, 4))
        fail();
    if (*std::max_element(e.color.begin(), e.color.end()) <= 0 ||
        !out.emplace(k, e).second)
      fail();
  }
  if (f.peek() != std::char_traits<char>::eof())
    fail();
  return out;
}
} // namespace mw34::effects
