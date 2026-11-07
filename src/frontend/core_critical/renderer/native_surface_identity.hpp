#pragma once
#include "gamez/zmodel/gmod_const.h"
#include <cmath>
#include <cstdint>
#include <optional>

namespace cc_mw3_re {
// Canonical BWSFM1 batch/vertex stream, built from original source corners.
// Positions and UVs are read before camera projection or clipping; pointers are
// borrowed only for this call. No native pointers enter a render packet.
inline std::optional<uint64_t> native_mesh_uv_hash(const zmod::Model &m) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Native BWSFM1 identity requires a little-endian host"
#endif
  constexpr uint64_t kFnvOffset = 0xcbf29ce484222325ull;
  constexpr uint64_t kFnvPrime = 0x100000001b3ull;
  const uint32_t polygon_count = m.count(0x10);
  const uint32_t position_count = m.count(0x14);
  const uint32_t normal_count = m.count(0x18);
  if (m.polygons == nullptr || m.vertices == nullptr)
    return std::nullopt;
  if (polygon_count == 0 || polygon_count > 4096)
    return std::nullopt;
  if (position_count == 0 || position_count > 1048576)
    return std::nullopt;
  uint64_t h = kFnvOffset;
  auto fold_bytes = [&h](const void *p, std::size_t n) {
    const auto *b = static_cast<const unsigned char *>(p);
    for (std::size_t i = 0; i < n; ++i) {
      h ^= b[i];
      h *= kFnvPrime;
    }
  };
  auto fold_u32 = [&fold_bytes](uint32_t v) { fold_bytes(&v, sizeof v); };
  fold_u32(polygon_count);
  for (uint32_t pi = 0; pi < polygon_count; ++pi) {
    const zmod::Polygon &poly = m.polygons[pi];
    const uint32_t corner_count = poly.flags & 0xffu;
    if (corner_count < 3 || corner_count > 64)
      return std::nullopt;
    if (poly.vertex_indices == nullptr)
      return std::nullopt;
    const bool per_corner_normals = (poly.flags & 0x200u) != 0u;
    if (per_corner_normals && (poly.normal_indices == nullptr ||
                               m.normals == nullptr || normal_count == 0))
      return std::nullopt;
    const bool textured =
        poly.material != nullptr && (poly.material->flags & 0x1u) != 0u;
    if (textured && poly.uvs == nullptr)
      return std::nullopt;
    fold_u32(corner_count);
    for (uint32_t c = 0; c < corner_count; ++c) {
      const uint32_t vi = poly.vertex_indices[c];
      if (vi >= position_count)
        return std::nullopt;
      float rec[8];
      const float *xyz = m.vertices + static_cast<std::size_t>(vi) * 3u;
      rec[0] = xyz[0];
      rec[1] = xyz[1];
      rec[2] = xyz[2];
      if (per_corner_normals) {
        const uint32_t ni = poly.normal_indices[c];
        if (ni >= normal_count)
          return std::nullopt;
        const float *n = m.normals + static_cast<std::size_t>(ni) * 3u;
        rec[3] = n[0];
        rec[4] = n[1];
        rec[5] = n[2];
      } else {
        rec[3] = 0.0f;
        rec[4] = 0.0f;
        rec[5] = 0.0f;
      }
      if (textured) {
        rec[6] = poly.uvs[c * 2u + 0u];
        rec[7] = poly.uvs[c * 2u + 1u];
      } else {
        rec[6] = 0.0f;
        rec[7] = 0.0f;
      }
      for (float f : rec)
        if (!std::isfinite(f))
          return std::nullopt;
      fold_bytes(rec, sizeof rec);
    }
  }
  return h;
}

} // namespace cc_mw3_re
