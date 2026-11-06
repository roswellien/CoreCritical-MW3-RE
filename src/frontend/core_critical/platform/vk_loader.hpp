#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan_core.h>

#include <stdexcept>

namespace cc_mw3_re::platform {

class VulkanLoader {
public:
  VulkanLoader() = default;
  ~VulkanLoader() { unload(); }

  VulkanLoader(const VulkanLoader &) = delete;
  VulkanLoader &operator=(const VulkanLoader &) = delete;

  VulkanLoader(VulkanLoader &&other) noexcept
      : loaded_(other.loaded_), sdl_loaded_(other.sdl_loaded_),
        shared_object_(other.shared_object_), get_proc_(other.get_proc_) {
    other.loaded_ = false;
    other.sdl_loaded_ = false;
    other.shared_object_ = nullptr;
    other.get_proc_ = nullptr;
  }

  VulkanLoader &operator=(VulkanLoader &&other) noexcept {
    if (this != &other) {
      unload();
      loaded_ = other.loaded_;
      sdl_loaded_ = other.sdl_loaded_;
      shared_object_ = other.shared_object_;
      get_proc_ = other.get_proc_;
      other.loaded_ = false;
      other.sdl_loaded_ = false;
      other.shared_object_ = nullptr;
      other.get_proc_ = nullptr;
    }
    return *this;
  }

  bool load() {
    if (loaded_)
      return true;

    if (SDL_Vulkan_LoadLibrary(nullptr)) {
      auto proc = SDL_Vulkan_GetVkGetInstanceProcAddr();
      if (proc) {
        get_proc_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(proc);
        sdl_loaded_ = true;
        loaded_ = true;
        return true;
      }
      SDL_Vulkan_UnloadLibrary();
    }

#if defined(_WIN32)
    const char *fallback_name = "vulkan-1.dll";
#elif defined(__APPLE__)
    const char *fallback_name = "libvulkan.dylib";
#else
    const char *fallback_name = "libvulkan.so.1";
#endif
    shared_object_ = SDL_LoadObject(fallback_name);
    if (shared_object_) {
      auto proc = SDL_LoadFunction(shared_object_, "vkGetInstanceProcAddr");
      if (proc) {
        get_proc_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(proc);
        loaded_ = true;
        return true;
      }
      SDL_UnloadObject(shared_object_);
      shared_object_ = nullptr;
    }
    return false;
  }

  void unload() noexcept {
    if (!loaded_)
      return;
    if (shared_object_) {
      SDL_UnloadObject(shared_object_);
      shared_object_ = nullptr;
    }
    if (sdl_loaded_) {
      SDL_Vulkan_UnloadLibrary();
      sdl_loaded_ = false;
    }
    loaded_ = false;
    get_proc_ = nullptr;
  }

  bool is_loaded() const noexcept { return loaded_; }

  PFN_vkGetInstanceProcAddr get_instance_proc_addr() const noexcept {
    return get_proc_;
  }

private:
  bool loaded_{false};
  bool sdl_loaded_{false};
  SDL_SharedObject *shared_object_{nullptr};
  PFN_vkGetInstanceProcAddr get_proc_{nullptr};
};

} // namespace cc_mw3_re::platform
