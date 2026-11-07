#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace sdlfe {
// Calling-thread wall time, including waits within the measured operation.
// A disabled interval performs no clock read or floating-point arithmetic.
class CpuInterval {
 public:
  explicit CpuInterval(std::uint64_t* ticks)
      : ticks_(ticks), start_(ticks ? SDL_GetPerformanceCounter() : 0) {}
  ~CpuInterval() {
    if (ticks_) *ticks_ += SDL_GetPerformanceCounter() - start_;
  }
  CpuInterval(const CpuInterval&) = delete;
  CpuInterval& operator=(const CpuInterval&) = delete;

 private:
  std::uint64_t* ticks_;
  std::uint64_t start_;
};

inline bool cpu_profile_requested() {
  const char* value = std::getenv("MW3_CC_CPU_PROFILE");
  return value && std::strcmp(value, "1") == 0;
}
}  // namespace sdlfe
