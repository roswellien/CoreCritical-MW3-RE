#pragma once

#include "platform/vk_loader.hpp"
#include "platform/wsi_surface.hpp"
#include "platform/wsi_win32_dxgi.hpp"
#include "renderer/launcher_display.hpp"

#include <stdexcept>
#include <string_view>

namespace cc_mw3_re::platform {

void check_portable_contract();

inline void run_platform_checks() {
  // 1. Portable display & coordinate contracts without Win32 types
  check_portable_contract();

  // 2. VulkanLoader verification
  VulkanLoader loader;
  if (!loader.load()) {
    throw std::runtime_error(
        "Platform check failed: VulkanLoader could not load Vulkan library");
  }
  if (!loader.get_instance_proc_addr()) {
    throw std::runtime_error("Platform check failed: VulkanLoader returned "
                             "null vkGetInstanceProcAddr");
  }
  loader.unload();

  // 3. Platform window & fullscreen abstraction contract checks
  if (is_window_foreground_root(nullptr)) {
    throw std::runtime_error(
        "Platform check failed: null window cannot be foreground root");
  }

#if defined(_WIN32)
  ExclusiveFullscreenHandler handler;
  handler.setup_swapchain_chain(nullptr);
  if (handler.swapchain_pnext() == nullptr) {
    throw std::runtime_error("Platform check failed: exclusive fullscreen "
                             "chain returned null pNext");
  }
#endif

  // 4. Platform surface extension query contract
#if defined(_WIN32)
  const char *plat_ext = get_platform_surface_extension_name();
  if (!plat_ext || std::string_view(plat_ext) != "VK_KHR_win32_surface") {
    throw std::runtime_error(
        "Platform check failed: expected VK_KHR_win32_surface on Windows");
  }
#endif

  // 5. Portable SDL native window handle contract (must preserve SDL window
  // identity without obtaining HWND)
  SDL_Window *test_win = SDL_CreateWindow(
      "ContractCheck", 320, 240, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
  if (test_win) {
    NativeWindowHandle handle = get_window_native_handle(test_win);
    if (handle != static_cast<NativeWindowHandle>(test_win)) {
      SDL_DestroyWindow(test_win);
      throw std::runtime_error(
          "Platform check failed: get_window_native_handle must preserve SDL "
          "window identity");
    }
#if defined(_WIN32)
    HWND hwnd = get_window_hwnd(test_win);
    if (handle == static_cast<NativeWindowHandle>(hwnd) && hwnd != nullptr) {
      SDL_DestroyWindow(test_win);
      throw std::runtime_error(
          "Platform check failed: get_window_native_handle must not return "
          "Win32 HWND");
    }
#endif
    int px_w = 0, px_h = 0;
    SDL_GetWindowSizeInPixels(test_win, &px_w, &px_h);
    if (px_w <= 0 || px_h <= 0) {
      SDL_DestroyWindow(test_win);
      throw std::runtime_error(
          "Platform check failed: invalid SDL drawable pixel extent");
    }

    // 6. SDL window state thread-ownership contract
    SdlWindowStateTracker tracker(test_win);
    tracker.sample_owner();
    bool foreign_query_rejected = false;
    std::thread worker([&] {
      try {
        tracker.sample_owner();
      } catch (const std::runtime_error &) {
        foreign_query_rejected = true;
      }
    });
    worker.join();
    if (!foreign_query_rejected) {
      SDL_DestroyWindow(test_win);
      throw std::runtime_error(
          "Platform check failed: foreign thread must not query SDL window "
          "state");
    }
    SDL_DestroyWindow(test_win);
  }
}

} // namespace cc_mw3_re::platform
