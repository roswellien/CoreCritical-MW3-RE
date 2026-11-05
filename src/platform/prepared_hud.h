#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

namespace platform {
// Owned premultiplied RGBA, ready for GPU filtering. UVs select the logical
// image inside an artwork or text resource; native positions/crops stay intact.
struct HudImage {
  unsigned width{}, height{};
  float u0{}, v0{}, u1{1}, v1{1};
  std::vector<uint32_t> pixels;
};
using HudArtwork = std::map<uint64_t, std::shared_ptr<const HudImage>>;
HudArtwork load_hud_artwork(const std::filesystem::path& directory);
uint64_t hud_identity(unsigned width, unsigned height, const uint16_t* opaque_pixels, size_t count);
inline uint64_t hud_identity(unsigned width, unsigned height, const std::vector<uint16_t>& pixels) {
  return hud_identity(width, height, pixels.data(), pixels.size());
}
}  // namespace platform
