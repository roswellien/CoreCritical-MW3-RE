#include "crt/msvc_rand.h"

namespace crt {

namespace {
// MSVCRT keeps one seed per thread; the game draws from its main thread only.
uint32_t g_seed = 1;
}  // namespace

std::optional<uint32_t>& rand_fixed_seed() {
  static std::optional<uint32_t> s;
  return s;
}

void msvc_srand(uint32_t seed) { g_seed = rand_fixed_seed() ? *rand_fixed_seed() : seed; }

int msvc_rand() {
  g_seed = g_seed * 214013u + 2531011u;
  return static_cast<int>((g_seed >> 16) & 0x7fff);
}

uint32_t msvc_rand_state() { return g_seed; }

}  // namespace crt
