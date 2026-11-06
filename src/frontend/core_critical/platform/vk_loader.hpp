#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan_core.h>

#include "wsi_win32_dxgi.hpp"

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
        get_proc_(other.get_proc_)
#ifdef _WIN32
        ,
        win32_module_(other.win32_module_)
#endif
  {
    other.loaded_ = false;
    other.sdl_loaded_ = false;
    other.get_proc_ = nullptr;
#ifdef _WIN32
    other.win32_module_ = nullptr;
#endif
  }

  VulkanLoader &operator=(VulkanLoader &&other) noexcept {
    if (this != &other) {
      unload();
      loaded_ = other.loaded_;
      sdl_loaded_ = other.sdl_loaded_;
      get_proc_ = other.get_proc_;
#ifdef _WIN32
      win32_module_ = other.win32_module_;
      other.win32_module_ = nullptr;
#endif
      other.loaded_ = false;
      other.sdl_loaded_ = false;
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

#ifdef _WIN32
    win32_module_ = load_vulkan_library();
    if (win32_module_) {
      get_proc_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
          get_proc_address(win32_module_, "vkGetInstanceProcAddr"));
      if (get_proc_) {
        loaded_ = true;
        return true;
      }
      free_vulkan_library(win32_module_);
      win32_module_ = nullptr;
    }
#endif
    return false;
  }

  void unload() noexcept {
    if (!loaded_)
      return;
#ifdef _WIN32
    if (win32_module_) {
      free_vulkan_library(win32_module_);
      win32_module_ = nullptr;
    }
#endif
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
  PFN_vkGetInstanceProcAddr get_proc_{nullptr};
#ifdef _WIN32
  HMODULE win32_module_{nullptr};
#endif
};

} // namespace cc_mw3_re::platform
