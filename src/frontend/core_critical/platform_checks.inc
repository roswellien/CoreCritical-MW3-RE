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

  PlatformExclusiveFullscreenChain chain;
  chain.setup(nullptr);
  if (chain.pNext() == nullptr) {
    throw std::runtime_error("Platform check failed: exclusive fullscreen "
                             "chain returned null pNext");
  }

  // 4. Platform surface extension query contract
#if defined(_WIN32)
  const char *plat_ext = get_platform_surface_extension_name();
  if (!plat_ext || std::string_view(plat_ext) != "VK_KHR_win32_surface") {
    throw std::runtime_error(
        "Platform check failed: expected VK_KHR_win32_surface on Windows");
  }
#endif
}

} // namespace cc_mw3_re::platform
