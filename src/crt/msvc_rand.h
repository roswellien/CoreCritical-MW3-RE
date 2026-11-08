#pragma once
#include <cstdint>
#include <optional>

// The original links MSVCRT's rand()/srand(). Random choices (music cues, AI,
// damage spread) must draw the same sequence, so the rebuild uses this instead of
// the host C library, whose rand() differs on every platform.
namespace crt {

void msvc_srand(uint32_t seed);
int msvc_rand();  // 0..32767, same sequence as MSVCRT for the same seed

uint32_t msvc_rand_state(); // Read-only state for bounded simulation-invariance
                            // checks; does not advance RNG.

// The game seeds from the clock (time(), as the exe at 0x00501ab0, 0x00508fc0
// and 0x0049aca0), so no two runs draw alike. For the tests only (the
// headless ones and mw3's --seed): while set, msvc_srand takes this in place
// of the seed it is given, so a run repeats bit for bit. Never set in play.
std::optional<uint32_t>& rand_fixed_seed();

}  // namespace crt
