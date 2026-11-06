#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan_core.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace cc_mw3_re::platform {

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
