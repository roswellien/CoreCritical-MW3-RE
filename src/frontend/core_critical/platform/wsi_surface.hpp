#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan_core.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace cc_mw3_re::platform {

struct SdlWindowState {
  bool visible{false};
  int drawable_width{0};
  int drawable_height{0};
};

static inline uint64_t pack_state(const SdlWindowState &s) noexcept {
  const uint64_t w = static_cast<uint32_t>(s.drawable_width) & 0x7FFFFFFFULL;
  const uint64_t v = s.visible ? 1ULL : 0ULL;
  const uint64_t h = static_cast<uint32_t>(s.drawable_height);
  return w | (v << 31) | (h << 32);
}

static inline SdlWindowState unpack_state(uint64_t raw) noexcept {
  SdlWindowState s;
  s.drawable_width = static_cast<int>(raw & 0x7FFFFFFFULL);
  s.visible = ((raw >> 31) & 1ULL) != 0;
  s.drawable_height = static_cast<int>(raw >> 32);
  return s;
}

class SdlWindowStateTracker {
public:
  explicit SdlWindowStateTracker(SDL_Window *window)
      : window_(window), owner_thread_id_(std::this_thread::get_id()) {
    sample_owner();
    set_worker_state(current_state_);
  }

  void sample_owner() {
    if (std::this_thread::get_id() != owner_thread_id_) {
      throw std::runtime_error(
          "SDL window state query requires the SDL owner thread");
    }
    const auto flags = SDL_GetWindowFlags(window_);
    current_state_.visible =
        (flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) == 0;
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    current_state_.drawable_width = w;
    current_state_.drawable_height = h;
  }

  SdlWindowState sample_and_get() {
    sample_owner();
    return current_state_;
  }

  bool is_owner_thread() const {
    return std::this_thread::get_id() == owner_thread_id_;
  }

  void set_worker_state(const SdlWindowState &state) {
    worker_packed_state_.store(pack_state(state), std::memory_order_release);
  }

  void enter_job_scope(const SdlWindowState &state) {
    active_job_state_ = state;
    in_active_job_scope_ = true;
  }

  void leave_job_scope() { in_active_job_scope_ = false; }

  SdlWindowState worker_state() const {
    if (in_active_job_scope_) {
      return active_job_state_;
    }
    return unpack_state(worker_packed_state_.load(std::memory_order_acquire));
  }

  bool worker_visible() const {
    if (in_active_job_scope_) {
      return active_job_state_.visible;
    }
    return worker_state().visible;
  }

  int worker_drawable_width() const {
    if (in_active_job_scope_) {
      return active_job_state_.drawable_width;
    }
    tl_paired_state_ =
        unpack_state(worker_packed_state_.load(std::memory_order_acquire));
    tl_has_paired_height_ = true;
    return tl_paired_state_.drawable_width;
  }

  int worker_drawable_height() const {
    if (in_active_job_scope_) {
      return active_job_state_.drawable_height;
    }
    if (tl_has_paired_height_) {
      tl_has_paired_height_ = false;
      return tl_paired_state_.drawable_height;
    }
    return unpack_state(worker_packed_state_.load(std::memory_order_acquire))
        .drawable_height;
  }

  const SdlWindowState &current_state() const { return current_state_; }

  std::thread::id owner_thread_id() const { return owner_thread_id_; }

private:
  SDL_Window *window_{};
  std::thread::id owner_thread_id_{};
  SdlWindowState current_state_{};
  std::atomic<uint64_t> worker_packed_state_{0};

  static inline thread_local SdlWindowState active_job_state_{};
  static inline thread_local bool in_active_job_scope_{false};
  static inline thread_local SdlWindowState tl_paired_state_{};
  static inline thread_local bool tl_has_paired_height_{false};
};

class SdlWindowStateScope {
public:
  SdlWindowStateScope(SdlWindowStateTracker *tracker,
                      const SdlWindowState &state)
      : tracker_(tracker) {
    if (tracker_)
      tracker_->enter_job_scope(state);
  }
  ~SdlWindowStateScope() {
    if (tracker_)
      tracker_->leave_job_scope();
  }
  SdlWindowStateScope(const SdlWindowStateScope &) = delete;
  SdlWindowStateScope &operator=(const SdlWindowStateScope &) = delete;

private:
  SdlWindowStateTracker *tracker_{};
};

inline VkSurfaceKHR create_surface(SDL_Window *window, VkInstance instance) {
  if (!window)
    throw std::runtime_error("Cannot create Vulkan surface: window is null");
  if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_VULKAN))
    throw std::runtime_error(
        "SDL Vulkan surface: window requires SDL_WINDOW_VULKAN");
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface) ||
      surface == VK_NULL_HANDLE) {
    throw std::runtime_error(std::string("SDL Vulkan surface: ") +
                             SDL_GetError());
  }
  return surface;
}

inline void destroy_surface(VkInstance instance,
                            VkSurfaceKHR surface) noexcept {
  if (instance != VK_NULL_HANDLE && surface != VK_NULL_HANDLE) {
    SDL_Vulkan_DestroySurface(instance, surface, nullptr);
  }
}

inline std::vector<const char *> get_instance_extensions() {
  Uint32 count = 0;
  const auto *extensions = SDL_Vulkan_GetInstanceExtensions(&count);
  if (!extensions) {
    throw std::runtime_error(std::string("SDL Vulkan extensions: ") +
                             SDL_GetError());
  }
  return std::vector<const char *>(extensions, extensions + count);
}

} // namespace cc_mw3_re::platform
