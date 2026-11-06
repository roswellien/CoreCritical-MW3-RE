#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan_core.h>

#include <atomic>
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

class SdlWindowStateTracker {
public:
  explicit SdlWindowStateTracker(SDL_Window *window)
      : window_(window), owner_thread_id_(std::this_thread::get_id()) {
    sample_owner();
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
    set_worker_state(current_state_);
  }

  SdlWindowState sample_and_get() {
    sample_owner();
    return current_state_;
  }

  bool is_owner_thread() const {
    return std::this_thread::get_id() == owner_thread_id_;
  }

  void set_worker_state(const SdlWindowState &state) {
    worker_visible_.store(state.visible, std::memory_order_release);
    worker_drawable_width_.store(state.drawable_width,
                                 std::memory_order_release);
    worker_drawable_height_.store(state.drawable_height,
                                  std::memory_order_release);
  }

  bool worker_visible() const {
    return worker_visible_.load(std::memory_order_acquire);
  }

  int worker_drawable_width() const {
    return worker_drawable_width_.load(std::memory_order_acquire);
  }

  int worker_drawable_height() const {
    return worker_drawable_height_.load(std::memory_order_acquire);
  }

  const SdlWindowState &current_state() const { return current_state_; }

  std::thread::id owner_thread_id() const { return owner_thread_id_; }

private:
  SDL_Window *window_{};
  std::thread::id owner_thread_id_{};
  SdlWindowState current_state_{};
  std::atomic<bool> worker_visible_{false};
  std::atomic<int> worker_drawable_width_{0};
  std::atomic<int> worker_drawable_height_{0};
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
