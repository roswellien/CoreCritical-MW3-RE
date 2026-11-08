// Canonical identity of the decoded native packed upload.
#pragma once
#include "material_normals.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace mw34::materials {

void set_active_surface_key(const SurfaceKey &);
void clear_active_surface_key();
bool get_active_surface_key(SurfaceKey *);

inline SurfaceKey canonical_upload_key(uint32_t width, uint32_t height, const uint8_t *rgba,
                                       uint32_t format) {
  // Display/device caps belong to the caller, not the optional catalog.
  if (width < 1 || height < 1)
    throw std::invalid_argument("dimensions out of range");
  if (!rgba)
    throw std::invalid_argument("rgba null");
  if (format < 1 || format > 3)
    throw std::invalid_argument("format out of range");

  const uint64_t offset = 0xcbf29ce484222325ULL;
  const uint64_t prime = 0x100000001b3ULL;
  uint64_t h = offset;
  const uint64_t count = uint64_t(width) * height;
  if (count > std::numeric_limits<std::size_t>::max() / 4)
    throw std::overflow_error("upload byte count is not representable");
  const std::size_t px = static_cast<std::size_t>(count);

  for (std::size_t i = 0; i < px; ++i) {
    const uint8_t *p = rgba + i * 4;
    const uint8_t r = p[0], g = p[1], b = p[2], a = p[3];
    uint16_t packed = 0;
    if (format == 1) {
      packed = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    } else if (format == 2) {
      packed =
          static_cast<uint16_t>(((a >> 7) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
    } else {
      packed =
          static_cast<uint16_t>(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
    }
    const uint8_t lo = static_cast<uint8_t>(packed & 0xFF);
    const uint8_t hi = static_cast<uint8_t>((packed >> 8) & 0xFF);
    h ^= lo;
    h *= prime;
    h ^= hi;
    h *= prime;
  }

  return SurfaceKey{width, height, format, h};
}

class ScopedUploadKey {
public:
  explicit ScopedUploadKey(const SurfaceKey &newkey) {
    had_ = get_active_surface_key(&prior_);
    set_active_surface_key(newkey);
  }
  ~ScopedUploadKey() {
    if (had_)
      set_active_surface_key(prior_);
    else
      clear_active_surface_key();
  }
  ScopedUploadKey(const ScopedUploadKey &) = delete;
  ScopedUploadKey &operator=(const ScopedUploadKey &) = delete;
  ScopedUploadKey(ScopedUploadKey &&) = delete;
  ScopedUploadKey &operator=(ScopedUploadKey &&) = delete;

private:
  SurfaceKey prior_{};
  bool had_ = false;
};

} // namespace mw34::materials
