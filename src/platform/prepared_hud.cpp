#include "platform/prepared_hud.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace platform {
namespace {
uint32_t word(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void fold(uint64_t& hash, uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i) {
    hash ^= uint8_t(value >> (8 * i));
    hash *= 0x100000001b3ULL;
  }
}
float real(const uint8_t* bytes) {
  const uint32_t bits = word(bytes);
  float result;
  static_assert(sizeof result == sizeof bits);
  std::memcpy(&result, &bits, sizeof result);
  return result;
}
}  // namespace
uint64_t hud_identity(unsigned width, unsigned height, const uint16_t* pixels, size_t count) {
  if (count != uint64_t(width) * height || (!pixels && count))
    throw std::runtime_error("HUD identity size mismatch");
  uint64_t hash = 0xcbf29ce484222325ULL;
  fold(hash, width, 4);
  fold(hash, height, 4);
  for (size_t i = 0; i < count; ++i) fold(hash, pixels[i], 2);
  return hash;
}
HudArtwork load_hud_artwork(const std::filesystem::path& requested) {
  auto directory = requested;
  if (std::filesystem::is_directory(directory / "ui" / "sprites")) directory /= "ui/sprites";
  if (!std::filesystem::is_directory(directory))
    throw std::runtime_error("HUD artwork directory is missing");
  HudArtwork images;
  uint64_t total = 0;
  for (const auto& file : std::filesystem::directory_iterator(directory)) {
    if (file.path().extension() != ".rgba") continue;
    const std::string name = file.path().stem().string();
    if (name.size() != 16 || name.find_first_not_of("0123456789abcdef") != std::string::npos ||
        !file.is_regular_file() || images.size() >= 256)
      throw std::runtime_error("Invalid or excessive HUD artwork identity");
    std::ifstream input(file.path(), std::ios::binary);
    uint8_t header[28]{};
    if (!input.read(reinterpret_cast<char*>(header), sizeof header) ||
        std::string(reinterpret_cast<char*>(header), 4) != "MW3H")
      throw std::runtime_error("HUD artwork requires MW3H-v1");
    auto image = std::make_shared<HudImage>();
    image->width = word(header + 4);
    image->height = word(header + 8);
    image->u0 = real(header + 12);
    image->v0 = real(header + 16);
    image->u1 = real(header + 20);
    image->v1 = real(header + 24);
    const uint64_t bytes = uint64_t(image->width) * image->height * 4;
    total += bytes;
    if (!image->width || !image->height || image->width > 8192 || image->height > 8192 ||
        total > 512 * 1024 * 1024 || file.file_size() != bytes + 28 || !std::isfinite(image->u0) ||
        !std::isfinite(image->v0) || !std::isfinite(image->u1) || !std::isfinite(image->v1) ||
        image->u0 < 0 || image->v0 < 0 || image->u0 >= image->u1 || image->v0 >= image->v1 ||
        image->u1 > 1 || image->v1 > 1)
      throw std::runtime_error("Invalid HUD artwork dimensions, bytes or UVs");
    image->pixels.resize(size_t(bytes / 4));
    for (auto& pixel : image->pixels) {
      uint8_t rgba[4];
      if (!input.read(reinterpret_cast<char*>(rgba), 4))
        throw std::runtime_error("Truncated HUD artwork");
      const unsigned alpha = rgba[3];
      pixel = (unsigned(rgba[0]) * alpha + 127) / 255 |
              ((unsigned(rgba[1]) * alpha + 127) / 255) << 8 |
              ((unsigned(rgba[2]) * alpha + 127) / 255) << 16 | alpha << 24;
    }
    if (!images.emplace(std::stoull(name, nullptr, 16), std::move(image)).second)
      throw std::runtime_error("Duplicate HUD artwork identity");
  }
  if (images.empty()) throw std::runtime_error("HUD artwork directory contains no MW3H sprites");
  return images;
}
}  // namespace platform
