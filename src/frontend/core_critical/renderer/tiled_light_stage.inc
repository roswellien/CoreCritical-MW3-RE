// renderer/tiled_light_stage.inc
#pragma once

#include "effect_lighting.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace mw34::tiled {

inline constexpr std::size_t tile_size = 16;
inline constexpr std::size_t max_width = 5120;
inline constexpr std::size_t max_height = 2160;
inline constexpr std::size_t capacity = 64;
inline constexpr std::size_t words_per_tile = 65;

struct alignas(16) View {
  std::array<float, 12> camera{};
  std::array<float, 4> projection{};
  std::array<float, 4> depth{};
  std::array<uint32_t, 4> extent{};
};

static_assert(sizeof(View) == 96, "View ABI size mismatch");
static_assert(offsetof(View, projection) == 48, "View projection offset mismatch");
static_assert(offsetof(View, depth) == 64, "View depth offset mismatch");
static_assert(offsetof(View, extent) == 80, "View extent offset mismatch");

inline View make_view(const std::array<float, 12> &camera, const std::array<float, 4> &projection,
                      float near_z, float far_z, uint32_t width, uint32_t height) {
  for (float v : camera) {
    if (!std::isfinite(v) || std::abs(v) > 1e6f) {
      throw std::invalid_argument("camera contains non-finite value");
    }
  }
  for (float v : projection) {
    if (!std::isfinite(v) || std::abs(v) > 1e6f) {
      throw std::invalid_argument("projection contains non-finite value");
    }
  }
  if (!std::isfinite(near_z) || !std::isfinite(far_z) || far_z > 1e6f) {
    throw std::invalid_argument("depth bounds must be finite");
  }
  if (width == 0 || height == 0 || width > max_width || height > max_height) {
    throw std::invalid_argument("width/height out of bounds");
  }
  if (near_z <= 0.0f || far_z <= near_z) {
    throw std::invalid_argument("invalid depth bounds");
  }
  if (projection[0] == 0.0f || projection[1] == 0.0f) {
    throw std::invalid_argument("projection ax/ay must be nonzero");
  }

  const float tol = 1e-3f;
  const float r0x = camera[0], r0y = camera[1], r0z = camera[2];
  const float r1x = camera[4], r1y = camera[5], r1z = camera[6];
  const float r2x = camera[8], r2y = camera[9], r2z = camera[10];

  auto len = [](float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); };
  auto dot = [](float ax, float ay, float az, float bx, float by, float bz) {
    return ax * bx + ay * by + az * bz;
  };

  if (std::abs(len(r0x, r0y, r0z) - 1.0f) > tol) {
    throw std::invalid_argument("camera rotation row 0 not unit length");
  }
  if (std::abs(len(r1x, r1y, r1z) - 1.0f) > tol) {
    throw std::invalid_argument("camera rotation row 1 not unit length");
  }
  if (std::abs(len(r2x, r2y, r2z) - 1.0f) > tol) {
    throw std::invalid_argument("camera rotation row 2 not unit length");
  }
  if (std::abs(dot(r0x, r0y, r0z, r1x, r1y, r1z)) > tol) {
    throw std::invalid_argument("camera rotation rows 0 and 1 not perpendicular");
  }
  if (std::abs(dot(r0x, r0y, r0z, r2x, r2y, r2z)) > tol) {
    throw std::invalid_argument("camera rotation rows 0 and 2 not perpendicular");
  }
  if (std::abs(dot(r1x, r1y, r1z, r2x, r2y, r2z)) > tol) {
    throw std::invalid_argument("camera rotation rows 1 and 2 not perpendicular");
  }

  const float det = (r0x * (r1y * r2z - r1z * r2y) - r0y * (r1x * r2z - r1z * r2x) +
                     r0z * (r1x * r2y - r1y * r2x));
  if (std::abs(det) < 1e-3f) {
    throw std::invalid_argument("camera rotation has unsupported scale");
  }

  View v{};
  v.camera = camera;
  v.projection = projection;
  v.depth[0] = near_z;
  v.depth[1] = far_z;
  v.extent[0] = width;
  v.extent[1] = height;
  v.extent[2] = static_cast<uint32_t>((width + 15) / 16);
  v.extent[3] = static_cast<uint32_t>((height + 15) / 16);
  return v;
}

inline uint64_t bytes(const View &v) {
  return static_cast<uint64_t>(v.extent[2]) * static_cast<uint64_t>(v.extent[3]) *
         static_cast<uint64_t>(words_per_tile) * static_cast<uint64_t>(4);
}

inline void validate_lights(const effects::Uniform &u) {
  if (!std::isfinite(u.params[0])) {
    throw std::invalid_argument("light count must be finite");
  }
  const float cf = u.params[0];
  if (cf < 0.0f || cf > static_cast<float>(capacity) || std::floor(cf) != cf) {
    throw std::invalid_argument("light count out of range or non-integral");
  }
  const uint32_t count = static_cast<uint32_t>(cf);
  for (uint32_t i = 0; i < count; ++i) {
    const auto &start = u.lights[i].start;
    const auto &end = u.lights[i].end;
    const auto &color = u.lights[i].color;
    const float kind = end[3];

    for (float v : start) {
      if (!std::isfinite(v) || std::abs(v) > 1e6f) {
        throw std::invalid_argument("light start contains non-finite value");
      }
    }
    for (float v : end) {
      if (!std::isfinite(v) || std::abs(v) > 1e6f) {
        throw std::invalid_argument("light end contains non-finite value");
      }
    }
    for (float v : color) {
      if (!std::isfinite(v) || std::abs(v) > 1e6f) {
        throw std::invalid_argument("light color contains non-finite value");
      }
    }
    if (start[3] <= 0.0f) {
      throw std::invalid_argument("light radius must be positive");
    }
    if (kind != 0.0f && kind != 1.0f && kind != 2.0f) {
      throw std::invalid_argument("light kind must be 0, 1, or 2");
    }
    if (kind == 0.0f) {
      if (end[0] != start[0] || end[1] != start[1] || end[2] != start[2]) {
        throw std::invalid_argument("legacy point light end.xyz must equal start.xyz");
      }
    }
  }
}

using Mat34 = std::array<float, 12>;

// Row-major 3x4 affine: [ r00 r01 r02 tx ; r10 r11 r12 ty ; r20 r21 r22 tz ]
// index = row*4 + col

namespace detail {

constexpr float kEps = 1e-12f;
constexpr float kMax = 1e6f;

[[nodiscard]] inline bool finite_all(const Mat34 &m) noexcept {
  for (float v : m)
    if (!std::isfinite(v))
      return false;
  return true;
}

[[nodiscard]] inline bool bounded_all(const Mat34 &m) noexcept {
  for (float v : m)
    if (std::abs(v) > kMax)
      return false;
  return true;
}

[[nodiscard]] inline bool valid_result(const Mat34 &m) noexcept {
  return finite_all(m) && bounded_all(m);
}

} // namespace detail

// Inverse of a 3x4 affine matrix (world->camera or model->world).
// Uses double-precision 3x3 cofactor inverse for the rotation/scale block.
// Rejects non-finite inputs or |det| < 1e-12.
// translation = -Rinv * t
[[nodiscard]] inline Mat34 inverse_affine(const Mat34 &m) {
  if (!detail::finite_all(m))
    throw std::invalid_argument("inverse_affine: non-finite input");

  const double r00 = m[0], r01 = m[1], r02 = m[2];
  const double r10 = m[4], r11 = m[5], r12 = m[6];
  const double r20 = m[8], r21 = m[9], r22 = m[10];
  const double tx = m[3], ty = m[7], tz = m[11];

  // Cofactors of the 3x3 block
  const double c00 = r11 * r22 - r12 * r21;
  const double c01 = -(r10 * r22 - r12 * r20);
  const double c02 = r10 * r21 - r11 * r20;
  const double c10 = -(r01 * r22 - r02 * r21);
  const double c11 = r00 * r22 - r02 * r20;
  const double c12 = -(r00 * r21 - r01 * r20);
  const double c20 = r01 * r12 - r02 * r11;
  const double c21 = -(r00 * r12 - r02 * r10);
  const double c22 = r00 * r11 - r01 * r10;

  const double det = r00 * c00 + r01 * c01 + r02 * c02;
  if (!std::isfinite(det) || std::abs(det) < 1e-12)
    throw std::invalid_argument("inverse_affine: singular or non-finite determinant");

  const double inv_det = 1.0 / det;

  // Inverse 3x3 (cofactor transpose * inv_det)
  const double i00 = c00 * inv_det;
  const double i01 = c10 * inv_det;
  const double i02 = c20 * inv_det;
  const double i10 = c01 * inv_det;
  const double i11 = c11 * inv_det;
  const double i12 = c21 * inv_det;
  const double i20 = c02 * inv_det;
  const double i21 = c12 * inv_det;
  const double i22 = c22 * inv_det;

  // translation = -Rinv * t
  const double ntx = -(i00 * tx + i01 * ty + i02 * tz);
  const double nty = -(i10 * tx + i11 * ty + i12 * tz);
  const double ntz = -(i20 * tx + i21 * ty + i22 * tz);

  Mat34 out{};
  out[0] = static_cast<float>(i00);
  out[1] = static_cast<float>(i01);
  out[2] = static_cast<float>(i02);
  out[3] = static_cast<float>(ntx);
  out[4] = static_cast<float>(i10);
  out[5] = static_cast<float>(i11);
  out[6] = static_cast<float>(i12);
  out[7] = static_cast<float>(nty);
  out[8] = static_cast<float>(i20);
  out[9] = static_cast<float>(i21);
  out[10] = static_cast<float>(i22);
  out[11] = static_cast<float>(ntz);

  if (!detail::valid_result(out))
    throw std::overflow_error("inverse_affine: result out of bounds");

  return out;
}

// Compose two affine matrices: returns a * b (apply b first, then a).
// world->camera = modelcamera * inverse(modelworld)
[[nodiscard]] inline Mat34 compose_affine(const Mat34 &a, const Mat34 &b) {
  if (!detail::finite_all(a) || !detail::finite_all(b))
    throw std::invalid_argument("compose_affine: non-finite input");

  Mat34 out{};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      const double s = static_cast<double>(a[r * 4 + 0]) * b[c] +
                       static_cast<double>(a[r * 4 + 1]) * b[4 + c] +
                       static_cast<double>(a[r * 4 + 2]) * b[8 + c];
      out[r * 4 + c] = static_cast<float>(s);
    }
    const double s = static_cast<double>(a[r * 4 + 0]) * b[3] +
                     static_cast<double>(a[r * 4 + 1]) * b[7] +
                     static_cast<double>(a[r * 4 + 2]) * b[11] + static_cast<double>(a[r * 4 + 3]);
    out[r * 4 + 3] = static_cast<float>(s);
  }

  if (!detail::valid_result(out))
    throw std::overflow_error("compose_affine: result out of bounds");

  return out;
}

inline View draw_view(const Mat34 &camera, const std::array<float, 6> &p, unsigned width,
                      unsigned height, unsigned raster_width, unsigned raster_height) {
  if (!width || !height)
    throw std::invalid_argument("Tiled camera has zero guest extent");
  for (float v : p)
    if (!std::isfinite(v) || std::abs(v) > 1e6f)
      throw std::invalid_argument("Tiled native projection outside supported range");
  const double sx = double(raster_width) / width, sy = double(raster_height) / height;
  return make_view(camera,
                   {float(double(p[1]) * p[0] * sx), float(double(p[2]) * p[0] * sy),
                    float((double(p[3]) + .5) * sx), float((double(p[4]) + .5) * sy)},
                   p[0], p[5], raster_width, raster_height);
}
} // namespace mw34::tiled
