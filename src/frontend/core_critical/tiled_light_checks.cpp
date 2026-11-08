#include "platform/render3d.h"
#include "renderer/native_texture_identity.hpp"
#include "renderer/vk_scene.hpp"
#include <cstdio>
#include <stdexcept>
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
#include "tiled_light_checks.inc"
} // namespace
int main() {
  try {
    check_tiled_gpu();
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "Tiled light checks: %s\n", e.what());
    return 1;
  }
}
